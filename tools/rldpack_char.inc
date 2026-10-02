// rldpack_char.inc - rldpack make-char: a character container (.rldchar) from a PLY or OBJ model
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
// THE CHAIN, one step after the other. The values chunk CPRM is not written
// yet; CICN only with --icon - both chunks are optional:
//
//    1  read the model: PLY (ascii 1.0 or binary_little_endian 1.0) or OBJ with
//       its MTL materials and textures, told apart by the content
//       (tools/rldpack_import.inc, tools/rldpack_obj.inc)
//    2  faces -> triangles: quads split at the shorter diagonal, faces of 5 to
//       1024 corners by ear clipping (degenerate ones refused)
//    3  weld the corners to positions (1e-5); the color stays per corner
//    3r --repair auto (default): weld split corners, drop degenerate and doubled
//       triangles, turn faces outward, close small holes (tools/rldpack_repair.inc);
//       open parts are drawn from both sides (--open-parts)
//    3a --fit crash (default): the model to Crash's size - its kart 112.4 long
//       (or the whole model, without a kart), at most 142.5 tall in game units;
//       silent passes of steps 2 to 5 measure it, then the chain runs again
//       with the factor as --scale
//    4  parts: kart (required), driver, steering wheel
//    4d --fit crash: the model placed onto the reference dummy - its kart on
//       Crash's kart (tools/rldpack_dummy.inc); --size and the poses turn
//       about the dummy's seat
//    4m --remesh on (on request only): each part group of more than 24
//       triangles becomes a closed hull (tools/rldpack_remesh.inc), which 4r
//       then reduces
//    4r --reduce auto (default): more than RLDMK_REDUCE_LIMIT triangles (the
//       draw memory of a character, include/rldchar.inc) are reduced to at most
//       RLDMK_REDUCE_TARGET - one budget shared by kart, driver and steering
//       wheel, each with a floor (tools/rldpack_reduce.inc); at or below the
//       limit nothing is reduced
//    4a --size: driver and steering wheel scaled about the pivot of the poses,
//       the kart not; the size checks of step 4 and 5 run on the result
//    5  axes and scale -> game units: +Y up, +Z forward, 64 per Blender unit
//    6  draw memory per frame: 28 bytes per untextured triangle
//    7  colors -> a palette of 64 (or 128)
//    8  the 47 frames 21/7/15/4: still, or the automatic poses
//    9  quantization: one byte per axis, the up axis on even values
//   10  command list: strips and fans, the vertex cache, cull bits per triangle
//   11  raw frames, 12 model and header, 13 pointer map -> the CMDL chunk
//   14  --icon: a PNG (include/rldpng.inc) -> the portrait CICN, 44 x 26, 16 colors
//   15  the game's own check (RldChar_CheckModel), read back by the renderer's
//       rule, the cull side replayed per triangle, built twice
//   16  CHRI + CMDL (+ CICN, CMSK, CVOI) -> .rldchar through the shared envelope
//
// --voices packs a folder of sound files as the voices CVOI (RldMk_PackVoices).
// BESIDE THE CHAIN, for Reload Studio: --voice-preview writes every sound as
// the game plays it, --preview writes the built model as the game
// draws it, --icon-preview the portrait before and after as BMP. --diagnose
// writes the model as every step saw it (RLDDIAG 1, RldMk_Diagnosis), with the
// limits of draw memory and file size lifted - and then no character at all.
//
// DETERMINISM. The same input gives the same bytes: no time, no random numbers,
// every sort has a complete tie-break, and floating point only enters through
// fixed roundings (floor(x + 0.5)). Floor, square root, sine and cosine are
// written out below instead of taken from the C library, so that two runtimes
// cannot round differently.

#include "rldpack_reduce.inc"
#include "rldpack_remesh.inc"
#include "rldpack_repair.inc"
#include "rldpack_dummy.inc"

//========================================================================================
// CONSTANTS
//========================================================================================

#define RLDMK_NONE 0xFFFFFFFFu

#define RLDMK_PLY_MAX 1000000u    // vertices and faces each (ply-big): a guard, not a format limit
#define RLDMK_PLY_LIST_MAX 1024u  // entries of one list the reader accepts at all
#define RLDMK_TEMPLATE_COUNT 15   // 0..14, include/namespace_Vehicle.h:39-53; 15 (N. Oxide) is never one
#define RLDMK_CLASS_COUNT 4       // enum EngineClass, include/namespace_Vehicle.h:563-578
#define RLDMK_FRAMES_TOTAL 47     // 21 + 7 + 15 + 4 (s_rldCharFrames)
#define RLDMK_NEUTRAL_FRAME 10    // turn frame 10: no steering (game/Vehicle/VehFrame.c:36-38, n >> 1)
#define RLDMK_UNITS 64.0          // game units per Blender unit: the retail karts are 112 x 56 units, 1.8 x 0.9 m
#define RLDMK_HI_TRIANGLES_MAX 564u  // the most triangles of an original driver (N. Oxide, HI model)
#define RLDMK_GT3_BYTES 40u       // one textured triangle in draw memory, RB:3088-3090

// THE OWN MASK (--mask-model, chunk CMSK: include/rldchar.inc). Measured once
// on the retail mask akumouth (model 0x39, frame 0) - only the numbers are
// here: x -35.1..33.2, y -30.7..55.4, z -14.7..9.7 in model units, so 86.1
// tall about the center (-0.95, 12.35, -2.5); 49 textured triangles = 1960
// bytes of draw memory per frame. An own mask is fitted to that height
// (--mask-size scales it) and that center; it may draw 400 triangles with
// vertex colors (RLDCHAR_MASK_DRAW_BYTES_MAX), which the game reserves on top
// of the frame's draw memory like the model's.
#define RLDMK_MASK_HEIGHT 86.1
#define RLDMK_MASK_CENTER {-0.95, 12.35, -2.5}
#define RLDMK_MASK_TRIANGLES_MAX (RLDCHAR_MASK_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3)   // 400
#define RLDMK_MASK_CMDL_MAX (RLDCHAR_MASK_BYTES_MAX - RLDCHAR_MASK_HEAD_BYTES)           // the model frame in CMSK
#define RLDMK_MASK_SIZE_MIN 50u
#define RLDMK_MASK_SIZE_MAX 150u
#define RLDMK_MASK_ORBIT 64.0     // the mask circles the driver at this radius (game/231/RB_MaskShieldCloud.c)
#define RLDMK_MASK_MODEL_NAME_TEXT "ownmask"
#define RLDMK_MASK_HEADER_NAME_TEXT "ownmask_hi"

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

// With --fit none the automatic poses turn the driver about the hip: 0.25
// Blender units above the ground, on the center line (16 game units). With
// --fit crash they turn about the seat of the reference kart
// (tools/rldpack_dummy.inc; --wheels off: the bottom of the driver).
#define RLDMK_HIP_Y 16.0

// Kart detection (ply-no-kart) and the plausibility of the size (ply-axes), in
// Blender units: a retail kart is 112 x 56 units with its bottom at 6, so
// 1.75 x 0.875 m, 0.09 above the ground.
#define RLDMK_KART_BOTTOM_MIN (-0.15)
#define RLDMK_KART_BOTTOM_MAX 0.3
#define RLDMK_KART_LENGTH_MIN 1.25
#define RLDMK_KART_LENGTH_MAX 2.5
#define RLDMK_DRIVER_LENGTH 1.8

// --wheels off: the model's own vehicle may be shorter than a kart
// (ply-kart-short, RldMk_ShortVehicle), but it is still a vehicle: at least
// RLDMK_VEHICLE_LENGTH_MIN long, longer than it is tall, and not the whole
// model - another part rides on it. The longest parts at the bottom of five
// test models, fitted to Crash's size (length x height): three karts
// 1.76 x 0.72, a motorbike 1.29 x 0.80, a short vehicle of its own
// 1.16 x 0.66; the motorbike's wheels 0.25 and 0.27 long. A figure standing
// on its feet is none: a separate shoe stays under the length (0.28 comes to
// about 0.35 when the fit caps a figure 1.8 tall at the tallest driver,
// x1.24), and a figure of one piece, or legs of one piece, are taller than
// long.
#define RLDMK_VEHICLE_LENGTH_MIN 0.6
#define RLDMK_WHEEL_EXTENT 0.6    // the steering wheel: a small part in front of the driver

// The retail kart in game units, for the size reference of Reload Studio
// (@value retail-kart): 112 long, 56 wide, its bottom 6 above the ground,
// taken as centered on x = 0 and z = 0. The height goes up to the seat line
// (hip y 16, RLDMK_HIP_Y); the retail kart's own height is not measured.
#define RLDMK_RETAIL_KART_LENGTH 112
#define RLDMK_RETAIL_KART_WIDTH 56
#define RLDMK_RETAIL_KART_BOTTOM 6
#define RLDMK_RETAIL_KART_TOP 16   // = RLDMK_HIP_Y

// CRASH AS THE STANDARD SIZE (--fit crash). Measured once on the retail driver
// models - only the numbers are here: the racer model HI of each character in
// BIGFILE, animation 0, frame 10 (the birth pose), decoded like RB:2443-2473
// and its delta frames like RB:2588-2637, in model units ((pos + byte) x
// scale / 4096, without the instance scale 0.8 the game draws a driver with).
//   Crash (character 0): x -33.8..34.3, y 5.6..88.3, z -54.1..58.3, so 112.4
//   long, 68.1 wide, 82.7 tall (y up, z forward).
//   Lengths: 14 of the 16 HI models are 111.9..112.4 long; Coco 120.8,
//   N. Oxide 243.2 (a spaceship).
//   Heights: the 15 drivers without N. Oxide are 53.6 (Penta Penguin) to
//   142.5 (Papu Papu) tall, their bottom 0.9..5.9 above the ground
//   (N. Oxide 164.0 tall, bottom 0.0).
// The length is that of a driver WITH his kart. A model is scaled so that its
// kart is as long as Crash's (the game draws the wheels at fixed points of a
// retail kart); the tallest of the 15 caps the height, also for a model
// without a vehicle (RldMk_StepFit).
#define RLDMK_CRASH_LENGTH 112.4
#define RLDMK_CRASH_HEIGHT 82.7
#define RLDMK_CRASH_BOX {-33.8, 5.6, -54.1, 34.3, 88.3, 58.3}   // x0 y0 z0 x1 y1 z1 (@value crash-box)
#define RLDMK_RETAIL_HEIGHT_MAX 142.5
#define RLDMK_FIT_MIN 0.0001   // the factor stays in the range --scale takes
#define RLDMK_FIT_MAX 1000.0

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
static int s_rldMkWarnings;   // for the self-test: a code said as a warning or as a note
static char s_rldMkCodes[2048];

static void RldMk_ResetSaid(void)
{
	s_rldMkErrors = 0;
	s_rldMkWarnings = 0;
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
	else if (strcmp(severity, "warning") == 0)
	{
		s_rldMkWarnings++;
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

// Is `code` in the list of said codes from p on?
static int RldMk_SaidFrom(const char *p, const char *code)
{
	const size_t length = strlen(code);

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

// Was this code said since the last RldMk_ResetSaid? For the self-test.
static int RldMk_Said(const char *code)
{
	return RldMk_SaidFrom(s_rldMkCodes, code);
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

// @file <kind> <state> <name> <bytes> - not while the self-test runs quietly.
static void RldMk_EmitFile(const char *kind, const char *state, const char *name, size_t bytes)
{
	char number[24];

	if (!s_machine || s_rldMkQuiet)
	{
		return;
	}

	snprintf(number, sizeof(number), "%llu", (unsigned long long)bytes);
	Rld_Emit("file", kind, state, name, number, (const char *)NULL);
}

// fopen(path, "wb") failed with errno `openError`: when that is ENOENT, a
// folder on the way does not exist - said with `code` (the folder named), and
// 1 is returned. Otherwise 0: the caller says its usual text.
static int RldMk_SayMissingFolder(const char *severity, const char *code, const char *path, const char *what, int openError)
{
	char folder[RLD_PATH_MAX];
	size_t length = strlen(path);

	if (openError != ENOENT)
	{
		return 0;
	}

	while ((length > 0u) && (path[length - 1u] != '/') && (path[length - 1u] != '\\'))
	{
		length--;
	}
	if (length > 1u)
	{
		length--;  // without the separator, but "/" stays "/"
	}
	if (length >= sizeof(folder))
	{
		length = sizeof(folder) - 1u;
	}
	memcpy(folder, path, length);
	folder[length] = '\0';

	RldMk_Say(severity, code, path, "%s %s cannot be written: the folder %s does not exist. Create it, or choose another place to save.", what, path,
	          (folder[0] != '\0') ? folder : ".");
	return 1;
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
	u32 *moreStart;    // per face of 5 or more corners: where all its corners start in more[]
	u32 *more;         // the corners of those faces, in file order
	u32 moreCount;
	u32 moreCapacity;
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
	free(ply->moreStart);
	free(ply->more);
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

// One corner of a face of 5 or more corners onto more[]. 0 = out of memory.
static int RldMk_PlyMore(struct RldMkPly *ply, u32 vertex)
{
	if (ply->moreCount == ply->moreCapacity)
	{
		const u32 capacity = (ply->moreCapacity != 0u) ? (2u * ply->moreCapacity) : 1024u;
		u32 *more = (u32 *)realloc(ply->more, (size_t)capacity * sizeof(u32));

		if ((more == NULL) || (capacity <= ply->moreCapacity))
		{
			return 0;
		}
		ply->more = more;
		ply->moreCapacity = capacity;
	}

	ply->more[ply->moreCount++] = vertex;
	return 1;
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
	ply->moreStart = (u32 *)calloc((size_t)ply->faceCount + 1u, sizeof(u32));
	if ((ply->xyz == NULL) || (ply->rgb == NULL) || (ply->corners == NULL) || (ply->cornerCount == NULL) || (ply->moreStart == NULL))
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
						ply->moreStart[i] = ply->moreCount;
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
							if ((count > 4u) && !RldMk_PlyMore(ply, (u32)value))
							{
								RldMk_Say("error", "memory", NULL, "rldpack ran out of memory while reading the model.");
								RldMk_FreePly(ply);
								return 0;
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

// The model file in front of the chain: which format, PLY or OBJ
// (RldMk_LoadModel).
#include "rldpack_import.inc"

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
	double scale;     // --scale (only with --fit none)
	int fit;          // --fit crash: scaled to Crash's size first (RldMk_StepFit); 0 = --fit none
	int reduce;       // --reduce auto: too many triangles are reduced (RldMk_StepReduce); 0 = --reduce off
	int noWheels;     // --wheels off: CHRI flag RLDCHAR_FLAG_NO_WHEELS, and --fit measures the whole model
	u32 mask;         // --mask: RLDCHAR_MASK_LIKE (default), _AKU or _UKA, CHRI flags bits 1-2
	int hasMapColor;  // --map-color: CHRI flag RLDCHAR_FLAG_MAP_COLOR and mapColor
	u32 mapColor;     // 0x00BBGGRR
	u32 reduceTarget; // set by RldMk_BuildModel: at most this many triangles after a reduction
	int reduceForce;  // set by RldMk_BuildModel: reduce even at RLDMK_REDUCE_LIMIT or below
	const char *reduceWhy;  // set by RldMk_BuildModel with reduceForce: why, for model-reduced
	int twoSided;     // --two-sided: no back face culling
	int still;        // --poses still
	u32 sizePercent;  // --size: driver and steering wheel, 100 (or 0) = as exported
	int repair;       // --repair auto: RldMk_StepRepair; 0 = --repair off
	int openOneSided; // --open-parts one-sided: open parts keep the cull bit; 0 = two-sided
	int remesh;       // --remesh on: closed hulls per group first (RldMk_StepRemesh)
	u32 remeshResolution;  // --remesh-resolution, cells along a group's longest side; 0 = 64
	int diagnose;     // --diagnose: the last build keeps what RldMk_Diagnosis writes, the limits
	                  // of draw memory and file size lifted; such a build is never a character
	int maskModel;    // the own mask (--mask-model): the profile of RldMk_BuildMaskOnce, sizePercent
	                  // is --mask-size then
};

struct RldMkPart
{
	u32 triangles;
	double lo[3];
	double hi[3];
	int group;
};

// --diagnose: one value step 9 cut off (RldMk_StepQuantize).
struct RldMkClamp
{
	u32 frame;
	u32 position;              // the position now
	u32 axis;                  // 0 x, 1 y, 2 z
	double value;              // the value before rounding and cutting, in record steps
};

struct RldMkBuild
{
	// the input as read
	u32 vertexCount;
	u32 faceCount;
	u32 quads;
	u32 degenerate;            // triangles without area (after splitting quads and larger faces)
	u32 degenerateFaces;       // faces of fewer than three corners

	// --fit crash (RldMk_StepFit), game units
	int fitted;                // the factor was found and applied
	double fitFactor;          // 4 significant digits, applied as --scale
	int fitBasis;              // RLDMK_FIT_KART or RLDMK_FIT_MODEL: what was brought to Crash's length
	int fitCapped;             // the height of the tallest retail driver decided instead
	double fitBefore[2];       // length (z) and height (y) of the whole model at scale 1
	double fitAfter[2];        // the same after the factor
	double fitKart[2];         // RLDMK_FIT_KART: the kart's length at scale 1 and after

	// welded positions (game units, neutral pose); only those a triangle uses
	u32 positionCount;
	double *position;          // 3 per position
	u32 *vertexPosition;       // per PLY vertex, RLDMK_NONE when no triangle uses it
	u8 *vertexRgb;             // NULL: the colors of the PLY; after a reduction 3 per corner vertex
	                           // (the corners are vertices of their own then, RldMk_StepReduce)

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
	double exported[4];        // the same before --size (ply-axes)
	double center;             // ply-center: the kart's middle to the side of x = 0, model units as exported
	int recordLimit;           // RldMk_StepWrite failed model-frame or mem-model (too many records)
	u32 reduceFrom;            // triangles before RldMk_StepReduce
	u32 reduceTo;              // and after; the same when nothing was reduced
	int reduced;               // RldMk_StepReduce reduced the model
	u32 sizeLow;               // --size: the percentages this model takes, 0 0 = none
	u32 sizeHigh;

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
	int maskModel;             // built as the own mask (RldMk_BuildMaskOnce): one animation of one frame
	u32 framesWritten;         // frames in the written model: RLDMK_FRAMES_TOTAL, 1 for the mask
	u8 *cmdl;
	size_t cmdlSize;
	u32 frameSize;
	u32 drawBytes;
	u8 *bothSides;             // per triangle: 1 = drawn without the cull bit (RldMk_StepFlips); NULL = none

	// --fit crash (RldMk_StepDummy): the model on the reference dummy
	int kartSourceValid;       // --remesh: the kart part and the kart group before the hulls
	double kartSource[6];      // x0 y0 z0 x1 y1 z1
	double kartGroupSource[6];
	int dummy;                 // placed: --size, the poses and the size range use dumFit
	struct RldDumFitResult dumFit;     // at the model's --size
	struct RldDumFitResult dumFit100;  // the same at 100 % (the size range, RldMk_SizeHull)

	// --repair auto (RldMk_StepRepair)
	int repaired;              // the repair ran
	u32 repair[10];            // welded, degenerate, duplicate, flipped, holesClosed, holeTriangles,
	                           // holesLeft, openEdgesBefore, openEdgesAfter, cracksSplit
	u8 *triangleOpen;          // per triangle: 1 = of a part still open after the repair; NULL = none
	u32 openTwoSided;          // drawn triangles without the cull bit because their part is open
	u32 quantFlips;            // drawn triangles without the cull bit because the packing turns them over

	// --remesh on (RldMk_StepRemesh): the source the hulls were made from,
	// kept for the colors after the reduction (RldRem_Recolor)
	int remeshed;
	u32 remeshFrom;            // triangles before
	u32 remeshHull;            // after the hulls (with the groups kept as they were)
	u32 remeshOpenBefore;      // open edges before
	u32 remeshSource[RLDMK_GROUPS];  // triangles per group before; the reduction shares by these
	u32 remeshResolution;
	u32 remPositionCount;
	double *remPosition;
	u32 remTriangleCount;      // only the groups that were remeshed
	u32 *remTriangle;
	u32 *remColor;
	u8 *remGroup;
	u32 bothSidesCount;

	// --diagnose (RldMk_Diagnosis): the model as the steps saw it. "diag"
	// numbers are those of the positions after welding and fitting.
	int diagnose;              // limits lifted, the fields below kept
	u32 *diagFace;             // per triangle: its PLY face (steps 2 to 4)
	u32 diagFaceNow;           // the face RldMk_AddTriangle is splitting
	u32 *diagPlyPosition;      // per PLY vertex: its diag position, RLDMK_NONE when dropped
	u32 diagPositionCount;     // after welding and fitting, before the reduction and --size
	double *diagPosition;      // 3 per diag position, game units
	u8 *diagPositionGroup;
	u32 diagTriangleCount;     // before the reduction
	u32 *diagTriangle;         // 4 per triangle: diag positions a b c, PLY face
	u32 *diagTriangleRgb;      // 3 per triangle, 0xRRGGBB as the PLY has it
	u32 diagReducedCount;      // after the reduction (the same triangles without one)
	u32 *diagReduced;          // 7 per triangle: diag positions a b c, source triangle, source corners (3 x t + k)
	u32 *diagPositionFrom;     // per position now: its diag position
	u32 *triangleReduced;      // per triangle now: its index among the reduced ones
	u32 diagClampCount;        // every value step 9 cut off at 0 or 255 (254)
	u32 diagClampRoom;
	struct RldMkClamp *diagClamp;
};

static void RldMk_FreeBuild(struct RldMkBuild *b)
{
	free(b->bothSides);
	free(b->triangleOpen);
	free(b->remPosition);
	free(b->remTriangle);
	free(b->remColor);
	free(b->remGroup);
	free(b->diagFace);
	free(b->diagPlyPosition);
	free(b->diagPosition);
	free(b->diagPositionGroup);
	free(b->diagTriangle);
	free(b->diagTriangleRgb);
	free(b->diagReduced);
	free(b->diagPositionFrom);
	free(b->triangleReduced);
	free(b->diagClamp);
	free(b->position);
	free(b->vertexPosition);
	free(b->vertexRgb);
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
	if (b->diagFace != NULL)
	{
		b->diagFace[b->triangleCount] = b->diagFaceNow;
	}
	b->triangleCount++;
	return 1;
}

// Is corner k of the ring an ear: it turns the way of the normal, and no other
// corner of the face lies in its triangle (on the edge counts as in; corners
// at the same position as the ear's do not)? *quality is the square of the
// sine of the triangle's smallest angle: (2 x area)^2 over the squares of the
// two longer sides - no square root, no angle function.
static int RldMk_NgonEar(const struct RldMkBuild *b, const u32 *c, const u32 *ring, u32 left, u32 k, const double normal[3], double *quality)
{
	const u32 pa = b->vertexPosition[c[ring[(k + left - 1u) % left]]];
	const u32 pv = b->vertexPosition[c[ring[k]]];
	const u32 pd = b->vertexPosition[c[ring[(k + 1u) % left]]];
	const double *a = &b->position[3u * pa];
	const double *v = &b->position[3u * pv];
	const double *d = &b->position[3u * pd];
	double turn[3];
	double l0;
	double l1;
	double l2;
	u32 w;

	*quality = 0.0;
	RldMk_Cross(a, v, d, turn);
	if (!(RldMk_Dot(turn, normal) > 0.0))
	{
		return 0;
	}

	for (w = 0; w < left; w++)
	{
		const u32 pw = b->vertexPosition[c[ring[w]]];
		const double *p = &b->position[3u * pw];
		double s0[3];
		double s1[3];
		double s2[3];

		if ((pw == pa) || (pw == pv) || (pw == pd))
		{
			continue;
		}
		RldMk_Cross(a, v, p, s0);
		RldMk_Cross(v, d, p, s1);
		RldMk_Cross(d, a, p, s2);
		if ((RldMk_Dot(s0, normal) >= 0.0) && (RldMk_Dot(s1, normal) >= 0.0) && (RldMk_Dot(s2, normal) >= 0.0))
		{
			return 0;
		}
	}

	l0 = RldMk_Distance2(a, v);
	l1 = RldMk_Distance2(v, d);
	l2 = RldMk_Distance2(d, a);
	if ((l0 <= l1) && (l0 <= l2))
	{
		*quality = RldMk_Dot(turn, turn) / (l1 * l2);
	}
	else if (l1 <= l2)
	{
		*quality = RldMk_Dot(turn, turn) / (l0 * l2);
	}
	else
	{
		*quality = RldMk_Dot(turn, turn) / (l0 * l1);
	}
	return 1;
}

// A face of 5 to 1024 corners -> n - 2 triangles by ear clipping in the plane
// of its normal (Newell's, from the welded positions). Each step cuts the best
// ear - the one whose smallest angle is largest, ties in file order - so that
// a round face does not become a fan of slivers. Only the two neighbors of a
// cut ear can change (a simple polygon); should no ear be left, every corner is
// tested again, and then a corner on a straight line between its neighbors is
// dropped without a triangle. Each triangle keeps the order of the face, so it
// winds like it. 0 = no ear and no straight corner: the face is degenerate
// (twisted, or without area).
static int RldMk_SplitNgon(struct RldMkBuild *b, const u32 *c, u32 n)
{
	u32 ring[RLDMK_PLY_LIST_MAX];
	u8 ear[RLDMK_PLY_LIST_MAX];
	double quality[RLDMK_PLY_LIST_MAX];
	double normal[3] = {0.0, 0.0, 0.0};
	u32 left = n;
	u32 i;

	for (i = 0; i < n; i++)
	{
		const double *p = &b->position[3u * b->vertexPosition[c[i]]];
		const double *q = &b->position[3u * b->vertexPosition[c[(i + 1u) % n]]];

		normal[0] += (p[1] - q[1]) * (p[2] + q[2]);
		normal[1] += (p[2] - q[2]) * (p[0] + q[0]);
		normal[2] += (p[0] - q[0]) * (p[1] + q[1]);
		ring[i] = i;
	}
	if (!(RldMk_Dot(normal, normal) > 0.0))
	{
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		ear[i] = (u8)RldMk_NgonEar(b, c, ring, left, i, normal, &quality[i]);
	}

	while (left > 3u)
	{
		u32 found = RLDMK_NONE;
		int pass;
		u32 k;

		for (pass = 0; (pass < 2) && (found == RLDMK_NONE); pass++)
		{
			if (pass == 1)
			{
				for (k = 0; k < left; k++)
				{
					ear[k] = (u8)RldMk_NgonEar(b, c, ring, left, k, normal, &quality[k]);
				}
			}
			for (k = 0; k < left; k++)
			{
				if (ear[k] && ((found == RLDMK_NONE) || (quality[k] > quality[found])))
				{
					found = k;
				}
			}
		}

		if (found != RLDMK_NONE)
		{
			RldMk_AddTriangle(b, c[ring[(found + left - 1u) % left]], c[ring[found]], c[ring[(found + 1u) % left]]);
		}
		else
		{
			// No ear: a corner on a straight line goes without a triangle.
			for (k = 0; (k < left) && (found == RLDMK_NONE); k++)
			{
				const double *a = &b->position[3u * b->vertexPosition[c[ring[(k + left - 1u) % left]]]];
				const double *v = &b->position[3u * b->vertexPosition[c[ring[k]]]];
				const double *d = &b->position[3u * b->vertexPosition[c[ring[(k + 1u) % left]]]];
				double turn[3];

				RldMk_Cross(a, v, d, turn);
				if (RldMk_Dot(turn, turn) <= (1.0e-20 * RldMk_Distance2(a, v) * RldMk_Distance2(a, d)))
				{
					found = k;
				}
			}
			if (found == RLDMK_NONE)
			{
				return 0;
			}
		}

		memmove(&ring[found], &ring[found + 1u], (size_t)(left - found - 1u) * sizeof(u32));
		memmove(&ear[found], &ear[found + 1u], (size_t)(left - found - 1u) * sizeof(u8));
		memmove(&quality[found], &quality[found + 1u], (size_t)(left - found - 1u) * sizeof(double));
		left--;

		// The neighbors of the cut corner.
		{
			const u32 before = (found + left - 1u) % left;
			const u32 after = found % left;

			ear[before] = (u8)RldMk_NgonEar(b, c, ring, left, before, normal, &quality[before]);
			ear[after] = (u8)RldMk_NgonEar(b, c, ring, left, after, normal, &quality[after]);
		}
	}

	RldMk_AddTriangle(b, c[ring[0]], c[ring[1]], c[ring[2]]);
	return 1;
}

static int RldMk_StepTriangles(const struct RldMkPly *ply, struct RldMkBuild *b, u32 keyCount)
{
	u32 ngons = 0;
	u32 split = 0;
	u32 *renumber;
	double *kept;
	size_t room = 0;
	u32 f;
	u32 i;

	for (f = 0; f < ply->faceCount; f++)
	{
		room += (ply->cornerCount[f] >= 3u) ? (size_t)ply->cornerCount[f] - 2u : 0u;
	}
	// More triangles than the reducer takes can never be built - and their
	// arrays below could pass what a 32-bit size counts.
	if (room > (size_t)RLDRED_INPUT_MAX)
	{
		RldMk_Say("error", "ply-big", NULL, "The model has %llu triangles; rldpack reads at most %u. Reduce it in Blender (Decimate modifier).",
		          (unsigned long long)room, (u32)RLDRED_INPUT_MAX);
		return 0;
	}
	b->triangle = (u32 *)malloc((room + 1u) * 3u * sizeof(u32));
	if (b->triangle == NULL)
	{
		return RldMk_OutOfMemory();
	}
	if (b->diagnose)
	{
		b->diagFace = (u32 *)malloc((room + 1u) * sizeof(u32));
		if (b->diagFace == NULL)
		{
			return RldMk_OutOfMemory();
		}
	}

	for (f = 0; f < ply->faceCount; f++)
	{
		const u32 n = ply->cornerCount[f];
		const u32 *c = &ply->corners[4u * f];

		b->diagFaceNow = f;
		if (n < 3u)
		{
			b->degenerateFaces++;
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
		else if (RldMk_SplitNgon(b, &ply->more[ply->moreStart[f]], n))
		{
			split++;
		}
		else
		{
			ngons++;
		}
	}

	if (ngons != 0u)
	{
		RldMk_Say("error", "ply-ngon", NULL,
		          "%u faces of more than four corners cannot be split into triangles (twisted, or without area). Triangulate the mesh before export (Blender: Triangulate modifier).",
		          ngons);
		return 0;
	}

	if (split != 0u)
	{
		RldMk_Say("info", "ply-ngon-split", NULL, "%u faces of more than four corners were split into triangles.", split);
	}

	if (b->quads != 0u)
	{
		if (b->quads == 1u)
		{
			RldMk_Say("info", "ply-quads", NULL, "1 four-sided face was split into two triangles.");
		}
		else
		{
			RldMk_Say("info", "ply-quads", NULL, "%u four-sided faces were split into triangles.", b->quads);
		}
	}

	if ((b->degenerate != 0u) && (b->degenerateFaces != 0u))
	{
		RldMk_Say("warning", "ply-degenerate", NULL,
		          "%u triangles have no area (counted after splitting quads and larger faces) and %u faces have fewer than three corners; they are left out.",
		          b->degenerate, b->degenerateFaces);
	}
	else if (b->degenerate != 0u)
	{
		RldMk_Say("warning", "ply-degenerate", NULL, "%u triangles have no area and are left out (counted after splitting quads and larger faces).",
		          b->degenerate);
	}
	else if (b->degenerateFaces != 0u)
	{
		RldMk_Say("warning", "ply-degenerate", NULL, "%u faces have fewer than three corners and are left out.", b->degenerateFaces);
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
// STEP 3r: --repair auto - A SURFACE THE CULLING CAN DRAW
//========================================================================================
//
// tools/rldpack_repair.inc on the welded and fitted positions, before the parts
// are found (they profit from the closer weld): corners closer than
// RldRep_DefaultWeld become one, triangles without area and exact doubles go,
// cracks of T-junctions are split, every piece is turned outward, holes of at
// most RLDREP_HOLE_MAX_EDGES_DEFAULT edges are closed. No position is made or
// moved by more than the weld distance; the triangles may grow by the fills.
// Each corner keeps the color of the corner it continues (sourceCorner); after
// a change the corners are vertices of their own (vertexRgb), the positions no
// triangle uses any more are dropped. What stays open is marked per triangle
// (triangleOpen): with --open-parts two-sided such a triangle gets no cull bit
// (RldMk_StepFlips) - the inside of an open kart shell is seen through it
// otherwise. A model the repair leaves as it is keeps its bytes.

static int RldMk_StepRepair(const struct RldMkPly *ply, struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const u32 triangles = b->triangleCount;
	RldRepInput in;
	RldRepResult r;
	char why[256];
	u32 *corner;
	u32 *renumber = NULL;
	double *position = NULL;
	u32 *vertexPosition = NULL;
	u8 *vertexRgb = NULL;
	u32 *triangle = NULL;
	u32 *diagFace = NULL;
	u32 *plyPosition = NULL;
	u32 used = 0;
	int same;
	u32 i;

	if (!opt->repair)
	{
		return 1;
	}

	corner = (u32 *)malloc(((size_t)triangles * 3u + 1u) * sizeof(u32));
	if (corner == NULL)
	{
		return RldMk_OutOfMemory();
	}
	for (i = 0; i < (3u * triangles); i++)
	{
		corner[i] = b->vertexPosition[b->triangle[i]];
	}

	in.positionCount = b->positionCount;
	in.position = b->position;
	in.triangleCount = triangles;
	in.triangle = corner;
	in.cornerColor = NULL;
	in.weldDistance = RldRep_DefaultWeld(b->positionCount, b->position);
	in.holeMaxEdges = RLDREP_HOLE_MAX_EDGES_DEFAULT;
	why[0] = '\0';
	if (!RldRep_Repair(&in, &r, why, sizeof(why)))
	{
		free(corner);
		RldMk_Say("error", "model-repair", why, "The model cannot be repaired (%s). Build it with --repair off.", why);
		return 0;
	}

	b->repaired = 1;
	b->repair[0] = r.welded;
	b->repair[1] = r.degenerate;
	b->repair[2] = r.duplicate;
	b->repair[3] = r.flipped;
	b->repair[4] = r.holesClosed;
	b->repair[5] = r.holeTriangles;
	b->repair[6] = r.holesLeft;
	b->repair[7] = r.openEdgesBefore;
	b->repair[8] = r.openEdgesAfter;
	b->repair[9] = r.cracksSplit;

	b->triangleOpen = (u8 *)malloc((size_t)r.triangleCount + 1u);
	if (b->triangleOpen == NULL)
	{
		free(corner);
		RldRep_Free(&r);
		return RldMk_OutOfMemory();
	}
	memcpy(b->triangleOpen, r.open, (size_t)r.triangleCount);

	// Nothing changed: the model keeps its vertices and bytes.
	same = (r.triangleCount == triangles);
	for (i = 0; same && (i < (3u * triangles)); i++)
	{
		same = (r.triangle[i] == corner[i]) && (r.sourceCorner[i] == i);
	}
	free(corner);
	if (same)
	{
		RldRep_Free(&r);
		return 1;
	}

	if (r.triangleCount == 0u)
	{
		RldRep_Free(&r);
		RldMk_Say("error", "ply-no-faces", NULL, "The model has no faces. Export the mesh, not only its points.");
		return 0;
	}

	renumber = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	position = (double *)malloc(((size_t)b->positionCount + 1u) * 3u * sizeof(double));
	vertexPosition = (u32 *)malloc(((size_t)r.triangleCount * 3u + 1u) * sizeof(u32));
	vertexRgb = (u8 *)malloc(((size_t)r.triangleCount * 3u + 1u) * 3u);
	triangle = (u32 *)malloc(((size_t)r.triangleCount * 3u + 1u) * sizeof(u32));
	if (b->diagnose)
	{
		diagFace = (u32 *)malloc(((size_t)r.triangleCount + 1u) * sizeof(u32));
		plyPosition = (u32 *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(u32));
	}
	if ((renumber == NULL) || (position == NULL) || (vertexPosition == NULL) || (vertexRgb == NULL) || (triangle == NULL) ||
	    (b->diagnose && ((diagFace == NULL) || (plyPosition == NULL))))
	{
		free(renumber);
		free(position);
		free(vertexPosition);
		free(vertexRgb);
		free(triangle);
		free(diagFace);
		free(plyPosition);
		RldRep_Free(&r);
		return RldMk_OutOfMemory();
	}

	// Only the positions a triangle uses, in their old order.
	for (i = 0; i < b->positionCount; i++)
	{
		renumber[i] = RLDMK_NONE;
	}
	for (i = 0; i < (3u * r.triangleCount); i++)
	{
		renumber[r.triangle[i]] = 0u;
	}
	for (i = 0; i < b->positionCount; i++)
	{
		if (renumber[i] != RLDMK_NONE)
		{
			renumber[i] = used;
			memcpy(&position[3u * used], &b->position[3u * i], 3u * sizeof(double));
			used++;
		}
	}

	// The corners as vertices of their own: the position from the result, the
	// color of the corner each one continues.
	for (i = 0; i < (3u * r.triangleCount); i++)
	{
		const u32 source = b->triangle[r.sourceCorner[i]];
		const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * source] : &ply->rgb[3u * source];

		vertexPosition[i] = renumber[r.triangle[i]];
		memcpy(&vertexRgb[3u * i], rgb, 3u);
		triangle[i] = i;
	}

	// --diagnose: the PLY face of each triangle (-1 for a fill) and where each
	// PLY vertex went. The three corners of a triangle from the input come from
	// one input triangle (RldRepResult.sourceCorner), a triangle split at a
	// crack too: its corners carry those of the nearer end of the split edge,
	// corners of the triangle it was split from. So the source corner of any
	// corner names the face; only a fill has none.
	if (diagFace != NULL)
	{
		for (i = 0; i < r.triangleCount; i++)
		{
			diagFace[i] = (r.added[i] || (b->diagFace == NULL)) ? RLDMK_NONE : b->diagFace[r.sourceCorner[3u * i] / 3u];
		}
		for (i = 0; i < ply->vertexCount; i++)
		{
			const u32 p = (b->diagPlyPosition != NULL) ? b->diagPlyPosition[i] : b->vertexPosition[i];

			plyPosition[i] = (p != RLDMK_NONE) ? renumber[r.positionMap[p]] : RLDMK_NONE;
		}
		free(b->diagFace);
		free(b->diagPlyPosition);
		b->diagFace = diagFace;
		b->diagPlyPosition = plyPosition;
	}

	free(renumber);
	free(b->position);
	free(b->vertexPosition);
	free(b->vertexRgb);
	free(b->triangle);
	b->position = position;
	b->vertexPosition = vertexPosition;
	b->vertexRgb = vertexRgb;
	b->triangle = triangle;
	b->positionCount = used;
	b->triangleCount = r.triangleCount;
	RldRep_Free(&r);
	RldMk_StepOpenEdges(b);

	RldMk_Say("info", "model-repaired", NULL,
	          "Repaired: %u corners welded, %u triangles without area and %u doubled ones dropped, %u turned outward, %u cracks split, %u holes closed with %u triangles; open edges %u -> %u.",
	          b->repair[0], b->repair[1], b->repair[2], b->repair[3], b->repair[9], b->repair[4], b->repair[5], b->repair[7], b->repair[8]);
	return 1;
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

// --repair off: the open parts by the topology alone, as RldRep_Repair marks
// them (open[]): triangles joined across edges with exactly two triangles are
// one piece; a piece with an edge of one triangle is open, and all its
// triangles are drawn from both sides (--open-parts two-sided). Nothing else
// changes. 0 = out of memory.

struct RldMkOpenEdge
{
	u64 key;   // the two positions, lower first
	u32 triangle;
};

static int RldMk_CompareOpenEdge(const void *x, const void *y)
{
	const struct RldMkOpenEdge *p = (const struct RldMkOpenEdge *)x;
	const struct RldMkOpenEdge *q = (const struct RldMkOpenEdge *)y;

	if (p->key != q->key)
	{
		return (p->key < q->key) ? -1 : 1;
	}
	return (p->triangle < q->triangle) ? -1 : ((p->triangle > q->triangle) ? 1 : 0);
}

static int RldMk_StepOpenParts(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const u32 corners = 3u * b->triangleCount;
	struct RldMkOpenEdge *edges;
	u32 *parent;
	u8 *open;
	u32 i;
	u32 t;

	if (opt->repair || opt->twoSided || opt->openOneSided)
	{
		return 1;
	}

	edges = (struct RldMkOpenEdge *)malloc(((size_t)corners + 1u) * sizeof(*edges));
	parent = (u32 *)malloc(((size_t)b->triangleCount + 1u) * sizeof(u32));
	open = (u8 *)calloc((size_t)b->triangleCount + 1u, 1u);
	b->triangleOpen = (u8 *)calloc((size_t)b->triangleCount + 1u, 1u);
	if ((edges == NULL) || (parent == NULL) || (open == NULL) || (b->triangleOpen == NULL))
	{
		free(edges);
		free(parent);
		free(open);
		return RldMk_OutOfMemory();
	}

	for (t = 0; t < b->triangleCount; t++)
	{
		int k;

		parent[t] = t;
		for (k = 0; k < 3; k++)
		{
			const u32 p = b->vertexPosition[b->triangle[3u * t + (u32)k]];
			const u32 q = b->vertexPosition[b->triangle[3u * t + (u32)((k + 1) % 3)]];

			edges[3u * t + (u32)k].key = (p < q) ? (((u64)p << 32) | q) : (((u64)q << 32) | p);
			edges[3u * t + (u32)k].triangle = t;
		}
	}
	qsort(edges, corners, sizeof(*edges), RldMk_CompareOpenEdge);

	// pieces across edges of exactly two triangles; edges of one mark their triangle
	i = 0;
	while (i < corners)
	{
		u32 run = 1;

		while (((i + run) < corners) && (edges[i + run].key == edges[i].key))
		{
			run++;
		}
		if (run == 2u)
		{
			RldMk_Join(parent, edges[i].triangle, edges[i + 1u].triangle);
		}
		else if (run == 1u)
		{
			open[edges[i].triangle] = 1u;
		}
		i += run;
	}

	// a piece is open when one of its triangles is
	for (t = 0; t < b->triangleCount; t++)
	{
		if (open[t])
		{
			b->triangleOpen[RldMk_Root(parent, t)] = 1u;
		}
	}
	for (t = 0; t < b->triangleCount; t++)
	{
		open[t] = b->triangleOpen[RldMk_Root(parent, t)];
	}
	memcpy(b->triangleOpen, open, (size_t)b->triangleCount);

	free(edges);
	free(parent);
	free(open);
	return 1;
}

// THE VEHICLE LIES UNDER THE DRIVER: is the middle of the driver - the part
// with the most triangles besides `vehicle` (ties: the first) - over the
// vehicle seen from above, x and z each within the vehicle's extent and an
// eighth of it more? A sword beside a figure or a tail behind it is long and
// low like a vehicle but not under the driver. The eighth lets a figure on a
// board reach a little past its end (a test model: 5 % of the board's length
// behind it); the tails and swords measured lie 25 % and more outside. With
// no other part there is no driver: 1.
static int RldMk_UnderDriver(const struct RldMkBuild *b, u32 vehicle)
{
	const struct RldMkPart *v = &b->parts[vehicle];
	const struct RldMkPart *driver;
	int d = -1;
	u32 i;
	int a;

	for (i = 0; i < b->partCount; i++)
	{
		if ((i != vehicle) && ((d < 0) || (b->parts[i].triangles > b->parts[d].triangles)))
		{
			d = (int)i;
		}
	}
	if (d < 0)
	{
		return 1;
	}

	driver = &b->parts[d];
	for (a = 0; a < 3; a += 2)
	{
		const double middle = 0.5 * (driver->lo[a] + driver->hi[a]);
		const double margin = 0.125 * (v->hi[a] - v->lo[a]);

		if ((middle < (v->lo[a] - margin)) || (middle > (v->hi[a] + margin)))
		{
			return 0;
		}
	}
	return 1;
}

// A vehicle shorter than a kart (--wheels off): of a model of two parts or
// more, the longest part with its bottom in the kart's range, a length of
// RLDMK_VEHICLE_LENGTH_MIN up to RLDMK_KART_LENGTH_MAX, longer than it is
// tall and under the driver (RldMk_UnderDriver); ties as for the kart: the
// lower bottom. -1 = none; *aside (may be NULL) the longest part that was all
// of that but not under the driver, -1 = none.
static int RldMk_ShortVehicleAside(const struct RldMkBuild *b, int *aside)
{
	int best = -1;
	int away = -1;
	u32 i;

	if (aside != NULL)
	{
		*aside = -1;
	}

	if (b->partCount < 2u)
	{
		return -1;
	}

	for (i = 0; i < b->partCount; i++)
	{
		const struct RldMkPart *part = &b->parts[i];
		const double bottom = part->lo[1] / RLDMK_UNITS;
		const double partLength = (part->hi[2] - part->lo[2]) / RLDMK_UNITS;

		if ((bottom < RLDMK_KART_BOTTOM_MIN) || (bottom > RLDMK_KART_BOTTOM_MAX) || (partLength < RLDMK_VEHICLE_LENGTH_MIN) ||
		    (partLength > RLDMK_KART_LENGTH_MAX) || ((part->hi[2] - part->lo[2]) <= (part->hi[1] - part->lo[1])))
		{
			continue;
		}

		if (!RldMk_UnderDriver(b, i))
		{
			if ((away < 0) || ((part->hi[2] - part->lo[2]) > (b->parts[away].hi[2] - b->parts[away].lo[2])))
			{
				away = (int)i;
			}
			continue;
		}

		if ((best < 0) || ((part->hi[2] - part->lo[2]) > (b->parts[best].hi[2] - b->parts[best].lo[2])) ||
		    (((part->hi[2] - part->lo[2]) == (b->parts[best].hi[2] - b->parts[best].lo[2])) && (part->lo[1] < b->parts[best].lo[1])))
		{
			best = (int)i;
		}
	}
	if (aside != NULL)
	{
		*aside = away;
	}
	return best;
}

static int RldMk_ShortVehicle(const struct RldMkBuild *b)
{
	return RldMk_ShortVehicleAside(b, NULL);
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
	int aside = -1;

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

		if (b->fitted)
		{
			// Fitted, the model is only this short when it is far taller than long.
			RldMk_Say("error", "model-scale", NULL,
			          "Fitted to Crash size (x%.4g), the model is %.2f model units long but %.2f tall - far taller than it is long. Check the axes (+Y up, +Z forward). On the command line, --fit none builds it at its exported size.",
			          b->fitFactor, length, (hi[1] - lo[1]) / RLDMK_UNITS);
		}
		else
		{
			RldMk_Say("error", "model-scale", NULL,
			          "The model is %.1f model units long - about %.0f times too large (or small) for a driver. Export it in meters (Blender: 1 unit = 1 meter); a driver with kart is about 1.8 units long.",
			          length, factor);
		}
		return 0;
	}

	// THE KART: a part at the bottom, as long as a kart, under the driver
	// (RldMk_UnderDriver). With one part the whole mesh has to be it.
	for (i = 0; i < b->partCount; i++)
	{
		const struct RldMkPart *part = &b->parts[i];
		const double bottom = part->lo[1] / RLDMK_UNITS;
		const double partLength = (part->hi[2] - part->lo[2]) / RLDMK_UNITS;

		if ((bottom < RLDMK_KART_BOTTOM_MIN) || (bottom > RLDMK_KART_BOTTOM_MAX) || (partLength < RLDMK_KART_LENGTH_MIN) || (partLength > RLDMK_KART_LENGTH_MAX))
		{
			continue;
		}

		if (!RldMk_UnderDriver(b, i))
		{
			if ((aside < 0) || ((part->hi[2] - part->lo[2]) > (b->parts[aside].hi[2] - b->parts[aside].lo[2])))
			{
				aside = (int)i;
			}
			continue;
		}

		if ((best < 0) || ((part->hi[2] - part->lo[2]) > (b->parts[best].hi[2] - b->parts[best].lo[2])) ||
		    (((part->hi[2] - part->lo[2]) == (b->parts[best].hi[2] - b->parts[best].lo[2])) && (part->lo[1] < b->parts[best].lo[1])))
		{
			best = (int)i;
		}
	}

	// NO PART A KART LONG: with --wheels off the model brings its own
	// vehicle, which may be shorter (step 3a) - a part at the bottom that is
	// still a vehicle (RldMk_ShortVehicle) is the kart. With the wheels on a
	// kart stays required; the error names such a part, and only then.
	if (best < 0)
	{
		int shortAside = -1;
		const int vehicle = RldMk_ShortVehicleAside(b, &shortAside);
		const double vehicleLength = (vehicle >= 0) ? ((b->parts[vehicle].hi[2] - b->parts[vehicle].lo[2]) / RLDMK_UNITS) : 0.0;
		char also[224];

		if ((vehicle >= 0) && opt->noWheels)
		{
			best = vehicle;
			RldMk_Say("info", "ply-kart-short", NULL, "Kart wheels hidden: the vehicle at the bottom (%.2f model units long) is taken as the kart.", vehicleLength);
		}
		else
		{
			also[0] = '\0';
			if (vehicle >= 0)
			{
				snprintf(also, sizeof(also),
				         " The vehicle at the bottom (%.2f model units long) is shorter than a kart; with the kart wheels hidden it is taken as the kart (--wheels off; Reload Studio: untick Show kart wheels).",
				         vehicleLength);
			}
			else if ((aside >= 0) || (shortAside >= 0))
			{
				const int away = (aside >= 0) ? aside : shortAside;

				snprintf(also, sizeof(also),
				         " A long part at the bottom (%.2f model units long) is not under the driver - beside or behind him - so it is not taken as the kart.",
				         (b->parts[away].hi[2] - b->parts[away].lo[2]) / RLDMK_UNITS);
			}
			if (b->fitted)
			{
				RldMk_Say("error", "ply-no-kart", NULL,
				          "No kart found in the model fitted to Crash size (x%.4g). A character needs a kart: a separate part at the bottom of the model, about as long as Crash's kart.%s On the command line, --fit none checks the model at its exported size.",
				          b->fitFactor, also);
			}
			else
			{
				RldMk_Say("error", "ply-no-kart", NULL,
				          "No kart found. A character needs a kart: a part at the bottom of the model, about 1.8 model units long (Blender: meters).%s", also);
			}
			return 0;
		}
	}

	b->kartPart = best;
	b->parts[best].group = RLDMK_GROUP_KART;

	// THE DRIVER: the largest other part. The steering wheel: small parts in
	// front of it. Everything else moves with the driver, except the parts
	// that belong to the vehicle (next).
	b->driverPart = -1;
	for (i = 0; i < b->partCount; i++)
	{
		if (((int)i != best) && ((b->driverPart < 0) || (b->parts[i].triangles > b->parts[b->driverPart].triangles)))
		{
			b->driverPart = (int)i;
		}
	}

	// MORE OF THE VEHICLE: another part stays still with the kart when it is
	//   - not a steering wheel (small and in front of the driver, below),
	//   - one piece across the middle: it reaches over the kart's middle in x
	//     to both sides,
	//   - over the kart seen from above, and
	//   - reaching down to the top of the kart: a part that rests on it counts
	//     (a tank's hull exported on its chassis starts exactly at the
	//     chassis' top; without it the hull and its turret leaned with the
	//     driver).
	// A vehicle's body, fork or seat lies in the middle; what a driver has in
	// pairs - shoes, hands, legs as parts of their own - lies to one side and
	// moves with him. Measured on four models fitted to Crash's size: the front
	// of a motorbike (x -22.5..22.8, bottom y 0.7) and its seat (x -12.4..12.4,
	// bottom 30.3, below the kart's top at 50.9) stay; Crash's shoes on it
	// (x -16.4..-2.1 and 1.6..16.5) go on with the driver; the only other parts,
	// the steering wheels of three kart models, are steering wheels. A part of
	// the driver that is modeled apart and lies across the middle low down
	// (trousers, say, even resting exactly on the kart's top) would stay still
	// as well - join it to the driver's mesh.
	if (b->driverPart >= 0)
	{
		const struct RldMkPart *kart = &b->parts[best];
		const struct RldMkPart *driver = &b->parts[b->driverPart];
		const double driverZ = 0.5 * (driver->lo[2] + driver->hi[2]);
		const double kartX = 0.5 * (kart->lo[0] + kart->hi[0]);

		for (i = 0; i < b->partCount; i++)
		{
			struct RldMkPart *part = &b->parts[i];
			const int wheel = (RldMk_Extent(part) <= (RLDMK_WHEEL_EXTENT * RLDMK_UNITS)) && ((0.5 * (part->lo[2] + part->hi[2])) > driverZ);

			if (((int)i != best) && ((int)i != b->driverPart) && !wheel && (part->lo[0] < kartX) && (part->hi[0] > kartX) && (part->lo[0] <= kart->hi[0]) &&
			    (part->hi[0] >= kart->lo[0]) && (part->lo[2] <= kart->hi[2]) && (part->hi[2] >= kart->lo[2]) && (part->lo[1] <= kart->hi[1]))
			{
				part->group = RLDMK_GROUP_KART;
			}
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

			if ((part->group == RLDMK_GROUP_KART) || ((int)i == b->driverPart) || (RldMk_Extent(part) > (RLDMK_WHEEL_EXTENT * RLDMK_UNITS)))
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

	// THE DRIVER CHOSEN BY THE TRIANGLES may be a detailed part of the vehicle
	// (a spoiler, a cannon) while the real driver - fewer triangles - went to
	// the kart and stands still. The rule itself stays (the highest part
	// instead breaks a separate head or an antenna on the kart); a large part
	// that reaches above the driver and stays with the kart is named. Large =
	// not a steering wheel by its extent; not a stick either: as wide and as
	// deep as an eighth of its height (an antenna is not a driver).
	if (b->driverPart >= 0)
	{
		const struct RldMkPart *driver = &b->parts[b->driverPart];
		int higher = -1;

		for (i = 0; i < b->partCount; i++)
		{
			const struct RldMkPart *part = &b->parts[i];
			const double height = part->hi[1] - part->lo[1];

			if (((int)i == best) || ((int)i == b->driverPart) || (part->group != RLDMK_GROUP_KART) || (RldMk_Extent(part) <= (RLDMK_WHEEL_EXTENT * RLDMK_UNITS)) ||
			    !(part->hi[1] > driver->hi[1]) || ((8.0 * (part->hi[0] - part->lo[0])) < height) || ((8.0 * (part->hi[2] - part->lo[2])) < height))
			{
				continue;
			}
			if ((higher < 0) || (part->hi[1] > b->parts[higher].hi[1]))
			{
				higher = (int)i;
			}
		}

		if (higher >= 0)
		{
			RldMk_Say("warning", "ply-driver", NULL,
			          "The driver was taken as the part with the most triangles (%u, up to %.2f model units high), but a larger part reaching higher (%u triangles, up to %.2f) stays still with the kart. If that part is the driver, join the vehicle's own parts to the kart's mesh (Blender: select them, Ctrl+J), so that the driver is the only other large part.",
			          driver->triangles, driver->hi[1] / RLDMK_UNITS, b->parts[higher].triangles, b->parts[higher].hi[1] / RLDMK_UNITS);
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

	// Size and centering (step 5) follow in RldMk_StepMeasure, after --size.
	return 1;
}

//========================================================================================
// STEP 3a: --fit crash - THE MODEL AT CRASH'S SIZE
//========================================================================================
//
// Before the parts, the model is scaled so that its kart is as long as Crash's
// (the game draws the wheels at fixed points of a retail kart), and the whole
// model at most as tall as the tallest retail driver. Three silent passes, in
// game units after the axes:
//   1  steps 2 and 3 at scale 1: L and H, the extent along z (forward) and y
//      (up) of the welded positions the triangles use;
//      f1 = min(112.4 / L, 142.5 / H)  - the whole model at Crash's length
//   2  steps 2 to 4 at f1: the kart as step 4 finds it. When it finds none,
//      the longest part that would be a kart at Crash's length (bottom near
//      the ground, the whole model within the length step 4 takes) - a driver
//      far longer than his kart leaves the kart too short at f1.
//   3  with a kart of length K at f1: f = min(f1 x 112.4 / K, 142.5 / H);
//      without one f = f1
// With --wheels off passes 2 and 3 are left out: f = f1. The game draws its
// wheel sprites at fixed points of a retail kart, so with wheels the kart has
// to be a retail kart's length; without them the model's own vehicle may be
// any length.
// f is rounded to 4 significant digits and applied as --scale f: the chain runs
// again from step 2 with that scale, so --fit crash gives the same bytes as
// --fit none --scale f. Kart detection, model-scale, ply-axes and ply-center
// see the fitted model, and --size works on it (100 % = Crash's size).

#define RLDMK_FIT_MODEL 0   // the whole model at Crash's length (no kart found, or --wheels off)
#define RLDMK_FIT_KART 1    // the kart at the length of Crash's

// 10^n for 0 <= n <= 22: every one of these is exact in a double.
static double RldMk_Pow10(int n)
{
	double p = 1.0;

	while (n-- > 0)
	{
		p *= 10.0;
	}
	return p;
}

// A factor -> 4 significant digits, halves up, within 0.0001..1000 (the range
// of --scale). The result is k / 10^n (or k x 10^n) with whole k and an exact
// power of ten: the same double strtod gives for the printed digits.
static double RldMk_FitRound(double f)
{
	int digits = 3;  // decimals after the point for 1 <= f < 10
	double k;

	if (!(f > 0.0))
	{
		return 1.0;
	}
	f = (f > RLDMK_FIT_MAX) ? RLDMK_FIT_MAX : ((f < RLDMK_FIT_MIN) ? RLDMK_FIT_MIN : f);

	while ((digits > 0) && (f >= RldMk_Pow10(4 - digits)))
	{
		digits--;
	}
	while ((digits < 7) && ((f * RldMk_Pow10(digits - 3)) < 1.0))
	{
		digits++;
	}

	k = RldMk_Floor(f * RldMk_Pow10(digits) + 0.5);
	f = k / RldMk_Pow10(digits);
	return (f > RLDMK_FIT_MAX) ? RLDMK_FIT_MAX : f;
}

// The factor for the whole model, `length` long and `height` tall (game
// units), not rounded; 0 for a model without either extent. *capped = 1 when
// the height decided.
static double RldMk_FitModel(double length, double height, int *capped)
{
	double f = 0.0;

	*capped = 0;
	if (length > 0.0)
	{
		f = RLDMK_CRASH_LENGTH / length;
	}
	if ((height > 0.0) && ((f == 0.0) || ((RLDMK_RETAIL_HEIGHT_MAX / height) < f)))
	{
		f = RLDMK_RETAIL_HEIGHT_MAX / height;
		*capped = 1;
	}
	return f;
}

// The rounded factor of the whole model (pass 1 alone).
static double RldMk_FitFactor(double length, double height)
{
	int capped;
	const double f = RldMk_FitModel(length, height, &capped);

	return (f > 0.0) ? RldMk_FitRound(f) : 1.0;
}

// Steps 2 and 3 (and 3r and 4 with `parts`) without a word: what they say is
// taken back, the real pass says it again. 1 = they got through. The parts
// are found on the repaired mesh, as in the real pass: an export with split
// seams (every corner a vertex of its own, moved by 1e-5 or more) falls apart
// into faces under the weld of step 3 alone, and a fragment of the kart then
// set the scale (a motorbike 13 % too large, or measured as a whole model and
// 26 % too small - built without a word).
static int RldMk_FitProbe(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *probe, int parts)
{
	const int quiet = s_rldMkQuiet;
	const int errors = s_rldMkErrors;
	const size_t said = strlen(s_rldMkCodes);
	u32 keyCount = 0;
	int ok;

	memset(probe, 0, sizeof(*probe));
	probe->kartPart = -1;
	probe->driverPart = -1;
	s_rldMkQuiet = 1;
	ok = RldMk_StepWeld(ply, opt, probe, &keyCount) && RldMk_StepTriangles(ply, probe, keyCount);
	if (ok && parts)
	{
		RldMk_StepOpenEdges(probe);
		ok = RldMk_StepRepair(ply, probe, opt) && RldMk_StepParts(probe, opt);
	}
	s_rldMkQuiet = quiet;
	s_rldMkErrors = errors;
	s_rldMkCodes[said] = '\0';
	return ok;
}

// Pass 2 without a kart from step 4: the longest part that is one at Crash's
// length - its bottom within the kart's range and the whole model within the
// length step 4 takes. Ties as step 4: the lower bottom. -1 = none.
static int RldMk_FitKart(const struct RldMkBuild *probe)
{
	double lo[3];
	double hi[3];
	int best = -1;
	u32 i;

	RldMk_Bounds(probe, -1, lo, hi);
	for (i = 0; i < probe->partCount; i++)
	{
		const struct RldMkPart *part = &probe->parts[i];
		const double length = part->hi[2] - part->lo[2];
		double r;

		if (!(length > 0.0))
		{
			continue;
		}

		r = RLDMK_CRASH_LENGTH / length;
		if (((part->lo[1] * r / RLDMK_UNITS) < RLDMK_KART_BOTTOM_MIN) || ((part->lo[1] * r / RLDMK_UNITS) > RLDMK_KART_BOTTOM_MAX) ||
		    (((hi[2] - lo[2]) * r / RLDMK_UNITS) > (RLDMK_DRIVER_LENGTH * 5.0)))
		{
			continue;
		}

		if ((best < 0) || (length > (probe->parts[best].hi[2] - probe->parts[best].lo[2])) ||
		    ((length == (probe->parts[best].hi[2] - probe->parts[best].lo[2])) && (part->lo[1] < probe->parts[best].lo[1])))
		{
			best = (int)i;
		}
	}

	return best;
}

// Passes 1 to 3; *scale becomes the factor. 0 = pass 1 did not get through:
// nothing is fitted, and the real pass says why.
static int RldMk_StepFit(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b, double *scale)
{
	struct RldMkOptions at = *opt;
	struct RldMkBuild probe;
	double lo[3];
	double hi[3];
	double f;
	int kart = -1;

	// 1: the whole model at scale 1
	at.scale = 1.0;
	if (!RldMk_FitProbe(ply, &at, &probe, 0))
	{
		RldMk_FreeBuild(&probe);
		return 0;
	}
	RldMk_Bounds(&probe, -1, lo, hi);
	RldMk_FreeBuild(&probe);

	b->fitted = 1;
	b->fitBefore[0] = hi[2] - lo[2];
	b->fitBefore[1] = hi[1] - lo[1];
	f = RldMk_FitModel(b->fitBefore[0], b->fitBefore[1], &b->fitCapped);
	b->fitFactor = (f > 0.0) ? RldMk_FitRound(f) : 1.0;
	b->fitBasis = RLDMK_FIT_MODEL;

	// 2: the kart at f1 - not with --wheels off: then the model brings its own
	// vehicle (a motorbike, say), which need not be as long as a retail kart,
	// and no wheel sprite has to sit on it - the whole model is the measure.
	at.scale = b->fitFactor;
	memset(&probe, 0, sizeof(probe));
	if (opt->noWheels)
	{
		kart = -1;
	}
	else if (RldMk_FitProbe(ply, &at, &probe, 1) && (probe.kartPart >= 0))
	{
		kart = probe.kartPart;
	}
	else if (probe.parts != NULL)
	{
		kart = RldMk_FitKart(&probe);
	}

	// 3: the kart at Crash's length, the height capped
	if ((kart >= 0) && ((probe.parts[kart].hi[2] - probe.parts[kart].lo[2]) > 0.0))
	{
		const double length = probe.parts[kart].hi[2] - probe.parts[kart].lo[2];

		f = b->fitFactor * RLDMK_CRASH_LENGTH / length;
		b->fitBasis = RLDMK_FIT_KART;
		b->fitCapped = 0;
		if ((b->fitBefore[1] > 0.0) && ((RLDMK_RETAIL_HEIGHT_MAX / b->fitBefore[1]) < f))
		{
			f = RLDMK_RETAIL_HEIGHT_MAX / b->fitBefore[1];
			b->fitCapped = 1;
		}
		b->fitKart[0] = length / b->fitFactor;
		b->fitFactor = RldMk_FitRound(f);
		b->fitKart[1] = b->fitKart[0] * b->fitFactor;
	}
	RldMk_FreeBuild(&probe);

	b->fitAfter[0] = b->fitBefore[0] * b->fitFactor;
	b->fitAfter[1] = b->fitBefore[1] * b->fitFactor;
	*scale = b->fitFactor;
	return 1;
}

// A measure of the fit for people and Reload Studio: one decimal, or 3
// significant digits below 1 (a model exported in millimeters).
static void RldMk_FitNumber(char *out, size_t size, double value)
{
	snprintf(out, size, (value >= 1.0) ? "%.1f" : "%.3g", value);
}

//========================================================================================
// STEP 4d: --fit crash - ONTO THE REFERENCE DUMMY
//========================================================================================
//
// After --fit crash has scaled the model and step 4 has found its parts (and
// after --remesh: a hull lies up to a cell outside its source, so its own
// bottom is the one placed), the model is moved onto the dummy (RldDum_Fit): its kart onto Crash's kart (x
// middle 0, bottom 5.6, z middle 2.1; the kart part alone, not the vehicle
// parts that joined it), with --wheels off the whole model onto the ground. One shift for every position; the parts' boxes follow. --size
// (RldMk_StepSize) and the poses (RldMk_StepPoses) then turn about the
// dummy's seat and its steering column. With --fit none nothing moves: the
// model stays where it was exported, and --size and the poses turn about the
// hip (0, RLDMK_HIP_Y, 0) as before.

static void RldMk_DummyBox(struct RldDumBox *box, const double lo[3], const double hi[3])
{
	int a;

	for (a = 0; a < 3; a++)
	{
		box->lo[a] = (!box->valid || (lo[a] < box->lo[a])) ? lo[a] : box->lo[a];
		box->hi[a] = (!box->valid || (hi[a] > box->hi[a])) ? hi[a] : box->hi[a];
	}
	box->valid = 1;
}

static int RldMk_StepDummy(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	struct RldDumFitInput in;
	struct RldDumBox *boxes[RLDMK_GROUPS];
	u32 i;
	int a;

	b->dummy = 0;
	if (!opt->fit || !b->fitted)
	{
		return 1;
	}

	memset(&in, 0, sizeof(in));
	boxes[RLDMK_GROUP_KART] = &in.kart;
	boxes[RLDMK_GROUP_DRIVER] = &in.driver;
	boxes[RLDMK_GROUP_WHEEL] = &in.wheel;
	for (i = 0; i < b->partCount; i++)
	{
		if (b->parts[i].triangles != 0u)
		{
			if (b->parts[i].group != RLDMK_GROUP_KART)
			{
				RldMk_DummyBox(boxes[b->parts[i].group], b->parts[i].lo, b->parts[i].hi);
			}
			RldMk_DummyBox(&in.model, b->parts[i].lo, b->parts[i].hi);
		}
	}

	// THE KART is the kart part alone - the part --fit crash brought to Crash's
	// length (RldMk_StepFit) and the game's wheel sprites belong to. Vehicle
	// parts that joined the kart group (a bumper, a motorbike's front) would
	// move it off the dummy kart. After --remesh the kart part holds the hull
	// of the whole group: each side of the kart part moves as far as the
	// group's side moved, where the kart part made that side.
	if (b->kartSourceValid)
	{
		double lo[3];
		double hi[3];

		for (a = 0; a < 3; a++)
		{
			lo[a] = b->kartSource[a] + ((b->kartSource[a] == b->kartGroupSource[a]) ? (b->parts[b->kartPart].lo[a] - b->kartGroupSource[a]) : 0.0);
			hi[a] = b->kartSource[3 + a] + ((b->kartSource[3 + a] == b->kartGroupSource[3 + a]) ? (b->parts[b->kartPart].hi[a] - b->kartGroupSource[3 + a]) : 0.0);
		}
		RldMk_DummyBox(&in.kart, lo, hi);
	}
	else
	{
		RldMk_DummyBox(&in.kart, b->parts[b->kartPart].lo, b->parts[b->kartPart].hi);
	}
	in.wheels = !opt->noWheels;
	in.sizePercent = 100u;
	if (!RldDum_Fit(&in, &b->dumFit100))
	{
		return 1;
	}
	in.sizePercent = opt->sizePercent;
	RldDum_Fit(&in, &b->dumFit);

	for (i = 0; i < b->positionCount; i++)
	{
		for (a = 0; a < 3; a++)
		{
			b->position[3u * i + (u32)a] += b->dumFit.shift[a];
		}
	}
	// after --remesh: the source the colors come from moves along (RldRem_Recolor)
	for (i = 0; (b->remPosition != NULL) && (i < b->remPositionCount); i++)
	{
		for (a = 0; a < 3; a++)
		{
			b->remPosition[3u * i + (u32)a] += b->dumFit.shift[a];
		}
	}
	for (i = 0; i < b->partCount; i++)
	{
		RldMk_Bounds(b, (int)i, b->parts[i].lo, b->parts[i].hi);
	}
	b->dummy = 1;
	return 1;
}

//========================================================================================
// STEP 4m: --remesh on - CLOSED HULLS
//========================================================================================
//
// On request only, for a model whose surface is open, doubled or tangled: each
// part group (kart, driver, steering wheel) of more than RLDMK_REDUCE_KEEP
// triangles is replaced by a closed, outward hull of its own surface
// (tools/rldpack_remesh.inc, RLDMK_REMESH_RESOLUTION cells along the group's
// longest side); a smaller group - a steering wheel - stays as it is. The hull
// has some 10^5 triangles: step 4r reduces it, the budget shared by the
// triangles the groups had before (a hull group at least RLDMK_REMESH_SHARE_MIN),
// and its corners take their colors from the source again (RldRem_Recolor).
// After it the corners are vertices of their own (vertexRgb), as after 4r.
// The parts keep their groups: a group's hull belongs to the kart part, the
// driver part, or the first part of the group.

// The limits of step 4r (see there).
#define RLDMK_REDUCE_LIMIT (RLDCHAR_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3)  // the triangles that draw memory holds
#define RLDMK_REDUCE_TARGET (RLDMK_REDUCE_LIMIT - (RLDMK_REDUCE_LIMIT / 100u))  // 1 % in hand
#define RLDMK_REDUCE_KEEP 24u
#define RLDMK_REDUCE_TRIES 10

#define RLDMK_REMESH_RESOLUTION 64u
#define RLDMK_REMESH_RESOLUTION_MIN 32u
#define RLDMK_REMESH_RESOLUTION_MAX 128u
#define RLDMK_REMESH_SHARE_MIN 40u

static int RldMk_StepRemesh(struct RldMkBuild *b, const struct RldMkOptions *opt, const struct RldMkPly *ply)
{
	const u32 resolution = (opt->remeshResolution != 0u) ? opt->remeshResolution : RLDMK_REMESH_RESOLUTION;
	u32 counts[RLDMK_GROUPS] = {0u, 0u, 0u};
	int partOfGroup[RLDMK_GROUPS] = {-1, -1, -1};
	RldRemInput in;
	RldRemResult hull;
	char why[256];
	u32 *keep = NULL;
	u32 *vertexPosition = NULL;
	u8 *vertexRgb = NULL;
	u32 *triangle = NULL;
	double *position = NULL;
	u32 *positionPart = NULL;
	u8 *positionGroup = NULL;
	u32 *diagFace = NULL;
	u32 *plyPosition = NULL;
	u32 keptPositions = 0;
	u32 keptTriangles = 0;
	u32 count = 0;
	u32 corners;
	u32 t;
	u32 i;
	int k;

	if (!opt->remesh)
	{
		return 1;
	}

	for (t = 0; t < b->triangleCount; t++)
	{
		counts[b->positionGroup[b->vertexPosition[b->triangle[3u * t]]]]++;
	}
	for (i = 0; i < b->partCount; i++)
	{
		if (partOfGroup[b->parts[i].group] < 0)
		{
			partOfGroup[b->parts[i].group] = (int)i;
		}
	}
	// The kart's hull belongs to the kart part (its bounds measure the kart,
	// RldMk_StepMeasure), the driver's to the driver part.
	partOfGroup[RLDMK_GROUP_KART] = b->kartPart;
	if ((b->driverPart >= 0) && (b->parts[b->driverPart].group == RLDMK_GROUP_DRIVER))
	{
		partOfGroup[RLDMK_GROUP_DRIVER] = b->driverPart;
	}

	// The kart part and the kart group before the hulls (RldMk_StepDummy).
	{
		double lo[3];
		double hi[3];
		int g;

		b->kartSourceValid = 1;
		for (g = 0; g < 3; g++)
		{
			b->kartSource[g] = b->parts[b->kartPart].lo[g];
			b->kartSource[3 + g] = b->parts[b->kartPart].hi[g];
			lo[g] = 1.0e30;
			hi[g] = -1.0e30;
		}
		for (i = 0; i < b->positionCount; i++)
		{
			if (b->positionGroup[i] == RLDMK_GROUP_KART)
			{
				for (g = 0; g < 3; g++)
				{
					lo[g] = (b->position[3u * i + (u32)g] < lo[g]) ? b->position[3u * i + (u32)g] : lo[g];
					hi[g] = (b->position[3u * i + (u32)g] > hi[g]) ? b->position[3u * i + (u32)g] : hi[g];
				}
			}
		}
		for (g = 0; g < 3; g++)
		{
			b->kartGroupSource[g] = lo[g];
			b->kartGroupSource[3 + g] = hi[g];
		}
	}

	// THE SOURCE: the triangles of the groups to remesh, the positions as they are.
	b->remPosition = (double *)malloc(((size_t)b->positionCount + 1u) * 3u * sizeof(double));
	b->remTriangle = (u32 *)malloc(((size_t)b->triangleCount + 1u) * 3u * sizeof(u32));
	b->remColor = (u32 *)malloc(((size_t)b->triangleCount + 1u) * 3u * sizeof(u32));
	b->remGroup = (u8 *)malloc((size_t)b->triangleCount + 1u);
	if ((b->remPosition == NULL) || (b->remTriangle == NULL) || (b->remColor == NULL) || (b->remGroup == NULL))
	{
		return RldMk_OutOfMemory();
	}
	memcpy(b->remPosition, b->position, (size_t)b->positionCount * 3u * sizeof(double));
	b->remPositionCount = b->positionCount;
	for (t = 0; t < b->triangleCount; t++)
	{
		const u8 group = b->positionGroup[b->vertexPosition[b->triangle[3u * t]]];

		if (counts[group] <= RLDMK_REDUCE_KEEP)
		{
			keptTriangles++;
			continue;
		}
		for (k = 0; k < 3; k++)
		{
			const u32 vertex = b->triangle[3u * t + (u32)k];
			const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * vertex] : &ply->rgb[3u * vertex];

			b->remTriangle[3u * count + (u32)k] = b->vertexPosition[vertex];
			b->remColor[3u * count + (u32)k] = ((u32)rgb[0] << 16) | ((u32)rgb[1] << 8) | (u32)rgb[2];
		}
		b->remGroup[count] = group;
		count++;
	}
	b->remTriangleCount = count;
	b->remeshResolution = resolution;
	memcpy(b->remeshSource, counts, sizeof(counts));

	if (count == 0u)
	{
		RldMk_Say("warning", "model-remesh", NULL, "Nothing was remeshed: kart, driver and steering wheel have at most %u triangles each.", RLDMK_REDUCE_KEEP);
		return 1;
	}

	in.positionCount = b->remPositionCount;
	in.position = b->remPosition;
	in.triangleCount = count;
	in.triangle = b->remTriangle;
	in.cornerColor = b->remColor;
	in.triangleGroup = b->remGroup;
	in.groupCount = RLDMK_GROUPS;
	in.resolution = resolution;
	why[0] = '\0';
	if (!RldRem_Remesh(&in, &hull, why, sizeof(why)))
	{
		RldMk_Say("error", "model-remesh", why, "The model cannot be remeshed (%s). Build it without --remesh.", why);
		return 0;
	}

	// THE NEW MODEL: the hulls, then the groups kept as they were (their
	// positions in their old order), the corners as vertices of their own.
	keep = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	if (keep == NULL)
	{
		RldRem_Free(&hull);
		return RldMk_OutOfMemory();
	}
	for (i = 0; i < b->positionCount; i++)
	{
		keep[i] = RLDMK_NONE;
	}
	for (t = 0; t < b->triangleCount; t++)
	{
		if (counts[b->positionGroup[b->vertexPosition[b->triangle[3u * t]]]] <= RLDMK_REDUCE_KEEP)
		{
			for (k = 0; k < 3; k++)
			{
				keep[b->vertexPosition[b->triangle[3u * t + (u32)k]]] = 0u;
			}
		}
	}
	for (i = 0; i < b->positionCount; i++)
	{
		if (keep[i] != RLDMK_NONE)
		{
			keep[i] = hull.positionCount + keptPositions++;
		}
	}

	corners = 3u * (hull.triangleCount + keptTriangles);
	vertexPosition = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	vertexRgb = (u8 *)malloc(((size_t)corners + 1u) * 3u);
	triangle = (u32 *)malloc(((size_t)corners + 1u) * sizeof(u32));
	position = (double *)malloc(((size_t)hull.positionCount + keptPositions + 1u) * 3u * sizeof(double));
	positionPart = (u32 *)malloc(((size_t)hull.positionCount + keptPositions + 1u) * sizeof(u32));
	positionGroup = (u8 *)calloc((size_t)hull.positionCount + keptPositions + 1u, 1u);
	if (b->diagnose)
	{
		diagFace = (u32 *)malloc(((size_t)hull.triangleCount + keptTriangles + 1u) * sizeof(u32));
		plyPosition = (u32 *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(u32));
	}
	if ((vertexPosition == NULL) || (vertexRgb == NULL) || (triangle == NULL) || (position == NULL) || (positionPart == NULL) || (positionGroup == NULL) ||
	    (b->diagnose && ((diagFace == NULL) || (plyPosition == NULL))))
	{
		free(keep);
		free(vertexPosition);
		free(vertexRgb);
		free(triangle);
		free(position);
		free(positionPart);
		free(positionGroup);
		free(diagFace);
		free(plyPosition);
		RldRem_Free(&hull);
		return RldMk_OutOfMemory();
	}

	memcpy(position, hull.position, (size_t)hull.positionCount * 3u * sizeof(double));
	for (i = 0; i < hull.positionCount; i++)
	{
		positionPart[i] = (u32)b->kartPart;
	}
	for (t = 0; t < hull.triangleCount; t++)
	{
		const u8 group = hull.triangleGroup[t];

		for (k = 0; k < 3; k++)
		{
			const u32 at = 3u * t + (u32)k;
			const u32 rgb = hull.cornerColor[at];

			vertexPosition[at] = hull.triangle[at];
			vertexRgb[3u * at + 0u] = (u8)(rgb >> 16);
			vertexRgb[3u * at + 1u] = (u8)(rgb >> 8);
			vertexRgb[3u * at + 2u] = (u8)rgb;
			triangle[at] = at;
			positionGroup[hull.triangle[at]] = group;
			positionPart[hull.triangle[at]] = (u32)partOfGroup[group];
		}
		if (diagFace != NULL)
		{
			diagFace[t] = RLDMK_NONE;
		}
	}
	for (i = 0; i < b->positionCount; i++)
	{
		if (keep[i] != RLDMK_NONE)
		{
			memcpy(&position[3u * keep[i]], &b->position[3u * i], 3u * sizeof(double));
			positionGroup[keep[i]] = b->positionGroup[i];
			positionPart[keep[i]] = b->positionPart[i];
		}
	}
	i = hull.triangleCount;
	for (t = 0; t < b->triangleCount; t++)
	{
		if (counts[b->positionGroup[b->vertexPosition[b->triangle[3u * t]]]] > RLDMK_REDUCE_KEEP)
		{
			continue;
		}
		for (k = 0; k < 3; k++)
		{
			const u32 vertex = b->triangle[3u * t + (u32)k];
			const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * vertex] : &ply->rgb[3u * vertex];
			const u32 at = 3u * i + (u32)k;

			vertexPosition[at] = keep[b->vertexPosition[vertex]];
			memcpy(&vertexRgb[3u * at], rgb, 3u);
			triangle[at] = at;
		}
		if (diagFace != NULL)
		{
			diagFace[i] = (b->diagFace != NULL) ? b->diagFace[t] : RLDMK_NONE;
		}
		i++;
	}
	if (plyPosition != NULL)
	{
		for (i = 0; i < ply->vertexCount; i++)
		{
			const u32 p = (b->diagPlyPosition != NULL) ? b->diagPlyPosition[i] : b->vertexPosition[i];

			plyPosition[i] = (p != RLDMK_NONE) ? keep[p] : RLDMK_NONE;
		}
	}

	// Open after the repair: the hulls are closed, the groups kept stay as they were.
	if (b->triangleOpen != NULL)
	{
		u8 *open = (u8 *)calloc((size_t)hull.triangleCount + keptTriangles + 1u, 1u);
		u32 at = hull.triangleCount;

		if (open == NULL)
		{
			free(keep);
			free(vertexPosition);
			free(vertexRgb);
			free(triangle);
			free(position);
			free(positionPart);
			free(positionGroup);
			free(diagFace);
			free(plyPosition);
			RldRem_Free(&hull);
			return RldMk_OutOfMemory();
		}
		for (t = 0; t < b->triangleCount; t++)
		{
			if (counts[b->positionGroup[b->vertexPosition[b->triangle[3u * t]]]] <= RLDMK_REDUCE_KEEP)
			{
				open[at++] = b->triangleOpen[t];
			}
		}
		free(b->triangleOpen);
		b->triangleOpen = open;
	}

	free(keep);
	free(b->position);
	free(b->vertexPosition);
	free(b->vertexRgb);
	free(b->triangle);
	free(b->positionPart);
	free(b->positionGroup);
	b->position = position;
	b->vertexPosition = vertexPosition;
	b->vertexRgb = vertexRgb;
	b->triangle = triangle;
	b->positionPart = positionPart;
	b->positionGroup = positionGroup;
	b->positionCount = hull.positionCount + keptPositions;
	b->triangleCount = hull.triangleCount + keptTriangles;
	if (diagFace != NULL)
	{
		free(b->diagFace);
		b->diagFace = diagFace;
		free(b->diagPlyPosition);
		b->diagPlyPosition = plyPosition;
	}
	RldRem_Free(&hull);

	// The parts as they are now.
	b->remeshFrom = b->remeshSource[0] + b->remeshSource[1] + b->remeshSource[2];
	b->remeshOpenBefore = b->openEdges;
	memset(b->groupTriangles, 0, sizeof(b->groupTriangles));
	for (i = 0; i < b->partCount; i++)
	{
		b->parts[i].triangles = 0;
	}
	for (t = 0; t < b->triangleCount; t++)
	{
		const u32 part = b->positionPart[b->vertexPosition[b->triangle[3u * t]]];

		b->parts[part].triangles++;
		b->groupTriangles[b->parts[part].group]++;
	}
	for (i = 0; i < b->partCount; i++)
	{
		RldMk_Bounds(b, (int)i, b->parts[i].lo, b->parts[i].hi);
	}
	RldMk_StepOpenEdges(b);
	b->remeshed = 1;
	b->remeshHull = b->triangleCount;
	return 1;
}

//========================================================================================
// STEP 4r: --reduce auto - FEWER TRIANGLES
//========================================================================================
//
// The draw memory of a character holds 28 x T <= RLDCHAR_DRAW_BYTES_MAX bytes
// per frame: T <= RLDMK_REDUCE_LIMIT (step 6, model-tris). With more, the model
// is reduced to at most RLDMK_REDUCE_TARGET triangles by
// tools/rldpack_reduce.inc. The target keeps 1 % in hand: the reduction removes
// one or two triangles per step and stops at the first count at or below the
// target, and the reserve keeps the model under the limit should the game count
// the command list differently. At or below the limit nothing is reduced -
// not even when the file would be too large (RldMk_BuildModel). When a reduced
// model then still fails mem-model or model-frame (too many records),
// RldMk_BuildModel lowers the target by 5 % and builds again, at most
// RLDMK_REDUCE_TRIES times.
//
// Per group, on the parts of step 4 (they are not found again): a group of at
// most RLDMK_REDUCE_KEEP triangles stays whole; the rest of the budget is
// shared by the other groups in proportion to their triangles, the leftover
// one by one by the largest remainder (ties: the lower group). The reduction
// moves positions onto their neighbors and drops triangles, never makes new
// positions; every kept corner keeps the color of the corner it continues.
// After it the corners are vertices of their own (vertexRgb), the positions
// no triangle uses any more are dropped, and the counts and bounds of the
// parts and the open edges are worked out again.

// The targets per group for `target` triangles in all. 0 = the small groups
// alone take the whole budget.
static int RldMk_ReduceTargets(const u32 counts[RLDMK_GROUPS], u32 target, u32 targets[RLDMK_GROUPS])
{
	u32 remainder[RLDMK_GROUPS];
	u32 fixed = 0;
	u32 rest = 0;
	u32 budget;
	u32 assigned = 0;
	int g;

	for (g = 0; g < RLDMK_GROUPS; g++)
	{
		if (counts[g] <= RLDMK_REDUCE_KEEP)
		{
			fixed += counts[g];
		}
		else
		{
			rest += counts[g];
		}
	}
	if ((rest == 0u) || (fixed >= target))
	{
		return 0;
	}

	budget = target - fixed;
	for (g = 0; g < RLDMK_GROUPS; g++)
	{
		remainder[g] = 0u;
		targets[g] = counts[g];
		if (counts[g] > RLDMK_REDUCE_KEEP)
		{
			targets[g] = (u32)(((u64)budget * counts[g]) / rest);
			remainder[g] = (u32)(((u64)budget * counts[g]) % rest);
			assigned += targets[g];
		}
	}

	while (assigned < budget)
	{
		int best = -1;

		for (g = 0; g < RLDMK_GROUPS; g++)
		{
			if ((counts[g] > RLDMK_REDUCE_KEEP) && (remainder[g] != RLDMK_NONE) && ((best < 0) || (remainder[g] > remainder[best])))
			{
				best = g;
			}
		}
		if (best < 0)
		{
			break;
		}
		targets[best]++;
		remainder[best] = RLDMK_NONE;  // each group once
		assigned++;
	}

	return 1;
}

// After --remesh no hull may be reduced below RLDMK_REMESH_SHARE_MIN triangles:
// a hull of a few dozen triangles loses its volume, or the reducer cannot get
// there. A hull below it is raised to it, the triangles taken from the hull
// with the highest target (ties: the lower group). 0 = not possible.
static int RldMk_RemeshFloor(const u32 source[RLDMK_GROUPS], u32 targets[RLDMK_GROUPS])
{
	int g;

	for (g = 0; g < RLDMK_GROUPS; g++)
	{
		if ((source[g] > RLDMK_REDUCE_KEEP) && (targets[g] < RLDMK_REMESH_SHARE_MIN))
		{
			const u32 deficit = RLDMK_REMESH_SHARE_MIN - targets[g];
			int largest = -1;
			int h;

			for (h = 0; h < RLDMK_GROUPS; h++)
			{
				if ((h != g) && (source[h] > RLDMK_REDUCE_KEEP) && ((largest < 0) || (targets[h] > targets[largest])))
				{
					largest = h;
				}
			}
			if ((largest < 0) || (targets[largest] < (RLDMK_REMESH_SHARE_MIN + deficit)))
			{
				return 0;
			}
			targets[largest] -= deficit;
			targets[g] = RLDMK_REMESH_SHARE_MIN;
		}
	}
	return 1;
}

// After --remesh, the reduced triangles' corner colors from the source surface
// (RldRem_Recolor; the groups kept as they were keep theirs). vertexRgb: 3 per
// corner of the result, overwritten. 0 = it failed, said.
static int RldMk_RemeshRecolor(const struct RldMkBuild *b, const RldRedResult *result, u8 *vertexRgb)
{
	RldRemInput in;
	u32 *color = (u32 *)malloc(((size_t)result->triangleCount * 3u + 1u) * sizeof(u32));
	u8 *group = (u8 *)malloc((size_t)result->triangleCount + 1u);
	char why[256];
	u32 i;

	if ((color == NULL) || (group == NULL))
	{
		free(color);
		free(group);
		return RldMk_OutOfMemory();
	}
	for (i = 0; i < (3u * result->triangleCount); i++)
	{
		color[i] = ((u32)vertexRgb[3u * i] << 16) | ((u32)vertexRgb[3u * i + 1u] << 8) | (u32)vertexRgb[3u * i + 2u];
	}
	for (i = 0; i < result->triangleCount; i++)
	{
		group[i] = b->positionGroup[result->triangle[3u * i]];
	}

	in.positionCount = b->remPositionCount;
	in.position = b->remPosition;
	in.triangleCount = b->remTriangleCount;
	in.triangle = b->remTriangle;
	in.cornerColor = b->remColor;
	in.triangleGroup = b->remGroup;
	in.groupCount = RLDMK_GROUPS;
	in.resolution = b->remeshResolution;
	why[0] = '\0';
	if (!RldRem_Recolor(&in, b->positionCount, b->position, result->triangleCount, result->triangle, group, color, why, sizeof(why)))
	{
		free(color);
		free(group);
		RldMk_Say("error", "model-remesh", why, "The colors of the remeshed model cannot be taken from the source (%s).", why);
		return 0;
	}
	for (i = 0; i < (3u * result->triangleCount); i++)
	{
		vertexRgb[3u * i + 0u] = (u8)(color[i] >> 16);
		vertexRgb[3u * i + 1u] = (u8)(color[i] >> 8);
		vertexRgb[3u * i + 2u] = (u8)color[i];
	}
	free(color);
	free(group);
	return 1;
}

// The reducer names groups by number; the message names them as the parts.
static void RldMk_GroupWhy(char *why, size_t size)
{
	char *at = strstr(why, "group ");
	char rest[256];

	if ((at != NULL) && (at[6] >= '0') && (at[6] < (char)('0' + RLDMK_GROUPS)) && ((at[7] < '0') || (at[7] > '9')))
	{
		const int group = at[6] - '0';

		snprintf(rest, sizeof(rest), "%s", &at[7]);
		snprintf(at, size - (size_t)(at - why), "the %s%s", (group == RLDMK_GROUP_WHEEL) ? "steering wheel" : s_rldMkGroupNames[group], rest);
	}
}

static int RldMk_StepReduce(const struct RldMkPly *ply, struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const u32 triangles = b->triangleCount;
	const u32 target = (opt->reduceTarget != 0u) ? opt->reduceTarget : RLDMK_REDUCE_TARGET;
	u32 counts[RLDMK_GROUPS] = {0u, 0u, 0u};
	u32 targets[RLDMK_GROUPS];
	RldRedMesh mesh;
	RldRedResult result;
	char why[256];
	u32 *corner;
	u32 *cornerColor;
	u8 *group;
	u32 *vertexPosition;
	u8 *vertexRgb;
	u32 *triangle;
	u32 *renumber;
	double *position;
	u32 *positionPart;
	u8 *positionGroup;
	u32 *diagReduced = NULL;
	u32 *diagFrom = NULL;
	u32 reduceStage;
	u32 used = 0;
	u32 t;
	u32 i;

	b->reduceFrom = triangles;
	b->reduceTo = triangles;

	// The model against its limit before anything is reduced, for Reload
	// Studio: triangles, their limit, draw memory, its limit. These are the
	// triangles the limit counts (step 6): after repair, before reduction and
	// before packing. @char triangles at the end is what the game draws: the
	// same less what the reduction took (@char reduced) and less the triangles
	// packing left without area (@char collapsed, model-collapse).
	if (!opt->maskModel && s_machine && !s_rldMkQuiet)
	{
		char text[4][16];

		snprintf(text[0], sizeof(text[0]), "%u", triangles);
		snprintf(text[1], sizeof(text[1]), "%u", (u32)RLDMK_REDUCE_LIMIT);
		snprintf(text[2], sizeof(text[2]), "%u", triangles * RLDCHAR_DRAW_BYTES_G3);
		snprintf(text[3], sizeof(text[3]), "%u", (u32)RLDCHAR_DRAW_BYTES_MAX);
		Rld_Emit("char", "budget", text[0], text[1], text[2], text[3], (const char *)NULL);
	}

	if (!opt->reduce || (triangles <= target) || (!opt->reduceForce && (triangles <= (opt->maskModel ? RLDMK_MASK_TRIANGLES_MAX : RLDMK_REDUCE_LIMIT))))
	{
		return 1;
	}

	corner = (u32 *)malloc(((size_t)triangles * 3u + 1u) * sizeof(u32));
	cornerColor = (u32 *)malloc(((size_t)triangles * 3u + 1u) * sizeof(u32));
	group = (u8 *)malloc((size_t)triangles + 1u);
	if ((corner == NULL) || (cornerColor == NULL) || (group == NULL))
	{
		free(corner);
		free(cornerColor);
		free(group);
		return RldMk_OutOfMemory();
	}
	for (i = 0; i < (3u * triangles); i++)
	{
		const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * b->triangle[i]] : &ply->rgb[3u * b->triangle[i]];

		corner[i] = b->vertexPosition[b->triangle[i]];
		cornerColor[i] = ((u32)rgb[0] << 16) | ((u32)rgb[1] << 8) | (u32)rgb[2];
	}
	for (t = 0; t < triangles; t++)
	{
		group[t] = b->positionGroup[corner[3u * t]];
		counts[group[t]]++;
	}

	// After --remesh the budget is shared by the triangles the groups had
	// before (a hull would take nearly all of it), a hull at least
	// RLDMK_REMESH_SHARE_MIN; the groups kept as they were count as they are.
	if (b->remeshed)
	{
		int g;

		for (g = 0; g < RLDMK_GROUPS; g++)
		{
			if (b->remeshSource[g] > RLDMK_REDUCE_KEEP)
			{
				counts[g] = (b->remeshSource[g] < RLDMK_REMESH_SHARE_MIN) ? RLDMK_REMESH_SHARE_MIN : b->remeshSource[g];
			}
		}
	}

	memset(&result, 0, sizeof(result));
	why[0] = '\0';
	if (!RldMk_ReduceTargets(counts, target, targets))
	{
		snprintf(why, sizeof(why), "the parts of at most %u triangles leave no room for the others", RLDMK_REDUCE_KEEP);
	}
	else if (b->remeshed && !RldMk_RemeshFloor(b->remeshSource, targets))
	{
		snprintf(why, sizeof(why), "a budget of %u triangles cannot give every hull at least %u", target, RLDMK_REMESH_SHARE_MIN);
	}
	else
	{
		int g;

		// ONE BUDGET for all groups: the cheapest collapse of any group goes
		// first (the reducer weighs shape and color seams), so detail stays
		// where it costs most. Each group has a floor: half of its triangles,
		// at most its share in proportion (RldMk_ReduceTargets - so the floors
		// fit the budget), a small group all of them.
		// NOT AFTER --remesh: each hull keeps its share by the triangles the
		// group had before (at least RLDMK_REMESH_SHARE_MIN, RldMk_RemeshFloor).
		// The hull of an open shell (a kart) has an inside and an outside; with
		// one budget it took 1.5 to 1.9 times the triangles its source had, the
		// driver only 0.5 to 0.7 times (four models, resolution 64).
		for (g = 0; !b->remeshed && (g < RLDMK_GROUPS); g++)
		{
			if (counts[g] > RLDMK_REDUCE_KEEP)
			{
				const u32 half = counts[g] / 2u;

				targets[g] = (half < targets[g]) ? half : targets[g];
			}
		}

		memset(&mesh, 0, sizeof(mesh));
		mesh.positionCount = b->positionCount;
		mesh.position = b->position;
		mesh.triangleCount = triangles;
		mesh.triangle = corner;
		mesh.triangleGroup = group;
		mesh.groupCount = RLDMK_GROUPS;
		mesh.groupTarget = targets;
		mesh.cornerColor = cornerColor;
		mesh.totalTarget = b->remeshed ? 0u : target;
		if (RldRed_Reduce(&mesh, &result, why, sizeof(why)))
		{
			why[0] = '\0';
		}
		else if (why[0] == '\0')
		{
			snprintf(why, sizeof(why), "reduce: not reached");
		}
	}
	free(corner);
	free(cornerColor);
	free(group);

	if (why[0] != '\0')
	{
		RldRed_Free(&result);
		RldMk_GroupWhy(why, sizeof(why));
		RldMk_Say("warning", "model-reduce", why, "The model could not be reduced automatically to %u triangles; it keeps %u.", target, triangles);
		return 1;
	}

	// The corners as vertices of their own: the position of the result, the
	// color of the corner each one continues.
	reduceStage = result.stage;
	vertexPosition = (u32 *)malloc(((size_t)result.triangleCount * 3u + 1u) * sizeof(u32));
	vertexRgb = (u8 *)malloc(((size_t)result.triangleCount * 3u + 1u) * 3u);
	triangle = (u32 *)malloc(((size_t)result.triangleCount * 3u + 1u) * sizeof(u32));
	renumber = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	position = (double *)malloc(((size_t)b->positionCount + 1u) * 3u * sizeof(double));
	positionPart = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	positionGroup = (u8 *)malloc((size_t)b->positionCount + 1u);
	if (b->diagnose)
	{
		diagReduced = (u32 *)malloc(((size_t)result.triangleCount * 7u + 1u) * sizeof(u32));
		diagFrom = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	}
	if ((vertexPosition == NULL) || (vertexRgb == NULL) || (triangle == NULL) || (renumber == NULL) || (position == NULL) || (positionPart == NULL) ||
	    (positionGroup == NULL) || (b->diagnose && ((diagReduced == NULL) || (diagFrom == NULL))))
	{
		free(vertexPosition);
		free(vertexRgb);
		free(triangle);
		free(renumber);
		free(position);
		free(positionPart);
		free(positionGroup);
		free(diagReduced);
		free(diagFrom);
		RldRed_Free(&result);
		return RldMk_OutOfMemory();
	}

	// Only the positions a kept triangle uses, in their old order.
	for (i = 0; i < b->positionCount; i++)
	{
		renumber[i] = RLDMK_NONE;
	}
	for (i = 0; i < (3u * result.triangleCount); i++)
	{
		renumber[result.triangle[i]] = 0u;
	}
	for (i = 0; i < b->positionCount; i++)
	{
		if (renumber[i] != RLDMK_NONE)
		{
			renumber[i] = used;
			memcpy(&position[3u * used], &b->position[3u * i], 3u * sizeof(double));
			positionPart[used] = b->positionPart[i];
			positionGroup[used] = b->positionGroup[i];
			if (diagFrom != NULL)
			{
				diagFrom[used] = i;
			}
			used++;
		}
	}

	for (i = 0; i < (3u * result.triangleCount); i++)
	{
		const u32 source = b->triangle[result.sourceCorner[i]];
		const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * source] : &ply->rgb[3u * source];

		vertexPosition[i] = renumber[result.triangle[i]];
		memcpy(&vertexRgb[3u * i], rgb, 3u);
		triangle[i] = i;
	}

	// After --remesh: the colors of the hull corners again from the source
	// surface (RldRem_Recolor); a kept triangle stands for hundreds of hull
	// triangles, and the color of whichever survived may be a speck of an eye.
	if (b->remeshed && (b->remTriangleCount != 0u) && !RldMk_RemeshRecolor(b, &result, vertexRgb))
	{
		free(vertexPosition);
		free(vertexRgb);
		free(triangle);
		free(renumber);
		free(position);
		free(positionPart);
		free(positionGroup);
		free(diagReduced);
		free(diagFrom);
		RldRed_Free(&result);
		return 0;
	}

	// The open parts: each kept triangle is as open as the one it continues.
	if (b->triangleOpen != NULL)
	{
		u8 *open = (u8 *)malloc((size_t)result.triangleCount + 1u);

		if (open == NULL)
		{
			free(vertexPosition);
			free(vertexRgb);
			free(triangle);
			free(renumber);
			free(position);
			free(positionPart);
			free(positionGroup);
			free(diagReduced);
			free(diagFrom);
			RldRed_Free(&result);
			return RldMk_OutOfMemory();
		}
		for (t = 0; t < result.triangleCount; t++)
		{
			open[t] = b->triangleOpen[result.sourceTriangle[t]];
		}
		free(b->triangleOpen);
		b->triangleOpen = open;
	}

	// --diagnose: the kept triangles in the numbers before the reduction (the
	// positions until here are the diag positions, RldMk_DiagKeep).
	if (diagReduced != NULL)
	{
		for (t = 0; t < result.triangleCount; t++)
		{
			diagReduced[7u * t + 0u] = result.triangle[3u * t + 0u];
			diagReduced[7u * t + 1u] = result.triangle[3u * t + 1u];
			diagReduced[7u * t + 2u] = result.triangle[3u * t + 2u];
			diagReduced[7u * t + 3u] = result.sourceTriangle[t];
			diagReduced[7u * t + 4u] = result.sourceCorner[3u * t + 0u];
			diagReduced[7u * t + 5u] = result.sourceCorner[3u * t + 1u];
			diagReduced[7u * t + 6u] = result.sourceCorner[3u * t + 2u];
		}
		free(b->diagReduced);
		free(b->diagPositionFrom);
		b->diagReduced = diagReduced;
		b->diagReducedCount = result.triangleCount;
		b->diagPositionFrom = diagFrom;
	}

	free(b->triangle);
	free(b->vertexPosition);
	free(b->vertexRgb);
	free(b->position);
	free(b->positionPart);
	free(b->positionGroup);
	free(renumber);
	b->triangle = triangle;
	b->vertexPosition = vertexPosition;
	b->vertexRgb = vertexRgb;
	b->position = position;
	b->positionPart = positionPart;
	b->positionGroup = positionGroup;
	b->positionCount = used;
	b->triangleCount = result.triangleCount;
	RldRed_Free(&result);

	// The parts as they are now.
	memset(b->groupTriangles, 0, sizeof(b->groupTriangles));
	for (i = 0; i < b->partCount; i++)
	{
		b->parts[i].triangles = 0;
	}
	for (t = 0; t < b->triangleCount; t++)
	{
		const u32 part = b->positionPart[b->vertexPosition[b->triangle[3u * t]]];

		b->parts[part].triangles++;
		b->groupTriangles[b->parts[part].group]++;
	}
	for (i = 0; i < b->partCount; i++)
	{
		RldMk_Bounds(b, (int)i, b->parts[i].lo, b->parts[i].hi);
	}
	RldMk_StepOpenEdges(b);

	b->reduced = 1;
	b->reduceTo = b->triangleCount;
	if (reduceStage != 0u)
	{
		RldMk_Say("info", "model-reduce-fallback", NULL,
		          (reduceStage == 1u) ? "The reduction reached its target only without its guards of color seams and creases (fallback 1 of 2)."
		                              : "The reduction reached its target only without its guards and its color cost (fallback 2 of 2).");
	}
	if (opt->reduceForce && (opt->reduceWhy != NULL))
	{
		RldMk_Say("info", "model-reduced", NULL, "Reduced to fit: %u -> %u triangles, because %s; draw memory %u -> %u bytes.", triangles,
		          b->triangleCount, opt->reduceWhy, triangles * RLDCHAR_DRAW_BYTES_G3, b->triangleCount * RLDCHAR_DRAW_BYTES_G3);
	}
	else
	{
		RldMk_Say("info", opt->maskModel ? "mask-reduced" : "model-reduced", NULL, "Reduced to fit: %u -> %u triangles, draw memory %u -> %u bytes (limit %u).",
		          triangles, b->triangleCount, triangles * RLDCHAR_DRAW_BYTES_G3, b->triangleCount * RLDCHAR_DRAW_BYTES_G3,
		          opt->maskModel ? (u32)RLDCHAR_MASK_DRAW_BYTES_MAX : (u32)RLDCHAR_DRAW_BYTES_MAX);
	}
	return 1;
}

//========================================================================================
// STEP 6: DRAW MEMORY PER FRAME
//========================================================================================
//
// B = 28 x G3 + 40 x GT3 bytes per model and frame (RB:3063-3070, 3088-3090), at
// most RLDCHAR_DRAW_BYTES_MAX (RLDMK_REDUCE_LIMIT untextured triangles: what one frame of the
// model format holds in the worst case, include/rldchar.inc). make-char writes
// G3 only. The number is shown (@char budget), never written into the file.

static int RldMk_StepBudget(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const u32 bytes = b->triangleCount * RLDCHAR_DRAW_BYTES_G3;

	if (((b->triangleCount > (RLDCHAR_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3)) || (bytes > RLDCHAR_DRAW_BYTES_MAX)) && opt->diagnose)
	{
		// --diagnose measures the model as it is: the limit is lifted, said once.
		RldMk_Say("info", "diagnose-lifted", "model-tris", "Lifted for the diagnosis: the model needs %u bytes of draw memory per frame (%u triangles), at most %u.",
		          bytes, b->triangleCount, RLDCHAR_DRAW_BYTES_MAX);
	}
	else if ((b->triangleCount > (RLDCHAR_DRAW_BYTES_MAX / RLDCHAR_DRAW_BYTES_G3)) || (bytes > RLDCHAR_DRAW_BYTES_MAX))
	{
		if (!opt->reduce)
		{
			RldMk_Say("error", "model-tris", NULL,
			          "The model needs %u bytes of draw memory per frame (%u triangles); a driver may use %u (%u triangles). Let rldpack reduce it (--reduce auto; Reload Studio: Reduce to fit), or reduce it in Blender (Decimate modifier).",
			          bytes, b->triangleCount, RLDCHAR_DRAW_BYTES_MAX, (u32)RLDMK_REDUCE_LIMIT);
		}
		else
		{
			RldMk_Say("error", "model-tris", NULL,
			          "The model needs %u bytes of draw memory per frame (%u triangles); a driver may use %u (%u triangles). Reduce it in Blender (Decimate modifier).",
			          bytes, b->triangleCount, RLDCHAR_DRAW_BYTES_MAX, (u32)RLDMK_REDUCE_LIMIT);
		}
		return 0;
	}

	if (b->triangleCount > RLDMK_HI_TRIANGLES_MAX)
	{
		RldMk_Say("note", "model-tris-info", NULL,
		          "%u triangles - more than any original driver (564); the game gives a custom driver draw memory of its own.", b->triangleCount);
	}

	if (opt->twoSided)
	{
		RldMk_Say("note", "draw-both-sides", NULL, "Both sides are drawn, so the model always costs its full %u bytes per frame instead of about half.", bytes);
	}
	else if ((b->openEdges != 0u) && opt->openOneSided)
	{
		RldMk_Say("note", "ply-open-edges", NULL,
		          "The mesh has %u open edges; you may see through it from some sides. Close the holes in the model, or draw open parts from both sides (--open-parts two-sided, the default; Reload Studio: Draw open parts from both sides).",
		          b->openEdges);
	}
	else if (b->openEdges != 0u)
	{
		RldMk_Say("note", "ply-open-edges", NULL,
		          "The mesh has %u open edges; the parts they belong to are drawn from both sides (no extra triangles). Close the holes in the model to have them culled.",
		          b->openEdges);
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
// (These are the poses of --fit none, about the hip; with --fit crash the
// dummy's poses apply instead, RldDum_PoseFrame.)
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

// The sines and cosines of a pose: pitch, yaw, roll.
static void RldMk_PoseTrig(const struct RldMkPose *pose, double trig[6])
{
	RldMk_SinCos(pose->pitch, &trig[0], &trig[1]);
	RldMk_SinCos(pose->yaw, &trig[2], &trig[3]);
	RldMk_SinCos(pose->roll, &trig[4], &trig[5]);
}

static void RldMk_ApplyPoseTrig(const struct RldMkPose *pose, const double trig[6], const double in[3], double out[3])
{
	const double sp = trig[0];
	const double cp = trig[1];
	const double sy = trig[2];
	const double cy = trig[3];
	const double sr = trig[4];
	const double cr = trig[5];
	double x = in[0];
	double y = (in[1] - RLDMK_HIP_Y) * pose->stretch;
	double z = in[2];
	double t;

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

static void RldMk_ApplyPose(const struct RldMkPose *pose, const double in[3], double out[3])
{
	double trig[6];

	RldMk_PoseTrig(pose, trig);
	RldMk_ApplyPoseTrig(pose, trig, in, out);
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
		struct RldDumPose dummyPose;

		RldMk_FramePose(f, &pose);
		RldDum_PoseFrame(f, &dummyPose);
		if (b->dummy)
		{
			RldDum_PoseFit(&dummyPose, &b->dumFit);
		}

		for (p = 0; p < b->positionCount; p++)
		{
			const double *neutral = &b->position[3u * p];
			double *out = &b->frame[((size_t)f * b->positionCount + p) * 3u];
			const int group = b->positionGroup[p];

			if (!b->posed || (group == RLDMK_GROUP_KART) || (!b->dummy && (group == RLDMK_GROUP_WHEEL) && !pose.wheel))
			{
				memcpy(out, neutral, 3u * sizeof(double));
			}
			else if (b->dummy)
			{
				// on the dummy: about its seat and steering column (RldDum_ApplyPose)
				RldDum_ApplyPose(&dummyPose, group, neutral, out);
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
// STEP 4a: --size - THE DRIVER, LARGER OR SMALLER
//========================================================================================
//
// p' = H + s x (p - H) for every position of the driver and the steering wheel,
// H the point the poses turn about, in game units: with --fit crash the seat
// of the reference kart (RldDum_Fit), with --wheels off the bottom of the
// driver, with --fit none the hip (0, RLDMK_HIP_Y, 0). The kart keeps its size: the game draws the wheels as sprites at
// fixed points of the kart (game/DrawTires.c:335-354), and physics and
// collision follow the driving style, not the model. So the size is a look,
// not a value: no field in the file, only the corners move.
//
// THE RANGE a model takes is worked out before: every check of the chain that
// depends on the size, and only those. They are three; everything else
// (triangles, records, vertex cache, frame size, draw memory, the model rules
// other than model-coords) counts the same at any size:
//   * model-scale: ModelHeader.scale is s16 per axis - the hull of all 47
//     frames, (hi - lo) x 4096 / 255 (254 up), stays <= 32767 (step 9)
//   * model-coords: ModelFrame.pos and pos + 255 stay in -8192..8191 (step 9, P10)
//   * model-scale: the model stays within 5 times of 1.8 Blender units long (step 4)
//   * on the dummy: no corner of the driver below the ground (y 0) in any pose
// The poses are rotations and a stretch about the same point H, so a posed
// driver corner at size s is H + s x (its pose at 100 % - H): the hull of all
// frames at any s follows from one pass over the frames at 100 %.

#define RLDMK_SIZE_MIN 50u
#define RLDMK_SIZE_MAX 200u
#define RLDMK_SIZE_TEXT "visual size only - physics and collision follow the driving style"

struct RldMkSizeHull
{
	int moves;          // there are driver or steering wheel corners
	int ground;         // on the dummy: no corner may go below the ground (y 0)
	double pivot[3];    // --size scales about this point: the hip, or the dummy's seat
	double kartLo[3];   // the kart, game units (the same in every frame)
	double kartHi[3];
	double lo[3];       // driver and steering wheel over all frames, relative to the pivot
	double hi[3];
	double neutralLo;   // z of driver and steering wheel in the neutral pose, for the length
	double neutralHi;
};

static void RldMk_SizeHull(const struct RldMkBuild *b, struct RldMkSizeHull *hull)
{
	u32 f;
	u32 p;
	int a;

	memset(hull, 0, sizeof(*hull));
	for (a = 0; a < 3; a++)
	{
		hull->kartLo[a] = 1.0e30;
		hull->kartHi[a] = -1.0e30;
		hull->lo[a] = 1.0e30;
		hull->hi[a] = -1.0e30;
	}
	hull->neutralLo = 1.0e30;
	hull->neutralHi = -1.0e30;
	hull->ground = b->dummy;
	if (b->dummy)
	{
		memcpy(hull->pivot, b->dumFit100.seat, sizeof(hull->pivot));
	}
	else
	{
		hull->pivot[1] = RLDMK_HIP_Y;
	}

	for (p = 0; p < b->positionCount; p++)
	{
		const double *v = &b->position[3u * p];

		if (b->positionGroup[p] == RLDMK_GROUP_KART)
		{
			for (a = 0; a < 3; a++)
			{
				hull->kartLo[a] = (v[a] < hull->kartLo[a]) ? v[a] : hull->kartLo[a];
				hull->kartHi[a] = (v[a] > hull->kartHi[a]) ? v[a] : hull->kartHi[a];
			}
		}
		else
		{
			hull->moves = 1;
			hull->neutralLo = ((v[2] - hull->pivot[2]) < hull->neutralLo) ? (v[2] - hull->pivot[2]) : hull->neutralLo;
			hull->neutralHi = ((v[2] - hull->pivot[2]) > hull->neutralHi) ? (v[2] - hull->pivot[2]) : hull->neutralHi;
		}
	}

	if (!hull->moves)
	{
		return;
	}

	// Without poses every frame is the neutral one: one pass is enough.
	for (f = 0; f < (b->posed ? (u32)RLDMK_FRAMES_TOTAL : 1u); f++)
	{
		struct RldMkPose pose;
		struct RldDumPose dummyPose;
		double trig[6];

		RldMk_FramePose(f, &pose);
		RldMk_PoseTrig(&pose, trig);
		RldDum_PoseFrame(f, &dummyPose);
		if (b->dummy)
		{
			// at 100 %: a posed corner at size s is seat + s x (this - seat)
			RldDum_PoseFit(&dummyPose, &b->dumFit100);
		}

		for (p = 0; p < b->positionCount; p++)
		{
			const double *neutral = &b->position[3u * p];
			const int group = b->positionGroup[p];
			double moved[3];

			if (group == RLDMK_GROUP_KART)
			{
				continue;
			}

			if (!b->posed || (!b->dummy && (group == RLDMK_GROUP_WHEEL) && !pose.wheel))
			{
				memcpy(moved, neutral, sizeof(moved));
			}
			else if (b->dummy)
			{
				RldDum_ApplyPose(&dummyPose, group, neutral, moved);
			}
			else
			{
				RldMk_ApplyPoseTrig(&pose, trig, neutral, moved);
			}

			for (a = 0; a < 3; a++)
			{
				moved[a] -= hull->pivot[a];
			}
			for (a = 0; a < 3; a++)
			{
				hull->lo[a] = (moved[a] < hull->lo[a]) ? moved[a] : hull->lo[a];
				hull->hi[a] = (moved[a] > hull->hi[a]) ? moved[a] : hull->hi[a];
			}
		}
	}
}

// Does `percent` fit? 1 = yes; 0 = no, *why says which check (English, for the message).
static int RldMk_SizeFits(const struct RldMkSizeHull *hull, u32 percent, char *why, size_t whySize)
{
	static const char *const axisNames[3] = {"wide", "tall", "long"};
	static const double spans[3] = {255.0, 254.0, 255.0};
	const double s = (double)percent / 100.0;
	const double *hip = hull->pivot;
	double length;
	int a;

	if (!hull->moves)
	{
		return 1;
	}

	// on the dummy: the lowest corner of all frames at size s is
	// seat + s x (its height at 100 % - seat), the jump's stretch included
	if (hull->ground && ((hip[1] + s * hull->lo[1]) < 0.0))
	{
		snprintf(why, whySize, "a corner of the driver would go %.1f game units below the ground in some pose", -(hip[1] + s * hull->lo[1]));
		return 0;
	}

	for (a = 0; a < 3; a++)
	{
		const double driverLo = hip[a] + s * hull->lo[a];
		const double driverHi = hip[a] + s * hull->hi[a];
		const double lo = (driverLo < hull->kartLo[a]) ? driverLo : hull->kartLo[a];
		const double hi = (driverHi > hull->kartHi[a]) ? driverHi : hull->kartHi[a];
		const double needed = RldMk_Ceil((hi - lo) * 4096.0 / spans[a]);
		int scale;
		int origin;
		double low;

		// as step 9 (RldMk_StepQuantize)
		if (needed > 32767.0)
		{
			snprintf(why, whySize, "the model would be %.1f model units %s, more than the game's 16-bit model scale holds", (hi - lo) / RLDMK_UNITS,
			         axisNames[a]);
			return 0;
		}

		scale = (needed < 1.0) ? 1 : (int)needed;
		low = lo * 4096.0 / (double)scale;
		origin = (a == 1) ? (2 * (int)RldMk_Round(low / 2.0)) : (int)RldMk_Round(low);
		if ((origin < RLDCHAR_COORD_MIN) || ((origin + RLDCHAR_RECORD_SPAN) > RLDCHAR_COORD_MAX))
		{
			snprintf(why, whySize, "the model would leave the game's coordinate range");
			return 0;
		}
	}

	// as step 4 (RldMk_StepParts): the whole model in the neutral pose
	{
		const double driverLo = hip[2] + s * hull->neutralLo;
		const double driverHi = hip[2] + s * hull->neutralHi;
		const double lo = (driverLo < hull->kartLo[2]) ? driverLo : hull->kartLo[2];
		const double hi = (driverHi > hull->kartHi[2]) ? driverHi : hull->kartHi[2];

		length = (hi - lo) / RLDMK_UNITS;
		if ((length < (RLDMK_DRIVER_LENGTH / 5.0)) || (length > (RLDMK_DRIVER_LENGTH * 5.0)))
		{
			snprintf(why, whySize, "the model would be %.2f model units long; a character is %.2f to %.2f long", length, RLDMK_DRIVER_LENGTH / 5.0,
			         RLDMK_DRIVER_LENGTH * 5.0);
			return 0;
		}
	}

	return 1;
}

static int RldMk_StepSize(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const u32 percent = (opt->sizePercent != 0u) ? opt->sizePercent : 100u;
	struct RldMkSizeHull hull;
	char why[160];
	u32 low = 0;
	u32 high = 0;
	u32 p;
	double s;

	// The measures as exported, for ply-axes in RldMk_StepMeasure (the same
	// formulas as b->size there).
	{
		double lo[3];
		double hi[3];

		RldMk_Bounds(b, -1, lo, hi);
		b->exported[0] = (hi[2] - lo[2]) / RLDMK_UNITS;
		b->exported[1] = (b->parts[b->kartPart].hi[0] - b->parts[b->kartPart].lo[0]) / RLDMK_UNITS;
		b->exported[2] = (hi[1] - lo[1]) / RLDMK_UNITS;
		b->exported[3] = (lo[1] - (b->dummy ? b->dumFit.shift[1] : 0.0)) / RLDMK_UNITS;  // before the dummy's shift
	}

	// THE RANGE: the run of fitting percentages around 100 (or the first run
	// when 100 itself does not fit). Every percentage is checked on its own below.
	RldMk_SizeHull(b, &hull);
	if (RldMk_SizeFits(&hull, 100u, why, sizeof(why)))
	{
		low = 100u;
	}
	else
	{
		for (p = RLDMK_SIZE_MIN; (p <= RLDMK_SIZE_MAX) && (low == 0u); p++)
		{
			if (RldMk_SizeFits(&hull, p, why, sizeof(why)))
			{
				low = p;
			}
		}
	}

	if (low != 0u)
	{
		high = low;
		while ((low > RLDMK_SIZE_MIN) && RldMk_SizeFits(&hull, low - 1u, why, sizeof(why)))
		{
			low--;
		}
		while ((high < RLDMK_SIZE_MAX) && RldMk_SizeFits(&hull, high + 1u, why, sizeof(why)))
		{
			high++;
		}
	}

	b->sizeLow = low;
	b->sizeHigh = high;

	if (!s_rldMkQuiet)
	{
		char lowText[16];
		char highText[16];

		snprintf(lowText, sizeof(lowText), "%u", low);
		snprintf(highText, sizeof(highText), "%u", high);
		Rld_Emit("value", "size-range", lowText, highText, (const char *)NULL);
	}

	// 100 %: nothing moves, the bytes are the ones without --size. On the
	// dummy 100 % is checked as well (below): a driver whose lowest corner
	// goes below the ground in some pose is refused at 100 % too - the author
	// then chooses a size in the range on purpose.
	if ((percent == 100u) && (!hull.ground || RldMk_SizeFits(&hull, percent, why, sizeof(why))))
	{
		return 1;
	}

	if (!RldMk_SizeFits(&hull, percent, why, sizeof(why)))
	{
		if (low == 0u)
		{
			RldMk_Say("error", "char-size", NULL, "Size %u%% does not fit this model at any size: %s.", percent, why);
		}
		else
		{
			RldMk_Say("error", "char-size", NULL, "Size %u%% is outside %u..%u%% for this model: %s. Choose a size in that range.", percent, low, high, why);
		}
		return 0;
	}

	s = (double)percent / 100.0;
	for (p = 0; p < b->positionCount; p++)
	{
		double *v = &b->position[3u * p];
		int a;

		if (b->positionGroup[p] == RLDMK_GROUP_KART)
		{
			continue;
		}

		for (a = 0; a < 3; a++)
		{
			v[a] = hull.pivot[a] + s * (v[a] - hull.pivot[a]);
		}
	}

	for (p = 0; p < b->partCount; p++)
	{
		RldMk_Bounds(b, (int)p, b->parts[p].lo, b->parts[p].hi);
	}

	return 1;
}

// Step 5 on the model as it will be: size and centering, in Blender units.
// With --size other than 100 the length check of step 4 runs again (a model
// whose size passed RldMk_SizeFits passes it).
static int RldMk_StepMeasure(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	const int best = b->kartPart;
	double lo[3];
	double hi[3];

	RldMk_Bounds(b, -1, lo, hi);
	b->size[0] = (hi[2] - lo[2]) / RLDMK_UNITS;
	b->size[1] = (b->parts[best].hi[0] - b->parts[best].lo[0]) / RLDMK_UNITS;
	b->size[2] = (hi[1] - lo[1]) / RLDMK_UNITS;
	b->size[3] = lo[1] / RLDMK_UNITS;

	if ((opt->sizePercent != 0u) && (opt->sizePercent != 100u) &&
	    ((b->size[0] < (RLDMK_DRIVER_LENGTH / 5.0)) || (b->size[0] > (RLDMK_DRIVER_LENGTH * 5.0))))
	{
		RldMk_Say("error", "char-size", NULL, "Size %u%% makes the model %.2f model units long; a character is %.2f to %.2f long. Choose a smaller size.",
		          opt->sizePercent, b->size[0], RLDMK_DRIVER_LENGTH / 5.0, RLDMK_DRIVER_LENGTH * 5.0);
		return 0;
	}

	// Scale and axes are a question of the export: checked on the model as
	// exported (b->exported, before --size), so that a large --size does not
	// send the author back to Blender. At 100 % both are the same.
	{
		const double *e = b->exported;

		// With --wheels off the model brings its own vehicle (a motorbike,
		// say): it need not be as wide as a retail kart.
		const int narrow = !opt->noWheels && ((e[1] < 0.8) || (e[1] > 1.0));

		if (((e[0] < RLDMK_KART_LENGTH_MIN) || (e[0] > RLDMK_KART_LENGTH_MAX) || narrow || (e[2] < 0.6) || (e[2] > 2.7) ||
		     (e[3] < RLDMK_KART_BOTTOM_MIN) || (e[3] > RLDMK_KART_BOTTOM_MAX)) &&
		    b->fitted)
		{
			// The scale is the fit's own: only the shape and the axes are left to check.
			RldMk_Say("warning", "ply-axes", NULL,
			          "Fitted to Crash size (x%.4g), the model is %.2f long, %.2f wide and %.2f tall and starts %.2f above the ground (model units; Blender: meters); Crash with his kart is %.2f long and %.2f tall, a retail kart %.2f wide. Check the axes (+Y up, +Z forward) and the kart. On the command line, --fit none shows the exported size.",
			          b->fitFactor, e[0], e[1], e[2], e[3], RLDMK_CRASH_LENGTH / RLDMK_UNITS, RLDMK_CRASH_HEIGHT / RLDMK_UNITS,
			          (double)RLDMK_RETAIL_KART_WIDTH / RLDMK_UNITS);
		}
		else if ((e[0] < RLDMK_KART_LENGTH_MIN) || (e[0] > RLDMK_KART_LENGTH_MAX) || narrow || (e[2] < 0.6) || (e[2] > 2.7) ||
		         (e[3] < RLDMK_KART_BOTTOM_MIN) || (e[3] > RLDMK_KART_BOTTOM_MAX))
		{
			RldMk_Say("warning", "ply-axes", NULL,
			          "The model is %.2f long, %.2f wide and %.2f tall and starts %.2f above the ground (model units; Blender: meters). A driver with kart is about 1.8 long, 0.9 wide and 0.9-2.3 tall. Check scale and axes (+Y up, +Z forward).",
			          e[0], e[1], e[2], e[3]);
		}
	}

	{
		// As exported: before the dummy's shift (RldMk_StepDummy) and in the
		// units of the file - the factor of --fit crash (or --scale) taken
		// back, so that the number is the one the author sees in Blender.
		// With the dummy the kart is centered for the game anyway: then it is
		// only worth knowing.
		const double fitted = (0.5 * (b->parts[best].lo[0] + b->parts[best].hi[0]) - (b->dummy ? b->dumFit.shift[0] : 0.0)) / RLDMK_UNITS;
		const double center = (opt->scale > 0.0) ? (fitted / opt->scale) : fitted;

		b->center = center;
		if (((fitted > 0.08) || (fitted < -0.08)) && b->dummy)
		{
			RldMk_Say("info", "ply-center", NULL, "The kart sits %.2f model units to the side of x = 0; it is centered for the game - nothing to do.",
			          (center < 0.0) ? -center : center);
		}
		else if ((fitted > 0.08) || (fitted < -0.08))
		{
			RldMk_Say("warning", "ply-center", NULL, "The kart sits %.2f model units to the side of x = 0. Center it on x = 0.", (center < 0.0) ? -center : center);
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

// One value in record steps (u = v x 4096 / scale - pos) -> its byte: X and Z
// 0..255, Y even 0..254. *cut = 1 when it lies beyond and was cut off. By the
// choice of scale and pos below this cannot happen: the span of every axis
// over all 47 frames fits the byte (scale = ceil(span x 4096 / 255, Y 254)),
// and pos is lo rounded, so the lowest value is at least -0.5 (Y -1) and the
// highest below 255.5 (Y 255) - both round inside. Only rounding of the last
// bit of a double could reach the ends, so it is still checked (model-quant).
static s64 RldMk_QuantValue(double u, int axis, int *cut)
{
	s64 q;

	if (axis == 1)
	{
		q = RldMk_Round(u / 2.0);
		*cut = (q < 0) || (q > 127);
		q = (q < 0) ? 0 : ((q > 127) ? 127 : q);
		return 2 * q;
	}

	q = RldMk_Round(u);
	*cut = (q < 0) || (q > 255);
	return (q < 0) ? 0 : ((q > 255) ? 255 : q);
}

// --diagnose: one more value cut off. 0 = out of memory.
static int RldMk_DiagClamp(struct RldMkBuild *b, u32 frame, u32 position, u32 axis, double value)
{
	struct RldMkClamp *clamp;

	if (b->diagClampCount == b->diagClampRoom)
	{
		const u32 room = (b->diagClampRoom != 0u) ? (2u * b->diagClampRoom) : 64u;
		struct RldMkClamp *grown = (struct RldMkClamp *)realloc(b->diagClamp, (size_t)room * sizeof(struct RldMkClamp));

		if (grown == NULL)
		{
			return 0;
		}
		b->diagClamp = grown;
		b->diagClampRoom = room;
	}

	clamp = &b->diagClamp[b->diagClampCount++];
	clamp->frame = frame;
	clamp->position = position;
	clamp->axis = axis;
	clamp->value = value;
	return 1;
}

static int RldMk_StepQuantize(struct RldMkBuild *b)
{
	static const char *const axisNames[3] = {"wide", "tall", "long"};
	static const double spans[3] = {255.0, 254.0, 255.0};
	double lo[3];
	double hi[3];
	size_t i;
	const size_t total = (size_t)RLDMK_FRAMES_TOTAL * b->positionCount;
	size_t cutFirst = 0;
	u32 cuts = 0;
	int cutAxis = 0;
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
			          "The model is %.1f model units %s - too large for a driver. Export it in meters (Blender: 1 unit = 1 meter); a driver with kart is about 1.8 units long.",
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
			int cut;
			const s64 q = RldMk_QuantValue(u, a, &cut);

			if (cut)
			{
				if (cuts == 0u)
				{
					cutFirst = i;
					cutAxis = a;
				}
				cuts++;

				// --diagnose: every value cut off, as a clamp line.
				if (b->diagnose && !RldMk_DiagClamp(b, (u32)(i / b->positionCount), (u32)(i % b->positionCount), (u32)a, u))
				{
					return RldMk_OutOfMemory();
				}
			}

			// stored X, Z (forward), Y (up)
			b->quant[3u * i + ((a == 0) ? 0u : ((a == 1) ? 2u : 1u))] = (u8)q;
		}
	}

	// Never cut off in silence: the corner would sit elsewhere in the game.
	if (cuts != 0u)
	{
		char technical[96];

		snprintf(technical, sizeof(technical), "first: frame %u, position %u, axis %c", (u32)(cutFirst / b->positionCount),
		         (u32)(cutFirst % b->positionCount), "xyz"[cutAxis]);
		if (b->diagnose)
		{
			RldMk_Say("info", "diagnose-lifted", "model-quant", "Kept for the diagnosis: %u packed values fall outside their byte and were cut off (%s).", cuts,
			          technical);
		}
		else
		{
			RldMk_Say("error", "model-quant", technical,
			          "Internal error: %u packed values of the model fall outside their byte and would be cut off. Please report it.", cuts);
			return 0;
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

// Is triangle t one of RldMk_StepFlips (drawn without the cull bit)?
static int RldMk_BothSides(const struct RldMkBuild *b, u32 t)
{
	return (b->bothSides != NULL) && (b->bothSides[t] != 0u);
}

// Triangles that lose their area in the packed neutral frame are left out: the
// renderer would not draw them (NCLIP 0, RB:2891-2896). One that turns over
// there but keeps an area stays and is drawn from both sides (RldMk_StepFlips,
// as in the other frames).
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

		if (!(RldMk_Dot(n, n) > 0.0) || !(RldMk_Dot(exact, exact) > 0.0))
		{
			b->collapsed++;
			continue;
		}

		if (kept != t)
		{
			memcpy(&b->triangle[3u * kept], &b->triangle[3u * t], 3u * sizeof(u32));
			if (b->triangleReduced != NULL)
			{
				b->triangleReduced[kept] = b->triangleReduced[t];
			}
			if (b->triangleOpen != NULL)
			{
				b->triangleOpen[kept] = b->triangleOpen[t];
			}
		}
		kept++;
	}

	b->triangleCount = kept;

	if (b->collapsed != 0u)
	{
		RldMk_Say("warning", "model-collapse", NULL, "%u triangles lose their area after packing and are left out.", b->collapsed);
	}

	if (kept == 0u)
	{
		RldMk_Say("error", "ply-no-faces", NULL, "The model has no faces. Export the mesh, not only its points.");
		return 0;
	}

	return 1;
}

// Thin triangles that turn over by the packing in some other frame (a pose
// moves them across the grid of the bytes): the game would cull them from the
// side they are seen from, a hole of a pixel or two. They get no cull bit
// (RldMk_DrawTriangle) - drawn from both sides they cost no more draw memory,
// and turned over they are still in place. Seen from behind such a triangle
// is a sliver within one step of the grid. The triangles of parts still open
// after the repair are drawn so as well (--open-parts two-sided, the default).
// With --two-sided every triangle is drawn so already.
static int RldMk_StepFlips(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	u32 t;
	u32 f;

	b->bothSidesCount = 0;
	b->openTwoSided = 0;
	b->quantFlips = 0;
	if (opt->twoSided)
	{
		return 1;
	}

	b->bothSides = (u8 *)calloc((size_t)b->triangleCount + 1u, 1u);
	if (b->bothSides == NULL)
	{
		return RldMk_OutOfMemory();
	}

	for (t = 0; t < b->triangleCount; t++)
	{
		const u32 p0 = b->vertexPosition[b->triangle[3u * t + 0u]];
		const u32 p1 = b->vertexPosition[b->triangle[3u * t + 1u]];
		const u32 p2 = b->vertexPosition[b->triangle[3u * t + 2u]];

		// a part still open after the repair (--open-parts two-sided)
		if (!opt->openOneSided && (b->triangleOpen != NULL) && b->triangleOpen[t])
		{
			b->bothSides[t] = 1u;
			b->bothSidesCount++;
			b->openTwoSided++;
		}

		for (f = 0; (f < RLDMK_FRAMES_TOTAL) && !b->bothSides[t]; f++)
		{
			double packed[3][3];
			double n[3];
			double m[3];

			RldMk_Decoded(b, f, p0, packed[0]);
			RldMk_Decoded(b, f, p1, packed[1]);
			RldMk_Decoded(b, f, p2, packed[2]);
			RldMk_Cross(packed[0], packed[1], packed[2], n);
			RldMk_Cross(RldMk_At(b, f, p0), RldMk_At(b, f, p1), RldMk_At(b, f, p2), m);
			if (RldMk_Dot(n, m) <= 0.0)
			{
				b->bothSides[t] = 1u;
				b->bothSidesCount++;
				b->quantFlips++;
			}
		}
	}

	if (b->quantFlips != 0u)
	{
		RldMk_Say("note", "model-quant-flip", NULL, "%u thin triangles turn over by the packing in some frames; they are drawn from both sides.",
		          b->quantFlips);
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

// THE PALETTE of at most `target` colors for the distinct colors[0..unique)
// (sorted by value, with their weights): kept as they are when they fit, else
// median cut and two k-means rounds; entries nobody comes to are dropped, the
// order stays. Sets colors[i].palette; returns the number of entries. Used for
// the model (step 7) and the portrait (step 14).
static u32 RldMk_Palette(struct RldMkColor *colors, u32 unique, u32 target, u32 palette[RLDCHAR_COLORS_MAX])
{
	u32 remap[RLDCHAR_COLORS_MAX];
	u32 count = 0;
	u32 i;

	if (unique <= target)
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

		while (boxes < target)
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

	return count;
}

static int RldMk_StepColors(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	const u32 corners = 3u * b->triangleCount;
	struct RldMkColor *colors;
	u32 *rgb;
	u32 palette[RLDCHAR_COLORS_MAX];
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
		const u8 *c = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * b->triangle[i]] : &ply->rgb[3u * b->triangle[i]];

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

	count = RldMk_Palette(colors, unique, opt->colorTarget, palette);

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
		RldMk_Say("note", "ply-uv-unused", NULL,
		          "The model has texture coordinates, but a PLY file brings no textures - only the vertex colors are used. To use textures, export as OBJ with its MTL file and images.");
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

	if (s->cull && !RldMk_BothSides(b, triangle))
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
	const int *origin;      // 3 per frame, RLDMK_FRAMES_TOTAL frames (or those of `frames`)
	const u8 *records;      // per frame recordCount x 3 bytes: X, Z, Y
	u32 animCount;          // 0: a character - RLDCHAR_ANIM_COUNT animations of s_rldCharFrames
	const u16 *frames;      // with animCount: frames per animation
	const char *const *animNames;
	const char *modelName;  // with animCount: Model.name and ModelHeader.name
	const char *headerName;
};

static u32 RldMk_FrameSize(u32 records)
{
	return (RLDMK_FRAME_HEADER + RLDMK_RECORD_BYTES * records + 3u) & ~3u;
}

// The model body as the renderer reads it. A character has the 4 animations
// of the templates (layout animCount 0); the own mask passes its own one
// animation: the command list then starts right behind its shorter table.
static u8 *RldMk_WriteCmdl(const struct RldMkLayout *in, size_t *sizeOut)
{
	const u32 animCount = (in->animCount != 0u) ? in->animCount : (u32)RLDCHAR_ANIM_COUNT;
	const u16 *frames = (in->animCount != 0u) ? in->frames : s_rldCharFrames;
	const char *const *animNames = (in->animCount != 0u) ? in->animNames : s_rldMkAnimNames;
	const char *modelName = (in->animCount != 0u) ? in->modelName : RLDMK_MODEL_NAME_TEXT;
	const char *headerName = (in->animCount != 0u) ? in->headerName : RLDMK_HEADER_NAME_TEXT;
	const u32 commandsAt = RLDMK_BODY_ANIM_TABLE + 4u * animCount;
	const u32 mapEntries = 4u + animCount;
	const u32 frameSize = RldMk_FrameSize(in->recordCount);
	const u32 colorsAt = commandsAt + 4u * (in->commandCount + 2u);
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
	if (animCount > (u32)RLDCHAR_ANIM_COUNT)
	{
		return NULL;
	}

	animAt[0] = colorsAt + 4u * in->colorCount;
	for (a = 0; a < (int)animCount; a++)
	{
		animAt[a + 1] = animAt[a] + RLDMK_ANIM_BYTES + (u32)frames[a] * frameSize;
	}
	bodySize = animAt[animCount];

	size = 8u + (size_t)bodySize + 4u * mapEntries;
	out = (u8 *)calloc(1, size);
	if (out == NULL)
	{
		return NULL;
	}

	Rld_WriteLE32(&out[0], bodySize);
	body = &out[4];

	// Model (model-head): a name of its own, id -1, one header.
	memcpy(&body[0], modelName, strlen(modelName));
	Rld_WriteLE16(&body[RLDMK_MODEL_ID], 0xFFFFu);
	Rld_WriteLE16(&body[RLDMK_MODEL_NUM_HEADERS], 1u);
	Rld_WriteLE32(&body[RLDMK_MODEL_HEADERS], RLDMK_BODY_HEADER);

	// ModelHeader (model-headers, model-lod, model-zero): maxDistanceLOD 8192 as the retail drivers,
	// flags 0, no static frame, no texture table, no texture animation.
	header = &body[RLDMK_BODY_HEADER];
	memcpy(&header[0], headerName, strlen(headerName));
	Rld_WriteLE16(&header[RLDMK_HEADER_LOD], (u32)RLDCHAR_LOD_MIN);
	for (a = 0; a < 3; a++)
	{
		Rld_WriteLE16(&header[RLDMK_HEADER_SCALE + 2u * (u32)a], (u32)in->scale[a]);
	}
	Rld_WriteLE32(&header[RLDMK_HEADER_COMMANDS], commandsAt);
	Rld_WriteLE32(&header[RLDMK_HEADER_COLORS], colorsAt);
	Rld_WriteLE32(&header[RLDMK_HEADER_NUM_ANIMS], animCount);
	Rld_WriteLE32(&header[RLDMK_HEADER_ANIMS], RLDMK_BODY_ANIM_TABLE);

	for (a = 0; a < (int)animCount; a++)
	{
		Rld_WriteLE32(&body[RLDMK_BODY_ANIM_TABLE + 4u * (u32)a], animAt[a]);
	}

	Rld_WriteLE32(&body[commandsAt], in->colorCount);
	for (i = 0; i < in->commandCount; i++)
	{
		Rld_WriteLE32(&body[commandsAt + 4u + 4u * i], in->commands[i]);
	}
	Rld_WriteLE32(&body[commandsAt + 4u + 4u * in->commandCount], RLDMK_CMD_END);

	for (i = 0; i < in->colorCount; i++)
	{
		Rld_WriteLE32(&body[colorsAt + 4u * i], in->colors[i]);
	}

	for (a = 0; a < (int)animCount; a++)
	{
		u8 *anim = &body[animAt[a]];
		u32 k;

		memcpy(anim, animNames[a], strlen(animNames[a]));
		Rld_WriteLE16(&anim[RLDMK_ANIM_NUM_FRAMES], (u32)frames[a]);
		Rld_WriteLE16(&anim[RLDMK_ANIM_FRAME_SIZE], frameSize);

		for (k = 0; k < (u32)frames[a]; k++)
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

	// The map: headers, ptrCommandList, ptrColors, ptrAnimations, then one
	// ModelAnim* per animation.
	map = &out[4u + (size_t)bodySize];
	Rld_WriteLE32(&map[0], 4u * mapEntries);
	Rld_WriteLE32(&map[4u], RLDMK_MODEL_HEADERS);
	Rld_WriteLE32(&map[8u], RLDMK_BODY_HEADER + RLDMK_HEADER_COMMANDS);
	Rld_WriteLE32(&map[12u], RLDMK_BODY_HEADER + RLDMK_HEADER_COLORS);
	Rld_WriteLE32(&map[16u], RLDMK_BODY_HEADER + RLDMK_HEADER_ANIMS);
	for (i = 0; i < animCount; i++)
	{
		Rld_WriteLE32(&map[20u + 4u * i], RLDMK_BODY_ANIM_TABLE + 4u * i);
	}

	*sizeOut = size;
	return out;
}

static const char *const s_rldMkMaskAnimNames[RLDCHAR_MASK_ANIM_COUNT] = {"still"};

static int RldMk_StepWrite(struct RldMkBuild *b)
{
	struct RldMkLayout layout;
	int origins[3 * RLDMK_FRAMES_TOTAL];
	const size_t limit = b->maskModel ? (size_t)RLDMK_MASK_CMDL_MAX : (size_t)RLDCHAR_LIMIT_CMDL;
	u8 *records;
	u32 f;
	u32 r;

	b->framesWritten = b->maskModel ? 1u : (u32)RLDMK_FRAMES_TOTAL;

	b->frameSize = RldMk_FrameSize(b->recordCount);
	if (b->frameSize > 0xFFFFu)
	{
		RldMk_Say("error", "model-frame", NULL, "One animation frame needs %u bytes; at most 65535 fit. Reduce the model.", b->frameSize);
		b->recordLimit = 1;
		return 0;
	}

	records = (u8 *)malloc((size_t)RLDMK_FRAMES_TOTAL * b->recordCount * 3u + 1u);
	if (records == NULL)
	{
		return RldMk_OutOfMemory();
	}

	for (f = 0; f < b->framesWritten; f++)
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
	if (b->maskModel)
	{
		layout.animCount = (u32)RLDCHAR_MASK_ANIM_COUNT;
		layout.frames = s_rldCharMaskFrames;
		layout.animNames = s_rldMkMaskAnimNames;
		layout.modelName = RLDMK_MASK_MODEL_NAME_TEXT;
		layout.headerName = RLDMK_MASK_HEADER_NAME_TEXT;
	}

	b->cmdl = RldMk_WriteCmdl(&layout, &b->cmdlSize);
	free(records);
	if (b->cmdl == NULL)
	{
		return RldMk_OutOfMemory();
	}

	b->drawBytes = b->drawCount * RLDCHAR_DRAW_BYTES_G3;

	if ((b->cmdlSize > limit) && b->diagnose)
	{
		RldMk_Say("info", "diagnose-lifted", "mem-model", "Lifted for the diagnosis: the model is %u KB, at most %u KB.", (u32)((b->cmdlSize + 1023u) / 1024u),
		          (u32)(RLDCHAR_LIMIT_CMDL / 1024u));
	}
	else if ((b->cmdlSize > limit) && b->maskModel)
	{
		RldMk_Say("error", "mask-memory", NULL, "The mask model is %u bytes; a mask may be at most %u. Reduce its triangles.", (u32)b->cmdlSize, (u32)limit);
		b->recordLimit = 1;
		return 0;
	}
	else if (b->cmdlSize > limit)
	{
		RldMk_Say("error", "mem-model", NULL, "The model is %u KB; a character model may be at most %u KB. Reduce the triangles.",
		          (u32)((b->cmdlSize + 1023u) / 1024u), (u32)(RLDCHAR_LIMIT_CMDL / 1024u));
		b->recordLimit = 1;
		return 0;
	}

	return 1;
}

//========================================================================================
// STEP 15: THE COUNTER-CHECK
//========================================================================================

// One record of a frame as the renderer unpacks it (RB:2437-2467): X = (byte0 +
// (pos.x & 0x7fff)) << 2 in 16 bits, Y = ((byte2 + pos.y) << 2) with bit 0 of the
// sum cleared (mask 0xfff8ffff), Z = (pos.z + byte1) << 2. These are the 16-bit
// values the GTE gets; game units = value x ModelHeader.scale / 16384. `frame`
// points at the ModelFrame.
static void RldMk_UnpackRecord(const u8 *frame, u32 record, int out[3])
{
	const u32 posX = Rld_ReadLE16(&frame[0]);
	const u32 posY = Rld_ReadLE16(&frame[2]);
	const u32 posZ = Rld_ReadLE16(&frame[4]);
	const u8 *q = &frame[Rld_ReadLE32(&frame[RLDMK_FRAME_VERTEX_OFFSET]) + 3u * record];
	const u32 originXY = (posX & 0x7FFFu) | (posY << 16);
	const u32 vertexXZ = (u32)q[0] | ((u32)q[2] << 16);
	const u32 packedXY = ((vertexXZ + originXY) << 2) & 0xFFF8FFFFu;
	const u32 packedZ = (posZ + (u32)q[1]) << 2;

	out[0] = (int)(packedXY & 0xFFFFu) - (((packedXY & 0x8000u) != 0u) ? 0x10000 : 0);
	out[1] = (int)(packedXY >> 16) - (((packedXY & 0x80000000u) != 0u) ? 0x10000 : 0);
	out[2] = (int)(packedZ & 0xFFFFu) - (((packedZ & 0x8000u) != 0u) ? 0x10000 : 0);
}

// ModelHeader.scale of a built model, as the s16 the GTE takes.
static void RldMk_ModelScale(const u8 *cmdl, int scale[3])
{
	const u8 *body = &cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	int a;

	for (a = 0; a < 3; a++)
	{
		const u32 raw = Rld_ReadLE16(&body[header + RLDMK_HEADER_SCALE + 2u * (u32)a]);

		scale[a] = (raw >= 0x8000u) ? ((int)raw - 0x10000) : (int)raw;
	}
}

// Read back by the renderer's rule (RldMk_UnpackRecord). Each record against the
// position it stands for, in every frame: X and Z within half a step, Y within
// half a double step. 0 = a record is off, *badOut says which.
static int RldMk_ReadBack(const struct RldMkBuild *b, u32 *badOut)
{
	const u8 *body = &b->cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);
	int scale[3];
	u32 frameIndex = 0;
	int a;

	*badOut = 0;
	RldMk_ModelScale(b->cmdl, scale);

	for (a = 0; a < (int)Rld_ReadLE32(&body[header + RLDMK_HEADER_NUM_ANIMS]); a++)
	{
		const u32 anim = Rld_ReadLE32(&body[table + 4u * (u32)a]);
		const u32 frames = Rld_ReadLE16(&body[anim + RLDMK_ANIM_NUM_FRAMES]) & 0x7FFFu;
		const u32 frameSize = Rld_ReadLE16(&body[anim + RLDMK_ANIM_FRAME_SIZE]);
		u32 k;

		for (k = 0; k < frames; k++)
		{
			const u8 *frame = &body[anim + RLDMK_ANIM_BYTES + k * frameSize];
			u32 r;

			for (r = 0; r < b->recordCount; r++)
			{
				const double *exact = RldMk_At(b, frameIndex, b->record[r]);
				int v[3];
				double got[3];
				double tolerance[3];
				int axis;

				RldMk_UnpackRecord(frame, r, v);
				got[0] = (double)v[0] * (double)scale[0] / 16384.0;
				got[1] = (double)v[1] * (double)scale[1] / 16384.0;
				got[2] = (double)v[2] * (double)scale[2] / 16384.0;
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

// THE GTE WINDOW, replayed from the written command words exactly as
// RenderBucket_DrawFunc_Normal moves it (RB:4630-4672, 2826-2844): which records
// a triangle is drawn from, in the order of the window, which command's cull
// bits apply, and the color index of each corner - the color window moves with
// the corner window (tempColor, RB:4636-4639, 4659). model-winding checks it,
// --preview draws with it.
struct RldMkReplay
{
	const u8 *body;
	u32 bodySize;
	u32 at;                // the next command word, body offset
	u32 recordCount;
	u32 vertexIndex;       // the next record
	u32 slotRecord[256];
	u32 window[4];         // records
	u32 windowColor[4];    // color indices
	u32 fifo[3];           // records in the order of the GTE window
	u32 stripLength;
	u32 prim;
	int ended;             // the end word was read
};

static void RldMk_ReplayStart(struct RldMkReplay *r, const u8 *cmdl, u32 recordCount)
{
	u32 header;
	u32 i;

	memset(r, 0, sizeof(*r));
	r->body = &cmdl[4];
	r->bodySize = Rld_ReadLE32(cmdl);
	r->recordCount = recordCount;
	header = Rld_ReadLE32(&r->body[RLDMK_MODEL_HEADERS]);
	r->at = Rld_ReadLE32(&r->body[header + RLDMK_HEADER_COMMANDS]) + 4u;

	for (i = 0; i < 256u; i++)
	{
		r->slotRecord[i] = RLDMK_NONE;
	}
	for (i = 0; i < 4u; i++)
	{
		r->window[i] = RLDMK_NONE;
	}
	r->fifo[0] = RLDMK_NONE;
	r->fifo[1] = RLDMK_NONE;
	r->fifo[2] = RLDMK_NONE;
}

// The next triangle. 1 = corner[] (records, in the order of the GTE window),
// color[] (color indices of the same corners), *draw (the command whose cull
// bits apply); 0 = no more: the end word (r->ended) or the end of the body;
// -1 = the list reads a record that is not there or a cache slot never written.
static int RldMk_ReplayNext(struct RldMkReplay *r, u32 corner[3], u32 color[3], u32 *draw)
{
	while ((r->at + 4u) <= r->bodySize)
	{
		const u32 command = Rld_ReadLE32(&r->body[r->at]);
		const u32 flags = command >> 24;
		const u32 slot = (command >> 16) & 0xFFu;
		u32 record;
		int useRtps;
		int reuse;

		r->at += 4u;
		if (command == RLDMK_CMD_END)
		{
			r->ended = 1;
			return 0;
		}
		if ((command >> 16) == 0u)
		{
			continue;
		}

		if ((flags & 0x04u) != 0u)
		{
			record = r->slotRecord[slot];
		}
		else
		{
			if (r->vertexIndex >= r->recordCount)
			{
				return -1;
			}
			record = r->vertexIndex++;
			r->slotRecord[slot] = record;
		}

		if (record == RLDMK_NONE)
		{
			return -1;
		}

		r->window[0] = r->window[1];
		r->window[1] = r->window[2];
		r->window[2] = r->window[3];
		r->window[3] = record;
		r->windowColor[0] = r->windowColor[1];
		r->windowColor[1] = r->windowColor[2];
		r->windowColor[2] = r->windowColor[3];
		r->windowColor[3] = (command >> RLDMK_COLOR_SHIFT) & 0x7Fu;

		if (((flags & 0x80u) != 0u) || (r->stripLength == 0u))
		{
			r->prim = command;
		}
		if ((flags & 0x80u) != 0u)
		{
			r->stripLength = 0;
		}

		useRtps = (r->stripLength > 2u);
		reuse = useRtps && ((flags & 0x40u) != 0u);
		if (reuse)
		{
			r->window[1] = r->window[0];
			r->windowColor[1] = r->windowColor[0];
		}

		if (r->stripLength < 2u)
		{
			r->stripLength++;
			continue;
		}

		*draw = (r->stripLength == 2u) ? r->prim : command;
		if (!useRtps)
		{
			// RTPT: the strip's three corners (RB:2812-2824)
			r->fifo[0] = r->window[1];
			r->fifo[1] = r->window[2];
			r->fifo[2] = r->window[3];
		}
		else if (reuse)
		{
			// SXY1 := SXY0, then RTPS shifts: corner 0 stays (RB:2828-2838)
			r->fifo[1] = r->fifo[2];
			r->fifo[2] = record;
		}
		else
		{
			r->fifo[0] = r->fifo[1];
			r->fifo[1] = r->fifo[2];
			r->fifo[2] = record;
		}
		r->stripLength++;

		corner[0] = r->fifo[0];
		corner[1] = r->fifo[1];
		corner[2] = r->fifo[2];
		color[0] = r->windowColor[1];
		color[1] = r->windowColor[2];
		color[2] = r->windowColor[3];
		return 1;
	}

	return 0;
}

// model-winding: the replayed window (RldMk_ReplayNext) with the positions the
// records stand for. Every triangle has to be the one the builder meant, carry
// the cull bit (unless both sides are drawn), and the side its cull bits let
// through (RB:2884-2899, RLDMK_CULL_SIGN) has to be the side the normal of its
// source face points to. 0 = not so, *badOut is the triangle in draw order.
static int RldMk_Winding(const struct RldMkBuild *b, int twoSided, u32 *badOut)
{
	struct RldMkReplay replay;
	u32 corner[3];
	u32 color[3];
	u32 draw = 0;
	u32 drawn = 0;
	int got;

	*badOut = 0;
	RldMk_ReplayStart(&replay, b->cmdl, b->recordCount);

	while ((got = RldMk_ReplayNext(&replay, corner, color, &draw)) == 1)
	{
		u32 fifo[3];

		fifo[0] = b->record[corner[0]];
		fifo[1] = b->record[corner[1]];
		fifo[2] = b->record[corner[2]];

		if ((drawn >= b->drawCount) || (fifo[0] != b->drawOrder[3u * drawn + 0u]) || (fifo[1] != b->drawOrder[3u * drawn + 1u]) ||
		    (fifo[2] != b->drawOrder[3u * drawn + 2u]))
		{
			*badOut = drawn;
			return 0;
		}

		if (!twoSided && RldMk_BothSides(b, b->drawTriangle[drawn]))
		{
			// turned over by the packing in some frame (RldMk_StepFlips): no cull bit
			if ((draw & RLDMK_CMD_CULL) != 0u)
			{
				*badOut = drawn;
				return 0;
			}
		}
		else if (!twoSided)
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

	if ((got < 0) || !replay.ended || (drawn != b->drawCount) || (replay.vertexIndex != b->recordCount))
	{
		*badOut = drawn;
		return 0;
	}

	return 1;
}

// The ModelFrame of frame `index` (0..46, the animations one after the other;
// 0 alone for the own mask).
static const u8 *RldMk_FrameAt(const u8 *cmdl, u32 index)
{
	const u8 *body = &cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);
	const u32 anims = Rld_ReadLE32(&body[header + RLDMK_HEADER_NUM_ANIMS]);
	u32 a;

	for (a = 0; (a < anims) && (a < (u32)RLDCHAR_ANIM_COUNT); a++)
	{
		const u32 anim = Rld_ReadLE32(&body[table + 4u * (u32)a]);
		const u32 frames = Rld_ReadLE16(&body[anim + RLDMK_ANIM_NUM_FRAMES]) & 0x7FFFu;
		const u32 frameSize = Rld_ReadLE16(&body[anim + RLDMK_ANIM_FRAME_SIZE]);

		if (index < frames)
		{
			return &body[anim + RLDMK_ANIM_BYTES + index * frameSize];
		}
		index -= frames;
	}

	return NULL;
}

// Every one of the 47 frames replayed by the renderer's rules (RldMk_ReplayNext
// with the records of that frame): each drawn triangle has the records and
// positions the builder meant in its window order, the color index of each
// corner, every corner within RldMk_ReadBack's tolerance of the exact pose, and
// the cull side of its source face (RldMk_Winding, in every frame; a triangle
// of RldMk_StepFlips has no cull bit). flips[f] counts the triangles of frame f
// whose packed normal points against the exact one: a thin triangle the
// rounding turned over - those must be drawn from both sides. 0 = a triangle
// differs, *badFrame and *badTriangle (draw order) say which.
static int RldMk_ReplayFrames(const struct RldMkBuild *b, int twoSided, u32 *badFrame, u32 *badTriangle, u32 flips[RLDMK_FRAMES_TOTAL])
{
	int scale[3];
	u32 f;

	*badFrame = 0;
	*badTriangle = 0;
	RldMk_ModelScale(b->cmdl, scale);

	for (f = 0; f < b->framesWritten; f++)
	{
		const u8 *frame = RldMk_FrameAt(b->cmdl, f);
		const double tolerance[3] = {(0.5 + 1.0e-6) * (double)scale[0] / 4096.0, (1.0 + 1.0e-6) * (double)scale[1] / 4096.0,
		                             (0.5 + 1.0e-6) * (double)scale[2] / 4096.0};
		struct RldMkReplay replay;
		u32 corner[3];
		u32 color[3];
		u32 draw = 0;
		u32 drawn = 0;
		int got;

		*badFrame = f;
		flips[f] = 0;
		if (frame == NULL)
		{
			return 0;
		}

		RldMk_ReplayStart(&replay, b->cmdl, b->recordCount);
		while ((got = RldMk_ReplayNext(&replay, corner, color, &draw)) == 1)
		{
			const u32 *source;
			double packed[3][3];
			const double *exact[3];
			double n[3];
			double m[3];
			u32 t;
			int k;

			*badTriangle = drawn;
			if (drawn >= b->drawCount)
			{
				return 0;
			}
			t = b->drawTriangle[drawn];
			source = &b->triangle[3u * t];

			for (k = 0; k < 3; k++)
			{
				const u32 position = b->record[corner[k]];
				int v[3];
				int j;
				int axis;

				if (position != b->drawOrder[3u * drawn + (u32)k])
				{
					return 0;
				}

				// The color of the source corner at this position.
				for (j = 0; (j < 3) && (b->vertexPosition[source[j]] != position); j++)
				{
				}
				if ((j == 3) || (color[k] != b->cornerColor[3u * t + (u32)j]))
				{
					return 0;
				}

				exact[k] = RldMk_At(b, f, position);
				RldMk_UnpackRecord(frame, corner[k], v);
				for (axis = 0; axis < 3; axis++)
				{
					double d;

					packed[k][axis] = (double)v[axis] * (double)scale[axis] / 16384.0;
					d = packed[k][axis] - exact[k][axis];
					if ((d > tolerance[axis]) || (d < -tolerance[axis]))
					{
						return 0;
					}
				}
			}

			RldMk_Cross(exact[0], exact[1], exact[2], n);
			if (!twoSided && RldMk_BothSides(b, t))
			{
				if ((draw & RLDMK_CMD_CULL) != 0u)
				{
					return 0;
				}
			}
			else if (!twoSided)
			{
				const u32 side = ((draw & RLDMK_CMD_SIDE) != 0u) ? 1u : 0u;
				double facing;

				RldMk_Cross(RldMk_At(b, f, b->vertexPosition[source[0]]), RldMk_At(b, f, b->vertexPosition[source[1]]),
				            RldMk_At(b, f, b->vertexPosition[source[2]]), m);
				facing = RldMk_Dot(n, m);
				if (side != RLDMK_CULL_SIGN)
				{
					facing = -facing;
				}
				if (((draw & RLDMK_CMD_CULL) == 0u) || (facing <= 0.0))
				{
					return 0;
				}
			}

			// Turned over by the packing: only allowed without the cull bit.
			RldMk_Cross(packed[0], packed[1], packed[2], m);
			if (RldMk_Dot(n, m) <= 0.0)
			{
				flips[f]++;
				if ((draw & RLDMK_CMD_CULL) != 0u)
				{
					return 0;
				}
			}
			drawn++;
		}

		*badTriangle = drawn;
		if ((got < 0) || !replay.ended || (drawn != b->drawCount) || (replay.vertexIndex != b->recordCount))
		{
			return 0;
		}
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

// --diagnose: the game's check without the rules of the lifted limits (model-draw;
// a model beyond model-size is not checked at all, RldMk_CheckBuilt). Counts
// the other findings and says them as RldMk_ReportFinding.
static int RldMk_DiagFinding(void *context, const struct RldCharFinding *finding)
{
	if (strcmp(finding->rule, "model-draw") != 0)
	{
		(*(u32 *)context)++;
		RldMk_ReportFinding(NULL, finding);
	}
	return 1;
}

// Step 15 on a built model. 0 = something is wrong, it was said.
static int RldMk_CheckBuilt(struct RldMkBuild *b, const struct RldMkOptions *opt)
{
	struct RldCharFinding first;
	struct RldCharModelFacts facts;
	u32 bad = 0;
	u32 found = 0;
	int ok = 1;

	memset(&facts, 0, sizeof(facts));
	if (b->diagnose && (b->cmdlSize > RLDCHAR_LIMIT_CMDL))
	{
		// The check reads no CMDL beyond model-size: the counts are the builder's.
		facts.triangles = b->drawCount;
		facts.records = b->recordCount;
		facts.drawBytes = b->drawBytes;
		facts.colorCount = b->colorCount;
		RldMk_Say("info", "diagnose-unchecked", NULL, "The model is larger than the game's check reads; it was read back and replayed only.");
	}
	else if (b->diagnose)
	{
		RldChar_CheckModel(b->cmdl, b->cmdlSize, &first, RldMk_DiagFinding, &found, &facts);
		ok = (found == 0u);
	}
	else if (b->maskModel)
	{
		// the model frame of CMSK, by the game's own rules for it
		ok = (RldChar_CheckProfile(&s_rldCharProfileMask, b->cmdl, b->cmdlSize, &first, RldMk_ReportFinding, NULL, &facts) == RLDCHAR_VERDICT_OK);
	}
	else if (RldChar_CheckModel(b->cmdl, b->cmdlSize, &first, RldMk_ReportFinding, NULL, &facts) != RLDCHAR_VERDICT_OK)
	{
		ok = 0;
	}

	if (ok && ((facts.triangles != b->drawCount) || (facts.records != b->recordCount) || (facts.drawBytes != b->drawBytes) ||
	           (facts.colorCount != b->colorCount)))
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

	if (ok)
	{
		u32 flips[RLDMK_FRAMES_TOTAL];
		u32 badFrame = 0;

		if (!RldMk_ReplayFrames(b, opt->twoSided, &badFrame, &bad, flips))
		{
			RldMk_Say("error", "model-roundtrip", NULL, "Internal error: the packed model does not read back the same (frame %u, triangle %u). Please report it.",
			          badFrame, bad);
			return 0;
		}
	}

	return ok;
}

//========================================================================================
// THE CHAIN
//========================================================================================

// --diagnose, after step 4: the welded and fitted positions, the triangles and
// their PLY faces and colors, before the reduction and --size move anything.
// 0 = out of memory.
static int RldMk_DiagKeep(const struct RldMkPly *ply, struct RldMkBuild *b)
{
	u32 i;
	u32 t;

	const int mapped = (b->diagPlyPosition != NULL);

	if (!mapped)
	{
		b->diagPlyPosition = (u32 *)malloc(((size_t)ply->vertexCount + 1u) * sizeof(u32));
	}
	b->diagPosition = (double *)malloc(((size_t)b->positionCount + 1u) * 3u * sizeof(double));
	b->diagPositionGroup = (u8 *)malloc((size_t)b->positionCount + 1u);
	b->diagTriangle = (u32 *)malloc(((size_t)b->triangleCount * 4u + 1u) * sizeof(u32));
	b->diagTriangleRgb = (u32 *)malloc(((size_t)b->triangleCount * 3u + 1u) * sizeof(u32));
	if ((b->diagPlyPosition == NULL) || (b->diagPosition == NULL) || (b->diagPositionGroup == NULL) || (b->diagTriangle == NULL) ||
	    (b->diagTriangleRgb == NULL) || (b->diagFace == NULL))
	{
		return RldMk_OutOfMemory();
	}

	// After --repair or --remesh their steps have made the map.
	for (i = 0; !mapped && (i < ply->vertexCount); i++)
	{
		b->diagPlyPosition[i] = b->vertexPosition[i];
	}
	b->diagPositionCount = b->positionCount;
	memcpy(b->diagPosition, b->position, (size_t)b->positionCount * 3u * sizeof(double));
	memcpy(b->diagPositionGroup, b->positionGroup, (size_t)b->positionCount);

	b->diagTriangleCount = b->triangleCount;
	for (t = 0; t < b->triangleCount; t++)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			const u32 vertex = b->triangle[3u * t + (u32)k];
			const u8 *rgb = (b->vertexRgb != NULL) ? &b->vertexRgb[3u * vertex] : &ply->rgb[3u * vertex];

			b->diagTriangle[4u * t + (u32)k] = b->vertexPosition[vertex];
			b->diagTriangleRgb[3u * t + (u32)k] = ((u32)rgb[0] << 16) | ((u32)rgb[1] << 8) | (u32)rgb[2];
		}
		b->diagTriangle[4u * t + 3u] = b->diagFace[t];
	}

	return 1;
}

// --diagnose, after step 4r: without a reduction the reduced triangles are the
// triangles, the positions the diag positions. Every triangle now knows its
// reduced one (RldMk_StepCollapse keeps it). 0 = out of memory.
static int RldMk_DiagReduced(struct RldMkBuild *b)
{
	u32 i;

	if (b->diagReduced == NULL)
	{
		b->diagReduced = (u32 *)malloc(((size_t)b->diagTriangleCount * 7u + 1u) * sizeof(u32));
		if (b->diagReduced == NULL)
		{
			return RldMk_OutOfMemory();
		}
		for (i = 0; i < b->diagTriangleCount; i++)
		{
			memcpy(&b->diagReduced[7u * i], &b->diagTriangle[4u * i], 3u * sizeof(u32));
			b->diagReduced[7u * i + 3u] = i;
			b->diagReduced[7u * i + 4u] = 3u * i + 0u;
			b->diagReduced[7u * i + 5u] = 3u * i + 1u;
			b->diagReduced[7u * i + 6u] = 3u * i + 2u;
		}
		b->diagReducedCount = b->diagTriangleCount;
	}

	if (b->diagPositionFrom == NULL)
	{
		b->diagPositionFrom = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
		if (b->diagPositionFrom == NULL)
		{
			return RldMk_OutOfMemory();
		}
		for (i = 0; i < b->positionCount; i++)
		{
			b->diagPositionFrom[i] = i;
		}
	}

	b->triangleReduced = (u32 *)malloc(((size_t)b->triangleCount + 1u) * sizeof(u32));
	if (b->triangleReduced == NULL)
	{
		return RldMk_OutOfMemory();
	}
	for (i = 0; i < b->triangleCount; i++)
	{
		b->triangleReduced[i] = i;
	}

	return 1;
}

// Steps 2-13 and 15 on a PLY that was read, once. 0 = the model cannot be
// built; every reason was said.
static int RldMk_BuildOnce(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	struct RldMkOptions fitted = *opt;
	u32 keyCount = 0;

	memset(b, 0, sizeof(*b));
	b->vertexCount = ply->vertexCount;
	b->faceCount = ply->faceCount;
	b->kartPart = -1;
	b->driverPart = -1;
	b->diagnose = opt->diagnose;

	// --fit crash: the scale first, then the chain with it (as --scale).
	if (opt->fit)
	{
		RldMk_StepFit(ply, opt, b, &fitted.scale);
	}
	opt = &fitted;

	if (!RldMk_StepWeld(ply, opt, b, &keyCount) || !RldMk_StepTriangles(ply, b, keyCount))
	{
		return 0;
	}

	if (b->fitted)
	{
		double lo[3];
		double hi[3];

		RldMk_Bounds(b, -1, lo, hi);
		b->fitAfter[0] = hi[2] - lo[2];
		b->fitAfter[1] = hi[1] - lo[1];
	}

	RldMk_StepOpenEdges(b);

	if (!RldMk_StepRepair(ply, b, opt) || !RldMk_StepOpenParts(b, opt) || !RldMk_StepParts(b, opt))
	{
		return 0;
	}

	// the kart as modeled (a hull of --remesh is a little longer)
	if (b->fitted && (b->fitBasis == RLDMK_FIT_KART))
	{
		b->fitKart[1] = b->parts[b->kartPart].hi[2] - b->parts[b->kartPart].lo[2];
	}

	// The kart at Crash's length would have made the model taller than
	// the tallest retail driver: the height decided, the kart is shorter
	// than 112.4 and the game's wheels (RLDDUM_TIRE_*) stand off its ends.
	// --size does not help here (it scales the driver after the fit).
	if (b->fitted && b->fitCapped && (b->fitBasis == RLDMK_FIT_KART) && (b->fitKart[0] > 0.0))
	{
		const double tall = b->fitBefore[1] * RLDMK_CRASH_LENGTH / b->fitKart[0];
		const u32 percent = (u32)RldMk_Floor(100.0 * RLDMK_RETAIL_HEIGHT_MAX / tall);

		RldMk_Say("warning", "fit-capped", NULL,
		          "With its kart at the length of Crash's the model would be %.2f model units tall, more than the tallest retail driver (%.2f): the whole model was made smaller, so the kart is %.2f long instead of %.2f and the game's wheels will not sit on it. Make the driver smaller in the model (to about %u%% of the model's height), or hide the kart wheels (--wheels off; Reload Studio: untick Show kart wheels).",
		          tall / RLDMK_UNITS, RLDMK_RETAIL_HEIGHT_MAX / RLDMK_UNITS, b->fitKart[1] / RLDMK_UNITS, RLDMK_CRASH_LENGTH / RLDMK_UNITS, percent);
	}

	if (!RldMk_StepRemesh(b, opt, ply) || !RldMk_StepDummy(b, opt) || (b->diagnose && !RldMk_DiagKeep(ply, b)))
	{
		return 0;
	}

	if (!RldMk_StepReduce(ply, b, opt) || (b->diagnose && !RldMk_DiagReduced(b)))
	{
		return 0;
	}

	if (b->remeshed)
	{
		RldMk_Say("info", "model-remeshed", NULL, "Remeshed as closed hulls: %u triangles with %u open edges -> %u -> %u triangles with %u open edges.",
		          b->remeshFrom, b->remeshOpenBefore, b->remeshHull, b->triangleCount, b->openEdges);
	}

	if (!RldMk_StepSize(b, opt) || !RldMk_StepMeasure(b, opt) || !RldMk_StepBudget(b, opt) || !RldMk_StepPoses(b) || !RldMk_StepQuantize(b) ||
	    !RldMk_StepCollapse(b) || !RldMk_StepFlips(b, opt) || !RldMk_StepColors(ply, opt, b) || !RldMk_StepCommands(b, opt) || !RldMk_StepWrite(b))
	{
		return 0;
	}

	return RldMk_CheckBuilt(b, opt);
}

//========================================================================================
// THE OWN MASK (--mask-model): THE CHAIN WITH THE PROFILE OF A MASK
//========================================================================================
//
// The same steps as a character, with these differences: the fit brings the
// model to the height of the retail mask (RLDMK_MASK_HEIGHT x --mask-size);
// there are no parts (no kart, no driver, no steering wheel), no dummy, no
// --remesh and no poses; the reduction aims at RLDMK_MASK_TRIANGLES_MAX; after
// it the model is centered where the retail mask's center lies; the draw
// memory may be RLDCHAR_MASK_DRAW_BYTES_MAX; one animation of one frame is
// written, and the check is the game's own for CMSK (RldChar_CheckProfile with
// s_rldCharProfileMask on the model frame).

// The fit: the height at scale 1, then the factor (4 significant digits, as
// --fit crash) that makes it RLDMK_MASK_HEIGHT x sizePercent / 100. 0 = the
// probe did not get through: nothing is fitted, the real pass says why.
static int RldMk_StepMaskFit(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b, double *scale)
{
	struct RldMkOptions at = *opt;
	struct RldMkBuild probe;
	double lo[3];
	double hi[3];
	double f = 0.0;

	at.scale = 1.0;
	if (!RldMk_FitProbe(ply, &at, &probe, 0))
	{
		RldMk_FreeBuild(&probe);
		return 0;
	}
	RldMk_Bounds(&probe, -1, lo, hi);
	RldMk_FreeBuild(&probe);

	b->fitted = 1;
	b->fitBefore[0] = hi[2] - lo[2];
	b->fitBefore[1] = hi[1] - lo[1];
	if (b->fitBefore[1] > 0.0)
	{
		f = RLDMK_MASK_HEIGHT * (double)opt->sizePercent / 100.0 / b->fitBefore[1];
	}
	b->fitFactor = (f > 0.0) ? RldMk_FitRound(f) : 1.0;
	b->fitAfter[0] = b->fitBefore[0] * b->fitFactor;
	b->fitAfter[1] = b->fitBefore[1] * b->fitFactor;
	*scale = b->fitFactor;
	return 1;
}

// One part, of the kart group: it stays still in every frame.
static int RldMk_StepMaskPart(struct RldMkBuild *b)
{
	u32 p;

	b->positionPart = (u32 *)calloc((size_t)b->positionCount + 1u, sizeof(u32));
	b->positionGroup = (u8 *)calloc((size_t)b->positionCount + 1u, 1u);
	b->parts = (struct RldMkPart *)calloc(2u, sizeof(struct RldMkPart));
	if ((b->positionPart == NULL) || (b->positionGroup == NULL) || (b->parts == NULL))
	{
		return RldMk_OutOfMemory();
	}

	for (p = 0; p < b->positionCount; p++)
	{
		b->positionGroup[p] = (u8)RLDMK_GROUP_KART;
	}
	b->partCount = 1u;
	b->parts[0].triangles = b->triangleCount;
	b->parts[0].group = RLDMK_GROUP_KART;
	RldMk_Bounds(b, 0, b->parts[0].lo, b->parts[0].hi);
	b->kartPart = 0;
	b->driverPart = -1;
	b->groupTriangles[RLDMK_GROUP_KART] = b->triangleCount;
	b->posed = 0;
	return 1;
}

// After the reduction: the center of the model's box onto the retail mask's;
// size[] = length (z), width (x), height (y) in game units, size[3] 0.
static int RldMk_StepMaskPlace(struct RldMkBuild *b)
{
	static const double center[3] = RLDMK_MASK_CENTER;
	double lo[3];
	double hi[3];
	double shift[3];
	u32 p;
	int a;

	RldMk_Bounds(b, -1, lo, hi);
	for (a = 0; a < 3; a++)
	{
		shift[a] = center[a] - 0.5 * (lo[a] + hi[a]);
	}
	for (p = 0; p < b->positionCount; p++)
	{
		for (a = 0; a < 3; a++)
		{
			b->position[3u * p + (u32)a] += shift[a];
		}
	}
	RldMk_Bounds(b, 0, b->parts[0].lo, b->parts[0].hi);

	b->size[0] = hi[2] - lo[2];
	b->size[1] = hi[0] - lo[0];
	b->size[2] = hi[1] - lo[1];
	b->size[3] = 0.0;

	if (b->size[1] > (2.0 * RLDMK_MASK_ORBIT))
	{
		RldMk_Say("note", "mask-wide", NULL,
		          "The mask is %.1f units wide; it circles the driver at %.0f, so a mask wider than %.0f cuts through him. Choose a smaller --mask-size.",
		          b->size[1], RLDMK_MASK_ORBIT, 2.0 * RLDMK_MASK_ORBIT);
	}
	return 1;
}

// Step 6 for the mask: at most the draw memory of the retail mask.
static int RldMk_StepMaskBudget(const struct RldMkBuild *b)
{
	const u32 bytes = b->triangleCount * RLDCHAR_DRAW_BYTES_G3;

	if ((b->triangleCount > RLDMK_MASK_TRIANGLES_MAX) || (bytes > RLDCHAR_MASK_DRAW_BYTES_MAX))
	{
		RldMk_Say("error", "mask-tris", NULL,
		          "The mask needs %u bytes of draw memory per frame (%u triangles); a mask may use %u, as much as Aku Aku: %u triangles. Reduce it in Blender (Decimate modifier).",
		          bytes, b->triangleCount, (u32)RLDCHAR_MASK_DRAW_BYTES_MAX, (u32)RLDMK_MASK_TRIANGLES_MAX);
		return 0;
	}
	return 1;
}

// Steps 2-13 and 15 of the own mask, once. 0 = it cannot be built; every reason was said.
static int RldMk_BuildMaskOnce(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	struct RldMkOptions fitted = *opt;
	u32 keyCount = 0;

	memset(b, 0, sizeof(*b));
	b->vertexCount = ply->vertexCount;
	b->faceCount = ply->faceCount;
	b->kartPart = -1;
	b->driverPart = -1;
	b->maskModel = 1;

	(void)RldMk_StepMaskFit(ply, opt, b, &fitted.scale);
	opt = &fitted;

	if (!RldMk_StepWeld(ply, opt, b, &keyCount) || !RldMk_StepTriangles(ply, b, keyCount))
	{
		return 0;
	}
	RldMk_StepOpenEdges(b);

	if (!RldMk_StepRepair(ply, b, opt) || !RldMk_StepOpenParts(b, opt) || !RldMk_StepMaskPart(b) || !RldMk_StepReduce(ply, b, opt))
	{
		return 0;
	}

	if (!RldMk_StepMaskPlace(b) || !RldMk_StepMaskBudget(b) || !RldMk_StepPoses(b) || !RldMk_StepQuantize(b) || !RldMk_StepCollapse(b) ||
	    !RldMk_StepFlips(b, opt) || !RldMk_StepColors(ply, opt, b) || !RldMk_StepCommands(b, opt) || !RldMk_StepWrite(b))
	{
		return 0;
	}

	return RldMk_CheckBuilt(b, opt);
}

// A character or the own mask, by the profile of `opt`.
static int RldMk_BuildPass(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	return opt->maskModel ? RldMk_BuildMaskOnce(ply, opt, b) : RldMk_BuildOnce(ply, opt, b);
}

// Steps 2-13 and 15, with the retries of step 4r: with --reduce auto the model
// is first built without a word. When that build fails mem-model or
// model-frame (too many records, RldMk_StepWrite) and the model has more
// triangles than its limit, the target is lowered by 5 % and it is built
// again, at most RLDMK_REDUCE_TRIES times in all. A model at or below the limit
// is never reduced: it speaks with its real cause.
// When such a build reduces nothing (the reducer cannot), lowering further is
// no use: the last build speaks again with the target before it - or the first
// one, when no reduction ever worked - so the author sees the real cause.
// Only that last build speaks. 0 = the model cannot be built; every reason was
// said. With --diagnose only that last build lifts the limits (RldMk_Diagnosis).
static int RldMk_BuildModel(const struct RldMkPly *ply, const struct RldMkOptions *opt, struct RldMkBuild *b)
{
	static char why[RLDMK_REDUCE_TRIES][128];
	struct RldMkOptions at = *opt;
	struct RldMkOptions speak;
	int tries;

	at.reduceTarget = opt->maskModel ? (u32)RLDMK_MASK_TRIANGLES_MAX : RLDMK_REDUCE_TARGET;
	at.reduceForce = 0;
	at.reduceWhy = NULL;
	at.diagnose = 0;  // the tries see the limits: --diagnose shows the build make-char would make
	speak = at;
	for (tries = 1; opt->reduce && (tries < RLDMK_REDUCE_TRIES); tries++)
	{
		const int quiet = s_rldMkQuiet;
		const int errors = s_rldMkErrors;
		const size_t said = strlen(s_rldMkCodes);
		int limit;
		int reduced;
		u32 from;
		u32 next;

		s_rldMkQuiet = 1;
		limit = !RldMk_BuildPass(ply, &at, b) && b->recordLimit;
		reduced = b->reduced;
		from = b->reduceFrom;
		next = ((b->reduceFrom < at.reduceTarget) ? b->reduceFrom : at.reduceTarget) * 95u / 100u;
		if (limit && (b->frameSize > 0xFFFFu))
		{
			snprintf(why[tries], sizeof(why[tries]), "at %u triangles one animation frame would need %u bytes, at most 65535", b->reduceTo, b->frameSize);
		}
		else if (limit && opt->maskModel)
		{
			snprintf(why[tries], sizeof(why[tries]), "at %u triangles the mask would need %u bytes, at most %u", b->reduceTo, (u32)b->cmdlSize,
			         (u32)RLDMK_MASK_CMDL_MAX);
		}
		else if (limit)
		{
			snprintf(why[tries], sizeof(why[tries]), "at %u triangles the model would need %u KB, at most %u KB", b->reduceTo,
			         (u32)((b->cmdlSize + 1023u) / 1024u), (u32)(RLDCHAR_LIMIT_CMDL / 1024u));
		}
		RldMk_FreeBuild(b);
		s_rldMkQuiet = quiet;
		s_rldMkErrors = errors;
		s_rldMkCodes[said] = '\0';

		if (!limit)
		{
			speak = at;
			break;
		}
		if (from <= (opt->maskModel ? (u32)RLDMK_MASK_TRIANGLES_MAX : (u32)RLDMK_REDUCE_LIMIT))
		{
			break;
		}
		if (at.reduceForce && !reduced)
		{
			break;
		}
		speak = at;
		if (next == 0u)
		{
			break;
		}
		at.reduceTarget = next;
		at.reduceForce = 1;
		at.reduceWhy = why[tries];
	}

	speak.diagnose = opt->diagnose;
	return RldMk_BuildPass(ply, &speak, b);
}

//========================================================================================
// --preview: THE BUILT MODEL AS THE GAME DRAWS IT
//========================================================================================
//
// For the 3D view of Reload Studio, which checks nothing and rebuilds nothing:
// it gets the triangles out of the written CMDL by the renderer's own rules -
// the corners unpacked from the frame records (RldMk_UnpackRecord), the
// triangles and their colors from the replayed command list (RldMk_ReplayNext).
//
// THE FILE, little endian:
//   "RLDPV1\0\0"               8 bytes
//   u32 poses                  3: turn frame 10 (neutral), turn frame 0, turn frame 20
//   per pose: u32 triangles, then per triangle 3 corners of
//     s16 x, s16 y, s16 z      game units in model space (rounded): +Y up,
//                              +Z forward, +X the driver's left
//     u8 r, u8 g, u8 b         the vertex color as the model stores it
//     u8 pad                   bit 0 = 1: drawn from both sides (no cull bit)
// The corners of a triangle run counter-clockwise seen from the side the game
// draws: the right-hand normal (b - a) x (c - a) points to the viewer who sees
// it. A triangle with bit 0 of pad set is seen from both sides.
//
// Turn frame 0 is the full stop for steering LEFT, frame 20 for steering right
// (game/Vehicle/VehFrame.c:208 maps -simpTurnState to the frame; a left turn
// makes simpTurnState positive, game/Vehicle/VehPhysProc.c:1107-1132). The
// automatic poses lean the driver toward +X - his left - in frame 0.

#define RLDMK_PREVIEW_POSES 3u
#define RLDMK_PREVIEW_CORNER_BYTES 10u

static const u32 s_rldMkPreviewFrames[RLDMK_PREVIEW_POSES] = {RLDMK_NEUTRAL_FRAME, 0u, 20u};

// A GTE value -> game units: value x scale / 16384, rounded half up, kept in 16 bits.
static int RldMk_PreviewUnit(int value, int scale)
{
	const s64 shifted = (s64)value * (s64)scale + 8192;
	s64 units = (shifted >= 0) ? (shifted / 16384) : -((-shifted + 16383) / 16384);

	units = (units < -32768) ? -32768 : ((units > 32767) ? 32767 : units);
	return (int)units;
}

// The preview of a built (and checked) model in memory, and the box of its kart
// in the neutral frame (x0 y0 z0 x1 y1 z1, the same units). NULL = out of
// memory, or the list does not replay - neither happens to a model that passed
// step 15.
static u8 *RldMk_Preview(const struct RldMkBuild *b, size_t *sizeOut, int kartBox[6])
{
	const u8 *body = &b->cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);
	const u32 colorsAt = Rld_ReadLE32(&body[header + RLDMK_HEADER_COLORS]);
	const u32 turn = Rld_ReadLE32(&body[table]);
	const u32 frameSize = Rld_ReadLE16(&body[turn + RLDMK_ANIM_FRAME_SIZE]);
	const size_t size = 12u + (size_t)RLDMK_PREVIEW_POSES * (4u + (size_t)b->drawCount * 3u * RLDMK_PREVIEW_CORNER_BYTES);
	int scale[3];
	size_t at = 12;
	u8 *out;
	u32 pose;
	u32 r;
	int a;

	*sizeOut = 0;
	RldMk_ModelScale(b->cmdl, scale);

	out = (u8 *)calloc(1, size);
	if (out == NULL)
	{
		return NULL;
	}
	memcpy(out, "RLDPV1\0\0", 8u);
	Rld_WriteLE32(&out[8], RLDMK_PREVIEW_POSES);

	for (pose = 0; pose < RLDMK_PREVIEW_POSES; pose++)
	{
		const u8 *frame = &body[turn + RLDMK_ANIM_BYTES + s_rldMkPreviewFrames[pose] * frameSize];
		struct RldMkReplay replay;
		u32 corner[3];
		u32 color[3];
		u32 draw = 0;
		u32 count = 0;
		int got;

		Rld_WriteLE32(&out[at], b->drawCount);
		at += 4u;

		RldMk_ReplayStart(&replay, b->cmdl, b->recordCount);
		while ((got = RldMk_ReplayNext(&replay, corner, color, &draw)) == 1)
		{
			const int cull = ((draw & RLDMK_CMD_CULL) != 0u);
			const u32 side = ((draw & RLDMK_CMD_SIDE) != 0u) ? 1u : 0u;
			// window order when its normal is the drawn side (RldMk_Winding), else turned round
			const int turnRound = cull && (side != RLDMK_CULL_SIGN);
			int k;

			if (count >= b->drawCount)
			{
				free(out);
				return NULL;
			}

			for (k = 0; k < 3; k++)
			{
				const int from = turnRound ? ((3 - k) % 3) : k;
				const u32 index = color[from];
				int v[3];

				RldMk_UnpackRecord(frame, corner[from], v);
				for (a = 0; a < 3; a++)
				{
					Rld_WriteLE16(&out[at + 2u * (u32)a], (u32)RldMk_PreviewUnit(v[a], scale[a]) & 0xFFFFu);
				}

				if (index < b->colorCount)
				{
					// file order r, g, b, code (step 7)
					out[at + 6u] = body[colorsAt + 4u * index + 0u];
					out[at + 7u] = body[colorsAt + 4u * index + 1u];
					out[at + 8u] = body[colorsAt + 4u * index + 2u];
				}
				out[at + 9u] = (u8)(cull ? 0u : 1u);
				at += RLDMK_PREVIEW_CORNER_BYTES;
			}

			count++;
		}

		if ((got < 0) || (count != b->drawCount))
		{
			free(out);
			return NULL;
		}
	}

	// The kart in the neutral frame: the size reference next to the driver.
	for (a = 0; a < 3; a++)
	{
		kartBox[a] = 32767;
		kartBox[3 + a] = -32768;
	}
	for (r = 0; r < b->recordCount; r++)
	{
		const u8 *frame = &body[turn + RLDMK_ANIM_BYTES + RLDMK_NEUTRAL_FRAME * frameSize];
		int v[3];

		if (b->positionGroup[b->record[r]] != RLDMK_GROUP_KART)
		{
			continue;
		}

		RldMk_UnpackRecord(frame, r, v);
		for (a = 0; a < 3; a++)
		{
			const int unit = RldMk_PreviewUnit(v[a], scale[a]);

			kartBox[a] = (unit < kartBox[a]) ? unit : kartBox[a];
			kartBox[3 + a] = (unit > kartBox[3 + a]) ? unit : kartBox[3 + a];
		}
	}

	*sizeOut = size;
	return out;
}

// Writes the preview next to the build; a failure is a warning: the character
// itself is not touched by it.
static void RldMk_WritePreview(const struct RldMkBuild *b, const char *path)
{
	int box[6];
	size_t size = 0;
	u8 *data = RldMk_Preview(b, &size, box);
	FILE *file;
	int openError;

	if (data == NULL)
	{
		RldMk_EmitFile("preview", "failed", path, 0u);
		RldMk_Say("warning", "preview", path, "The preview cannot be made (out of memory). The character is not affected.");
		return;
	}

	file = fopen(path, "wb");
	openError = (file == NULL) ? errno : 0;
	if ((file == NULL) || (fwrite(data, 1, size, file) != size) || (fclose(file) != 0))
	{
		RldMk_EmitFile("preview", "failed", path, 0u);
		if (!RldMk_SayMissingFolder("warning", "preview", path, "The preview file", openError))
		{
			RldMk_Say("warning", "preview", path, "The preview file %s cannot be written. Is the folder write-protected?", path);
		}
		free(data);
		return;
	}
	free(data);

	RldMk_EmitFile("preview", "ok", path, size);

	if (s_machine && !s_rldMkQuiet)
	{
		char text[6][16];
		int k;

		for (k = 0; k < 6; k++)
		{
			snprintf(text[k], sizeof(text[k]), "%d", box[k]);
		}
		Rld_Emit("value", "kart-box", text[0], text[1], text[2], text[3], text[4], text[5], (const char *)NULL);
	}

	if (!s_rldMkQuiet)
	{
		printf("  preview       %s, %u bytes (3 poses, %u triangles each)\n", path, (u32)size, b->drawCount);
	}
}

//========================================================================================
// --diagnose: THE MODEL AS EVERY STEP SAW IT (RLDDIAG 1)
//========================================================================================
//
// To find where a model loses its shape. Text, LF, fields split by one space,
// indices from 0, -1 where nothing belongs, numbers as %.6f:
//   RLDDIAG 1
//   fit <f>                    the factor of --fit crash (1 with --fit none)
//   scale <s>                  --scale
//   size <percent>             --size
//   hip <x> <y> <z>            the point the poses and --size turn about, game units:
//                              with --fit crash the seat on the dummy (RldDum_Fit),
//                              with --fit none the hip (0, 16, 0)
//   ply <vertices> <faces>
//   pv <vertex> <position>     per PLY vertex: its welded position, -1 = dropped
//   positions <P>              welded and fitted, before step 4r and --size, game units:
//   p <i> <x> <y> <z> <group>  group RLDMK_GROUP_*: 0 kart, 1 driver, 2 steering wheel
//   triangles <T>              after step 2, before step 4r:
//   t <i> <a> <b> <c> <group> <face> <rgb0> <rgb1> <rgb2>   the PLY face and its corner colors
//                              (after --remesh: the hulls, face -1, and the groups kept)
//   reduced <T1>               after step 4r (the triangles when nothing was reduced):
//   r <i> <a> <b> <c> <source> <sc0> <sc1> <sc2>   positions as p, the triangle t it continues,
//                              and per corner the corner 3 x t + k of the t lines whose color
//                              it carries (at the same position)
//   records <R>
//   rec <r> <position>
//   frames 47
//   x <frame> <position> <ex> <ey> <ez> <dx> <dy> <dz>   per frame and position with a
//                              record: exact (before step 9) and decoded from the
//                              written CMDL by the renderer's rule (RldMk_ReadBack)
//   clamp <frame> <position> <axis> <value>   a value step 9 cut off at the ends of
//                              its byte: axis 0 x, 1 y, 2 z; value in record steps
//                              before rounding (y: steps of one, stored as even bytes)
//   collapsed <n>              step 9's triangles without area (RldMk_StepCollapse)
//   drawn <D>                  replayed from the written command list in the game's
//                              order (RldMk_ReplayNext), the corners turned as --preview
//                              turns them (counter-clockwise seen from the drawn side):
//   d <i> <rec0> <rec1> <rec2> <rgb0> <rgb1> <rgb2> <cull> <reduced>   palette colors,
//                              cull 1 = one side drawn, reduced = r index
//   palette <n>
//   c <index> <rgb>
//   end
// rgb is 6 hex digits RRGGBB; positions are p numbers throughout. With --reduce
// auto it is the last build, the one make-char would write. Its limits of draw
// memory (model-tris, model-draw) and file size (mem-model) are lifted; the
// 16 bits of a frame (model-frame) are not - the bytes would not read back.

struct RldMkText
{
	char *data;
	size_t size;
	size_t capacity;
	int failed;
};

static void RldMk_TextAdd(struct RldMkText *text, const char *format, ...)
{
	char line[256];
	va_list list;
	int n;

	if (text->failed)
	{
		return;
	}

	va_start(list, format);
	n = vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	if ((n < 0) || ((size_t)n >= sizeof(line)))
	{
		text->failed = 1;
		return;
	}

	if ((text->size + (size_t)n + 1u) > text->capacity)
	{
		size_t capacity = (text->capacity != 0u) ? text->capacity : 65536u;
		char *grown;

		while ((text->size + (size_t)n + 1u) > capacity)
		{
			capacity *= 2u;
		}
		grown = (char *)realloc(text->data, capacity);
		if (grown == NULL)
		{
			text->failed = 1;
			return;
		}
		text->data = grown;
		text->capacity = capacity;
	}

	memcpy(&text->data[text->size], line, (size_t)n);
	text->size += (size_t)n;
	text->data[text->size] = '\0';
}

// A palette color of the written CMDL as RRGGBB, -1 for an index beyond it.
static void RldMk_DiagColor(const struct RldMkBuild *b, u32 index, char out[8])
{
	const u8 *body = &b->cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 colorsAt = Rld_ReadLE32(&body[header + RLDMK_HEADER_COLORS]);

	if (index >= b->colorCount)
	{
		snprintf(out, 8, "-1");
		return;
	}
	// file order r, g, b, code (step 7)
	snprintf(out, 8, "%02x%02x%02x", (u32)body[colorsAt + 4u * index + 0u], (u32)body[colorsAt + 4u * index + 1u], (u32)body[colorsAt + 4u * index + 2u]);
}

// The diagnosis of a build made with --diagnose, as text (*sizeOut bytes, NUL
// after them). NULL = out of memory, or the build is not one of --diagnose.
static char *RldMk_Diagnosis(const struct RldMkBuild *b, const struct RldMkOptions *opt, size_t *sizeOut)
{
	struct RldMkText text;
	struct RldMkReplay replay;
	u32 *firstRecord;
	u32 corner[3];
	u32 color[3];
	u32 draw = 0;
	u32 drawn = 0;
	int scale[3];
	int got;
	u32 i;
	u32 f;

	*sizeOut = 0;
	if (!b->diagnose || (b->cmdl == NULL) || (b->diagPosition == NULL) || (b->diagReduced == NULL) || (b->triangleReduced == NULL))
	{
		return NULL;
	}

	memset(&text, 0, sizeof(text));
	firstRecord = (u32 *)malloc(((size_t)b->positionCount + 1u) * sizeof(u32));
	if (firstRecord == NULL)
	{
		return NULL;
	}

	RldMk_TextAdd(&text, "RLDDIAG 1\n");
	RldMk_TextAdd(&text, "fit %.6f\n", b->fitted ? b->fitFactor : 1.0);
	RldMk_TextAdd(&text, "scale %.6f\n", opt->scale);
	RldMk_TextAdd(&text, "size %u\n", (opt->sizePercent != 0u) ? opt->sizePercent : 100u);
	if (b->dummy)
	{
		RldMk_TextAdd(&text, "hip %.6f %.6f %.6f\n", b->dumFit.seat[0], b->dumFit.seat[1], b->dumFit.seat[2]);
	}
	else
	{
		RldMk_TextAdd(&text, "hip %.6f %.6f %.6f\n", 0.0, RLDMK_HIP_Y, 0.0);
	}
	RldMk_TextAdd(&text, "ply %u %u\n", b->vertexCount, b->faceCount);
	for (i = 0; i < b->vertexCount; i++)
	{
		RldMk_TextAdd(&text, "pv %u %d\n", i, (b->diagPlyPosition[i] != RLDMK_NONE) ? (int)b->diagPlyPosition[i] : -1);
	}

	RldMk_TextAdd(&text, "positions %u\n", b->diagPositionCount);
	for (i = 0; i < b->diagPositionCount; i++)
	{
		const double *v = &b->diagPosition[3u * i];

		RldMk_TextAdd(&text, "p %u %.6f %.6f %.6f %u\n", i, v[0], v[1], v[2], (u32)b->diagPositionGroup[i]);
	}

	RldMk_TextAdd(&text, "triangles %u\n", b->diagTriangleCount);
	for (i = 0; i < b->diagTriangleCount; i++)
	{
		const u32 *t = &b->diagTriangle[4u * i];
		const u32 *rgb = &b->diagTriangleRgb[3u * i];

		RldMk_TextAdd(&text, "t %u %u %u %u %u %d %06x %06x %06x\n", i, t[0], t[1], t[2], (u32)b->diagPositionGroup[t[0]],
		              (t[3] != RLDMK_NONE) ? (int)t[3] : -1, rgb[0], rgb[1], rgb[2]);
	}

	RldMk_TextAdd(&text, "reduced %u\n", b->diagReducedCount);
	for (i = 0; i < b->diagReducedCount; i++)
	{
		const u32 *r = &b->diagReduced[7u * i];

		RldMk_TextAdd(&text, "r %u %u %u %u %u %u %u %u\n", i, r[0], r[1], r[2], r[3], r[4], r[5], r[6]);
	}

	for (i = 0; i < b->positionCount; i++)
	{
		firstRecord[i] = RLDMK_NONE;
	}
	RldMk_TextAdd(&text, "records %u\n", b->recordCount);
	for (i = 0; i < b->recordCount; i++)
	{
		RldMk_TextAdd(&text, "rec %u %u\n", i, b->diagPositionFrom[b->record[i]]);
		if (firstRecord[b->record[i]] == RLDMK_NONE)
		{
			firstRecord[b->record[i]] = i;
		}
	}

	// Positions ascending: the diag numbers keep their order (RldMk_StepReduce).
	RldMk_ModelScale(b->cmdl, scale);
	RldMk_TextAdd(&text, "frames %u\n", (u32)RLDMK_FRAMES_TOTAL);
	for (f = 0; f < RLDMK_FRAMES_TOTAL; f++)
	{
		const u8 *frame = RldMk_FrameAt(b->cmdl, f);

		for (i = 0; (frame != NULL) && (i < b->positionCount); i++)
		{
			const double *exact = RldMk_At(b, f, i);
			int v[3];

			if (firstRecord[i] == RLDMK_NONE)
			{
				continue;
			}
			RldMk_UnpackRecord(frame, firstRecord[i], v);
			RldMk_TextAdd(&text, "x %u %u %.6f %.6f %.6f %.6f %.6f %.6f\n", f, b->diagPositionFrom[i], exact[0], exact[1], exact[2],
			              (double)v[0] * (double)scale[0] / 16384.0, (double)v[1] * (double)scale[1] / 16384.0, (double)v[2] * (double)scale[2] / 16384.0);
		}
	}
	free(firstRecord);

	for (i = 0; i < b->diagClampCount; i++)
	{
		const struct RldMkClamp *c = &b->diagClamp[i];

		RldMk_TextAdd(&text, "clamp %u %u %u %.6f\n", c->frame, b->diagPositionFrom[c->position], c->axis, c->value);
	}
	RldMk_TextAdd(&text, "collapsed %u\n", b->collapsed);

	RldMk_TextAdd(&text, "drawn %u\n", b->drawCount);
	RldMk_ReplayStart(&replay, b->cmdl, b->recordCount);
	while ((got = RldMk_ReplayNext(&replay, corner, color, &draw)) == 1)
	{
		const int cull = ((draw & RLDMK_CMD_CULL) != 0u);
		const u32 side = ((draw & RLDMK_CMD_SIDE) != 0u) ? 1u : 0u;
		const int turnRound = cull && (side != RLDMK_CULL_SIGN);
		char rgb[3][8];
		u32 record[3];
		int reduced = -1;
		int k;

		// The triangle the builder meant (RldMk_Winding checks the same).
		if ((drawn < b->drawCount) && (b->record[corner[0]] == b->drawOrder[3u * drawn + 0u]) && (b->record[corner[1]] == b->drawOrder[3u * drawn + 1u]) &&
		    (b->record[corner[2]] == b->drawOrder[3u * drawn + 2u]))
		{
			reduced = (int)b->triangleReduced[b->drawTriangle[drawn]];
		}

		for (k = 0; k < 3; k++)
		{
			const int from = turnRound ? ((3 - k) % 3) : k;

			record[k] = corner[from];
			RldMk_DiagColor(b, color[from], rgb[k]);
		}
		RldMk_TextAdd(&text, "d %u %u %u %u %s %s %s %d %d\n", drawn, record[0], record[1], record[2], rgb[0], rgb[1], rgb[2], cull, reduced);
		drawn++;
	}

	RldMk_TextAdd(&text, "palette %u\n", b->colorCount);
	for (i = 0; i < b->colorCount; i++)
	{
		char rgb[8];

		RldMk_DiagColor(b, i, rgb);
		RldMk_TextAdd(&text, "c %u %s\n", i, rgb);
	}
	RldMk_TextAdd(&text, "end\n");

	// A list that does not replay to the end never passed step 15.
	if (text.failed || (got < 0) || (drawn != b->drawCount))
	{
		free(text.data);
		return NULL;
	}

	*sizeOut = text.size;
	return text.data;
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
// fixedSize 0x20 with the flags only when a flag is set: without one the
// bytes are those from before the flags existed (include/rldchar.inc).
// fixedSize 0x24 with the map color only when RLDCHAR_FLAG_MAP_COLOR is set:
// mapColor 0x00BBGGRR is written as it is (r, g, b, 0).
static u8 *RldMk_BuildChriColor(u32 templateId, u32 classId, const u8 name[RLDCHAR_NAME_FIELD], u32 charVersion, const char *author, u32 flags,
                                u32 mapColor, size_t *sizeOut)
{
	const size_t length = strlen(author);
	const u32 fixedSize = ((flags & RLDCHAR_FLAG_MAP_COLOR) != 0u) ? RLDCHAR_CHRI_SIZE_MAP_COLOR
	                                                               : ((flags != 0u) ? RLDCHAR_CHRI_SIZE_FLAGS : RLDCHAR_INFO_FIXED_SIZE);
	const size_t size = fixedSize + 4u + 4u + length;
	u8 *data = (u8 *)calloc(1, size);

	*sizeOut = 0;
	if (data == NULL)
	{
		return NULL;
	}

	Rld_WriteLE16(&data[0x00], fixedSize);
	data[0x02] = (u8)templateId;
	data[0x03] = (u8)classId;
	memcpy(&data[0x04], name, RLDCHAR_NAME_FIELD);
	Rld_WriteLE32(&data[0x18], charVersion);
	if (flags != 0u)
	{
		Rld_WriteLE32(&data[RLDCHAR_CHRI_FLAGS_OFFSET], flags);
	}
	if ((flags & RLDCHAR_FLAG_MAP_COLOR) != 0u)
	{
		Rld_WriteLE32(&data[RLDCHAR_CHRI_MAP_COLOR_OFFSET], mapColor & 0x00FFFFFFu);
	}
	Rld_WriteLE32(&data[fixedSize], 1u);
	Rld_WriteLE32(&data[fixedSize + 4u], (u32)length);
	memcpy(&data[fixedSize + 8u], author, length);

	*sizeOut = size;
	return data;
}

static u8 *RldMk_BuildChri(u32 templateId, u32 classId, const u8 name[RLDCHAR_NAME_FIELD], u32 charVersion, const char *author, u32 flags,
                           size_t *sizeOut)
{
	return RldMk_BuildChriColor(templateId, classId, name, charVersion, author, flags, 0u, sizeOut);
}

// CHRI, CMDL, then CICN (--icon), CMSK (--mask-model) and CVOI (--voices) when
// there are any, in this order, through the shared envelope
// (Rld_WriteEnvelope, rldtrack.inc). Without them two chunks, as before CICN
// existed.
static u8 *RldMk_ContainerVoices(const u8 *chri, size_t chriSize, const u8 *cmdl, size_t cmdlSize, const u8 *cicn, const u8 *cmsk, size_t cmskSize,
                                 const u8 *cvoi, size_t cvoiSize, size_t *sizeOut)
{
	struct RldContainer container;
	u8 *out;
	int i;

	memset(&container, 0, sizeof(container));
	Rld_AddChunk(&container, "CHRI", chri, chriSize, RLD_COMPRESSION_NONE);
	Rld_AddChunk(&container, "CMDL", cmdl, cmdlSize, RLD_COMPRESSION_NONE);
	if (cicn != NULL)
	{
		Rld_AddChunk(&container, "CICN", cicn, RLDCHAR_ICON_BYTES, RLD_COMPRESSION_NONE);
	}
	if (cmsk != NULL)
	{
		Rld_AddChunk(&container, "CMSK", cmsk, cmskSize, RLD_COMPRESSION_NONE);
	}
	if (cvoi != NULL)
	{
		Rld_AddChunk(&container, "CVOI", cvoi, cvoiSize, RLD_COMPRESSION_NONE);
	}

	out = Rld_WriteEnvelope(&s_rldCharFormat, 0u, &container, sizeOut);

	for (i = 0; i < container.chunkCount; i++)
	{
		free(container.chunks[i].stored);
	}

	return out;
}

static u8 *RldMk_ContainerMask(const u8 *chri, size_t chriSize, const u8 *cmdl, size_t cmdlSize, const u8 *cicn, const u8 *cmsk, size_t cmskSize,
                               size_t *sizeOut)
{
	return RldMk_ContainerVoices(chri, chriSize, cmdl, cmdlSize, cicn, cmsk, cmskSize, NULL, 0u, sizeOut);
}

static u8 *RldMk_Container(const u8 *chri, size_t chriSize, const u8 *cmdl, size_t cmdlSize, const u8 *cicn, size_t *sizeOut)
{
	return RldMk_ContainerMask(chri, chriSize, cmdl, cmdlSize, cicn, NULL, 0u, sizeOut);
}

//========================================================================================
// FOREIGN FILES OF LIMITED SIZE (--icon, --voices)
//========================================================================================

// The size of a file without reading it; 0 = it cannot be opened (*ok 0).
static size_t RldMk_FileSize(const char *path, int *ok)
{
	FILE *file = fopen(path, "rb");
	long size = -1;

	*ok = 0;
	if (file == NULL)
	{
		return 0;
	}
	if (fseek(file, 0, SEEK_END) == 0)
	{
		size = ftell(file);
	}
	fclose(file);

	if (size < 0)
	{
		return 0;
	}
	*ok = 1;
	return (size_t)size;
}

// Reads a file only when it is at most `max` bytes: the size is asked first.
// 1 = read; 0 = cannot be opened or read (*tooLarge 0), or larger than max
// (*tooLarge 1, *sizeOut the size, nothing read).
static int RldMk_ReadLimited(const char *path, size_t max, u8 **dataOut, size_t *sizeOut, int *tooLarge)
{
	int ok = 0;
	const size_t size = RldMk_FileSize(path, &ok);

	*dataOut = NULL;
	*sizeOut = size;
	*tooLarge = 0;
	if (!ok)
	{
		return 0;
	}
	if (size > max)
	{
		*tooLarge = 1;
		return 0;
	}

	if (!Rld_ReadFile(path, dataOut, sizeOut))
	{
		return 0;
	}

	// It may have grown in between.
	if (*sizeOut > max)
	{
		free(*dataOut);
		*dataOut = NULL;
		*tooLarge = 1;
		return 0;
	}

	return 1;
}

//========================================================================================
// STEP 14: THE PORTRAIT (CICN) FROM A PNG
//========================================================================================
//
// The PNG (include/rldpng.inc, 8-bit RGBA) -> 44 x 26 texels, 4 bits, 16 colors
// (layout and rules CICN-1..3: include/rldchar.inc). The menu tile shows only
// 43 x 25 of them: it draws the own portrait at the size of the template's
// (platform/native_chars.c, NativeChar_PortraitLayout), and that one is
// 43 x 25 (the game's log line "portrait geometry"). So the picture is fitted
// to those 43 x 25:
//   1  cut to 43:25 in the middle: the longer side loses the same on both ends
//      (one pixel more at the end when it is odd)
//   2  a box filter to 43 x 25 with exact shares, in integers, as Rld_ScaleMap
//      (include/rldtrack.inc): every texel averages the area it covers, the
//      colors weighted by their alpha; a texel less than half covered is
//      transparent (index 0)
//   3  column 43 repeats column 42, row 25 repeats row 24: the texels the tile
//      does not show, filled so that no seam appears if one ever is shown
//   4  the colors of the opaque texels -> at most 15 by median cut and two
//      k-means rounds (RldMk_Palette, the same as the model's colors)
//   5  RGB555 in CLUT entries 1..15, entry 0 = 0x0000 (transparent); a color
//      that rounds to 0x0000 becomes 0x0421, the PS1's opaque black
// A picture of exactly 43 x 25 becomes those texels one to one; one of exactly
// 44 x 26 fills all of CICN one to one, as before the fit to 43 x 25. A picture
// smaller than 43 x 25 is enlarged the same way (and looks blurry).
//
// THREE OPTIONS, each off by default - without them the CICN is the same, byte
// for byte (RLDMK_GOLDEN_SIZE_ICON):
//   --icon-background corners  before step 1, the plain color around the
//      subject becomes transparent (RldMk_ClearBackground); auto does that only
//      for a PNG without a single transparent pixel
//   --icon-fit fit|fill  step 1 cuts around the subject (the pixels at least
//      half opaque) instead of the whole picture and places it where a retail
//      portrait has its head (RLDMK_ICON_HEAD_*): fit shows all of it, fill
//      fills the head box and cuts the rest - at the sides, or above and below
//      the face (half the subject's width below its top). One factor for both
//      axes: never stretched
//   --icon-frame retail  after step 2, the retail frame and its dark box
//      behind the subject (RLDMK_ICON_FRAME_*): two CLUT entries, 13 colors
//      for the subject

#define RLDMK_ICON_BACKGROUND_ALPHA 0u     // the PNG's alpha, nothing else (as before)
#define RLDMK_ICON_BACKGROUND_CORNERS 1u   // and the plain color around the subject
#define RLDMK_ICON_BACKGROUND_AUTO 2u      // CORNERS for a PNG without transparent pixels, else ALPHA
#define RLDMK_ICON_FIT_NONE 0u             // the whole picture cut to 43:25 (as before)
#define RLDMK_ICON_FIT_FIT 1u
#define RLDMK_ICON_FIT_FILL 2u
#define RLDMK_ICON_FRAME_NONE 0u
#define RLDMK_ICON_FRAME_RETAIL 1u
#define RLDMK_ICON_TOLERANCE 24u           // --icon-tolerance default, per channel of 255
#define RLDMK_ICON_TOLERANCE_MAX 128u

// THE HEAD BOX, measured once on the 16 driver portraits of the disc (43 x 25
// each, 4 bits; numbers only, no pixels): the head - every texel that is
// neither transparent, nor the frame, nor the dark box - is 34..43 texels wide
// (median 41) and 24..25 high (median 25), starts at x 0 in 13 of 16 and ends
// at x 37..42 (median 40), reaches the bottom row in 15 and the top row in 15;
// its center of mass lies at x
// 18.4..23.0 (median 20.2), y 9.9..14.3 (median 11.2). So the subject goes into
// x 0..40, y 0..24: centered across, standing on the bottom row.
#define RLDMK_ICON_HEAD_X 0
#define RLDMK_ICON_HEAD_W 41
#define RLDMK_ICON_HEAD_H 25

// THE RETAIL FRAME, measured on the same portraits: 2 texels wide at the sides
// and 1 at the top and bottom, in the opaque near-black 0x0400 (STP clear),
// at x 2..38 and y 1..23 (9 of the 15 portraits with such a frame; 4 end at x
// 40, 2 at x 39; the 16th, Fake Crash, draws the frame in 0x8000 like its
// box); inside it 0x8000, black WITH the STP
// bit (30..246 texels per portrait, median 100). Every place that draws a
// portrait draws it semi-transparent (TRANS_50_DECAL: game/UI/UI_Rank.c,
// game/222.c, game/UI/UI_CupStandings.c, game/230/MM_Characters.c), and then
// a texel with the STP bit halves what lies behind it while one without stays
// opaque (platform/native_shaders.inc, GPU_STP_PASS_FUNC): the box darkens the
// race behind the head, the frame does not let it through. The head lies over
// both and reaches past the frame (29..98 texels per portrait).
#define RLDMK_ICON_FRAME_X0 2
#define RLDMK_ICON_FRAME_X1 38
#define RLDMK_ICON_FRAME_Y0 1
#define RLDMK_ICON_FRAME_Y1 23
#define RLDMK_ICON_FRAME_WIDTH 2
#define RLDMK_ICON_FRAME_COLOR 0x0400u
#define RLDMK_ICON_BOX_COLOR 0x8000u
#define RLDMK_ICON_FRAME_TEXEL 0xFFFFFFFEu   // in rgb[] beside RLDMK_NONE: never a 24-bit color
#define RLDMK_ICON_BOX_TEXEL 0xFFFFFFFDu

struct RldMkIconOptions
{
	u32 background;   // RLDMK_ICON_BACKGROUND_*
	u32 tolerance;    // 0..RLDMK_ICON_TOLERANCE_MAX
	u32 fit;          // RLDMK_ICON_FIT_*
	u32 frame;        // RLDMK_ICON_FRAME_*
};

static const struct RldMkIconOptions s_rldMkIconDefault = {RLDMK_ICON_BACKGROUND_ALPHA, RLDMK_ICON_TOLERANCE, RLDMK_ICON_FIT_NONE,
                                                           RLDMK_ICON_FRAME_NONE};

// The words of the options, by their value (the machine lines say these).
static const char *const s_rldMkIconBackgroundWords[3] = {"alpha", "corners", "auto"};
static const char *const s_rldMkIconFitWords[3] = {"none", "fit", "fill"};
static const char *const s_rldMkIconFrameWords[2] = {"none", "retail"};

// One word of `words` -> its index. 0 = not one of them.
static int RldMk_IconWord(const char *text, const char *const *words, u32 count, u32 *out)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		if (strcmp(text, words[i]) == 0)
		{
			*out = i;
			return 1;
		}
	}
	return 0;
}

// --icon-background, --icon-tolerance, --icon-fit and --icon-frame (each NULL
// when not given) into *options; a value that is not known is said (usage)
// and leaves that option as it was. 1 = all of them taken.
static int RldMk_IconSwitches(const char *background, const char *tolerance, const char *fit, const char *frame, struct RldMkIconOptions *options)
{
	int ok = 1;

	if ((background != NULL) && !RldMk_IconWord(background, s_rldMkIconBackgroundWords, 3u, &options->background))
	{
		RldMk_Say("error", "usage", "--icon-background", "--icon-background is alpha, corners or auto, not '%s'.", background);
		ok = 0;
	}
	if (tolerance != NULL)
	{
		char *end = NULL;
		const unsigned long value = strtoul(tolerance, &end, 10);

		if ((end == tolerance) || (*end != '\0') || (tolerance[0] == '-') || (tolerance[0] == '+') || (value > RLDMK_ICON_TOLERANCE_MAX))
		{
			RldMk_Say("error", "usage", "--icon-tolerance", "--icon-tolerance is a whole number from 0 to %u, not '%s'.", RLDMK_ICON_TOLERANCE_MAX, tolerance);
			ok = 0;
		}
		else
		{
			options->tolerance = (u32)value;
		}
	}
	if ((fit != NULL) && !RldMk_IconWord(fit, s_rldMkIconFitWords, 3u, &options->fit))
	{
		RldMk_Say("error", "usage", "--icon-fit", "--icon-fit is none, fit or fill, not '%s'.", fit);
		ok = 0;
	}
	if ((frame != NULL) && !RldMk_IconWord(frame, s_rldMkIconFrameWords, 2u, &options->frame))
	{
		RldMk_Say("error", "usage", "--icon-frame", "--icon-frame is none or retail, not '%s'.", frame);
		ok = 0;
	}
	return ok;
}

#define RLDMK_ICON_TEXELS (RLDCHAR_ICON_WIDTH * RLDCHAR_ICON_HEIGHT)
#define RLDMK_ICON_SHOWN_WIDTH 43u    // the part of CICN the menu tile shows, from the top left
#define RLDMK_ICON_SHOWN_HEIGHT 25u
#define RLDMK_ICON_SHOWN_TEXELS (RLDMK_ICON_SHOWN_WIDTH * RLDMK_ICON_SHOWN_HEIGHT)
#define RLDMK_ICON_FILE_MAX (16u * 1024u * 1024u)   // PNG file size, asked before reading

struct RldMkIcon
{
	u8 cicn[RLDCHAR_ICON_BYTES];
	u8 index[RLDCHAR_ICON_HEIGHT][RLDCHAR_ICON_WIDTH];   // 0 = transparent
	u16 clut[RLDCHAR_ICON_COLORS];
	u32 source[2];       // width and height of the picture
	u32 crop[4];         // x, y, width, height of the cut in the picture
	u32 scaled[2];       // the texels the cut was scaled to: 43 x 25, or 44 x 26 for a picture of exactly that size
	u32 colorsIn;        // distinct colors of the opaque texels
	u32 colorCount;      // CLUT entries used, 1..15 (1..13 with the frame)
	u32 opaque;          // opaque texels
	u32 cleared;         // pixels the background step made transparent (0 without it)
	u32 backgroundRgb;   // the color it took for the background, 0xRRGGBB
	u32 subject[4];      // x, y, width, height of the subject in the picture (--icon-fit)
	u32 place[4];        // x, y, width, height of the subject in the 43 x 25 (--icon-fit)
};

// --icon-background corners: the plain color around the subject becomes
// transparent.
// THE COLOR: the opaque pixels on the border of the picture, and those beside a
// transparent pixel that a path through transparent pixels joins to the
// border, vote. The most frequent RGB555 value among them (on a tie the lower)
// gives a first color, the average of its voters. It is the background when at
// least half of all voters lie within `tolerance` of it in every channel - a
// background with a little noise still counts - else nothing changes. The
// color is then the average of those voters.
// THE FILL: from the four corners, to the 4 neighbors, through transparent
// pixels and pixels within `tolerance` of that color in every channel. Only the
// area the corners reach is cleared: a pixel of the same color inside the
// subject (an eye, a tooth, a hole it closes) stays, and so does a part of the
// subject that touches the border but is closed off from the corners by its
// outline (a white shirt at the bottom of a white picture).
// THE FRINGE: an opaque pixel beside a cleared one, within twice the tolerance,
// is an edge blended with the background; it keeps alpha in proportion to its
// distance, but never less than half - so at one pixel per texel an outline
// close to the background's color stays.
// THE BOX: when a corner the fill starts from is transparent, the opaque
// part it reaches lies between the transparent surroundings and the subject:
// a box the picture was exported with (it goes), or a part of the subject in
// the color of its outline - the outline itself, dark hair, a dark coat (they
// stay). The fill clears it only when it is a box on both counts:
//   deep: somewhere at least RldMk_BgCore pixels deep (a band twice as wide);
//      a thinner one is the outline (*keptOut = RLDMK_BG_KEPT_LINE)
//   square: at least 3/4 of its pixels beside the transparent surroundings lie
//      on the edges of its own bounding box - a box's edge is straight, the
//      edge of hair or a head is round (RLDMK_BG_KEPT_SHAPE)
//   around: at least 3/4 of the opaque pixels it leaves lie inside its
//      bounding box - a box stands behind the subject, a rectangular body
//      stands below the head (RLDMK_BG_KEPT_AROUND)
// THE SUBJECT: whatever the corners, the fill may not take (nearly) all of the
// picture: when less than 1/20 of the opaque pixels would stay, it took the
// subject, and nothing changes (RLDMK_BG_KEPT_SUBJECT).
// What it cannot tell apart: an outline in the background's own color that
// touches a plain opaque background (black on black) goes with it.
// Integers only, in a fixed order: the same picture gives the same pixels
// everywhere. 1 = done (cleared may be 0); 0 = out of memory.
#define RLDMK_BG_REACHED 1u   // state[]: joined to the border through transparent pixels
#define RLDMK_BG_FILLED 2u    // state[]: reached by the fill from the corners
#define RLDMK_BG_KEPT_LINE 1u      // *keptOut: only a thin line in the color - an outline
#define RLDMK_BG_KEPT_SHAPE 2u     // not shaped like a box - a part of the subject
#define RLDMK_BG_KEPT_SUBJECT 3u   // it would take (nearly) all of the picture
#define RLDMK_BG_KEPT_AROUND 4u    // it does not stand around the rest - a part of the subject

static int RldMk_BgNear(const u8 *p, const u32 color[3], u32 tolerance)
{
	return ((u32)abs((int)p[0] - (int)color[0]) <= tolerance) && ((u32)abs((int)p[1] - (int)color[1]) <= tolerance) &&
	       ((u32)abs((int)p[2] - (int)color[2]) <= tolerance);
}

static u32 RldMk_BgKey(const u8 *p)
{
	return ((u32)p[0] >> 3) | (((u32)p[1] >> 3) << 5) | (((u32)p[2] >> 3) << 10);
}

// A voter: opaque, on the border or beside a pixel of RLDMK_BG_REACHED.
static int RldMk_BgVoter(const u8 *rgba, const u8 *state, u32 width, u32 height, size_t i)
{
	const u32 x = (u32)(i % width);
	const u32 y = (u32)(i / width);

	if (rgba[i * 4u + 3u] < 128u)
	{
		return 0;
	}
	return (x == 0u) || (y == 0u) || (x + 1u == width) || (y + 1u == height) || (state[i - 1u] == RLDMK_BG_REACHED) ||
	       (state[i + 1u] == RLDMK_BG_REACHED) || (state[i - width] == RLDMK_BG_REACHED) || (state[i + width] == RLDMK_BG_REACHED);
}

// How deep the opaque part of the fill has to be somewhere (chessboard
// distance to the nearest pixel outside it) when the corners are transparent:
// 3 pixels, more for a larger picture (1/16 of its shorter side), so that an
// outline that grows with the picture stays an outline.
static u32 RldMk_BgCore(u32 width, u32 height)
{
	const u32 side = (width < height) ? width : height;

	return (side / 16u > 3u) ? (side / 16u) : 3u;
}

static int RldMk_ClearBackground(u8 *rgba, u32 width, u32 height, u32 tolerance, u32 *clearedOut, u32 *colorOut, u32 *keptOut)
{
	const size_t count = (size_t)width * height;
	u8 *state = (u8 *)calloc(count, 1);
	u32 *queue = (u32 *)malloc(count * sizeof(u32));
	u32 *votes = (u32 *)calloc(32768u, sizeof(u32));
	u64 sum[3] = {0, 0, 0};
	u32 total = 0;
	u32 best = 0;
	u32 bestVotes = 0;
	u32 nearCount = 0;
	u32 color[3] = {0, 0, 0};
	size_t head;
	size_t tail;
	size_t i;
	int pass;
	int round;
	int clearSeed = 0;   // a corner the fill starts from is transparent

	*clearedOut = 0;
	*colorOut = 0;
	*keptOut = 0;
	if ((state == NULL) || (queue == NULL) || (votes == NULL))
	{
		free(state);
		free(queue);
		free(votes);
		return 0;
	}

	// Pass 0: the transparent pixels joined to the border, from every border
	// pixel. Pass 1: the fill, from the four corners only.
	for (pass = 0; pass < 2; pass++)
	{
		const u8 mark = (u8)((pass == 0) ? RLDMK_BG_REACHED : RLDMK_BG_FILLED);

		head = 0;
		tail = 0;
		if (pass == 0)
		{
			for (i = 0; i < count; i++)
			{
				const u32 x = (u32)(i % width);
				const u32 y = (u32)(i / width);

				if ((rgba[i * 4u + 3u] < 128u) && ((x == 0u) || (y == 0u) || (x + 1u == width) || (y + 1u == height)))
				{
					state[i] = mark;
					queue[tail++] = (u32)i;
				}
			}
		}
		else
		{
			const size_t corners[4] = {0, (size_t)width - 1u, count - width, count - 1u};
			int k;

			for (k = 0; k < 4; k++)
			{
				const u8 *p = &rgba[corners[k] * 4u];

				if ((state[corners[k]] != mark) && ((p[3] < 128u) || RldMk_BgNear(p, color, tolerance)))
				{
					state[corners[k]] = mark;
					queue[tail++] = (u32)corners[k];
					clearSeed |= (p[3] < 128u);
				}
			}
		}

		while (head < tail)
		{
			const u32 at = queue[head++];
			const u32 x = at % width;
			const u32 y = at / width;
			u32 n[4];
			int k;
			int ways = 0;

			if (x > 0u)
			{
				n[ways++] = at - 1u;
			}
			if (x + 1u < width)
			{
				n[ways++] = at + 1u;
			}
			if (y > 0u)
			{
				n[ways++] = at - width;
			}
			if (y + 1u < height)
			{
				n[ways++] = at + width;
			}

			for (k = 0; k < ways; k++)
			{
				const u8 *p = &rgba[(size_t)n[k] * 4u];

				if (state[n[k]] >= mark)
				{
					continue;
				}
				if ((p[3] < 128u) || ((pass == 1) && RldMk_BgNear(p, color, tolerance)))
				{
					state[n[k]] = mark;
					queue[tail++] = n[k];
				}
			}
		}

		if (pass == 1)
		{
			break;
		}

		// The vote. Round 0: the most frequent RGB555 value; round 1: the
		// average of its voters; round 2: the voters near that average.
		for (i = 0; i < count; i++)
		{
			if (RldMk_BgVoter(rgba, state, width, height, i))
			{
				votes[RldMk_BgKey(&rgba[i * 4u])]++;
				total++;
			}
		}
		for (i = 0; i < 32768u; i++)
		{
			if (votes[i] > bestVotes)
			{
				bestVotes = votes[i];
				best = (u32)i;
			}
		}
		for (round = 1; (round <= 2) && (total > 0u); round++)
		{
			u32 n = 0;

			sum[0] = 0;
			sum[1] = 0;
			sum[2] = 0;
			for (i = 0; i < count; i++)
			{
				const u8 *p = &rgba[i * 4u];

				if (RldMk_BgVoter(rgba, state, width, height, i) && ((round == 1) ? (RldMk_BgKey(p) == best) : RldMk_BgNear(p, color, tolerance)))
				{
					sum[0] += p[0];
					sum[1] += p[1];
					sum[2] += p[2];
					n++;
				}
			}
			if (round == 2)
			{
				nearCount = n;
			}
			if (n == 0u)
			{
				break;
			}
			color[0] = (u32)((sum[0] + n / 2u) / n);
			color[1] = (u32)((sum[1] + n / 2u) / n);
			color[2] = (u32)((sum[2] + n / 2u) / n);
		}
		if ((total == 0u) || ((nearCount * 2u) < total))
		{
			free(state);
			free(queue);
			free(votes);
			return 1;
		}
		*colorOut = (color[0] << 16) | (color[1] << 8) | color[2];
	}

	// The box (see above): from a transparent corner, the opaque pixels of the
	// fill need a core and straight edges. Chessboard distance in two sweeps, in queue[].
	if (clearSeed)
	{
		const u32 core = RldMk_BgCore(width, height);
		u32 deepest = 0;
		int sweep;

		for (sweep = 0; sweep < 2; sweep++)
		{
			for (i = 0; i < count; i++)
			{
				const size_t at = (sweep == 0) ? i : (count - 1u - i);
				const u32 x = (u32)(at % width);
				const u32 y = (u32)(at / width);
				const int dir = (sweep == 0) ? -1 : 1;
				const int yn = (int)y + dir;
				u32 d = 0xFFFFFFFFu;
				int dx;

				if ((state[at] != RLDMK_BG_FILLED) || (rgba[at * 4u + 3u] < 128u))
				{
					queue[at] = 0;
					continue;
				}
				if (sweep == 1)
				{
					d = queue[at];
				}
				// The row behind (three neighbors) and the pixel before in this row.
				for (dx = -1; dx <= 1; dx++)
				{
					const int xn = (int)x + dx;
					const u32 n = ((xn < 0) || (xn >= (int)width) || (yn < 0) || (yn >= (int)height)) ? 0u : queue[(size_t)yn * width + (size_t)xn];

					d = (n + 1u < d) ? (n + 1u) : d;
				}
				{
					const int xn = (int)x + dir;
					const u32 n = ((xn < 0) || (xn >= (int)width)) ? 0u : queue[(size_t)y * width + (size_t)xn];

					d = (n + 1u < d) ? (n + 1u) : d;
				}
				queue[at] = d;
				deepest = (d > deepest) ? d : deepest;
			}
		}
		*keptOut = (deepest < core) ? RLDMK_BG_KEPT_LINE : 0u;

		// Square: its pixels beside the transparent surroundings (which the
		// fill went through), on the edges of its bounding box.
		if (*keptOut == 0u)
		{
			u32 box[4] = {width, height, 0, 0};
			u32 edge = 0;
			u32 straight = 0;

			for (i = 0; i < count; i++)
			{
				const u32 x = (u32)(i % width);
				const u32 y = (u32)(i / width);

				if ((state[i] == RLDMK_BG_FILLED) && (rgba[i * 4u + 3u] >= 128u))
				{
					box[0] = (x < box[0]) ? x : box[0];
					box[1] = (y < box[1]) ? y : box[1];
					box[2] = (x > box[2]) ? x : box[2];
					box[3] = (y > box[3]) ? y : box[3];
				}
			}
			for (i = 0; i < count; i++)
			{
				const u32 x = (u32)(i % width);
				const u32 y = (u32)(i / width);
				int beside = 0;

				if ((state[i] != RLDMK_BG_FILLED) || (rgba[i * 4u + 3u] < 128u))
				{
					continue;
				}
				beside = ((x > 0u) && (state[i - 1u] == RLDMK_BG_FILLED) && (rgba[(i - 1u) * 4u + 3u] < 128u)) ||
				         ((x + 1u < width) && (state[i + 1u] == RLDMK_BG_FILLED) && (rgba[(i + 1u) * 4u + 3u] < 128u)) ||
				         ((y > 0u) && (state[i - width] == RLDMK_BG_FILLED) && (rgba[(i - width) * 4u + 3u] < 128u)) ||
				         ((y + 1u < height) && (state[i + width] == RLDMK_BG_FILLED) && (rgba[(i + width) * 4u + 3u] < 128u));
				if (beside)
				{
					edge++;
					straight += ((x == box[0]) || (x == box[2]) || (y == box[1]) || (y == box[3])) ? 1u : 0u;
				}
			}
			*keptOut = ((straight * 4u) < (edge * 3u)) ? RLDMK_BG_KEPT_SHAPE : 0u;

			// Around: the rest of the opaque pixels inside that box.
			if (*keptOut == 0u)
			{
				u32 rest = 0;
				u32 within = 0;

				for (i = 0; i < count; i++)
				{
					const u32 x = (u32)(i % width);
					const u32 y = (u32)(i / width);

					if ((state[i] != RLDMK_BG_FILLED) && (rgba[i * 4u + 3u] >= 128u))
					{
						rest++;
						within += ((x >= box[0]) && (x <= box[2]) && (y >= box[1]) && (y <= box[3])) ? 1u : 0u;
					}
				}
				*keptOut = ((within * 4u) < (rest * 3u)) ? RLDMK_BG_KEPT_AROUND : 0u;
			}
		}
	}

	// The subject: at least 1/20 of the opaque pixels stays.
	if (*keptOut == 0u)
	{
		u32 opaque = 0;
		u32 filled = 0;

		for (i = 0; i < count; i++)
		{
			if (rgba[i * 4u + 3u] >= 128u)
			{
				opaque++;
				filled += (state[i] == RLDMK_BG_FILLED) ? 1u : 0u;
			}
		}
		*keptOut = ((filled > 0u) && ((u64)(opaque - filled) * 20u < (u64)opaque)) ? RLDMK_BG_KEPT_SUBJECT : 0u;
	}
	if (*keptOut != 0u)
	{
		free(state);
		free(queue);
		free(votes);
		return 1;
	}

	// Cleared: what the fill reached. The fringe: opaque, beside a cleared
	// pixel, its largest channel distance d within twice the tolerance - alpha
	// times (d - tolerance) / tolerance, at least 128.
	for (i = 0; i < count; i++)
	{
		u8 *p = &rgba[i * 4u];

		if ((state[i] == RLDMK_BG_FILLED) && (p[3] >= 128u))
		{
			p[3] = 0;
			(*clearedOut)++;
		}
	}
	for (i = 0; (tolerance > 0u) && (i < count); i++)
	{
		const u32 x = (u32)(i % width);
		const u32 y = (u32)(i / width);
		u8 *p = &rgba[i * 4u];
		u32 d = 0;
		u32 faded;
		int c;

		if ((state[i] == RLDMK_BG_FILLED) || (p[3] < 128u))
		{
			continue;
		}
		if (!(((x > 0u) && (state[i - 1u] == RLDMK_BG_FILLED)) || ((x + 1u < width) && (state[i + 1u] == RLDMK_BG_FILLED)) ||
		      ((y > 0u) && (state[i - width] == RLDMK_BG_FILLED)) || ((y + 1u < height) && (state[i + width] == RLDMK_BG_FILLED))))
		{
			continue;
		}
		for (c = 0; c < 3; c++)
		{
			const u32 e = (u32)abs((int)p[c] - (int)color[c]);

			d = (e > d) ? e : d;
		}
		if ((d > tolerance) && (d < 2u * tolerance))
		{
			faded = ((u32)p[3] * (d - tolerance)) / tolerance;
			p[3] = (u8)((faded < 128u) ? 128u : faded);
		}
	}

	free(state);
	free(queue);
	free(votes);
	return 1;
}

// --icon-background on the picture as read: alpha leaves it, corners clears
// the background (RldMk_ClearBackground), auto does that only for a picture
// without a single pixel that is not fully opaque. *usedOut = alpha or
// corners, what it came to. 1 = done; 0 = out of memory.
static int RldMk_IconBackground(struct RldPngImage *image, const struct RldMkIconOptions *options, u32 *usedOut, u32 *clearedOut, u32 *colorOut, u32 *keptOut)
{
	u32 used = options->background;

	*clearedOut = 0;
	*colorOut = 0;
	*keptOut = 0;
	if (used == RLDMK_ICON_BACKGROUND_AUTO)
	{
		size_t i;

		used = RLDMK_ICON_BACKGROUND_CORNERS;
		for (i = 0; i < (size_t)image->width * image->height; i++)
		{
			if (image->rgba[i * 4u + 3u] != 255u)
			{
				used = RLDMK_ICON_BACKGROUND_ALPHA;
				break;
			}
		}
	}
	*usedOut = used;
	return (used != RLDMK_ICON_BACKGROUND_CORNERS) ||
	       RldMk_ClearBackground(image->rgba, image->width, image->height, options->tolerance, clearedOut, colorOut, keptOut);
}

// The subject: the box of the pixels at least half opaque. 0 = there is none.
static int RldMk_SubjectBox(const struct RldPngImage *image, s64 box[4])
{
	s64 x0 = (s64)image->width;
	s64 y0 = (s64)image->height;
	s64 x1 = -1;
	s64 y1 = -1;
	u32 x;
	u32 y;

	for (y = 0; y < image->height; y++)
	{
		for (x = 0; x < image->width; x++)
		{
			if (image->rgba[(((size_t)y * image->width) + x) * 4u + 3u] >= 128u)
			{
				x0 = ((s64)x < x0) ? (s64)x : x0;
				y0 = ((s64)y < y0) ? (s64)y : y0;
				x1 = ((s64)x > x1) ? (s64)x : x1;
				y1 = ((s64)y > y1) ? (s64)y : y1;
			}
		}
	}

	if (x1 < 0)
	{
		return 0;
	}
	box[0] = x0;
	box[1] = y0;
	box[2] = x1 - x0 + 1;
	box[3] = y1 - y0 + 1;
	return 1;
}

static u32 RldMk_To5(u32 value)
{
	return (value * 31u + 127u) / 255u;
}

static u32 RldMk_From5(u32 value)
{
	return (value * 255u + 15u) / 31u;
}

// 1 = made. The only failure is an image without pixels, which rldpng never
// returns. `image` is the picture after the background step (RldMk_IconStep).
static int RldMk_MakeIconWith(const struct RldPngImage *image, const struct RldMkIconOptions *options, struct RldMkIcon *icon)
{
	const s64 stride = RLDCHAR_ICON_WIDTH;
	const int framed = (options->frame == RLDMK_ICON_FRAME_RETAIL);
	const u32 subjectColors = framed ? (RLDCHAR_ICON_COLORS - 3u) : (RLDCHAR_ICON_COLORS - 1u);
	s64 box[4];
	const int fitted = (options->fit != RLDMK_ICON_FIT_NONE) && (image->rgba != NULL) && RldMk_SubjectBox(image, box);
	const int whole = !fitted && (image->width == RLDCHAR_ICON_WIDTH) && (image->height == RLDCHAR_ICON_HEIGHT);
	const s64 shownW = whole ? (s64)RLDCHAR_ICON_WIDTH : (s64)RLDMK_ICON_SHOWN_WIDTH;
	const s64 shownH = whole ? (s64)RLDCHAR_ICON_HEIGHT : (s64)RLDMK_ICON_SHOWN_HEIGHT;
	s64 tw = shownW;   // the texels the cut is scaled to
	s64 th = shownH;
	s64 dx = 0;        // and where they lie
	s64 dy = 0;
	u32 frameIndex = 0;
	u32 boxIndex = 0;
	struct RldMkColor colors[RLDMK_ICON_TEXELS];
	u32 rgb[RLDMK_ICON_TEXELS];
	u32 sorted[RLDMK_ICON_TEXELS];
	u32 palette[RLDCHAR_COLORS_MAX];
	u32 opaqueCount = 0;
	u32 unique = 0;
	s64 sw;
	s64 sh;
	s64 left;
	s64 top;
	s64 X;
	s64 Y;
	u32 i;

	memset(icon, 0, sizeof(*icon));
	if ((image->rgba == NULL) || (image->width == 0u) || (image->height == 0u))
	{
		return 0;
	}

	if (fitted)
	{
		const s64 w = box[2];
		const s64 h = box[3];

		if (options->fit == RLDMK_ICON_FIT_FIT)
		{
			// 1 (fit): the side that is longer for the head box sets the one
			// factor; the cut then takes exactly tw:th around the subject, the
			// extra above it (it stands on the bottom row) and on both sides -
			// beyond the picture's edge it is transparent.
			if ((w * RLDMK_ICON_HEAD_H) >= (h * RLDMK_ICON_HEAD_W))
			{
				tw = RLDMK_ICON_HEAD_W;
				th = (h * RLDMK_ICON_HEAD_W + w / 2) / w;
			}
			else
			{
				th = RLDMK_ICON_HEAD_H;
				tw = (w * RLDMK_ICON_HEAD_H + h / 2) / h;
			}
			tw = (tw < 1) ? 1 : tw;
			th = (th < 1) ? 1 : th;
			if ((w * th) > (h * tw))
			{
				sw = w;
				sh = (w * th + tw - 1) / tw;
			}
			else
			{
				sh = h;
				sw = (h * tw + th - 1) / th;
			}
			left = box[0] - (sw - w) / 2;
			top = box[1] + h - sh;
		}
		else
		{
			// 1 (fill): the head box filled; the longer side is cut. Across:
			// the same at both sides. Upright: around the face, which lies
			// about half the subject's width below its top - a head is about
			// as tall as it is wide, and the head of a bust or a figure is at
			// its top and no wider than it. Cut from the top, a tall picture
			// kept only the top of the head; cut in the middle, a bust kept its
			// chest.
			tw = RLDMK_ICON_HEAD_W;
			th = RLDMK_ICON_HEAD_H;
			if ((w * th) > (h * tw))
			{
				sh = h;
				sw = (h * tw + th / 2) / th;
				sw = (sw < 1) ? 1 : sw;
				left = box[0] + (w - sw) / 2;
				top = box[1];
			}
			else
			{
				s64 face;

				sw = w;
				sh = (w * th + tw / 2) / tw;
				sh = (sh < 1) ? 1 : sh;
				left = box[0];
				face = ((w < h) ? w : h) / 2 - sh / 2;
				top = box[1] + ((face < 0) ? 0 : ((face > (h - sh)) ? (h - sh) : face));
			}
		}
		dx = RLDMK_ICON_HEAD_X + (RLDMK_ICON_HEAD_W - tw) / 2;
		dy = (s64)RLDMK_ICON_SHOWN_HEIGHT - th;
		icon->subject[0] = (u32)box[0];
		icon->subject[1] = (u32)box[1];
		icon->subject[2] = (u32)box[2];
		icon->subject[3] = (u32)box[3];
		icon->place[0] = (u32)dx;
		icon->place[1] = (u32)dy;
		icon->place[2] = (u32)tw;
		icon->place[3] = (u32)th;
	}
	else
	{
		// 1: the cut. Wider than tw:th -> the full height, else the full width.
		if (((s64)image->width * th) > ((s64)image->height * tw))
		{
			sh = image->height;
			sw = ((s64)image->height * tw + th / 2) / th;
			sw = (sw < 1) ? 1 : ((sw > (s64)image->width) ? (s64)image->width : sw);
		}
		else
		{
			sw = image->width;
			sh = ((s64)image->width * th + tw / 2) / tw;
			sh = (sh < 1) ? 1 : ((sh > (s64)image->height) ? (s64)image->height : sh);
		}
		left = ((s64)image->width - sw) / 2;
		top = ((s64)image->height - sh) / 2;
	}
	icon->crop[0] = (u32)((left < 0) ? 0 : left);
	icon->crop[1] = (u32)((top < 0) ? 0 : top);
	icon->crop[2] = (u32)sw;
	icon->crop[3] = (u32)sh;
	icon->scaled[0] = (u32)tw;
	icon->scaled[1] = (u32)th;

	// Texels the scaled cut does not cover (--icon-fit) stay transparent.
	for (i = 0; i < (u32)RLDMK_ICON_TEXELS; i++)
	{
		rgb[i] = RLDMK_NONE;
	}

	// 2: the box filter. Texel X covers the picture's x from X*sw to (X+1)*sw,
	// measured in parts of 1/tw; pixel x lies at x*tw to (x+1)*tw. The same
	// vertically. The shares of one texel add up to sw*sh. A pixel beyond the
	// picture (only a fitted cut reaches there) counts as transparent.
	for (Y = 0; Y < th; Y++)
	{
		for (X = 0; X < tw; X++)
		{
			s64 weight = 0;
			s64 cover = 0;
			s64 sum[3] = {0, 0, 0};
			s64 y;

			for (y = (Y * sh) / th; (y < sh) && ((y * th) < ((Y + 1) * sh)); y++)
			{
				const s64 y0 = ((Y * sh) > (y * th)) ? (Y * sh) : (y * th);
				const s64 y1 = (((Y + 1) * sh) < ((y + 1) * th)) ? ((Y + 1) * sh) : ((y + 1) * th);
				s64 x;

				for (x = (X * sw) / tw; (x < sw) && ((x * tw) < ((X + 1) * sw)); x++)
				{
					const s64 x0 = ((X * sw) > (x * tw)) ? (X * sw) : (x * tw);
					const s64 x1 = (((X + 1) * sw) < ((x + 1) * tw)) ? ((X + 1) * sw) : ((x + 1) * tw);
					const s64 part = (x1 - x0) * (y1 - y0);
					const u8 *pixel;
					int c;

					if (part <= 0)
					{
						continue;
					}

					weight += part;
					if (((top + y) < 0) || ((left + x) < 0) || ((top + y) >= (s64)image->height) || ((left + x) >= (s64)image->width))
					{
						continue;
					}
					pixel = &image->rgba[(((size_t)(top + y) * image->width) + (size_t)(left + x)) * 4u];
					cover += part * (s64)pixel[3];
					for (c = 0; c < 3; c++)
					{
						sum[c] += part * (s64)pixel[3] * (s64)pixel[c];
					}
				}
			}

			// Below half covered: transparent. rgb[] keeps the average color of the rest.
			if ((cover == 0) || ((cover * 2) < (weight * 255)))
			{
				rgb[(dy + Y) * stride + dx + X] = RLDMK_NONE;
				continue;
			}

			rgb[(dy + Y) * stride + dx + X] =
			    ((u32)((sum[0] + cover / 2) / cover) << 16) | ((u32)((sum[1] + cover / 2) / cover) << 8) | (u32)((sum[2] + cover / 2) / cover);
		}
	}

	// 2b (--icon-frame retail): where the subject leaves the texels
	// transparent, the frame - 2 texels at the sides, 1 at the top and bottom,
	// as the retail portraits draw it - and inside it the dark box.
	if (framed)
	{
		for (Y = RLDMK_ICON_FRAME_Y0; Y <= RLDMK_ICON_FRAME_Y1; Y++)
		{
			for (X = RLDMK_ICON_FRAME_X0; X <= RLDMK_ICON_FRAME_X1; X++)
			{
				const int edge = (X < (RLDMK_ICON_FRAME_X0 + RLDMK_ICON_FRAME_WIDTH)) || (X > (RLDMK_ICON_FRAME_X1 - RLDMK_ICON_FRAME_WIDTH)) ||
				                 (Y == RLDMK_ICON_FRAME_Y0) || (Y == RLDMK_ICON_FRAME_Y1);

				if (rgb[Y * stride + X] == RLDMK_NONE)
				{
					rgb[Y * stride + X] = edge ? RLDMK_ICON_FRAME_TEXEL : RLDMK_ICON_BOX_TEXEL;
				}
			}
		}
	}

	// 3: the texels the tile does not show repeat their neighbors.
	for (Y = 0; Y < shownH; Y++)
	{
		for (X = shownW; X < stride; X++)
		{
			rgb[Y * stride + X] = rgb[Y * stride + shownW - 1];
		}
	}
	for (Y = shownH; Y < (s64)RLDCHAR_ICON_HEIGHT; Y++)
	{
		memcpy(&rgb[Y * stride], &rgb[(shownH - 1) * stride], (size_t)stride * sizeof(u32));
	}

	// Every texel of CICN, the copies included: they weigh in the palette like
	// the texels they repeat (counting them or not changes the palette of a
	// picture of exactly 43 x 25, which is taken one to one). Frame and box
	// have CLUT entries of their own.
	for (i = 0; i < (u32)RLDMK_ICON_TEXELS; i++)
	{
		if (rgb[i] <= 0xFFFFFFu)
		{
			sorted[opaqueCount++] = rgb[i];
		}
	}
	icon->opaque = opaqueCount;

	// 4: the palette of the opaque texels, as the model's colors (step 7).
	qsort(sorted, opaqueCount, sizeof(u32), RldMk_CompareU32);
	for (i = 0; i < opaqueCount; i++)
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
	icon->colorsIn = unique;
	icon->colorCount = RldMk_Palette(colors, unique, subjectColors, palette);

	// colors[] back in value order for the lookup (red is the top byte).
	s_rldMkSortChannel = 0;
	qsort(colors, unique, sizeof(struct RldMkColor), RldMk_CompareColor);

	// 5: the CLUT.
	icon->clut[0] = (u16)RLDCHAR_ICON_TRANSPARENT;
	for (i = 0; i < icon->colorCount; i++)
	{
		const u32 value = RldMk_To5((palette[i] >> 16) & 0xFFu) | (RldMk_To5((palette[i] >> 8) & 0xFFu) << 5) | (RldMk_To5(palette[i] & 0xFFu) << 10);

		icon->clut[i + 1u] = (u16)((value != 0u) ? value : RLDCHAR_ICON_BLACK);
	}
	if (framed)
	{
		frameIndex = icon->colorCount + 1u;
		boxIndex = icon->colorCount + 2u;
		icon->clut[frameIndex] = (u16)RLDMK_ICON_FRAME_COLOR;
		icon->clut[boxIndex] = (u16)RLDMK_ICON_BOX_COLOR;
	}

	for (Y = 0; Y < (s64)RLDCHAR_ICON_HEIGHT; Y++)
	{
		for (X = 0; X < stride; X++)
		{
			const u32 value = rgb[Y * stride + X];
			u32 low = 0;
			u32 high = unique;

			if (value > 0xFFFFFFu)
			{
				icon->index[Y][X] = (u8)((value == RLDMK_ICON_FRAME_TEXEL) ? frameIndex : ((value == RLDMK_ICON_BOX_TEXEL) ? boxIndex : 0u));
				continue;
			}

			while (low < high)
			{
				const u32 mid = low + (high - low) / 2u;

				if (colors[mid].rgb < value)
				{
					low = mid + 1u;
				}
				else
				{
					high = mid;
				}
			}
			icon->index[Y][X] = (u8)(colors[low].palette + 1u);
		}
	}

	// The chunk.
	Rld_WriteLE16(&icon->cicn[0x00], RLDCHAR_ICON_VERSION);
	Rld_WriteLE16(&icon->cicn[0x02], RLDCHAR_ICON_DEPTH);
	Rld_WriteLE16(&icon->cicn[0x04], RLDCHAR_ICON_WIDTH);
	Rld_WriteLE16(&icon->cicn[0x06], RLDCHAR_ICON_HEIGHT);
	for (i = 0; i < RLDCHAR_ICON_COLORS; i++)
	{
		Rld_WriteLE16(&icon->cicn[0x08u + 2u * i], icon->clut[i]);
	}
	for (Y = 0; Y < (s64)RLDCHAR_ICON_HEIGHT; Y++)
	{
		for (X = 0; X < stride; X += 4)
		{
			const u32 word = (u32)icon->index[Y][X] | ((u32)icon->index[Y][X + 1] << 4) | ((u32)icon->index[Y][X + 2] << 8) | ((u32)icon->index[Y][X + 3] << 12);

			Rld_WriteLE16(&icon->cicn[RLDCHAR_ICON_WORDS_AT + 2u * ((u32)Y * RLDCHAR_ICON_ROW_WORDS + (u32)X / 4u)], word);
		}
	}

	return 1;
}

// The portrait as before the options (the self-test and the golden hash).
static int RldMk_MakeIcon(const struct RldPngImage *image, struct RldMkIcon *icon)
{
	return RldMk_MakeIconWith(image, &s_rldMkIconDefault, icon);
}

// A 32-bit BMP (BITMAPINFOHEADER, BI_RGB, rows bottom-up), the alpha in the
// fourth byte of each pixel (B, G, R, A). For Reload Studio's side-by-side view.
static int RldMk_WriteBmp(const char *path, u32 width, u32 height, const u8 *rgba)
{
	const size_t pixels = (size_t)width * height * 4u;
	u8 head[54];
	u8 *row;
	FILE *file;
	u32 y;
	int ok = 1;

	memset(head, 0, sizeof(head));
	head[0] = 'B';
	head[1] = 'M';
	Rld_WriteLE32(&head[2], (u32)(sizeof(head) + pixels));
	Rld_WriteLE32(&head[10], (u32)sizeof(head));
	Rld_WriteLE32(&head[14], 40u);
	Rld_WriteLE32(&head[18], width);
	Rld_WriteLE32(&head[22], height);
	Rld_WriteLE16(&head[26], 1u);
	Rld_WriteLE16(&head[28], 32u);
	Rld_WriteLE32(&head[34], (u32)pixels);
	Rld_WriteLE32(&head[38], 2835u);
	Rld_WriteLE32(&head[42], 2835u);

	row = (u8 *)malloc((size_t)width * 4u + 1u);
	file = fopen(path, "wb");
	if ((row == NULL) || (file == NULL) || (fwrite(head, 1, sizeof(head), file) != sizeof(head)))
	{
		free(row);
		if (file != NULL)
		{
			fclose(file);
		}
		return 0;
	}

	for (y = 0; ok && (y < height); y++)
	{
		const u8 *source = &rgba[(size_t)(height - 1u - y) * width * 4u];
		u32 x;

		for (x = 0; x < width; x++)
		{
			row[4u * x + 0u] = source[4u * x + 2u];
			row[4u * x + 1u] = source[4u * x + 1u];
			row[4u * x + 2u] = source[4u * x + 0u];
			row[4u * x + 3u] = source[4u * x + 3u];
		}
		ok = (fwrite(row, 1, (size_t)width * 4u, file) == ((size_t)width * 4u));
	}

	free(row);
	return (fclose(file) == 0) && ok;
}

static void RldMk_EmitWritten(const char *kind, const char *path, int ok, size_t bytes)
{
	RldMk_EmitFile(kind, ok ? "ok" : "failed", path, ok ? bytes : 0u);
	if (!ok)
	{
		RldMk_Say("warning", "icon-preview", path, "The picture %s cannot be written. Is the folder write-protected?", path);
	}
}

// THE TEMPLATE'S RETAIL PORTRAIT, for Reload Studio's side-by-side view only
// (--icon-preview). Read at the moment from the game's own data, found the way
// Rld_FindHwl finds KART.HWL: assets\BIGFILE.BIG, or BIGFILE.BIG inside
// assets\ctr-u.bin, next to this program or up to two folders above it.
// BIGFILE entry 258 is shared.vrm, the 16-bit pictures the game puts into VRAM
// at boot (game/MAIN/MainMain.c, LOAD_VramFile 0x102; one picture, or 0x20 and
// a chain of them: game/LOAD/LOAD_File.c, LOAD_VramFileCallback); entry 259 is
// the shared pack, whose first word leads to its icon table (struct
// LevTexLookup, game/DecalGlobal.c). The icon with the template's iconID
// (game/zGlobal_DATA.c, MetaDataCharacters) gives page, CLUT and corners.
// Nothing of it stays: the pixels go to <prefix>-retail.bmp in the Studio's
// temporary folder, never into a container. No retail pixels are written in
// this file - only where they are. 1 = rgba holds the 43 x 25 portrait, STP
// texels at alpha 128 (drawn at half, as the game draws them).
#define RLDMK_BIG_SHARED_VRM 258u
#define RLDMK_BIG_SHARED_PACK 259u
#define RLDMK_BIG_ENTRIES_MAX 4096u
#define RLDMK_BIG_ENTRY_MAX (4u * 1024u * 1024u)
#define RLDMK_SECTOR 2048u

// MetaDataCharacters[id].iconID per template (game/zGlobal_DATA.c).
static const u8 s_rldMkIconId[RLDMK_TEMPLATE_COUNT] = {32, 33, 34, 35, 36, 37, 38, 39, 43, 41, 40, 42, 44, 53, 54};

struct RldMkBig
{
	FILE *loose;                       // assets\BIGFILE.BIG, or NULL
	struct NativeDiscImageFile inImage; // BIGFILE.BIG inside ctr-u.bin when loose is NULL
};

// `count` sectors from `sector` of BIGFILE.BIG. 1 = read.
static int RldMk_BigSectors(const struct RldMkBig *big, u32 sector, u32 count, u8 *dst)
{
	if (big->loose != NULL)
	{
		return (fseek(big->loose, (long)sector * (long)RLDMK_SECTOR, SEEK_SET) == 0) &&
		       (fread(dst, 1, (size_t)count * RLDMK_SECTOR, big->loose) == (size_t)count * RLDMK_SECTOR);
	}
	return NativeDiscImage_ReadDataSectors(&big->inImage, sector, count, dst);
}

// One entry of BIGFILE.BIG (struct BigHeader, include/namespace_Load.h), at most
// RLDMK_BIG_ENTRY_MAX bytes. NULL = not there.
static u8 *RldMk_BigEntry(const struct RldMkBig *big, u32 index, u32 *sizeOut)
{
	u8 first[RLDMK_SECTOR];
	u8 *table;
	u8 *data;
	u32 count;
	u32 tableSectors;
	u32 offset;
	u32 size;

	*sizeOut = 0;
	if (!RldMk_BigSectors(big, 0u, 1u, first))
	{
		return NULL;
	}
	count = Rld_ReadLE32(&first[4]);
	if ((count <= index) || (count > RLDMK_BIG_ENTRIES_MAX))
	{
		return NULL;
	}
	tableSectors = (8u + 8u * count + RLDMK_SECTOR - 1u) / RLDMK_SECTOR;
	table = (u8 *)malloc((size_t)tableSectors * RLDMK_SECTOR);
	if ((table == NULL) || !RldMk_BigSectors(big, 0u, tableSectors, table))
	{
		free(table);
		return NULL;
	}
	offset = Rld_ReadLE32(&table[8u + 8u * index]);
	size = Rld_ReadLE32(&table[12u + 8u * index]);
	free(table);
	if ((size == 0u) || (size > RLDMK_BIG_ENTRY_MAX))
	{
		return NULL;
	}

	data = (u8 *)malloc(((size_t)size + RLDMK_SECTOR - 1u) / RLDMK_SECTOR * RLDMK_SECTOR);
	if ((data == NULL) || !RldMk_BigSectors(big, offset, (size + RLDMK_SECTOR - 1u) / RLDMK_SECTOR, data))
	{
		free(data);
		return NULL;
	}
	*sizeOut = size;
	return data;
}

// The VRAM word at x, y as shared.vrm leaves it; 0 where it puts nothing.
static u32 RldMk_VrmWord(const u8 *vrm, u32 size, u32 x, u32 y)
{
	u32 at = 0;
	u32 next = 0;
	int chain = (size >= 8u) && (Rld_ReadLE32(&vrm[0]) == 0x20u);

	if (chain)
	{
		at = 8u;
		next = Rld_ReadLE32(&vrm[4]);
	}
	// at only grows, by 4 bytes at least, and every sum is taken in 64 bits: a
	// damaged chain ends the walk, it never wraps around.
	while ((u64)at + 0x14u <= (u64)size)
	{
		const u32 rx = Rld_ReadLE16(&vrm[at + 0x0Cu]);
		const u32 ry = Rld_ReadLE16(&vrm[at + 0x0Eu]);
		const u32 rw = Rld_ReadLE16(&vrm[at + 0x10u]);
		const u32 rh = Rld_ReadLE16(&vrm[at + 0x12u]);

		if ((x >= rx) && (x < rx + rw) && (y >= ry) && (y < ry + rh))
		{
			const u64 word = (u64)at + 0x14u + 2u * ((u64)(y - ry) * rw + (x - rx));

			return (word + 2u <= (u64)size) ? Rld_ReadLE16(&vrm[(size_t)word]) : 0u;
		}
		if (!chain || ((next & ~3u) == 0u) || ((u64)at + (next & ~3u) + 4u > (u64)size))
		{
			break;
		}
		at += next & ~3u;
		next = Rld_ReadLE32(&vrm[at]);
		at += 4u;
	}
	return 0u;
}

// The icon `iconId` of the shared pack, its texels through shared.vrm, into
// width x height RGBA: 0x0000 transparent, a word with the STP bit at alpha
// 128, every other opaque. Every offset is checked against the data. 1 = found.
static int RldMk_RetailIcon(const u8 *vrm, u32 vrmSize, const u8 *pack, u32 packSize, u32 iconId, u8 *rgba, u32 width, u32 height)
{
	// The pack: u32 size, then the data; its pointers are offsets into the data.
	const u8 *packData = pack + 4u;
	const u32 dataSize = (packSize >= 4u) ? (packSize - 4u) : 0u;
	u32 lookup;
	u32 icons = 0;
	u32 iconCount = 0;
	u32 k;

	if (dataSize < 20u)
	{
		return 0;
	}
	lookup = Rld_ReadLE32(&packData[0]);
	if (lookup <= (dataSize - 16u))
	{
		iconCount = Rld_ReadLE32(&packData[lookup]);
		icons = Rld_ReadLE32(&packData[lookup + 4u]);
	}
	for (k = 0; (k < iconCount) && (icons <= dataSize) && (k < (dataSize - icons) / 0x20u); k++)
	{
		const u8 *icon = &packData[icons + 0x20u * k];
		u32 u0;
		u32 v0;
		u32 clut;
		u32 tpage;
		u32 pageX;
		u32 pageY;
		u32 clutX;
		u32 clutY;
		u32 x;
		u32 y;

		if (Rld_ReadLE32(&icon[0x10]) != iconId)
		{
			continue;
		}
		u0 = icon[0x14];
		v0 = icon[0x15];
		clut = Rld_ReadLE16(&icon[0x16]);
		tpage = Rld_ReadLE16(&icon[0x1A]);
		if (((tpage >> 7) & 3u) != 0u)   // a portrait is 4 bits; anything else is not drawn here
		{
			return 0;
		}
		pageX = (tpage & 0xFu) * 64u;
		pageY = ((tpage >> 4) & 1u) * 256u;
		clutX = (clut & 0x3Fu) * 16u;
		clutY = (clut >> 6) & 0x1FFu;
		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				const u32 word = RldMk_VrmWord(vrm, vrmSize, pageX + (u0 + x) / 4u, pageY + v0 + y);
				const u32 c = RldMk_VrmWord(vrm, vrmSize, clutX + ((word >> (4u * ((u0 + x) & 3u))) & 0xFu), clutY);
				u8 *pixel = &rgba[(y * width + x) * 4u];

				pixel[0] = (u8)RldMk_From5(c & 31u);
				pixel[1] = (u8)RldMk_From5((c >> 5) & 31u);
				pixel[2] = (u8)RldMk_From5((c >> 10) & 31u);
				pixel[3] = (u8)((c == 0u) ? 0u : ((c & 0x8000u) ? 128u : 255u));
			}
		}
		return 1;
	}
	return 0;
}

static int RldMk_RetailPortrait(const char *argv0, u32 templateId, u8 *rgba, u32 width, u32 height)
{
	struct RldMkBig big;
	char dir[RLD_PATH_MAX];
	u8 *vrm = NULL;
	u8 *pack = NULL;
	u32 vrmSize = 0;
	u32 packSize = 0;
	int found = 0;
	int level;

	memset(&big, 0, sizeof(big));
	if ((templateId >= (u32)RLDMK_TEMPLATE_COUNT) || !Rld_ExeDir(argv0, dir, sizeof(dir)))
	{
		return 0;
	}

	for (level = 0; (level < 3) && !found; level++)
	{
		char loose[RLD_PATH_MAX + 32];
		char image[RLD_PATH_MAX + 32];
		char parent[RLD_PATH_MAX];

		snprintf(loose, sizeof(loose), "%s" RLD_SEP "assets" RLD_SEP "BIGFILE.BIG", dir);
		snprintf(image, sizeof(image), "%s" RLD_SEP "assets" RLD_SEP "ctr-u.bin", dir);
		big.loose = fopen(loose, "rb");
		found = (big.loose != NULL) ||
		        (Rld_FileExists(image) && NativeDiscImage_OpenImagePath(image) && NativeDiscImage_FindFile("BIGFILE.BIG", &big.inImage));
		if (!found && Rld_ParentDir(dir, parent, sizeof(parent)))
		{
			snprintf(dir, sizeof(dir), "%s", parent);
		}
	}
	if (!found)
	{
		return 0;
	}

	vrm = RldMk_BigEntry(&big, RLDMK_BIG_SHARED_VRM, &vrmSize);
	pack = RldMk_BigEntry(&big, RLDMK_BIG_SHARED_PACK, &packSize);
	if (big.loose != NULL)
	{
		fclose(big.loose);
	}
	found = (vrm != NULL) && (pack != NULL) && RldMk_RetailIcon(vrm, vrmSize, pack, packSize, s_rldMkIconId[templateId], rgba, width, height);

	free(vrm);
	free(pack);
	return found;
}

// --icon: the PNG read, turned into CICN (built twice: the same bytes), checked
// with the reader the game will run, the findings said. --icon-preview writes
// <prefix>-original.bmp (the PNG as read), <prefix>-icon.bmp (43 x 25 as the
// menu tile shows it; texels with the STP bit at alpha 128, the half the game
// draws them with) and, when the game's data is found, <prefix>-retail.bmp
// (the template's retail portrait, RldMk_RetailPortrait). 1 = *icon holds a
// sound portrait.
static int RldMk_IconStep(const char *path, const char *previewPrefix, const struct RldMkIconOptions *options, const char *argv0, u32 templateId,
                          struct RldMkIcon *icon)
{
	static struct RldMkIcon again;
	struct RldPngImage image;
	const char *why = NULL;
	u8 *data = NULL;
	size_t size = 0;
	int tooLarge = 0;
	int result;
	u32 background = RLDMK_ICON_BACKGROUND_ALPHA;
	u32 kept = 0;
	int hadAlpha = 0;
	u32 cleared = 0;
	u32 backgroundRgb = 0;

	if (!RldMk_ReadLimited(path, RLDMK_ICON_FILE_MAX, &data, &size, &tooLarge))
	{
		if (tooLarge)
		{
			RldMk_EmitFile("icon", "bad", path, size);
			RldMk_Say("error", "icon-file", path,
			          "The icon %s is %u MB; rldpack reads PNG files of at most 16 MB. Save it smaller - a portrait is 43 x 25, a few hundred pixels are plenty.",
			          path, (u32)((size + 1048575u) / 1048576u));
			return 0;
		}

		RldMk_EmitFile("icon", "missing", path, 0u);
		RldMk_Say("error", "icon-file", path, "The icon file %s cannot be opened. Check the path, or leave the icon out - the game then shows the template's portrait.",
		          path);
		return 0;
	}

	result = RldPng_Decode(data, size, &image, &why);
	free(data);

	if (result != RLDPNG_OK)
	{
		RldMk_EmitFile("icon", "bad", path, size);
		if (result == RLDPNG_TOO_LARGE)
		{
			RldMk_Say("error", "icon-file", why, "The icon %s is too large (%s). Scale it down to at most 4096 x 4096 pixels - 43 x 25 or a multiple is best.", path,
			          why);
		}
		else if (result == RLDPNG_MEMORY)
		{
			RldMk_OutOfMemory();
		}
		else
		{
			RldMk_Say("error", "icon-file", why, "The icon %s is not a PNG rldpack can read (%s). Save it again as PNG (8 bit).", path, why);
		}
		return 0;
	}

	RldMk_EmitFile("icon", "ok", path, size);

	if (previewPrefix != NULL)
	{
		char out[RLD_PATH_MAX + 32];
		int ok;

		snprintf(out, sizeof(out), "%s-original.bmp", previewPrefix);
		ok = RldMk_WriteBmp(out, image.width, image.height, image.rgba);
		RldMk_EmitWritten("icon-original", out, ok, 54u + (size_t)image.width * image.height * 4u);
	}

	// --icon-background, on the picture as read and before the cut. A picture
	// with transparency of its own is told about every pixel the step takes.
	{
		size_t i;

		for (i = 0; !hadAlpha && (i < (size_t)image.width * image.height); i++)
		{
			hadAlpha = (image.rgba[i * 4u + 3u] != 255u);
		}
	}
	if (!RldMk_IconBackground(&image, options, &background, &cleared, &backgroundRgb, &kept))
	{
		RldPng_Free(&image);
		RldMk_OutOfMemory();
		return 0;
	}

	if (!RldMk_MakeIconWith(&image, options, icon) || !RldMk_MakeIconWith(&image, options, &again) || (memcmp(icon->cicn, again.cicn, sizeof(icon->cicn)) != 0) ||
	    (RldChar_CheckIcon(icon->cicn, sizeof(icon->cicn)) != NULL))
	{
		RldMk_Say("error", "icon-roundtrip", NULL, "Internal error: the portrait does not come out the same twice. Please report it.");
		RldPng_Free(&image);
		return 0;
	}
	icon->source[0] = image.width;
	icon->source[1] = image.height;
	icon->cleared = cleared;
	icon->backgroundRgb = backgroundRgb;

	if ((background == RLDMK_ICON_BACKGROUND_CORNERS) && (kept == RLDMK_BG_KEPT_LINE))
	{
		RldMk_Say("note", "icon-background", NULL,
		          "The color around the transparent part is only a thin line, so it was kept as the subject's outline and nothing was made transparent.");
	}
	else if ((background == RLDMK_ICON_BACKGROUND_CORNERS) && (kept == RLDMK_BG_KEPT_SHAPE))
	{
		RldMk_Say("note", "icon-background", NULL,
		          "The color around the transparent part is not shaped like a box (its edge is not straight), so it was kept as part of the subject - dark hair or clothes beside a dark outline - and nothing was made transparent.");
	}
	else if ((background == RLDMK_ICON_BACKGROUND_CORNERS) && (kept == RLDMK_BG_KEPT_AROUND))
	{
		RldMk_Say("note", "icon-background", NULL,
		          "The opaque color beside the transparent part does not stand around the rest of the picture, so it was kept as part of the subject and nothing was made transparent.");
	}
	else if ((background == RLDMK_ICON_BACKGROUND_CORNERS) && (kept == RLDMK_BG_KEPT_SUBJECT))
	{
		RldMk_Say("note", "icon-background", NULL,
		          "The background step would have taken the whole subject (it is all in the background's color), so nothing was made transparent.");
	}
	else if ((background == RLDMK_ICON_BACKGROUND_CORNERS) && (cleared > 0u) && hadAlpha)
	{
		RldMk_Say("note", "icon-background", NULL,
		          "Removed an opaque box of %u pixels behind the picture (color %06X). Untick Make background transparent (--icon-background alpha) to keep it.",
		          cleared, backgroundRgb);
	}
	else if ((background == RLDMK_ICON_BACKGROUND_CORNERS) && (cleared == 0u))
	{
		RldMk_Say("note", "icon-background", NULL,
		          "No plain background was found around the subject, so nothing was made transparent. A single color around it works best.");
	}

	if (s_machine && !s_rldMkQuiet)
	{
		char text[3][32];

		snprintf(text[0], sizeof(text[0]), "%u %u", image.width, image.height);
		snprintf(text[1], sizeof(text[1]), "%u %u %u %u", icon->crop[0], icon->crop[1], icon->crop[2], icon->crop[3]);
		snprintf(text[2], sizeof(text[2]), "%u", icon->colorCount);
		Rld_Emit("char", "icon_source", text[0], (const char *)NULL);
		Rld_Emit("char", "icon_crop", text[1], (const char *)NULL);
		snprintf(text[1], sizeof(text[1]), "%u %u", icon->scaled[0], icon->scaled[1]);
		Rld_Emit("char", "icon_scaled", text[1], (const char *)NULL);
		Rld_Emit("char", "icon_colors", text[2], (const char *)NULL);
		snprintf(text[2], sizeof(text[2]), "%u", icon->opaque);
		Rld_Emit("char", "icon_opaque", text[2], (const char *)NULL);
		snprintf(text[0], sizeof(text[0]), "%u %06X", icon->cleared, icon->backgroundRgb);
		Rld_Emit("char", "icon_background", text[0], (const char *)NULL);
		snprintf(text[1], sizeof(text[1]), "%u %u %u %u", icon->place[0], icon->place[1], icon->place[2], icon->place[3]);
		Rld_Emit("char", "icon_place", text[1], (const char *)NULL);
	}

	// Enlarged so that it shows: the cut has less than 3/4 of the texels it
	// became across or down (the whole 43 x 25, or with --icon-fit the place
	// of the subject) - one stretched column does not count.
	if (((icon->crop[2] * 4u) < (icon->scaled[0] * 3u)) || ((icon->crop[3] * 4u) < (icon->scaled[1] * 3u)))
	{
		RldMk_Say("note", "icon-small", NULL, "%u x %u pixels of the picture were enlarged to %u x %u texels, so the icon looks blurry. Use a larger picture.",
		          icon->crop[2], icon->crop[3], icon->scaled[0], icon->scaled[1]);
	}

	if ((icon->opaque == 0u) && (cleared > 0u))
	{
		RldMk_Say("warning", "icon-empty", NULL,
		          "The icon is fully transparent at 43 x 25 after the background step made %u pixels transparent, so the menu would show an empty tile. Keep the PNG's background (--icon-background alpha; Reload Studio: untick Make background transparent).",
		          cleared);
	}
	else if (icon->opaque == 0u)
	{
		RldMk_Say("warning", "icon-empty", NULL,
		          "The icon is fully transparent at 43 x 25, so the menu would show an empty tile. Use a picture whose subject covers most of it.");
	}
	else if (icon->colorsIn > icon->colorCount)
	{
		RldMk_Say("info", "icon-colors", NULL, "The icon was reduced to %u colors (the portrait has 15 and transparent%s).", icon->colorCount,
		          (options->frame == RLDMK_ICON_FRAME_RETAIL) ? "; the retail frame takes 2 of them" : "");
	}

	if (previewPrefix != NULL)
	{
		u8 shown[RLDMK_ICON_SHOWN_TEXELS * 4u];
		char out[RLD_PATH_MAX + 32];
		u32 y;
		u32 x;

		for (y = 0; y < RLDMK_ICON_SHOWN_HEIGHT; y++)
		{
			for (x = 0; x < RLDMK_ICON_SHOWN_WIDTH; x++)
			{
				const u32 index = icon->index[y][x];
				const u32 c = icon->clut[index];
				u8 *pixel = &shown[(y * RLDMK_ICON_SHOWN_WIDTH + x) * 4u];

				pixel[0] = (u8)((index != 0u) ? RldMk_From5(c & 31u) : 0u);
				pixel[1] = (u8)((index != 0u) ? RldMk_From5((c >> 5) & 31u) : 0u);
				pixel[2] = (u8)((index != 0u) ? RldMk_From5((c >> 10) & 31u) : 0u);
				pixel[3] = (u8)((index == 0u) ? 0u : ((c & 0x8000u) ? 128u : 255u));
			}
		}

		snprintf(out, sizeof(out), "%s-icon.bmp", previewPrefix);
		RldMk_EmitWritten("icon-preview", out, RldMk_WriteBmp(out, RLDMK_ICON_SHOWN_WIDTH, RLDMK_ICON_SHOWN_HEIGHT, shown), 54u + sizeof(shown));

		// Without the game's data there is no retail portrait beside it - the
		// Studio then shows the head box and the frame as lines.
		if (RldMk_RetailPortrait(argv0, templateId, shown, RLDMK_ICON_SHOWN_WIDTH, RLDMK_ICON_SHOWN_HEIGHT))
		{
			snprintf(out, sizeof(out), "%s-retail.bmp", previewPrefix);
			RldMk_EmitWritten("icon-retail", out, RldMk_WriteBmp(out, RLDMK_ICON_SHOWN_WIDTH, RLDMK_ICON_SHOWN_HEIGHT, shown), 54u + sizeof(shown));
		}
	}

	RldPng_Free(&image);
	return 1;
}

//========================================================================================
// --voices: THE VOICES -> CVOI
//========================================================================================
//
// A folder of sound files becomes the chunk CVOI (layout and rules CVOI-1..4:
// include/rldchar.inc). The file name says the event (upper or lower case
// alike), the extension the format:
//
//   boost1..4 hit1..4 spin1..4 bigair1..4 drop1..4 shield1..4 passing1..4 fire1..4
//                    the 8 sets of voice lines (events 0..7), up to four each
//   yes, hit         the two short sounds (events 8 and 9)
//
// --voice <file>=<event|none> overrides the name for one file of the folder:
// any file goes to any event (s_rldCharVoiceEvents), or stays out. The clips
// of an event are ordered by file name (in lower case, then as written); an
// event takes at most RLDCHAR_VOICE_PER_EVENT (voice-too-many).
//
//   .wav  RIFF/WAVE, PCM 8/16/24 bit or float 32, 1 or 2 channels, 8000..48000 Hz
//         (also WAVE_FORMAT_EXTENSIBLE); LIST/INFO ISFT is read (voice-source)
//   .vag  the PS1 format: "VAGp", 0x30 bytes of header (include/psx/libspu.h:477),
//         big-endian data size and rate, 16-byte SPU-ADPCM blocks up to the
//         first one with the end flag - decoded, never encoded
//
// THE SOUND THE GAME PLAYS, made in this order: the samples as 16 bit, stereo
// mixed to mono (the mean, rounded down), the rate converted to 22050 Hz with
// a low-pass (RldMk_VoiceResample; a file at 22050 Hz keeps every sample), cut
// after 3.5 s (events 0..7) or 1.0 s (events 8 and 9), and with
// --voice-normalize the peak set to -1 dBFS (RldMk_VoiceNormalize). Integers
// throughout but the filter taps, which are rounded once: the same files give
// the same bytes. --voice-preview writes every readable file of the folder in
// exactly that form as a WAV, for Reload Studio's Play button; a file in no
// event is cut like a voice line there.
//
// Every file is a foreign one: lengths are checked against the bytes. A file
// rldpack cannot read stops the build when it is in an event (voice-wav,
// voice-vag); in no event it is only listed as bad.

#define RLDMK_VOICE_NAMES 34             // 8 events x 4 numbered names, then yes and hit
#define RLDMK_VOICE_LINE_MS 3500u        // RLDCHAR_VOICE_LINE_FRAMES
#define RLDMK_VOICE_SHORT_MS 1000u       // RLDCHAR_VOICE_SHORT_FRAMES
#define RLDMK_VOICE_FILES_MAX 64u
#define RLDMK_VOICE_ASSIGN_MAX 64u       // --voice switches
#define RLDMK_VOICE_BYTES_MAX (32u * 1024u * 1024u)
#define RLDMK_VOICE_PEAK 29204           // -1 dBFS: 32768 x 10^(-1/20), rounded
#define RLDMK_VOICE_PHASES 256u          // the positions between two samples the filter knows
#define RLDMK_VOICE_ZEROS 8              // zero crossings of the filter on each side
#define RLDMK_VOICE_PASS 0.92            // the cutoff, a part of the lower of the two Nyquist frequencies
#define RLDMK_VOICE_ONE 65536            // the taps of one phase add up to this
#define RLDMK_VOICE_TAPS_MAX 64u         // 2 x ceil(8 / (22050 / 48000 x 0.92)) = 38 at most
#define RLDMK_VOICE_CLIP_AUDIBLE 16u     // samples at full level from which voice-clip is a warning, below a note
#define RLDMK_VOICE_QUIET 328            // -40 dBFS: a clip with a lower peak is not normalized (voice-quiet)

struct RldMkVoiceFile
{
	char name[RLD_NAME_MAX];
	u8 *data;            // the bytes when given in memory (self-test); NULL: read from the folder when needed
	size_t size;
};

struct RldMkVoiceSound
{
	int vag;
	u32 rate;
	u32 channels;
	u32 frames;          // samples per channel
	u32 clipped;         // samples at full level
	u32 peak;            // the largest |sample| of any channel as 16 bit, 32768 = full scale
	int silent;
	int psound;          // LIST/INFO ISFT names PSound
	u8 hash[32];         // of the sample bytes, for voice-same
	s16 *mono;           // when asked for: `frames` samples, the channels mixed (malloc)
};

// What make-char passes on from its switches.
struct RldMkVoiceOptions
{
	const char **assign;         // the values of --voice: <file>=<event|none>
	u32 assignCount;
	int normalize;               // --voice-normalize
	const char *preview;         // --voice-preview <prefix>; NULL: no preview
};

// One file of the folder as make-char sees it.
struct RldMkVoiceItem
{
	int byName;          // RldMk_VoiceEvent: -2 not .wav/.vag, -1 no event's name, else the event
	int vag;
	int event;           // -1: in no event
	int chosen;          // the event (or none) comes from --voice
	int read;            // the samples were read
	int cut;             // longer than its event allows
	struct RldMkVoiceSound sound;
	s16 *clip;           // the clip as the game plays it - kept only for a file in an event
	u32 clipFrames;
	u32 clipFull;        // samples of the clip at full level before normalizing, the resampler's clamps included
	u32 clipPeak;        // the largest |sample| of the clip before normalizing
	int doubleOf;        // lost a voice-double to file doubleOf - 1; 0: no double
	int previewDone;
};

static s64 RldMk_VoiceFloorDiv(s64 value, s64 divisor)
{
	return (value >= 0) ? (value / divisor) : -((-value + divisor - 1) / divisor);
}

static char RldMk_VoiceLowerChar(char c)
{
	return ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
}

// Names compared in lower case, then as written - the order of the clips
// within an event.
static int RldMk_VoiceNameOrder(const char *a, const char *b)
{
	size_t i;

	for (i = 0; (a[i] != '\0') || (b[i] != '\0'); i++)
	{
		const char x = RldMk_VoiceLowerChar(a[i]);
		const char y = RldMk_VoiceLowerChar(b[i]);

		if (x != y)
		{
			return ((u8)x < (u8)y) ? -1 : 1;
		}
	}

	return strcmp(a, b);
}

static int RldMk_VoiceSameNoCase(const char *a, const char *b)
{
	size_t i;

	for (i = 0; (a[i] != '\0') || (b[i] != '\0'); i++)
	{
		if (RldMk_VoiceLowerChar(a[i]) != RldMk_VoiceLowerChar(b[i]))
		{
			return 0;
		}
	}

	return 1;
}

// The event a file name stands for (0..9), and the place of that name
// (0..RLDMK_VOICE_NAMES-1, for voice-double). -2: not a .wav or .vag file,
// -1: no event's name.
static int RldMk_VoiceEvent(const char *name, int *vag, int *place)
{
	char base[32];
	size_t length = strlen(name);
	size_t i;
	int e;

	*vag = 0;
	*place = -1;
	if (Rld_EndsWithNoCase(name, ".vag"))
	{
		*vag = 1;
	}
	else if (!Rld_EndsWithNoCase(name, ".wav"))
	{
		return -2;
	}

	length -= 4u;
	if ((length == 0u) || (length >= sizeof(base)))
	{
		return -1;
	}

	for (i = 0; i < length; i++)
	{
		base[i] = RldMk_VoiceLowerChar(name[i]);
	}
	base[length] = '\0';

	for (e = 0; e < RLDCHAR_VOICE_SHORT_FIRST; e++)
	{
		const size_t keyLength = strlen(s_rldCharVoiceEvents[e]);

		if ((length == (keyLength + 1u)) && (memcmp(base, s_rldCharVoiceEvents[e], keyLength) == 0) && (base[keyLength] >= '1') &&
		    (base[keyLength] <= (char)('0' + RLDCHAR_VOICE_PER_EVENT)))
		{
			*place = e * RLDCHAR_VOICE_PER_EVENT + (base[keyLength] - '1');
			return e;
		}
	}
	if (strcmp(base, "yes") == 0)
	{
		*place = RLDMK_VOICE_NAMES - 2;
		return RLDCHAR_VOICE_SHORT_FIRST;
	}
	if (strcmp(base, "hit") == 0)
	{
		*place = RLDMK_VOICE_NAMES - 1;
		return RLDCHAR_VOICE_SHORT_FIRST + 1;
	}

	return -1;
}

// The event of a --voice value: 0..9, -1 for none, -2 for no event.
static int RldMk_VoiceKey(const char *key)
{
	int e;

	if (RldMk_VoiceSameNoCase(key, "none"))
	{
		return -1;
	}
	for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
	{
		if (RldMk_VoiceSameNoCase(key, s_rldCharVoiceEvents[e]))
		{
			return e;
		}
	}

	return -2;
}

static int RldMk_ContainsNoCase(const u8 *text, size_t size, const char *word)
{
	const size_t length = strlen(word);
	size_t i;
	size_t k;

	for (i = 0; (i + length) <= size; i++)
	{
		for (k = 0; k < length; k++)
		{
			char c = (char)text[i + k];

			c = ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
			if (c != word[k])
			{
				break;
			}
		}
		if (k == length)
		{
			return 1;
		}
	}

	return 0;
}

// A WAV file. NULL = read, else the reason (fixed text). decode: also the
// samples mixed to mono in out->mono.
static const char *RldMk_ReadWav(const u8 *data, size_t size, struct RldMkVoiceSound *out, int decode)
{
	size_t at = 12;
	size_t dataAt = 0;
	u32 dataSize = 0;
	int haveFormat = 0;
	int haveData = 0;
	u32 format = 0;
	u32 bits = 0;
	u32 blockAlign = 0;
	u32 zero = 0;
	u32 i;
	u32 total;
	int left = 0;

	memset(out, 0, sizeof(*out));

	if ((size < 12u) || (memcmp(data, "RIFF", 4u) != 0) || (memcmp(&data[8], "WAVE", 4u) != 0))
	{
		return "no RIFF/WAVE header";
	}

	while ((size - at) >= 8u)
	{
		const u8 *id = &data[at];
		const u32 length = Rld_ReadLE32(&data[at + 4u]);

		at += 8u;
		if ((size_t)length > (size - at))
		{
			return "a chunk runs past the end of the file";
		}

		if (memcmp(id, "fmt ", 4u) == 0)
		{
			if (length < 16u)
			{
				return "the format chunk is too short";
			}
			format = Rld_ReadLE16(&data[at]);
			out->channels = Rld_ReadLE16(&data[at + 2u]);
			out->rate = Rld_ReadLE32(&data[at + 4u]);
			blockAlign = Rld_ReadLE16(&data[at + 12u]);
			bits = Rld_ReadLE16(&data[at + 14u]);
			if (format == 0xFFFEu)
			{
				// WAVE_FORMAT_EXTENSIBLE: the real format leads the sub-format GUID
				if (length < 40u)
				{
					return "the extensible format chunk is too short";
				}
				format = Rld_ReadLE16(&data[at + 24u]);
			}
			haveFormat = 1;
		}
		else if ((memcmp(id, "data", 4u) == 0) && !haveData)
		{
			dataAt = at;
			dataSize = length;
			haveData = 1;
		}
		else if ((memcmp(id, "LIST", 4u) == 0) && (length >= 4u) && (memcmp(&data[at], "INFO", 4u) == 0))
		{
			size_t sub = at + 4u;
			const size_t end = at + length;

			while ((end - sub) >= 8u)
			{
				const u32 subLength = Rld_ReadLE32(&data[sub + 4u]);

				if ((size_t)subLength > (end - sub - 8u))
				{
					break;
				}
				if ((memcmp(&data[sub], "ISFT", 4u) == 0) && RldMk_ContainsNoCase(&data[sub + 8u], subLength, "psound"))
				{
					out->psound = 1;
				}
				sub += 8u + subLength;
				if (((subLength & 1u) != 0u) && (sub < end))
				{
					sub++;
				}
			}
		}

		at += length;
		if (((length & 1u) != 0u) && (at < size))
		{
			at++;
		}
	}

	if (!haveFormat || !haveData)
	{
		return "the format or the data chunk is missing";
	}
	if (!(((format == 1u) && ((bits == 8u) || (bits == 16u) || (bits == 24u))) || ((format == 3u) && (bits == 32u))))
	{
		return "only PCM with 8, 16 or 24 bits and 32-bit float are read";
	}
	if ((out->channels < 1u) || (out->channels > 2u))
	{
		return "only mono and stereo are read";
	}
	if ((out->rate < 8000u) || (out->rate > 48000u))
	{
		return "the sample rate is outside 8000..48000 Hz";
	}
	if (blockAlign != (out->channels * bits / 8u))
	{
		return "the block size does not match channels and bits";
	}

	out->frames = dataSize / blockAlign;
	if (out->frames == 0u)
	{
		return "the file holds no samples";
	}
	total = out->frames * out->channels;

	if (decode)
	{
		out->mono = (s16 *)malloc((size_t)out->frames * sizeof(s16));
		if (out->mono == NULL)
		{
			return "the file is too large to decode";
		}
	}

	for (i = 0; i < total; i++)
	{
		const u8 *s = &data[dataAt + (size_t)i * (bits / 8u)];
		int full;
		int isZero;
		int value;

		if (bits == 8u)
		{
			full = (s[0] == 0u) || (s[0] == 255u);
			isZero = (s[0] == 128u);
			value = (s[0] == 255u) ? 32767 : (((int)s[0] - 128) * 256);   // 255 is full level, as 0 is
		}
		else if (bits == 16u)
		{
			const u32 v = Rld_ReadLE16(s);

			full = (v == 0x7FFFu) || (v == 0x8000u);
			isZero = (v == 0u);
			value = (int)v - (int)((v & 0x8000u) << 1);
		}
		else if (bits == 24u)
		{
			const u32 v = (u32)s[0] | ((u32)s[1] << 8) | ((u32)s[2] << 16);

			full = (v == 0x7FFFFFu) || (v == 0x800000u);
			isZero = (v == 0u);
			value = (int)RldMk_VoiceFloorDiv((s64)v - (s64)((v & 0x800000u) << 1), 256);
		}
		else
		{
			float f;
			u32 word = Rld_ReadLE32(s);

			memcpy(&f, &word, sizeof(f));
			full = (f >= 1.0f) || (f <= -1.0f);
			isZero = (f == 0.0f);
			if (!(f == f))
			{
				value = 0;
			}
			else if (f >= 1.0f)
			{
				value = 32767;
			}
			else if (f <= -1.0f)
			{
				value = -32768;
			}
			else
			{
				const s64 rounded = RldMk_Round((double)f * 32768.0);

				value = (rounded > 32767) ? 32767 : (int)rounded;
			}
		}

		out->clipped += full ? 1u : 0u;
		zero += isZero ? 1u : 0u;
		if ((u32)((value < 0) ? -value : value) > out->peak)
		{
			out->peak = (u32)((value < 0) ? -value : value);
		}

		if (out->mono != NULL)
		{
			if (out->channels == 1u)
			{
				out->mono[i] = (s16)value;
			}
			else if ((i & 1u) == 0u)
			{
				left = value;
			}
			else
			{
				out->mono[i / 2u] = (s16)RldMk_VoiceFloorDiv((s64)left + value, 2);
			}
		}
	}

	out->silent = (zero == total);
	Sha256(&data[dataAt], (size_t)out->frames * blockAlign, out->hash);
	return NULL;
}

// Rounded down, also for negative numbers.
static int RldMk_FloorDiv64(int value)
{
	return (value >= 0) ? (value / 64) : -((-value + 63) / 64);
}

// A VAG file: header, size, end flag, and the samples decoded (the SPU's
// ADPCM: 28 samples per 16-byte block, 5 prediction filters). decode: the
// samples also in out->mono.
static const char *RldMk_ReadVag(const u8 *data, size_t size, struct RldMkVoiceSound *out, int decode)
{
	static const int filter0[5] = {0, 60, 115, 98, 122};
	static const int filter1[5] = {0, 0, -52, -55, -60};
	u32 dataSize;
	size_t payload;
	size_t blocks;
	size_t end;
	size_t b;
	int s1 = 0;
	int s2 = 0;
	u32 zero = 0;

	memset(out, 0, sizeof(*out));
	out->vag = 1;
	out->channels = 1;

	if ((size < 0x30u) || (memcmp(data, "VAGp", 4u) != 0))
	{
		return "no VAGp header";
	}

	dataSize = RldPng_BE32(&data[0x0C]);
	out->rate = RldPng_BE32(&data[0x10]);
	payload = size - 0x30u;

	// The data size counts the blocks; writers differ by up to two blocks of
	// padding at the start and the end.
	if (((size_t)dataSize > payload) || ((payload - (size_t)dataSize) > 32u) || ((payload % 16u) != 0u))
	{
		return "the size in the header does not match the file";
	}
	if ((out->rate < 8000u) || (out->rate > 48000u))
	{
		return "the sample rate is outside 8000..48000 Hz";
	}

	blocks = payload / 16u;
	for (end = 0; end < blocks; end++)
	{
		if ((data[0x30u + 16u * end + 1u] & 1u) != 0u)
		{
			break;
		}
	}
	if (end == blocks)
	{
		return "no block carries the end flag";
	}

	if (decode)
	{
		out->mono = (s16 *)malloc((end + 1u) * 28u * sizeof(s16));
		if (out->mono == NULL)
		{
			return "the file is too large to decode";
		}
	}

	for (b = 0; b <= end; b++)
	{
		const u8 *block = &data[0x30u + 16u * b];
		u32 shift = block[0] & 0x0Fu;
		const u32 filter = (block[0] >> 4) & 0x0Fu;
		u32 k;

		if (filter > 4u)
		{
			free(out->mono);
			out->mono = NULL;
			return "a block uses an unknown filter";
		}
		shift = (shift > 12u) ? 9u : shift;

		for (k = 0; k < 28u; k++)
		{
			const u32 nibble = (block[2u + k / 2u] >> ((k & 1u) * 4u)) & 0x0Fu;
			const int signedNibble = (nibble >= 8u) ? ((int)nibble - 16) : (int)nibble;
			int value = signedNibble * (1 << (12u - shift)) + RldMk_FloorDiv64(s1 * filter0[filter] + s2 * filter1[filter] + 32);

			value = (value < -32768) ? -32768 : ((value > 32767) ? 32767 : value);
			out->clipped += ((value == 32767) || (value == -32768)) ? 1u : 0u;
			zero += (value == 0) ? 1u : 0u;
			if ((u32)((value < 0) ? -value : value) > out->peak)
			{
				out->peak = (u32)((value < 0) ? -value : value);
			}
			if (out->mono != NULL)
			{
				out->mono[b * 28u + k] = (s16)value;
			}
			s2 = s1;
			s1 = value;
		}
	}

	out->frames = (u32)(end + 1u) * 28u;
	out->silent = (zero == out->frames);
	Sha256(&data[0x30], (end + 1u) * 16u, out->hash);
	return NULL;
}

// The sine of any angle in radians: brought into -90..90 degrees for
// RldMk_SinCos, whose series is exact there.
static double RldMk_VoiceSin(double radians)
{
	const double pi = 3.14159265358979323846;
	double x = radians - 2.0 * pi * (double)RldMk_Round(radians / (2.0 * pi));
	double sine;
	double cosine;

	if (x > 0.5 * pi)
	{
		x = pi - x;
	}
	else if (x < -0.5 * pi)
	{
		x = -pi - x;
	}
	RldMk_SinCos(x * (180.0 / pi), &sine, &cosine);
	return sine;
}

// From `rate` to RLDCHAR_VOICE_RATE: out[n] lies at n * rate / 22050 input
// samples, the position rounded down to 1/RLDMK_VOICE_PHASES of a sample. The
// filter is a sinc with its cutoff at RLDMK_VOICE_PASS of the lower Nyquist
// frequency - against aliasing when the rate goes down, against images when it
// goes up - in a Hann window of RLDMK_VOICE_ZEROS zero crossings on each side.
// Its taps are computed in doubles once per call and rounded to integers that
// add up to RLDMK_VOICE_ONE in every phase; all else is integer. Samples
// before the start and after the end count as 0. At most maxFrames samples are
// made (malloc, *framesOut of them); *fullFrames: how many the whole sound
// has. At 22050 Hz the samples stay as they are. NULL: out of memory.
static s16 *RldMk_VoiceResample(const s16 *in, u32 frames, u32 rate, u32 maxFrames, u32 *framesOut, u32 *fullFrames)
{
	const u32 target = RLDCHAR_VOICE_RATE;
	const u32 full = (u32)(((u64)frames * target + rate - 1u) / rate);
	const u32 count = (full < maxFrames) ? full : maxFrames;
	s16 *out = (s16 *)malloc(((count != 0u) ? count : 1u) * sizeof(s16));
	const double pi = 3.14159265358979323846;
	double ratio;
	double cutoff;
	s32 *table;
	s32 half;
	u32 taps;
	u32 p;
	u32 n;

	*framesOut = 0;
	*fullFrames = full;
	if (out == NULL)
	{
		return NULL;
	}
	if (rate == target)
	{
		memcpy(out, in, (size_t)count * sizeof(s16));
		*framesOut = count;
		return out;
	}

	ratio = (rate > target) ? ((double)target / (double)rate) : 1.0;
	cutoff = 0.5 * ratio * RLDMK_VOICE_PASS;   // cycles per input sample
	half = (s32)RldMk_Ceil((double)RLDMK_VOICE_ZEROS / (2.0 * cutoff));
	taps = 2u * (u32)half;
	if (taps > RLDMK_VOICE_TAPS_MAX)
	{
		free(out);
		return NULL;
	}
	table = (s32 *)malloc((size_t)RLDMK_VOICE_PHASES * taps * sizeof(s32));
	if (table == NULL)
	{
		free(out);
		return NULL;
	}

	for (p = 0; p < RLDMK_VOICE_PHASES; p++)
	{
		const double fraction = (double)p / (double)RLDMK_VOICE_PHASES;
		double weight[RLDMK_VOICE_TAPS_MAX];
		double sum = 0.0;
		s32 total = 0;
		u32 largest = 0;
		u32 k;

		for (k = 0; k < taps; k++)
		{
			const double x = (double)((s32)k - half + 1) - fraction;   // from the output position, in input samples
			const double t = 2.0 * cutoff * x;
			const double sinc = (t == 0.0) ? 1.0 : (RldMk_VoiceSin(pi * t) / (pi * t));
			const double window = 0.5 + 0.5 * RldMk_VoiceSin(pi * x / (double)half + 0.5 * pi);

			weight[k] = sinc * window;
			sum += weight[k];
			largest = (weight[k] > weight[largest]) ? k : largest;
		}
		for (k = 0; k < taps; k++)
		{
			table[p * taps + k] = (s32)RldMk_Round(weight[k] / sum * (double)RLDMK_VOICE_ONE);
			total += table[p * taps + k];
		}
		table[p * taps + largest] += RLDMK_VOICE_ONE - total;
	}

	for (n = 0; n < count; n++)
	{
		const u64 position = (u64)n * rate;
		const s64 base = (s64)(position / target);
		const u32 phase = (u32)(((position % target) * RLDMK_VOICE_PHASES) / target);
		const s32 *tap = &table[phase * taps];
		s64 sum = 0;
		s64 value;
		u32 k;

		for (k = 0; k < taps; k++)
		{
			const s64 at = base + (s64)k - half + 1;

			if ((at >= 0) && (at < (s64)frames))
			{
				sum += (s64)tap[k] * in[(size_t)at];
			}
		}
		value = RldMk_VoiceFloorDiv(sum + RLDMK_VOICE_ONE / 2, RLDMK_VOICE_ONE);
		out[n] = (s16)((value > 32767) ? 32767 : ((value < -32768) ? -32768 : value));
	}

	free(table);
	*framesOut = count;
	return out;
}

// The largest |sample| of a clip and how many samples lie at full level
// (32767 or -32768).
static u32 RldMk_VoicePeak(const s16 *samples, u32 frames, u32 *full)
{
	u32 peak = 0;
	u32 i;

	*full = 0;
	for (i = 0; i < frames; i++)
	{
		const u32 v = (u32)((samples[i] < 0) ? -(s32)samples[i] : (s32)samples[i]);

		peak = (v > peak) ? v : peak;
		*full += ((samples[i] == 32767) || (samples[i] == -32768)) ? 1u : 0u;
	}

	return peak;
}

// --voice-normalize: the largest |sample| becomes RLDMK_VOICE_PEAK (-1 dBFS),
// every sample scaled alike, rounded half up. A clip with its peak below
// RLDMK_VOICE_QUIET (-40 dBFS) stays as it is: lifting noise or near silence
// to full level helps no one (voice-quiet says so).
static void RldMk_VoiceNormalize(s16 *samples, u32 frames)
{
	u32 full = 0;
	const s64 peak = (s64)RldMk_VoicePeak(samples, frames, &full);
	u32 i;

	if (peak < RLDMK_VOICE_QUIET)
	{
		return;
	}

	for (i = 0; i < frames; i++)
	{
		samples[i] = (s16)RldMk_VoiceFloorDiv((s64)samples[i] * RLDMK_VOICE_PEAK * 2 + peak, 2 * peak);
	}
}

// A 16-bit mono WAV at 22050 Hz: what the game plays (--voice-preview).
static int RldMk_WriteWav(const char *path, const s16 *samples, u32 frames)
{
	u8 head[44];
	u8 pair[2];
	FILE *file;
	u32 i;
	int ok;

	memcpy(head, "RIFF", 4u);
	Rld_WriteLE32(&head[4], 36u + 2u * frames);
	memcpy(&head[8], "WAVEfmt ", 8u);
	Rld_WriteLE32(&head[16], 16u);
	Rld_WriteLE16(&head[20], 1u);
	Rld_WriteLE16(&head[22], 1u);
	Rld_WriteLE32(&head[24], RLDCHAR_VOICE_RATE);
	Rld_WriteLE32(&head[28], 2u * RLDCHAR_VOICE_RATE);
	Rld_WriteLE16(&head[32], 2u);
	Rld_WriteLE16(&head[34], 16u);
	memcpy(&head[36], "data", 4u);
	Rld_WriteLE32(&head[40], 2u * frames);

	file = fopen(path, "wb");
	if (file == NULL)
	{
		return 0;
	}
	ok = (fwrite(head, 1, sizeof(head), file) == sizeof(head));
	for (i = 0; ok && (i < frames); i++)
	{
		Rld_WriteLE16(pair, (u32)(u16)samples[i]);
		ok = (fwrite(pair, 1, 2u, file) == 2u);
	}

	return (fclose(file) == 0) && ok;
}

// The name of a clip in CVOI: the file name without its extension, at most
// 15 characters, printable ASCII (anything else becomes '_') - for the log only.
static void RldMk_VoiceClipName(const char *fileName, u8 field[RLDCHAR_VOICE_NAME_BYTES])
{
	size_t length = strlen(fileName);
	size_t i;

	memset(field, 0, RLDCHAR_VOICE_NAME_BYTES);
	if (Rld_EndsWithNoCase(fileName, ".wav") || Rld_EndsWithNoCase(fileName, ".vag"))
	{
		length -= 4u;
	}
	if (length == 0u)
	{
		memcpy(field, "clip", 4u);
		return;
	}
	length = (length < (RLDCHAR_VOICE_NAME_BYTES - 1u)) ? length : (RLDCHAR_VOICE_NAME_BYTES - 1u);
	for (i = 0; i < length; i++)
	{
		const u8 c = (u8)fileName[i];

		field[i] = ((c >= 0x20u) && (c <= 0x7Eu)) ? c : (u8)'_';
	}
}

// One sound file of the folder: from files[i].data, or read from dir (at
// most RLDMK_VOICE_BYTES_MAX, the size asked first) and let go again. NULL =
// read (item->sound filled, the samples in item->sound.mono), else why not.
static const char *RldMk_VoiceRead(const struct RldMkVoiceFile *file, const char *dir, struct RldMkVoiceItem *item)
{
	const char *why = NULL;
	const u8 *data = file->data;
	u8 *read = NULL;
	size_t size = file->size;

	if ((data == NULL) && (dir != NULL))
	{
		char path[RLD_PATH_MAX + RLD_NAME_MAX];
		int tooLarge = 0;

		snprintf(path, sizeof(path), "%s" RLD_SEP "%s", dir, file->name);
		if (RldMk_ReadLimited(path, RLDMK_VOICE_BYTES_MAX, &read, &size, &tooLarge))
		{
			data = read;
		}
		why = tooLarge ? "the file is larger than 32 MB" : "the file cannot be read";
	}

	if (data != NULL)
	{
		why = item->vag ? RldMk_ReadVag(data, size, &item->sound, 1) : RldMk_ReadWav(data, size, &item->sound, 1);
	}
	else if (dir == NULL)
	{
		why = "the file cannot be read";
	}
	free(read);

	if (why != NULL)
	{
		free(item->sound.mono);
		item->sound.mono = NULL;
	}
	return why;
}

// --voice <file>=<event|none>, split at the last '=' (no event key holds
// one, a file name may); the last one for a file wins. 0 = said wrong.
static int RldMk_VoiceAssign(const char *value, const struct RldMkVoiceFile *files, u32 count, struct RldMkVoiceItem *items)
{
	const char *equals = strrchr(value, '=');
	char name[RLD_NAME_MAX];
	size_t length;
	int file = -1;
	int event;
	u32 i;

	if ((equals == NULL) || (equals == value) || (equals[1] == '\0') || ((size_t)(equals - value) >= sizeof(name)))
	{
		RldMk_Say("error", "voice-assign", value, "\"%s\" is not a file and its event, as in laugh.wav=shield.", value);
		return 0;
	}
	length = (size_t)(equals - value);
	memcpy(name, value, length);
	name[length] = '\0';

	for (i = 0; (i < count) && (file < 0); i++)
	{
		file = (strcmp(files[i].name, name) == 0) ? (int)i : -1;
	}
	for (i = 0; (i < count) && (file < 0); i++)
	{
		file = RldMk_VoiceSameNoCase(files[i].name, name) ? (int)i : -1;
	}
	if (file < 0)
	{
		RldMk_Say("error", "voice-assign", value, "%s is not in the voice folder.", name);
		return 0;
	}

	event = RldMk_VoiceKey(&equals[1]);
	if (event == -2)
	{
		RldMk_Say("error", "voice-assign", value,
		          "\"%s\" is not an event. The events: boost, hit, spin, bigair, drop, shield, passing, fire, short-yes, short-hit, or none.", &equals[1]);
		return 0;
	}
	if ((items[file].byName == -2) && (event >= 0))
	{
		RldMk_Say("error", "voice-assign", value, "%s is not a .wav or .vag file.", files[file].name);
		return 0;
	}

	items[file].event = event;
	items[file].chosen = 1;
	return 1;
}

static const char *RldMk_VoiceState(const struct RldMkVoiceItem *item)
{
	if (item->byName == -2)
	{
		return "ignored";
	}
	if (!item->read)
	{
		return "bad";
	}
	if (item->event >= 0)
	{
		return item->cut ? "cut" : "ok";
	}
	return ((item->byName == -1) && !item->chosen) ? "unknown" : "unused";
}

// THE VOICES of a folder, from its files (sorted by name), into CVOI. dir:
// the folder the files are read from when needed; NULL: the bytes are in
// files[].data (self-test). Every finding is said, every file gets its @file
// and @voice line, and the events their @voiceevent. The chunk (malloc, size
// in *sizeOut), already checked by the game's own RldChar_CheckVoices; NULL
// when no file is in an event or a voice error was said.
static u8 *RldMk_PackVoices(const struct RldMkVoiceFile *files, u32 count, const struct RldMkVoiceOptions *options, const char *dir, size_t *sizeOut)
{
	struct RldMkVoiceItem *items = (struct RldMkVoiceItem *)calloc((count != 0u) ? count : 1u, sizeof(struct RldMkVoiceItem));
	int placeFile[RLDMK_VOICE_NAMES];
	u32 order[RLDCHAR_VOICE_EVENTS][RLDMK_VOICE_FILES_MAX];
	u32 inEvent[RLDCHAR_VOICE_EVENTS];
	u8 first[RLDCHAR_VOICE_EVENTS];
	u8 *cvoi = NULL;
	const int errorsBefore = s_rldMkErrors;
	u32 clipCount = 0;
	u32 eventsUsed = 0;
	u32 bytes;
	u32 i;
	int e;

	*sizeOut = 0;
	if (items == NULL)
	{
		RldMk_OutOfMemory();
		return NULL;
	}
	for (i = 0; i < RLDMK_VOICE_NAMES; i++)
	{
		placeFile[i] = -1;
	}

	// V1: the names. WAV before VAG when both fill a place.
	for (i = 0; i < count; i++)
	{
		int place = -1;

		items[i].byName = RldMk_VoiceEvent(files[i].name, &items[i].vag, &place);
		items[i].event = -1;
		if (items[i].byName == -2)
		{
			RldMk_Say("note", "voice-other", NULL, "%s is not a .wav or .vag file and is left out.", files[i].name);
			continue;
		}
		if (place < 0)
		{
			continue;
		}

		if (placeFile[place] >= 0)
		{
			const int kept = placeFile[place];
			const int use = (items[kept].vag && !items[i].vag) ? (int)i : kept;
			const int other = (use == kept) ? (int)i : kept;

			items[use].event = items[i].byName;
			items[use].doubleOf = 0;
			items[other].event = -1;
			items[other].doubleOf = use + 1;
			placeFile[place] = use;
			continue;
		}

		placeFile[place] = (int)i;
		items[i].event = items[i].byName;
	}

	// V1: what --voice says wins over the name.
	for (i = 0; i < options->assignCount; i++)
	{
		(void)RldMk_VoiceAssign(options->assign[i], files, count, items);
	}
	for (i = 0; i < count; i++)
	{
		// A double only while --voice has not settled it: neither file got an
		// event of its own.
		if ((items[i].doubleOf != 0) && !items[i].chosen && !items[items[i].doubleOf - 1].chosen)
		{
			const char *used = files[items[i].doubleOf - 1].name;

			RldMk_Say("warning", "voice-double", NULL, "%s and %s have the name of the same voice line; %s is used. Remove the other one, or choose an event for it.",
			          used, files[i].name, used);
		}
		if ((items[i].byName == -1) && !items[i].chosen)
		{
			RldMk_Say("warning", "voice-unknown", NULL, "%s does not have the name of a voice line and is left out. Name it like boost1.wav or yes.wav, or choose its event.",
			          files[i].name);
		}
	}

	// V2-V4: every .wav and .vag is read - for the list and the preview -, and
	// made into the clip the game plays.
	for (i = 0; i < count; i++)
	{
		struct RldMkVoiceItem *item = &items[i];
		const int isShort = (item->event >= RLDCHAR_VOICE_SHORT_FIRST);
		const u32 limit = isShort ? RLDCHAR_VOICE_SHORT_FRAMES : RLDCHAR_VOICE_LINE_FRAMES;
		const char *why;
		u32 full = 0;

		if (item->byName == -2)
		{
			continue;
		}

		why = RldMk_VoiceRead(&files[i], dir, item);
		if (why != NULL)
		{
			if (item->event < 0)
			{
				continue;
			}
			if (item->vag)
			{
				RldMk_Say("error", "voice-vag", why, "%s is not a VAG file rldpack can read (%s). Export it again as a PS1 VAG, or use a 16-bit PCM WAV.",
				          files[i].name, why);
			}
			else
			{
				RldMk_Say("error", "voice-wav", why, "%s is not a WAV file rldpack can read (%s). Export it as 16-bit PCM WAV.", files[i].name, why);
			}
			continue;
		}

		item->clip = RldMk_VoiceResample(item->sound.mono, item->sound.frames, item->sound.rate, limit, &item->clipFrames, &full);
		free(item->sound.mono);
		item->sound.mono = NULL;
		if (item->clip == NULL)
		{
			RldMk_OutOfMemory();
			continue;
		}
		item->read = 1;
		item->cut = (full > limit);
		item->clipPeak = RldMk_VoicePeak(item->clip, item->clipFrames, &item->clipFull);
		if (options->normalize)
		{
			RldMk_VoiceNormalize(item->clip, item->clipFrames);
		}

		if (item->event >= 0)
		{
			// Rounded up, so that a sound just over the limit never reads as the limit.
			const u32 ms = (u32)(((u64)item->sound.frames * 1000u + item->sound.rate - 1u) / item->sound.rate);
			const u32 hundredths = (ms + 9u) / 10u;

			if (item->sound.channels == 2u)
			{
				RldMk_Say("note", "voice-stereo", NULL, "%s is stereo; it is mixed to mono.", files[i].name);
			}
			if (item->sound.silent)
			{
				RldMk_Say("warning", "voice-silent", NULL, "%s is silent - every sample is zero. Export the sound again.", files[i].name);
			}
			else if (item->clipPeak < (u32)RLDMK_VOICE_QUIET)
			{
				RldMk_Say("warning", "voice-quiet", NULL, "%s is nearly silent - its peak is below -40 dBFS%s. Export it louder.", files[i].name,
				          options->normalize ? ", so it is not normalized" : "");
			}
			else if (item->clipFull != 0u)
			{
				// Counted on the clip as the game plays it (before normalizing,
				// which keeps the distortion): the samples at full level, the ones
				// the resampler had to clamp included. A warning only where it can
				// be heard: a few lone samples at full level are a note.
				RldMk_Say((item->clipFull >= RLDMK_VOICE_CLIP_AUDIBLE) ? "warning" : "note", "voice-clip", NULL,
				          "%s is clipped (%u sample%s at full level). Lower its volume a little.", files[i].name, item->clipFull,
				          (item->clipFull == 1u) ? "" : "s");
			}
			if (item->cut && isShort)
			{
				RldMk_Say("warning", "voice-length-short", NULL, "%s is %u.%02u s long; short sounds are cut after %u.%02u s. Shorten it.", files[i].name,
				          hundredths / 100u, hundredths % 100u, RLDMK_VOICE_SHORT_MS / 1000u, (RLDMK_VOICE_SHORT_MS % 1000u) / 10u);
			}
			else if (item->cut)
			{
				RldMk_Say("warning", "voice-length-line", NULL, "%s is %u.%02u s long; voice lines are cut after 3.50 s. Shorten it.", files[i].name,
				          hundredths / 100u, hundredths % 100u);
			}
			if (item->sound.psound)
			{
				RldMk_Say("note", "voice-source", NULL,
				          "%s was made with PSound, a tool that copies sounds out of PlayStation games. Keep this character on your own computer.",
				          files[i].name);
			}
		}

		if (options->preview != NULL)
		{
			char path[RLD_PATH_MAX + 32];

			snprintf(path, sizeof(path), "%s-voice-%u.wav", options->preview, i + 1u);
			item->previewDone = RldMk_WriteWav(path, item->clip, item->clipFrames);
			if (!item->previewDone)
			{
				RldMk_Say("warning", "voice-preview", path, "The sound %s cannot be written. Is the folder write-protected?", path);
			}
		}

		if (item->event < 0)
		{
			free(item->clip);
			item->clip = NULL;
		}
	}

	// The events: their files in the order of the names, at most 4 each.
	for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
	{
		inEvent[e] = 0;
		for (i = 0; i < count; i++)
		{
			u32 at;

			if (items[i].event != e)
			{
				continue;
			}
			at = inEvent[e];
			while ((at > 0u) && (RldMk_VoiceNameOrder(files[i].name, files[order[e][at - 1u]].name) < 0))
			{
				order[e][at] = order[e][at - 1u];
				at--;
			}
			order[e][at] = i;
			inEvent[e]++;
		}
		if (inEvent[e] > (u32)RLDCHAR_VOICE_PER_EVENT)
		{
			char detail[64];

			snprintf(detail, sizeof(detail), "%u sounds in the event %s, at most %d", inEvent[e], s_rldCharVoiceEvents[e], RLDCHAR_VOICE_PER_EVENT);
			RldMk_Say("error", "voice-too-many", detail, "The event %s has %u sounds; it takes at most %d. Move %s to another event.",
			          s_rldCharVoiceEvents[e], inEvent[e], RLDCHAR_VOICE_PER_EVENT, files[order[e][inEvent[e] - 1u]].name);
		}
		first[e] = (u8)((clipCount < 255u) ? clipCount : 255u);
		clipCount += inEvent[e];
		eventsUsed += (inEvent[e] != 0u) ? 1u : 0u;
	}

	// V4: the same sound twice.
	for (i = 0; i < count; i++)
	{
		u32 k;

		for (k = i + 1u; items[i].read && (items[i].event >= 0) && (k < count); k++)
		{
			const struct RldMkVoiceSound *a = &items[i].sound;
			const struct RldMkVoiceSound *b = &items[k].sound;

			if (items[k].read && (items[k].event >= 0) && (a->vag == b->vag) && (a->rate == b->rate) && (a->channels == b->channels) &&
			    (a->frames == b->frames) && (memcmp(a->hash, b->hash, 32u) == 0))
			{
				RldMk_Say("note", "voice-same", NULL, "%s and %s are the same sound.", files[i].name, files[k].name);
			}
		}
	}

	// V9: the silent events.
	{
		char missing[256];

		missing[0] = '\0';
		for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
		{
			const size_t used = strlen(missing);

			if ((inEvent[e] == 0u) && ((used + strlen(s_rldCharVoiceEvents[e]) + 3u) < sizeof(missing)))
			{
				snprintf(&missing[used], sizeof(missing) - used, "%s%s", (used != 0u) ? ", " : "", s_rldCharVoiceEvents[e]);
			}
		}
		if (clipCount == 0u)
		{
			RldMk_Say("note", "voice-missing", NULL, "No sound is in an event - the driver stays silent in the game.");
		}
		else if (missing[0] != '\0')
		{
			RldMk_Say("note", "voice-missing", NULL, "No sound for: %s. The driver is silent there.", missing);
		}
	}

	// The lines for Reload Studio, file by file in the folder's order.
	if (s_machine && !s_rldMkQuiet)
	{
		for (i = 0; i < count; i++)
		{
			const struct RldMkVoiceItem *item = &items[i];
			const char *state = RldMk_VoiceState(item);
			char number[6][24];
			char path[RLD_PATH_MAX + 32];
			snprintf(number[0], sizeof(number[0]), "%u",
			         item->read ? (u32)(((u64)item->sound.frames * 1000u + item->sound.rate - 1u) / item->sound.rate) : 0u);
			snprintf(number[1], sizeof(number[1]), "%llu", (unsigned long long)files[i].size);
			snprintf(number[2], sizeof(number[2]), "%u", item->read ? item->sound.rate : 0u);
			snprintf(number[3], sizeof(number[3]), "%u", item->read ? item->sound.channels : 0u);
			snprintf(number[4], sizeof(number[4]), "%u", item->read ? (u32)(((u64)item->sound.peak * 1000u + 16384u) / 32768u) : 0u);
			if (item->previewDone)
			{
				snprintf(path, sizeof(path), "%s-voice-%u.wav", options->preview, i + 1u);
			}
			else
			{
				snprintf(path, sizeof(path), "-");
			}

			RldMk_EmitFile("voice", state, files[i].name, files[i].size);
			Rld_Emit("voice", files[i].name, state, (item->event >= 0) ? s_rldCharVoiceEvents[item->event] : "-", number[0], number[1], number[2], number[3],
			         number[4], path, (const char *)NULL);
		}
		for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
		{
			char number[16];

			snprintf(number, sizeof(number), "%u", inEvent[e]);
			Rld_Emit("voiceevent", s_rldCharVoiceEvents[e], number, (const char *)NULL);
		}
		{
			char clips[16];
			char events[16];

			snprintf(clips, sizeof(clips), "%u", clipCount);
			snprintf(events, sizeof(events), "%u", eventsUsed);
			Rld_Emit("char", "voices", clips, events, (const char *)NULL);
		}
	}

	// CVOI: head, events, clips, samples (include/rldchar.inc).
	bytes = RLDCHAR_VOICE_CLIPS_AT + clipCount * RLDCHAR_VOICE_CLIP_BYTES;
	for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
	{
		u32 k;

		for (k = 0; k < inEvent[e]; k++)
		{
			bytes += 2u * items[order[e][k]].clipFrames;
		}
	}
	if ((s_rldMkErrors == errorsBefore) && (clipCount != 0u) && (clipCount <= (u32)RLDCHAR_VOICE_CLIPS_MAX))
	{
		cvoi = (u8 *)calloc(1, bytes);
		if (cvoi == NULL)
		{
			RldMk_OutOfMemory();
		}
	}
	if (cvoi != NULL)
	{
		u32 clip = 0;
		u32 at = RLDCHAR_VOICE_CLIPS_AT + clipCount * RLDCHAR_VOICE_CLIP_BYTES;
		const char *why;

		Rld_WriteLE32(&cvoi[0x00], RLDCHAR_VOICE_VERSION);
		Rld_WriteLE32(&cvoi[0x04], RLDCHAR_VOICE_RATE);
		Rld_WriteLE32(&cvoi[0x08], clipCount);
		for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
		{
			u8 *event = &cvoi[RLDCHAR_VOICE_HEAD_BYTES + (u32)e * RLDCHAR_VOICE_EVENT_BYTES];
			u32 k;

			event[0] = first[e];
			event[1] = (u8)inEvent[e];
			for (k = 0; k < inEvent[e]; k++, clip++)
			{
				const struct RldMkVoiceItem *item = &items[order[e][k]];
				u8 *entry = &cvoi[RLDCHAR_VOICE_CLIPS_AT + clip * RLDCHAR_VOICE_CLIP_BYTES];
				u32 s;

				RldMk_VoiceClipName(files[order[e][k]].name, entry);
				Rld_WriteLE32(&entry[0x10], at);
				Rld_WriteLE32(&entry[0x14], item->clipFrames);
				Rld_WriteLE16(&entry[0x18], RLDCHAR_VOICE_VOLUME_FULL);
				for (s = 0; s < item->clipFrames; s++)
				{
					Rld_WriteLE16(&cvoi[at + 2u * s], (u32)(u16)item->clip[s]);
				}
				at += 2u * item->clipFrames;
			}
		}

		// The game's own check: rldpack never writes voices the game drops.
		why = RldChar_CheckVoices(cvoi, bytes, NULL);
		if (why != NULL)
		{
			RldMk_Say("error", "voice-pack", why, "Internal error: the packed voices do not pass the game's check (%s). Please report it.", why);
			free(cvoi);
			cvoi = NULL;
		}
		else
		{
			*sizeOut = bytes;
		}
	}

	for (i = 0; i < count; i++)
	{
		free(items[i].clip);
		free(items[i].sound.mono);
	}
	free(items);
	return cvoi;
}

struct RldMkVoiceFolder
{
	struct RldMkVoiceFile *files;   // the first RLDMK_VOICE_FILES_MAX names, sorted
	u32 count;
	u32 seen;
};

// Keeps the RLDMK_VOICE_FILES_MAX first names by strcmp, whatever order the
// system lists them in.
static void RldMk_VoiceVisit(void *context, const char *name)
{
	struct RldMkVoiceFolder *folder = (struct RldMkVoiceFolder *)context;
	char kept[RLD_NAME_MAX];
	u32 at;

	folder->seen++;
	snprintf(kept, sizeof(kept), "%s", name);

	at = folder->count;
	while ((at > 0u) && (strcmp(kept, folder->files[at - 1u].name) < 0))
	{
		at--;
	}
	if (at >= RLDMK_VOICE_FILES_MAX)
	{
		return;
	}

	if (folder->count < RLDMK_VOICE_FILES_MAX)
	{
		folder->count++;
	}
	memmove(&folder->files[at + 1u], &folder->files[at], (size_t)(folder->count - 1u - at) * sizeof(folder->files[0]));
	memcpy(folder->files[at].name, kept, sizeof(kept));
}

// --voices <dir>: list the folder, read its sound files one at a time, pack
// CVOI (RldMk_PackVoices). NULL: no chunk - see the messages.
static u8 *RldMk_VoiceStep(const char *dir, const struct RldMkVoiceOptions *options, size_t *sizeOut)
{
	static struct RldMkVoiceFile files[RLDMK_VOICE_FILES_MAX];
	struct RldMkVoiceFolder folder;
	u32 i;

	*sizeOut = 0;
	memset(files, 0, sizeof(files));
	folder.files = files;
	folder.count = 0;
	folder.seen = 0;

	if (!Rld_EachFile(dir, RldMk_VoiceVisit, &folder))
	{
		RldMk_Say("error", "voice-folder", dir, "The voice folder %s cannot be opened. Check the path, or leave the voices out.", dir);
		return NULL;
	}

	if (folder.seen > folder.count)
	{
		RldMk_Say("warning", "voice-folder", NULL, "The voice folder holds %u files; only the first %u by name are looked at. Keep only the voices in it.",
		          folder.seen, folder.count);
	}

	// Only the sizes, for the @file lines; the files are read one at a time.
	for (i = 0; i < folder.count; i++)
	{
		char path[RLD_PATH_MAX + RLD_NAME_MAX];
		int ok = 0;

		snprintf(path, sizeof(path), "%s" RLD_SEP "%s", dir, files[i].name);
		files[i].data = NULL;
		files[i].size = RldMk_FileSize(path, &ok);
	}

	return RldMk_PackVoices(files, folder.count, options, dir, sizeOut);
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

// `text` in lower case into `word`, as --up takes its value in any case.
// 0 = longer than `size` - 1: no word of a switch.
static int RldMk_Lower(const char *text, char *word, size_t size)
{
	size_t k;

	for (k = 0; ((k + 1u) < size) && (text[k] != '\0'); k++)
	{
		word[k] = (char)(((text[k] >= 'A') && (text[k] <= 'Z')) ? (text[k] - 'A' + 'a') : text[k]);
	}
	word[k] = '\0';
	return (text[k] == '\0');
}

// --fit (default crash) and whether --scale may go with it: *fit is 1 for
// crash, 0 for none. 0 = a wrong value or --scale with crash, said (fit,
// fit-scale); *fit stays crash after a wrong value.
static int RldMk_FitSwitch(const char *fitArg, const char *scaleArg, int *fit)
{
	char word[8];

	*fit = 1;
	if ((fitArg != NULL) && (!RldMk_Lower(fitArg, word, sizeof(word)) || (strcmp(word, "crash") != 0)))
	{
		if (!RldMk_Lower(fitArg, word, sizeof(word)) || (strcmp(word, "none") != 0))
		{
			RldMk_Say("error", "fit", fitArg, "Fit must be crash or none.");
			return 0;
		}
		*fit = 0;
	}

	if (*fit && (scaleArg != NULL))
	{
		RldMk_Say("error", "fit-scale", scaleArg, "--scale needs --fit none.");
		return 0;
	}

	return 1;
}

// --mask (default like) in any case, as --wheels: *mask is RLDCHAR_MASK_LIKE,
// _AKU or _UKA. 0 = a wrong value, said (mask); *mask then stays like.
static int RldMk_MaskSwitch(const char *maskArg, u32 *mask)
{
	char word[8];
	const int known = (maskArg != NULL) ? RldMk_Lower(maskArg, word, sizeof(word)) : 0;

	*mask = RLDCHAR_MASK_LIKE;
	if (maskArg == NULL)
	{
		return 1;
	}
	if (known && (strcmp(word, "aku") == 0))
	{
		*mask = RLDCHAR_MASK_AKU;
	}
	else if (known && (strcmp(word, "uka") == 0))
	{
		*mask = RLDCHAR_MASK_UKA;
	}
	else if (!known || (strcmp(word, "like") != 0))
	{
		RldMk_Say("error", "mask", maskArg, "The mask must be aku, uka or like.");
		return 0;
	}

	return 1;
}

// --map-color RRGGBB (hex, any case, a leading # allowed): *color is
// 0x00BBGGRR, the order of CHRI and of the game's color words. 0 = not six hex
// digits, said (map-color).
static int RldMk_MapColorSwitch(const char *arg, u32 *color)
{
	const char *digits = (arg[0] == '#') ? &arg[1] : arg;
	u32 rgb = 0;
	int k;

	for (k = 0; k < 6; k++)
	{
		const char c = digits[k];
		u32 v = 16u;  // not a hex digit

		if ((c >= '0') && (c <= '9'))
		{
			v = (u32)(c - '0');
		}
		else if ((c >= 'a') && (c <= 'f'))
		{
			v = (u32)(c - 'a' + 10);
		}
		else if ((c >= 'A') && (c <= 'F'))
		{
			v = (u32)(c - 'A' + 10);
		}

		if (v > 15u)
		{
			break;
		}
		rgb = (rgb << 4) | v;
	}
	if ((k != 6) || (digits[6] != '\0'))
	{
		RldMk_Say("error", "map-color", arg, "The map color \"%s\" must be six hex digits RRGGBB, as FF8000 for orange.", arg);
		return 0;
	}

	*color = ((rgb >> 16) & 0xFFu) | (rgb & 0xFF00u) | ((rgb & 0xFFu) << 16);
	return 1;
}

// 0x00BBGGRR -> "RRGGBB" (7 bytes with the end).
static void RldMk_MapColorText(u32 color, char text[8])
{
	snprintf(text, 8, "%02X%02X%02X", color & 0xFFu, (color >> 8) & 0xFFu, (color >> 16) & 0xFFu);
}

// The CHRI flags of a build: no wheels (bit 0), the mask (bits 1-2) and an
// own map color (bit 3). All unset = 0, and CHRI keeps fixedSize 0x1C as
// before the flags.
static u32 RldMk_ChriFlags(const struct RldMkOptions *opt)
{
	return (opt->noWheels ? RLDCHAR_FLAG_NO_WHEELS : 0u) | ((opt->mask << RLDCHAR_FLAG_MASK_SHIFT) & RLDCHAR_FLAG_MASK_BITS) |
	       (opt->hasMapColor ? RLDCHAR_FLAG_MAP_COLOR : 0u);
}

// The mask a driver wears, as a word: "aku" or "uka"; with RLDCHAR_MASK_LIKE
// the template's (RLDCHAR_TEMPLATE_WEARS_AKU); "none" without a template.
static const char *RldMk_MaskWord(u32 mask, u32 templateId)
{
	if (mask == RLDCHAR_MASK_AKU)
	{
		return "aku";
	}
	if (mask == RLDCHAR_MASK_UKA)
	{
		return "uka";
	}
	if (templateId > (u32)RLDCHAR_TEMPLATE_MAX)
	{
		return "none";
	}
	return RLDCHAR_TEMPLATE_WEARS_AKU(templateId) ? "aku" : "uka";
}

// The same for people: "Aku Aku", "Uka Uka" or "not determined".
static const char *RldMk_MaskName(u32 mask, u32 templateId)
{
	const char *word = RldMk_MaskWord(mask, templateId);

	return (strcmp(word, "aku") == 0) ? "Aku Aku" : ((strcmp(word, "uka") == 0) ? "Uka Uka" : "not determined");
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
		// What the game draws; with @char collapsed the step back to the
		// count of @char budget (and @char reduced).
		RldMk_Fact("triangles", "%u", b->triangleCount);
		RldMk_Fact("collapsed", "%u", b->collapsed);
	}

	if (b->dummy)
	{
		char dummy[7][24];
		int k;

		for (k = 0; k < 3; k++)
		{
			snprintf(dummy[k], sizeof(dummy[k]), "%.2f", b->dumFit.shift[k]);
			snprintf(dummy[3 + k], sizeof(dummy[3 + k]), "%.2f", b->dumFit.seat[k]);
		}
		snprintf(dummy[6], sizeof(dummy[6]), "%d", b->dumFit.hasWheel);
		Rld_Emit("char", "dummy", dummy[0], dummy[1], dummy[2], dummy[3], dummy[4], dummy[5], dummy[6], (const char *)NULL);
	}

	if (b->repaired)
	{
		char repaired[10][16];
		int k;

		for (k = 0; k < 10; k++)
		{
			snprintf(repaired[k], sizeof(repaired[k]), "%u", b->repair[k]);
		}
		Rld_Emit("char", "repaired", repaired[0], repaired[1], repaired[2], repaired[3], repaired[4], repaired[5], repaired[6], repaired[7], repaired[8],
		         repaired[9], (const char *)NULL);
	}

	if (b->bothSides != NULL)
	{
		RldMk_Fact("two-sided", "%u", b->openTwoSided);
		RldMk_Fact("two-sided-packing", "%u", b->quantFlips);
	}

	if (b->remeshed)
	{
		char remeshed[5][16];

		snprintf(remeshed[0], sizeof(remeshed[0]), "%u", b->remeshFrom);
		snprintf(remeshed[1], sizeof(remeshed[1]), "%u", b->remeshHull);
		snprintf(remeshed[2], sizeof(remeshed[2]), "%u", b->reduceTo);
		snprintf(remeshed[3], sizeof(remeshed[3]), "%u", b->remeshOpenBefore);
		snprintf(remeshed[4], sizeof(remeshed[4]), "%u", b->openEdges);
		Rld_Emit("char", "remeshed", remeshed[0], remeshed[1], remeshed[2], remeshed[3], remeshed[4], (const char *)NULL);
	}

	if (b->reduced)
	{
		char reduced[4][16];

		snprintf(reduced[0], sizeof(reduced[0]), "%u", b->reduceFrom);
		snprintf(reduced[1], sizeof(reduced[1]), "%u", b->reduceTo);
		snprintf(reduced[2], sizeof(reduced[2]), "%u", b->reduceFrom * RLDCHAR_DRAW_BYTES_G3);
		snprintf(reduced[3], sizeof(reduced[3]), "%u", b->reduceTo * RLDCHAR_DRAW_BYTES_G3);
		Rld_Emit("char", "reduced", reduced[0], reduced[1], reduced[2], reduced[3], (const char *)NULL);
	}

	if (b->fitted)
	{
		char fit[5][24];

		snprintf(fit[0], sizeof(fit[0]), "%.4g", b->fitFactor);
		RldMk_FitNumber(fit[1], sizeof(fit[1]), b->fitBefore[0]);
		RldMk_FitNumber(fit[2], sizeof(fit[2]), b->fitAfter[0]);
		RldMk_FitNumber(fit[3], sizeof(fit[3]), b->fitBefore[1]);
		RldMk_FitNumber(fit[4], sizeof(fit[4]), b->fitAfter[1]);
		Rld_Emit("char", "fit", fit[0], fit[1], fit[2], fit[3], fit[4], (b->fitBasis == RLDMK_FIT_KART) ? "kart" : "model", (const char *)NULL);
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

	if (b->fitted)
	{
		char fit[6][24];
		char basis[80];

		RldMk_FitNumber(fit[0], sizeof(fit[0]), b->fitBefore[0]);
		RldMk_FitNumber(fit[1], sizeof(fit[1]), b->fitAfter[0]);
		RldMk_FitNumber(fit[2], sizeof(fit[2]), b->fitBefore[1]);
		RldMk_FitNumber(fit[3], sizeof(fit[3]), b->fitAfter[1]);
		RldMk_FitNumber(fit[4], sizeof(fit[4]), b->fitKart[0]);
		RldMk_FitNumber(fit[5], sizeof(fit[5]), b->fitKart[1]);
		if (b->fitBasis == RLDMK_FIT_KART)
		{
			snprintf(basis, sizeof(basis), "kart length %s -> %s%s", fit[4], fit[5], b->fitCapped ? ", capped at the tallest driver" : "");
		}
		else
		{
			snprintf(basis, sizeof(basis), "%s %s", b->fitCapped ? "model height," : "model length,", opt->noWheels ? "wheels hidden" : "no kart found");
		}
		printf("  fit           x%.4g to Crash size (%s): %s -> %s long, %s -> %s tall (game units)\n", b->fitFactor, basis, fit[0], fit[1], fit[2],
		       fit[3]);
	}
	else if (!opt->fit)
	{
		printf("  fit           none - the model as exported, x%g\n", opt->scale);
	}

	if (b->dummy)
	{
		printf("  dummy         placed on the reference kart: shifted %.2f %.2f %.2f, seat %.2f %.2f %.2f, steering wheel %s\n", b->dumFit.shift[0],
		       b->dumFit.shift[1], b->dumFit.shift[2], b->dumFit.seat[0], b->dumFit.seat[1], b->dumFit.seat[2],
		       b->dumFit.hasWheel ? "turns about its column" : (b->dumFit.wheelAsDriver ? "leans with the driver" : "none"));
	}

	if (b->repaired)
	{
		printf("  repaired      %u corners welded, %u triangles without area and %u doubled dropped, %u turned, %u cracks split,\n", b->repair[0],
		       b->repair[1], b->repair[2], b->repair[3], b->repair[9]);
		printf("                %u holes closed (%u triangles), %u open pieces left; open edges %u -> %u\n", b->repair[4], b->repair[5], b->repair[6],
		       b->repair[7], b->repair[8]);
	}

	if (b->bothSides != NULL)
	{
		printf("  two-sided     %u triangles of open parts%s, %u turned over by the packing (+0 triangles)\n", b->openTwoSided,
		       opt->openOneSided ? " (--open-parts one-sided: none)" : "", b->quantFlips);
	}

	if (b->remeshed)
	{
		printf("  remeshed      %u -> %u -> %u triangles (closed hulls, %u cells), open edges %u -> %u\n", b->remeshFrom, b->remeshHull, b->reduceTo,
		       b->remeshResolution, b->remeshOpenBefore, b->openEdges);
	}

	if (b->reduced)
	{
		printf("  reduced       %u -> %u triangles (automatically), draw memory %u -> %u bytes\n", b->reduceFrom, b->reduceTo,
		       b->reduceFrom * RLDCHAR_DRAW_BYTES_G3, b->reduceTo * RLDCHAR_DRAW_BYTES_G3);
	}

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

// The options of make-char without switches: --fit crash, --reduce auto.
static void RldMk_DefaultOptions(struct RldMkOptions *opt)
{
	memset(opt, 0, sizeof(*opt));
	opt->colorTarget = 64u;
	opt->scale = 1.0;
	opt->sizePercent = 100u;
	opt->fit = 1;
	opt->reduce = 1;
	opt->repair = 1;
}

// --diagnose: the diagnosis of a build into `path` (RldMk_Diagnosis); never a
// character. The exit code of make-char: 0 = written and nothing else wrong.
static int RldMk_WriteDiagnosis(const struct RldMkBuild *b, const struct RldMkOptions *opt, int built, const char *path)
{
	size_t size = 0;
	char *text = built ? RldMk_Diagnosis(b, opt, &size) : NULL;
	FILE *file;
	int openError;

	if (text == NULL)
	{
		if (built)
		{
			RldMk_OutOfMemory();
		}
		RldMk_EmitFile("diagnosis", "failed", path, 0u);
		printf("\nno diagnosis written - see the errors above\n");
		return RldMk_Fail(1);
	}

	file = fopen(path, "wb");
	openError = (file == NULL) ? errno : 0;
	if ((file == NULL) || (fwrite(text, 1, size, file) != size) || (fclose(file) != 0))
	{
		RldMk_EmitFile("diagnosis", "failed", path, 0u);
		if (!RldMk_SayMissingFolder("error", "write", path, "The diagnosis file", openError))
		{
			RldMk_Say("error", "write", path, "The diagnosis file %s cannot be written. Is the folder write-protected?", path);
		}
		free(text);
		return RldMk_Fail(1);
	}
	free(text);
	RldMk_EmitFile("diagnosis", "ok", path, size);

	printf("\n%s (diagnosis only - limits lifted, no character written)\n", path);
	printf("  size          %u bytes\n", (u32)size);

	if (s_rldMkErrors != 0)
	{
		return RldMk_Fail(1);
	}

	{
		char bytes[24];

		snprintf(bytes, sizeof(bytes), "%u", (u32)size);
		Rld_Emit("result", "diagnosed", path, bytes, "", (const char *)NULL);
		s_machineResult = 1;
	}
	return 0;
}

// --mask-model: the own mask's model (PLY or OBJ) through the chain with the
// profile of a mask (RldMk_BuildMaskOnce), built twice, then CMSK: version,
// flags 0 and the model frame, checked with the game's RldChar_CheckMask.
// NULL = no mask; the reason was said.
static u8 *RldMk_MaskStep(const char *path, const struct RldMkOptions *opt, size_t *sizeOut)
{
	struct RldMkPly ply;
	struct RldMkModelInfo info;
	struct RldMkBuild build;
	u8 *cmsk = NULL;
	char number[32];
	int built;

	*sizeOut = 0;
	if (!RldMk_LoadModel(path, &ply, &info, RLDMK_ROLE_MASK))
	{
		return NULL;
	}

	built = RldMk_BuildModel(&ply, opt, &build);
	if (built)
	{
		struct RldMkBuild again;
		const int quiet = s_rldMkQuiet;
		const int errorsBefore = s_rldMkErrors;
		int same;

		s_rldMkQuiet = 1;
		same = RldMk_BuildModel(&ply, opt, &again) && (again.cmdlSize == build.cmdlSize) && (memcmp(again.cmdl, build.cmdl, build.cmdlSize) == 0);
		s_rldMkQuiet = quiet;
		s_rldMkErrors = errorsBefore;
		RldMk_FreeBuild(&again);
		if (!same)
		{
			RldMk_Say("error", "model-roundtrip", "two builds of the same mask differ",
			          "Internal error: the packed model does not read back the same (vertex %u). Please report it.", 0u);
			built = 0;
		}
	}

	if (built)
	{
		cmsk = (u8 *)malloc(RLDCHAR_MASK_HEAD_BYTES + build.cmdlSize);
		if (cmsk == NULL)
		{
			RldMk_OutOfMemory();
		}
		else
		{
			Rld_WriteLE16(&cmsk[0x00], RLDCHAR_MASK_VERSION);
			Rld_WriteLE16(&cmsk[0x02], 0u);
			memcpy(&cmsk[RLDCHAR_MASK_HEAD_BYTES], build.cmdl, build.cmdlSize);
			*sizeOut = RLDCHAR_MASK_HEAD_BYTES + build.cmdlSize;
			if (RldChar_CheckMask(cmsk, *sizeOut, NULL, NULL, NULL, NULL) != RLDCHAR_VERDICT_OK)
			{
				RldMk_Say("error", "model-roundtrip", "CMSK", "Internal error: the packed model does not read back the same (vertex %u). Please report it.", 0u);
				free(cmsk);
				cmsk = NULL;
				*sizeOut = 0;
			}
		}
	}

	if (cmsk != NULL)
	{
		char reduced[48];

		reduced[0] = '\0';
		if (build.reduced)
		{
			snprintf(reduced, sizeof(reduced), " (reduced from %u)", build.reduceFrom);
		}
		printf("  own mask      %u triangles%s, draw memory %u bytes per frame (at most %u), %u colors\n", build.drawCount, reduced, build.drawBytes,
		       (u32)RLDCHAR_MASK_DRAW_BYTES_MAX, build.colorCount);
		printf("                x%.4g: %.1f tall (--mask-size %u%% of the retail mask's %.1f), %.1f wide, %.1f deep; checks: the game's mask rules, built twice\n",
		       build.fitFactor, build.size[2], opt->sizePercent, RLDMK_MASK_HEIGHT, build.size[1], build.size[0]);
		snprintf(number, sizeof(number), "%u", build.drawCount);
		Rld_Emit("value", "mask-triangles", number, "built", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", build.drawBytes);
		Rld_Emit("value", "mask-draw-bytes", number, "built", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", build.colorCount);
		Rld_Emit("value", "mask-colors", number, "built", (const char *)NULL);
	}

	RldMk_FreeBuild(&build);
	RldMk_FreePly(&ply);
	return cmsk;
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
	const char *fitArg = NULL;
	const char *reduceArg = NULL;
	const char *wheelsArg = NULL;
	const char *maskArg = NULL;
	const char *maskModelArg = NULL;
	const char *mapColorArg = NULL;
	const char *maskSizeArg = NULL;
	const char *maskUpArg = NULL;
	const char *maskForwardArg = NULL;
	struct RldMkOptions maskOpt;
	u8 *cmsk = NULL;
	size_t cmskSize = 0;
	const char *outArg = NULL;
	const char *sizeArg = NULL;
	const char *iconArg = NULL;
	const char *iconPreviewArg = NULL;
	const char *iconBackgroundArg = NULL;
	const char *iconToleranceArg = NULL;
	const char *iconFitArg = NULL;
	const char *iconFrameArg = NULL;
	struct RldMkIconOptions iconOptions = s_rldMkIconDefault;
	const char *voicesArg = NULL;
	static const char *voiceAssign[RLDMK_VOICE_ASSIGN_MAX];
	u32 voiceAssignCount = 0;
	int voiceNormalize = 0;
	const char *voicePreviewArg = NULL;
	u8 *cvoi = NULL;
	size_t cvoiSize = 0;
	const char *previewArg = NULL;
	const char *diagnoseArg = NULL;
	const char *remeshArg = NULL;
	const char *resolutionArg = NULL;
	const char *repairArg = NULL;
	const char *openArg = NULL;
	static struct RldMkIcon icon;
	int haveIcon = 0;
	int check = 0;
	int authorGiven;
	int built;
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkModelInfo modelInfo;
	struct RldMkBuild build;
	u8 nameField[RLDCHAR_NAME_FIELD];
	u32 templateId = RLDMK_NONE;
	u32 classId = RLDMK_NONE;
	u32 charVersion = 1;
	u8 *chri = NULL;
	size_t chriSize = 0;
	u8 *container = NULL;
	size_t containerSize = 0;
	char digestText[65];
	int i;

	RldMk_DefaultOptions(&opt);
	memset(&ply, 0, sizeof(ply));
	memset(&build, 0, sizeof(build));
	memset(nameField, 0, sizeof(nameField));
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
		else if ((strcmp(arg, "--fit") == 0) && hasValue)
		{
			fitArg = argv[++i];
		}
		else if ((strcmp(arg, "--reduce") == 0) && hasValue)
		{
			reduceArg = argv[++i];
		}
		else if ((strcmp(arg, "--wheels") == 0) && hasValue)
		{
			wheelsArg = argv[++i];
		}
		else if ((strcmp(arg, "--mask") == 0) && hasValue)
		{
			maskArg = argv[++i];
		}
		else if ((strcmp(arg, "--mask-model") == 0) && hasValue)
		{
			maskModelArg = argv[++i];
		}
		else if ((strcmp(arg, "--map-color") == 0) && hasValue)
		{
			mapColorArg = argv[++i];
		}
		else if ((strcmp(arg, "--mask-size") == 0) && hasValue)
		{
			maskSizeArg = argv[++i];
		}
		else if ((strcmp(arg, "--mask-up") == 0) && hasValue)
		{
			maskUpArg = argv[++i];
		}
		else if ((strcmp(arg, "--mask-forward") == 0) && hasValue)
		{
			maskForwardArg = argv[++i];
		}
		else if ((strcmp(arg, "--out") == 0) && hasValue)
		{
			outArg = argv[++i];
		}
		else if ((strcmp(arg, "--size") == 0) && hasValue)
		{
			sizeArg = argv[++i];
		}
		else if ((strcmp(arg, "--icon") == 0) && hasValue)
		{
			iconArg = argv[++i];
		}
		else if ((strcmp(arg, "--icon-preview") == 0) && hasValue)
		{
			iconPreviewArg = argv[++i];
		}
		else if ((strcmp(arg, "--icon-background") == 0) && hasValue)
		{
			iconBackgroundArg = argv[++i];
		}
		else if ((strcmp(arg, "--icon-tolerance") == 0) && hasValue)
		{
			iconToleranceArg = argv[++i];
		}
		else if ((strcmp(arg, "--icon-fit") == 0) && hasValue)
		{
			iconFitArg = argv[++i];
		}
		else if ((strcmp(arg, "--icon-frame") == 0) && hasValue)
		{
			iconFrameArg = argv[++i];
		}
		else if ((strcmp(arg, "--voices") == 0) && hasValue)
		{
			voicesArg = argv[++i];
		}
		else if ((strcmp(arg, "--voice") == 0) && hasValue)
		{
			if (voiceAssignCount < RLDMK_VOICE_ASSIGN_MAX)
			{
				voiceAssign[voiceAssignCount++] = argv[i + 1];
			}
			else
			{
				RldMk_Say("error", "usage", argv[i + 1], "At most %u sounds can be given an event one by one.", RLDMK_VOICE_ASSIGN_MAX);
			}
			i++;
		}
		else if (strcmp(arg, "--voice-normalize") == 0)
		{
			voiceNormalize = 1;
		}
		else if ((strcmp(arg, "--voice-preview") == 0) && hasValue)
		{
			voicePreviewArg = argv[++i];
		}
		else if ((strcmp(arg, "--preview") == 0) && hasValue)
		{
			previewArg = argv[++i];
		}
		else if ((strcmp(arg, "--repair") == 0) && hasValue)
		{
			repairArg = argv[++i];
		}
		else if ((strcmp(arg, "--open-parts") == 0) && hasValue)
		{
			openArg = argv[++i];
		}
		else if ((strcmp(arg, "--remesh") == 0) && hasValue)
		{
			remeshArg = argv[++i];
		}
		else if ((strcmp(arg, "--remesh-resolution") == 0) && hasValue)
		{
			resolutionArg = argv[++i];
		}
		else if ((strcmp(arg, "--diagnose") == 0) && hasValue)
		{
			diagnoseArg = argv[++i];
			opt.diagnose = 1;
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

	// --diagnose writes the diagnosis and never a character (RldMk_Diagnosis).
	if (opt.diagnose)
	{
		RldMk_Say("info", "diagnose", NULL, "Diagnosis only: limits lifted, no character written.");
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

	// --scale only with --fit none: the fit decides the scale itself.
	if (RldMk_FitSwitch(fitArg, scaleArg, &opt.fit) && (scaleArg != NULL))
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

	// --wheels and --reduce in any case, as --fit and --up.
	if (wheelsArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(wheelsArg, word, sizeof(word));

		if (known && (strcmp(word, "off") == 0))
		{
			opt.noWheels = 1;
		}
		else if (!known || (strcmp(word, "on") != 0))
		{
			RldMk_Say("error", "wheels", wheelsArg, "Wheels must be on or off.");
		}
	}
	(void)RldMk_MaskSwitch(maskArg, &opt.mask);
	if (mapColorArg != NULL)
	{
		opt.hasMapColor = RldMk_MapColorSwitch(mapColorArg, &opt.mapColor);
	}

	// THE OWN MASK: its own options, the profile of a mask (RldMk_BuildMaskOnce).
	RldMk_DefaultOptions(&maskOpt);
	maskOpt.maskModel = 1;
	maskOpt.still = 1;
	if ((maskModelArg == NULL) && ((maskSizeArg != NULL) || (maskUpArg != NULL) || (maskForwardArg != NULL)))
	{
		RldMk_Say("error", "usage", "--mask-size", "--mask-size, --mask-up and --mask-forward need --mask-model <ply>.");
	}
	if ((voicesArg == NULL) && ((voiceAssignCount != 0u) || voiceNormalize || (voicePreviewArg != NULL)))
	{
		RldMk_Say("error", "usage", "--voice", "--voice, --voice-normalize and --voice-preview need --voices <folder>.");
	}
	if ((maskSizeArg != NULL) && (!RldMk_ParseWhole(maskSizeArg, 100000u, &maskOpt.sizePercent) || (maskOpt.sizePercent < RLDMK_MASK_SIZE_MIN) ||
	                              (maskOpt.sizePercent > RLDMK_MASK_SIZE_MAX)))
	{
		RldMk_Say("error", "mask-size", maskSizeArg, "The mask size \"%s\" must be a whole number from 50 to 150 (percent of the retail mask's height).",
		          maskSizeArg);
		maskOpt.sizePercent = 100u;
	}
	if (maskUpArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(maskUpArg, word, sizeof(word));

		if (known && (strcmp(word, "z") == 0))
		{
			maskOpt.upZ = 1;
		}
		else if (!known || (strcmp(word, "y") != 0))
		{
			RldMk_Say("error", "mask-up", maskUpArg, "The up axis of the mask must be y or z.");
		}
	}
	if (maskForwardArg != NULL)
	{
		if (strcmp(maskForwardArg, "-z") == 0)
		{
			maskOpt.backwards = 1;
		}
		else if ((strcmp(maskForwardArg, "z") != 0) && (strcmp(maskForwardArg, "+z") != 0))
		{
			RldMk_Say("error", "mask-forward", maskForwardArg, "Forward of the mask must be z or -z.");
		}
	}

	if (reduceArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(reduceArg, word, sizeof(word));

		if (known && (strcmp(word, "off") == 0))
		{
			opt.reduce = 0;
		}
		else if (!known || (strcmp(word, "auto") != 0))
		{
			RldMk_Say("error", "reduce", reduceArg, "Reduce must be auto or off.");
		}
	}

	if (repairArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(repairArg, word, sizeof(word));

		if (known && (strcmp(word, "off") == 0))
		{
			opt.repair = 0;
		}
		else if (!known || (strcmp(word, "auto") != 0))
		{
			RldMk_Say("error", "repair", repairArg, "Repair must be auto or off.");
		}
	}
	if (openArg != NULL)
	{
		char word[16];
		const int known = RldMk_Lower(openArg, word, sizeof(word));

		if (known && (strcmp(word, "one-sided") == 0))
		{
			opt.openOneSided = 1;
		}
		else if (!known || (strcmp(word, "two-sided") != 0))
		{
			RldMk_Say("error", "open-parts", openArg, "Open parts must be two-sided or one-sided.");
		}
	}

	// --remesh (on request only) needs the reduction: a hull has some 10^5 triangles.
	if (remeshArg != NULL)
	{
		char word[8];
		const int known = RldMk_Lower(remeshArg, word, sizeof(word));

		if (known && (strcmp(word, "on") == 0))
		{
			opt.remesh = 1;
		}
		else if (!known || (strcmp(word, "off") != 0))
		{
			RldMk_Say("error", "remesh", remeshArg, "Remesh must be on or off.");
		}
	}
	if ((resolutionArg != NULL) &&
	    (!RldMk_ParseWhole(resolutionArg, 100000u, &opt.remeshResolution) || (opt.remeshResolution < RLDMK_REMESH_RESOLUTION_MIN) ||
	     (opt.remeshResolution > RLDMK_REMESH_RESOLUTION_MAX)))
	{
		RldMk_Say("error", "remesh", resolutionArg, "The remesh resolution \"%s\" must be a whole number from %u to %u.", resolutionArg,
		          RLDMK_REMESH_RESOLUTION_MIN, RLDMK_REMESH_RESOLUTION_MAX);
		opt.remeshResolution = 0u;
	}
	if (opt.remesh && !opt.reduce)
	{
		RldMk_Say("error", "remesh", NULL, "--remesh on needs --reduce auto: the hulls have far more triangles than a driver may draw.");
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

	if ((sizeArg != NULL) && (!RldMk_ParseWhole(sizeArg, 100000u, &opt.sizePercent) || (opt.sizePercent < RLDMK_SIZE_MIN) || (opt.sizePercent > RLDMK_SIZE_MAX)))
	{
		RldMk_Say("error", "char-size", sizeArg, "The size \"%s\" must be a whole number from 50 to 200 (percent; %s).", sizeArg, RLDMK_SIZE_TEXT);
		opt.sizePercent = 100u;
	}

	if ((iconPreviewArg != NULL) && (iconArg == NULL))
	{
		RldMk_Say("error", "usage", "--icon-preview", "--icon-preview needs --icon <png>: there is no portrait to show without it.");
	}
	if (((iconBackgroundArg != NULL) || (iconToleranceArg != NULL) || (iconFitArg != NULL) || (iconFrameArg != NULL)) && (iconArg == NULL))
	{
		RldMk_Say("error", "usage", "--icon-*", "--icon-background, --icon-tolerance, --icon-fit and --icon-frame need --icon <png>.");
	}
	(void)RldMk_IconSwitches(iconBackgroundArg, iconToleranceArg, iconFitArg, iconFrameArg, &iconOptions);

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

		if ((Rld_EndsWithNoCase(modelArg, ".ply") || Rld_EndsWithNoCase(modelArg, ".obj")) && (length > 4u))
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
		Rld_Emit("value", "scale", number, ((scaleArg != NULL) && !opt.fit) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "fit", opt.fit ? "crash" : "none", (fitArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "reduce", opt.reduce ? "auto" : "off", (reduceArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "wheels", opt.noWheels ? "off" : "on", (wheelsArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "mask", RldMk_MaskWord(opt.mask, templateId), (opt.mask != RLDCHAR_MASK_LIKE) ? "switch" : "template", (const char *)NULL);
		{
			char color[8];

			RldMk_MapColorText(opt.mapColor, color);
			Rld_Emit("value", "map-color", opt.hasMapColor ? color : "template", opt.hasMapColor ? "switch" : "template", (const char *)NULL);
		}
		if (maskModelArg != NULL)
		{
			Rld_Emit("value", "mask-model", maskModelArg, "switch", (const char *)NULL);
			snprintf(number, sizeof(number), "%u", maskOpt.sizePercent);
			Rld_Emit("value", "mask-size", number, (maskSizeArg != NULL) ? "switch" : "default", (const char *)NULL);
		}
		Rld_Emit("value", "remesh", opt.remesh ? "on" : "off", (remeshArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "repair", opt.repair ? "auto" : "off", (repairArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "open-parts", opt.openOneSided ? "one-sided" : "two-sided", (openArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "two_sided", opt.twoSided ? "yes" : "no", opt.twoSided ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "out", opt.diagnose ? "none" : outPath, (outArg != NULL) ? "switch" : "default", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", opt.sizePercent);
		Rld_Emit("value", "size", number, (sizeArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "icon", (iconArg != NULL) ? iconArg : "", (iconArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "icon-background", s_rldMkIconBackgroundWords[iconOptions.background], (iconBackgroundArg != NULL) ? "switch" : "default",
		         (const char *)NULL);
		snprintf(number, sizeof(number), "%u", iconOptions.tolerance);
		Rld_Emit("value", "icon-tolerance", number, (iconToleranceArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "icon-fit", s_rldMkIconFitWords[iconOptions.fit], (iconFitArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "icon-frame", s_rldMkIconFrameWords[iconOptions.frame], (iconFrameArg != NULL) ? "switch" : "default", (const char *)NULL);
		Rld_Emit("value", "voices", (voicesArg != NULL) ? voicesArg : "", (voicesArg != NULL) ? "switch" : "default", (const char *)NULL);
		if (voicesArg != NULL)
		{
			Rld_Emit("value", "voice-normalize", voiceNormalize ? "on" : "off", voiceNormalize ? "switch" : "default", (const char *)NULL);
		}
	}

	// The retail kart as the size reference, in the units of the preview
	// (x0 y0 z0 x1 y1 z1); the height up to the seat line (hip y 16) - the
	// retail kart's own height is not measured.
	{
		char box[6][16];

		snprintf(box[0], sizeof(box[0]), "%d", -RLDMK_RETAIL_KART_WIDTH / 2);
		snprintf(box[1], sizeof(box[1]), "%d", RLDMK_RETAIL_KART_BOTTOM);
		snprintf(box[2], sizeof(box[2]), "%d", -RLDMK_RETAIL_KART_LENGTH / 2);
		snprintf(box[3], sizeof(box[3]), "%d", RLDMK_RETAIL_KART_WIDTH / 2);
		snprintf(box[4], sizeof(box[4]), "%d", RLDMK_RETAIL_KART_TOP);
		snprintf(box[5], sizeof(box[5]), "%d", RLDMK_RETAIL_KART_LENGTH / 2);
		Rld_Emit("value", "retail-kart", box[0], box[1], box[2], box[3], box[4], box[5], (const char *)NULL);
	}

	// Crash with his kart in the birth pose, the size --fit crash scales to
	// (x0 y0 z0 x1 y1 z1, the same units).
	{
		static const double crash[6] = RLDMK_CRASH_BOX;
		char box[6][16];
		int k;

		for (k = 0; k < 6; k++)
		{
			snprintf(box[k], sizeof(box[k]), "%.1f", crash[k]);
		}
		Rld_Emit("value", "crash-box", box[0], box[1], box[2], box[3], box[4], box[5], (const char *)NULL);
	}

	// THE PORTRAIT AND THE VOICES before the model: their findings show even
	// when the model cannot be read.
	if (iconArg != NULL)
	{
		haveIcon = RldMk_IconStep(iconArg, iconPreviewArg, &iconOptions, argv[0], templateId, &icon);
	}
	if (voicesArg != NULL)
	{
		struct RldMkVoiceOptions voiceOptions;

		voiceOptions.assign = voiceAssign;
		voiceOptions.assignCount = voiceAssignCount;
		voiceOptions.normalize = voiceNormalize;
		voiceOptions.preview = voicePreviewArg;
		cvoi = RldMk_VoiceStep(voicesArg, &voiceOptions, &cvoiSize);
	}

	// THE MODEL: PLY or OBJ, by its content (tools/rldpack_import.inc).
	if (!RldMk_LoadModel(modelArg, &ply, &modelInfo, RLDMK_ROLE_MAIN))
	{
		free(cvoi);
		return RldMk_Fail(1);
	}

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

	if (maskModelArg != NULL)
	{
		cmsk = RldMk_MaskStep(maskModelArg, &maskOpt, &cmskSize);
	}

	// THE PREVIEW of a model that was built, whatever else is wrong: Reload
	// Studio shows it while the author fixes the name.
	if (built && (previewArg != NULL))
	{
		RldMk_WritePreview(&build, previewArg);
	}

	if (!s_rldMkQuiet)
	{
		if (opt.sizePercent != 100u)
		{
			printf("  driver size   %u%% (the kart stays as it is; %s)\n", opt.sizePercent, RLDMK_SIZE_TEXT);
		}
		if (haveIcon)
		{
			printf("  icon          %s: %u x %u, cut %u x %u at %u,%u -> %u x %u (the menu shows 43 x 25 of 44 x 26), %u colors, %u of %u texels opaque\n",
			       iconArg, icon.source[0], icon.source[1], icon.crop[2], icon.crop[3], icon.crop[0], icon.crop[1], icon.scaled[0], icon.scaled[1],
			       icon.colorCount, icon.opaque, RLDMK_ICON_TEXELS);
			if (icon.cleared > 0u)
			{
				printf("  icon bg       %u pixels around the subject made transparent (background %06X)\n", icon.cleared, icon.backgroundRgb);
			}
			if (icon.place[2] > 0u)
			{
				printf("  icon fit      subject %u x %u at %u,%u -> %u x %u at %u,%u of 43 x 25 (a retail head: 41 x 25 at 0,0)\n", icon.subject[2],
				       icon.subject[3], icon.subject[0], icon.subject[1], icon.place[2], icon.place[3], icon.place[0], icon.place[1]);
			}
		}
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
		printf("  wheels        %s\n", opt.noWheels ? "hidden - the game draws no kart wheels (--wheels off)" : "shown - the game draws its kart wheels");
		if (templateId < RLDMK_TEMPLATE_COUNT)
		{
			printf("  mask          %s (%s)\n", RldMk_MaskName(opt.mask, templateId),
			       (opt.mask == RLDCHAR_MASK_AKU) ? "--mask aku" : ((opt.mask == RLDCHAR_MASK_UKA) ? "--mask uka" : "like the template"));
		}
		if (opt.hasMapColor)
		{
			char color[8];

			RldMk_MapColorText(opt.mapColor, color);
			printf("  map color     %s (--map-color)\n", color);
		}
		else
		{
			printf("  map color     the template's\n");
		}
	}

	// THE DIAGNOSIS ends here: a build with lifted limits never becomes a
	// container, whatever --out or --check say.
	if (opt.diagnose)
	{
		const int code = RldMk_WriteDiagnosis(&build, &opt, built, diagnoseArg);

		free(cmsk);
		free(cvoi);
		RldMk_FreeBuild(&build);
		RldMk_FreePly(&ply);
		return code;
	}

	if (!built || (s_rldMkErrors != 0) || ((maskModelArg != NULL) && (cmsk == NULL)))
	{
		printf("\nnothing written - see the errors above\n");
		free(cmsk);
		free(cvoi);
		RldMk_FreeBuild(&build);
		RldMk_FreePly(&ply);
		return RldMk_Fail(1);
	}

	// CHRI, checked with the reader the game runs, and the container.
	chri = RldMk_BuildChriColor(templateId, classId, nameField, charVersion, authorArg, RldMk_ChriFlags(&opt), opt.mapColor, &chriSize);
	if (chri != NULL)
	{
		struct RldCharInfo back;
		const char *why = RldChar_ParseInfo(&back, chri, chriSize);
		u32 color = 0;

		if ((why == NULL) && (back.flags != RldMk_ChriFlags(&opt)))
		{
			why = "the flags differ";
		}
		if ((why == NULL) && (RldChar_MapColor(&back, &color) != opt.hasMapColor))
		{
			why = "the map color differs";
		}
		if ((why == NULL) && opt.hasMapColor && (color != opt.mapColor))
		{
			why = "the map color differs";
		}
		if (why != NULL)
		{
			RldMk_Say("error", "chri", why, "Internal error: the character info does not read back (%s). Please report it.", why);
			free(chri);
			chri = NULL;
		}
	}

	if (chri != NULL)
	{
		container = RldMk_ContainerVoices(chri, chriSize, build.cmdl, build.cmdlSize, haveIcon ? icon.cicn : NULL, cmsk, cmskSize, cvoi, cvoiSize,
		                                  &containerSize);
	}

	if (container == NULL)
	{
		if (s_rldMkErrors == 0)
		{
			RldMk_OutOfMemory();
		}
		free(cmsk);
		free(cvoi);
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
		const int openError = (file == NULL) ? errno : 0;

		if ((file == NULL) || (fwrite(container, 1, containerSize, file) != containerSize) || (fclose(file) != 0))
		{
			if (!RldMk_SayMissingFolder("error", "write", outPath, "The character file", openError))
			{
				RldMk_Say("error", "write", outPath,
				          "The character file cannot be written. Is the folder write-protected, or is the file open in another program?");
			}
			free(cmsk);
			free(cvoi);
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
	if (haveIcon)
	{
		printf("  CICN          %u bytes (the portrait)\n", (u32)RLDCHAR_ICON_BYTES);
	}
	if (cmsk != NULL)
	{
		printf("  CMSK          %u bytes (the own mask)\n", (u32)cmskSize);
	}
	if (cvoi != NULL)
	{
		struct RldCharVoices voices;
		u32 frames = 0;
		u32 events = 0;
		u32 c;
		int e;

		(void)RldChar_CheckVoices(cvoi, cvoiSize, &voices);
		for (c = 0; c < voices.clipCount; c++)
		{
			frames += voices.clip[c].frames;
		}
		for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
		{
			events += (voices.count[e] != 0u) ? 1u : 0u;
		}
		printf("  CVOI          %u bytes (the voices: %u sounds in %u of %d events, %u.%02u s%s)\n", (u32)cvoiSize, voices.clipCount, events,
		       RLDCHAR_VOICE_EVENTS, frames / RLDCHAR_VOICE_RATE, (frames % RLDCHAR_VOICE_RATE) * 100u / RLDCHAR_VOICE_RATE,
		       voiceNormalize ? ", normalized" : "");
	}
	printf("  sha256        %s\n", digestText);
	printf("  format        %d.%d%s\n", RLDCHAR_VERSION_MAJOR, RLDCHAR_VERSION_MINOR, (RLDCHAR_VERSION_MINOR == 0) ? " (draft)" : "");

	{
		char bytes[24];

		snprintf(bytes, sizeof(bytes), "%u", (u32)containerSize);
		Rld_Emit("result", check ? "checked" : "ok", outPath, bytes, digestText, (const char *)NULL);
		s_machineResult = 1;
	}

	free(cmsk);
	free(cvoi);
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

// Length (z) and height (y) of a checked model in the neutral turn frame, game
// units as the renderer decodes them (RldMk_UnpackRecord) - next to Crash's.
static void RldMk_ModelExtent(const u8 *cmdl, u32 records, double extent[2])
{
	const u8 *body = &cmdl[4];
	const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
	const u32 table = Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS]);
	const u32 turn = Rld_ReadLE32(&body[table]);
	const u32 frameSize = Rld_ReadLE16(&body[turn + RLDMK_ANIM_FRAME_SIZE]);
	const u32 frames = Rld_ReadLE16(&body[turn + RLDMK_ANIM_NUM_FRAMES]) & 0x7FFFu;
	const u8 *frame = &body[turn + RLDMK_ANIM_BYTES + ((frames > RLDMK_NEUTRAL_FRAME) ? RLDMK_NEUTRAL_FRAME : 0u) * frameSize];
	double lo[3] = {1.0e30, 1.0e30, 1.0e30};
	double hi[3] = {-1.0e30, -1.0e30, -1.0e30};
	int scale[3];
	u32 r;
	int a;

	RldMk_ModelScale(cmdl, scale);
	for (r = 0; r < records; r++)
	{
		int v[3];

		RldMk_UnpackRecord(frame, r, v);
		for (a = 0; a < 3; a++)
		{
			const double unit = (double)v[a] * (double)scale[a] / 16384.0;

			lo[a] = (unit < lo[a]) ? unit : lo[a];
			hi[a] = (unit > hi[a]) ? unit : hi[a];
		}
	}

	extent[0] = (records != 0u) ? (hi[2] - lo[2]) : 0.0;
	extent[1] = (records != 0u) ? (hi[1] - lo[1]) : 0.0;
}

// "18 sounds in 10 of 10 events, 21.30 s: boost 2, hit 2, ..." - the voices
// of a CVOI that passed RldChar_CheckVoices.
static void RldMk_VoiceSummary(const struct RldCharVoices *voices, char *text, size_t size)
{
	u32 frames = 0;
	u32 events = 0;
	size_t used;
	u32 c;
	int e;

	for (c = 0; c < voices->clipCount; c++)
	{
		frames += voices->clip[c].frames;
	}
	for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
	{
		events += (voices->count[e] != 0u) ? 1u : 0u;
	}
	snprintf(text, size, "%u sounds in %u of %d events, %u.%02u s:", voices->clipCount, events, RLDCHAR_VOICE_EVENTS, frames / RLDCHAR_VOICE_RATE,
	         (frames % RLDCHAR_VOICE_RATE) * 100u / RLDCHAR_VOICE_RATE);
	for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
	{
		used = strlen(text);
		snprintf(&text[used], size - used, "%s %s %u", (e == 0) ? "" : ",", s_rldCharVoiceEvents[e], (u32)voices->count[e]);
	}
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
	int iconIndex = -1;
	const char *iconState = "none";
	const char *iconWhy = "";
	int maskIndex = -1;
	const char *maskState = "none";
	struct RldCharFinding maskFirst;
	struct RldCharModelFacts maskFacts;
	int voiceIndex = -1;
	const char *voiceState = "none";
	const char *voiceWhy = "";
	struct RldCharVoices voices;
	char voiceText[256];
	int failed = 0;
	char text[256];
	int i;

	memset(&reader, 0, sizeof(reader));
	memset(&facts, 0, sizeof(facts));
	memset(&first, 0, sizeof(first));
	memset(&maskFirst, 0, sizeof(maskFirst));
	memset(&maskFacts, 0, sizeof(maskFacts));
	memset(&voices, 0, sizeof(voices));
	voiceText[0] = '\0';
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

	// The portrait: optional, and a broken one drops only itself (CICN-1..3) -
	// the game then shows the template's portrait.
	if (Rld_FindEntry(&reader, "CICN", &iconIndex) != NULL)
	{
		size_t iconSize = 0;
		const char *iconError = NULL;
		u8 *iconData = Rld_ReadChunk(&reader, iconIndex, &iconSize, &iconError);

		iconWhy = (iconData != NULL) ? RldChar_CheckIcon(iconData, iconSize) : ((iconError != NULL) ? iconError : "CICN cannot be read");
		iconState = (iconWhy == NULL) ? "ok" : "ignored";
		iconWhy = (iconWhy == NULL) ? "" : iconWhy;
		free(iconData);
	}

	// The own mask: optional, and a broken one drops only itself (CMSK-1..3 and
	// the model rules) - the game then shows the retail mask.
	if (Rld_FindEntry(&reader, "CMSK", &maskIndex) != NULL)
	{
		size_t maskSize = 0;
		const char *maskError = NULL;
		u8 *maskData = Rld_ReadChunk(&reader, maskIndex, &maskSize, &maskError);

		maskState = "ignored";
		if (maskData == NULL)
		{
			maskFirst.rule = "CMSK";
			snprintf(maskFirst.detail, sizeof(maskFirst.detail), "%s", (maskError != NULL) ? maskError : "CMSK cannot be read");
		}
		else if (RldChar_CheckMask(maskData, maskSize, &maskFirst, NULL, NULL, &maskFacts) == RLDCHAR_VERDICT_OK)
		{
			maskState = "own";
		}
		free(maskData);
	}

	// The voices: optional, and broken ones drop only themselves (CVOI-1..4) -
	// the driver is then silent.
	if (Rld_FindEntry(&reader, "CVOI", &voiceIndex) != NULL)
	{
		size_t voiceSize = 0;
		const char *voiceError = NULL;
		u8 *voiceData = Rld_ReadChunk(&reader, voiceIndex, &voiceSize, &voiceError);

		voiceWhy = (voiceData != NULL) ? RldChar_CheckVoices(voiceData, (u64)voiceSize, &voices) : ((voiceError != NULL) ? voiceError : "CVOI cannot be read");
		voiceState = (voiceWhy == NULL) ? "ok" : "ignored";
		voiceWhy = (voiceWhy == NULL) ? "" : voiceWhy;
		if (voiceWhy[0] == '\0')
		{
			RldMk_VoiceSummary(&voices, voiceText, sizeof(voiceText));
		}
		free(voiceData);
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
		printf("  wheels        %s\n", ((info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0u) ? "hidden" : "shown");
		if (RldChar_Mask(info.flags) != RLDCHAR_MASK_LIKE)
		{
			printf("  mask          %s\n", RldMk_MaskName(RldChar_Mask(info.flags), info.templateId));
		}
		else
		{
			printf("  mask          %s (%s)\n", RldMk_MaskName(RLDCHAR_MASK_LIKE, info.templateId),
			       ((info.flags & RLDCHAR_FLAG_MASK_BITS) != 0u) ? "reserved value 3, read as like the template" : "like the template");
		}
		if ((info.flags & ~(u32)RLDCHAR_FLAGS_KNOWN) != 0u)
		{
			printf("  flags         0x%08x (unknown bits ignored)\n", info.flags);
		}
		printf("  chunks        ");
		for (i = 0; i < reader.chunkCount; i++)
		{
			const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];

			printf("%.4s%s%s", (const char *)entry, (RldChar_ChunkLimit(entry) != 0u) ? "" : " (skipped)", ((i + 1) < reader.chunkCount) ? " " : "\n");
		}
		if (strcmp(iconState, "ok") == 0)
		{
			printf("  portrait      its own (CICN, 44 x 26)\n");
		}
		else if (strcmp(iconState, "none") == 0)
		{
			printf("  portrait      the template's (no CICN)\n");
		}
		else
		{
			printf("  portrait      the template's - CICN ignored: %s\n", iconWhy);
		}
		{
			char color[8];
			u32 mapColor = 0;

			if (RldChar_MapColor(&info, &mapColor))
			{
				RldMk_MapColorText(mapColor, color);
				printf("  map color     %s\n", color);
			}
			else
			{
				printf("  map color     the template's\n");
			}
		}
		if (strcmp(maskState, "own") == 0)
		{
			printf("  own mask      %u triangles, %u bytes of draw memory per frame (at most %u), %u colors (CMSK)\n", maskFacts.triangles,
			       maskFacts.drawBytes, (u32)RLDCHAR_MASK_DRAW_BYTES_MAX, maskFacts.colorCount);
		}
		else if (strcmp(maskState, "none") == 0)
		{
			printf("  own mask      none - the retail mask (no CMSK)\n");
		}
		else
		{
			printf("  own mask      the retail mask - CMSK ignored: %s %s\n", (maskFirst.rule != NULL) ? maskFirst.rule : "", maskFirst.detail);
		}
		if (strcmp(voiceState, "ok") == 0)
		{
			printf("  voices        %s (CVOI)\n", voiceText);
		}
		else if (strcmp(voiceState, "none") == 0)
		{
			printf("  voices        none - the driver is silent (no CVOI)\n");
		}
		else
		{
			printf("  voices        none - the driver is silent; CVOI ignored: %s\n", voiceWhy);
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
		Rld_Emit("value", "wheels", ((info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0u) ? "off" : "on", "container", (const char *)NULL);
		Rld_Emit("value", "mask", RldMk_MaskWord(RldChar_Mask(info.flags), info.templateId),
		         (RldChar_Mask(info.flags) != RLDCHAR_MASK_LIKE) ? "container" : "template", (const char *)NULL);
		{
			char color[8];
			u32 mapColor = 0;
			const int own = RldChar_MapColor(&info, &mapColor);

			RldMk_MapColorText(mapColor, color);
			Rld_Emit("value", "map-color", own ? color : "template", own ? "container" : "template", (const char *)NULL);
		}
		if ((info.flags & ~(u32)RLDCHAR_FLAGS_KNOWN) != 0u)
		{
			snprintf(number, sizeof(number), "0x%08x", info.flags);
			Rld_Emit("value", "flags", number, "container", (const char *)NULL);
		}
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
		Rld_Emit("char", "icon", iconState, iconWhy, (const char *)NULL);
		if (strcmp(maskState, "ignored") == 0)
		{
			snprintf(text, sizeof(text), "%s %s", (maskFirst.rule != NULL) ? maskFirst.rule : "", maskFirst.detail);
			Rld_Emit("value", "own-mask", "ignored", text, (const char *)NULL);
		}
		else
		{
			Rld_Emit("value", "own-mask", maskState, "container", (const char *)NULL);
		}
		if (strcmp(maskState, "own") == 0)
		{
			snprintf(number, sizeof(number), "%u", maskFacts.triangles);
			Rld_Emit("value", "mask-triangles", number, "container", (const char *)NULL);
			snprintf(number, sizeof(number), "%u", maskFacts.drawBytes);
			Rld_Emit("value", "mask-draw-bytes", number, "container", (const char *)NULL);
		}
		Rld_Emit("value", "own-voices", voiceState, (strcmp(voiceState, "ok") == 0) ? voiceText : ((strcmp(voiceState, "none") == 0) ? "container" : voiceWhy),
		         (const char *)NULL);
		if (strcmp(voiceState, "ok") == 0)
		{
			u32 events = 0;
			int e;

			for (e = 0; e < RLDCHAR_VOICE_EVENTS; e++)
			{
				snprintf(number, sizeof(number), "%u", (u32)voices.count[e]);
				Rld_Emit("voiceevent", s_rldCharVoiceEvents[e], number, (const char *)NULL);
				events += (voices.count[e] != 0u) ? 1u : 0u;
			}
			snprintf(number, sizeof(number), "%u", voices.clipCount);
			snprintf(text, sizeof(text), "%u", events);
			Rld_Emit("char", "voices", number, text, (const char *)NULL);
		}
	}
	else
	{
		printf("  model         %u triangles, %u records, %u colors, frames %u,%u,%u,%u of %u bytes, scale %u %u %u\n", facts.triangles, facts.records,
		       facts.colorCount, facts.frames[0], facts.frames[1], facts.frames[2], facts.frames[3], facts.frameSize[0], facts.scale[0], facts.scale[1],
		       facts.scale[2]);
		printf("  draw memory   %u bytes per frame, at most %u\n", facts.drawBytes, RLDCHAR_DRAW_BYTES_MAX);
		if (verdict == RLDCHAR_VERDICT_OK)
		{
			double extent[2];

			RldMk_ModelExtent(cmdl, facts.records, extent);
			printf("  size          %.1f long, %.1f tall (game units, steering straight); Crash is %.1f long, %.1f tall\n", extent[0], extent[1],
			       RLDMK_CRASH_LENGTH, RLDMK_CRASH_HEIGHT);
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

			if ((i == iconIndex) && (RldChar_CheckIcon(data, size) != NULL))
			{
				printf("  IGNORED CICN  %s - the game shows the template's portrait\n", RldChar_CheckIcon(data, size));
			}
			else if ((i == maskIndex) && (strcmp(maskState, "own") != 0))
			{
				printf("  IGNORED CMSK  %s %s - the game shows the retail mask\n", (maskFirst.rule != NULL) ? maskFirst.rule : "", maskFirst.detail);
			}
			else if ((i == voiceIndex) && (strcmp(voiceState, "ok") != 0))
			{
				printf("  IGNORED CVOI  %s - the driver is silent\n", voiceWhy);
			}
			else
			{
				printf("  ok     %.4s   %u bytes\n", (const char *)entry, (u32)size);
			}
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
	fprintf(out, "  rldpack make-char --model <model> --name <text> --template <0..14> [switches]\n");
	fprintf(out, "  Builds a character (.rldchar) from a PLY or OBJ model, +Y up and +Z\n");
	fprintf(out, "  forward, at any scale: it is fitted to Crash's\n");
	fprintf(out, "  size (--fit). The model brings its kart: a part at the bottom, as long\n");
	fprintf(out, "  as the kart of a driver; the game draws the wheels.\n");
	fprintf(out, "  The file lands NEXT TO the model as <model>.rldchar.\n");
	fprintf(out, "\n");
	fprintf(out, "  --model <model>             the model (required), PLY or OBJ - the content\n");
	fprintf(out, "                              decides. PLY: vertex colors, ASCII or binary.\n");
	fprintf(out, "                              OBJ: vertex colors (v x y z r g b) win over\n");
	fprintf(out, "                              the MTL file; else Kd times the texture of\n");
	fprintf(out, "                              map_Kd (PNG, JPEG, TGA, sampled at the vt of\n");
	fprintf(out, "                              each corner); else grey\n");
	fprintf(out, "  --name <text>               the name in the menu (required), 1 to 17 of\n");
	fprintf(out, "                              A-Z 0-9 space ! %% ' + , - . / : < = > ? _\n");
	fprintf(out, "  --template <0..14>          the driver the character stands in for\n");
	fprintf(out, "                              (required) - the portrait (without --icon)\n");
	fprintf(out, "                              and the frame counts come from it; the class\n");
	fprintf(out, "                              decides the kart's values:\n");
	for (t = 0; t < RLDMK_TEMPLATE_COUNT; t++)
	{
		fprintf(out, "%s%2d %-16s%s", ((t % 3) == 0) ? "                              " : "", t, s_rldMkTemplates[t].name, ((t % 3) == 2) ? "\n" : "");
	}
	fprintf(out, "  --class <style>             the driving style: like (the template's, default),\n");
	fprintf(out, "                              balanced, acceleration, speed, turning (or 0..3)\n");
	fprintf(out, "  --size <50..200>            the driver and steering wheel in percent of the\n");
	fprintf(out, "                              model after fitting, or as exported with\n");
	fprintf(out, "                              --fit none (default 100), scaled about the\n");
	fprintf(out, "                              seat of the reference kart (--wheels off: the\n");
	fprintf(out, "                              bottom of the driver; --fit none: the hip);\n");
	fprintf(out, "                              the kart keeps its size: %s.\n", RLDMK_SIZE_TEXT);
	fprintf(out, "                              rldpack says which range the model takes\n");
	fprintf(out, "  --icon <png>                the portrait in the menu: cut to 43:25 in the\n");
	fprintf(out, "                              middle, scaled to the 43 x 25 the menu shows\n");
	fprintf(out, "                              (of 44 x 26 in the file; a picture of exactly\n");
	fprintf(out, "                              44 x 26 is taken whole), 15 colors and\n");
	fprintf(out, "                              transparent. Without it the game shows the\n");
	fprintf(out, "                              template's portrait\n");
	fprintf(out, "  --icon-preview <prefix>     also write <prefix>-original.bmp and\n");
	fprintf(out, "                              <prefix>-icon.bmp (the 43 x 25 the menu shows),\n");
	fprintf(out, "                              and <prefix>-retail.bmp (the template's portrait,\n");
	fprintf(out, "                              read from the game's data when it is found)\n");
	fprintf(out, "  --icon-background <how>     alpha (default): the PNG's transparency only;\n");
	fprintf(out, "                              corners: also the plain color around the\n");
	fprintf(out, "                              subject, filled from the four corners - the\n");
	fprintf(out, "                              same color inside the subject, or closed off by\n");
	fprintf(out, "                              its outline, stays; auto: corners for a PNG\n");
	fprintf(out, "                              without any transparent pixel\n");
	fprintf(out, "  --icon-tolerance <0..128>   how far a color may be from the background\n");
	fprintf(out, "                              and still count as it, per channel (default 24)\n");
	fprintf(out, "  --icon-fit <how>            none (default): the whole picture cut to 43:25;\n");
	fprintf(out, "                              fit: the subject whole, as large as a retail\n");
	fprintf(out, "                              head (41 x 25, standing on the bottom row);\n");
	fprintf(out, "                              fill: the subject fills that box, cut at the\n");
	fprintf(out, "                              sides, or around the face. Never stretched\n");
	fprintf(out, "  --icon-frame <how>          none (default) or retail: the retail frame and\n");
	fprintf(out, "                              the dark half-transparent box behind the subject\n");
	fprintf(out, "  --voices <folder>           the driver's voice (chunk CVOI): .wav or .vag\n");
	fprintf(out, "                              files named boost1..4 hit1..4 spin1..4\n");
	fprintf(out, "                              bigair1..4 drop1..4 shield1..4 passing1..4\n");
	fprintf(out, "                              fire1..4 (voice lines, cut after 3.5 s) and\n");
	fprintf(out, "                              yes, hit (short sounds, cut after 1.0 s); made\n");
	fprintf(out, "                              mono at 22050 Hz. An event without a sound is\n");
	fprintf(out, "                              silent; without --voices the driver is silent\n");
	fprintf(out, "  --voice <file>=<event>      put one file of the folder into an event, or\n");
	fprintf(out, "                              take it out with none (up to %u times):\n", RLDMK_VOICE_ASSIGN_MAX);
	fprintf(out, "                              boost, hit, spin, bigair, drop, shield,\n");
	fprintf(out, "                              passing, fire, short-yes, short-hit; at most %d\n", RLDCHAR_VOICE_PER_EVENT);
	fprintf(out, "                              per event, in the order of their names; split\n");
	fprintf(out, "                              at the last =, so a file name may hold one\n");
	fprintf(out, "  --voice-normalize           set the peak of every sound to -1 dBFS;\n");
	fprintf(out, "                              without it the samples stay as given\n");
	fprintf(out, "  --voice-preview <prefix>    also write every readable file of the folder\n");
	fprintf(out, "                              as the game plays it: <prefix>-voice-<n>.wav,\n");
	fprintf(out, "                              n counting the folder's files from 1 by name\n");
	fprintf(out, "  --preview <file>            also write the built model as the game draws it,\n");
	fprintf(out, "                              3 poses (for Reload Studio's 3D view)\n");
	fprintf(out, "  --author <text>             your name or handle, 64 bytes at most\n");
	fprintf(out, "  --char-version <n>          counts your own revisions (default 1)\n");
	fprintf(out, "  --poses auto|still          auto: the driver leans when steering (default);\n");
	fprintf(out, "                              still: all 47 frames the same\n");
	fprintf(out, "  --colors 64|128             the palette (default 64)\n");
	fprintf(out, "  --up y|z                    the up axis of the model (default y); z for\n");
	fprintf(out, "                              Blender's own axes, with the front at -Y\n");
	fprintf(out, "  --forward z|-z              -z: the model looks backwards and is turned\n");
	fprintf(out, "  --fit crash|none            crash (default): scale the model to Crash's\n");
	fprintf(out, "                              size - its kart as long as Crash with his kart\n");
	fprintf(out, "                              (%.1f game units; the whole model with\n", RLDMK_CRASH_LENGTH);
	fprintf(out, "                              --wheels off or when no kart is found), at\n");
	fprintf(out, "                              most as tall as the tallest driver\n");
	fprintf(out, "                              (%.1f), the factor rounded to 4 significant\n", RLDMK_RETAIL_HEIGHT_MAX);
	fprintf(out, "                              digits; none: as exported, in meters\n");
	fprintf(out, "  --wheels on|off             on (default): the game draws its kart wheels at\n");
	fprintf(out, "                              the points of a retail kart; off: no wheels -\n");
	fprintf(out, "                              for a model with its own (a motorbike, say);\n");
	fprintf(out, "                              --fit then measures the whole model\n");
	fprintf(out, "  --mask aku|uka|like         the mask the driver wears: the mask item, the\n");
	fprintf(out, "                              rescue after a fall, its sound and music and\n");
	fprintf(out, "                              the HUD icon; like (default): the template's\n");
	fprintf(out, "                              (Fake Crash: Uka Uka); aku: Aku Aku; uka: Uka Uka\n");
	fprintf(out, "  --map-color RRGGBB          the color of the driver's marker on the minimap\n");
	fprintf(out, "                              (hex, as FF8000); default the template's\n");
	fprintf(out, "  --mask-model <model>        an own mask drawn in place of Aku Aku / Uka Uka\n");
	fprintf(out, "                              (--mask still decides which of the two it is:\n");
	fprintf(out, "                              sound, music, beam): a PLY or OBJ as --model,\n");
	fprintf(out, "                              fitted to the retail mask's height (%.1f),\n", RLDMK_MASK_HEIGHT);
	fprintf(out, "                              reduced to at most %u triangles\n", (u32)RLDMK_MASK_TRIANGLES_MAX);
	fprintf(out, "  --mask-size <50..150>       the own mask's height in percent of the retail\n");
	fprintf(out, "                              mask's (default 100)\n");
	fprintf(out, "  --mask-up y|z               the up axis of the mask's model, as --up\n");
	fprintf(out, "  --mask-forward z|-z         the front of the mask's model, as --forward\n");
	fprintf(out, "  --reduce auto|off           auto (default): a model of more than %u\n", (u32)RLDMK_REDUCE_LIMIT);
	fprintf(out, "                              triangles is reduced to at most %u, one budget\n", RLDMK_REDUCE_TARGET);
	fprintf(out, "                              shared by kart, driver and steering wheel, each\n");
	fprintf(out, "                              with a floor; off: such a model is refused\n");
	fprintf(out, "                              (model-tris). At or below the limit nothing is\n");
	fprintf(out, "                              reduced. Reload Studio: Reduce to fit. The limit\n");
	fprintf(out, "                              counts the triangles after repair; packing may\n");
	fprintf(out, "                              then drop a few without area (model-collapse)\n");
	fprintf(out, "  --repair auto|off           auto (default): weld split corners, drop\n");
	fprintf(out, "                              triangles without area and doubles, split\n");
	fprintf(out, "                              triangles at cracks (T-junctions), turn faces\n");
	fprintf(out, "                              outward, close holes of up to %u edges; off: the\n", RLDREP_HOLE_MAX_EDGES_DEFAULT);
	fprintf(out, "                              surface as exported\n");
	fprintf(out, "  --open-parts two-sided|one-sided\n");
	fprintf(out, "                              two-sided (default): parts still open after the\n");
	fprintf(out, "                              repair (with --repair off: open as exported) are\n");
	fprintf(out, "                              drawn from both sides (no extra triangles);\n");
	fprintf(out, "                              one-sided: culled like the rest\n");
	fprintf(out, "  --remesh on|off             on: replace kart, driver and steering wheel\n");
	fprintf(out, "                              (each of more than %u triangles) by closed\n", RLDMK_REDUCE_KEEP);
	fprintf(out, "                              hulls of their surface, then reduce them -\n");
	fprintf(out, "                              for a model with holes or loose sheets; off\n");
	fprintf(out, "                              (default): the surface as modeled\n");
	fprintf(out, "  --remesh-resolution <n>     cells along a part's longest side for\n");
	fprintf(out, "                              --remesh, %u..%u (default %u)\n", RLDMK_REMESH_RESOLUTION_MIN, RLDMK_REMESH_RESOLUTION_MAX,
	        RLDMK_REMESH_RESOLUTION);
	fprintf(out, "  --scale <f>                 multiply the model (default 1), only with\n");
	fprintf(out, "                              --fit none\n");
	fprintf(out, "  --two-sided                 draw both sides of every triangle\n");
	fprintf(out, "  --out <file>                somewhere else than next to the model\n");
	fprintf(out, "  --check                     run every check and build in memory, write\n");
	fprintf(out, "                              nothing (the previews are written)\n");
	fprintf(out, "  --diagnose <file>           write the model as every step saw it (text,\n");
	fprintf(out, "                              RLDDIAG 1) and never a character: the limits of\n");
	fprintf(out, "                              draw memory and file size are lifted for the\n");
	fprintf(out, "                              measurement; --preview is written, --out ignored\n");
	fprintf(out, "\n");
	fprintf(out, "  rldpack make-char --model driver.ply --name \"MY DRIVER\" --template 14\n");
	fprintf(out, "  rldpack make-char --model driver.obj --name \"MY DRIVER\" --template 14\n");
	fprintf(out, "      --class speed --size 110 --icon portrait.png --voices voices\n");
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

#define RLDMK_TEST_VERTICES 32768
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

// The driver split of RldMk_TestStandard whose model - kart 12, steering
// wheel 12 and driver 12 x n x n triangles - is the first over
// RLDMK_REDUCE_LIMIT (or `limit`); *trianglesOut its triangles.
static u32 RldMk_TestSplitOver(u32 limit, u32 *trianglesOut)
{
	u32 n = 1;

	while ((24u + 12u * n * n) <= limit)
	{
		n++;
	}
	*trianglesOut = 24u + 12u * n * n;
	return n;
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
	opt->sizePercent = 100u;
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
	RldMk_Test(RldMk_TestPlyText("ply\nformat ascii 1.0\nelement vertex 1000001\nproperty float x\nproperty float y\nproperty float z\n"
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
	RldMk_Test(ok && RldMk_Said("ply-ngon-split") && !RldMk_Said("ply-ngon"), "five corners split into triangles (ply-ngon-split)", "said: %s",
	           s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// Five corners on one line: no triangle to cut, refused.
	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	{
		const u32 v = RldMk_TestVertex(mesh, 0.0, 0.5, -0.7, 50u);

		RldMk_TestVertex(mesh, 0.1, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.2, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.3, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.4, 0.5, -0.7, 50u);
		RldMk_TestFace(mesh, 5u, v, v + 1u, v + 2u, v + 3u, v + 4u);
	}
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && RldMk_Said("ply-ngon"), "five corners on a line refused (ply-ngon)", "said: %s", s_rldMkCodes);
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

	// Too many triangles for the draw memory (the first split over the limit),
	// with --reduce off (RldMk_SelfTestReduce has the same model with auto).
	{
		u32 over = 0;

		RldMk_TestStandard(mesh, RldMk_TestSplitOver(RLDMK_REDUCE_LIMIT, &over), 0, 0.0, -1);
		opt.reduce = 0;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(!ok && RldMk_Said("model-tris") && (b.triangleCount == over), "more draw memory than allowed refused (--reduce off)",
		           "%u triangles, said: %s", b.triangleCount, s_rldMkCodes);
	}
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

	// stage 4: RLDMK_REDUCE_LIMIT untextured triangles are exactly
	// RLDCHAR_DRAW_BYTES_MAX, one more is 28 bytes over (a strip: 2 records more
	// than triangles).
	m = RldMk_TestStrip(RLDMK_REDUCE_LIMIT + 2u, RLDMK_REDUCE_LIMIT + 2u, &size);
	RldMk_TestRule("good-drawbytes-max (the limit of triangles)", m, size, NULL, RLDCHAR_VERDICT_OK);
	free(m);

	m = RldMk_TestStrip(RLDMK_REDUCE_LIMIT + 3u, RLDMK_REDUCE_LIMIT + 3u, &size);
	RldMk_TestRule("bad-drawbytes (one triangle over the limit)", m, size, "model-draw", RLDCHAR_VERDICT_MODEL_LIMIT);
	free(m);
}

// CHRI flags (--wheels off): written only when set, read forward compatibly.
// The mask (bits 1-2) has its own test, RldMk_SelfTestChriMask.
static void RldMk_SelfTestChriFlags(const u8 field[RLDCHAR_NAME_FIELD], const u8 *good, size_t goodSize)
{
	struct RldCharInfo info;
	const char *why;
	size_t size = 0;
	size_t sizeAgain = 0;
	u8 *plain;
	u8 *flagged;
	u8 *again;
	u8 *m;

	// Without a flag: fixedSize 0x1C as always, flags 0 - although the word at
	// 0x1C (the string count) is not 0.
	plain = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", 0u, &size);
	why = (plain != NULL) ? RldChar_ParseInfo(&info, plain, size) : "out of memory";
	RldMk_Test((why == NULL) && (Rld_ReadLE16(&plain[0x00]) == RLDCHAR_INFO_FIXED_SIZE) && (Rld_ReadLE32(&plain[0x1C]) == 1u) && (info.flags == 0u) &&
	               (RldChar_Mask(info.flags) == RLDCHAR_MASK_LIKE),
	           "CHRI without flags: fixedSize 0x1C, flags 0 (the string count at 0x1C is no flag), mask like the template", "%s",
	           (why != NULL) ? why : "a field differs");
	free(plain);

	// No wheels: fixedSize 0x20, everything else read as before, twice the same,
	// and the container with it checks.
	flagged = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", RLDCHAR_FLAG_NO_WHEELS, &size);
	again = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", RLDCHAR_FLAG_NO_WHEELS, &sizeAgain);
	why = (flagged != NULL) ? RldChar_ParseInfo(&info, flagged, size) : "out of memory";
	RldMk_Test((why == NULL) && (again != NULL) && (size == sizeAgain) && (memcmp(flagged, again, size) == 0) &&
	               (Rld_ReadLE16(&flagged[0x00]) == RLDCHAR_CHRI_SIZE_FLAGS) && (info.flags == RLDCHAR_FLAG_NO_WHEELS) && (info.templateId == 14u) &&
	               (info.classId == 0u) && (strcmp(info.name, "SELF TEST") == 0) && (info.charVersion == 3u) && (strcmp(info.author, "self-test") == 0),
	           "CHRI with no wheels: fixedSize 0x20, flag and fields there and back, twice the same", "%s", (why != NULL) ? why : "a field differs");
	if ((flagged != NULL) && (good != NULL))
	{
		size_t fileSize = 0;
		u8 *file = RldMk_Container(flagged, size, good, goodSize, NULL, &fileSize);
		int ok = (file != NULL);

		if (ok)
		{
			const u64 dirAt = Rld_ReadLE64(&file[0x18]);
			const u8 *entry = &file[(size_t)dirAt + RLD_DIR_ENTRY_SIZE];

			ok = (RldChar_CheckModel(&file[(size_t)Rld_ReadLE64(&entry[0x08])], (size_t)Rld_ReadLE64(&entry[0x10]), NULL, NULL, NULL, NULL) ==
			      RLDCHAR_VERDICT_OK);
		}
		RldMk_Test(ok, "container with no wheels: the model checks", "not as written");
		free(file);
	}
	free(again);

	// Unknown bits are kept and ignored, never refused; 0x10 alone is no flag
	// this reader knows.
	m = RldMk_TestCopy(flagged, size);
	if (m != NULL)
	{
		Rld_WriteLE32(&m[RLDCHAR_CHRI_FLAGS_OFFSET], 0x80000001u);
		why = RldChar_ParseInfo(&info, m, size);
		RldMk_Test((why == NULL) && (info.flags == 0x80000001u) && ((info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0u), "CHRI flags 0x80000001: accepted, no wheels",
		           "%s", (why != NULL) ? why : "flags differ");
		Rld_WriteLE32(&m[RLDCHAR_CHRI_FLAGS_OFFSET], 0x10u);
		why = RldChar_ParseInfo(&info, m, size);
		RldMk_Test((why == NULL) && (info.flags == 0x10u) && ((info.flags & RLDCHAR_FLAG_NO_WHEELS) == 0u) && (RldChar_Mask(info.flags) == RLDCHAR_MASK_LIKE),
		           "CHRI flags 0x10 alone: accepted, wheels shown, mask like the template", "%s", (why != NULL) ? why : "flags differ");

		// fixedSize 0x22: not a multiple of 4.
		Rld_WriteLE32(&m[RLDCHAR_CHRI_FLAGS_OFFSET], RLDCHAR_FLAG_NO_WHEELS);
		Rld_WriteLE16(&m[0x00], 0x22u);
		why = RldChar_ParseInfo(&info, m, size);
		RldMk_Test((why != NULL) && (strncmp(why, "CHRI-1", 6) == 0), "CHRI fixedSize 0x22 refused (CHRI-1)", "got %s", (why != NULL) ? why : "accepted");

		// fixedSize 0x20, but the chunk ends before the string count.
		Rld_WriteLE16(&m[0x00], RLDCHAR_CHRI_SIZE_FLAGS);
		why = RldChar_ParseInfo(&info, m, RLDCHAR_CHRI_SIZE_FLAGS + 2u);
		RldMk_Test((why != NULL) && (strncmp(why, "CHRI-1", 6) == 0), "CHRI fixedSize 0x20 without room for the string count refused (CHRI-1)", "got %s",
		           (why != NULL) ? why : "accepted");
		free(m);
	}

	// fixedSize 0x24: the flags and 4 bytes of a later field, then the author.
	if (flagged != NULL)
	{
		const size_t longer = size + 4u;

		m = (u8 *)calloc(1, longer);
		if (m != NULL)
		{
			memcpy(m, flagged, RLDCHAR_CHRI_SIZE_FLAGS);
			memcpy(&m[RLDCHAR_CHRI_SIZE_FLAGS + 4u], &flagged[RLDCHAR_CHRI_SIZE_FLAGS], size - RLDCHAR_CHRI_SIZE_FLAGS);
			Rld_WriteLE16(&m[0x00], 0x24u);
			why = RldChar_ParseInfo(&info, m, longer);
			RldMk_Test((why == NULL) && (info.flags == RLDCHAR_FLAG_NO_WHEELS) && (strcmp(info.author, "self-test") == 0),
			           "CHRI fixedSize 0x24 (a later field): accepted, flags and author read", "%s", (why != NULL) ? why : "a field differs");
			free(m);
		}
	}
	free(flagged);
}

// --mask: CHRI flags bits 1-2, written only when chosen; the reserved value 3
// reads as like the template, never refused.
static void RldMk_SelfTestChriMask(const u8 field[RLDCHAR_NAME_FIELD])
{
	static const struct
	{
		const char *word;
		int wheelsOff;
		u32 mask;
		u32 flags;
		const char *name;
	} cases[3] = {
		{"aku", 0, RLDCHAR_MASK_AKU, 0x2u, "CHRI with --mask aku: flags 0x2, fixedSize 0x20, Aku Aku there and back, twice the same"},
		{"uka", 0, RLDCHAR_MASK_UKA, 0x4u, "CHRI with --mask uka: flags 0x4, fixedSize 0x20, Uka Uka there and back, twice the same"},
		{"uka", 1, RLDCHAR_MASK_UKA, 0x5u, "CHRI with --wheels off --mask uka: flags 0x5, both there and back, twice the same"},
	};
	static const char *const words[4] = {"AKU", "Uka", "like", "maybe"};
	static const u32 wordMasks[4] = {RLDCHAR_MASK_AKU, RLDCHAR_MASK_UKA, RLDCHAR_MASK_LIKE, RLDCHAR_MASK_LIKE};
	struct RldMkOptions opt;
	struct RldCharInfo info;
	const char *why;
	size_t size = 0;
	size_t sizeAgain = 0;
	u32 wearsAku = 0;
	u32 t;
	int k;

	for (k = 0; k < 3; k++)
	{
		u8 *chri;
		u8 *again;
		u32 flags;

		RldMk_DefaultOptions(&opt);
		opt.noWheels = cases[k].wheelsOff;
		opt.mask = cases[k].mask;
		flags = RldMk_ChriFlags(&opt);
		chri = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", flags, &size);
		again = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", flags, &sizeAgain);
		why = (chri != NULL) ? RldChar_ParseInfo(&info, chri, size) : "out of memory";
		RldMk_Test((why == NULL) && (flags == cases[k].flags) && (again != NULL) && (size == sizeAgain) && (memcmp(chri, again, size) == 0) &&
		               (Rld_ReadLE16(&chri[0x00]) == RLDCHAR_CHRI_SIZE_FLAGS) && (info.flags == cases[k].flags) &&
		               (RldChar_Mask(info.flags) == cases[k].mask) && (((info.flags & RLDCHAR_FLAG_NO_WHEELS) != 0u) == (cases[k].wheelsOff != 0)) &&
		               (strcmp(RldMk_MaskWord(RldChar_Mask(info.flags), info.templateId), cases[k].word) == 0),
		           cases[k].name, "%s, flags 0x%x", (why != NULL) ? why : "a field differs", flags);
		free(chri);
		free(again);
	}

	// The reserved value 3 (raw 0x6): read without a finding, the mask like
	// the template - Uka Uka on Fake Crash.
	{
		u8 *chri = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", 0x6u, &size);

		why = (chri != NULL) ? RldChar_ParseInfo(&info, chri, size) : "out of memory";
		RldMk_Test((why == NULL) && (info.flags == 0x6u) && (RldChar_Mask(info.flags) == RLDCHAR_MASK_LIKE) &&
		               ((info.flags & ~(u32)RLDCHAR_FLAGS_KNOWN) == 0u) && (strcmp(RldMk_MaskWord(RldChar_Mask(info.flags), info.templateId), "uka") == 0),
		           "CHRI flags 0x6 (mask 3, reserved): accepted, read as like the template", "%s", (why != NULL) ? why : "a field differs");
		free(chri);
	}

	// The templates that wear Aku Aku: Crash, Coco, Polar, Pura, Penta - and no
	// 15 (N. Oxide is never a template).
	for (t = 0u; t <= 15u; t++)
	{
		if (RLDCHAR_TEMPLATE_WEARS_AKU(t))
		{
			wearsAku |= 1u << t;
		}
	}
	RldMk_Test(wearsAku == ((1u << 0) | (1u << 3) | (1u << 6) | (1u << 7) | (1u << 13)), "templates wearing Aku Aku: 0, 3, 6, 7, 13 (none at 15)",
	           "bits 0x%x", wearsAku);
	RldMk_Test((strcmp(RldMk_MaskWord(RLDCHAR_MASK_LIKE, 14u), "uka") == 0) && (strcmp(RldMk_MaskWord(RLDCHAR_MASK_LIKE, 13u), "aku") == 0) &&
	               (strcmp(RldMk_MaskWord(RLDCHAR_MASK_UKA, 0u), "uka") == 0) && (strcmp(RldMk_MaskWord(RLDCHAR_MASK_LIKE, RLDMK_NONE), "none") == 0),
	           "mask words: like on Fake Crash uka, like on Penta aku, uka on Crash uka, without a template none", "%s",
	           RldMk_MaskWord(RLDCHAR_MASK_LIKE, 14u));

	// The switch in any case; anything else is said (mask) and stays like.
	for (k = 0; k < 4; k++)
	{
		u32 mask = 0xFFu;
		int ok;
		char name[64];

		RldMk_ResetSaid();
		ok = RldMk_MaskSwitch(words[k], &mask);
		snprintf(name, sizeof(name), "--mask %s: %s", words[k], (k < 3) ? "taken" : "refused (mask)");
		RldMk_Test((ok == (k < 3)) && (mask == wordMasks[k]) && ((k < 3) ? (s_rldMkCodes[0] == '\0') : RldMk_Said("mask")), name, "said: %s, mask %u",
		           s_rldMkCodes, mask);
	}
	RldMk_ResetSaid();
}

// --map-color: CHRI flag bit 3 and the color at 0x20 (fixedSize 0x24), written
// only when chosen; bit 3 without the field and the field without bit 3 read
// as the template's color, without a finding.
static void RldMk_SelfTestMapColor(const u8 field[RLDCHAR_NAME_FIELD])
{
	static const char *const words[5] = {"FF8000", "#00ff7f", "FF80", "GG0000", "FF80000"};
	static const u32 colors[5] = {0x000080FFu, 0x007FFF00u, 0u, 0u, 0u};
	struct RldMkOptions opt;
	struct RldCharInfo info;
	const char *why;
	size_t size = 0;
	size_t sizeAgain = 0;
	u32 color = 0;
	u8 *chri;
	u8 *again;
	char text[8];
	int k;

	for (k = 0; k < 5; k++)
	{
		char name[64];
		int ok;

		RldMk_ResetSaid();
		color = 0xFFFFFFFFu;
		ok = RldMk_MapColorSwitch(words[k], &color);
		snprintf(name, sizeof(name), "--map-color %s: %s", words[k], (k < 2) ? "taken" : "refused (map-color)");
		RldMk_Test((k < 2) ? (ok && (color == colors[k])) : (!ok && RldMk_Said("map-color")), name, "said: %s, color 0x%06x", s_rldMkCodes, color);
	}
	RldMk_ResetSaid();
	RldMk_MapColorText(0x000080FFu, text);
	RldMk_Test(strcmp(text, "FF8000") == 0, "map color 0x000080FF written as FF8000", "%s", text);

	// --wheels off --mask uka --map-color FF8000: flags 0xD, fixedSize 0x24,
	// the bytes FF 80 00 00 at 0x20, read back, twice the same.
	RldMk_DefaultOptions(&opt);
	opt.noWheels = 1;
	opt.mask = RLDCHAR_MASK_UKA;
	opt.hasMapColor = 1;
	opt.mapColor = 0x000080FFu;
	chri = RldMk_BuildChriColor(14u, 0u, field, 3u, "self-test", RldMk_ChriFlags(&opt), opt.mapColor, &size);
	again = RldMk_BuildChriColor(14u, 0u, field, 3u, "self-test", RldMk_ChriFlags(&opt), opt.mapColor, &sizeAgain);
	why = (chri != NULL) ? RldChar_ParseInfo(&info, chri, size) : "out of memory";
	color = 0;
	RldMk_Test((why == NULL) && (again != NULL) && (size == sizeAgain) && (memcmp(chri, again, size) == 0) &&
	               (Rld_ReadLE16(&chri[0x00]) == RLDCHAR_CHRI_SIZE_MAP_COLOR) && (info.flags == 0xDu) && (chri[0x20] == 0xFFu) && (chri[0x21] == 0x80u) &&
	               (chri[0x22] == 0x00u) && (chri[0x23] == 0x00u) && RldChar_MapColor(&info, &color) && (color == 0x000080FFu) &&
	               (RldChar_Mask(info.flags) == RLDCHAR_MASK_UKA) && (strcmp(info.author, "self-test") == 0),
	           "CHRI with --wheels off --mask uka --map-color FF8000: flags 0xD, fixedSize 0x24, color and fields there and back", "%s",
	           (why != NULL) ? why : "a field differs");
	free(again);

	// The field without bit 3: the template's color.
	if (chri != NULL)
	{
		Rld_WriteLE32(&chri[RLDCHAR_CHRI_FLAGS_OFFSET], RLDCHAR_FLAG_NO_WHEELS);
		why = RldChar_ParseInfo(&info, chri, size);
		color = 0x123456u;
		RldMk_Test((why == NULL) && !RldChar_MapColor(&info, &color) && (color == 0x123456u), "CHRI color without bit 3: accepted, the template's color",
		           "%s", (why != NULL) ? why : "a color was read");
	}
	free(chri);

	// Bit 3 through RldMk_BuildChri (no color passed): fixedSize 0x24, the color 0.
	chri = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", RLDCHAR_FLAG_MAP_COLOR, &size);
	why = (chri != NULL) ? RldChar_ParseInfo(&info, chri, size) : "out of memory";
	color = 0x123456u;
	RldMk_Test((why == NULL) && (Rld_ReadLE16(&chri[0x00]) == RLDCHAR_CHRI_SIZE_MAP_COLOR) && RldChar_MapColor(&info, &color) && (color == 0u),
	           "CHRI bit 3 through BuildChri: fixedSize 0x24 with color 0", "%s", (why != NULL) ? why : "a field differs");
	if (chri != NULL)
	{
		u8 *m = (u8 *)calloc(1, size);

		if (m != NULL)
		{
			// Bit 3 without the field: the same CHRI cut to fixedSize 0x20 reads
			// as the template's color.
			memcpy(m, chri, RLDCHAR_CHRI_SIZE_FLAGS);
			memcpy(&m[RLDCHAR_CHRI_SIZE_FLAGS], &chri[RLDCHAR_CHRI_SIZE_MAP_COLOR], size - RLDCHAR_CHRI_SIZE_MAP_COLOR);
			Rld_WriteLE16(&m[0x00], RLDCHAR_CHRI_SIZE_FLAGS);
			why = RldChar_ParseInfo(&info, m, size - 4u);
			color = 0x123456u;
			RldMk_Test((why == NULL) && (info.flags == RLDCHAR_FLAG_MAP_COLOR) && !RldChar_MapColor(&info, &color) && (color == 0x123456u),
			           "CHRI bit 3 without the field (fixedSize 0x20): accepted, the template's color", "%s", (why != NULL) ? why : "a color was read");
			free(m);
		}
	}
	free(chri);
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
	chri = RldMk_BuildChri(14u, 0u, field, 3u, "self-test", 0u, &chriSize);
	why = (chri != NULL) ? RldChar_ParseInfo(&info, chri, chriSize) : "out of memory";
	RldMk_Test((why == NULL) && (info.templateId == 14u) && (info.classId == 0u) && (strcmp(info.name, "SELF TEST") == 0) && (info.charVersion == 3u) &&
	               (strcmp(info.author, "self-test") == 0),
	           "CHRI there and back", "%s", (why != NULL) ? why : "a field differs");

	// The container: header, directory, hashes, and the model still checks.
	if ((chri != NULL) && (good != NULL))
	{
		size_t size = 0;
		size_t sizeAgain = 0;
		u8 *file = RldMk_Container(chri, chriSize, good, goodSize, NULL, &size);
		u8 *again = RldMk_Container(chri, chriSize, good, goodSize, NULL, &sizeAgain);
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
			m = RldMk_BuildChri(14u, 0u, field, 1u, author, 0u, &size);
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

	RldMk_SelfTestChriFlags(field, good, goodSize);
	RldMk_SelfTestChriMask(field);
	RldMk_SelfTestMapColor(field);

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

//========================================================================================
// SELF-TEST: --size, --preview, the PNG reader, --icon, --voices, golden hashes
//========================================================================================

// GOLDEN HASHES: the SHA-256 of whole .rldchar files built from fixed inputs -
// the standard test model (RldMk_TestStandard with a driver split 2, written as
// ascii PLY) with CHRI template 14, class 0, name "SELF TEST", version 1 and no
// author. These are the bytes of
//   rldpack make-char --model <that PLY> --name "SELF TEST" --template 14
// PLAIN with --fit none - the file make-char wrote without switches before
// --size, --icon, --preview and --fit existed. SIZE_ICON with --fit none,
// --size 120 and test picture A (RldMk_TestPixelA) as --icon. FIT without
// further switches (--fit crash, the factor 0.9757). A different value of
// these three means characters are no longer written the same way - never
// update them to make the test pass.
// NO_WHEELS with --fit none and --wheels off: the CMDL of PLAIN, the CHRI with
// fixedSize 0x20 and the flag RLDCHAR_FLAG_NO_WHEELS - frozen like those.
// SIZE_ICON last changed on 2026-10-02 with the fit of the portrait to the
// 43 x 25 the menu shows (RldMk_MakeIcon): only its CICN changed, CHRI and CMDL
// stayed. A deliberate change of the portrait conversion updates it the same
// way; nothing else may.
// REDUCED from the same model with the first driver split over the limit
// (RldMk_TestSplitOver: 25, 7524 triangles at 7200) and --fit none: --reduce
// auto brings it to at most RLDMK_REDUCE_TARGET. Unlike the three above it
// follows deliberate changes of the reducer (tools/rldpack_reduce.inc) and of
// the limit (RLDCHAR_DRAW_BYTES_MAX): such a change updates it, on purpose and
// in the same change; nothing else may. Last changed on 2026-10-02 with the
// limit 805 -> 7200 triangles (it was split 9, 996 triangles -> 797).
#define RLDMK_GOLDEN_PLAIN "ba7e3208bc98b2af5460110ae6d7d64afe888771b76747a74ae30f3190ce3186"
#define RLDMK_GOLDEN_SIZE_ICON "61960b1c58d8a38e9728d891799fad7d53f8682864c44ede49f9e61f42e536d7"
#define RLDMK_GOLDEN_FIT "73695623084cc66c55b389df5f2db169ffc1066431695a6d05558ad0a1102231"
#define RLDMK_GOLDEN_NO_WHEELS "0427aee8b259b0f0076efc6f8cba195fff3b105c8b7216992056bb4ee333b0fe"
#define RLDMK_GOLDEN_REDUCED "b602e356987fc179466d0bffa3481470da83e26815e40727bd82b3dfb7d3abe2"

// --- --size --------------------------------------------------------------------------

// The highest decoded Y of driver and steering wheel and the kart's decoded
// box, neutral frame, game units.
static void RldMk_TestMeasure(const struct RldMkBuild *b, double *top, double kartLo[3], double kartHi[3])
{
	u32 r;
	int a;

	*top = -1.0e30;
	for (a = 0; a < 3; a++)
	{
		kartLo[a] = 1.0e30;
		kartHi[a] = -1.0e30;
	}

	for (r = 0; r < b->recordCount; r++)
	{
		double p[3];

		RldMk_Decoded(b, RLDMK_NEUTRAL_FRAME, b->record[r], p);
		if (b->positionGroup[b->record[r]] == RLDMK_GROUP_KART)
		{
			for (a = 0; a < 3; a++)
			{
				kartLo[a] = (p[a] < kartLo[a]) ? p[a] : kartLo[a];
				kartHi[a] = (p[a] > kartHi[a]) ? p[a] : kartHi[a];
			}
		}
		else if (p[1] > *top)
		{
			*top = p[1];
		}
	}
}

static void RldMk_SelfTestSize(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkPly plyAgain;
	struct RldMkBuild b;
	struct RldMkBuild again;
	int ok;
	int same;

	memset(&plyAgain, 0, sizeof(plyAgain));
	memset(&again, 0, sizeof(again));
	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);

	// 100 and 0 are the model as exported.
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	opt.sizePercent = 0u;
	same = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
	RldMk_Test(same && (b.sizeLow == 50u) && (b.sizeHigh == 200u), "--size 100: the model as exported, range 50..200", "range %u..%u, same %d",
	           b.sizeLow, b.sizeHigh, same);
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);

	// 120: the kart stays, the driver grows about the hip.
	opt.sizePercent = 120u;
	if (ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again))
	{
		double top100;
		double top120;
		double lo100[3];
		double hi100[3];
		double lo120[3];
		double hi120[3];
		int kartSame = 1;
		int a;

		RldMk_TestMeasure(&b, &top100, lo100, hi100);
		RldMk_TestMeasure(&again, &top120, lo120, hi120);
		for (a = 0; a < 3; a++)
		{
			const double step = (double)again.scale[a] / 4096.0 * ((a == 1) ? 2.0 : 1.0) + (double)b.scale[a] / 4096.0 * ((a == 1) ? 2.0 : 1.0);

			if (((lo120[a] - lo100[a]) > step) || ((lo100[a] - lo120[a]) > step) || ((hi120[a] - hi100[a]) > step) || ((hi100[a] - hi120[a]) > step))
			{
				kartSame = 0;
			}
		}
		top100 = RLDMK_HIP_Y + 1.2 * (top100 - RLDMK_HIP_Y);
		RldMk_Test(kartSame && ((top120 - top100) < 1.0) && ((top100 - top120) < 1.0), "--size 120: the kart stays, the driver grows about the hip",
		           "kart same %d, driver top %.2f, expected %.2f", kartSame, top120, top100);
	}
	else
	{
		RldMk_Test(0, "--size 120: the kart stays, the driver grows about the hip", "not built, said: %s", s_rldMkCodes);
	}
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// A driver 4.8 long: at 188 % the model would be longer than 9 Blender units.
	{
		const double kartLo[3] = {-0.45, 0.10, -0.90};
		const double kartHi[3] = {0.45, 0.35, 0.90};
		const double driverLo[3] = {-0.25, 0.40, -2.40};
		const double driverHi[3] = {0.25, 1.00, 2.40};
		const double wheelLo[3] = {-0.15, 0.50, 0.30};
		const double wheelHi[3] = {0.15, 0.60, 0.36};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
		RldMk_TestBox(mesh, wheelLo, wheelHi, 1u, 20u, 0, -1);

		opt.sizePercent = 100u;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(ok && (b.sizeLow == 50u) && (b.sizeHigh == 187u), "--size range of a long driver: 50..187", "built %d, range %u..%u", ok, b.sizeLow,
		           b.sizeHigh);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);

		opt.sizePercent = 187u;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(ok && !RldMk_Said("char-size"), "--size 187 of the long driver built", "said: %s", s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);

		opt.sizePercent = 200u;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(!ok && RldMk_Said("char-size"), "--size 200 of the long driver refused (char-size)", "said: %s", s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}
}

// --- --fit ---------------------------------------------------------------------------

// How often `code` was said since the last RldMk_ResetSaid.
static u32 RldMk_TestSaidCount(const char *code)
{
	const size_t length = strlen(code);
	const char *p = s_rldMkCodes;
	u32 count = 0;

	while (*p != '\0')
	{
		if ((strncmp(p, code, length) == 0) && ((p[length] == ' ') || (p[length] == '\0')))
		{
			count++;
		}

		p = strchr(p, ' ');
		if (p == NULL)
		{
			break;
		}
		p++;
	}

	return count;
}

static void RldMk_SelfTestFit(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkPly plyAgain;
	struct RldMkBuild b;
	struct RldMkBuild again;
	double f;
	int fit = -1;
	int ok;
	int same;
	u32 v;

	// THE FACTOR OF THE WHOLE MODEL: the length decides, the tallest driver
	// caps, 4 significant digits (halves up), within the range of --scale.
	f = RldMk_FitFactor(115.2, 57.6);
	RldMk_Test(f == 0.9757, "fit: 112.4 / 115.2 = 0.97569 rounds up to 0.9757", "got %.10f", f);
	f = RldMk_FitFactor(159.2, 131.2);
	RldMk_Test(f == 0.706, "fit: 112.4 / 159.2 = 0.70603 rounds down to 0.7060", "got %.10f", f);
	f = RldMk_FitFactor(100.0, 200.0);
	RldMk_Test(f == 0.7125, "fit: a model 200 tall is capped at 142.5 (0.7125)", "got %.10f", f);
	f = RldMk_FitFactor(56.2, 0.0);
	RldMk_Test(f == 2.0, "fit: a model 56.2 long and flat doubles", "got %.10f", f);
	f = RldMk_FitFactor(0.0, 0.0);
	RldMk_Test(f == 1.0, "fit: no extent at all stays at 1", "got %.10f", f);
	RldMk_Test((RldMk_FitFactor(1.0e-9, 1.0e-9) == RLDMK_FIT_MAX) && (RldMk_FitFactor(1.0e12, 1.0e12) == RLDMK_FIT_MIN),
	           "fit: the factor stays within 0.0001..1000", "got %g and %g", RldMk_FitFactor(1.0e-9, 1.0e-9), RldMk_FitFactor(1.0e12, 1.0e12));
	f = RldMk_FitFactor(11520.0, 5760.0);
	RldMk_Test(f == 0.009757, "fit: exported in centimeters, 4 significant digits (0.009757)", "got %.12f", f);
	RldMk_Test((RldMk_FitRound(12.3456) == 12.35) && (RldMk_FitRound(0.00012346) == 0.0001235) && (RldMk_FitRound(999.96) == 1000.0) &&
	               (RldMk_FitRound(9.99996) == 10.0) && (RldMk_FitRound(0.1) == 0.1) && (RldMk_FitRound(1.0) == 1.0),
	           "fit: 4 significant digits at every size, the double of the printed digits", "got %.12g %.12g %.12g %.12g", RldMk_FitRound(12.3456),
	           RldMk_FitRound(0.00012346), RldMk_FitRound(999.96), RldMk_FitRound(9.99996));

	// THE SWITCHES: crash by default, --scale only with --fit none.
	RldMk_ResetSaid();
	RldMk_Test(RldMk_FitSwitch(NULL, NULL, &fit) && (fit == 1) && (s_rldMkCodes[0] == '\0'), "--fit: crash by default", "fit %d, said: %s", fit,
	           s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(!RldMk_FitSwitch(NULL, "2", &fit) && RldMk_Said("fit-scale"), "--scale without --fit none refused (fit-scale)", "said: %s", s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(!RldMk_FitSwitch("crash", "2", &fit) && RldMk_Said("fit-scale"), "--scale with --fit crash refused (fit-scale)", "said: %s", s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(RldMk_FitSwitch("none", "2", &fit) && (fit == 0) && (s_rldMkCodes[0] == '\0'), "--fit none takes --scale", "fit %d, said: %s", fit,
	           s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(!RldMk_FitSwitch("box", NULL, &fit) && (fit == 1) && RldMk_Said("fit"), "--fit box refused (fit)", "fit %d, said: %s", fit, s_rldMkCodes);
	RldMk_ResetSaid();
	RldMk_Test(RldMk_FitSwitch("NONE", "2", &fit) && (fit == 0) && RldMk_FitSwitch("Crash", NULL, &fit) && (fit == 1) &&
	               !RldMk_FitSwitch("crashes", NULL, &fit) && RldMk_Said("fit"),
	           "--fit in any case; crashes refused", "fit %d, said: %s", fit, s_rldMkCodes);
	RldMk_ResetSaid();

	// THE STANDARD MODEL, 115.2 long and 57.6 tall: fitted by 0.9757, the
	// positions of --fit none --scale 0.9757 moved by one shift onto the dummy
	// (its kart on the dummy kart), the size range unchanged.
	memset(&plyAgain, 0, sizeof(plyAgain));
	memset(&again, 0, sizeof(again));
	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	opt.fit = 1;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && b.fitted && (b.fitFactor == 0.9757) && (b.fitBasis == RLDMK_FIT_KART) && (b.fitBefore[0] > 115.19) && (b.fitBefore[0] < 115.21) && (b.fitBefore[1] > 57.59) &&
	               (b.fitBefore[1] < 57.61) && (b.fitAfter[0] > 112.35) && (b.fitAfter[0] < 112.45) && (b.sizeLow == 50u) && (b.sizeHigh == 200u),
	           "--fit crash: 115.2 -> 112.4 long, factor 0.9757, size range 50..200", "built %d, fitted %d, x%.4f, %.2f -> %.2f long, %.2f tall, range %u..%u",
	           ok, b.fitted, b.fitFactor, b.fitBefore[0], b.fitAfter[0], b.fitBefore[1], b.sizeLow, b.sizeHigh);
	RldMk_Test(ok && (RldMk_TestSaidCount("ply-quads") == 1u), "--fit crash: the measuring pass says nothing (ply-quads once)", "said: %s",
	           s_rldMkCodes);

	opt.fit = 0;
	opt.scale = 0.9757;
	same = ok && b.dummy && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && !again.fitted && !again.dummy &&
	       (again.positionCount == b.positionCount) && (again.drawCount == b.drawCount);
	{
		u32 q;
		int a;

		for (q = 0; same && (q < b.positionCount); q++)
		{
			for (a = 0; a < 3; a++)
			{
				const double d = b.position[3u * q + (u32)a] - (again.position[3u * q + (u32)a] + b.dumFit.shift[a]);

				same = same && (d < 1.0e-9) && (d > -1.0e-9);
			}
		}
	}
	RldMk_Test(same && (b.parts[b.kartPart].lo[1] > 5.59) && (b.parts[b.kartPart].lo[1] < 5.61) &&
	               (0.5 * (b.parts[b.kartPart].lo[0] + b.parts[b.kartPart].hi[0]) < 1.0e-9) && (0.5 * (b.parts[b.kartPart].lo[0] + b.parts[b.kartPart].hi[0]) > -1.0e-9),
	           "--fit crash = --fit none --scale 0.9757 moved onto the dummy: kart bottom 5.6, centered", "differ");
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);

	// --fit none is the model as exported: nothing fitted, the bytes of scale 1.
	opt.scale = 1.0;
	ok = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && !again.fitted && (again.fitFactor == 0.0) &&
	     ((again.cmdlSize != b.cmdlSize) || (memcmp(again.cmdl, b.cmdl, b.cmdlSize) != 0));
	RldMk_Test(ok, "--fit none: nothing fitted, other bytes than --fit crash", "not as expected");
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);

	// --size 120 on the fitted model: 100 % is Crash's size.
	opt.fit = 1;
	opt.sizePercent = 120u;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && again.fitted && (again.fitFactor == 0.9757);
	RldMk_Test(ok, "--fit crash with --size 120 builds", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// TEN TIMES TOO LARGE (exported in decimeters): --fit none stops at
	// model-scale, --fit crash takes it at 0.09757.
	for (v = 0; v < mesh->vertexCount; v++)
	{
		mesh->xyz[v][0] *= 10.0;
		mesh->xyz[v][1] *= 10.0;
		mesh->xyz[v][2] *= 10.0;
	}
	RldMk_TestOptions(&opt);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && RldMk_Said("model-scale"), "--fit none: a model 18 units long refused (model-scale)", "said: %s", s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	opt.fit = 1;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && b.fitted && (b.fitFactor == 0.09757) && !RldMk_Said("model-scale") && !RldMk_Said("ply-axes"),
	           "--fit crash: a model 18 units long fitted by 0.09757", "built %d, x%.4g, said: %s", ok, b.fitFactor, s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// THE KART DECIDES. A tail behind the driver (2.3 long in all): step 4
	// finds the kart at f1, and the kart comes out as long as Crash's.
	{
		const double kartLo[3] = {-0.45, 0.05, -0.90};
		const double kartHi[3] = {0.45, 0.25, 0.90};
		const double driverLo[3] = {-0.30, 0.60, -1.40};
		const double driverHi[3] = {0.30, 1.40, 0.40};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(ok && (b.fitBasis == RLDMK_FIT_KART) && (b.fitFactor == 0.9757) && (b.fitKart[1] > 112.35) && (b.fitKart[1] < 112.45) &&
		               (b.fitAfter[0] > 143.0),
		           "--fit crash with a tail: the kart 115.2 -> 112.4 long (x0.9757)", "built %d, basis %d, x%.4g, kart %.2f, model %.2f, said: %s", ok,
		           b.fitBasis, b.fitFactor, b.fitKart[1], b.fitAfter[0], s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);

		// --wheels off: the whole model, 147.2 -> 112.4 (x0.7636).
		opt.noWheels = 1;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		opt.noWheels = 0;
		RldMk_Test(ok && (b.fitBasis == RLDMK_FIT_MODEL) && (b.fitFactor == 0.7636) && (b.fitAfter[0] > 112.35) && (b.fitAfter[0] < 112.45),
		           "--fit crash with a tail and --wheels off: the whole model 147.2 -> 112.4 long (x0.7636)", "built %d, basis %d, x%.4g, model %.2f, said: %s",
		           ok, b.fitBasis, b.fitFactor, b.fitAfter[0], s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// A driver far longer than his kart (kart 1.4, all 2.4): at f1 the kart is
	// too short for step 4, the fit still finds it (the longest part that is a
	// kart at Crash's length) - x1.254, the kart 112.4 long, and it builds.
	{
		const double kartLo[3] = {-0.45, 0.05, -0.70};
		const double kartHi[3] = {0.45, 0.25, 0.70};
		const double driverLo[3] = {-0.30, 0.60, -1.10};
		const double driverHi[3] = {0.30, 1.40, 1.30};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(ok && (b.fitBasis == RLDMK_FIT_KART) && (b.fitFactor == 1.254) && (b.fitKart[1] > 112.3) && (b.fitKart[1] < 112.5) &&
		               !RldMk_Said("ply-no-kart"),
		           "--fit crash with a long driver: the kart found, x1.254", "built %d, basis %d, x%.4g, kart %.2f, said: %s", ok, b.fitBasis, b.fitFactor,
		           b.fitKart[1], s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// No kart at all (a driver in the air): the whole model at Crash's length,
	// and ply-no-kart.
	{
		const double driverLo[3] = {-0.30, 0.60, -0.40};
		const double driverHi[3] = {0.30, 1.40, 0.40};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(!ok && b.fitted && (b.fitBasis == RLDMK_FIT_MODEL) && (b.fitFactor == 2.195) && RldMk_Said("ply-no-kart"),
		           "--fit crash without a kart: the model's length (x2.195), ply-no-kart", "built %d, basis %d, x%.4g, said: %s", ok, b.fitBasis,
		           b.fitFactor, s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// A face of five corners on a line: nothing to measure, so nothing fitted,
	// and the real pass says ply-ngon once (the measuring pass leaves no error
	// behind).
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	{
		const u32 first = RldMk_TestVertex(mesh, 0.0, 0.5, -0.7, 50u);

		RldMk_TestVertex(mesh, 0.1, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.2, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.3, 0.5, -0.7, 50u);
		RldMk_TestVertex(mesh, 0.4, 0.5, -0.7, 50u);
		RldMk_TestFace(mesh, 5u, first, first + 1u, first + 2u, first + 3u, first + 4u);
	}
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(!ok && !b.fitted && (RldMk_TestSaidCount("ply-ngon") == 1u) && (s_rldMkErrors == 1), "--fit crash on an n-gon: ply-ngon once, not fitted",
	           "built %d, fitted %d, errors %d, said: %s", ok, b.fitted, s_rldMkErrors, s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// --- n-gons and --reduce ---------------------------------------------------------------

static void RldMk_TestReport(const char *line)
{
	printf("%s\n", line);
}

// RldMk_SplitNgon alone, on positions of its own: an L of six corners (area 3)
// and five corners on a line.
static void RldMk_SelfTestNgon(void)
{
	double position[6 * 3] = {0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 2.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 2.0, 0.0, 0.0, 2.0, 0.0};
	double line[5 * 3] = {0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2.0, 0.0, 0.0, 3.0, 0.0, 0.0, 4.0, 0.0, 0.0};
	u32 vertexPosition[6] = {0u, 1u, 2u, 3u, 4u, 5u};
	const u32 corners[6] = {0u, 1u, 2u, 3u, 4u, 5u};
	u32 triangle[3 * 6];
	struct RldMkBuild b;
	double area = 0.0;
	int turned = 0;
	int ok;
	u32 t;

	memset(&b, 0, sizeof(b));
	b.position = position;
	b.vertexPosition = vertexPosition;
	b.triangle = triangle;
	ok = RldMk_SplitNgon(&b, corners, 6u);
	for (t = 0; ok && (t < b.triangleCount); t++)
	{
		double n[3];

		RldMk_Cross(&position[3u * triangle[3u * t]], &position[3u * triangle[3u * t + 1u]], &position[3u * triangle[3u * t + 2u]], n);
		area += 0.5 * n[2];
		turned |= !(n[2] > 0.0);
	}
	RldMk_Test(ok && (b.triangleCount == 4u) && (area == 3.0) && !turned && (triangle[0] == 1u) && (triangle[1] == 2u) && (triangle[2] == 3u),
	           "an L of six corners: 4 triangles, area 3, wound like the face, the best ear first (corner 2, a tie with 4)", "split %d, %u triangles, area %g, turned %d", ok,
	           b.triangleCount, area, turned);

	memset(&b, 0, sizeof(b));
	b.position = line;
	b.vertexPosition = vertexPosition;
	b.triangle = triangle;
	RldMk_Test(!RldMk_SplitNgon(&b, corners, 5u) && (b.triangleCount == 0u), "five corners on a line: not split", "%u triangles", b.triangleCount);
}

static void RldMk_SelfTestReduce(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkPly plyAgain;
	struct RldMkBuild b;
	struct RldMkBuild again;
	struct RldCharFinding first;
	struct RldCharModelFacts facts;
	u32 counts[RLDMK_GROUPS];
	u32 targets[RLDMK_GROUPS];
	u32 over = 0;
	int ok;
	int same;

	s_rldMkTestFailed += RldRed_SelfTest(RldMk_TestReport);
	RldMk_SelfTestNgon();

	// THE TARGETS: small groups whole, the rest in proportion, the leftover
	// by the largest remainder. A fixed target of 797 (the limit of make-char
	// before 2026-10-02): these test the sharing, not the limit.
	counts[0] = 12u;
	counts[1] = 972u;
	counts[2] = 12u;
	ok = RldMk_ReduceTargets(counts, 797u, targets);
	RldMk_Test(ok && (targets[0] == 12u) && (targets[1] == 773u) && (targets[2] == 12u), "reduce targets 12/972/12 -> 12/773/12", "%u %u %u",
	           targets[0], targets[1], targets[2]);
	counts[0] = 300u;
	counts[1] = 600u;
	counts[2] = 100u;
	ok = RldMk_ReduceTargets(counts, 797u, targets);
	RldMk_Test(ok && (targets[0] == 239u) && (targets[1] == 478u) && (targets[2] == 80u) && ((targets[0] + targets[1] + targets[2]) == 797u),
	           "reduce targets 300/600/100 -> 239/478/80 (largest remainder)", "%u %u %u", targets[0], targets[1], targets[2]);
	counts[0] = 20u;
	counts[1] = 30u;
	counts[2] = 24u;
	RldMk_Test(!RldMk_ReduceTargets(counts, 40u, targets), "reduce targets: small groups beyond the target refused", "accepted");
	counts[0] = 100u;
	counts[1] = 100u;
	counts[2] = 100u;
	ok = RldMk_ReduceTargets(counts, 298u, targets);
	RldMk_Test(ok && (targets[0] == 100u) && (targets[1] == 99u) && (targets[2] == 99u), "reduce targets: equal remainders go to the lower group",
	           "%u %u %u", targets[0], targets[1], targets[2]);

	// The first split over the limit, --reduce auto: at most
	// RLDMK_REDUCE_TARGET, the game's check passes, built twice the same,
	// model-reduced said once.
	memset(&plyAgain, 0, sizeof(plyAgain));
	memset(&again, 0, sizeof(again));
	RldMk_TestOptions(&opt);
	opt.reduce = 1;
	RldMk_TestStandard(mesh, RldMk_TestSplitOver(RLDMK_REDUCE_LIMIT, &over), 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	ok = ok && b.reduced && (b.reduceFrom == over) && (b.reduceTo <= RLDMK_REDUCE_TARGET) && (b.drawCount <= RLDMK_REDUCE_TARGET) &&
	     (RldChar_CheckModel(b.cmdl, b.cmdlSize, &first, NULL, NULL, &facts) == RLDCHAR_VERDICT_OK) && (facts.triangles == b.drawCount) &&
	     (b.groupTriangles[RLDMK_GROUP_KART] == 12u) && (b.groupTriangles[RLDMK_GROUP_WHEEL] == 12u);
	RldMk_Test(ok && (RldMk_TestSaidCount("model-reduced") == 1u) && !RldMk_Said("model-tris"),
	           "over the limit: reduced automatically to at most the target, the game's check passes", "built %d, %u -> %u, kart %u, wheel %u, said: %s", ok,
	           b.reduceFrom, b.reduceTo, b.groupTriangles[RLDMK_GROUP_KART], b.groupTriangles[RLDMK_GROUP_WHEEL], s_rldMkCodes);
	same = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
	RldMk_Test(same, "the reduced model built twice: the same bytes", "differ");
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// The default options of make-char (--fit crash, --reduce auto) on the same
	// model.
	RldMk_DefaultOptions(&opt);
	RldMk_TestStandard(mesh, RldMk_TestSplitOver(RLDMK_REDUCE_LIMIT, &over), 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && b.fitted && b.reduced && (b.reduceTo <= RLDMK_REDUCE_TARGET), "make-char's defaults: fitted and reduced", "built %d, said: %s", ok,
	           s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	RldMk_TestOptions(&opt);
	opt.reduce = 1;

	// 804 TRIANGLES IN 582 PARTS: a kart, a driver and 200 quads and 380
	// triangles apart behind him - many records for few triangles. Under the
	// limit: built with --reduce off, and auto reduces nothing - the same bytes.
	{
		const double kartLo[3] = {-0.40, 0.05, -0.85};
		const double kartHi[3] = {0.40, 0.30, 0.85};
		const double driverLo[3] = {-0.20, 0.50, 0.00};
		const double driverHi[3] = {0.20, 1.20, 0.40};
		u32 k;

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 1u, 10u, 0, -1);
		for (k = 0; k < 200u; k++)
		{
			const double x = -0.30 + 0.03 * (double)(k % 20u);
			const double y = 0.48 + 0.03 * (double)(k / 20u);
			const u32 v = RldMk_TestVertex(mesh, x, y, -0.5, 30u + (k % 30u));

			RldMk_TestVertex(mesh, x + 0.015, y, -0.5, 30u + (k % 30u));
			RldMk_TestVertex(mesh, x + 0.015, y + 0.015, -0.5, 30u + (k % 30u));
			RldMk_TestVertex(mesh, x, y + 0.015, -0.5, 30u + (k % 30u));
			RldMk_TestFace(mesh, 4u, v, v + 1u, v + 2u, v + 3u, 0u);
		}
		for (k = 0; k < 380u; k++)
		{
			const double x = -0.30 + 0.03 * (double)(k % 20u);
			const double y = 0.80 + 0.03 * (double)(k / 20u);
			const u32 v = RldMk_TestVertex(mesh, x, y, -0.5, 60u + (k % 30u));

			RldMk_TestVertex(mesh, x + 0.015, y, -0.5, 60u + (k % 30u));
			RldMk_TestVertex(mesh, x, y + 0.015, -0.5, 60u + (k % 30u));
			RldMk_TestFace(mesh, 3u, v, v + 1u, v + 2u, 0u, 0u);
		}

		opt.reduce = 0;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.triangleCount == 804u) &&
		     (RldChar_CheckModel(again.cmdl, again.cmdlSize, &first, NULL, NULL, &facts) == RLDCHAR_VERDICT_OK);
		RldMk_Test(ok && !RldMk_Said("mem-model"), "804 triangles in 582 parts, --reduce off: built", "built %d, %u triangles, said: %s", ok,
		           again.triangleCount, s_rldMkCodes);

		opt.reduce = 1;
		ok = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && !b.reduced && (b.cmdlSize == again.cmdlSize) && (memcmp(b.cmdl, again.cmdl, b.cmdlSize) == 0);
		RldMk_Test(ok && !RldMk_Said("model-reduced"), "the same, --reduce auto: under the limit nothing is reduced, the bytes of off",
		           "built %d, %u -> %u, %u bytes, said: %s", ok, b.reduceFrom, b.reduceTo, (u32)b.cmdlSize, s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyAgain);
	}

	// TRIANGLES THE REDUCER CANNOT TOUCH (one-triangle parts, more than the
	// limit, in layers of 30 x 30): warning model-reduce, then model-tris -
	// nothing else.
	{
		const double kartLo[3] = {-0.40, 0.05, -0.85};
		const double kartHi[3] = {0.40, 0.30, 0.85};
		const double driverLo[3] = {-0.20, 0.50, 0.00};
		const double driverHi[3] = {0.20, 1.20, 0.40};
		u32 k;

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 1u, 10u, 0, -1);
		for (k = 0; k < RLDMK_REDUCE_LIMIT; k++)
		{
			const double x = -0.30 + 0.02 * (double)(k % 30u);
			const double y = 0.48 + 0.02 * (double)((k / 30u) % 30u);
			const double z = -0.5 - 0.02 * (double)(k / 900u);
			const u32 v = RldMk_TestVertex(mesh, x, y, z, 60u + (k % 30u));

			RldMk_TestVertex(mesh, x + 0.01, y, z, 60u + (k % 30u));
			RldMk_TestVertex(mesh, x, y + 0.01, z, 60u + (k % 30u));
			RldMk_TestFace(mesh, 3u, v, v + 1u, v + 2u, 0u, 0u);
		}
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(!ok && !b.reduced && RldMk_Said("model-reduce") && RldMk_Said("model-tris") && !RldMk_Said("model-reduced"),
		           "more triangles than the limit the reducer cannot touch: model-reduce, then model-tris", "built %d, said: %s", ok, s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// At most the limit nothing is reduced: the bytes of --reduce off.
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && !b.reduced;
	opt.reduce = 0;
	same = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
	RldMk_Test(same, "--reduce auto leaves a small model alone (the bytes of off)", "built %d", ok);
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// --- --preview -----------------------------------------------------------------------

static int RldMk_TestPreviewCorner(const u8 *data, size_t at, int axis)
{
	const u32 raw = Rld_ReadLE16(&data[at + 2u * (u32)axis]);

	return (raw >= 0x8000u) ? ((int)raw - 0x10000) : (int)raw;
}

static void RldMk_SelfTestPreview(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	int box[6];
	size_t size = 0;
	u8 *data = NULL;
	int ok;

	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	if (ok)
	{
		data = RldMk_Preview(&b, &size, box);
	}

	ok = (data != NULL) && (size == (12u + 3u * (4u + (size_t)b.drawCount * 30u))) && (memcmp(data, "RLDPV1\0\0", 8u) == 0) && (Rld_ReadLE32(&data[8]) == 3u);
	if (ok)
	{
		u32 pose;

		for (pose = 0; pose < 3u; pose++)
		{
			ok = ok && (Rld_ReadLE32(&data[12u + (size_t)pose * (4u + (size_t)b.drawCount * 30u)]) == b.drawCount);
		}
	}
	RldMk_Test(ok, "preview: RLDPV1, 3 poses, every triangle", "size %u, %u triangles", (u32)size, b.drawCount);

	// The kart box: 0.9 x 0.25 x 1.8 at 0.1 above the ground, in game units.
	RldMk_Test(ok && (box[0] >= -30) && (box[0] <= -28) && (box[3] >= 28) && (box[3] <= 30) && (box[1] >= 5) && (box[1] <= 8) && (box[4] >= 21) &&
	               (box[4] <= 24) && (box[2] >= -59) && (box[2] <= -56) && (box[5] >= 56) && (box[5] <= 59),
	           "preview: kart box in game units", "%d %d %d .. %d %d %d", box[0], box[1], box[2], box[3], box[4], box[5]);

	// Neutral pose: every kart triangle faces out (its right-hand normal points
	// away from the kart's middle) and has no both-sides bit; the driver leans
	// toward +X in turn frame 0 (pose 1) and the other way in frame 20 (pose 2).
	if (ok)
	{
		const int middle[3] = {(box[0] + box[3]) / 2, (box[1] + box[4]) / 2, (box[2] + box[5]) / 2};
		s64 lean[3] = {0, 0, 0};
		u32 kartTriangles = 0;
		u32 inward = 0;
		u32 bothSides = 0;
		u32 pose;
		u32 t;

		for (pose = 0; pose < 3u; pose++)
		{
			const size_t first = 16u + (size_t)pose * (4u + (size_t)b.drawCount * 30u);

			for (t = 0; t < b.drawCount; t++)
			{
				const size_t at = first + (size_t)t * 30u;
				int c[3][3];
				int inKart = 1;
				int k;
				int a;

				for (k = 0; k < 3; k++)
				{
					for (a = 0; a < 3; a++)
					{
						c[k][a] = RldMk_TestPreviewCorner(data, at + 10u * (u32)k, a);
						if ((c[k][a] < box[a]) || (c[k][a] > box[3 + a]))
						{
							inKart = 0;
						}
					}
					bothSides += ((data[at + 10u * (u32)k + 9u] & 1u) != 0u) ? 1u : 0u;

					// The same corners in every pose: those high up in the neutral one.
					if (RldMk_TestPreviewCorner(data, 16u + (size_t)t * 30u + 10u * (u32)k, 1) > 30)
					{
						lean[pose] += c[k][0];
					}
				}

				if ((pose == 0u) && inKart)
				{
					const s64 u[3] = {c[1][0] - c[0][0], c[1][1] - c[0][1], c[1][2] - c[0][2]};
					const s64 v[3] = {c[2][0] - c[0][0], c[2][1] - c[0][1], c[2][2] - c[0][2]};
					const s64 n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
					const s64 out[3] = {c[0][0] + c[1][0] + c[2][0] - 3 * middle[0], c[0][1] + c[1][1] + c[2][1] - 3 * middle[1],
					                    c[0][2] + c[1][2] + c[2][2] - 3 * middle[2]};

					kartTriangles++;
					if ((n[0] * out[0] + n[1] * out[1] + n[2] * out[2]) <= 0)
					{
						inward++;
					}
				}
			}
		}

		RldMk_Test((kartTriangles == 12u) && (inward == 0u) && (bothSides == 0u), "preview: drawn sides counter-clockwise, facing out",
		           "%u kart triangles, %u inward, %u both-sides corners", kartTriangles, inward, bothSides);
		RldMk_Test((lean[1] > lean[0]) && (lean[0] > lean[2]), "preview: turn frame 0 leans toward +X, frame 20 the other way", "x sums %lld %lld %lld",
		           (long long)lean[0], (long long)lean[1], (long long)lean[2]);
	}
	free(data);
	data = NULL;
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// Both sides drawn: bit 0 of pad in every corner.
	opt.twoSided = 1;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	if (ok)
	{
		data = RldMk_Preview(&b, &size, box);
	}
	if (data != NULL)
	{
		u32 marked = 0;
		u32 t;

		for (t = 0; t < 3u * b.drawCount; t++)
		{
			marked += ((data[16u + (size_t)t * 10u + 9u] & 1u) != 0u) ? 1u : 0u;
		}
		ok = (marked == (3u * b.drawCount));
	}
	RldMk_Test((data != NULL) && ok, "preview: --two-sided marks every corner", "not marked");
	free(data);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// --- --diagnose ----------------------------------------------------------------------

// Does `text` hold the line `line` (with its LF)?
static int RldMk_TestHasLine(const char *text, const char *line)
{
	const size_t length = strlen(line);
	const char *at = text;

	while ((at = strstr(at, line)) != NULL)
	{
		if (((at == text) || (at[-1] == '\n')) && (at[length] == '\n'))
		{
			return 1;
		}
		at += length;
	}
	return 0;
}

static void RldMk_SelfTestDiagnose(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkPly plyAgain;
	struct RldMkBuild b;
	struct RldMkBuild again;
	char line[64];
	char overLine[2][32];
	char *text = NULL;
	char *textAgain = NULL;
	size_t size = 0;
	size_t sizeAgain = 0;
	u32 over = 0;
	int ok;
	u32 i;

	memset(&plyAgain, 0, sizeof(plyAgain));
	memset(&again, 0, sizeof(again));

	// The first split over the limit, --reduce off: model-tris lifted, the
	// whole model measured; the text from "RLDDIAG 1" to "end", twice the same.
	RldMk_TestOptions(&opt);
	opt.diagnose = 1;
	RldMk_TestStandard(mesh, RldMk_TestSplitOver(RLDMK_REDUCE_LIMIT, &over), 0, 0.0, -1);
	snprintf(overLine[0], sizeof(overLine[0]), "triangles %u", over);
	snprintf(overLine[1], sizeof(overLine[1]), "reduced %u", over);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
	RldMk_Test(ok && !RldMk_Said("model-tris") && RldMk_Said("diagnose-lifted") && !b.reduced && (b.drawCount + b.collapsed == over),
	           "--diagnose, over the limit, --reduce off: model-tris lifted, built", "built %d, %u drawn, said: %s", ok, b.drawCount, s_rldMkCodes);
	if (ok)
	{
		text = RldMk_Diagnosis(&b, &opt, &size);
	}
	snprintf(line, sizeof(line), "drawn %u", b.drawCount);
	ok = (text != NULL) && (size > 14u) && (strncmp(text, "RLDDIAG 1\n", 10u) == 0) && (strcmp(&text[size - 4u], "end\n") == 0) && (strlen(text) == size) &&
	     RldMk_TestHasLine(text, overLine[0]) && RldMk_TestHasLine(text, overLine[1]) && RldMk_TestHasLine(text, "frames 47") && RldMk_TestHasLine(text, line);
	RldMk_Test(ok, "--diagnose text: RLDDIAG 1 ... end, all triangles not reduced, every drawn one", "%u bytes", (u32)size);
	ok = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && ((textAgain = RldMk_Diagnosis(&again, &opt, &sizeAgain)) != NULL) && (sizeAgain == size) &&
	     (memcmp(textAgain, text, size) == 0);
	RldMk_Test(ok, "--diagnose built twice: the same text", "differ");
	free(text);
	free(textAgain);
	text = NULL;
	textAgain = NULL;
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// The same with --reduce auto: the build make-char would write; every drawn
	// triangle knows its reduced one, every reduced one its source.
	opt.reduce = 1;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && b.reduced && (b.reduceTo <= RLDMK_REDUCE_TARGET) && (b.diagReducedCount == b.reduceTo) &&
	     !RldMk_Said("diagnose-lifted");
	for (i = 0; ok && (i < b.diagReducedCount); i++)
	{
		const u32 *r = &b.diagReduced[7u * i];
		int k;

		ok = (r[3] < b.diagTriangleCount);
		for (k = 0; ok && (k < 3); k++)
		{
			// the corner whose color it carries sits at the same position
			ok = (r[4 + k] < (3u * b.diagTriangleCount)) && (b.diagTriangle[4u * (r[4 + k] / 3u) + (r[4 + k] % 3u)] == r[k]);
		}
	}
	for (i = 0; ok && (i < b.drawCount); i++)
	{
		ok = (b.triangleReduced[b.drawTriangle[i]] < b.diagReducedCount);
	}
	if (ok)
	{
		text = RldMk_Diagnosis(&b, &opt, &size);
	}
	snprintf(line, sizeof(line), "reduced %u", b.reduceTo);
	ok = ok && (text != NULL) && RldMk_TestHasLine(text, overLine[0]) && RldMk_TestHasLine(text, line);
	if (ok)
	{
		// No drawn triangle without its reduced one: no "d" line ends in -1.
		const char *at = text;

		while (ok && ((at = strstr(at, "\nd ")) != NULL))
		{
			const char *end = strchr(at + 1, '\n');

			ok = (end != NULL) && !(((end - at) > 3) && (strncmp(end - 3, " -1", 3u) == 0));
			at = (end != NULL) ? end : &at[1];
		}
	}
	RldMk_Test(ok,
	           "--diagnose with --reduce auto: reduced as without it, every triangle traced", "built %d, %u -> %u", ok, b.reduceFrom, b.reduceTo);
	free(text);
	text = NULL;
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// A model within the limits: the same bytes with and without --diagnose, and
	// no diagnosis from a build without it.
	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && (RldMk_Diagnosis(&b, &opt, &size) == NULL);
	opt.diagnose = 1;
	ok = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
	RldMk_Test(ok, "--diagnose changes no byte of a model within the limits", "differ");
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// --- Vehicle parts, packing, every frame read back -----------------------------------

static void RldMk_SelfTestVehicle(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	u32 flips[RLDMK_FRAMES_TOTAL];
	u32 badFrame = 0;
	u32 badTriangle = 0;
	int cut[6];
	int ok;
	u32 p;

	// THE PACKING: a value beyond its byte is cut off and says so; the ends
	// themselves are not cut.
	ok = (RldMk_QuantValue(-0.6, 0, &cut[0]) == 0) && (RldMk_QuantValue(255.6, 2, &cut[1]) == 255) && (RldMk_QuantValue(255.2, 1, &cut[2]) == 254) &&
	     (RldMk_QuantValue(-0.5, 0, &cut[3]) == 0) && (RldMk_QuantValue(255.4, 2, &cut[4]) == 255) && (RldMk_QuantValue(254.9, 1, &cut[5]) == 254);
	RldMk_Test(ok && cut[0] && cut[1] && cut[2] && !cut[3] && !cut[4] && !cut[5], "packing: values beyond the byte are found, the ends are kept",
	           "%d %d %d %d %d %d", cut[0], cut[1], cut[2], cut[3], cut[4], cut[5]);

	// THE VEHICLE: the standard model with a front part across the middle of
	// the kart's nose (0.05 to 0.8 above the ground) and a seat across the
	// middle behind the driver (0.30 to 0.38, below the kart's top at 0.35):
	// both stay with the kart in every frame. A shoe to one side of the middle
	// and the steering wheel go on with the driver.
	RldMk_TestOptions(&opt);
	RldMk_TestStandard(mesh, 1u, 0, 0.0, -1);
	{
		const double frontLo[3] = {-0.20, 0.05, 0.60};
		const double frontHi[3] = {0.20, 0.80, 0.95};
		const double seatLo[3] = {-0.20, 0.30, -0.55};
		const double seatHi[3] = {0.20, 0.38, -0.42};
		const double shoeLo[3] = {0.05, 0.12, -0.40};
		const double shoeHi[3] = {0.25, 0.40, 0.30};

		RldMk_TestBox(mesh, frontLo, frontHi, 1u, 30u, 0, -1);
		RldMk_TestBox(mesh, seatLo, seatHi, 1u, 35u, 0, -1);
		RldMk_TestBox(mesh, shoeLo, shoeHi, 1u, 40u, 0, -1);
	}
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && (b.groupTriangles[RLDMK_GROUP_KART] == 36u) && (b.groupTriangles[RLDMK_GROUP_DRIVER] == 24u) &&
	     (b.groupTriangles[RLDMK_GROUP_WHEEL] == 12u) && !RldMk_Said("model-quant");
	for (p = 0; ok && (p < b.positionCount); p++)
	{
		const double *still = RldMk_At(&b, 0u, p);
		const double *neutral = RldMk_At(&b, RLDMK_NEUTRAL_FRAME, p);

		if ((b.positionGroup[p] == RLDMK_GROUP_KART) && (b.position[3u * p + 2u] > (0.55 * RLDMK_UNITS)))
		{
			ok = (still[0] == neutral[0]) && (still[1] == neutral[1]) && (still[2] == neutral[2]);
		}
	}
	RldMk_Test(ok, "vehicle parts across the middle stay with the kart, a shoe to one side and the steering wheel do not",
	           "kart %u, driver %u, wheel %u, said: %s", b.groupTriangles[RLDMK_GROUP_KART], b.groupTriangles[RLDMK_GROUP_DRIVER],
	           b.groupTriangles[RLDMK_GROUP_WHEEL], s_rldMkCodes);

	// EVERY FRAME READ BACK: the built model passes; one color index changed in
	// the command list fails in frame 0 at the first triangle, one record moved
	// in frame 30 only fails there.
	ok = ok && RldMk_ReplayFrames(&b, 0, &badFrame, &badTriangle, flips);
	RldMk_Test(ok, "every frame replayed: the triangles the builder meant", "frame %u, triangle %u", badFrame, badTriangle);
	if (ok)
	{
		u8 *word = &b.cmdl[4u + RLDMK_BODY_COMMANDS + 4u + 4u * 2u];

		Rld_WriteLE32(word, Rld_ReadLE32(word) ^ (1u << RLDMK_COLOR_SHIFT));
		ok = !RldMk_ReplayFrames(&b, 0, &badFrame, &badTriangle, flips) && (badFrame == 0u) && (badTriangle == 0u);
		Rld_WriteLE32(word, Rld_ReadLE32(word) ^ (1u << RLDMK_COLOR_SHIFT));
		RldMk_Test(ok, "a color index changed: found in frame 0, triangle 0", "frame %u, triangle %u", badFrame, badTriangle);
	}
	if (ok)
	{
		u8 *frame = (u8 *)RldMk_FrameAt(b.cmdl, 30u);
		u8 *record = &frame[Rld_ReadLE32(&frame[RLDMK_FRAME_VERTEX_OFFSET])];

		record[0] = (u8)(record[0] + 100u);
		ok = !RldMk_ReplayFrames(&b, 0, &badFrame, &badTriangle, flips) && (badFrame == 30u) && (badTriangle == 0u);
		RldMk_Test(ok, "a record moved in frame 30 only: found there", "frame %u, triangle %u", badFrame, badTriangle);
	}
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// --- --repair ------------------------------------------------------------------------

// The cull bits of the triangles a build draws: *culled with the bit, *both without.
static void RldMk_TestCull(const struct RldMkBuild *b, u32 *culled, u32 *both)
{
	struct RldMkReplay replay;
	u32 corner[3];
	u32 color[3];
	u32 draw = 0;

	*culled = 0;
	*both = 0;
	RldMk_ReplayStart(&replay, b->cmdl, b->recordCount);
	while (RldMk_ReplayNext(&replay, corner, color, &draw) == 1)
	{
		if ((draw & RLDMK_CMD_CULL) != 0u)
		{
			(*culled)++;
		}
		else
		{
			(*both)++;
		}
	}
}

static void RldMk_SelfTestRepair(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkPly plyAgain;
	struct RldMkBuild b;
	struct RldMkBuild again;
	struct RldCharFinding first;
	u32 culled = 0;
	u32 both = 0;
	u32 over = 0;
	int all = 1;
	int ok;
	int k;

	s_rldMkTestFailed += RldRep_SelfTest(RldMk_TestReport);
	memset(&plyAgain, 0, sizeof(plyAgain));
	memset(&again, 0, sizeof(again));

	// A steering wheel turned inside out (x mirrored) and a driver with a hole
	// of 4 edges at the top: --repair auto turns the 12 back and closes the
	// hole with 2 triangles; the game's check passes, built twice the same,
	// every triangle culled.
	RldMk_TestOptions(&opt);
	opt.repair = 1;
	{
		const double kartLo[3] = {-0.45, 0.10, -0.90};
		const double kartHi[3] = {0.45, 0.35, 0.90};
		const double driverLo[3] = {-0.25, 0.40, -0.40};
		const double driverHi[3] = {0.25, 1.00, 0.10};
		const double wheelLo[3] = {0.15, 0.50, 0.30};
		const double wheelHi[3] = {-0.15, 0.60, 0.36};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 1u, 10u, 0, 2);
		RldMk_TestBox(mesh, wheelLo, wheelHi, 1u, 20u, 0, -1);
	}
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && b.repaired && (b.repair[3] == 12u) && (b.repair[4] == 1u) && (b.repair[5] == 2u) &&
	     (b.repair[8] == 0u) && (b.openEdges == 0u) && (RldChar_CheckModel(b.cmdl, b.cmdlSize, &first, NULL, NULL, NULL) == RLDCHAR_VERDICT_OK) &&
	     RldMk_Said("model-repaired");
	if (ok)
	{
		RldMk_TestCull(&b, &culled, &both);
	}
	RldMk_Test(ok && (both == 0u) && (culled == b.drawCount), "--repair auto: a part turned inside out turned back, a small hole closed, the game's check passes",
	           "built %d, turned %u, holes %u (%u triangles), open edges %u -> %u, both sides %u, said: %s", ok, b.repair[3], b.repair[4], b.repair[5],
	           b.repair[7], b.repair[8], both, s_rldMkCodes);
	ok = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
	RldMk_Test(ok, "--repair auto built twice: the same bytes", "differ");
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// An open part: the driver of 90 triangles open at the top (12 edges, more
	// than the 8 a hole may have): it stays open and is drawn from both sides;
	// with --open-parts one-sided it is culled.
	RldMk_TestStandard(mesh, 3u, 0, 0.0, 2);
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && (b.repair[6] != 0u) && (b.openTwoSided == 90u);
	if (ok)
	{
		RldMk_TestCull(&b, &culled, &both);
	}
	RldMk_Test(ok && (both == 90u) && (culled == 24u), "--repair auto: an open part is drawn from both sides, +0 triangles",
	           "built %d, open two-sided %u, drawn culled %u, both %u", ok, b.openTwoSided, culled, both);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	opt.openOneSided = 1;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && (b.openTwoSided == 0u);
	if (ok)
	{
		RldMk_TestCull(&b, &culled, &both);
	}
	RldMk_Test(ok && (both == 0u), "--open-parts one-sided: the open part is culled", "built %d, both %u", ok, both);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// --repair off: the open part is found by its topology alone, the same 90.
	opt.openOneSided = 0;
	opt.repair = 0;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && !b.repaired && (b.openTwoSided == 90u);
	if (ok)
	{
		RldMk_TestCull(&b, &culled, &both);
	}
	RldMk_Test(ok && (both == 90u) && (culled == 24u), "--repair off: the open part is drawn from both sides as well", "built %d, open two-sided %u, both %u",
	           ok, b.openTwoSided, both);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
	opt.repair = 1;

	// The models of the golden hashes need no repair: --repair auto gives the
	// bytes of --repair off for all five.
	for (k = 0; k < 5; k++)
	{
		RldMk_TestOptions(&opt);
		opt.sizePercent = (k == 1) ? 120u : 100u;
		opt.fit = (k == 2);
		opt.reduce = (k == 3);
		opt.noWheels = (k == 4);
		RldMk_TestStandard(mesh, (k == 3) ? RldMk_TestSplitOver(RLDMK_REDUCE_LIMIT, &over) : 2u, 0, 0.0, -1);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		opt.repair = 1;
		ok = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && again.repaired && (again.cmdlSize == b.cmdlSize) &&
		     (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
		if (!ok && all)
		{
			all = 0;
			RldMk_Test(0, "--repair auto on the golden models: the bytes of --repair off", "golden model %d differs", k);
		}
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyAgain);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}
	if (all)
	{
		RldMk_Test(1, "--repair auto on the golden models: the bytes of --repair off (all five)", "");
	}
}

// --- --remesh ------------------------------------------------------------------------

static void RldMk_SelfTestRemesh(struct RldMkTestMesh *mesh)
{
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkPly plyAgain;
	struct RldMkBuild b;
	struct RldMkBuild again;
	struct RldCharFinding first;
	int ok;
	int same;

	s_rldMkTestFailed += RldRem_SelfTest(RldMk_TestReport);

	// No hull below 40 triangles: a hull of 30 source triangles would get 33
	// of 797 in proportion and is raised to 40, the largest hull gives them.
	{
		const u32 source[RLDMK_GROUPS] = {12u, 30u, 900u};
		const u32 counts[RLDMK_GROUPS] = {12u, RLDMK_REMESH_SHARE_MIN, 900u};
		u32 targets[RLDMK_GROUPS];

		ok = RldMk_ReduceTargets(counts, 797u, targets) && (targets[1] == 33u) && RldMk_RemeshFloor(source, targets);
		RldMk_Test(ok && (targets[0] == 12u) && (targets[1] == 40u) && (targets[2] == 745u), "--remesh targets: no hull below 40 (12/30/900 -> 12/40/745)",
		           "%u %u %u", targets[0], targets[1], targets[2]);
	}

	// The standard model with a driver of 90 triangles, open at the top:
	// --remesh on makes it a closed hull and reduces it; kart and steering
	// wheel (12 triangles each) stay as they are. The game's check passes, no
	// open edge is left, built twice the same.
	RldMk_TestOptions(&opt);
	opt.reduce = 1;
	opt.remesh = 1;
	RldMk_TestStandard(mesh, 3u, 0, 0.0, 2);
	memset(&plyAgain, 0, sizeof(plyAgain));
	memset(&again, 0, sizeof(again));
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && b.remeshed && (b.remeshFrom == 114u) && (b.remeshOpenBefore != 0u) && (b.remeshHull > 1000u) &&
	     (b.openEdges == 0u) && (b.drawCount <= RLDMK_REDUCE_TARGET) && (b.groupTriangles[RLDMK_GROUP_KART] == 12u) &&
	     (b.groupTriangles[RLDMK_GROUP_WHEEL] == 12u) && (RldChar_CheckModel(b.cmdl, b.cmdlSize, &first, NULL, NULL, NULL) == RLDCHAR_VERDICT_OK) &&
	     RldMk_Said("model-remeshed");
	RldMk_Test(ok, "--remesh on: an open driver becomes a closed hull, reduced, the game's check passes, no open edge",
	           "built %d, %u -> %u -> %u triangles, open edges %u -> %u, kart %u, wheel %u, said: %s", ok, b.remeshFrom, b.remeshHull, b.drawCount,
	           b.remeshOpenBefore, b.openEdges, b.groupTriangles[RLDMK_GROUP_KART], b.groupTriangles[RLDMK_GROUP_WHEEL], s_rldMkCodes);
	same = ok && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (again.cmdlSize == b.cmdlSize) && (memcmp(again.cmdl, b.cmdl, b.cmdlSize) == 0);
	RldMk_Test(same, "--remesh on built twice: the same bytes", "differ");
	RldMk_FreeBuild(&again);
	RldMk_FreePly(&plyAgain);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);

	// Without --remesh the same model is built as modeled, open edges and all.
	opt.remesh = 0;
	ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && !b.remeshed && (b.openEdges != 0u) && !RldMk_Said("model-remeshed");
	RldMk_Test(ok, "--remesh off (the default): nothing remeshed", "built %d, said: %s", ok, s_rldMkCodes);
	RldMk_FreeBuild(&b);
	RldMk_FreePly(&ply);
}

// --- The PNG reader ------------------------------------------------------------------
//
// Pictures made here: samples from a formula, packed, filtered (every row
// another filter), compressed into stored or fixed-Huffman blocks by the
// small deflate below, wrapped in chunks with their CRC. A dynamic-Huffman
// stream is given as bytes (made once by zlib 1.3.1 at level 9 from the samples
// of RldMk_TestDynamicSample) - writing one here would be a second encoder.

struct RldMkBitWriter
{
	struct RldMkBuffer *buffer;
	u32 bits;
	u32 count;
};

static void RldMk_PutBits(struct RldMkBitWriter *w, u32 value, u32 count)
{
	w->bits |= value << w->count;
	w->count += count;
	while (w->count >= 8u)
	{
		const u8 byte = (u8)(w->bits & 0xFFu);

		RldMk_BufferBytes(w->buffer, &byte, 1u);
		w->bits >>= 8;
		w->count -= 8u;
	}
}

static void RldMk_PutFlush(struct RldMkBitWriter *w)
{
	if (w->count != 0u)
	{
		const u8 byte = (u8)(w->bits & 0xFFu);

		RldMk_BufferBytes(w->buffer, &byte, 1u);
	}
	w->bits = 0;
	w->count = 0;
}

// A Huffman code goes out with its highest bit first.
static void RldMk_PutCode(struct RldMkBitWriter *w, u32 code, u32 length)
{
	u32 reversed = 0;
	u32 i;

	for (i = 0; i < length; i++)
	{
		reversed = (reversed << 1) | ((code >> i) & 1u);
	}
	RldMk_PutBits(w, reversed, length);
}

// The fixed literal/length code (RFC 1951 3.2.6).
static void RldMk_PutSymbol(struct RldMkBitWriter *w, u32 symbol)
{
	if (symbol < 144u)
	{
		RldMk_PutCode(w, 0x30u + symbol, 8u);
	}
	else if (symbol < 256u)
	{
		RldMk_PutCode(w, 0x190u + (symbol - 144u), 9u);
	}
	else if (symbol < 280u)
	{
		RldMk_PutCode(w, symbol - 256u, 7u);
	}
	else
	{
		RldMk_PutCode(w, 0xC0u + (symbol - 280u), 8u);
	}
}

static void RldMk_PutMatch(struct RldMkBitWriter *w, u32 length, u32 distance)
{
	int i = RLDPNG_LENGTH_CODES - 1;
	int j = RLDPNG_DISTANCE_CODES - 1;

	while ((u32)s_rldPngLengthBase[i] > length)
	{
		i--;
	}
	RldMk_PutSymbol(w, 257u + (u32)i);
	RldMk_PutBits(w, length - s_rldPngLengthBase[i], s_rldPngLengthExtra[i]);

	while ((u32)s_rldPngDistanceBase[j] > distance)
	{
		j--;
	}
	RldMk_PutCode(w, (u32)j, 5u);
	RldMk_PutBits(w, distance - s_rldPngDistanceBase[j], s_rldPngDistanceExtra[j]);
}

// A zlib stream. mode 0: stored blocks of 300 bytes; mode 1: two fixed-Huffman
// blocks with literals and matches at distance 1 and `stride` (the row above).
static void RldMk_TestDeflate(struct RldMkBuffer *out, const u8 *data, size_t size, int mode, size_t stride, int breakAdler)
{
	static const u8 header[2] = {0x78u, 0x01u};
	struct RldMkBitWriter w;
	u8 adler[4];
	u32 sum;

	memset(&w, 0, sizeof(w));
	w.buffer = out;
	RldMk_BufferBytes(out, header, 2u);

	if (mode == 0)
	{
		size_t at = 0;

		do
		{
			const size_t n = ((size - at) < 300u) ? (size - at) : 300u;
			u8 lengths[4];

			RldMk_PutBits(&w, ((at + n) == size) ? 1u : 0u, 1u);
			RldMk_PutBits(&w, 0u, 2u);
			RldMk_PutFlush(&w);
			lengths[0] = (u8)(n & 0xFFu);
			lengths[1] = (u8)((n >> 8) & 0xFFu);
			lengths[2] = (u8)(~n & 0xFFu);
			lengths[3] = (u8)((~n >> 8) & 0xFFu);
			RldMk_BufferBytes(out, lengths, 4u);
			RldMk_BufferBytes(out, &data[at], n);
			at += n;
		} while (at < size);
	}
	else
	{
		size_t at = 0;
		int block;

		for (block = 0; block < 2; block++)
		{
			const size_t end = (block == 0) ? (size / 2u) : size;

			RldMk_PutBits(&w, (block == 1) ? 1u : 0u, 1u);
			RldMk_PutBits(&w, 1u, 2u);

			while (at < end)
			{
				const size_t candidates[2] = {1u, stride};
				size_t bestLength = 0;
				size_t bestDistance = 0;
				int k;

				for (k = 0; k < 2; k++)
				{
					const size_t d = candidates[k];
					size_t length = 0;

					if ((d == 0u) || (d > at))
					{
						continue;
					}
					while ((length < 258u) && ((at + length) < end) && (data[at + length] == data[at + length - d]))
					{
						length++;
					}
					if (length > bestLength)
					{
						bestLength = length;
						bestDistance = d;
					}
				}

				if (bestLength >= 3u)
				{
					RldMk_PutMatch(&w, (u32)bestLength, (u32)bestDistance);
					at += bestLength;
				}
				else
				{
					RldMk_PutSymbol(&w, data[at]);
					at++;
				}
			}

			RldMk_PutSymbol(&w, 256u);
		}
		RldMk_PutFlush(&w);
	}

	sum = RldPng_Adler(data, size) ^ (breakAdler ? 1u : 0u);
	adler[0] = (u8)(sum >> 24);
	adler[1] = (u8)(sum >> 16);
	adler[2] = (u8)(sum >> 8);
	adler[3] = (u8)sum;
	RldMk_BufferBytes(out, adler, 4u);
}

static void RldMk_PutBE32(u8 *at, u32 value)
{
	at[0] = (u8)(value >> 24);
	at[1] = (u8)(value >> 16);
	at[2] = (u8)(value >> 8);
	at[3] = (u8)value;
}

static void RldMk_TestChunk(struct RldMkBuffer *png, const char *type, const u8 *data, u32 length)
{
	u8 *both = (u8 *)malloc((size_t)length + 4u);
	u8 word[4];

	if (both == NULL)
	{
		return;
	}
	memcpy(both, type, 4u);
	if (length != 0u)
	{
		memcpy(&both[4], data, length);
	}

	RldMk_PutBE32(word, length);
	RldMk_BufferBytes(png, word, 4u);
	RldMk_BufferBytes(png, both, (size_t)length + 4u);
	RldMk_PutBE32(word, RldPng_Crc(both, (size_t)length + 4u));
	RldMk_BufferBytes(png, word, 4u);
	free(both);
}

struct RldMkTestImage
{
	u32 width;
	u32 height;
	u32 colorType;
	u32 depth;
	int mode;            // 0 stored blocks, 1 fixed Huffman
	int trns;            // a transparent key (types 0, 2) or palette alpha (type 3)
	u32 paletteCount;    // type 3
	int pictureA;        // the samples of RldMk_TestPixelA (RGBA 8 bit)
	int badFilter;       // row 0 gets the unknown filter 5
	int breakAdler;
	int interlaced;      // Adam7: seven passes, each filtered on its own
};

// Test picture A, 60 x 40: transparent corners, a black ring, inside a
// color ramp, the top rows at alpha 90 (below half coverage).
static void RldMk_TestPixelA(u32 x, u32 y, u8 rgba[4])
{
	const int dx = 2 * (int)x - 59;
	const int dy = 2 * (int)y - 39;
	const int r2 = dx * dx + dy * dy;

	if (r2 > 36 * 36)
	{
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
	}
	else if (r2 > 31 * 31)
	{
		rgba[0] = rgba[1] = rgba[2] = 0;
		rgba[3] = 255;
	}
	else
	{
		rgba[0] = (u8)((x * 4u) & 0xFFu);
		rgba[1] = (u8)((y * 6u) & 0xFFu);
		rgba[2] = (u8)(((x + y) * 3u) & 0xFFu);
		rgba[3] = (u8)((y < 10u) ? 90u : 255u);
	}
}

static u32 RldMk_TestMax(const struct RldMkTestImage *t)
{
	return (t->depth == 16u) ? 65535u : ((1u << t->depth) - 1u);
}

// Sample c of pixel x, y as stored, at the picture's depth.
static u32 RldMk_TestSample(const struct RldMkTestImage *t, u32 x, u32 y, u32 c)
{
	if (t->pictureA)
	{
		u8 rgba[4];

		RldMk_TestPixelA(x, y, rgba);
		return rgba[c];
	}

	if (t->colorType == 3u)
	{
		return (x * 5u + y * 3u + x * y) % t->paletteCount;
	}

	return ((x * 5u + y * 3u + c * 7u + x * y) * ((t->depth == 16u) ? 2741u : 1u)) % (RldMk_TestMax(t) + 1u);
}

static u8 RldMk_TestPalette(u32 index, u32 channel)
{
	return (u8)((channel == 0u) ? (index * 16u) : ((channel == 1u) ? (255u - index * 16u) : ((index * 37u) & 0xFFu)));
}

// The pixel a reader has to give, from the formula - not from the reader.
static void RldMk_TestExpected(const struct RldMkTestImage *t, u32 x, u32 y, u8 rgba[4])
{
	const u32 max = RldMk_TestMax(t);
	u32 s[4];
	u32 c;

	if (t->pictureA)
	{
		RldMk_TestPixelA(x, y, rgba);
		return;
	}

	for (c = 0; c < 4u; c++)
	{
		const u32 v = RldMk_TestSample(t, x, y, c);

		s[c] = (t->depth == 16u) ? ((v * 255u + 32767u) / 65535u) : (v * 255u / max);
	}

	switch (t->colorType)
	{
	case 0:
		rgba[0] = rgba[1] = rgba[2] = (u8)s[0];
		rgba[3] = (u8)((t->trns && (RldMk_TestSample(t, x, y, 0u) == RldMk_TestSample(t, 1u, 0u, 0u))) ? 0u : 255u);
		break;
	case 2:
		rgba[0] = (u8)s[0];
		rgba[1] = (u8)s[1];
		rgba[2] = (u8)s[2];
		rgba[3] = (u8)((t->trns && (RldMk_TestSample(t, x, y, 0u) == RldMk_TestSample(t, 1u, 0u, 0u)) &&
		                (RldMk_TestSample(t, x, y, 1u) == RldMk_TestSample(t, 1u, 0u, 1u)) && (RldMk_TestSample(t, x, y, 2u) == RldMk_TestSample(t, 1u, 0u, 2u)))
		                   ? 0u
		                   : 255u);
		break;
	case 3:
	{
		const u32 index = RldMk_TestSample(t, x, y, 0u);

		rgba[0] = RldMk_TestPalette(index, 0u);
		rgba[1] = RldMk_TestPalette(index, 1u);
		rgba[2] = RldMk_TestPalette(index, 2u);
		rgba[3] = (u8)((t->trns && (index < 6u)) ? (index * 40u) : 255u);
		break;
	}
	case 4:
		rgba[0] = rgba[1] = rgba[2] = (u8)s[0];
		rgba[3] = (u8)s[1];
		break;
	default:
		rgba[0] = (u8)s[0];
		rgba[1] = (u8)s[1];
		rgba[2] = (u8)s[2];
		rgba[3] = (u8)s[3];
		break;
	}
}

static int RldMk_TestPaeth(int a, int b, int c)
{
	const int p = a + b - c;
	const int pa = (p > a) ? (p - a) : (a - p);
	const int pb = (p > b) ? (p - b) : (b - p);
	const int pc = (p > c) ? (p - c) : (c - p);

	return ((pa <= pb) && (pa <= pc)) ? a : ((pb <= pc) ? b : c);
}

// One pass of the picture (with t->interlaced the pixels of Adam7 geometry g,
// else all of them): w x h pixels packed into rows, each row filtered by y % 5
// (row 0 with the unknown filter 5 for badFilter), into raw. Returns the bytes
// written, 0 = out of memory.
static size_t RldMk_TestPass(const struct RldMkTestImage *t, const u8 g[4], u32 w, u32 h, u8 *raw)
{
	const u32 channels = RldPng_Channels(t->colorType);
	const size_t rowBytes = ((size_t)w * channels * t->depth + 7u) / 8u;
	const size_t bpp = ((channels * t->depth) >= 8u) ? ((channels * t->depth) / 8u) : 1u;
	u8 *plain = (u8 *)calloc(rowBytes * h + 1u, 1u);
	u32 x;
	u32 y;
	u32 c;

	if (plain == NULL)
	{
		return 0;
	}

	for (y = 0; y < h; y++)
	{
		u8 *row = &plain[(size_t)y * rowBytes];

		for (x = 0; x < w; x++)
		{
			for (c = 0; c < channels; c++)
			{
				const u32 v = RldMk_TestSample(t, g[0] + x * g[2], g[1] + y * g[3], c);
				const u32 bit = (x * channels + c) * t->depth;

				if (t->depth == 16u)
				{
					row[bit / 8u] = (u8)(v >> 8);
					row[bit / 8u + 1u] = (u8)v;
				}
				else if (t->depth == 8u)
				{
					row[bit / 8u] = (u8)v;
				}
				else
				{
					row[bit / 8u] = (u8)(row[bit / 8u] | (v << (8u - t->depth - (bit & 7u))));
				}
			}
		}
	}

	for (y = 0; y < h; y++)
	{
		const u8 *row = &plain[(size_t)y * rowBytes];
		const u8 *above = (y > 0u) ? &plain[(size_t)(y - 1u) * rowBytes] : NULL;
		u8 *line = &raw[(size_t)y * (rowBytes + 1u)];
		const u32 filter = (t->badFilter && (y == 0u)) ? 5u : (y % 5u);
		size_t i;

		line[0] = (u8)filter;
		for (i = 0; i < rowBytes; i++)
		{
			const int a = (i >= bpp) ? row[i - bpp] : 0;
			const int b = (above != NULL) ? above[i] : 0;
			const int cc = ((i >= bpp) && (above != NULL)) ? above[i - bpp] : 0;
			int predicted = 0;

			if (filter == 1u)
			{
				predicted = a;
			}
			else if (filter == 2u)
			{
				predicted = b;
			}
			else if (filter == 3u)
			{
				predicted = (a + b) >> 1;
			}
			else if (filter == 4u)
			{
				predicted = RldMk_TestPaeth(a, b, cc);
			}
			line[1u + i] = (u8)(row[i] - predicted);
		}
	}

	free(plain);
	return (rowBytes + 1u) * h;
}

// The whole PNG. NULL = out of memory.
static u8 *RldMk_TestPng(const struct RldMkTestImage *t, size_t *sizeOut)
{
	const u32 channels = RldPng_Channels(t->colorType);
	const u32 passes = t->interlaced ? 7u : 1u;
	struct RldPngImage geometry;
	struct RldMkBuffer idat;
	struct RldMkBuffer png;
	size_t total = 0;
	size_t at = 0;
	u8 *raw;
	u8 head[13];
	u32 pass;
	u32 c;

	*sizeOut = 0;
	memset(&idat, 0, sizeof(idat));
	memset(&png, 0, sizeof(png));
	memset(&geometry, 0, sizeof(geometry));
	geometry.width = t->width;
	geometry.height = t->height;
	geometry.interlace = t->interlaced ? 1u : 0u;

	for (pass = 0; pass < passes; pass++)
	{
		u32 w;
		u32 h;

		(void)RldPng_Pass(&geometry, pass, &w, &h);
		if ((w != 0u) && (h != 0u))
		{
			total += (((size_t)w * channels * t->depth + 7u) / 8u + 1u) * h;
		}
	}

	raw = (u8 *)malloc(total + 1u);
	if (raw == NULL)
	{
		return NULL;
	}

	for (pass = 0; pass < passes; pass++)
	{
		u32 w;
		u32 h;
		const u8 *g = RldPng_Pass(&geometry, pass, &w, &h);
		size_t written;

		if ((w == 0u) || (h == 0u))
		{
			continue;
		}
		written = RldMk_TestPass(t, g, w, h, &raw[at]);
		if (written == 0u)
		{
			free(raw);
			return NULL;
		}
		at += written;
	}

	RldMk_TestDeflate(&idat, raw, total, t->mode, ((size_t)t->width * channels * t->depth + 7u) / 8u + 1u, t->breakAdler);
	free(raw);

	RldMk_BufferBytes(&png, s_rldPngSignature, 8u);
	RldMk_PutBE32(&head[0], t->width);
	RldMk_PutBE32(&head[4], t->height);
	head[8] = (u8)t->depth;
	head[9] = (u8)t->colorType;
	head[10] = 0;
	head[11] = 0;
	head[12] = (u8)(t->interlaced ? 1u : 0u);
	RldMk_TestChunk(&png, "IHDR", head, 13u);
	RldMk_TestChunk(&png, "tEXt", (const u8 *)"Comment\0rldpack self-test", 25u);

	if (t->colorType == 3u)
	{
		u8 palette[3u * 256u];
		u8 alpha[6];
		u32 i;

		for (i = 0; i < t->paletteCount; i++)
		{
			palette[3u * i + 0u] = RldMk_TestPalette(i, 0u);
			palette[3u * i + 1u] = RldMk_TestPalette(i, 1u);
			palette[3u * i + 2u] = RldMk_TestPalette(i, 2u);
		}
		RldMk_TestChunk(&png, "PLTE", palette, 3u * t->paletteCount);

		if (t->trns)
		{
			for (i = 0; i < 6u; i++)
			{
				alpha[i] = (u8)(i * 40u);
			}
			RldMk_TestChunk(&png, "tRNS", alpha, (t->paletteCount < 6u) ? t->paletteCount : 6u);
		}
	}
	else if (t->trns && ((t->colorType == 0u) || (t->colorType == 2u)))
	{
		u8 key[6];

		for (c = 0; c < 3u; c++)
		{
			const u32 v = RldMk_TestSample(t, 1u, 0u, c);

			key[2u * c] = (u8)(v >> 8);
			key[2u * c + 1u] = (u8)v;
		}
		RldMk_TestChunk(&png, "tRNS", key, (t->colorType == 0u) ? 2u : 6u);
	}

	// Two IDAT: the reader joins them.
	RldMk_TestChunk(&png, "IDAT", idat.data, (u32)(idat.size / 2u));
	RldMk_TestChunk(&png, "IDAT", &idat.data[idat.size / 2u], (u32)(idat.size - idat.size / 2u));
	RldMk_TestChunk(&png, "IEND", NULL, 0u);
	free(idat.data);

	*sizeOut = png.size;
	return png.data;
}

// Decoded as expected, pixel for pixel?
static int RldMk_TestDecodes(const struct RldMkTestImage *t, const u8 *png, size_t size)
{
	struct RldPngImage image;
	const char *why = NULL;
	int ok;
	u32 x;
	u32 y;

	ok = (png != NULL) && (RldPng_Decode(png, size, &image, &why) == RLDPNG_OK) && (image.width == t->width) && (image.height == t->height);
	for (y = 0; ok && (y < t->height); y++)
	{
		for (x = 0; ok && (x < t->width); x++)
		{
			u8 want[4];

			RldMk_TestExpected(t, x, y, want);
			ok = (memcmp(&image.rgba[((size_t)y * t->width + x) * 4u], want, 4u) == 0);
		}
	}

	if (png != NULL)
	{
		RldPng_Free(&image);
	}
	return ok;
}

// The samples the dynamic-Huffman stream below holds: gray, 8 bit, 32 x 16, filter 0.
static u8 RldMk_TestDynamicSample(u32 x, u32 y)
{
	return (u8)(((((x * x + 3u * y * y + x * y) % 7u) < 2u) ? 200u : 40u) + ((x ^ y) & 3u));
}

static const u8 s_rldMkTestDynamic[142] = {
	0x78, 0xDA, 0x8D, 0x51, 0xD9, 0x0D, 0x43, 0x31, 0x08, 0xF3, 0x38, 0x04, 0xF6, 0xDF, 0x81, 0x63,
	0xA9, 0xC7, 0x51, 0xA9, 0x7D, 0x57, 0xD4, 0x0F, 0xCB, 0x52, 0x20, 0xD8, 0x06, 0xA8, 0xB1, 0xD0,
	0xF2, 0xD0, 0x95, 0x6C, 0x1E, 0x94, 0xAC, 0xE6, 0xF9, 0xC6, 0x51, 0x35, 0x2C, 0x12, 0x4F, 0xF0,
	0xA2, 0xE0, 0x66, 0x95, 0x66, 0xEB, 0x37, 0x71, 0x70, 0xFE, 0xA9, 0xFE, 0x44, 0xCD, 0x4A, 0xD4,
	0xAC, 0x44, 0xCD, 0x4A, 0x60, 0xFA, 0xDF, 0x81, 0xD2, 0xDC, 0xE1, 0xE3, 0x21, 0xC6, 0x83, 0x8E,
	0x07, 0x1B, 0x0F, 0x3E, 0x1E, 0x46, 0x7F, 0x8D, 0x3E, 0x8D, 0xBE, 0x8C, 0x7E, 0x31, 0xA2, 0x33,
	0x98, 0x76, 0x06, 0xB7, 0xCE, 0x10, 0xDE, 0x19, 0xB4, 0x6A, 0xF8, 0xF6, 0x3E, 0xCF, 0xC1, 0x55,
	0xF3, 0xEA, 0x07, 0xFB, 0x0C, 0x7F, 0xEE, 0x61, 0xBB, 0x4B, 0xFC, 0xEE, 0xFD, 0xE9, 0x26, 0xB8,
	0xDF, 0xEF, 0x7C, 0x5B, 0xDC, 0xEF, 0x77, 0x9E, 0x73, 0x00, 0xF7, 0x89, 0x92, 0x21,
};

// Rewrites the CRC of the chunk at `at` after a change of its data.
static void RldMk_TestPatchCrc(u8 *png, size_t at)
{
	const u32 length = RldPng_BE32(&png[at]);

	RldMk_PutBE32(&png[at + 8u + length], RldPng_Crc(&png[at + 4u], (size_t)length + 4u));
}

static int RldMk_TestRefused(const u8 *png, size_t size, int expected)
{
	struct RldPngImage image;
	const char *why = NULL;
	const int result = RldPng_Decode(png, size, &image, &why);

	if (result == RLDPNG_OK)
	{
		RldPng_Free(&image);
	}
	return (result == expected) && (why != NULL) && (image.rgba == NULL);
}

// Adam7 as PNG section 8.2 writes it, typed here and not taken from the
// reader: per pass {first column, first row, column step, row step}, and the
// pass sizes of a 9 x 9 picture (columns, rows).
static int RldMk_TestAdam7Geometry(void)
{
	static const u8 spec[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
	static const u32 nine[7][2] = {{2, 2}, {1, 2}, {3, 1}, {2, 3}, {5, 2}, {4, 5}, {9, 4}};
	struct RldPngImage image;
	u32 pass;
	int ok = 1;

	memset(&image, 0, sizeof(image));
	image.width = 9u;
	image.height = 9u;
	image.interlace = 1u;
	for (pass = 0; pass < 7u; pass++)
	{
		u32 w;
		u32 h;
		const u8 *g = RldPng_Pass(&image, pass, &w, &h);

		ok = ok && (memcmp(g, spec[pass], 4u) == 0) && (w == nine[pass][0]) && (h == nine[pass][1]);
	}

	return ok;
}

// The picture t interlaced (Adam7) and not: both read as the formula says,
// and pixel for pixel the same as each other.
static int RldMk_TestInterlaced(const struct RldMkTestImage *t)
{
	struct RldMkTestImage both[2];
	struct RldPngImage image[2];
	const char *why = NULL;
	int ok = 1;
	int k;

	memset(image, 0, sizeof(image));
	for (k = 0; k < 2; k++)
	{
		size_t size = 0;
		u8 *png;

		both[k] = *t;
		both[k].interlaced = k;
		png = RldMk_TestPng(&both[k], &size);
		ok = ok && RldMk_TestDecodes(&both[k], png, size) && (png != NULL) && (RldPng_Decode(png, size, &image[k], &why) == RLDPNG_OK) &&
		     (image[k].interlace == (u32)k);
		free(png);
	}

	ok = ok && (image[0].rgba != NULL) && (image[1].rgba != NULL) &&
	     (memcmp(image[0].rgba, image[1].rgba, (size_t)t->width * t->height * 4u) == 0);
	RldPng_Free(&image[0]);
	RldPng_Free(&image[1]);
	return ok;
}

static void RldMk_SelfTestPng(void)
{
	static const struct
	{
		u32 width, height, colorType, depth;
		int mode, trns;
		u32 paletteCount;
		const char *name;
	} cases[15] = {
		{13u, 7u, 3u, 4u, 0, 1, 11u, "png: palette 4 bit, tRNS, stored blocks"},
		{17u, 5u, 0u, 1u, 0, 1, 0u, "png: gray 1 bit, tRNS, stored blocks"},
		{9u, 4u, 0u, 2u, 1, 0, 0u, "png: gray 2 bit, fixed Huffman"},
		{5u, 4u, 0u, 16u, 1, 0, 0u, "png: gray 16 bit, fixed Huffman"},
		{9u, 6u, 2u, 8u, 1, 1, 0u, "png: RGB 8 bit, tRNS, fixed Huffman"},
		{7u, 3u, 4u, 8u, 0, 0, 0u, "png: gray and alpha 8 bit, stored blocks"},
		{4u, 2u, 6u, 16u, 1, 0, 0u, "png: RGBA 16 bit, fixed Huffman"},
		{6u, 5u, 3u, 1u, 1, 0, 2u, "png: palette 1 bit, fixed Huffman"},
		{11u, 6u, 3u, 8u, 1, 1, 200u, "png: palette 8 bit, tRNS, fixed Huffman"},
		{8u, 5u, 3u, 2u, 0, 1, 4u, "png: palette 2 bit, tRNS, stored blocks"},
		{9u, 5u, 0u, 8u, 1, 1, 0u, "png: gray 8 bit, tRNS, fixed Huffman"},
		{6u, 4u, 0u, 16u, 0, 1, 0u, "png: gray 16 bit, tRNS, stored blocks"},
		{5u, 4u, 2u, 16u, 1, 1, 0u, "png: RGB 16 bit, tRNS, fixed Huffman"},
		{6u, 3u, 4u, 16u, 0, 0, 0u, "png: gray and alpha 16 bit, stored blocks"},
		{7u, 4u, 6u, 8u, 1, 0, 0u, "png: RGBA 8 bit, fixed Huffman"},
	};
	struct RldMkTestImage t;
	size_t size = 0;
	u8 *png;
	int c;

	for (c = 0; c < 15; c++)
	{
		memset(&t, 0, sizeof(t));
		t.width = cases[c].width;
		t.height = cases[c].height;
		t.colorType = cases[c].colorType;
		t.depth = cases[c].depth;
		t.mode = cases[c].mode;
		t.trns = cases[c].trns;
		t.paletteCount = cases[c].paletteCount;
		png = RldMk_TestPng(&t, &size);
		RldMk_Test(RldMk_TestDecodes(&t, png, size), cases[c].name, "the pixels differ");

		// With tRNS, transparency has to arrive: the key color or the
		// palette entries with alpha 0 (a portrait's background) at alpha 0.
		if (t.trns && (png != NULL))
		{
			struct RldPngImage image;
			const char *why = NULL;
			u32 clear = 0;
			u32 i;
			char name[96];

			if (RldPng_Decode(png, size, &image, &why) == RLDPNG_OK)
			{
				for (i = 0; i < (t.width * t.height); i++)
				{
					clear += (image.rgba[4u * i + 3u] == 0u) ? 1u : 0u;
				}
				RldPng_Free(&image);
			}
			snprintf(name, sizeof(name), "%s: transparent pixels arrive", cases[c].name);
			RldMk_Test(clear > 0u, name, "none");
		}
		free(png);

		// The same interlaced (Adam7): the pixels of the image without.
		{
			char name[96];

			snprintf(name, sizeof(name), "%s, interlaced", cases[c].name);
			RldMk_Test(RldMk_TestInterlaced(&t), name, "the pixels differ");
		}
	}

	RldMk_Test(RldMk_TestAdam7Geometry(), "png: Adam7 passes as the specification has them, the pass sizes of 9 x 9", "not so");

	// Adam7 at sizes where passes are empty (1 x 1: only pass 1; 5 x 1: no
	// pass below row 0) or end early, and picture A.
	{
		static const u32 sizes[5][2] = {{1u, 1u}, {5u, 1u}, {1u, 6u}, {3u, 2u}, {9u, 9u}};

		for (c = 0; c < 5; c++)
		{
			char name[96];

			memset(&t, 0, sizeof(t));
			t.width = sizes[c][0];
			t.height = sizes[c][1];
			t.colorType = 2u;
			t.depth = 8u;
			t.mode = c & 1;
			snprintf(name, sizeof(name), "png: RGB 8 bit, %u x %u, interlaced", t.width, t.height);
			RldMk_Test(RldMk_TestInterlaced(&t), name, "the pixels differ");
		}

		memset(&t, 0, sizeof(t));
		t.width = 60u;
		t.height = 40u;
		t.colorType = 6u;
		t.depth = 8u;
		t.pictureA = 1;
		t.mode = 1;
		RldMk_Test(RldMk_TestInterlaced(&t), "png: picture A interlaced, the pixels of the one without", "the pixels differ");
	}

	// Picture A in both block kinds.
	memset(&t, 0, sizeof(t));
	t.width = 60u;
	t.height = 40u;
	t.colorType = 6u;
	t.depth = 8u;
	t.pictureA = 1;
	for (c = 0; c < 2; c++)
	{
		t.mode = c;
		png = RldMk_TestPng(&t, &size);
		RldMk_Test(RldMk_TestDecodes(&t, png, size), (c == 0) ? "png: picture A, stored blocks" : "png: picture A, fixed Huffman with matches",
		           "the pixels differ");
		free(png);
	}

	// A dynamic-Huffman block.
	{
		struct RldMkBuffer file;
		struct RldPngImage image;
		const char *why = NULL;
		u8 head[13];
		int ok;
		u32 x;
		u32 y;

		memset(&file, 0, sizeof(file));
		memset(&image, 0, sizeof(image));
		RldMk_BufferBytes(&file, s_rldPngSignature, 8u);
		RldMk_PutBE32(&head[0], 32u);
		RldMk_PutBE32(&head[4], 16u);
		head[8] = 8u;
		head[9] = 0u;
		head[10] = 0u;
		head[11] = 0u;
		head[12] = 0u;
		RldMk_TestChunk(&file, "IHDR", head, 13u);
		RldMk_TestChunk(&file, "IDAT", s_rldMkTestDynamic, (u32)sizeof(s_rldMkTestDynamic));
		RldMk_TestChunk(&file, "IEND", NULL, 0u);

		ok = (file.data != NULL) && (RldPng_Decode(file.data, file.size, &image, &why) == RLDPNG_OK);
		for (y = 0; ok && (y < 16u); y++)
		{
			for (x = 0; ok && (x < 32u); x++)
			{
				const u8 *pixel = &image.rgba[(y * 32u + x) * 4u];
				const u8 v = RldMk_TestDynamicSample(x, y);

				ok = (pixel[0] == v) && (pixel[1] == v) && (pixel[2] == v) && (pixel[3] == 255u);
			}
		}
		if ((file.data != NULL) && (image.rgba != NULL))
		{
			RldPng_Free(&image);
		}
		RldMk_Test(ok, "png: a dynamic-Huffman block", "%s", (why != NULL) ? why : "the pixels differ");

		// The same stream cut short.
		if (file.data != NULL)
		{
			struct RldMkBuffer cut;

			memset(&cut, 0, sizeof(cut));
			RldMk_BufferBytes(&cut, file.data, 8u + 25u);
			RldMk_TestChunk(&cut, "IDAT", s_rldMkTestDynamic, 100u);
			RldMk_TestChunk(&cut, "IEND", NULL, 0u);
			RldMk_Test((cut.data != NULL) && RldMk_TestRefused(cut.data, cut.size, RLDPNG_BAD), "png: a cut dynamic block refused", "accepted");
			free(cut.data);
		}
		free(file.data);
	}

	// THE REFUSALS, each from picture A with one thing broken. IHDR lies at 8,
	// its data at 16, its CRC at 29; tEXt follows at 33.
	t.mode = 1;
	png = RldMk_TestPng(&t, &size);
	if (png != NULL)
	{
		u8 *bad = (u8 *)malloc(size + 64u);
		const u32 textLength = RldPng_BE32(&png[33]);
		const size_t idatAt = 33u + 12u + textLength;

		if (bad != NULL)
		{
			memcpy(bad, png, size);
			bad[idatAt + 8u + 5u] ^= 0x10u;
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_BAD), "png: a wrong CRC refused", "accepted");

			memcpy(bad, png, size);
			bad[16u + 12u] = 2u;
			RldMk_TestPatchCrc(bad, 8u);
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_BAD), "png: interlace method 2 refused", "accepted");

			memcpy(bad, png, size);
			RldMk_PutBE32(&bad[16u], 5000u);
			RldMk_TestPatchCrc(bad, 8u);
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_TOO_LARGE), "png: 5000 pixels wide refused before any memory", "not so");

			// 4096 x 4096 with the data of 60 x 40: far too short, refused
			// before the 64 MB of the image are taken.
			memcpy(bad, png, size);
			RldMk_PutBE32(&bad[16u], 4096u);
			RldMk_PutBE32(&bad[20u], 4096u);
			RldMk_TestPatchCrc(bad, 8u);
			{
				struct RldPngImage image;
				const char *why = NULL;
				const int result = RldPng_Decode(bad, size, &image, &why);

				RldMk_Test((result == RLDPNG_BAD) && (why != NULL) && (strncmp(why, "the image data is far too short", 31) == 0),
				           "png: 4096 x 4096 with a few bytes of data refused before its memory", "%s", (why != NULL) ? why : "accepted");
				if (result == RLDPNG_OK)
				{
					RldPng_Free(&image);
				}
			}

			memcpy(bad, png, size);
			bad[16u + 9u] = 2u;
			bad[16u + 8u] = 4u;
			RldMk_TestPatchCrc(bad, 8u);
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_BAD), "png: RGB with 4 bits refused", "accepted");

			memcpy(bad, png, size);
			RldMk_PutBE32(&bad[idatAt], 0x7FFFFFF0u);
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_BAD), "png: a chunk longer than the file refused", "accepted");

			memcpy(bad, png, size);
			RldMk_PutBE32(&bad[8u], 12u);
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_BAD), "png: IHDR of 12 bytes refused", "accepted");

			RldMk_Test(RldMk_TestRefused(png, size - 12u, RLDPNG_BAD), "png: no IEND refused", "accepted");
			RldMk_Test(RldMk_TestRefused(png, 20u, RLDPNG_BAD), "png: cut in the header refused", "accepted");

			memcpy(bad, png, size);
			bad[1] = 'Q';
			RldMk_Test(RldMk_TestRefused(bad, size, RLDPNG_BAD), "png: no signature refused", "accepted");

			// An unknown critical chunk in front of IEND.
			memcpy(bad, png, size - 12u);
			{
				struct RldMkBuffer more;

				memset(&more, 0, sizeof(more));
				RldMk_BufferBytes(&more, bad, size - 12u);
				RldMk_TestChunk(&more, "ABCD", (const u8 *)"x", 1u);
				RldMk_TestChunk(&more, "IEND", NULL, 0u);
				RldMk_Test((more.data != NULL) && RldMk_TestRefused(more.data, more.size, RLDPNG_BAD), "png: an unknown critical chunk refused", "accepted");
				free(more.data);
			}
			free(bad);
		}
		free(png);
	}

	t.breakAdler = 1;
	png = RldMk_TestPng(&t, &size);
	RldMk_Test((png != NULL) && RldMk_TestRefused(png, size, RLDPNG_BAD), "png: a wrong Adler-32 refused", "accepted");
	free(png);
	t.breakAdler = 0;

	t.badFilter = 1;
	png = RldMk_TestPng(&t, &size);
	RldMk_Test((png != NULL) && RldMk_TestRefused(png, size, RLDPNG_BAD), "png: an unknown row filter refused", "accepted");
	free(png);
}

// --- --icon --------------------------------------------------------------------------

// Picture A decoded, for the icon tests and the golden hash. 1 = read.
static int RldMk_TestPictureA(struct RldPngImage *image)
{
	struct RldMkTestImage t;
	const char *why = NULL;
	size_t size = 0;
	u8 *png;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(image, 0, sizeof(*image));
	t.width = 60u;
	t.height = 40u;
	t.colorType = 6u;
	t.depth = 8u;
	t.mode = 1;
	t.pictureA = 1;

	png = RldMk_TestPng(&t, &size);
	ok = (png != NULL) && (RldPng_Decode(png, size, image, &why) == RLDPNG_OK);
	free(png);
	return ok;
}

static void RldMk_SelfTestIcon(const struct RldPngImage *a)
{
	static struct RldMkIcon icon;
	static struct RldMkIcon again;
	static u8 pixels[RLDMK_ICON_TEXELS * 4u];
	struct RldPngImage small;
	u8 broken[RLDCHAR_ICON_BYTES + 4u];
	int ok;
	u32 i;

	ok = RldMk_MakeIcon(a, &icon) && RldMk_MakeIcon(a, &again) && (memcmp(icon.cicn, again.cicn, sizeof(icon.cicn)) == 0);
	RldMk_Test(ok && (RldChar_CheckIcon(icon.cicn, sizeof(icon.cicn)) == NULL) && (Rld_ReadLE16(&icon.cicn[0x04]) == 44u) &&
	               (Rld_ReadLE16(&icon.cicn[0x06]) == 26u),
	           "icon: CICN 1, 4 bit, 44 x 26, 612 bytes, twice the same", "not so");
	RldMk_Test(ok && (icon.crop[0] == 0u) && (icon.crop[1] == 2u) && (icon.crop[2] == 60u) && (icon.crop[3] == 35u) && (icon.scaled[0] == 43u) &&
	               (icon.scaled[1] == 25u),
	           "icon: 60 x 40 cut to 60 x 35 (43:25) in the middle, scaled to 43 x 25", "cut %u x %u at %u,%u, scaled %u x %u", icon.crop[2], icon.crop[3],
	           icon.crop[0], icon.crop[1], icon.scaled[0], icon.scaled[1]);
	{
		int same = 1;

		for (i = 0; i < RLDCHAR_ICON_HEIGHT; i++)
		{
			same = same && (icon.index[i][43] == icon.index[i][42]);
		}
		same = same && (memcmp(icon.index[25], icon.index[24], RLDCHAR_ICON_WIDTH) == 0);
		RldMk_Test(ok && same, "icon: column 43 repeats column 42, row 25 repeats row 24", "not so");
	}
	RldMk_Test(ok && (icon.index[0][0] == 0u) && (icon.index[25][43] == 0u) && (icon.index[13][22] != 0u) && (icon.index[3][22] == 0u) &&
	               (icon.colorCount >= 2u) && (icon.colorCount <= 15u) && (icon.clut[0] == 0u),
	           "icon: corners and the half-covered top transparent, at most 15 colors", "%u colors, corner %u, middle %u, top %u", icon.colorCount,
	           (u32)icon.index[0][0], (u32)icon.index[13][22], (u32)icon.index[3][22]);
	{
		int used = 1;

		for (i = 0; i < (u32)RLDMK_ICON_TEXELS; i++)
		{
			const u32 index = icon.index[i / RLDCHAR_ICON_WIDTH][i % RLDCHAR_ICON_WIDTH];
			const u32 word = Rld_ReadLE16(&icon.cicn[RLDCHAR_ICON_WORDS_AT + 2u * (i / 4u)]);

			if ((index > icon.colorCount) || (((word >> (4u * (i % 4u))) & 15u) != index) || ((index != 0u) && (icon.clut[index] == 0u)))
			{
				used = 0;
			}
		}
		RldMk_Test(ok && used, "icon: texel words, low nibble first, no opaque texel on 0x0000", "not so");
	}

	// Black and white, exactly 43 x 25: taken one to one, 0x0421 and 0x7FFF.
	memset(&small, 0, sizeof(small));
	small.width = RLDMK_ICON_SHOWN_WIDTH;
	small.height = RLDMK_ICON_SHOWN_HEIGHT;
	small.rgba = pixels;
	for (i = 0; i < RLDMK_ICON_SHOWN_TEXELS; i++)
	{
		const u8 v = (u8)(((i % RLDMK_ICON_SHOWN_WIDTH) < 21u) ? 0u : 255u);

		pixels[4u * i + 0u] = v;
		pixels[4u * i + 1u] = v;
		pixels[4u * i + 2u] = v;
		pixels[4u * i + 3u] = 255u;
	}
	ok = RldMk_MakeIcon(&small, &icon) && (icon.opaque == (u32)RLDMK_ICON_TEXELS) && (icon.colorCount == 2u) && (icon.crop[2] == 43u) &&
	     (icon.crop[3] == 25u) && (icon.scaled[0] == 43u) && (icon.scaled[1] == 25u);
	RldMk_Test(ok && (icon.clut[1] == RLDCHAR_ICON_BLACK) && (icon.clut[2] == 0x7FFFu) && (icon.index[0][20] == 1u) && (icon.index[24][21] == 2u) &&
	               (icon.index[0][43] == 2u) && (icon.index[25][0] == 1u),
	           "icon: 43 x 25 one to one, black is 0x0421, white 0x7FFF", "%u colors, clut %04x %04x", icon.colorCount, (u32)icon.clut[1],
	           (u32)icon.clut[2]);

	// Exactly 44 x 26: all of CICN one to one, as before the fit to 43 x 25.
	small.width = RLDCHAR_ICON_WIDTH;
	small.height = RLDCHAR_ICON_HEIGHT;
	for (i = 0; i < (u32)RLDMK_ICON_TEXELS; i++)
	{
		const u8 v = (u8)(((i % RLDCHAR_ICON_WIDTH) < 22u) ? 0u : 255u);

		pixels[4u * i + 0u] = v;
		pixels[4u * i + 1u] = v;
		pixels[4u * i + 2u] = v;
		pixels[4u * i + 3u] = (u8)((i < RLDCHAR_ICON_WIDTH) ? 0u : 255u);
	}
	ok = RldMk_MakeIcon(&small, &icon) && (icon.opaque == (u32)(RLDMK_ICON_TEXELS - RLDCHAR_ICON_WIDTH)) && (icon.colorCount == 2u) &&
	     (icon.scaled[0] == 44u) && (icon.scaled[1] == 26u);
	RldMk_Test(ok && (icon.index[0][0] == 0u) && (icon.index[1][21] == 1u) && (icon.index[1][22] == 2u) && (icon.index[25][43] == 2u),
	           "icon: 44 x 26 one to one, the whole of CICN", "%u colors, %u opaque", icon.colorCount, icon.opaque);

	// 22 x 13: smaller than 43 x 25, enlarged.
	small.width = 22u;
	small.height = 13u;
	for (i = 0; i < (22u * 13u); i++)
	{
		const u8 v = (u8)(((i % 22u) < 11u) ? 0u : 255u);

		pixels[4u * i + 0u] = v;
		pixels[4u * i + 1u] = v;
		pixels[4u * i + 2u] = v;
		pixels[4u * i + 3u] = 255u;
	}
	ok = RldMk_MakeIcon(&small, &icon) && (icon.opaque == (u32)RLDMK_ICON_TEXELS) && (icon.crop[2] == 22u) && (icon.crop[3] == 13u) &&
	     (icon.index[0][0] != icon.index[0][42]);
	RldMk_Test(ok, "icon: 22 x 13 enlarged to 43 x 25", "%u opaque, cut %u x %u", icon.opaque, icon.crop[2], icon.crop[3]);

	// Coverage: alpha 127 is transparent, 128 is not.
	for (i = 0; i < (22u * 13u); i++)
	{
		pixels[4u * i + 3u] = 127u;
	}
	ok = RldMk_MakeIcon(&small, &icon) && (icon.opaque == 0u);
	for (i = 0; i < (22u * 13u); i++)
	{
		pixels[4u * i + 3u] = 128u;
	}
	ok = ok && RldMk_MakeIcon(&small, &icon) && (icon.opaque == (u32)RLDMK_ICON_TEXELS);
	RldMk_Test(ok, "icon: below half coverage transparent", "not so");

	// CICN-1..3.
	RldMk_MakeIcon(a, &icon);
	memcpy(broken, icon.cicn, RLDCHAR_ICON_BYTES);
	Rld_WriteLE16(&broken[0x00], 2u);
	ok = (RldChar_CheckIcon(broken, RLDCHAR_ICON_BYTES) != NULL) && (strncmp(RldChar_CheckIcon(broken, RLDCHAR_ICON_BYTES), "CICN-1", 6) == 0);
	memcpy(broken, icon.cicn, RLDCHAR_ICON_BYTES);
	Rld_WriteLE16(&broken[0x02], 8u);
	ok = ok && (strncmp(RldChar_CheckIcon(broken, RLDCHAR_ICON_BYTES), "CICN-1", 6) == 0);
	memcpy(broken, icon.cicn, RLDCHAR_ICON_BYTES);
	Rld_WriteLE16(&broken[0x04], 43u);
	ok = ok && (strncmp(RldChar_CheckIcon(broken, RLDCHAR_ICON_BYTES), "CICN-2", 6) == 0);
	memcpy(broken, icon.cicn, RLDCHAR_ICON_BYTES);
	ok = ok && (strncmp(RldChar_CheckIcon(broken, RLDCHAR_ICON_BYTES - 1u), "CICN-3", 6) == 0) &&
	     (strncmp(RldChar_CheckIcon(broken, RLDCHAR_ICON_BYTES + 1u), "CICN-3", 6) == 0) && (strncmp(RldChar_CheckIcon(broken, 4u), "CICN-3", 6) == 0);
	RldMk_Test(ok, "icon: CICN-1..3 refuse version, depth, size, length", "not so");
}

// --- --icon-background, --icon-fit, --icon-frame, the retail portrait -------------------

// Test picture B, 60 x 40, every pixel opaque: a plain blue with a little noise
// around a round figure with a black outline 2 pixels wide that runs off the
// bottom edge. Inside: a shirt at the bottom in the background's own color
// (closed off from the corners by the outline), two eyes (white, black
// pupil), a mouth in the background's own color, skin. 1 = background.
#define RLDMK_TEST_BG_R 40u
#define RLDMK_TEST_BG_G 160u
#define RLDMK_TEST_BG_B 220u

static int RldMk_TestPixelB(u32 x, u32 y, u8 rgba[4])
{
	const int dx = 2 * (int)x - 59;
	const int dy = 2 * (int)y - 60;
	const int r2 = dx * dx + dy * dy;
	const int e1 = (2 * (int)x - 46) * (2 * (int)x - 46) + (2 * (int)y - 44) * (2 * (int)y - 44);
	const int e2 = (2 * (int)x - 72) * (2 * (int)x - 72) + (2 * (int)y - 44) * (2 * (int)y - 44);
	const int noise = (int)((x * 7u + y * 3u) % 7u) - 3;

	rgba[3] = 255u;
	if (r2 > 34 * 34)
	{
		rgba[0] = (u8)((int)RLDMK_TEST_BG_R + noise);
		rgba[1] = (u8)((int)RLDMK_TEST_BG_G + noise);
		rgba[2] = (u8)((int)RLDMK_TEST_BG_B + noise);
		return 1;
	}
	if (r2 > 30 * 30)
	{
		rgba[0] = rgba[1] = rgba[2] = 0u;
	}
	else if ((y >= 32u) || ((x >= 28u) && (x <= 31u) && (y >= 27u) && (y <= 28u)))
	{
		rgba[0] = (u8)RLDMK_TEST_BG_R;
		rgba[1] = (u8)RLDMK_TEST_BG_G;
		rgba[2] = (u8)RLDMK_TEST_BG_B;
	}
	else if ((e1 <= 4) || (e2 <= 4))
	{
		rgba[0] = rgba[1] = rgba[2] = 0u;
	}
	else if ((e1 <= 64) || (e2 <= 64))
	{
		rgba[0] = rgba[1] = rgba[2] = 255u;
	}
	else
	{
		rgba[0] = 230u;
		rgba[1] = 180u;
		rgba[2] = 120u;
	}
	return 0;
}

// Test picture C, 43 x 25: a transparent border 2 pixels wide, inside it an
// opaque box of near-black (8, 8, 8) - the box a portrait exported from the
// PS1 brings along - and on it a figure with a dark grey outline (40, 40, 40),
// an eye whose pupil has the box's own color. 0 = transparent, 1 = box,
// 2 = outline, 3 = figure.
static int RldMk_TestPixelC(u32 x, u32 y, u8 rgba[4])
{
	const int ex = 2 * (int)x - 42;
	const int ey = 2 * (int)y - 24;
	const int e = ex * ex + 3 * ey * ey;
	const int eye = ((int)x - 24) * ((int)x - 24) + ((int)y - 11) * ((int)y - 11);

	rgba[3] = 255u;
	if ((x < 2u) || (x > 40u) || (y < 2u) || (y > 22u))
	{
		rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0u;
		return 0;
	}
	if (e > 26 * 26)
	{
		rgba[0] = rgba[1] = rgba[2] = 8u;
		return 1;
	}
	if (e > 22 * 22)
	{
		rgba[0] = rgba[1] = rgba[2] = 40u;
		return 2;
	}
	if (eye == 0)
	{
		rgba[0] = rgba[1] = rgba[2] = 8u;
	}
	else if (eye <= 4)
	{
		rgba[0] = rgba[1] = rgba[2] = 255u;
	}
	else
	{
		rgba[0] = 170u;
		rgba[1] = 120u;
		rgba[2] = 200u;
	}
	return 3;
}

// A transparent 200 x 100 picture with one opaque rectangle (for --icon-fit).
static void RldMk_TestRect(u8 *rgba, u32 x0, u32 y0, u32 w, u32 h)
{
	u32 x;
	u32 y;

	memset(rgba, 0, 200u * 100u * 4u);
	for (y = y0; y < y0 + h; y++)
	{
		for (x = x0; x < x0 + w; x++)
		{
			u8 *p = &rgba[(y * 200u + x) * 4u];

			p[0] = (u8)(60u + x);
			p[1] = (u8)(40u + y);
			p[2] = 90u;
			p[3] = 255u;
		}
	}
}

// Texels [x0, x1] x [y0, y1] of the 43 x 25 opaque and every other one shown
// transparent?
static int RldMk_TestOpaqueBox(const struct RldMkIcon *icon, u32 x0, u32 y0, u32 x1, u32 y1)
{
	u32 x;
	u32 y;

	for (y = 0; y < RLDMK_ICON_SHOWN_HEIGHT; y++)
	{
		for (x = 0; x < RLDMK_ICON_SHOWN_WIDTH; x++)
		{
			const int inside = (x >= x0) && (x <= x1) && (y >= y0) && (y <= y1);

			if ((icon->index[y][x] != 0u) != inside)
			{
				return 0;
			}
		}
	}
	return 1;
}

// A TIM block for the synthetic shared.vrm: 16-bit, w x h words at x, y.
static void RldMk_TestTim(struct RldMkBuffer *vrm, u32 x, u32 y, u32 w, u32 h, const u16 *words)
{
	u8 rect[8];
	u32 i;

	RldMk_BufferLE32(vrm, 0x10u);
	RldMk_BufferLE32(vrm, 2u);
	RldMk_BufferLE32(vrm, 12u + 2u * w * h);
	Rld_WriteLE16(&rect[0], x);
	Rld_WriteLE16(&rect[2], y);
	Rld_WriteLE16(&rect[4], w);
	Rld_WriteLE16(&rect[6], h);
	RldMk_BufferBytes(vrm, rect, 8u);
	for (i = 0; i < (w * h); i++)
	{
		Rld_WriteLE16(rect, words[i]);
		RldMk_BufferBytes(vrm, rect, 2u);
	}
}

static void RldMk_SelfTestIconOptions(const struct RldPngImage *a)
{
	static struct RldMkIcon icon;
	static struct RldMkIcon plain;
	static u8 pixels[200u * 100u * 4u];
	struct RldMkIconOptions options = s_rldMkIconDefault;
	struct RldPngImage image;
	u32 used = 0;
	u32 cleared = 0;
	u32 color = 0;
	u32 expected = 0;
	u32 x;
	u32 y;
	u32 thin = 0;
	int ok;
	int k;

	memset(&image, 0, sizeof(image));
	image.rgba = pixels;

	// THE SWITCHES. The defaults are what make-char did before them.
	RldMk_Test((options.background == RLDMK_ICON_BACKGROUND_ALPHA) && (options.tolerance == 24u) && (options.fit == RLDMK_ICON_FIT_NONE) &&
	               (options.frame == RLDMK_ICON_FRAME_NONE),
	           "icon options: defaults alpha, 24, none, none", "not so");
	RldMk_ResetSaid();
	ok = RldMk_IconSwitches("corners", "0", "fill", "retail", &options) && (options.background == RLDMK_ICON_BACKGROUND_CORNERS) &&
	     (options.tolerance == 0u) && (options.fit == RLDMK_ICON_FIT_FILL) && (options.frame == RLDMK_ICON_FRAME_RETAIL);
	ok = ok && RldMk_IconSwitches("auto", "128", "fit", "none", &options) && (options.background == RLDMK_ICON_BACKGROUND_AUTO) &&
	     (options.tolerance == 128u) && (options.fit == RLDMK_ICON_FIT_FIT) && (options.frame == RLDMK_ICON_FRAME_NONE) && (s_rldMkCodes[0] == '\0');
	RldMk_Test(ok, "icon options: every word taken", "said: %s", s_rldMkCodes);
	{
		static const char *const bad[4][4] = {{"Corners", NULL, NULL, NULL}, {NULL, "129", NULL, NULL}, {NULL, "-1", "stretch", NULL}, {NULL, "2x", NULL, "box"}};

		for (k = 0; k < 4; k++)
		{
			options = s_rldMkIconDefault;
			RldMk_ResetSaid();
			ok = !RldMk_IconSwitches(bad[k][0], bad[k][1], bad[k][2], bad[k][3], &options) && RldMk_Said("usage") &&
			     (memcmp(&options, &s_rldMkIconDefault, sizeof(options)) == 0);
			RldMk_Test(ok, (k == 0) ? "icon options: Corners refused (usage), nothing changed"
			                        : ((k == 1) ? "icon options: tolerance 129 refused" : ((k == 2) ? "icon options: tolerance -1 and fit stretch refused" : "icon options: tolerance 2x and frame box refused")),
			           "said: %s", s_rldMkCodes);
		}
		RldMk_ResetSaid();
	}

	// CORNERS on picture B: exactly the background goes - not the outline
	// that runs off the bottom edge, not the shirt in the background's color
	// that it closes off, not the mouth in that color, not the eyes.
	image.width = 60u;
	image.height = 40u;
	for (y = 0; y < 40u; y++)
	{
		for (x = 0; x < 60u; x++)
		{
			expected += (u32)RldMk_TestPixelB(x, y, &pixels[(y * 60u + x) * 4u]);
		}
	}
	options = s_rldMkIconDefault;
	options.background = RLDMK_ICON_BACKGROUND_CORNERS;
	ok = RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (used == RLDMK_ICON_BACKGROUND_CORNERS) && (cleared == expected);
	for (y = 0; ok && (y < 40u); y++)
	{
		for (x = 0; ok && (x < 60u); x++)
		{
			u8 want[4];
			const int background = RldMk_TestPixelB(x, y, want);

			ok = (pixels[(y * 60u + x) * 4u + 3u] == (background ? 0u : 255u)) && (memcmp(&pixels[(y * 60u + x) * 4u], want, 3u) == 0);
		}
	}
	RldMk_Test(ok && ((u32)abs((int)(color >> 16) - (int)RLDMK_TEST_BG_R) <= 2u) && ((u32)abs((int)((color >> 8) & 0xFFu) - (int)RLDMK_TEST_BG_G) <= 2u) &&
	               ((u32)abs((int)(color & 0xFFu) - (int)RLDMK_TEST_BG_B) <= 2u),
	           "icon corners: a noisy plain background goes, the outline at the edge, the shirt and mouth in its color and the eyes stay",
	           "%u of %u cleared, color %06X", cleared, expected, color);
	ok = RldMk_MakeIcon(&image, &icon) && (icon.index[0][0] == 0u) && (icon.index[0][42] == 0u) && (icon.index[24][0] == 0u) && (icon.index[24][21] != 0u) &&
	     (icon.index[12][21] != 0u);
	RldMk_Test(ok, "icon corners: the CICN has transparent corners and the shirt at the bottom", "corner %u, bottom %u", (u32)icon.index[0][0],
	           (u32)icon.index[24][21]);

	// Tolerance 0 on the noisy background: under half the voters match - nothing changes.
	for (y = 0; y < 40u; y++)
	{
		for (x = 0; x < 60u; x++)
		{
			(void)RldMk_TestPixelB(x, y, &pixels[(y * 60u + x) * 4u]);
		}
	}
	options.tolerance = 0u;
	ok = RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (cleared == 0u);
	for (x = 0; ok && (x < 60u * 40u); x++)
	{
		ok = (pixels[x * 4u + 3u] == 255u);
	}
	RldMk_Test(ok, "icon corners: tolerance 0 on a noisy background changes nothing", "%u cleared", cleared);

	// AUTO: corners for picture B (not one pixel transparent), alpha for C.
	options.tolerance = RLDMK_ICON_TOLERANCE;
	options.background = RLDMK_ICON_BACKGROUND_AUTO;
	ok = RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (used == RLDMK_ICON_BACKGROUND_CORNERS) && (cleared == expected);

	// CORNERS on picture C, one pixel per texel: the box goes through the
	// transparent border, the outline near its color and the pupil in its color stay.
	image.width = RLDMK_ICON_SHOWN_WIDTH;
	image.height = RLDMK_ICON_SHOWN_HEIGHT;
	expected = 0;
	for (y = 0; y < RLDMK_ICON_SHOWN_HEIGHT; y++)
	{
		for (x = 0; x < RLDMK_ICON_SHOWN_WIDTH; x++)
		{
			expected += (RldMk_TestPixelC(x, y, &pixels[(y * RLDMK_ICON_SHOWN_WIDTH + x) * 4u]) == 1) ? 1u : 0u;
		}
	}
	ok = ok && RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (used == RLDMK_ICON_BACKGROUND_ALPHA) && (cleared == 0u);
	ok = ok && RldMk_MakeIcon(&image, &plain);
	RldMk_Test(ok, "icon auto: corners for a picture without transparency, nothing for one with it", "used %u, %u cleared", used, cleared);

	options.background = RLDMK_ICON_BACKGROUND_CORNERS;
	ok = RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (cleared == expected) && (color == 0x080808u) && RldMk_MakeIcon(&image, &icon);
	for (y = 0; ok && (y < RLDMK_ICON_SHOWN_HEIGHT); y++)
	{
		for (x = 0; ok && (x < RLDMK_ICON_SHOWN_WIDTH); x++)
		{
			u8 want[4];
			const int kind = RldMk_TestPixelC(x, y, want);

			ok = ((icon.index[y][x] != 0u) == (kind >= 2)) && ((plain.index[y][x] != 0u) == (kind >= 1));
		}
	}
	RldMk_Test(ok && !thin && (icon.index[11][24] != 0u), "icon corners: a near-black box goes, the dark outline and the pupil in the box's color stay",
	           "%u of %u cleared, color %06X", cleared, expected, color);

	// Picture B with its background transparent: the black outline borders
	// the transparent part, but it is a line - it stays, nothing changes.
	image.width = 60u;
	image.height = 40u;
	for (y = 0; y < 40u; y++)
	{
		for (x = 0; x < 60u; x++)
		{
			u8 *p = &pixels[(y * 60u + x) * 4u];

			if (RldMk_TestPixelB(x, y, p))
			{
				p[0] = p[1] = p[2] = p[3] = 0u;
			}
		}
	}
	ok = RldMk_MakeIcon(&image, &plain) && RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (thin == RLDMK_BG_KEPT_LINE) && (cleared == 0u) &&
	     RldMk_MakeIcon(&image, &icon) && (memcmp(icon.cicn, plain.cicn, sizeof(icon.cicn)) == 0) && (icon.index[24][14] != 0u);
	RldMk_Test(ok, "icon corners: a black outline around a transparent background is a line - it stays", "kept %u, %u cleared", thin, cleared);

	// Thick parts of the subject in the outline's color, on transparent: a
	// black disc (k 0), and dark hair joined to a black outline above a face
	// (k 1) - round edges, not a box: they stay. An opaque picture of one
	// color (k 2): the step would take all of it - nothing changes. A bust on
	// transparent whose body runs off the bottom, opaque in both lower corners
	// (k 3): the body stays. A rectangular body below a round head, on
	// transparent (k 4): straight like a box, but not around the head - it stays.
	for (k = 0; k < 5; k++)
	{
		static const char *const names[5] = {"icon corners: a black disc on transparent is the subject - it stays",
		                                     "icon corners: dark hair at a black outline is the subject - it stays",
		                                     "icon corners: a picture of one color - the step would take all of it, nothing changes",
		                                     "icon corners: a body in two opaque corners of a transparent picture is the subject - it stays",
		                                     "icon corners: a rectangular body below a head is the subject - it stays"};
		static const u32 why[5] = {RLDMK_BG_KEPT_SHAPE, RLDMK_BG_KEPT_SHAPE, RLDMK_BG_KEPT_SUBJECT, RLDMK_BG_KEPT_SHAPE, RLDMK_BG_KEPT_AROUND};

		image.width = 64u;
		image.height = 64u;
		for (y = 0; y < 64u; y++)
		{
			for (x = 0; x < 64u; x++)
			{
				const int d = ((int)x - 32) * ((int)x - 32) + ((int)y - 34) * ((int)y - 34);
				u8 *p = &pixels[(y * 64u + x) * 4u];

				p[3] = 255u;
				if (k == 2)
				{
					p[0] = 200u;
					p[1] = 30u;
					p[2] = 30u;
				}
				else if (k == 4)
				{
					const int body = (x >= 12u) && (x < 52u) && (y >= 30u);
					const int head = (((int)x - 32) * ((int)x - 32) + ((int)y - 15) * ((int)y - 15)) <= 13 * 13;

					p[0] = 60u;
					p[1] = (u8)(body ? 60u : 200u);
					p[2] = (u8)(body ? 220u : 60u);
					p[3] = (u8)((body || head) ? 255u : 0u);
				}
				else if (k == 3)
				{
					const int body = (y >= 36u) && ((u32)abs((int)x - 32) < 10u + (y - 36u));
					const int head = (((int)x - 32) * ((int)x - 32) + ((int)y - 20) * ((int)y - 20)) <= 14 * 14;

					p[0] = (u8)(body ? 60u : 230u);
					p[1] = (u8)(body ? 90u : 180u);
					p[2] = (u8)(body ? 170u : 140u);
					p[3] = (u8)((body || head) ? 255u : 0u);
				}
				else if (d > 28 * 28)
				{
					p[0] = p[1] = p[2] = p[3] = 0u;
				}
				else if ((k == 0) || (d > 27 * 27))
				{
					p[0] = p[1] = p[2] = 0u;
				}
				else
				{
					p[0] = p[1] = p[2] = (u8)((y < 26u) ? 10u : 0u);
					if (y >= 26u)
					{
						p[0] = 230u;
						p[1] = 180u;
						p[2] = 140u;
					}
				}
			}
		}
		options = s_rldMkIconDefault;
		options.background = RLDMK_ICON_BACKGROUND_CORNERS;
		ok = RldMk_MakeIcon(&image, &plain) && RldMk_IconBackground(&image, &options, &used, &cleared, &color, &thin) && (thin == why[k]) &&
		     (cleared == 0u) && RldMk_MakeIcon(&image, &icon) && (memcmp(icon.cicn, plain.cicn, sizeof(icon.cicn)) == 0) && (icon.opaque > 0u);
		RldMk_Test(ok, names[k], "kept %u, %u cleared", thin, cleared);
	}
	options = s_rldMkIconDefault;
	options.background = RLDMK_ICON_BACKGROUND_CORNERS;

	// FIT and FILL: a tall and a wide rectangle; one factor for both axes.
	image.width = 200u;
	image.height = 100u;
	for (k = 0; k < 4; k++)
	{
		static const u32 rect[2][4] = {{100u, 20u, 40u, 50u}, {10u, 60u, 120u, 30u}};
		static const u32 place[4][4] = {{10u, 0u, 20u, 25u}, {0u, 15u, 41u, 10u}, {0u, 0u, 41u, 25u}, {0u, 0u, 41u, 25u}};
		static const u32 opaque[4][4] = {{10u, 0u, 29u, 24u}, {0u, 15u, 39u, 24u}, {0u, 0u, 40u, 24u}, {0u, 0u, 40u, 24u}};
		// The cuts: fit takes the extra above and at both sides (the wide one
		// 3 pixels more: 123 x 30 for 41 x 10); fill cuts the wide one at both
		// sides, the tall one around the face: 40 / 2 - 24 / 2 = 8 above.
		static const u32 crop[4][2] = {{100u, 20u}, {9u, 60u}, {100u, 28u}, {45u, 60u}};
		static const char *const names[4] = {"icon fit: a tall subject 20 x 25, centered in the head box, standing on the bottom row",
		                                     "icon fit: a wide subject 41 x 10 on the bottom row",
		                                     "icon fill: a tall subject fills the head box 41 x 25, cut around the face",
		                                     "icon fill: a wide subject fills the head box 41 x 25"};
		const u32 *r = rect[k & 1];

		RldMk_TestRect(pixels, r[0], r[1], r[2], r[3]);
		options = s_rldMkIconDefault;
		options.fit = (k < 2) ? RLDMK_ICON_FIT_FIT : RLDMK_ICON_FIT_FILL;
		ok = RldMk_MakeIconWith(&image, &options, &icon) && (memcmp(icon.place, place[k], sizeof(icon.place)) == 0) && (icon.crop[0] == crop[k][0]) &&
		     (icon.crop[1] == crop[k][1]) &&
		     (icon.subject[0] == r[0]) && (icon.subject[1] == r[1]) && (icon.subject[2] == r[2]) && (icon.subject[3] == r[3]) &&
		     RldMk_TestOpaqueBox(&icon, opaque[k][0], opaque[k][1], opaque[k][2], opaque[k][3]);
		// Never stretched: the cut has the shape of the texels it becomes,
		// to half a texel.
		{
			const s64 skew = (s64)icon.crop[2] * icon.scaled[1] - (s64)icon.crop[3] * icon.scaled[0];

			ok = ok && ((2 * ((skew < 0) ? -skew : skew)) <= (s64)((icon.crop[2] > icon.crop[3]) ? icon.crop[2] : icon.crop[3]));
		}
		RldMk_Test(ok, names[k], "place %u,%u %u x %u, cut %u x %u -> %u x %u", icon.place[0], icon.place[1], icon.place[2], icon.place[3], icon.crop[2], icon.crop[3],
		           icon.scaled[0], icon.scaled[1]);
	}

	// FRAME: around the tall subject, the frame 0x0400 (x 2..38, y 1..23, 2
	// wide at the sides) and inside it the box 0x8000; the subject over both.
	RldMk_TestRect(pixels, 100u, 20u, 40u, 50u);
	options = s_rldMkIconDefault;
	options.fit = RLDMK_ICON_FIT_FIT;
	options.frame = RLDMK_ICON_FRAME_RETAIL;
	ok = RldMk_MakeIconWith(&image, &options, &icon) && (RldChar_CheckIcon(icon.cicn, sizeof(icon.cicn)) == NULL);
	{
		const u32 f = icon.colorCount + 1u;
		const u32 b = icon.colorCount + 2u;

		ok = ok && (icon.clut[f] == 0x0400u) && (icon.clut[b] == 0x8000u) && (Rld_ReadLE16(&icon.cicn[0x08u + 2u * f]) == 0x0400u) &&
		     (icon.index[12][2] == f) && (icon.index[12][3] == f) && (icon.index[12][4] == b) && (icon.index[12][36] == b) && (icon.index[12][37] == f) &&
		     (icon.index[12][38] == f) && (icon.index[12][39] == 0u) && (icon.index[12][0] == 0u) && (icon.index[0][5] == 0u) && (icon.index[1][5] == f) &&
		     (icon.index[2][5] == b) && (icon.index[23][5] == f) && (icon.index[24][5] == 0u) && (icon.index[12][20] >= 1u) && (icon.index[12][20] <= icon.colorCount) &&
		     (icon.index[1][20] >= 1u) && (icon.index[1][20] <= icon.colorCount);
	}
	RldMk_Test(ok, "icon frame: 0x0400 frame and 0x8000 box where the subject leaves them, the subject over both", "colors %u, x2 %u x4 %u x20 %u",
	           icon.colorCount, (u32)icon.index[12][2], (u32)icon.index[12][4], (u32)icon.index[12][20]);
	options.fit = RLDMK_ICON_FIT_NONE;
	ok = RldMk_MakeIconWith(a, &options, &icon) && (icon.colorCount <= 13u) && (icon.clut[icon.colorCount + 1u] == 0x0400u) &&
	     (icon.clut[icon.colorCount + 2u] == 0x8000u) && (icon.index[3][2] == icon.colorCount + 1u);
	RldMk_Test(ok, "icon frame: picture A in 13 colors and the frame's two", "%u colors", icon.colorCount);

	// THE RETAIL PORTRAIT: a made-up shared.vrm (two pictures in a chain: the
	// CLUT at 16,5, the texels on page 1) and a pack with two icons.
	{
		static const u16 clut[16] = {0x0000u, 0x7FFFu, 0x8000u, 0x001Fu};
		static u16 texels[11u * 25u];
		struct RldMkBuffer vrm;
		struct RldMkBuffer pack;
		u8 record[0x20];
		u8 rgba[RLDMK_ICON_SHOWN_TEXELS * 4u];
		size_t first;
		int n;

		for (y = 0; y < 25u; y++)
		{
			for (x = 0; x < 11u; x++)
			{
				u32 word = 0;
				u32 t;

				for (t = 0; t < 4u; t++)
				{
					word |= ((4u * x + t + y) % 4u) << (4u * t);
				}
				texels[y * 11u + x] = (u16)word;
			}
		}
		memset(&vrm, 0, sizeof(vrm));
		RldMk_BufferLE32(&vrm, 0x20u);
		RldMk_BufferLE32(&vrm, 20u + 2u * 16u);
		first = vrm.size;
		RldMk_TestTim(&vrm, 16u, 5u, 16u, 1u, clut);
		RldMk_BufferLE32(&vrm, 20u + 2u * 11u * 25u);
		RldMk_TestTim(&vrm, 64u, 0u, 11u, 25u, texels);
		RldMk_BufferLE32(&vrm, 0u);

		memset(&pack, 0, sizeof(pack));
		RldMk_BufferLE32(&pack, 8u + 16u + 2u * 0x20u);   // the size, then the data
		RldMk_BufferLE32(&pack, 8u);                       // -> LevTexLookup
		RldMk_BufferLE32(&pack, 0u);
		RldMk_BufferLE32(&pack, 2u);                       // numIcon
		RldMk_BufferLE32(&pack, 24u);                      // firstIcon
		RldMk_BufferLE32(&pack, 0u);
		RldMk_BufferLE32(&pack, 0u);
		for (n = 0; n < 2; n++)
		{
			memset(record, 0, sizeof(record));
			record[0] = (u8)'x';
			Rld_WriteLE32(&record[0x10], (n == 0) ? 99u : (u32)s_rldMkIconId[14]);
			Rld_WriteLE16(&record[0x16], (5u << 6) | (16u >> 4));
			Rld_WriteLE16(&record[0x1A], 1u);
			RldMk_BufferBytes(&pack, record, sizeof(record));
		}

		ok = (vrm.data != NULL) && (pack.data != NULL) && (first == 8u) &&
		     RldMk_RetailIcon(vrm.data, (u32)vrm.size, pack.data, (u32)pack.size, s_rldMkIconId[14], rgba, RLDMK_ICON_SHOWN_WIDTH, RLDMK_ICON_SHOWN_HEIGHT);
		for (y = 0; ok && (y < RLDMK_ICON_SHOWN_HEIGHT); y++)
		{
			for (x = 0; ok && (x < RLDMK_ICON_SHOWN_WIDTH); x++)
			{
				static const u8 want[4][4] = {{0, 0, 0, 0}, {255, 255, 255, 255}, {0, 0, 0, 128}, {255, 0, 0, 255}};

				ok = (memcmp(&rgba[(y * RLDMK_ICON_SHOWN_WIDTH + x) * 4u], want[(x + y) % 4u], 4u) == 0);
			}
		}
		RldMk_Test(ok, "retail portrait: icon by iconID, CLUT and texels through a chain of VRAM pictures, STP at alpha 128", "not so");
		ok = (vrm.data != NULL) && (pack.data != NULL) && !RldMk_RetailIcon(vrm.data, (u32)vrm.size, pack.data, (u32)pack.size, 7u, rgba, 43u, 25u) &&
		     !RldMk_RetailIcon(vrm.data, (u32)vrm.size, pack.data, 20u, s_rldMkIconId[14], rgba, 43u, 25u);
		RldMk_Test(ok, "retail portrait: an iconID not there and a pack cut short give nothing", "not so");

		// A damaged chain: a step that would wrap around, one of 0, and one
		// past the end - each walk ends, nothing is found beyond.
		ok = (vrm.data != NULL);
		for (n = 0; ok && (n < 3); n++)
		{
			static const u32 steps[3] = {0xFFFFFFFCu, 0u, 0x7FFFFFF0u};

			Rld_WriteLE32(&vrm.data[4], steps[n]);
			ok = (RldMk_VrmWord(vrm.data, (u32)vrm.size, 64u, 0u) == 0u) && (RldMk_VrmWord(vrm.data, (u32)vrm.size, 16u, 5u) == 0x0000u) &&
			     (RldMk_VrmWord(vrm.data, (u32)vrm.size, 17u, 5u) == 0x7FFFu);
		}
		RldMk_Test(ok, "retail portrait: a damaged chain of VRAM pictures ends the walk (no wrap, no endless loop)", "not so");
		free(vrm.data);
		free(pack.data);
	}
}

// --- --voices ------------------------------------------------------------------------

// A 16-bit PCM WAV. kind 0: a triangle wave, 1: some samples at full level, 2: silence,
// 3: a square wave of +-31000.
static u8 *RldMk_TestWav(u32 rate, u32 channels, u32 frames, int kind, int psound, size_t *sizeOut)
{
	struct RldMkBuffer wav;
	const u32 dataBytes = frames * channels * 2u;
	const u32 listBytes = psound ? 32u : 0u;
	u8 word[4];
	u32 i;

	memset(&wav, 0, sizeof(wav));
	RldMk_BufferBytes(&wav, "RIFF", 4u);
	RldMk_BufferLE32(&wav, 4u + 24u + listBytes + 8u + dataBytes);
	RldMk_BufferBytes(&wav, "WAVEfmt ", 8u);
	RldMk_BufferLE32(&wav, 16u);
	Rld_WriteLE16(word, 1u);
	Rld_WriteLE16(&word[2], channels);
	RldMk_BufferBytes(&wav, word, 4u);
	RldMk_BufferLE32(&wav, rate);
	RldMk_BufferLE32(&wav, rate * channels * 2u);
	Rld_WriteLE16(word, channels * 2u);
	Rld_WriteLE16(&word[2], 16u);
	RldMk_BufferBytes(&wav, word, 4u);

	if (psound)
	{
		// LIST INFO ISFT "PSound 3.0" with its NUL, 11 bytes and one pad byte.
		RldMk_BufferBytes(&wav, "LIST", 4u);
		RldMk_BufferLE32(&wav, 24u);
		RldMk_BufferBytes(&wav, "INFOISFT", 8u);
		RldMk_BufferLE32(&wav, 11u);
		RldMk_BufferBytes(&wav, "PSound 3.0\0\0", 12u);
	}

	RldMk_BufferBytes(&wav, "data", 4u);
	RldMk_BufferLE32(&wav, dataBytes);
	for (i = 0; i < (frames * channels); i++)
	{
		const u32 phase = i % 64u;
		int value = (phase < 32u) ? ((int)phase * 500 - 8000) : ((64 - (int)phase) * 500 - 8000);

		if (kind == 1)
		{
			value = ((i % 100u) == 0u) ? 32767 : value;
		}
		else if (kind == 2)
		{
			value = 0;
		}
		else if (kind == 3)
		{
			value = (phase < 32u) ? 31000 : -31000;
		}
		Rld_WriteLE16(word, (u32)value & 0xFFFFu);
		RldMk_BufferBytes(&wav, word, 2u);
	}

	*sizeOut = wav.size;
	return wav.data;
}

// A VAG: header, a zero block, `blocks` blocks of samples, the last with the end flag.
static u8 *RldMk_TestVag(u32 blocks, int badSize, size_t *sizeOut)
{
	struct RldMkBuffer vag;
	u8 head[0x30];
	u8 block[16];
	u32 b;
	u32 k;

	memset(&vag, 0, sizeof(vag));
	memset(head, 0, sizeof(head));
	memcpy(head, "VAGp", 4u);
	RldMk_PutBE32(&head[0x04], 3u);
	RldMk_PutBE32(&head[0x0C], (blocks + 1u) * 16u + (badSize ? 64u : 0u));
	RldMk_PutBE32(&head[0x10], 11025u);
	memcpy(&head[0x20], "test", 4u);
	RldMk_BufferBytes(&vag, head, sizeof(head));

	memset(block, 0, sizeof(block));
	RldMk_BufferBytes(&vag, block, sizeof(block));
	for (b = 0; b < blocks; b++)
	{
		block[0] = 0x02u;   // shift 2, filter 0
		block[1] = (u8)(((b + 1u) == blocks) ? 1u : 0u);
		for (k = 2; k < 16u; k++)
		{
			block[k] = (u8)((k * 3u + b) & 0x77u);
		}
		RldMk_BufferBytes(&vag, block, sizeof(block));
	}

	*sizeOut = vag.size;
	return vag.data;
}

// A 16-bit PCM WAV of a sine of `hz` at `amplitude`, every channel alike.
static u8 *RldMk_TestSine(u32 rate, u32 channels, u32 frames, u32 hz, int amplitude, size_t *sizeOut)
{
	struct RldMkBuffer wav;
	const u32 dataBytes = frames * channels * 2u;
	u8 word[4];
	u32 f;
	u32 c;

	memset(&wav, 0, sizeof(wav));
	RldMk_BufferBytes(&wav, "RIFF", 4u);
	RldMk_BufferLE32(&wav, 4u + 24u + 8u + dataBytes);
	RldMk_BufferBytes(&wav, "WAVEfmt ", 8u);
	RldMk_BufferLE32(&wav, 16u);
	Rld_WriteLE16(word, 1u);
	Rld_WriteLE16(&word[2], channels);
	RldMk_BufferBytes(&wav, word, 4u);
	RldMk_BufferLE32(&wav, rate);
	RldMk_BufferLE32(&wav, rate * channels * 2u);
	Rld_WriteLE16(word, channels * 2u);
	Rld_WriteLE16(&word[2], 16u);
	RldMk_BufferBytes(&wav, word, 4u);
	RldMk_BufferBytes(&wav, "data", 4u);
	RldMk_BufferLE32(&wav, dataBytes);
	for (f = 0; f < frames; f++)
	{
		const double turns = (double)((u64)f * hz % rate) / (double)rate;
		const int value = (int)RldMk_Round((double)amplitude * RldMk_VoiceSin(2.0 * 3.14159265358979323846 * turns));

		for (c = 0; c < channels; c++)
		{
			Rld_WriteLE16(word, (u32)value & 0xFFFFu);
			RldMk_BufferBytes(&wav, word, 2u);
		}
	}

	*sizeOut = wav.size;
	return wav.data;
}

// A PCM WAV of 8 or 24 bits, or of 32-bit float (bits 32), of a triangle
// wave in every channel.
static u8 *RldMk_TestWavBits(u32 rate, u32 channels, u32 bits, u32 frames, size_t *sizeOut)
{
	struct RldMkBuffer wav;
	const u32 bytes = bits / 8u;
	const u32 dataBytes = frames * channels * bytes;
	u8 word[4];
	u32 f;
	u32 c;

	memset(&wav, 0, sizeof(wav));
	RldMk_BufferBytes(&wav, "RIFF", 4u);
	RldMk_BufferLE32(&wav, 4u + 24u + 8u + dataBytes);
	RldMk_BufferBytes(&wav, "WAVEfmt ", 8u);
	RldMk_BufferLE32(&wav, 16u);
	Rld_WriteLE16(word, (bits == 32u) ? 3u : 1u);
	Rld_WriteLE16(&word[2], channels);
	RldMk_BufferBytes(&wav, word, 4u);
	RldMk_BufferLE32(&wav, rate);
	RldMk_BufferLE32(&wav, rate * channels * bytes);
	Rld_WriteLE16(word, channels * bytes);
	Rld_WriteLE16(&word[2], bits);
	RldMk_BufferBytes(&wav, word, 4u);
	RldMk_BufferBytes(&wav, "data", 4u);
	RldMk_BufferLE32(&wav, dataBytes);
	for (f = 0; f < frames; f++)
	{
		for (c = 0; c < channels; c++)
		{
			const u32 phase = (f + 7u * c) % 50u;
			const int value = ((phase < 25u) ? (int)phase : (50 - (int)phase)) * 1200 - 15000 - (int)c * 333;   // 16-bit scale

			if (bits == 8u)
			{
				word[0] = (u8)((value / 256) + 128);
			}
			else if (bits == 24u)
			{
				const s32 v24 = value * 256 - 77;

				word[0] = (u8)((u32)v24 & 0xFFu);
				word[1] = (u8)(((u32)v24 >> 8) & 0xFFu);
				word[2] = (u8)(((u32)v24 >> 16) & 0xFFu);
			}
			else
			{
				const float x = (float)value / 32768.0f;
				u32 w;

				memcpy(&w, &x, sizeof(w));
				Rld_WriteLE32(word, w);
			}
			RldMk_BufferBytes(&wav, word, bytes);
		}
	}

	*sizeOut = wav.size;
	return wav.data;
}

// A VAG whose blocks run through every filter (0..4) and every shift
// (0..15; 13..15 are read as 9), at 22050 Hz.
static u8 *RldMk_TestVagAll(u32 blocks, size_t *sizeOut)
{
	struct RldMkBuffer vag;
	u8 head[0x30];
	u8 block[16];
	u32 b;
	u32 k;

	memset(&vag, 0, sizeof(vag));
	memset(head, 0, sizeof(head));
	memcpy(head, "VAGp", 4u);
	RldMk_PutBE32(&head[0x04], 3u);
	RldMk_PutBE32(&head[0x0C], blocks * 16u);
	RldMk_PutBE32(&head[0x10], 22050u);
	RldMk_BufferBytes(&vag, head, sizeof(head));
	for (b = 0; b < blocks; b++)
	{
		block[0] = (u8)(((b % 5u) << 4) | (b % 16u));
		block[1] = (u8)(((b + 1u) == blocks) ? 1u : 0u);
		for (k = 2; k < 16u; k++)
		{
			block[k] = (u8)((k * 37u + b * 11u) & 0xFFu);
		}
		RldMk_BufferBytes(&vag, block, sizeof(block));
	}

	*sizeOut = vag.size;
	return vag.data;
}

// The samples of a 16-bit WAV of RldMk_TestWav without PSound: 44 bytes of
// header, then the data.
static const u8 *RldMk_TestWavSamples(const u8 *wav, u32 *frames)
{
	*frames = Rld_ReadLE32(&wav[40]) / 2u;
	return &wav[44];
}

struct RldMkTestVoices
{
	struct RldMkVoiceFile files[16];
	u32 count;
};

static void RldMk_TestVoice(struct RldMkTestVoices *v, const char *name, u8 *data, size_t size)
{
	snprintf(v->files[v->count].name, sizeof(v->files[0].name), "%s", name);
	v->files[v->count].data = data;
	v->files[v->count].size = size;
	v->count++;
}

static void RldMk_TestVoicesFree(struct RldMkTestVoices *v)
{
	u32 i;

	for (i = 0; i < v->count; i++)
	{
		free(v->files[i].data);
	}
	memset(v, 0, sizeof(*v));
}

// RldMk_PackVoices on the files in memory, quietly, the said codes reset first.
static u8 *RldMk_TestPack(const struct RldMkTestVoices *v, const char **assign, u32 assignCount, int normalize, size_t *size)
{
	struct RldMkVoiceOptions options;

	options.assign = assign;
	options.assignCount = assignCount;
	options.normalize = normalize;
	options.preview = NULL;
	RldMk_ResetSaid();
	return RldMk_PackVoices(v->files, v->count, &options, NULL, size);
}

// The largest |sample| of samples [from, to) of a clip, or of 16-bit bytes.
static u32 RldMk_TestPeak(const u8 *samples, u32 from, u32 to)
{
	u32 peak = 0;
	u32 i;

	for (i = from; i < to; i++)
	{
		const int v = (int)Rld_ReadLE16(&samples[2u * i]) - (int)((Rld_ReadLE16(&samples[2u * i]) & 0x8000u) << 1);
		const u32 a = (u32)((v < 0) ? -v : v);

		peak = (a > peak) ? a : peak;
	}

	return peak;
}

// The largest |sample| of a resampled sound away from its ends.
static u32 RldMk_TestPeakMiddle(const s16 *samples, u32 frames)
{
	u32 peak = 0;
	u32 i;

	for (i = 64u; (i + 64u) < frames; i++)
	{
		const u32 a = (u32)((samples[i] < 0) ? -(int)samples[i] : (int)samples[i]);

		peak = (a > peak) ? a : peak;
	}

	return peak;
}

// The same bytes from the same sounds, across runtimes: a line resampled
// from 44100 Hz, a short sound from 11025 Hz, one at 22050 Hz kept as it is,
// all normalized. A different value means the voices of every character
// sound different now - never update it to make the test pass.
// The same for every input form: a VAG through all filters and shifts, 24
// bit at 48000 Hz, 8 bit at 8000 Hz, float stereo at 44100 Hz.
#define RLDMK_GOLDEN_VOICES_FORMATS "79a0d831cff14217a10e496c3b82b4b5b4ae37157d531a02a64d8fdd7959983f"

#define RLDMK_GOLDEN_VOICES "bfd085935f41c4c71f52b727ebcb4d90f73f34af027dc912e88f3c8251e35528"

static void RldMk_SelfTestVoices(const u8 *good, size_t goodSize)
{
	struct RldMkTestVoices v;
	struct RldCharVoices voices;
	size_t size = 0;
	size_t cvoiSize = 0;
	u8 *cvoi;
	u8 *data;
	int ok;

	// A good set: one line as WAV, a short sound as VAG.
	memset(&v, 0, sizeof(v));
	data = RldMk_TestWav(11025u, 1u, 5513u, 0, 0, &size);
	RldMk_TestVoice(&v, "boost1.wav", data, size);
	data = RldMk_TestVag(20u, 0, &size);
	RldMk_TestVoice(&v, "yes.vag", data, size);
	cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
	ok = (cvoi != NULL) && (s_rldMkErrors == 0) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clipCount == 2u) &&
	     (voices.count[0] == 1u) && (voices.count[RLDCHAR_VOICE_SHORT_FIRST] == 1u) && (voices.first[RLDCHAR_VOICE_SHORT_FIRST] == 1u) &&
	     (voices.clip[0].frames == 11026u) && (voices.clip[1].frames == 21u * 28u * 2u) && (strcmp(voices.clip[0].name, "boost1") == 0) &&
	     (strcmp(voices.clip[1].name, "yes") == 0) && (voices.clip[0].volume == RLDCHAR_VOICE_VOLUME_FULL);
	RldMk_Test(ok && RldMk_Said("voice-missing") && !RldMk_Said("voice-clip") && !RldMk_Said("voice-length-line") && !RldMk_Said("voice-silent"),
	           "voices: a WAV line and a VAG sound packed into CVOI at 22050 Hz, the silent events named", "said: %s", s_rldMkCodes);
	free(cvoi);
	RldMk_TestVoicesFree(&v);

	// Names: a double, an unknown, another file.
	data = RldMk_TestVag(4u, 0, &size);
	RldMk_TestVoice(&v, "BOOST1.VAG", data, size);
	data = RldMk_TestWav(22050u, 1u, 2000u, 0, 0, &size);
	RldMk_TestVoice(&v, "boost1.wav", data, size);
	data = RldMk_TestWav(22050u, 1u, 2000u, 0, 0, &size);
	RldMk_TestVoice(&v, "hit5.wav", data, size);
	data = (u8 *)malloc(4u);
	if (data != NULL)
	{
		memcpy(data, "text", 4u);
	}
	RldMk_TestVoice(&v, "readme.txt", data, 4u);
	cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
	ok = (cvoi != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clipCount == 1u) && (voices.clip[0].frames == 2000u);
	RldMk_Test(ok && (s_rldMkErrors == 0) && RldMk_Said("voice-double") && RldMk_Said("voice-unknown") && RldMk_Said("voice-other"),
	           "voices: double (the WAV kept), unknown and other files named", "said: %s", s_rldMkCodes);
	free(cvoi);
	RldMk_TestVoicesFree(&v);

	// Level, length, channels, source, the same sound twice.
	data = RldMk_TestWav(11025u, 2u, 3000u, 0, 0, &size);
	RldMk_TestVoice(&v, "drop1.wav", data, size);
	data = RldMk_TestWav(22050u, 1u, 3000u, 1, 0, &size);
	RldMk_TestVoice(&v, "drop2.wav", data, size);
	data = RldMk_TestWav(11025u, 1u, 44100u, 0, 0, &size);
	RldMk_TestVoice(&v, "fire1.wav", data, size);
	data = RldMk_TestWav(11025u, 1u, 3000u, 2, 0, &size);
	RldMk_TestVoice(&v, "hit1.wav", data, size);
	data = RldMk_TestWav(11025u, 1u, 3000u, 0, 1, &size);
	RldMk_TestVoice(&v, "spin1.wav", data, size);
	data = RldMk_TestWav(11025u, 1u, 3000u, 0, 0, &size);
	RldMk_TestVoice(&v, "spin2.wav", data, size);
	data = RldMk_TestWav(11025u, 1u, 16538u, 0, 0, &size);
	RldMk_TestVoice(&v, "yes.wav", data, size);
	cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
	ok = (cvoi != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clipCount == 7u) &&
	     (voices.clip[voices.first[7]].frames == RLDCHAR_VOICE_LINE_FRAMES) && (voices.clip[voices.first[8]].frames == RLDCHAR_VOICE_SHORT_FRAMES);
	RldMk_Test(ok && (s_rldMkErrors == 0) && RldMk_Said("voice-stereo") && RldMk_Said("voice-clip") && RldMk_Said("voice-length-line") &&
	               RldMk_Said("voice-silent") && RldMk_Said("voice-source") && RldMk_Said("voice-same") && RldMk_Said("voice-length-short"),
	           "voices: stereo, clipped, too long (cut to 3.5 s and 1.0 s), silent, PSound, the same sound", "said: %s", s_rldMkCodes);
	free(cvoi);
	RldMk_TestVoicesFree(&v);

	// Files rldpack cannot read stop the build.
	data = RldMk_TestWav(11025u, 1u, 100u, 0, 0, &size);
	if (data != NULL)
	{
		memcpy(&data[8], "WAVX", 4u);
	}
	RldMk_TestVoice(&v, "shield1.wav", data, size);
	data = RldMk_TestWav(96000u, 1u, 100u, 0, 0, &size);
	RldMk_TestVoice(&v, "shield2.wav", data, size);
	data = RldMk_TestVag(4u, 1, &size);
	RldMk_TestVoice(&v, "hit.vag", data, size);
	cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
	RldMk_Test((cvoi == NULL) && (s_rldMkErrors == 3) && RldMk_Said("voice-wav") && RldMk_Said("voice-vag"),
	           "voices: a broken WAV, 96 kHz and a VAG of a wrong size refused, no CVOI", "%d errors, said: %s", s_rldMkErrors, s_rldMkCodes);
	free(cvoi);
	RldMk_TestVoicesFree(&v);

	// voice-clip: 15 samples at full level are a note, 16 a warning.
	data = RldMk_TestWav(22050u, 1u, 1500u, 1, 0, &size);
	RldMk_TestVoice(&v, "hit1.wav", data, size);
	cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
	ok = RldMk_Said("voice-clip") && (s_rldMkWarnings == 0);
	free(cvoi);
	RldMk_TestVoicesFree(&v);
	data = RldMk_TestWav(22050u, 1u, 1600u, 1, 0, &size);
	RldMk_TestVoice(&v, "hit1.wav", data, size);
	cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
	ok = ok && RldMk_Said("voice-clip") && (s_rldMkWarnings == 1);
	RldMk_Test(ok, "voices: 15 samples at full level a note, 16 a warning (voice-clip)", "%d warnings, said: %s", s_rldMkWarnings, s_rldMkCodes);
	free(cvoi);
	RldMk_TestVoicesFree(&v);

	// --voice splits at the last '=': a file name may hold one.
	{
		static const char *assign[] = {"a=b.wav=fire"};

		data = RldMk_TestWav(22050u, 1u, 100u, 0, 0, &size);
		RldMk_TestVoice(&v, "a=b.wav", data, size);
		cvoi = RldMk_TestPack(&v, assign, 1u, 0, &cvoiSize);
		ok = (cvoi != NULL) && (s_rldMkErrors == 0) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.count[7] == 1u) &&
		     (strcmp(voices.clip[0].name, "a=b") == 0);
		RldMk_Test(ok, "voices: --voice a=b.wav=fire splits at the last '='", "said: %s", s_rldMkCodes);
		free(cvoi);
		RldMk_TestVoicesFree(&v);
	}

	// A double that --voice settles is no double; one it does not settle is.
	{
		static const char *assign[] = {"BOOST1.VAG=fire"};

		data = RldMk_TestVag(4u, 0, &size);
		RldMk_TestVoice(&v, "BOOST1.VAG", data, size);
		data = RldMk_TestWav(22050u, 1u, 2000u, 0, 0, &size);
		RldMk_TestVoice(&v, "boost1.wav", data, size);
		cvoi = RldMk_TestPack(&v, assign, 1u, 0, &cvoiSize);
		ok = (cvoi != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.count[0] == 1u) && (voices.count[7] == 1u) &&
		     !RldMk_Said("voice-double");
		free(cvoi);
		cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
		ok = ok && RldMk_Said("voice-double");
		RldMk_Test(ok, "voices: voice-double only while --voice has not given the other file an event", "said: %s", s_rldMkCodes);
		free(cvoi);
		RldMk_TestVoicesFree(&v);
	}

	// What the game plays decides: a square wave below full level whose
	// overshoot the resampler clamps is clipped; a near-silent sound is
	// voice-quiet and not lifted by --voice-normalize.
	{
		data = RldMk_TestWav(44100u, 1u, 8820u, 3, 0, &size);
		RldMk_TestVoice(&v, "hit1.wav", data, size);
		data = RldMk_TestSine(22050u, 1u, 4000u, 300u, 100, &size);
		RldMk_TestVoice(&v, "spin1.wav", data, size);
		cvoi = RldMk_TestPack(&v, NULL, 0u, 1, &cvoiSize);
		ok = (cvoi != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && RldMk_Said("voice-clip") && RldMk_Said("voice-quiet") &&
		     (RldMk_TestPeak(&cvoi[voices.clip[voices.first[2]].offset], 0u, voices.clip[voices.first[2]].frames) <= 100u) &&
		     (RldMk_TestPeak(&cvoi[voices.clip[voices.first[2]].offset], 0u, voices.clip[voices.first[2]].frames) >= 95u);
		RldMk_Test(ok, "voices: clamps of the resampler count as clipped, a near-silent sound is not normalized (voice-quiet)", "said: %s",
		           s_rldMkCodes);
		free(cvoi);
		RldMk_TestVoicesFree(&v);
	}

	// 8 bit: 255 is full level, as 0 is - 20 of them clip.
	{
		u32 k;

		data = RldMk_TestWavBits(22050u, 1u, 8u, 1000u, &size);
		for (k = 0; (data != NULL) && (k < 20u); k++)
		{
			data[44u + 10u * k] = 255u;
		}
		RldMk_TestVoice(&v, "hit1.wav", data, size);
		cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
		RldMk_Test((cvoi != NULL) && RldMk_Said("voice-clip") && (s_rldMkWarnings == 1), "voices: 8-bit samples of 255 count as full level (voice-clip)",
		           "%d warnings, said: %s", s_rldMkWarnings, s_rldMkCodes);
		free(cvoi);
		RldMk_TestVoicesFree(&v);
	}

	// Every input form, packed twice the same and against its golden hash.
	{
		u8 *again;
		size_t againSize = 0;

		data = RldMk_TestVagAll(80u, &size);
		RldMk_TestVoice(&v, "boost1.vag", data, size);
		data = RldMk_TestWavBits(48000u, 1u, 24u, 4800u, &size);
		RldMk_TestVoice(&v, "hit1.wav", data, size);
		data = RldMk_TestWavBits(8000u, 1u, 8u, 1600u, &size);
		RldMk_TestVoice(&v, "spin1.wav", data, size);
		data = RldMk_TestWavBits(44100u, 2u, 32u, 4410u, &size);
		RldMk_TestVoice(&v, "fire1.wav", data, size);
		cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
		again = RldMk_TestPack(&v, NULL, 0u, 0, &againSize);
		ok = (cvoi != NULL) && (s_rldMkErrors == 0) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clipCount == 4u) &&
		     (voices.clip[0].frames == 80u * 28u) && (voices.clip[1].frames == 2205u) && (voices.clip[2].frames == 4410u) &&
		     (voices.clip[3].frames == 2205u) && (again != NULL) && (againSize == cvoiSize) && (memcmp(cvoi, again, cvoiSize) == 0);
		RldMk_Test(ok, "voices: VAG (all filters and shifts), 24 bit, 8 bit and float stereo read, packed twice the same", "%d errors, said: %s",
		           s_rldMkErrors, s_rldMkCodes);
		if (Rld_CheckGolden("make-char --voices (VAG, 24 bit, 8 bit, float)", cvoi, cvoiSize, RLDMK_GOLDEN_VOICES_FORMATS) != 0)
		{
			s_rldMkTestFailed++;
		}
		free(cvoi);
		free(again);
		RldMk_TestVoicesFree(&v);
	}

	// --voice: any file into any event, a name taken out; the order by name.
	{
		static const char *assign[] = {"laugh.wav=shield", "BOOST1.WAV=none", "hit.wav=passing", "d.wav=fire", "B.wav=FIRE", "a.wav=fire", "c.wav=fire"};
		static const char *wrong[] = {"nothere.wav=boost", "laugh.wav=loud", "laugh.wav"};
		static const char *names[] = {"B.wav", "a.wav", "boost1.wav", "boost2.wav", "c.wav", "d.wav", "e.wav", "hit.wav", "laugh.wav"};
		u32 n;

		for (n = 0; n < 9u; n++)
		{
			data = RldMk_TestWav(22050u, 1u, 100u + n, 0, 0, &size);
			RldMk_TestVoice(&v, names[n], data, size);
		}
		cvoi = RldMk_TestPack(&v, assign, 7u, 0, &cvoiSize);
		ok = (cvoi != NULL) && (s_rldMkErrors == 0) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clipCount == 7u) &&
		     (voices.count[0] == 1u) && (voices.clip[voices.first[0]].frames == 103u) && (voices.count[5] == 1u) && (voices.count[6] == 1u) &&
		     (voices.count[RLDCHAR_VOICE_SHORT_FIRST + 1] == 0u) && (voices.count[7] == 4u) && (strcmp(voices.clip[voices.first[7]].name, "a") == 0) &&
		     (strcmp(voices.clip[voices.first[7] + 1u].name, "B") == 0) && (strcmp(voices.clip[voices.first[7] + 3u].name, "d") == 0) &&
		     RldMk_Said("voice-unknown") && !RldMk_Said("voice-too-many");
		RldMk_Test(ok, "voices: --voice puts files into events and takes one out, 4 in an event by name", "said: %s", s_rldMkCodes);
		free(cvoi);

		cvoi = RldMk_TestPack(&v, wrong, 3u, 0, &cvoiSize);
		RldMk_Test((s_rldMkErrors == 3) && RldMk_Said("voice-assign") && (cvoi == NULL), "voices: --voice with an unknown file, event and form refused",
		           "%d errors, said: %s", s_rldMkErrors, s_rldMkCodes);
		free(cvoi);

		assign[0] = "e.wav=fire";
		cvoi = RldMk_TestPack(&v, assign, 7u, 0, &cvoiSize);
		RldMk_Test((cvoi == NULL) && (s_rldMkErrors == 1) && RldMk_Said("voice-too-many"), "voices: 5 sounds in one event refused (voice-too-many)",
		           "%d errors, said: %s", s_rldMkErrors, s_rldMkCodes);
		assign[0] = "laugh.wav=shield";
		free(cvoi);
		RldMk_TestVoicesFree(&v);
	}

	// Resampling: 22050 Hz sample for sample; down from 44100 and 48000 Hz,
	// up from 8000 and 11025 Hz, the level of a tone kept and one above the
	// new Nyquist frequency filtered out; the cut.
	{
		static const u32 rates[4] = {8000u, 11025u, 44100u, 48000u};
		s16 tone[9600];
		s16 *out;
		u32 frames = 0;
		u32 full = 0;
		u32 r;
		u32 i;

		for (i = 0; i < 9600u; i++)
		{
			tone[i] = (s16)((int)(i % 200u) * 100 - 10000);
		}
		out = RldMk_VoiceResample(tone, 9600u, 22050u, RLDCHAR_VOICE_LINE_FRAMES, &frames, &full);
		ok = (out != NULL) && (frames == 9600u) && (full == 9600u) && (memcmp(out, tone, sizeof(tone)) == 0);
		free(out);

		for (r = 0; ok && (r < 4u); r++)
		{
			const u32 n = rates[r] / 5u;   // 0.2 s
			u32 k;

			for (k = 0; k < n; k++)
			{
				const double turns = (double)((u64)k * 1000u % rates[r]) / (double)rates[r];

				tone[k] = (s16)RldMk_Round(16000.0 * RldMk_VoiceSin(2.0 * 3.14159265358979323846 * turns));
			}
			out = RldMk_VoiceResample(tone, n, rates[r], RLDCHAR_VOICE_LINE_FRAMES, &frames, &full);
			ok = (out != NULL) && (frames == 4410u) && (full == 4410u) && (RldMk_TestPeakMiddle(out, frames) >= 15200u) &&
			     (RldMk_TestPeakMiddle(out, frames) <= 16800u);
			free(out);
		}
		RldMk_Test(ok, "voices: 22050 Hz kept sample for sample, a 1 kHz tone keeps its level from 8000, 11025, 44100 and 48000 Hz", "not so");

		for (i = 0; i < 8820u; i++)
		{
			const double turns = (double)((u64)i * 15000u % 44100u) / 44100.0;

			tone[i] = (s16)RldMk_Round(16000.0 * RldMk_VoiceSin(2.0 * 3.14159265358979323846 * turns));
		}
		out = RldMk_VoiceResample(tone, 8820u, 44100u, RLDCHAR_VOICE_LINE_FRAMES, &frames, &full);
		ok = (out != NULL) && (frames == 4410u) && (RldMk_TestPeakMiddle(out, frames) < 1600u);
		free(out);
		out = RldMk_VoiceResample(tone, 8820u, 44100u, 1000u, &frames, &full);
		ok = ok && (out != NULL) && (frames == 1000u) && (full == 4410u);
		free(out);
		RldMk_Test(ok, "voices: 15 kHz at 44100 Hz filtered out (low-pass), the cut keeps the length asked", "not so");
	}

	// --voice-normalize: the peak at -1 dBFS, loud or quiet; without it a
	// sound at 22050 Hz keeps its samples. Packed twice: the same bytes.
	{
		u8 *again;
		size_t againSize = 0;
		u32 frames = 0;
		const u8 *samples;

		data = RldMk_TestSine(44100u, 2u, 30000u, 440u, 1000, &size);
		RldMk_TestVoice(&v, "boost1.wav", data, size);
		data = RldMk_TestWav(22050u, 1u, 4000u, 1, 0, &size);
		RldMk_TestVoice(&v, "hit1.wav", data, size);
		data = RldMk_TestSine(11025u, 1u, 5000u, 300u, 20000, &size);
		RldMk_TestVoice(&v, "yes.wav", data, size);

		cvoi = RldMk_TestPack(&v, NULL, 0u, 0, &cvoiSize);
		samples = RldMk_TestWavSamples(v.files[1].data, &frames);
		ok = (cvoi != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clip[1].frames == frames) &&
		     (memcmp(&cvoi[voices.clip[1].offset], samples, 2u * frames) == 0);
		free(cvoi);
		RldMk_Test(ok, "voices: without --voice-normalize a sound at 22050 Hz keeps every sample", "not so");

		cvoi = RldMk_TestPack(&v, NULL, 0u, 1, &cvoiSize);
		again = RldMk_TestPack(&v, NULL, 0u, 1, &againSize);
		ok = (cvoi != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, &voices) == NULL) && (voices.clipCount == 3u);
		ok = ok && (RldMk_TestPeak(&cvoi[voices.clip[0].offset], 0u, voices.clip[0].frames) == (u32)RLDMK_VOICE_PEAK) &&
		     (RldMk_TestPeak(&cvoi[voices.clip[1].offset], 0u, voices.clip[1].frames) == (u32)RLDMK_VOICE_PEAK) &&
		     (RldMk_TestPeak(&cvoi[voices.clip[2].offset], 0u, voices.clip[2].frames) == (u32)RLDMK_VOICE_PEAK);
		RldMk_Test(ok, "voices: --voice-normalize sets every peak to -1 dBFS (29204)", "not so");
		RldMk_Test((cvoi != NULL) && (again != NULL) && (againSize == cvoiSize) && (memcmp(cvoi, again, cvoiSize) == 0),
		           "voices: packed twice, the same bytes", "not so");
		if (Rld_CheckGolden("make-char --voices (44100 Hz stereo, 22050 Hz, 11025 Hz, normalized)", cvoi, cvoiSize, RLDMK_GOLDEN_VOICES) != 0)
		{
			s_rldMkTestFailed++;
		}
		free(again);
		RldMk_TestVoicesFree(&v);
	}

	// CVOI-1..4, each broken once on a good chunk; the reader the game runs.
	if (cvoi != NULL)
	{
		u8 *broken = (u8 *)malloc(cvoiSize + 2u);

		ok = (broken != NULL) && (RldChar_CheckVoices(cvoi, cvoiSize, NULL) == NULL);
		if (ok)
		{
			const char *why;

			memcpy(broken, cvoi, cvoiSize);
			Rld_WriteLE32(&broken[0x00], 2u);
			why = RldChar_CheckVoices(broken, cvoiSize, &voices);
			ok = (why != NULL) && (strncmp(why, "CVOI-1", 6) == 0) && (voices.clipCount == 0u);

			memcpy(broken, cvoi, cvoiSize);
			broken[RLDCHAR_VOICE_HEAD_BYTES + 1u] = 5u;
			why = RldChar_CheckVoices(broken, cvoiSize, NULL);
			ok = ok && (why != NULL) && (strncmp(why, "CVOI-2", 6) == 0);

			memcpy(broken, cvoi, cvoiSize);
			Rld_WriteLE32(&broken[RLDCHAR_VOICE_CLIPS_AT + 0x14u], 0u);
			why = RldChar_CheckVoices(broken, cvoiSize, NULL);
			ok = ok && (why != NULL) && (strncmp(why, "CVOI-3", 6) == 0);

			memcpy(broken, cvoi, cvoiSize);
			broken[cvoiSize] = 0u;
			broken[cvoiSize + 1u] = 0u;
			why = RldChar_CheckVoices(broken, cvoiSize + 2u, NULL);
			ok = ok && (why != NULL) && (strncmp(why, "CVOI-4", 6) == 0);
		}
		RldMk_Test(ok, "voices: CVOI-1..4 drop a wrong version, an event of 5, an empty clip, bytes behind the last clip", "not so");
		free(broken);
	}

	// A container with CVOI: three chunks, CVOI last with its hash; without
	// it the container is the one of before.
	if ((good != NULL) && (cvoi != NULL))
	{
		u8 field[RLDCHAR_NAME_FIELD];
		size_t chriSize = 0;
		size_t plainSize = 0;
		u8 *chri;
		u8 *file = NULL;
		u8 *plain = NULL;
		u8 *before = NULL;
		size_t beforeSize = 0;

		memset(field, 0, sizeof(field));
		memcpy(field, "SELF TEST", 9u);
		chri = RldMk_BuildChri(14u, 0u, field, 1u, "", 0u, &chriSize);
		ok = (chri != NULL);
		if (ok)
		{
			file = RldMk_ContainerVoices(chri, chriSize, good, goodSize, NULL, NULL, 0u, cvoi, cvoiSize, &size);
			plain = RldMk_ContainerVoices(chri, chriSize, good, goodSize, NULL, NULL, 0u, NULL, 0u, &plainSize);
			before = RldMk_Container(chri, chriSize, good, goodSize, NULL, &beforeSize);
		}
		ok = ok && (file != NULL) && (plain != NULL) && (before != NULL);
		if (ok)
		{
			const u64 dirAt = Rld_ReadLE64(&file[0x18]);
			const u8 *entry = &file[(size_t)dirAt + 2u * RLD_DIR_ENTRY_SIZE];
			u8 digest[32];

			Sha256(&file[(size_t)Rld_ReadLE64(&entry[0x08])], cvoiSize, digest);
			ok = (Rld_ReadLE32(&file[0x10]) == 3u) && (memcmp(entry, "CVOI", 4u) == 0) && (Rld_ReadLE64(&entry[0x10]) == (u64)cvoiSize) &&
			     (memcmp(&entry[RLD_DIR_HASH_OFFSET], digest, 32u) == 0) && (RldChar_ChunkLimit(entry) == RLDCHAR_LIMIT_CVOI) &&
			     (plainSize == beforeSize) && (memcmp(plain, before, plainSize) == 0) && (Rld_ReadLE32(&plain[0x10]) == 2u);
		}
		RldMk_Test(ok, "voices: container with CVOI last, its hash in the directory; without voices the same bytes as before", "not so");
		free(file);
		free(plain);
		free(before);
		free(chri);
	}
	free(cvoi);
}

// --- Golden hashes -------------------------------------------------------------------

static void RldMk_SelfTestGolden(struct RldMkTestMesh *mesh, const struct RldPngImage *a)
{
	static struct RldMkIcon icon;
	u8 field[RLDCHAR_NAME_FIELD];
	size_t chriSize = 0;
	size_t chriNoWheelsSize = 0;
	u8 *chri;
	u8 *chriNoWheels;
	u32 over = 0;
	int k;

	memset(field, 0, sizeof(field));
	memcpy(field, "SELF TEST", 9u);
	chri = RldMk_BuildChri(14u, 0u, field, 1u, "", 0u, &chriSize);
	chriNoWheels = RldMk_BuildChri(14u, 0u, field, 1u, "", RLDCHAR_FLAG_NO_WHEELS, &chriNoWheelsSize);
	if ((chri == NULL) || (chriNoWheels == NULL) || !RldMk_MakeIcon(a, &icon))
	{
		RldMk_Test(0, "golden hashes", "out of memory");
		free(chri);
		free(chriNoWheels);
		return;
	}

	for (k = 0; k < 5; k++)
	{
		static const char *const builtNames[5] = {"golden model built twice: the same bytes",
		                                          "golden model, --size 120 and icon, built twice: the same bytes",
		                                          "golden model, --fit crash, built twice: the same bytes",
		                                          "golden model, over the limit and --reduce auto, built twice: the same bytes",
		                                          "golden model, --wheels off, built twice: the same bytes"};
		static const char *const goldenNames[5] = {"make-char --fit none", "make-char --fit none --size 120 --icon", "make-char (--fit crash)",
		                                           "make-char --fit none, over the limit (--reduce auto)", "make-char --fit none --wheels off"};
		static const char *const goldens[5] = {RLDMK_GOLDEN_PLAIN, RLDMK_GOLDEN_SIZE_ICON, RLDMK_GOLDEN_FIT, RLDMK_GOLDEN_REDUCED, RLDMK_GOLDEN_NO_WHEELS};
		const u8 *info = (k == 4) ? chriNoWheels : chri;
		const size_t infoSize = (k == 4) ? chriNoWheelsSize : chriSize;
		struct RldMkOptions opt;
		struct RldMkPly ply;
		struct RldMkBuild b;
		struct RldMkPly plyAgain;
		struct RldMkBuild again;
		u8 *file = NULL;
		u8 *fileAgain = NULL;
		size_t size = 0;
		size_t sizeAgain = 0;
		int ok;

		RldMk_TestOptions(&opt);
		opt.sizePercent = (k == 1) ? 120u : 100u;
		opt.fit = (k == 2);
		opt.reduce = (k == 3);
		opt.noWheels = (k == 4);
		RldMk_TestStandard(mesh, (k == 3) ? RldMk_TestSplitOver(RLDMK_REDUCE_LIMIT, &over) : 2u, 0, 0.0, -1);

		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again);
		if (ok)
		{
			file = RldMk_Container(info, infoSize, b.cmdl, b.cmdlSize, (k == 1) ? icon.cicn : NULL, &size);
			fileAgain = RldMk_Container(info, infoSize, again.cmdl, again.cmdlSize, (k == 1) ? icon.cicn : NULL, &sizeAgain);
		}
		ok = ok && (file != NULL) && (fileAgain != NULL) && (size == sizeAgain) && (memcmp(file, fileAgain, size) == 0);

		// With the portrait: three chunks, CICN last, its hash in the directory.
		if (ok && (k == 1))
		{
			const u64 dirAt = Rld_ReadLE64(&file[0x18]);
			const u8 *entry = &file[(size_t)dirAt + 2u * RLD_DIR_ENTRY_SIZE];
			u8 digest[32];

			Sha256(&file[(size_t)Rld_ReadLE64(&entry[0x08])], RLDCHAR_ICON_BYTES, digest);
			ok = (Rld_ReadLE32(&file[0x10]) == 3u) && (memcmp(entry, "CICN", 4u) == 0) && (Rld_ReadLE64(&entry[0x10]) == (u64)RLDCHAR_ICON_BYTES) &&
			     (memcmp(&entry[RLD_DIR_HASH_OFFSET], digest, 32u) == 0);
		}

		RldMk_Test(ok, builtNames[k], "differ");
		if (Rld_CheckGolden(goldenNames[k], file, size, goldens[k]) != 0)
		{
			s_rldMkTestFailed++;
		}

		free(file);
		free(fileAgain);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyAgain);
	}

	free(chri);
	free(chriNoWheels);
}

// THE OWN MASK (--mask-model): a box as the mask - fitted to the retail mask's
// height and centered on its center, one animation of one frame, CMSK by the
// game's rules, built twice the same; a finer box, over the mask's limit,
// reduced under it; the rules CMSK-1..3 and model-anims on a broken copy; a container
// with CMSK last and its golden hash. Without --mask-model nothing changes:
// the five golden hashes above.
#define RLDMK_GOLDEN_MASK "bddb29319b18eace9a99be406977fbf0b5f6d2e31fa7d432059bd23ed49e3a32"

static void RldMk_SelfTestMask(struct RldMkTestMesh *mesh)
{
	static const double lo[3] = {-0.5, 0.0, -0.1};
	static const double hi[3] = {0.5, 1.3, 0.1};
	static const double center[3] = RLDMK_MASK_CENTER;
	u8 *cmsk = NULL;
	size_t cmskSize = 0;
	u32 maskSplit = 1;
	int k;

	while ((12u * maskSplit * maskSplit) <= RLDMK_MASK_TRIANGLES_MAX)
	{
		maskSplit++;
	}

	// The large box first (the first split over the mask's limit): the CMSK of
	// the small one stays for the rules below.
	for (k = 0; k < 2; k++)
	{
		const u32 n = (k == 0) ? maskSplit : 1u;
		struct RldMkOptions opt;
		struct RldMkPly ply;
		struct RldMkPly plyAgain;
		struct RldMkBuild b;
		struct RldMkBuild again;
		struct RldCharModelFacts facts;
		int ok;

		RldMk_TestOptions(&opt);
		opt.maskModel = 1;
		opt.still = 1;
		opt.reduce = 1;
		opt.repair = 1;
		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, lo, hi, n, 30u, 0, -1);

		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && RldMk_TestBuild(mesh, 0, 1, &opt, &plyAgain, &again) && (b.cmdlSize == again.cmdlSize) &&
		     (memcmp(b.cmdl, again.cmdl, b.cmdlSize) == 0);
		if (ok)
		{
			const u8 *body = &b.cmdl[4];
			const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);
			const u32 anim = Rld_ReadLE32(&body[Rld_ReadLE32(&body[header + RLDMK_HEADER_ANIMS])]);
			double boxLo[3];
			double boxHi[3];

			RldMk_Bounds(&b, -1, boxLo, boxHi);
			free(cmsk);
			cmskSize = RLDCHAR_MASK_HEAD_BYTES + b.cmdlSize;
			cmsk = (u8 *)malloc(cmskSize);
			ok = (cmsk != NULL);
			if (ok)
			{
				Rld_WriteLE16(&cmsk[0x00], RLDCHAR_MASK_VERSION);
				Rld_WriteLE16(&cmsk[0x02], 0u);
				memcpy(&cmsk[RLDCHAR_MASK_HEAD_BYTES], b.cmdl, b.cmdlSize);
			}
			ok = ok && (RldChar_CheckMask(cmsk, cmskSize, NULL, NULL, NULL, &facts) == RLDCHAR_VERDICT_OK) &&
			     (Rld_ReadLE32(&body[header + RLDMK_HEADER_NUM_ANIMS]) == 1u) && ((Rld_ReadLE16(&body[anim + RLDMK_ANIM_NUM_FRAMES]) & 0x7FFFu) == 1u) &&
			     (facts.triangles == b.drawCount) && (facts.drawBytes <= RLDCHAR_MASK_DRAW_BYTES_MAX) && (b.framesWritten == 1u);
			ok = ok && ((0.5 * (boxLo[0] + boxHi[0]) - center[0]) < 1.0e-9) && ((0.5 * (boxLo[0] + boxHi[0]) - center[0]) > -1.0e-9) &&
			     ((0.5 * (boxLo[1] + boxHi[1]) - center[1]) < 1.0e-9) && ((0.5 * (boxLo[1] + boxHi[1]) - center[1]) > -1.0e-9) &&
			     ((0.5 * (boxLo[2] + boxHi[2]) - center[2]) < 1.0e-9) && ((0.5 * (boxLo[2] + boxHi[2]) - center[2]) > -1.0e-9) &&
			     ((boxHi[1] - boxLo[1]) > 85.0) && ((boxHi[1] - boxLo[1]) < 87.2);
			if (k == 0)
			{
				ok = ok && b.reduced && (b.reduceFrom == 12u * maskSplit * maskSplit) && (b.triangleCount <= RLDMK_MASK_TRIANGLES_MAX);
			}
		}
		RldMk_Test(ok, (n == 1u) ? "mask: a box of 12 triangles - 1 x 1 frame, 86.1 tall on the retail center, CMSK passes, built twice the same"
		                         : "mask: a box over the mask's limit reduced under it, CMSK passes",
		           "not so");

		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
		RldMk_FreeBuild(&again);
		RldMk_FreePly(&plyAgain);
	}

	if (cmsk == NULL)
	{
		RldMk_Test(0, "mask: the rules", "no mask built");
		return;
	}

	// The rules on broken copies.
	{
		static u8 big[RLDCHAR_MASK_BYTES_MAX + 8u];
		struct RldCharFinding first;
		u8 *m = RldMk_TestCopy(cmsk, cmskSize);

		if (m != NULL)
		{
			const u8 *body = &m[RLDCHAR_MASK_HEAD_BYTES + 4u];
			const u32 header = Rld_ReadLE32(&body[RLDMK_MODEL_HEADERS]);

			Rld_WriteLE16(&m[0x00], 2u);
			RldMk_Test((RldChar_CheckMask(m, cmskSize, &first, NULL, NULL, NULL) != RLDCHAR_VERDICT_OK) && (strcmp(first.rule, "CMSK-1") == 0),
			           "mask: version 2 ignored (CMSK-1)", "not so");
			Rld_WriteLE16(&m[0x00], RLDCHAR_MASK_VERSION);
			Rld_WriteLE16(&m[0x02], 1u);
			RldMk_Test((RldChar_CheckMask(m, cmskSize, &first, NULL, NULL, NULL) != RLDCHAR_VERDICT_OK) && (strcmp(first.rule, "CMSK-2") == 0),
			           "mask: flags 1 ignored (CMSK-2)", "not so");
			Rld_WriteLE16(&m[0x02], 0u);
			Rld_WriteLE32(&m[RLDCHAR_MASK_HEAD_BYTES + 4u + header + RLDMK_HEADER_NUM_ANIMS], 2u);
			RldMk_Test((RldChar_CheckMask(m, cmskSize, &first, NULL, NULL, NULL) != RLDCHAR_VERDICT_OK) && (first.rule != NULL) &&
			               (strncmp(first.rule, "model-", 6) == 0),
			           "mask: two animations ignored (model rules)", "rule %s", (first.rule != NULL) ? first.rule : "none");
			free(m);
		}

		memset(big, 0, sizeof(big));
		memcpy(big, cmsk, cmskSize);
		RldMk_Test((RldChar_CheckMask(big, RLDCHAR_MASK_BYTES_MAX + 4u, &first, NULL, NULL, NULL) == RLDCHAR_VERDICT_MODEL_LIMIT) &&
		               (strcmp(first.rule, "CMSK-3") == 0),
		           "mask: CMSK over 16 KiB ignored (CMSK-3)", "not so");
	}

	// A container with CMSK: three chunks (no portrait), CMSK last with its
	// hash; the golden.
	{
		u8 field[RLDCHAR_NAME_FIELD];
		struct RldMkOptions opt;
		struct RldMkPly ply;
		struct RldMkBuild b;
		size_t chriSize = 0;
		size_t size = 0;
		u8 *chri;
		u8 *file = NULL;
		int ok;

		memset(field, 0, sizeof(field));
		memcpy(field, "SELF TEST", 9u);
		chri = RldMk_BuildChri(14u, 0u, field, 1u, "", 0u, &chriSize);
		RldMk_TestOptions(&opt);
		RldMk_TestStandard(mesh, 2u, 0, 0.0, -1);
		ok = (chri != NULL) && RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		if (ok)
		{
			file = RldMk_ContainerMask(chri, chriSize, b.cmdl, b.cmdlSize, NULL, cmsk, cmskSize, &size);
		}
		ok = ok && (file != NULL);
		if (ok)
		{
			const u64 dirAt = Rld_ReadLE64(&file[0x18]);
			const u8 *entry = &file[(size_t)dirAt + 2u * RLD_DIR_ENTRY_SIZE];
			u8 digest[32];

			Sha256(&file[(size_t)Rld_ReadLE64(&entry[0x08])], cmskSize, digest);
			ok = (Rld_ReadLE32(&file[0x10]) == 3u) && (memcmp(entry, "CMSK", 4u) == 0) && (Rld_ReadLE64(&entry[0x10]) == (u64)cmskSize) &&
			     (memcmp(&entry[RLD_DIR_HASH_OFFSET], digest, 32u) == 0) && (RldChar_ChunkLimit(entry) == RLDCHAR_LIMIT_CMSK);
		}
		RldMk_Test(ok, "mask: container with CMSK last, its hash in the directory", "not so");
		if (Rld_CheckGolden("make-char --fit none --mask-model (a box)", file, size, RLDMK_GOLDEN_MASK) != 0)
		{
			s_rldMkTestFailed++;
		}

		free(file);
		free(chri);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	free(cmsk);
}

#include "rldpack_obj_test.inc"

static void RldMk_SelfTestShortVehicle(struct RldMkTestMesh *mesh)
{
	static const double vehicleLo[3] = {-0.40, 0.10, -0.50};
	static const double vehicleHi[3] = {0.40, 0.40, 0.50};
	static const double driverLo[3] = {-0.25, 0.42, -0.30};
	static const double driverHi[3] = {0.25, 2.00, 0.20};
	// figure, box: lo x y z, hi x y z (an unused box has hi[0] 0)
	static const double figures[4][3][6] = {
	    {{-0.25, 0.12, -0.15, 0.25, 1.80, 0.15}, {-0.25, 0.00, -0.14, -0.08, 0.08, 0.14}, {0.08, 0.00, -0.14, 0.25, 0.08, 0.14}},
	    {{-0.20, 0.00, -0.25, 0.20, 1.80, 0.25}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}},
	    {{-0.20, 0.00, -0.15, 0.20, 1.00, 0.15}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}},
	    {{-0.20, 0.00, -0.15, 0.20, 0.60, 0.25}, {-0.25, 0.60, -0.20, 0.25, 1.00, 0.20}, {0, 0, 0, 0, 0, 0}}};
	static const char *const figureNames[4] = {"on two shoes", "of one piece 1.8 x 0.5", "of one piece 1.0 x 0.3", "legs of one piece and a head"};
	int f;
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild build;
	int wheelsOff;
	int ok;

	mesh->vertexCount = 0;
	mesh->faceCount = 0;
	RldMk_TestBox(mesh, vehicleLo, vehicleHi, 1u, 0u, 0, -1);
	RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
	for (wheelsOff = 1; wheelsOff >= 0; wheelsOff--)
	{
		RldMk_TestOptions(&opt);
		opt.fit = 1;
		opt.noWheels = wheelsOff;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &build);
		if (wheelsOff)
		{
			RldMk_Test(ok && RldMk_Said("ply-kart-short") && !RldMk_Said("ply-no-kart") && (build.kartPart == 0) && (build.fitFactor < 1.25),
			           "--wheels off, a vehicle shorter than a kart: it is the kart (ply-kart-short)", "built %d, kart part %d, x%.4g, said: %s", ok,
			           build.kartPart, build.fitFactor, s_rldMkCodes);
		}
		else
		{
			RldMk_Test(!ok && RldMk_Said("ply-no-kart") && !RldMk_Said("ply-kart-short"), "--wheels on, a vehicle shorter than a kart: ply-no-kart",
			           "built %d, said: %s", ok, s_rldMkCodes);
		}
		RldMk_FreeBuild(&build);
		RldMk_FreePly(&ply);
	}

	for (f = 0; f < 4; f++)
	{
		int box;

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		for (box = 0; box < 3; box++)
		{
			if (figures[f][box][3] != 0.0)
			{
				RldMk_TestBox(mesh, &figures[f][box][0], &figures[f][box][3], 2u, 10u * (u32)box, 0, -1);
			}
		}
		for (wheelsOff = 1; wheelsOff >= 0; wheelsOff--)
		{
			char name[96];

			RldMk_TestOptions(&opt);
			opt.fit = 1;
			opt.noWheels = wheelsOff;
			ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &build);
			snprintf(name, sizeof(name), "--wheels %s, a figure %s: no kart, no hint (ply-no-kart)", wheelsOff ? "off" : "on", figureNames[f]);
			RldMk_Test(!ok && RldMk_Said("ply-no-kart") && !RldMk_Said("ply-kart-short") && ((build.parts == NULL) || (RldMk_ShortVehicle(&build) < 0)),
			           name, "built %d, said: %s", ok, s_rldMkCodes);
			RldMk_FreeBuild(&build);
			RldMk_FreePly(&ply);
		}
	}
}

// THE PARTS: which part goes with the kart, which with the driver.
static void RldMk_SelfTestParts(struct RldMkTestMesh *mesh)
{
	static const double kartLo[3] = {-0.45, 0.10, -0.90};
	static const double kartHi[3] = {0.45, 0.35, 0.90};
	static const double driverLo[3] = {-0.25, 0.60, -0.40};
	static const double driverHi[3] = {0.25, 1.20, 0.10};
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	int lift;
	int ok;

	// A HULL RESTING ON THE KART: its bottom exactly at the kart's top (a tank's
	// hull on its chassis) stays with the kart; lifted by 0.01 it is something
	// else and leans with the driver.
	for (lift = 0; lift < 2; lift++)
	{
		const double hullLo[3] = {-0.35, 0.35 + 0.01 * (double)lift, -0.70};
		const double hullHi[3] = {0.35, 0.55 + 0.01 * (double)lift, 0.70};
		char name[96];

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
		RldMk_TestBox(mesh, hullLo, hullHi, 1u, 30u, 0, -1);
		RldMk_TestOptions(&opt);
		opt.noWheels = 1;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && (b.partCount == 3u) && (b.driverPart >= 0) && (b.parts[b.driverPart].triangles == 48u) &&
		     (b.groupTriangles[RLDMK_GROUP_KART] == (lift ? 12u : 24u)) && (b.groupTriangles[RLDMK_GROUP_DRIVER] == (lift ? 60u : 48u));
		snprintf(name, sizeof(name), "parts: a hull %s the kart's top %s", lift ? "0.01 above" : "resting on", lift ? "leans with the driver" : "stays with the kart");
		RldMk_Test(ok, name, "parts %u, driver part %d, kart %u, driver %u, said: %s", b.partCount, b.driverPart, b.groupTriangles[RLDMK_GROUP_KART],
		           b.groupTriangles[RLDMK_GROUP_DRIVER], s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// PLY-CENTER: the kart 0.5 to the side in the file. Fitted onto the dummy
	// the kart is centered anyway: info, in model units (0.5, not the fitted
	// value); --fit none at --scale 1.2: a warning, 0.5 as well.
	for (lift = 0; lift < 2; lift++)
	{
		const double lo[3] = {0.05, 0.10, -0.90};
		const double hi[3] = {0.95, 0.35, 0.90};
		const double dLo[3] = {0.25, 0.40, -0.40};
		const double dHi[3] = {0.75, 1.00, 0.10};
		u32 warningsBefore;

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, lo, hi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, dLo, dHi, 2u, 10u, 0, -1);
		RldMk_TestOptions(&opt);
		opt.fit = (lift == 0);
		opt.scale = (lift == 0) ? 1.0 : 1.2;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		warningsBefore = (u32)s_rldMkWarnings;
		if (lift == 0)
		{
			RldMk_Test(ok && b.dummy && RldMk_Said("ply-center") && (warningsBefore == 0u) && (b.center > 0.4999) && (b.center < 0.5001),
			           "ply-center fitted onto the dummy: info, 0.50 model units as in the file", "built %d, dummy %d, center %.4f, warnings %u, said: %s", ok,
			           b.dummy, b.center, warningsBefore, s_rldMkCodes);
		}
		else
		{
			RldMk_Test(ok && !b.dummy && RldMk_Said("ply-center") && (warningsBefore >= 1u) && (b.center > 0.4999) && (b.center < 0.5001),
			           "ply-center with --fit none --scale 1.2: warning, 0.50 model units as in the file", "built %d, center %.4f, warnings %u, said: %s", ok,
			           b.center, warningsBefore, s_rldMkCodes);
		}
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// FIT-CAPPED: a driver 2.5 tall on a kart 1.8 long - the height decides
	// the fit, the kart ends up shorter than Crash's: a warning. 1.0 tall: none.
	for (lift = 0; lift < 2; lift++)
	{
		const double dHi[3] = {0.25, lift ? 1.00 : 2.50, 0.10};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, dHi, 2u, 10u, 0, -1);
		RldMk_TestOptions(&opt);
		opt.fit = 1;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
		RldMk_Test(ok && (b.fitBasis == RLDMK_FIT_KART) && ((b.fitCapped != 0) == (lift == 0)) && (RldMk_Said("fit-capped") == (lift == 0)),
		           lift ? "fit not capped: no fit-capped" : "fit capped by the height: fit-capped", "built %d, basis %d, capped %d, said: %s", ok, b.fitBasis,
		           b.fitCapped, s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}

	// PLY-DRIVER: (0) a detailed spoiler is taken as the driver, the real
	// driver (12 triangles, reaching into the kart) stands still: warned.
	// (1) a separate large head above the body moves with it, (2) a thin
	// antenna on the kart reaches higher than the driver: no warning.
	for (lift = 0; lift < 3; lift++)
	{
		static const double boxes[3][2][6] = {
		    {{-0.25, 0.25, -0.40, 0.25, 1.20, 0.10}, {-0.40, 0.40, -0.85, 0.40, 0.55, -0.70}},
		    {{-0.25, 0.25, -0.30, 0.25, 1.00, 0.10}, {-0.35, 1.00, -0.35, 0.35, 1.60, 0.25}},
		    {{-0.25, 0.40, -0.30, 0.25, 1.00, 0.10}, {-0.02, 0.20, -0.80, 0.02, 1.90, -0.76}}};
		static const u32 splits[3][2] = {{1u, 3u}, {3u, 1u}, {3u, 1u}};
		static const char *const names[3] = {"parts: a detailed spoiler taken as the driver, the real one still: ply-driver",
		                                     "parts: a separate large head above the body: no ply-driver",
		                                     "parts: a thin antenna on the kart higher than the driver: no ply-driver"};

		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, &boxes[lift][0][0], &boxes[lift][0][3], splits[lift][0], 10u, 0, -1);
		RldMk_TestBox(mesh, &boxes[lift][1][0], &boxes[lift][1][3], splits[lift][1], 20u, 0, -1);
		RldMk_TestOptions(&opt);
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && (RldMk_Said("ply-driver") == (lift == 0));
		RldMk_Test(ok, names[lift], "said: %s", s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}
}

// THE VEHICLE UNDER THE DRIVER: a sword beside a figure (wheels on and off)
// and a tail behind it are no kart; a board a figure reaches a little past is.
static void RldMk_SelfTestUnderDriver(struct RldMkTestMesh *mesh)
{
	static const double boxes[3][2][6] = {
	    {{-0.04, 0.00, -0.80, 0.04, 0.05, 0.80}, {-0.90, 0.00, -0.20, -0.40, 1.80, 0.20}},
	    {{-0.05, 0.00, -1.00, 0.05, 0.08, 0.00}, {-0.25, 0.00, 0.05, 0.25, 1.80, 0.55}},
	    {{-0.20, 0.00, -0.15, 0.20, 0.05, 0.85}, {-0.15, 0.06, -0.70, 0.15, 1.80, 0.30}}};
	static const char *const names[3] = {"a sword beside a figure", "a tail behind a figure", "a board a figure reaches 5 % past"};
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	int model;
	int wheelsOff;

	for (model = 0; model < 3; model++)
	{
		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, &boxes[model][0][0], &boxes[model][0][3], 1u, 0u, 0, -1);
		RldMk_TestBox(mesh, &boxes[model][1][0], &boxes[model][1][3], 2u, 10u, 0, -1);
		for (wheelsOff = 1; wheelsOff >= ((model == 0) ? 0 : 1); wheelsOff--)
		{
			char name[128];
			int ok;

			RldMk_TestOptions(&opt);
			opt.fit = 1;
			opt.noWheels = wheelsOff;
			ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b);
			snprintf(name, sizeof(name), "--wheels %s, %s: %s", wheelsOff ? "off" : "on", names[model], (model == 2) ? "the board is the kart" : "no kart (ply-no-kart)");
			if (model == 2)
			{
				RldMk_Test(ok && (b.kartPart >= 0) && (b.parts[b.kartPart].triangles == 12u), name, "built %d, said: %s", ok, s_rldMkCodes);
			}
			else
			{
				RldMk_Test(!ok && RldMk_Said("ply-no-kart") && !RldMk_Said("ply-kart-short"), name, "built %d, said: %s", ok, s_rldMkCodes);
			}
			RldMk_FreeBuild(&b);
			RldMk_FreePly(&ply);
		}
	}
}

// THE FIT ON SPLIT SEAMS: kart, driver and steering wheel with every corner
// of every face a vertex of its own, moved by up to 1e-5 or 2e-5 (an export
// with split seams; the kart's faces a quarter of its length) are fitted like
// the clean model - the same factor, its kart the measure (with --repair
// auto, the default).
static void RldMk_SelfTestFitSeams(struct RldMkTestMesh *mesh)
{
	static const double noises[3] = {0.0, 1.0e-5, 2.0e-5};
	static const double kartLo[3] = {-0.45, 0.10, -0.90};
	static const double kartHi[3] = {0.45, 0.35, 0.90};
	static const double driverLo[3] = {-0.25, 0.40, -0.40};
	static const double driverHi[3] = {0.25, 1.00, 0.10};
	static const double wheelLo[3] = {-0.15, 0.50, 0.30};
	static const double wheelHi[3] = {0.15, 0.60, 0.36};
	struct RldMkOptions opt;
	struct RldMkPly ply;
	struct RldMkBuild b;
	double clean = 0.0;
	int n;

	for (n = 0; n < 3; n++)
	{
		char name[96];
		u32 f;
		int ok;

		// kart, driver and steering wheel, then every corner of every face a
		// vertex of its own, moved
		mesh->vertexCount = 0;
		mesh->faceCount = 0;
		RldMk_TestBox(mesh, kartLo, kartHi, 4u, 0u, 0, -1);
		RldMk_TestBox(mesh, driverLo, driverHi, 2u, 10u, 0, -1);
		RldMk_TestBox(mesh, wheelLo, wheelHi, 1u, 20u, 0, -1);
		for (f = 0; f < mesh->faceCount; f++)
		{
			u32 k;

			for (k = 0; k < mesh->cornerCount[f]; k++)
			{
				const u32 from = mesh->corners[f][k];
				const u32 v = mesh->vertexCount;
				int a;

				if (v >= RLDMK_TEST_VERTICES)
				{
					break;
				}
				for (a = 0; a < 3; a++)
				{
					// -1 .. +1 in eleven fixed steps, different per corner and axis
					const double step = (double)((int)((v * 7u + (u32)a * 3u) % 11u) - 5) / 5.0;

					mesh->xyz[v][a] = mesh->xyz[from][a] + noises[n] * step;
				}
				memcpy(mesh->rgb[v], mesh->rgb[from], 3u);
				mesh->corners[f][k] = v;
				mesh->vertexCount++;
			}
		}
		RldMk_TestOptions(&opt);
		opt.fit = 1;
		opt.repair = 1;
		ok = RldMk_TestBuild(mesh, 0, 1, &opt, &ply, &b) && b.fitted && (b.fitBasis == RLDMK_FIT_KART);
		if (n == 0)
		{
			clean = b.fitFactor;
		}
		snprintf(name, sizeof(name), "--fit crash on split seams moved by %g: the kart is the measure, x%.4g", noises[n], clean);
		RldMk_Test(ok && (b.fitFactor == clean), name, "built %d, basis %d, x%.4g, said: %s", ok, b.fitBasis, b.fitFactor, s_rldMkCodes);
		RldMk_FreeBuild(&b);
		RldMk_FreePly(&ply);
	}
}

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
	RldMk_SelfTestSize(mesh);
	RldMk_SelfTestFit(mesh);
	RldMk_SelfTestReduce(mesh);
	RldMk_SelfTestPreview(mesh);
	RldMk_SelfTestDiagnose(mesh);
	RldMk_SelfTestVehicle(mesh);
	RldMk_SelfTestRemesh(mesh);
	RldMk_SelfTestRepair(mesh);
	s_rldMkTestFailed += RldDum_SelfTest(RldMk_TestReport);
	RldMk_SelfTestPng();
	RldMk_SelfTestVoices(good, goodSize);
	RldMk_SelfTestMask(mesh);
	{
		struct RldPngImage pictureA;

		if (RldMk_TestPictureA(&pictureA))
		{
			RldMk_SelfTestIcon(&pictureA);
			RldMk_SelfTestIconOptions(&pictureA);
			RldMk_SelfTestGolden(mesh, &pictureA);
		}
		else
		{
			RldMk_Test(0, "icon and golden hashes", "test picture A cannot be read");
		}
		RldPng_Free(&pictureA);
	}
	RldMk_SelfTestObj(mesh);
	RldMk_SelfTestShortVehicle(mesh);
	RldMk_SelfTestParts(mesh);
	RldMk_SelfTestUnderDriver(mesh);
	RldMk_SelfTestFitSeams(mesh);
	s_rldMkQuiet = quietBefore;
	RldMk_ResetSaid();

	free(good);
	free(mesh);
	return s_rldMkTestFailed;
}
