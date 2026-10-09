// rldpack - the packer for .rldtrack containers
//
// A program of its own, not part of the game. A track author uses it to build
// their container and passes on one file. The same source also runs inside
// Reload Studio as `ReloadStudio.exe --rldpack <command> ...`
// (tools/reloadstudio/rs_rldpack.c).
//
// NO SIGNING PATH ANY MORE.
//
// This is where `keygen`, reading and writing author.key/author.pub, the
// SIGN chunk and the signature check in `verify` used to be. All gone; the reason
// is in the head of include/rldtrack.inc.
//
// VERSION 4.1: a container consists of META, LEVD, VRMD and optionally SNDB
// and PARM. The packer no longer writes MMAP, the game shrinks the menu map
// itself.
//
// ALL WRITTEN IN-HOUSE, AND HERE THAT IS A DECISION, NOT A NECESSITY.
//
// SHA-256 is in include/rldtrack.inc in full. No library, no
// vendoring - this tree has neither zlib nor libsodium, and a dependency
// an author first has to obtain is a hurdle in front of the tool that is
// meant to remove the hurdles. The one piece taken from elsewhere is a
// reader, not a check: the JPEG and TGA decoders of stb_image for the
// textures of OBJ models (tools/rldpack_image.c, from the SDL copy in this
// tree, so nothing has to be obtained either).
//
// The game pulls in the same file to read containers. Written once,
// used by two sides: if the hash path existed here and there once each,
// the packer could at some point write what the game rejects.
//
// The price for that is that the hash MUST be right. That is why the program
// carries the test vector from RFC 6234 with it and checks itself against it with
// `rldpack selftest`. A build that fails the self-test has not built a valid
// container, no matter what it looks like - and because it is the same lines,
// the verdict also holds for the reading side in the game.

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_RAND_S

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

// FLOATING POINT: STOP THE BUILD, NOT THE BYTES.
//
// make-char computes in double, and a container must come out byte for byte
// the same on every machine (the goldens in tools/rldpack_char.inc). That
// holds when every + - * / is rounded to double on its own: SSE2, no
// contraction into FMA, no reordering, no 80-bit x87 intermediates.
// CMakeLists.txt pins the compiler flags for that. These checks only end a
// build in which something overrode them anyway - a build that passes them
// compiles exactly the same code, there is no second path behind them.
#if defined(_M_FP_FAST) || defined(__FAST_MATH__)
#error "rldpack must not be built with fast math (/fp:fast, -ffast-math): it changes the bytes of containers"
#endif
#if defined(_M_FP_CONTRACT)
#error "rldpack must not be built with /fp:contract: FMA contraction changes the bytes of containers"
#endif
#if defined(_M_IX86_FP) && (_M_IX86_FP < 2)
#error "rldpack needs /arch:SSE2: x87 arithmetic changes the bytes of containers"
#endif
#if defined(__FLT_EVAL_METHOD__) && (__FLT_EVAL_METHOD__ != 0)
#error "rldpack needs double evaluated as double (-msse2 -mfpmath=sse): x87 intermediates change the bytes of containers"
#endif

// For make: list the track folder and resolve paths.
#if defined(_WIN32)
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed long long s64;
typedef signed short s16;
typedef signed int s32;

#define RLD_ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// The format itself: hash, layout, reader. With the build switch, because the packer
// is the side that writes. The game pulls in the same file without it.
#define RLDTRACK_WITH_BUILDER
#include <rldtrack.inc>
#include <rldchar.inc>

// The PNG reader for make-char --icon and CTXT. include/rldchar.inc pulls it
// in already: the game decodes the textures of CTXT with it (preview).
#include <rldpng.inc>

// The mip levels of a native texture, the game's code (platform/native_tex.c
// builds them with it). Only the self-test uses it here: it proves that the
// levels Reload Studio builds for its preview are the game's.
#include <rldmip.inc>

// glTF models (tools/rldpack_gltf.inc): cgltf v1.15 (externals/cgltf/cgltf.h,
// MIT, see THIRD_PARTY_NOTICES.md), its implementation compiled here, once.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// Build ID (CTR_NATIVE_BUILD_ID), generated on every build - see Rld_Usage.
#include "ctr_build_id.h"

// THE GAME'S DISC IMAGE READER (platform/native_disc_image.c), for make and
// for make-char --icon-preview.
//
// Declared by hand here and not via native_disc_image.h: that header pulls in
// macros.h, and its types are already defined at the top of this file. The
// functions below, with exactly the signatures from the header.
int NativeDiscImage_OpenImagePath(const char *path);
int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut);

// And for the retail portrait of make-char --icon-preview: one file of the image
// and some of its sectors (the same struct as in native_disc_image.h).
struct NativeDiscImageFile
{
	u32 lba;
	u32 size;
};
int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut);
int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);

// In the game the reader reports through Platform_Log. rldpack has no log; what
// an author needs to know, the report of make says itself.
void Platform_Log(const char *format, ...)
{
	(void)format;
}

//========================================================================================
// THE MACHINE LINES (--machine)
//========================================================================================
//
// For Reload Studio (tools/reloadstudio). With --machine, rldpack writes, next to
// its report, lines that a GUI can read without taking the report
// apart: each starts with '@', the fields are separated by a TAB. The protocol is
// in tools/reloadstudio/reloadstudio.h. Without --machine not one character of the
// output changes, and never one of the container.
//
// A message for authors (@msg) has its own text without switch names and
// source locations. It sits here next to the check that triggers it, and
// nowhere else - the GUI only shows it and checks nothing itself.

static int s_machine;
static int s_machineResult; // @result has been written

// Appends a field: TAB and line ends become a space, the
// indentation of a continuation line ("\n         ") is dropped as well.
static void Rld_MachineAppend(char *line, size_t size, size_t *at, const char *text)
{
	const char *p = (text != NULL) ? text : "";

	while ((*p != '\0') && ((*at + 1u) < size))
	{
		if ((*p == '\n') || (*p == '\r') || (*p == '\t'))
		{
			while ((*p == '\n') || (*p == '\r') || (*p == '\t') || (*p == ' '))
			{
				p++;
			}
			line[(*at)++] = ' ';
			continue;
		}
		line[(*at)++] = *p++;
	}
	line[*at] = '\0';
}

// One line: the kind and its fields, the list ends with NULL. One call,
// one fputs - stdout is unbuffered with --machine.
static void Rld_Emit(const char *kind, ...)
{
	static char line[4096];
	size_t at = 0;
	va_list list;
	const char *field;

	if (!s_machine)
	{
		return;
	}

	line[at++] = '@';
	Rld_MachineAppend(line, sizeof(line) - 1u, &at, kind);
	va_start(list, kind);
	while ((field = va_arg(list, const char *)) != NULL)
	{
		if ((at + 2u) < sizeof(line))
		{
			line[at++] = '\t';
		}
		Rld_MachineAppend(line, sizeof(line) - 1u, &at, field);
	}
	va_end(list);
	line[at++] = '\n';
	line[at] = '\0';
	fputs(line, stdout);
}

// A message for authors: severity (error, warning, note, info), code, the
// technical line (may be NULL) and the text as a printf format.
static void Rld_Say(const char *severity, const char *code, const char *technical, const char *format, ...)
{
	char text[1024];
	va_list list;

	if (!s_machine)
	{
		return;
	}

	va_start(list, format);
	vsnprintf(text, sizeof(text), format, list);
	va_end(list);
	Rld_Emit("msg", severity, code, text, (technical != NULL) ? technical : "", (const char *)NULL);
}

static void Rld_EmitNumber(const char *kind, const char *key, unsigned long long number)
{
	char text[24];

	snprintf(text, sizeof(text), "%llu", number);
	Rld_Emit(kind, key, text, (const char *)NULL);
}

// The largest file Rld_ReadFile reads. Models, track files and sounds stay far
// below it. Above it the 32-bit process would fail on its own - malloc from
// about 1 GB, ftell (a 32-bit long) from 2 GB - and the caller could only say
// "cannot be opened", which sends an author looking for a wrong path.
#define RLD_READ_MAX (512ul * 1024ul * 1024ul)

// Set by Rld_ReadFile when it refused a file for its size (and said so),
// cleared on every call. A caller can leave out its own "cannot be opened".
static int s_rldReadTooLarge;

static void Rld_SayTooLarge(const char *path, const char *size)
{
	s_rldReadTooLarge = 1;
	fprintf(stderr, "rldpack: %s is too large (%s, at most 512 MiB)\n", path, size);
	Rld_Say("error", "file-too-large", size, "The file %s is too large: at most 512 MiB can be read.", path);
}

static int Rld_ReadFile(const char *path, u8 **dataOut, size_t *sizeOut)
{
	FILE *file = fopen(path, "rb");
	long size = -1;
	u8 *data;

	*dataOut = NULL;
	*sizeOut = 0;
	s_rldReadTooLarge = 0;

	if (file == NULL)
	{
		return 0;
	}

	if ((fseek(file, 0, SEEK_END) != 0) || ((size = ftell(file)) < 0))
	{
		// No size - with a 32-bit long that is a file of 2 GB or more. One
		// byte read behind the limit tells whether that is the reason.
		int large = (fseek(file, (long)RLD_READ_MAX, SEEK_SET) == 0) && (fgetc(file) != EOF);

		fclose(file);
		if (large)
		{
			Rld_SayTooLarge(path, "size not determined, more than 536870912 bytes");
		}
		return 0;
	}

	if ((unsigned long)size > RLD_READ_MAX)
	{
		char text[48];

		fclose(file);
		snprintf(text, sizeof(text), "%ld bytes", size);
		Rld_SayTooLarge(path, text);
		return 0;
	}

	if (fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}

	data = (u8 *)malloc((size_t)size + 1u);
	if (data == NULL)
	{
		fclose(file);
		return 0;
	}

	if ((size != 0) && (fread(data, 1, (size_t)size, file) != (size_t)size))
	{
		free(data);
		fclose(file);
		return 0;
	}

	fclose(file);
	data[size] = 0;
	*dataOut = data;
	*sizeOut = (size_t)size;
	return 1;
}

static int Rld_HexToBytes(const char *text, u8 *out, size_t count)
{
	size_t i;

	if (strlen(text) != (count * 2u))
	{
		return 0;
	}

	for (i = 0; i < (count * 2u); i++)
	{
		char c = text[i];
		int value;

		if ((c >= '0') && (c <= '9'))
		{
			value = c - '0';
		}
		else if ((c >= 'a') && (c <= 'f'))
		{
			value = 10 + (c - 'a');
		}
		else if ((c >= 'A') && (c <= 'F'))
		{
			value = 10 + (c - 'A');
		}
		else
		{
			return 0;
		}

		if ((i & 1u) == 0)
		{
			out[i / 2u] = (u8)(value << 4);
		}
		else
		{
			out[i / 2u] |= (u8)value;
		}
	}

	return 1;
}

static void Rld_PrintHex(const u8 *data, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
	{
		printf("%02x", data[i]);
	}
}

//----------------------------------------------------------------------------------------
// META

// --modes race,time - the same list that Rld_ModesText shows. 0 means there
// was something in it that is not a mode; the caller reports that.
static u32 Rld_ParseModes(const char *text)
{
	u32 modes = 0;

	while (*text != '\0')
	{
		size_t length = 0;
		int known = 0;

		while ((text[length] != '\0') && (text[length] != ','))
		{
			length++;
		}

#define RLD_MODE_MATCH(bit, word, label, tag)                                     \
	if ((length == strlen(word)) && (memcmp(text, (word), length) == 0))       \
	{                                                                          \
		modes |= (bit);                                                          \
		known = 1;                                                               \
	}
		RLD_MODE_LIST(RLD_MODE_MATCH)
#undef RLD_MODE_MATCH

		if (!known)
		{
			return 0;
		}

		text += length;
		if (*text == ',')
		{
			text++;
		}
	}

	return modes;
}

static const char *const s_metaStringNames[RLD_STRING_COUNT] = {"track_name", "author_name"};
static const u32 s_metaStringLimits[RLD_STRING_COUNT] = {64, 64};

// The switch the author actually types. "track_name" is the field name from
// the format and tells them nothing; "--name" is in their command line.
static const char *const s_metaStringFlags[RLD_STRING_COUNT] = {"--name", "--author"};

// META is UTF-8 by the format. The packer used to take any byte sequence -
// a Windows console in code page 1252 delivers for umlauts exactly what does
// not belong here, and the game would get a string it cannot
// parse. It can be detected before writing, so it is checked.
static int Rld_IsUtf8(const char *text)
{
	const u8 *p = (const u8 *)text;

	while (*p != 0u)
	{
		int extra;
		int i;

		if (*p < 0x80u)
		{
			p++;
			continue;
		}
		else if ((*p & 0xE0u) == 0xC0u) { extra = 1; }
		else if ((*p & 0xF0u) == 0xE0u) { extra = 2; }
		else if ((*p & 0xF8u) == 0xF0u) { extra = 3; }
		else { return 0; }

		p++;
		for (i = 0; i < extra; i++)
		{
			if ((*p & 0xC0u) != 0x80u)
			{
				return 0;
			}
			p++;
		}
	}

	return 1;
}

// The 32 bytes at 0x04 stay zero. publicKey used to be there; the gap remains because
// closing it would have shifted every field behind it and so read every existing
// container wrong - see struct RldMeta in rldtrack.inc.
static u8 *Rld_BuildMeta(u32 trackVersion, u32 modes, const struct RldMemNeed *need,
                         const char *const strings[RLD_STRING_COUNT], size_t *sizeOut)
{
	size_t size = RLD_META_FIXED_SIZE;
	u8 *data;
	size_t at;
	int i;

	for (i = 0; i < RLD_STRING_COUNT; i++)
	{
		size += 4u + strlen(strings[i]);
	}

	data = (u8 *)calloc(1, size);
	if (data == NULL)
	{
		return NULL;
	}

	Rld_WriteLE32(&data[0x00], RLD_META_VERSION);
	// 0x04..0x23 stays zero - calloc has already done that.
	Rld_WriteLE32(&data[0x24], trackVersion);
	Rld_WriteLE32(&data[0x28], modes);
	Rld_WriteLE32(&data[0x2c], need->primBytes);
	Rld_WriteLE32(&data[0x30], need->total);
	Rld_WriteLE32(&data[0x34], RLD_STRING_COUNT);

	at = RLD_META_FIXED_SIZE;
	for (i = 0; i < RLD_STRING_COUNT; i++)
	{
		size_t length = strlen(strings[i]);

		Rld_WriteLE32(&data[at], (u32)length);
		memcpy(&data[at + 4u], strings[i], length);
		at += 4u + length;
	}

	*sizeOut = size;
	return data;
}

//========================================================================================
// BUILDING
//========================================================================================

struct RldBuildInput
{
	const u8 *lev;
	size_t levSize;
	const u8 *vrm;
	size_t vrmSize;

	// Optional. NULL means: three-part container, and that is the normal case
	// for a track that keeps the sound of its host slot.
	const u8 *sndb;
	size_t sndbSize;

	// Optional (4.1): the values of the track. NULL means: no value set, the
	// game uses its defaults (Rld_BuildParm).
	const u8 *parm;
	size_t parmSize;

	u32 trackVersion;
	u32 modes;

	const char *strings[RLD_STRING_COUNT];
};

static void Rld_AddChunk(struct RldContainer *container, const char type[4], const u8 *raw, size_t rawSize, u32 compression)
{
	struct RldChunk *chunk = &container->chunks[container->chunkCount];

	memcpy(chunk->type, type, 4);
	chunk->type[4] = '\0';
	chunk->flags = compression;
	chunk->sizeRaw = (u64)rawSize;
	chunk->sizeStored = (u64)rawSize; // compression method 0: the format compresses nothing
	chunk->offset = 0;

	Sha256(raw, rawSize, chunk->hash);

	chunk->stored = (u8 *)malloc(rawSize ? rawSize : 1u);
	memcpy(chunk->stored, raw, rawSize);

	container->chunkCount++;
}

// Builds the whole container in memory. Separate from writing, so that the
// self-test can build it twice and compare byte for byte - the
// determinism the format requires is thus a checked property and not
// an intention.
static u8 *Rld_Build(const struct RldBuildInput *in, size_t *sizeOut, const char **errorOut)
{
	struct RldContainer container;
	struct RldMemNeed need;
	u8 *meta;
	size_t metaSize;
	u8 *out;
	int i;

	*sizeOut = 0;
	*errorOut = NULL;

	memset(&container, 0, sizeof(container));

	if (in->levSize > RLD_LIMIT_LEVD)
	{
		*errorOut = "LEV is larger than the format allows (16 MB)";
		return NULL;
	}
	if (in->vrmSize > RLD_LIMIT_VRMD)
	{
		*errorOut = "VRM is larger than the format allows (4 MB)";
		return NULL;
	}
	if ((in->sndb != NULL) && (in->sndbSize > RLD_LIMIT_SNDB))
	{
		*errorOut = "SNDB is larger than the format allows (4 MB)";
		return NULL;
	}
	if ((in->parm != NULL) && (in->parmSize > RLD_LIMIT_PARM))
	{
		*errorOut = "PARM is larger than the format allows (4 KB)";
		return NULL;
	}
	// Previously the whole error message here was the field name: "rldpack:
	// track_name". That does not tell an author what is too long, by how much, and
	// which switch they have to shorten.
	for (i = 0; i < RLD_STRING_COUNT; i++)
	{
		static char message[160];
		size_t length = strlen(in->strings[i]);

		if (length > s_metaStringLimits[i])
		{
			snprintf(message, sizeof(message), "%s is %u bytes long, at most %u are allowed (field %s)", s_metaStringFlags[i], (unsigned int)length,
			         (unsigned int)s_metaStringLimits[i], s_metaStringNames[i]);
			*errorOut = message;
			return NULL;
		}

		if (!Rld_IsUtf8(in->strings[i]))
		{
			snprintf(message, sizeof(message), "%s is not valid UTF-8 (field %s)", s_metaStringFlags[i], s_metaStringNames[i]);
			*errorOut = message;
			return NULL;
		}
	}

	Rld_MemNeed(&need, in->lev, in->levSize);

	meta = Rld_BuildMeta(in->trackVersion, in->modes, &need, in->strings, &metaSize);
	if ((meta == NULL) || (metaSize > RLD_LIMIT_META))
	{
		free(meta);
		*errorOut = "META does not fit its 64 KB limit";
		return NULL;
	}

	// Order is directory order and thus offset order:
	// META, LEVD, VRMD, then the optional SNDB and PARM, if they exist.
	// The reader demands no order; this is custom, not a rule.
	Rld_AddChunk(&container, "META", meta, metaSize, RLD_COMPRESSION_NONE);
	Rld_AddChunk(&container, "LEVD", in->lev, in->levSize, RLD_COMPRESSION_NONE);
	Rld_AddChunk(&container, "VRMD", in->vrm, in->vrmSize, RLD_COMPRESSION_NONE);

	if (in->sndb != NULL)
	{
		Rld_AddChunk(&container, "SNDB", in->sndb, in->sndbSize, RLD_COMPRESSION_NONE);
	}

	if (in->parm != NULL)
	{
		Rld_AddChunk(&container, "PARM", in->parm, in->parmSize, RLD_COMPRESSION_NONE);
	}

	// Header, chunks and directory: the envelope every container type shares
	// (rldtrack.inc). Required features: this packer sets none
	// (RLD_HEADER_FLAGS_KNOWN).
	out = Rld_WriteEnvelope(&s_rldTrackFormat, 0, &container, sizeOut);

	for (i = 0; i < container.chunkCount; i++)
	{
		free(container.chunks[i].stored);
	}
	free(meta);

	if (out == NULL)
	{
		*errorOut = "out of memory";
		return NULL;
	}

	return out;
}

//========================================================================================
// COMMANDS
//========================================================================================

// The help is further down, but `build` already needs it: whoever forgets the
// required arguments gets the whole help and not one line.
static void Rld_Usage(void);

// Characters (.rldchar): make-char and its info, verify, self-test and help are in
// tools/rldpack_char.inc, included near the end of this file, after every helper
// it uses. info, verify, selftest and the help call into it from here on.
static int RldChar_MakeCommand(int argc, char **argv);
static int RldChar_InfoCommand(const char *path, int machine);
static int RldChar_VerifyCommand(const char *path);
static int RldChar_SelfTest(void);
static int RldChar_NativeTestsCommand(int argc, char **argv);
static void RldChar_Usage(FILE *out);

// Previews for Reload Studio that never write a container: char-poses
// (tools/rldpack_anim.inc) and char-wheel (tools/rldpack_wheel.inc), both
// included next to tools/rldpack_char.inc.
static int RldAnim_PosesCommand(int argc, char **argv);
static int RldAnim_SelfTest(void);
static void RldAnim_Usage(FILE *out);
static int RldWheel_Command(int argc, char **argv);
static int RldWheel_SelfTest(void);
static void RldWheel_Usage(FILE *out);


//========================================================================================
// CHECK WHAT IS INSIDE - BEFORE IT IS WRITTEN
//
// Noticed with the first real track data: --lev and --vrm
// built SWAPPED gave a flawless container. `verify`
// waved it through, all hashes matched. Only the game would have
// stumbled over it, and there it looks like a broken container.
//
// That is the mistake an author makes at two in the morning, and the whole
// promise of this format is that `verify` is worth something. A packer that
// accepts whatever it is given turns a typo into a finished
// certificate.
//
// The rule for VRM is NOT invented - it is in LOAD_File.c, in
// LOAD_VramFileCallback, and is exactly what the game does with the file:
// if it starts with 0x20, it is a chain of TIM blocks, otherwise it is a
// single TIM. A block is a u32 size, then a VramHeader (0xC bytes, then
// RECT x/y/w/h as s16), then w*h*2 bytes of pixels.
//
// Recomputed on warppad1.vrm: first block x=512 y=0 w=384 h=256,
// block size 196628 = 20 + 384*256*2. It fits to the byte.

#define RLD_VRAM_WIDTH   1024
#define RLD_VRAM_HEIGHT  512
#define RLD_VRAM_HEADER  0x14
#define RLD_VRAM_PACKED  0x20

static u32 Rld_ReadLE32At(const u8 *data, size_t offset)
{
	return (u32)data[offset] | ((u32)data[offset + 1u] << 8) | ((u32)data[offset + 2u] << 16) | ((u32)data[offset + 3u] << 24);
}

static int Rld_ReadLE16At(const u8 *data, size_t offset)
{
	return (int)((u32)data[offset] | ((u32)data[offset + 1u] << 8));
}

// A single TIM block at `offset`: does its rectangle fit into VRAM, and do
// header and pixels still fit into the file? Returns the block size, 0 if
// it is none.
static size_t Rld_VramBlockSize(const u8 *data, size_t size, size_t offset)
{
	int x, y, w, h;
	size_t pixels;

	if ((offset + RLD_VRAM_HEADER) > size)
	{
		return 0;
	}

	x = Rld_ReadLE16At(data, offset + 0x0C);
	y = Rld_ReadLE16At(data, offset + 0x0E);
	w = Rld_ReadLE16At(data, offset + 0x10);
	h = Rld_ReadLE16At(data, offset + 0x12);

	if ((w <= 0) || (h <= 0) || (x < 0) || (y < 0))
	{
		return 0;
	}

	if (((x + w) > RLD_VRAM_WIDTH) || ((y + h) > RLD_VRAM_HEIGHT))
	{
		return 0;
	}

	pixels = (size_t)w * (size_t)h * 2u;

	if ((offset + RLD_VRAM_HEADER + pixels) > size)
	{
		return 0;
	}

	return RLD_VRAM_HEADER + pixels;
}

// Does this file look like a VRM? Exactly by the game's rule.
static int Rld_LooksLikeVram(const u8 *data, size_t size)
{
	size_t offset;

	if (size < (RLD_VRAM_HEADER + 4u))
	{
		return 0;
	}

	if (Rld_ReadLE32At(data, 0) != RLD_VRAM_PACKED)
	{
		// Single TIM: the header sits at 0, and the file must be exactly as
		// large as header plus pixels. "Exactly" is intentional - otherwise
		// any file with suitable-looking bytes would pass.
		return (Rld_VramBlockSize(data, size, 0) == size);
	}

	// Chain. The size of the first block is at offset 4, the block itself
	// follows directly. The chain ends with a size of 0.
	offset = 4;
	for (;;)
	{
		u32 declared;
		size_t actual;

		if ((offset + 4u) > size)
		{
			return 0;
		}

		declared = Rld_ReadLE32At(data, offset);
		if (declared == 0)
		{
			return 1; // ran cleanly to the end
		}

		actual = Rld_VramBlockSize(data, size, offset + 4u);
		if (actual == 0)
		{
			return 0;
		}

		if ((size_t)(declared & ~3u) != actual)
		{
			return 0;
		}

		offset += 4u + actual;
	}
}

// The tables every disc track has in its LEV.
//
// MEASURED against the NTSC-U image: all 18 arcade tracks and
// all 5 battle arenas have every one of these nine fields set. Not one of
// the 23 has even one of them at zero.
//
// Early exported test tracks had NONE of them.
//
// This is a WARNING and not an abort, and that matters more than it looks:
// six of the nine fields are read NOWHERE in the game, the other three
// are guarded against zero. So what is measured is "no disc track does
// that" - not "that does not work". Whoever turns it into a block holds up a
// track that might run.
//
// That makes the list of what a packer cannot check shorter: "whether the track is
// drivable" a packer cannot know, but "whether it brings what every other one brings"
// it very well can.

struct RldLevField
{
	u32 offset;
	const char *name;
};

static const struct RldLevField s_levFields[] = {
    {0x1c, "unk3"},         {0x20, "unk4"},          {0x28, "visOVertSrc"},
    {0x3c, "levTexLookup"}, {0x40, "namedTexArray"}, {0xcc, "unk_Lev_CC"},
    {0xd0, "unk_Lev_D0"},   {0xd4, "lowTexArray"},   {0x170, "visSCVertSrc"},
};

static int Rld_WarnAboutLevTables(const u8 *lev, size_t size)
{
    int missing = 0;
    char names[192];
    size_t at = 0;
    int i;

    names[0] = '\0';

    // 4-byte pointer to the pointer map, then the level header.
    if (size < (4u + 0x200u))
    {
        return 0;
    }

    for (i = 0; i < RLD_ARRAY_COUNT(s_levFields); i++)
    {
        if (Rld_ReadLE32(&lev[4 + s_levFields[i].offset]) != 0)
        {
            continue;
        }

        if (missing == 0)
        {
            printf("\nWARNING: this LEV has no ");
        }
        else
        {
            printf(", ");
        }

        printf("%s", s_levFields[i].name);
        if ((at + strlen(s_levFields[i].name) + 3u) < sizeof(names))
        {
            at += (size_t)snprintf(&names[at], sizeof(names) - at, "%s%s", (at != 0u) ? ", " : "", s_levFields[i].name);
        }
        missing++;
    }

    if (missing != 0)
    {
        Rld_Say("warning", "lev-tables", names, "The track leaves %d level table(s) empty that every disc track fills. The container still loads.", missing);
        printf(".\n");
        printf("  All 18 arcade tracks and all 5 arenas on the disc fill every one of these.\n");
        printf("  Six of them are read nowhere in the game and three are guarded against\n");
        printf("  null, so this is a difference and not a verdict. The container is built\n");
        printf("  either way, and the game will load it.\n");
    }

    return missing;
}

// THE VISIBILITY LISTS - what the author learns nowhere else.
//
// Every quad block carries at 0x44 a pointer to struct PVS: visLeafSrc at
// 0x00, visFaceSrc at 0x04. Both are bit masks. RenderLists_IsVisible asks
// the first BEFORE a single box is held against the view frustum;
// Ovr226_800a0f34_ConsumeFullDynamicVisibilityBit asks the second, for every
// single quad block in a visible leaf.
//
// MEASURED over all 25 disc tracks and 21 exported test tracks:
//
//   quad block mask, share visible     disc 40 to 55 %     test data 93 to 100 %
//   different masks per track          disc 136 to 738     test data 1
//
// On the disc the game knows at every point of the track that 45 to 60
// percent of the level cannot be seen from there, and skips it
// for free. A single mask shared by all quad blocks, in which everything is
// set to visible, costs 24 to 316 bytes and discards nothing.
//
// This is a WARNING and not a verdict. The container is built,
// and the track runs - it only runs more expensively, and nobody else
// tells you that.
static int Rld_WarnAboutVisibility(const u8 *lev, size_t size)
{
	const size_t body = 4;
	u32 mesh;
	u32 blocks;
	u32 quadArray;
	u32 faceWords;
	u32 withPvs = 0;
	u32 withFaceList = 0;
	u32 distinctFace = 0;
	u32 distinctLeaf = 0;
	u32 setLo = 101;
	u32 setHi = 0;
	u32 *seenFace = NULL;
	u32 *seenLeaf = NULL;
	u32 i;

	if (size < (body + 0x200u))
	{
		return 0;
	}

	mesh = Rld_ReadLE32(&lev[body + 0x00]);
	if ((mesh == 0u) || ((body + mesh + 0x20u) > size))
	{
		return 0;
	}

	blocks = Rld_ReadLE32(&lev[body + mesh + 0x00]);
	quadArray = Rld_ReadLE32(&lev[body + mesh + 0x0cu]);

	if ((blocks == 0u) || (blocks > (size / RLD_QUADBLOCK_BYTES)) || ((body + quadArray + blocks * RLD_QUADBLOCK_BYTES) > size))
	{
		return 0;
	}

	faceWords = (blocks + 31u) / 32u;

	seenFace = (u32 *)calloc(blocks, sizeof(u32));
	seenLeaf = (u32 *)calloc(blocks, sizeof(u32));

	if ((seenFace == NULL) || (seenLeaf == NULL))
	{
		free(seenFace);
		free(seenLeaf);
		return 0;
	}

	for (i = 0; i < blocks; i++)
	{
		const u32 pvs = Rld_ReadLE32(&lev[body + quadArray + (size_t)i * RLD_QUADBLOCK_BYTES + 0x44u]);
		u32 faceSrc;
		u32 leafSrc;
		u32 d;
		int known;

		if ((pvs == 0u) || ((body + pvs + 16u) > size))
		{
			continue;
		}

		withPvs++;
		leafSrc = Rld_ReadLE32(&lev[body + pvs + 0x00u]);
		faceSrc = Rld_ReadLE32(&lev[body + pvs + 0x04u]);

		if ((leafSrc != 0u) && ((body + leafSrc + 4u) <= size))
		{
			known = 0;
			for (d = 0; d < distinctLeaf; d++)
			{
				if (seenLeaf[d] == leafSrc) { known = 1; break; }
			}
			if (!known) { seenLeaf[distinctLeaf++] = leafSrc; }
		}

		if ((faceSrc == 0u) || ((body + faceSrc + faceWords * 4u) > size))
		{
			continue;
		}

		withFaceList++;
		known = 0;
		for (d = 0; d < distinctFace; d++)
		{
			if (seenFace[d] == faceSrc) { known = 1; break; }
		}

		if (known)
		{
			continue;
		}

		seenFace[distinctFace++] = faceSrc;

		{
			u32 bits = 0;
			u32 w;
			u32 percent;

			for (w = 0; w < faceWords; w++)
			{
				u32 word = Rld_ReadLE32(&lev[body + faceSrc + w * 4u]);

				while (word != 0u)
				{
					bits += (word & 1u);
					word >>= 1;
				}
			}

			percent = (bits * 100u) / blocks;
			if (percent < setLo) { setLo = percent; }
			if (percent > setHi) { setHi = percent; }
		}
	}

	free(seenFace);
	free(seenLeaf);

	if (withFaceList == 0u)
	{
		printf("\nWARNING: not one quad block in this LEV carries a per-quad visibility list.\n");
		Rld_Say("warning", "vis-none", NULL, "The track has no visibility lists, so the game draws the whole level in every frame. It may run slower than the disc tracks.");
		printf("  All 25 tracks on the disc carry one. Without it nothing is skipped before\n");
		printf("  the frustum test, and the frame costs what the whole level costs.\n");
		return 1;
	}

	// Silence when the lists work. The tree reports here only what is
	// different from the disc - a line per track that always comes
	// is read by nobody after the third build.
	if ((distinctFace > 1u) && (setHi < 90u))
	{
		return 0;
	}

	printf("\nWARNING: the per-quad visibility lists in this LEV cull next to nothing.\n");
	Rld_Say("warning", "vis-full", NULL, "The visibility lists of this track hide almost nothing (%u list(s), %u to %u %% of the level visible). The game draws nearly the whole level in every frame.",
			distinctFace, setLo, setHi);
	printf("  %u list(s) over %u quad blocks, %u to %u %% of the level marked visible.\n", distinctFace, blocks, setLo, setHi);

	if (distinctFace == 1u)
	{
		printf("  Every quad block points at the SAME list, so the answer is the same from\n");
		printf("  everywhere on the track.\n");
	}

	printf("  On the disc 40 to 55 %% of a level is marked visible from any one point, and\n");
	printf("  each track carries 136 to 738 different lists. RenderLists_IsVisible asks\n");
	printf("  yours before a single box is tested, and a full list answers yes every time.\n");
	printf("  The container is built either way, and the game will load it.\n");
	return 1;
}

// The sky, held against the machine's budget.
//
// The drawing memory holds 97,280 bytes, a sky triangle costs 28 of them.
// Per frame FOUR of the eight segments are drawn - the ones facing the view direction.
//
// MEASURED against the NTSC-U image: of the 14 tracks with a sky, the
// largest (Tiger Temple) needs 407 faces for its four segments, i.e. 11,396
// bytes or 11.7 %. The game gives the sky a quarter; whatever lies above that
// is dropped from the picture.
//
// AND THE EIGHT SEGMENTS. On the disc they are eight different pieces of a
// dome. If they are all the same, the same sky is drawn four times on top of each other
// - four times the cost for the same picture. That is exactly what early
// exported test tracks had.
//
// This too is a WARNING. The container is built.

#define RLD_SKY_SEGMENTS   8
#define RLD_SKY_FACE_BYTES 8
#define RLD_PRIM_BYTES     97280u
#define RLD_POLY_G3_BYTES  28u

static int Rld_WarnAboutSky(const u8 *lev, size_t size)
{
	size_t body = 4;
	u32 skyOffset;
	u32 numVertex;
	u32 counts[RLD_SKY_SEGMENTS];
	u32 offsets[RLD_SKY_SEGMENTS];
	u32 total = 0;
	u32 drawn;
	int identical = 1;
	int i;

	if (size < (body + 0x200u))
	{
		return 0;
	}

	skyOffset = Rld_ReadLE32(&lev[body + 0x04]);
	if ((skyOffset == 0) || ((body + skyOffset + 56u) > size))
	{
		return 0;
	}

	numVertex = Rld_ReadLE32(&lev[body + skyOffset]);

	for (i = 0; i < RLD_SKY_SEGMENTS; i++)
	{
		counts[i] = Rld_ReadLE16(&lev[body + skyOffset + 8u + (u32)i * 2u]);
		offsets[i] = Rld_ReadLE32(&lev[body + skyOffset + 24u + (u32)i * 4u]);

		if ((body + offsets[i] + counts[i] * RLD_SKY_FACE_BYTES) > size)
		{
			printf("\nWARNING: sky segment %d runs past the end of the LEV. Not checked further.\n", i);
			Rld_Say("warning", "sky-broken", NULL, "A part of the sky runs past the end of the track file. The sky was not checked further.");
			return 1;
		}

		total += counts[i];
	}

	for (i = 1; i < RLD_SKY_SEGMENTS; i++)
	{
		if ((counts[i] != counts[0]) ||
		    (memcmp(&lev[body + offsets[i]], &lev[body + offsets[0]], (size_t)counts[0] * RLD_SKY_FACE_BYTES) != 0))
		{
			identical = 0;
			break;
		}
	}

	// The four largest segments are the upper bound of what a frame
	// can cost.
	{
		u32 sorted[RLD_SKY_SEGMENTS];
		int a, bIdx;

		for (i = 0; i < RLD_SKY_SEGMENTS; i++)
		{
			sorted[i] = counts[i];
		}

		for (a = 0; a < RLD_SKY_SEGMENTS; a++)
		{
			for (bIdx = a + 1; bIdx < RLD_SKY_SEGMENTS; bIdx++)
			{
				if (sorted[bIdx] > sorted[a])
				{
					u32 swap = sorted[a];

					sorted[a] = sorted[bIdx];
					sorted[bIdx] = swap;
				}
			}
		}

		drawn = sorted[0] + sorted[1] + sorted[2] + sorted[3];
	}

	// The radius of the dome. It is drawn around the camera, so it decides
	// how far away it is projected. Measured against the image: the 14
	// disc tracks with a sky lie between 20,482 and 41,731.
	{
		u32 pv = Rld_ReadLE32(&lev[body + skyOffset + 4u]);
		u64 worstSq = 0;
		u32 radius = 0;
		u32 v;

		if ((body + pv + numVertex * 12u) <= size)
		{
			for (v = 0; v < numVertex; v++)
			{
				const s64 x = (s16)Rld_ReadLE16(&lev[body + pv + v * 12u + 0u]);
				const s64 y = (s16)Rld_ReadLE16(&lev[body + pv + v * 12u + 2u]);
				const s64 z = (s16)Rld_ReadLE16(&lev[body + pv + v * 12u + 4u]);
				const u64 lenSq = (u64)((x * x) + (y * y) + (z * z));

				if (lenSq > worstSq)
				{
					worstSq = lenSq;
				}
			}

			while (((u64)(radius + 1u) * (u64)(radius + 1u)) <= worstSq)
			{
				radius++;
			}

			if ((radius < 20000u) || (radius > 45000u))
			{
				printf("\nWARNING: the sky dome has radius %u. Every track on the disc sits between\n", radius);
				Rld_Say("warning", "sky-radius", NULL, "The sky dome has radius %u; the disc tracks use 20482 to 41731. The sky may show as large flat triangles.", radius);
				printf("  20482 and 41731. The dome is drawn around the camera, so one this far off\n");
				printf("  projects into large flat triangles across the picture.\n");
			}
		}
	}

	if (identical)
	{
		printf("\nWARNING: all eight sky segments hold the same data.\n");
		Rld_Say("warning", "sky-same", NULL, "All eight parts of the sky are the same, so the game draws one sky four times over.");
		printf("  On the disc they are eight different pieces of one dome, and the game draws\n");
		printf("  the four facing the camera. Yours draws one sky four times over.\n");
	}

	if ((drawn * RLD_POLY_G3_BYTES) > (RLD_PRIM_BYTES / 4u))
	{
		if (!identical)
		{
			printf("\n");
		}

		printf("WARNING: the sky needs %u bytes of draw memory (%u faces in the four drawn\n", drawn * RLD_POLY_G3_BYTES, drawn);
		Rld_Say("warning", "sky-memory", NULL, "The sky needs %u bytes of draw memory and the game gives it %u. Part of the sky will be missing.",
				drawn * RLD_POLY_G3_BYTES, RLD_PRIM_BYTES / 4u);
		printf("  segments). The game gives the sky %u of its %u bytes, so part of your sky\n", RLD_PRIM_BYTES / 4u, RLD_PRIM_BYTES);
		printf("  will be missing. The biggest sky on the disc needs 11396 bytes.\n");
		return 1;
	}

	if (identical)
	{
		return 1;
	}

	(void)numVertex;
	(void)total;
	return 0;
}

// For LEV there is no header record with an identifier - the file starts with a
// table of pointers. So this does NOT claim "this is a LEV",
// it only excludes what is certainly not one: a VRM, something tiny,
// and a first pointer that points out of the file. That is enough for the case
// at hand, and it rejects no real track.
static const char *Rld_WhyNotLev(const u8 *data, size_t size)
{
	u32 first;

	if (size < 64u)
	{
		return "the file is too small to be a track";
	}

	if (Rld_LooksLikeVram(data, size))
	{
		return "this is a VRM - are --lev and --vrm the wrong way round?";
	}

	first = Rld_ReadLE32At(data, 0);

	if ((first & 3u) != 0u)
	{
		return "the first offset is not a multiple of four - this is not a LEV";
	}

	if ((size_t)first >= size)
	{
		return "the first offset points outside the file - this is not a LEV";
	}

	return NULL;
}

static const char *Rld_WhyNotVram(const u8 *data, size_t size)
{
	if (size < (RLD_VRAM_HEADER + 4u))
	{
		return "the file is too small to be a VRM";
	}

	if (!Rld_LooksLikeVram(data, size))
	{
		return "this does not look like a VRM - are --lev and --vrm the wrong way round?";
	}

	return NULL;
}

// NO PREVIEW IMAGE - THMB HAS BEEN TAKEN OUT OF THE FORMAT
//
// It was specified as PNG, because PNG is already compressed. That is an
// argument from the packer side; the costs arise on the game side, and
// there is neither a PNG reader nor zlib nor anything else that can
// inflate. A menu image would have cost an inflate implementation plus a PNG reader
// including the five line filters.
//
// A different image format would be a commitment authors get used to
// before we know whether we want it at all. So none at all.
//
// The FourCC "THMB" stays reserved. Unknown chunks are skipped like any
// unknown type, that costs nothing, and if we do want it later
// after all, the name is still free.

//========================================================================================
// READING THE LEV WITHOUT INTERPRETING IT (for make)
//========================================================================================
//
// Word 0 of the LEV is ptrMapOffset, the body starts at 4 (LOAD_File.c:123).
// The pointer map says which words of the body are pointers: LOAD_RunPtrMap
// relocates ONLY the listed places. A field that is not
// in the map is not a pointer - following it would be guessing.

#define RLD_LEV_BODY          4u
#define RLD_LEV_NUM_INSTANCES 0x0cu  // Level.numInstances

// Instance limit in a race: 128 pool - 10 drivers/HUD = 118 hard, of which 8
// are kept free for the wake (110); from 90 on fewer than 28 remain for the race.
#define RLD_INSTANCES_MAX  110u
#define RLD_INSTANCES_WARN 90u
#define RLD_LEV_PTR_INSTDEFS  0x10u  // Level.ptrInstDefs
#define RLD_LEV_PTR_SPAWN1    0x134u // Level.ptrSpawnType1, namespace_Level.h:818
#define RLD_INSTDEF_BYTES     0x40u
#define RLD_INSTDEF_MODEL     0x10u // InstDef.model
#define RLD_INSTDEF_MODELID   0x3cu // InstDef.modelID - read by collision
#define RLD_MODEL_ID          0x10u // Model.id, s16 - read by spawning

#define RLD_MODEL_CRYSTAL 0x60 // STATIC_CRYSTAL, namespace_Instance.h:169
#define RLD_MODEL_C       0x93 // STATIC_C, :220 - T and R follow
#define RLD_MODEL_R       0x95

struct RldPtrMap
{
	const u8 *slots;
	u32 count;
};

static int Rld_PtrMapRead(const u8 *lev, size_t size, struct RldPtrMap *map)
{
	int mapOffset;
	int numBytes;
	size_t at;

	map->slots = NULL;
	map->count = 0;

	if (size < (RLD_LEV_BODY + 4u))
	{
		return 0;
	}

	mapOffset = (int)Rld_ReadLE32At(lev, 0);
	if ((mapOffset < 0) || (((size_t)mapOffset + RLD_LEV_BODY + 4u) > size))
	{
		return 0;
	}

	at = RLD_LEV_BODY + (size_t)mapOffset;
	numBytes = (int)Rld_ReadLE32At(lev, at);
	if ((numBytes < 0) || ((numBytes & 3) != 0) || ((at + 4u + (size_t)numBytes) > size))
	{
		return 0;
	}

	map->slots = &lev[at + 4u];
	map->count = (u32)numBytes / 4u;
	return 1;
}

// Every entry is rounded down to a word as in the game (LOAD_Assets.c:98).
static int Rld_PtrMapHas(const struct RldPtrMap *map, u32 bodyOffset)
{
	u32 i;

	for (i = 0; i < map->count; i++)
	{
		if ((Rld_ReadLE32At(map->slots, (size_t)i * 4u) & ~3u) == bodyOffset)
		{
			return 1;
		}
	}

	return 0;
}

// THE MODEL ID - port of the model-ID fix of an earlier external track tool,
// step by step.
//
// The ID is stored twice in every instance. InstDef.modelID (+0x3c, int)
// is read by collision (COLL.c, VehPickupItem.c), Model.id (+0x10, s16) is read
// by spawning and counting (INSTANCE.c). On the disc both agree in all
// 897 resolvable instances of the 25 tracks. If they differ,
// two systems take the same instance for two different things: the collision of
// Baby T Park from the Saphi data saw C, T, T where C, T, R are drawn.
//
// Nothing is interpreted. InstDef.modelID gets the value of Model.id,
// sign-extended, otherwise not a byte changes. Model.id wins because the
// model pointer names the model that really is in the file.
#define RLD_MODELID_SHOWN 16

struct RldModelIdFix
{
	u32 index;
	int before;
	int after;
};

// THE ID OF THE LETTERS. The HUD shows the letters by Model.id; the test
// track "CTR Test" carried 0x95 on model "t" and
// 0x94 on "r", the HUD read C R T. The game is right (retail), the data
// is not. Only the three names and only IDs 0x93..0x95 - nothing else.
struct RldLetterFix
{
	u32 model;  // model, body-relative
	char name;  // as written in the file
	int before;
	int after;
};

struct RldModelIds
{
	// Set means: cannot be checked at all, nothing changed.
	const char *problem;

	u32 instances; // numInstances from the header
	u32 checked;   // of those, with a resolvable model pointer
	u32 differ;    // of those, with a differing ID
	struct RldModelIdFix fixes[RLD_MODELID_SHOWN];

	// Counted by Model.id, for the modes ctr and crystal -
	// after the letter correction, if it is applied.
	u32 letters[3];
	u32 crystals;

	// Letter models whose name ("c", "t", "r") says a different ID
	// than their Model.id 0x93..0x95 - once per model, not per instance.
	u32 letterFixes;
	struct RldLetterFix letterFix[RLD_MODELID_SHOWN];
};

// apply 0 only counts what differs (--keep-model-ids).
static void Rld_ModelIds(u8 *lev, size_t size, int apply, struct RldModelIds *out)
{
	struct RldPtrMap map;
	u32 ptrInstDefs;
	u32 i;

	memset(out, 0, sizeof(*out));

	if (!Rld_PtrMapRead(lev, size, &map))
	{
		out->problem = "the pointer map is unreadable";
		return;
	}

	if ((RLD_LEV_BODY + RLD_LEV_PTR_INSTDEFS + 4u) > size)
	{
		out->problem = "the level header does not reach the instance pointer";
		return;
	}

	out->instances = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_NUM_INSTANCES);

	// No instances is not an error. There is nothing to align.
	if (out->instances == 0u)
	{
		return;
	}

	if (!Rld_PtrMapHas(&map, RLD_LEV_PTR_INSTDEFS))
	{
		out->problem = "ptrInstDefs is not in the pointer map although the header names instances";
		return;
	}

	ptrInstDefs = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_INSTDEFS);

	if ((ptrInstDefs == 0u) || (((u64)RLD_LEV_BODY + ptrInstDefs + ((u64)out->instances * RLD_INSTDEF_BYTES)) > size))
	{
		out->problem = "the instance table does not fit the file";
		return;
	}

	for (i = 0; i < out->instances; i++)
	{
		const size_t record = RLD_LEV_BODY + (size_t)ptrInstDefs + ((size_t)i * RLD_INSTDEF_BYTES);
		u32 modelPtr;
		int modelId;
		int declared;

		// Without a model pointer in the map there is nothing to align to.
		if (!Rld_PtrMapHas(&map, (u32)(record + RLD_INSTDEF_MODEL - RLD_LEV_BODY)))
		{
			continue;
		}

		modelPtr = Rld_ReadLE32At(lev, record + RLD_INSTDEF_MODEL);

		if ((modelPtr == 0u) || (((u64)RLD_LEV_BODY + modelPtr + RLD_MODEL_ID + 2u) > size))
		{
			continue;
		}

		modelId = (int)(s16)Rld_ReadLE16At(lev, RLD_LEV_BODY + (size_t)modelPtr + RLD_MODEL_ID);
		declared = (int)Rld_ReadLE32At(lev, record + RLD_INSTDEF_MODELID);
		out->checked++;

		// First the letters by their name, then InstDef.modelID by the
		// corrected Model.id. A model shared by several instances is
		// corrected once; with apply 0 reported once.
		if ((modelId >= RLD_MODEL_C) && (modelId <= RLD_MODEL_R))
		{
			const u8 *name = &lev[RLD_LEV_BODY + (size_t)modelPtr];
			const int letter = ((name[0] | 0x20) == 'c') ? 0 : (((name[0] | 0x20) == 't') ? 1 : (((name[0] | 0x20) == 'r') ? 2 : -1));

			if ((letter >= 0) && (name[1] == 0u) && (modelId != (RLD_MODEL_C + letter)))
			{
				u32 k;

				for (k = 0; (k < out->letterFixes) && (k < RLD_MODELID_SHOWN); k++)
				{
					if (out->letterFix[k].model == modelPtr)
					{
						break;
					}
				}

				if ((k >= out->letterFixes) || (k >= RLD_MODELID_SHOWN))
				{
					if (out->letterFixes < RLD_MODELID_SHOWN)
					{
						out->letterFix[out->letterFixes].model = modelPtr;
						out->letterFix[out->letterFixes].name = (char)name[0];
						out->letterFix[out->letterFixes].before = modelId;
						out->letterFix[out->letterFixes].after = RLD_MODEL_C + letter;
					}
					out->letterFixes++;
				}

				if (apply)
				{
					Rld_WriteLE16(&lev[RLD_LEV_BODY + (size_t)modelPtr + RLD_MODEL_ID], (u32)(RLD_MODEL_C + letter));
					modelId = RLD_MODEL_C + letter;
				}
			}
		}

		if ((modelId >= RLD_MODEL_C) && (modelId <= RLD_MODEL_R))
		{
			out->letters[modelId - RLD_MODEL_C]++;
		}

		if (modelId == RLD_MODEL_CRYSTAL)
		{
			out->crystals++;
		}

		if (declared == modelId)
		{
			continue;
		}

		if (apply)
		{
			Rld_WriteLE32(&lev[record + RLD_INSTDEF_MODELID], (u32)modelId);
		}

		if (out->differ < RLD_MODELID_SHOWN)
		{
			out->fixes[out->differ].index = i;
			out->fixes[out->differ].before = declared;
			out->fixes[out->differ].after = modelId;
		}

		out->differ++;
	}
}

// A model ID as a human reads it: 0xa6, and a negative one as -1.
static void Rld_PrintModelId(int id)
{
	if (id < 0)
	{
		printf("%d", id);
	}
	else
	{
		printf("0x%02x", (unsigned int)id);
	}
}

// The letter correction for humans (NOTE) and for Reload Studio (msg).
// applied 0: found, but left alone (--keep-model-ids).
static void Rld_LetterReport(const struct RldModelIds *ids, int applied)
{
	const u32 shown = (ids->letterFixes < RLD_MODELID_SHOWN) ? ids->letterFixes : RLD_MODELID_SHOWN;
	char hud[6] = {'C', ' ', 'T', ' ', 'R', '\0'};
	static const char *const s_letterNames[8] = {"", "model C", "model T", "models C and T", "model R", "models C and R", "models T and R", "models C, T and R"};
	const char *names;
	u32 mask = 0;
	char technical[256];
	size_t used = 0;
	u32 k;

	if ((ids->problem != NULL) || (ids->letterFixes == 0u))
	{
		return;
	}

	// What the HUD shows without correction: at slot 0x93..0x95 the model that
	// carried this ID.
	for (k = 0; k < shown; k++)
	{
		const char upper = (char)(ids->letterFix[k].name & ~0x20);

		hud[(ids->letterFix[k].before - RLD_MODEL_C) * 2] = upper;
	}

	printf("\n");
	technical[0] = '\0';
	for (k = 0; k < shown; k++)
	{
		const struct RldLetterFix *fix = &ids->letterFix[k];

		if (applied)
		{
			printf("NOTE: model '%c' carried id 0x%02x (%c) - set to 0x%02x (%c): the HUD would show %s\n", fix->name, (unsigned int)fix->before,
			       "CTR"[fix->before - RLD_MODEL_C], (unsigned int)fix->after, "CTR"[fix->after - RLD_MODEL_C], hud);
		}
		else
		{
			printf("NOTE: model '%c' carries id 0x%02x (%c), its name says 0x%02x (%c) - left alone (--keep-model-ids): the HUD shows %s\n", fix->name,
			       (unsigned int)fix->before, "CTR"[fix->before - RLD_MODEL_C], (unsigned int)fix->after, "CTR"[fix->after - RLD_MODEL_C], hud);
		}

		if (used < sizeof(technical))
		{
			const int n = snprintf(&technical[used], sizeof(technical) - used, "%smodel '%c' 0x%02x -> 0x%02x", (k != 0u) ? "; " : "", fix->name,
			                       (unsigned int)fix->before, (unsigned int)fix->after);

			used += (n > 0) ? (size_t)n : 0u;
		}
	}

	// The letters in the order C, T, R, for the sentence.
	for (k = 0; k < shown; k++)
	{
		mask |= 1u << (ids->letterFix[k].after - RLD_MODEL_C);
	}
	names = s_letterNames[mask];

	// Two that have swapped their IDs - the case from "CTR Test".
	if ((shown == 2u) && (ids->letterFixes == 2u) && (ids->letterFix[0].before == ids->letterFix[1].after) &&
	    (ids->letterFix[1].before == ids->letterFix[0].after))
	{
		if (applied)
		{
			Rld_Say("info", "letter-ids", technical, "The letter %s carried each other's ids; the packer swapped them back so the HUD shows C T R.", names);
		}
		else
		{
			Rld_Say("warning", "letter-ids", technical, "The letter %s carry each other's ids, so the HUD shows %s. The ids were left alone (--keep-model-ids).", names,
			        hud);
		}
	}
	else if (applied)
	{
		Rld_Say("info", "letter-ids", technical, "The ids of the letter %s belonged to other letters; the packer set the ids by the names so the HUD shows C T R.",
		        names);
	}
	else
	{
		Rld_Say("warning", "letter-ids", technical, "The ids of the letter %s belong to other letters, so the HUD shows %s. The ids were left alone (--keep-model-ids).",
		        names, hud);
	}
}

// THE SPAWN TABLE (SpawnType1 at Level+0x134).
//
// count, then count pointers: MAP 0, SPAWN 1, CAMERA_EOR 2, CAMERA_PATH 3,
// NTROPY 4, NOXIDE 5, CREDITS 6 (namespace_Level.h:584-601). Two places
// read from it without a check, and at both the game crashes:
//
//   GhostReplay.c:356-360  time trial takes pointer 4 or 5 as the ghost
//                          without looking at count;
//   CAM_FollowDriver_Normal the fly-in takes pointer 3 as the camera path
//                          at count >= 4 - in an older container of Baby T
//                          Park count 7 with NULL at slot 3, crash at
//                          NULL+0x354.
//
// Both are in the track data, so it is detected BEFORE packing and
// aborts. Since 4.1 also slot 2: the camera at the
// race end reads it at count >= 3 without a check (CAM_FollowDriver_Normal, end-of-race camera). Slot 1
// is not checked: in the retail image it is often 0, and that is
// correct there. Since 4.1 build checks the same as make.
#define RLD_SPAWN_SLOTS 16

struct RldSpawn
{
	// ptrSpawnType1 is set and the table lies in the file.
	int present;
	int count;
	int nonNull[RLD_SPAWN_SLOTS];
	int inMap[RLD_SPAWN_SLOTS];

	// Set means: the pointer is there, but the table cannot be read.
	const char *problem;
};

static void Rld_SpawnRead(const u8 *lev, size_t size, struct RldSpawn *out)
{
	struct RldPtrMap map;
	const int haveMap = Rld_PtrMapRead(lev, size, &map);
	u32 ptr;
	int k;

	memset(out, 0, sizeof(*out));

	if ((RLD_LEV_BODY + RLD_LEV_PTR_SPAWN1 + 4u) > size)
	{
		out->problem = "the level header does not reach the spawn table pointer";
		return;
	}

	ptr = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_SPAWN1);

	if (ptr == 0u)
	{
		return;
	}

	if (((u64)RLD_LEV_BODY + ptr + 4u) > size)
	{
		out->problem = "the spawn table pointer points outside the file";
		return;
	}

	out->count = (int)Rld_ReadLE32At(lev, RLD_LEV_BODY + (size_t)ptr);

	if ((out->count < 0) || (out->count > RLD_SPAWN_SLOTS) || (((u64)RLD_LEV_BODY + ptr + 4u + (4u * (u64)out->count)) > size))
	{
		out->problem = "the spawn table count is out of range";
		return;
	}

	out->present = 1;

	for (k = 0; k < out->count; k++)
	{
		const u32 slot = ptr + 4u + (4u * (u32)k);

		out->nonNull[k] = (Rld_ReadLE32At(lev, RLD_LEV_BODY + (size_t)slot) != 0u);
		out->inMap[k] = haveMap && Rld_PtrMapHas(&map, slot);
	}
}

// NULL, or the reason for the abort in why.
static const char *Rld_SpawnCheck(const struct RldSpawn *spawn, u32 modes, char *why, size_t whySize)
{
	if (spawn->problem != NULL)
	{
		snprintf(why, whySize, "the spawn table cannot be read (%s) - the game reads it at the start line", spawn->problem);
		return why;
	}

	if (spawn->present && (spawn->count >= 3) && !spawn->nonNull[2])
	{
		snprintf(why, whySize,
		         "this LEV's spawn table has count %d but no end-of-race camera at slot 2 - the race end would crash (CAM_FollowDriver_Normal, end-of-race camera).\n"
		         "         This is in the track data; the exporter must write the camera or a count below 3.",
		         spawn->count);
		return why;
	}

	if (spawn->present && (spawn->count >= 4) && !spawn->nonNull[3])
	{
		snprintf(why, whySize,
		         "this LEV's spawn table has count %d but no camera path at slot 3 - the start line fly-in would crash (CAM_FollowDriver_Normal, start-line fly-in).\n"
		         "         This is in the track data; the exporter must write the camera path or a count below 4.",
		         spawn->count);
		return why;
	}

	if ((modes & RLD_MODE_TIME_TRIAL) != 0u)
	{
		if (!spawn->present || (spawn->count < 6))
		{
			snprintf(why, whySize,
			         "--modes time: this LEV carries no N. Tropy / N. Oxide ghosts (spawn table count %d, time trial needs 6) -\n"
			         "         the game would crash in GhostReplay.c. Leave time out of --modes.",
			         spawn->present ? spawn->count : 0);
			return why;
		}

		if (!spawn->nonNull[4] || !spawn->nonNull[5] || !spawn->inMap[4] || !spawn->inMap[5])
		{
			snprintf(why, whySize,
			         "--modes time: the spawn table has count %d, but slots 4 and 5 hold no N. Tropy / N. Oxide ghost -\n"
			         "         the game would crash in GhostReplay.c. Leave time out of --modes.",
			         spawn->count);
			return why;
		}
	}

	return NULL;
}

//========================================================================================
// WHAT EACH MODE NEEDS IN THE TRACK DATA (4.1)
//========================================================================================
//
// A mode may only be declared if its data is there - build and make
// check the same, and a missing piece aborts before a file is created.
// Reload Studio later shows the same texts next to a grey checkbox.
//
//   all         SpawnType1: at count >= 3 slot 2 (finish camera, CAM_FollowDriver_Normal),
//               at count >= 4 slot 3 (fly-in, CAM_FollowDriver_Normal) - Rld_SpawnCheck
//   race        at least one restart point (otherwise VehLap.c:73 counts
//               no lap, the race never ends). Nav paths: only a
//               warning, without them the bots do not drive off (BOTS.c:118-151)
//   time        the race data and the ghosts N. Tropy / N. Oxide in
//               slots 4 and 5 (GhostReplay.c:356-373)
//   ctr         the race data and the letters C, T, R (Model.id 0x93..0x95)
//   crystal     at least one crystal (Model.id 0x60)
//   battle      reserved - what an arena needs is not specified
#define RLD_LEV_NUM_SPAWN2   0x138u // Level.numSpawnType2, namespace_Level.h:830
#define RLD_LEV_PTR_SPAWN2   0x13cu // Level.ptrSpawnType2, 8 bytes per entry
#define RLD_LEV_NUM_RESTART  0x148u // Level.cnt_restart_points, :849
#define RLD_LEV_PTR_NAVTABLE 0x188u // Level.LevNavTable, :893
#define RLD_SPAWN2_BYTES     8u
#define RLD_NAV_PATHS        3
#define RLD_NAV_MAGIC        (-0x1303) // BOTS.c:135

// The modes rldpack checks when building AND that a container may write.
#define RLD_LAP_MODES (RLD_MODE_RACE | RLD_MODE_TIME_TRIAL | RLD_MODE_CTR_CHALLENGE)

#define RLD_LEV_DRIVER_SPAWN 0x6cu // Level.DriverSpawn[8], each pos s16[3] + rot s16[3], namespace_Level.h:777-781
#define RLD_DRIVER_SPAWNS    8

struct RldLevModes
{
	u32 restartPoints;
	int navPaths;                   // paths of 3 with more than one point - only those are taken
	                                // BOTS_Driver_Init (BOTS.c:3116-3134)
	int navPoints[RLD_NAV_PATHS];   // points per path, -1 = no path in this slot
	int startSpots;                 // distinct start spots of 8, (0,0,0) not counted
	int startZero;                  // start spots at (0,0,0)

	// SpawnType2 in slots 5 and 6: the places of the ambient sound
	// (HOWL_LevelAudio.c:251-257). Points, or -1 for "no place".
	int ambientCoords[2];
};

static void Rld_LevModesRead(const u8 *lev, size_t size, struct RldLevModes *out)
{
	u32 table;
	u32 count;
	u32 ptr;
	int k;

	memset(out, 0, sizeof(*out));
	out->ambientCoords[0] = -1;
	out->ambientCoords[1] = -1;
	for (k = 0; k < RLD_NAV_PATHS; k++)
	{
		out->navPoints[k] = -1;
	}

	if ((RLD_LEV_BODY + RLD_LEV_PTR_NAVTABLE + 4u) > size)
	{
		return;
	}

	out->restartPoints = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_NUM_RESTART);

	// The start spots: count distinct places. If two drivers sit on
	// the same point, they start inside each other.
	{
		int j;

		for (k = 0; k < RLD_DRIVER_SPAWNS; k++)
		{
			const size_t at = RLD_LEV_BODY + RLD_LEV_DRIVER_SPAWN + (12u * (size_t)k);
			int seen = 0;

			if (memcmp(&lev[at], "\0\0\0\0\0\0", 6) == 0)
			{
				out->startZero++;
				continue;
			}

			for (j = 0; j < k; j++)
			{
				if (memcmp(&lev[at], &lev[RLD_LEV_BODY + RLD_LEV_DRIVER_SPAWN + (12u * (size_t)j)], 6) == 0)
				{
					seen = 1;
					break;
				}
			}

			out->startSpots += seen ? 0 : 1;
		}
	}

	table = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_NAVTABLE);
	if ((table != 0u) && (((u64)RLD_LEV_BODY + table + (4u * RLD_NAV_PATHS)) <= size))
	{
		for (k = 0; k < RLD_NAV_PATHS; k++)
		{
			const u32 header = Rld_ReadLE32At(lev, RLD_LEV_BODY + (size_t)table + (4u * (u32)k));

			if ((header != 0u) && (((u64)RLD_LEV_BODY + header + 4u) <= size) &&
			    ((int)(s16)Rld_ReadLE16At(lev, RLD_LEV_BODY + (size_t)header) == RLD_NAV_MAGIC))
			{
				out->navPoints[k] = (int)(s16)Rld_ReadLE16At(lev, RLD_LEV_BODY + (size_t)header + 2u);

				if (out->navPoints[k] > 1)
				{
					out->navPaths++;
				}
			}
		}
	}

	count = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_NUM_SPAWN2);
	ptr = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_SPAWN2);
	for (k = 0; k < 2; k++)
	{
		const u32 slot = 5u + (u32)k;

		if ((ptr != 0u) && (slot < count) && (((u64)RLD_LEV_BODY + ptr + ((u64)(slot + 1u) * RLD_SPAWN2_BYTES)) <= size))
		{
			out->ambientCoords[k] = (int)Rld_ReadLE32At(lev, RLD_LEV_BODY + (size_t)ptr + (slot * RLD_SPAWN2_BYTES));
		}
	}
}

// NULL, or the reason for the abort in why. Rld_SpawnCheck has already seen
// the spawn table.
static const char *Rld_ModeCheck(u32 modes, const struct RldLevModes *data, const struct RldModelIds *ids, char *why, size_t whySize)
{
	if ((modes & RLD_MODE_BATTLE) != 0u)
	{
		snprintf(why, whySize, "--modes battle: battle is reserved - its data requirements are not defined yet");
		return why;
	}

	if (((modes & RLD_LAP_MODES) != 0u) && (data->restartPoints == 0u))
	{
		snprintf(why, whySize,
		         "--modes %s: this LEV has no restart points (Level+0x148 is 0) - laps never count and the race\n"
		         "         never ends (VehLap.c:73). This is in the track data; the exporter must write the checkpoints.",
		         Rld_ModesText(modes & RLD_LAP_MODES));
		return why;
	}

	if ((modes & (RLD_MODE_CTR_CHALLENGE | RLD_MODE_CRYSTAL_CHALLENGE)) != 0u)
	{
		if (ids->problem != NULL)
		{
			snprintf(why, whySize, "--modes ctr/crystal: the instances cannot be checked (%s)", ids->problem);
			return why;
		}

		// Each letter exactly once: the game counts pickups and wins
		// at three (222.c) - C, C, T would give YOU WIN without R.
		if (((modes & RLD_MODE_CTR_CHALLENGE) != 0u) && ((ids->letters[0] != 1u) || (ids->letters[1] != 1u) || (ids->letters[2] != 1u)))
		{
			snprintf(why, whySize, "--modes ctr: the LEV must carry each letter exactly once (C %u, T %u, R %u by Model.id 0x93..0x95)",
			         ids->letters[0], ids->letters[1], ids->letters[2]);
			return why;
		}

		if (((modes & RLD_MODE_CRYSTAL_CHALLENGE) != 0u) && (ids->crystals == 0u))
		{
			snprintf(why, whySize, "--modes crystal: the LEV carries no crystal (STATIC_CRYSTAL, Model.id 0x60)");
			return why;
		}
	}

	return NULL;
}

// The modes in the order in which Reload Studio shows them.
static const u32 s_modeOrder[5] = {RLD_MODE_RACE, RLD_MODE_TIME_TRIAL, RLD_MODE_CTR_CHALLENGE, RLD_MODE_CRYSTAL_CHALLENGE, RLD_MODE_BATTLE};
static const char *const s_modeWords[5] = {"race", "time", "ctr", "crystal", "battle"};

// Whether the LEV carries a mode: exactly the two checks of the build, nothing
// beside them. why gets the technical reason.
static int Rld_ModeData(u32 bit, const struct RldSpawn *spawn, const struct RldLevModes *data, const struct RldModelIds *ids, char *why, size_t whySize)
{
	return (Rld_SpawnCheck(spawn, bit, why, whySize) == NULL) && (Rld_ModeCheck(bit, data, ids, why, whySize) == NULL);
}

// The reason for an author, in the same order as Rld_SpawnCheck and
// Rld_ModeCheck. WHETHER a mode works is decided by Rld_ModeData alone; here only
// the text is chosen. bit 0 asks for what blocks every mode.
static void Rld_ModeReason(u32 bit, const struct RldSpawn *spawn, const struct RldLevModes *data, const struct RldModelIds *ids, char *out, size_t size)
{
	const int ghosts = spawn->present && (spawn->count >= 6) && spawn->nonNull[4] && spawn->nonNull[5] && spawn->inMap[4] && spawn->inMap[5];

	out[0] = '\0';

	if (spawn->problem != NULL)
	{
		snprintf(out, size, "The start positions of this track cannot be read (%s), so no mode can be offered.", spawn->problem);
	}
	else if (spawn->present && (spawn->count >= 3) && !spawn->nonNull[2])
	{
		snprintf(out, size, "The track has no camera for the end of the race - the game would crash there. The exporter has to write it.");
	}
	else if (spawn->present && (spawn->count >= 4) && !spawn->nonNull[3])
	{
		snprintf(out, size, "The track has no camera path for the fly-in at the start - the game would crash there. The exporter has to write it.");
	}
	else if (((bit & RLD_MODE_TIME_TRIAL) != 0u) && !ghosts)
	{
		snprintf(out, size, "Needs the start spots of the N. Tropy and N. Oxide ghosts - this track has none.");
	}
	else if ((bit & RLD_MODE_BATTLE) != 0u)
	{
		snprintf(out, size, "Battle is reserved for a later version of CTR Reload.");
	}
	else if (((bit & RLD_LAP_MODES) != 0u) && (data->restartPoints == 0u))
	{
		snprintf(out, size, "Needs restart points (the checkpoints that count the laps) - this track has none.");
	}
	else if (((bit & (RLD_MODE_CTR_CHALLENGE | RLD_MODE_CRYSTAL_CHALLENGE)) != 0u) && (ids->problem != NULL))
	{
		snprintf(out, size, "The objects on this track cannot be read, so its letters and crystals cannot be counted.");
	}
	else if (((bit & RLD_MODE_CTR_CHALLENGE) != 0u) && ((ids->letters[0] != 1u) || (ids->letters[1] != 1u) || (ids->letters[2] != 1u)))
	{
		snprintf(out, size, "Needs each of the letters C, T and R exactly once on the track - this track has C %u, T %u, R %u.", ids->letters[0], ids->letters[1],
		         ids->letters[2]);
	}
	else if (((bit & RLD_MODE_CRYSTAL_CHALLENGE) != 0u) && (ids->crystals == 0u))
	{
		snprintf(out, size, "Needs at least one crystal on the track - this track has none.");
	}
}

// @mode for all five modes, not only the declared ones - Reload Studio shows
// each as a checkbox, grey with a reason. Plus @lev.
static void Rld_EmitModes(u32 declared, const struct RldSpawn *spawn, const struct RldLevModes *data, const struct RldModelIds *ids)
{
	int k;

	if (!s_machine)
	{
		return;
	}

	for (k = 0; k < 5; k++)
	{
		const u32 bit = s_modeOrder[k];
		char why[512];
		char reason[512];
		const int has = Rld_ModeData(bit, spawn, data, ids, why, sizeof(why));

		reason[0] = '\0';
		if (!has)
		{
			Rld_ModeReason(bit, spawn, data, ids, reason, sizeof(reason));
			if (reason[0] == '\0')
			{
				snprintf(reason, sizeof(reason), "%s", why);
			}
		}

		Rld_Emit("mode", s_modeWords[k], has ? "yes" : "no", ((declared & bit) != 0u) ? "yes" : "no", ((bit & RLD_MODES_PLAYABLE) != 0u) ? "yes" : "no",
				 reason, (const char *)NULL);
	}

	Rld_EmitNumber("lev", "restart_points", data->restartPoints);
	Rld_EmitNumber("lev", "nav_paths", (unsigned long long)data->navPaths);
	{
		char points[48];

		snprintf(points, sizeof(points), "%d,%d,%d", data->navPoints[0], data->navPoints[1], data->navPoints[2]);
		Rld_Emit("lev", "nav_points", points, (const char *)NULL);
	}
	Rld_EmitNumber("lev", "start_spots", (unsigned long long)data->startSpots);
	Rld_EmitNumber("lev", "spawn_count", spawn->present ? (unsigned long long)spawn->count : 0u);
	// The numbers behind CTR and Crystal (Model.id 0x93..0x95 and 0x60), for the
	// "Modes" card in Reload Studio - so far only in the report for humans.
	Rld_EmitNumber("lev", "crystals", (unsigned long long)ids->crystals);
	{
		char letters[48];

		snprintf(letters, sizeof(letters), "%u,%u,%u", ids->letters[0], ids->letters[1], ids->letters[2]);
		Rld_Emit("lev", "letters", letters, (const char *)NULL);
	}
	for (k = 0; k < 2; k++)
	{
		const int place = (data->ambientCoords[k] >= 0) && (data->ambientCoords[k] <= 9);

		Rld_Emit("lev", (k == 0) ? "ambient_place_1" : "ambient_place_2", place ? "yes" : "no", (const char *)NULL);
	}
}

// A finished container that declares ctr or crystal without carrying the data for it
// (seen with containers of Ice Rink and Inferno Island: letters 0,0,0). The
// track list reads only META, so the declaration must be right - make and build
// reject such a thing today, old containers get a warning here. Nothing
// is changed. file NULL: the line for humans (verify), otherwise the
// machine line (info --machine).
static void Rld_ModesDeclaredWarn(const char *file, u32 declared, const struct RldSpawn *spawn, const struct RldLevModes *data, const struct RldModelIds *ids)
{
	static const u32 checked[2] = {RLD_MODE_CTR_CHALLENGE, RLD_MODE_CRYSTAL_CHALLENGE};
	int k;

	for (k = 0; k < 2; k++)
	{
		char why[512];
		char reason[512];

		if (((declared & checked[k]) == 0u) || Rld_ModeData(checked[k], spawn, data, ids, why, sizeof(why)))
		{
			continue;
		}

		Rld_ModeReason(checked[k], spawn, data, ids, reason, sizeof(reason));
		if (reason[0] == '\0')
		{
			snprintf(reason, sizeof(reason), "%s", why);
		}

		if (file == NULL)
		{
			printf("  WARN   MODE   %s is declared, but the track lacks its data - %s\n", Rld_ModesText(checked[k]), reason);
		}
		else
		{
			Rld_Say("warning", "mode-data", why, "%s offers %s, but its track cannot carry it. %s Pack the track again without %s.", file,
			        Rld_ModesText(checked[k]), reason, Rld_ModesText(checked[k]));
		}
	}
}

// The modes as words, as --modes takes them: "race,time".
static const char *Rld_ModesWords(u32 modes)
{
	static char text[64];
	size_t at = 0;

	text[0] = '\0';

#define RLD_MODE_WORD(bit, word, label, tag)                                      \
	if (((modes & (bit)) != 0u) && ((at + strlen(word) + 2u) < sizeof(text)))  \
	{                                                                          \
		at += (size_t)snprintf(&text[at], sizeof(text) - at, "%s%s", (at != 0u) ? "," : "", (word)); \
	}
	RLD_MODE_LIST(RLD_MODE_WORD)
#undef RLD_MODE_WORD

	return text;
}

//----------------------------------------------------------------------------------------
// PARSING PARM VALUES - --reverb, --bots, --ambient and the same keys in
// track.txt. NULL means parsed; otherwise the reason.

// An integer, decimal or 0x..., without sign and without remainder.
static int Rld_ParseParmNumber(const char *text, size_t length, u32 max, u32 *out)
{
	char buffer[16];
	char *end = NULL;
	unsigned long value;

	if ((length == 0u) || (length >= sizeof(buffer)) || (text[0] == '-') || (text[0] == '+'))
	{
		return 0;
	}

	memcpy(buffer, text, length);
	buffer[length] = '\0';
	value = strtoul(buffer, &end, ((length > 2u) && (buffer[0] == '0') && ((buffer[1] == 'x') || (buffer[1] == 'X'))) ? 16 : 10);

	if ((end == buffer) || (*end != '\0') || (value > (unsigned long)max))
	{
		return 0;
	}

	*out = (u32)value;
	return 1;
}

static const char *Rld_ParseParmValues(const char *reverb, const char *bots, const char *ambient, struct RldParm *out)
{
	memset(out, 0, sizeof(*out));

	if (reverb != NULL)
	{
		if (strcmp(reverb, "off") == 0)
		{
			out->reverb = RLD_PARM_REVERB_OFF;
		}
		else if (!Rld_ParseParmNumber(reverb, strlen(reverb), RLD_PARM_REVERB_MAX, &out->reverb))
		{
			return "--reverb takes a reverb step 0..4 or off (2 is what Dingo Canyon uses)";
		}
		out->reverbState = RLD_PARM_SET;
	}

	if (bots != NULL)
	{
		if (!Rld_ParseParmNumber(bots, strlen(bots), RLD_PARM_BOTS_MAX, &out->bots))
		{
			return "--bots takes a retail track row 0..17 - the bots drive as on that track (0 Dingo Canyon .. 17 Turbo Track)";
		}
		out->botsState = RLD_PARM_SET;
	}

	if (ambient != NULL)
	{
		const char *comma = strchr(ambient, ',');
		const size_t first = (comma != NULL) ? (size_t)(comma - ambient) : strlen(ambient);

		if (!Rld_ParseParmNumber(ambient, first, 0xffffu, &out->ambient[0]) ||
		    ((comma != NULL) && !Rld_ParseParmNumber(comma + 1, strlen(comma + 1), 0xffffu, &out->ambient[1])))
		{
			return "--ambient takes one or two sound numbers, 0 = none, for example --ambient 0x83 or --ambient 131,0";
		}
		out->ambientState = RLD_PARM_SET;
	}

	return NULL;
}

//========================================================================================
// THE SHARED PACKING PATH OF build AND make
//========================================================================================
//
// All of this used to be in Cmd_Build. make needs the same path -
// reading, checking, ABR, menu map, build, writing, report -, and a second
// path next to it would be the place where the two drift apart. Since 4.1
// both check the same: spawn table, data per mode, PARM. make additionally switches on
// the model ID correction and the SNDB from memory.

struct RldPackJob
{
	const char *levPath;
	const char *vrmPath;
	const char *outPath;

	// What LEV and VRM are called in a message: "--lev " for build; for make
	// empty, there the file name stands for itself.
	const char *levFlag;
	const char *vrmFlag;

	// The sound: for build a file (--sndb), for make built in memory.
	const char *sndbPath;
	const u8 *sndbData;
	size_t sndbSize;

	const char *trackVersionText;
	const char *modesText;
	const char *strings[RLD_STRING_COUNT];
	int abrKeep;

	// PARM (4.1): --reverb, --bots, --ambient or the same keys in
	// track.txt. NULL means not set.
	const char *reverbText;
	const char *botsText;
	const char *ambientText;

	// make only: align the model ID (off with --keep-model-ids), report ID
	// and spawn table.
	int make;
	int keepModelIds;

	// make --check only: check and build everything, but write nothing.
	// failedBefore: make has already found something (the music) and only lets
	// Rld_Pack keep running so that the modes are reported.
	int check;
	int failedBefore;
};

static int Rld_Pack(const struct RldPackJob *job)
{
	struct RldBuildInput in;
	u8 *lev = NULL, *vrm = NULL, *sndbFile = NULL, *out = NULL;
	const u8 *sndb = NULL;
	size_t levSize = 0, vrmSize = 0, sndbSize = 0, outSize = 0;
	int abrChecked = 0;
	struct RldAbrReport abrReport;
	struct RldModelIds modelIds;
	struct RldSpawn spawn;
	struct RldLevModes levModes;
	struct RldParm parm;
	u8 *parmChunk = NULL;
	size_t parmChunkSize = 0;
	char mapLine[256];
	const char *mapKind = "none";
	const char *error = NULL;
	char digestText[65];
	int i;
	FILE *file;

	memset(&in, 0, sizeof(in));
	memset(&modelIds, 0, sizeof(modelIds));
	memset(&spawn, 0, sizeof(spawn));
	memset(&parm, 0, sizeof(parm));

	for (i = 0; i < RLD_STRING_COUNT; i++)
	{
		in.strings[i] = job->strings[i];
	}

	in.trackVersion = 1;

	// The default is RACE, and it is the honest one: a track that says nothing
	// is a race course. Assuming anything else would be a claim about
	// data the packer cannot read.
	in.modes = RLD_MODE_RACE;

	// Separate, so that the message says WHICH file is missing. "LEV or VRM cannot
	// be read" sends an author looking for both.
	if (!Rld_ReadFile(job->levPath, &lev, &levSize))
	{
		if (!s_rldReadTooLarge) // said already, with its reason
		{
			fprintf(stderr, "rldpack: cannot read %s\n", job->levPath);
			Rld_Say("error", "lev-read", job->levPath, "The track geometry file (.lev) cannot be read.");
		}
		return 1;
	}

	if (!Rld_ReadFile(job->vrmPath, &vrm, &vrmSize))
	{
		if (!s_rldReadTooLarge) // said already, with its reason
		{
			fprintf(stderr, "rldpack: cannot read %s\n", job->vrmPath);
			Rld_Say("error", "vrm-read", job->vrmPath, "The texture file (.vrm) cannot be read.");
		}
		return 1;
	}

	// The upper limits of the format. Otherwise they are only in the
	// game's validator, and there the author learns about them only when their
	// track is rejected - after uploading.
	if (levSize > RLD_LIMIT_LEVD)
	{
		fprintf(stderr, "rldpack: %s is %llu bytes - the format allows 16 MB at most\n", job->levPath, (unsigned long long)levSize);
		Rld_Say("error", "lev-size", job->levPath, "The track geometry file is %llu bytes. A container allows 16 MB at most.", (unsigned long long)levSize);
		return 1;
	}

	if (vrmSize > RLD_LIMIT_VRMD)
	{
		fprintf(stderr, "rldpack: %s is %llu bytes - the format allows 4 MB at most\n", job->vrmPath, (unsigned long long)vrmSize);
		Rld_Say("error", "vrm-size", job->vrmPath, "The texture file is %llu bytes. A container allows 4 MB at most.", (unsigned long long)vrmSize);
		return 1;
	}

	{
		const char *why = Rld_WhyNotLev(lev, levSize);

		if (why != NULL)
		{
			fprintf(stderr, "rldpack: %s%s: %s\n", job->levFlag, job->levPath, why);
			Rld_Say("error", "lev-bad", why, "The .lev file is not a CTR track (%s).", job->levPath);
			return 1;
		}

		why = Rld_WhyNotVram(vrm, vrmSize);

		if (why != NULL)
		{
			fprintf(stderr, "rldpack: %s%s: %s\n", job->vrmFlag, job->vrmPath, why);
			Rld_Say("error", "vrm-bad", why, "The .vrm file is not a CTR texture file (%s).", job->vrmPath);
			return 1;
		}
	}

	// --track-version used to take whatever atoi returned. "0" went through, and "-5"
	// silently became 4,294,967,291 - a track that can never be
	// updated again, because no version can be greater. The field
	// serves update detection, so it starts at 1.
	if (job->trackVersionText != NULL)
	{
		char *end = NULL;
		unsigned long value = strtoul(job->trackVersionText, &end, 10);

		if ((end == job->trackVersionText) || (*end != '\0') || (job->trackVersionText[0] == '-') || (value < 1uL) || (value > 0xffffffffuL))
		{
			fprintf(stderr, "rldpack: --track-version needs a whole number, 1 or higher\n");
			Rld_Say("error", "track-version", job->trackVersionText, "The version must be a whole number, 1 or higher.");
			return 1;
		}

		in.trackVersion = (u32)value;
	}

	if (job->modesText != NULL)
	{
		in.modes = Rld_ParseModes(job->modesText);

		if (in.modes == 0u)
		{
			fprintf(stderr, "rldpack: --modes takes a comma separated list of race, ctr, time, crystal (battle is reserved)\n");
			fprintf(stderr, "         for example: --modes race,time\n");
			Rld_Say("error", "modes", job->modesText, "The list of modes is not valid. The modes are race, ctr, time and crystal.");
			return 1;
		}
	}

	// THE TRACK VALUES (PARM), before anything else that could write.
	{
		const char *why = Rld_ParseParmValues(job->reverbText, job->botsText, job->ambientText, &parm);

		if (why != NULL)
		{
			fprintf(stderr, "rldpack: %s\n", why);
			Rld_Say("error", "parm", why, "A value for reverb, bots or ambient sound is not valid.");
			return 1;
		}
	}

	// THE SPAWN TABLE, build and make, on the raw LEV: before any correction.
	// ABR and model ID do not touch it anyway (tpage in the
	// TextureLayouts, InstDef+0x3c). It is only checked
	// together with the modes further down, so that --machine can report all five
	// modes; nothing is written before that.
	Rld_SpawnRead(lev, levSize, &spawn);

	// THE SOUND CHUNK, optional.
	//
	// It is checked with THE SAME function the game will run when loading
	// - not with a second opinion about what an SNDB is. That
	// is exactly the trap that swapped --lev/--vrm set:
	// a packer that accepts everything moves the error to where it
	// looks like a broken container instead of like a typo.
	if (job->sndbPath != NULL)
	{
		if (!Rld_ReadFile(job->sndbPath, &sndbFile, &sndbSize))
		{
			if (!s_rldReadTooLarge) // said already, with its reason
			{
				fprintf(stderr, "rldpack: cannot read %s\n", job->sndbPath);
				Rld_Say("error", "sndb-read", job->sndbPath, "The sound file cannot be read.");
			}
			return 1;
		}

		sndb = sndbFile;
	}
	else if (job->sndbData != NULL)
	{
		sndb = job->sndbData;
		sndbSize = job->sndbSize;
	}

	if (sndb != NULL)
	{
		struct RldSndb probe;
		const char *why;

		if (sndbSize > RLD_LIMIT_SNDB)
		{
			fprintf(stderr, "rldpack: %s is %llu bytes - the format allows 4 MB at most\n", (job->sndbPath != NULL) ? job->sndbPath : "the SNDB built from the .sca",
			        (unsigned long long)sndbSize);
			Rld_Say("error", "sndb-size", NULL, "The music is %llu bytes. A container allows 4 MB at most.", (unsigned long long)sndbSize);
			return 1;
		}

		why = Rld_ParseSndb(&probe, sndb, sndbSize);
		if (why != NULL)
		{
			fprintf(stderr, "rldpack: --sndb %s: %s\n", (job->sndbPath != NULL) ? job->sndbPath : "(built from the .sca)", why);
			Rld_Say("error", "sndb-bad", why, "The music does not fit the game's sound tables.");
			return 1;
		}
	}

	// CORRECT THE ABR FIELD, before anything is hashed.
	//
	// Here and not in the renderer: the cause is in the export, and the VRM
	// is in the same container - the packer can check instead of guessing. The
	// renderer already does the right thing with what it gets.
	//
	// The LEV in the container is then no longer byte for byte the input file.
	// That is the purpose, and that is why the report says how much has
	// changed. --abr-keep leaves everything as it was.
	if (job->abrKeep)
	{
		memset(&abrReport, 0, sizeof(abrReport));
		abrChecked = 0;
	}
	else
	{
		abrChecked = Rld_FixAbr(lev, levSize, vrm, vrmSize, &abrReport);
	}

	// THE MODEL ID, after ABR and before the map. Counted
	// in build and make - the check of the modes needs the letters and crystals
	// -, aligned only in make. ABR and ID touch different
	// bytes; META does not change because of it, Rld_MemNeed reads no instances.
	Rld_ModelIds(lev, levSize, job->make && !job->keepModelIds, &modelIds);

	// THE DATA PER MODE (4.1): a declared mode without its data aborts
	// before a file is created.
	Rld_LevModesRead(lev, levSize, &levModes);
	Rld_EmitModes(in.modes, &spawn, &levModes, &modelIds);
	Rld_EmitNumber("lev", "model_ids_fixed", (job->make && !job->keepModelIds && (modelIds.problem == NULL)) ? modelIds.differ : 0u);
	{
		char why[512];
		char reason[512];
		int k;

		// First the spawn table, then the modes - as before; the report names
		// the first reason, the machine lines every declared mode.
		const int spawnFails = (Rld_SpawnCheck(&spawn, in.modes, why, sizeof(why)) != NULL);

		if (spawnFails || (Rld_ModeCheck(in.modes, &levModes, &modelIds, why, sizeof(why)) != NULL))
		{
			fprintf(stderr, "rldpack: %s\n", why);
			if (spawnFails && (Rld_SpawnCheck(&spawn, 0u, reason, sizeof(reason)) != NULL))
			{
				Rld_ModeReason(0u, &spawn, &levModes, &modelIds, reason, sizeof(reason));
				Rld_Say("error", "spawn", why, "%s", reason);
			}
			else
			{
				for (k = 0; k < 5; k++)
				{
					char one[512];

					if (((in.modes & s_modeOrder[k]) != 0u) && !Rld_ModeData(s_modeOrder[k], &spawn, &levModes, &modelIds, one, sizeof(one)))
					{
						Rld_ModeReason(s_modeOrder[k], &spawn, &levModes, &modelIds, reason, sizeof(reason));
						Rld_Say("error", "mode", one, "%s is ticked, but this track cannot carry it. %s", Rld_ModesText(s_modeOrder[k]), reason);
					}
				}
			}
			return 1;
		}
	}

	// THE INSTANCE LIMIT. Every InstDef of the LEV takes a
	// slot in the game's instance pool in a race (128, MainInit.c:342), including
	// hidden ones like time crates and letters. 10 slots are taken beforehand by the 8
	// drivers and 2 HUD instances, 8 more by the wake if the LEV
	// carries a wake model; during the race up to 21 more come in, as measured
	// (crate explosions, weapons). Above 118 the game cuts off when loading.
	// The largest disc race track (Dingo Canyon) has 66.
	if (modelIds.instances > RLD_INSTANCES_MAX)
	{
		char technical[96];

		snprintf(technical, sizeof(technical), "Level.numInstances %u, at most %u", modelIds.instances, RLD_INSTANCES_MAX);
		fprintf(stderr, "rldpack: the LEV has %u instances, at most %u fit the game's instance pool in a race\n", modelIds.instances,
		        RLD_INSTANCES_MAX);
		Rld_Say("error", "instances", technical,
		        "The track has %u objects (crates, fruit, letters and every other placed model). The game has room for %u in a race - remove at least %u.",
		        modelIds.instances, RLD_INSTANCES_MAX, modelIds.instances - RLD_INSTANCES_MAX);
		return 1;
	}

	if (modelIds.instances > RLD_INSTANCES_WARN)
	{
		char technical[96];

		snprintf(technical, sizeof(technical), "Level.numInstances %u, warning above %u, limit %u", modelIds.instances, RLD_INSTANCES_WARN,
		         RLD_INSTANCES_MAX);
		printf("\nWARNING: the LEV has %u instances. Above %u the race may run out of room for explosions and weapons; the limit is %u.\n",
		       modelIds.instances, RLD_INSTANCES_WARN, RLD_INSTANCES_MAX);
		Rld_Say("warning", "instances", technical,
		        "The track has %u objects (crates, fruit, letters and every other placed model). Above %u the race may run short of room for explosions and weapons; the disc tracks use at most 66.",
		        modelIds.instances, RLD_INSTANCES_WARN);
	}

	Rld_EmitNumber("lev", "instances", modelIds.instances);

	// THE MENU MAP - only a report now (4.1).
	//
	// Up to 4.0 the packer put a shrunk map as MMAP into the container
	// if the LEV's map did not fit the menu strip. Since 4.1
	// the game shrinks it itself, with the same Rld_ScaleMap. All that is left here
	// is what the menu will show - read from the LEV AFTER the
	// ABR pass, the one that goes into the container.
	{
		struct RldMapHalf levMap[2];
		const char *why = Rld_LevMap(lev, levSize, vrm, vrmSize, levMap);

		mapKind = "none";
		if (why != NULL)
		{
			snprintf(mapLine, sizeof(mapLine), "none - %s", why);

			// A map table (SpawnType1 slot 0) without a map picture used to
			// make the race crash (UI_Map.c). The game now draws no map
			// then; it is said here.
			if (spawn.present && (spawn.count > 0) && spawn.nonNull[0])
			{
				Rld_Say("warning", "map-icons", why,
				        "The track has a minimap table but no minimap picture (%s). The race shows no minimap. Export the map textures or remove the map table.", why);
			}
		}
		else
		{
			const char *tooBig = Rld_MapWhyNotMenu(levMap);

			if (tooBig == NULL)
			{
				mapKind = "fits";
				snprintf(mapLine, sizeof(mapLine), "from the LEV, it fits the menu strip (halves %dx%d and %dx%d VRAM halfwords)", levMap[0].w,
				         levMap[0].h, levMap[1].w, levMap[1].h);
			}
			else
			{
				struct RldMapHalf scaled[2];

				why = Rld_ScaleMap(levMap, scaled);

				if (why != NULL)
				{
					snprintf(mapLine, sizeof(mapLine), "none - %s, and it cannot be scaled: %s", tooBig, why);
				}
				else
				{
					mapKind = "scaled";
					snprintf(mapLine, sizeof(mapLine), "the game scales it from %dx%d and %dx%d texels to %dx%d and %dx%d, %d bit",
					         levMap[0].layout[4] - levMap[0].layout[0], levMap[0].layout[9] - levMap[0].layout[1],
					         levMap[1].layout[4] - levMap[1].layout[0], levMap[1].layout[9] - levMap[1].layout[1], scaled[0].layout[4],
					         scaled[0].layout[9], scaled[1].layout[4], scaled[1].layout[9], (scaled[0].depth == 0) ? 4 : 8);
					Rld_FreeMap(scaled);
				}
			}

			Rld_FreeMap(levMap);
		}
	}

	in.lev = lev;
	in.levSize = levSize;
	in.vrm = vrm;
	in.vrmSize = vrmSize;
	in.sndb = sndb;
	in.sndbSize = sndbSize;

	parmChunk = Rld_BuildParm(&parm, &parmChunkSize);
	in.parm = parmChunk;
	in.parmSize = parmChunkSize;

	out = Rld_Build(&in, &outSize, &error);

	if (out == NULL)
	{
		fprintf(stderr, "rldpack: %s\n", error ? error : "build failed");
		Rld_Say("error", "build", error, "The container could not be put together (%s).", error ? error : "build failed");
		return 1;
	}

	// --check builds everything and writes nothing. The hash is the same one a build
	// would give: the build is deterministic.
	if (!job->check)
	{
		file = fopen(job->outPath, "wb");
		if ((file == NULL) || (fwrite(out, 1, outSize, file) != outSize) || (fclose(file) != 0))
		{
			fprintf(stderr, "rldpack: cannot write %s\n", job->outPath);
			Rld_Say("error", "write", job->outPath,
					"The container file cannot be written. Is the folder write-protected, or is the file open in another program?");
			return 1;
		}
	}

	{
		u8 digest[32];
		int k;

		Sha256(out, outSize, digest);
		for (k = 0; k < 32; k++)
		{
			snprintf(&digestText[k * 2], 3, "%02x", digest[k]);
		}
	}

	if (job->check)
	{
		printf("%s (check only - nothing written)\n", job->outPath);
	}
	else
	{
		printf("%s\n", job->outPath);
	}
	printf("  size          %llu bytes\n", (unsigned long long)outSize);
	printf("  LEVD          %llu bytes\n", (unsigned long long)levSize);
	printf("  VRMD          %llu bytes\n", (unsigned long long)vrmSize);
	if (sndb != NULL)
	{
		struct RldSndb shownSound;

		if (Rld_ParseSndb(&shownSound, sndb, sndbSize) == NULL)
		{
			printf("  SNDB          %llu bytes, %u entries, plays bank %u and sequence %u\n", (unsigned long long)sndbSize,
			       shownSound.entryCount, shownSound.playBank, shownSound.playSong);
		}
	}
	if (parmChunk != NULL)
	{
		printf("  PARM          %llu bytes -", (unsigned long long)parmChunkSize);
		if (parm.reverbState == RLD_PARM_SET)
		{
			if (parm.reverb == RLD_PARM_REVERB_OFF)
			{
				printf(" reverb off");
			}
			else
			{
				printf(" reverb %u", parm.reverb);
			}
		}
		if (parm.botsState == RLD_PARM_SET)
		{
			printf(" bots row %u", parm.bots);
		}
		if (parm.ambientState == RLD_PARM_SET)
		{
			printf(" ambient 0x%x/0x%x", parm.ambient[0], parm.ambient[1]);
		}
		printf("\n");
	}
	else
	{
		printf("  PARM          none - the game uses its defaults (reverb 2, bots row 0, no ambient sound)\n");
	}
	printf("  menu map      %s\n", mapLine);
	printf("  modes         %s\n", Rld_ModesText(in.modes));
	if (job->make)
	{
		if (modelIds.problem != NULL)
		{
			printf("  model ids     not checked - %s\n", modelIds.problem);
		}
		else
		{
			u32 k;

			if (job->keepModelIds)
			{
				printf("  model ids     left alone (--keep-model-ids) - %u of %u instances disagree\n", modelIds.differ, modelIds.instances);
			}
			else
			{
				printf("  model ids     %u of %u instances corrected\n", modelIds.differ, modelIds.instances);
			}

			for (k = 0; (k < modelIds.differ) && (k < RLD_MODELID_SHOWN); k++)
			{
				printf("                instance %u: modelID ", modelIds.fixes[k].index);
				Rld_PrintModelId(modelIds.fixes[k].before);
				printf(" -> ");
				Rld_PrintModelId(modelIds.fixes[k].after);
				printf(" (from Model.id)\n");
			}

			if (modelIds.differ > RLD_MODELID_SHOWN)
			{
				printf("                ... and %u more\n", modelIds.differ - RLD_MODELID_SHOWN);
			}

			if (modelIds.letterFixes != 0u)
			{
				printf("                %u letter model(s) %s by name - see NOTE below\n", modelIds.letterFixes,
				       job->keepModelIds ? "disagree with their Model.id" : "got their Model.id");
			}

			printf("                letters C %u, T %u, R %u and %u crystal(s) by Model.id\n", modelIds.letters[0], modelIds.letters[1],
			       modelIds.letters[2], modelIds.crystals);
		}

		if (!spawn.present)
		{
			printf("  spawn table   none - the level carries no SpawnType1 (Level+0x134 is 0)\n");
		}
		else
		{
			const int ghosts = (spawn.count >= 6) && spawn.nonNull[4] && spawn.nonNull[5] && spawn.inMap[4] && spawn.inMap[5];

			printf("  spawn table   count %d - %s, %s\n", spawn.count, ghosts ? "N. Tropy / N. Oxide ghosts at slots 4 and 5" : "no ghosts (time trial not offered)",
			       (spawn.count >= 4) ? "fly-in camera path at slot 3" : "no fly-in (count below 4)");
		}
	}
	{
		// What the track demands in memory - visible when BUILDING and not
		// only when a player reports holes in the picture. The same calculation that
		// ends up in META and that the game sets aside before MEMPACK_Init.
		struct RldMemNeed shown;

		Rld_MemNeed(&shown, lev, levSize);
		printf("  draw memory   %u bytes per buffer, %u quad blocks\n", shown.primBytes,
		       shown.primBytes / (RLD_QUADS_PER_BLOCK * RLD_POLY_GT4_BYTES));
		if (shown.skyBytes != 0u)
		{
			printf("  sky memory    %u bytes per buffer\n", shown.skyBytes);
		}
		printf("  reserves      %u bytes in the game\n", shown.total);
	}
	if (job->abrKeep)
	{
		printf("  ABR           left alone (--abr-keep) - every face draws the slow way\n");
	}
	else if (!abrChecked)
	{
		printf("  ABR           not checked - no readable quad block table in this LEV\n");
	}
	else
	{
		printf("  ABR           %u of %u texture layouts corrected, %u of %u faces draw opaque now\n", abrReport.layoutsFixed,
		       abrReport.layouts, abrReport.facesFixed, abrReport.faces);

		if ((abrReport.layoutsStp != 0u) || (abrReport.layoutsUnknown != 0u))
		{
			printf("                %u kept (a texel really carries STP), %u left alone (not decidable)\n",
			       abrReport.layoutsStp, abrReport.layoutsUnknown);
		}
	}
	printf("  format        %u.%u, meta_version %d (frozen)\n", s_rldTrackFormat.major, s_rldTrackFormat.minorWritten, RLD_META_VERSION);

	Rld_Emit("lev", "map", mapKind, (const char *)NULL);
	// Without a map this is not an error: a note, not an abort.
	if (strcmp(mapKind, "none") == 0)
	{
		Rld_Say("note", "no-map", mapLine,
		        "The track has no minimap, so the menu and the race show none. The track works without one. To get a minimap, export it with the track: the LEV needs the two map icons (top and bottom half) and the map entry in its spawn table.");
	}
	// The letters like the ID: make only, off with --keep-model-ids - then
	// reported, but left alone.
	if (job->make)
	{
		Rld_LetterReport(&modelIds, !job->keepModelIds);
	}
	if (job->make && !job->keepModelIds && (modelIds.problem == NULL) && (modelIds.differ != 0u))
	{
		Rld_Say("info", "model-ids", NULL, "The model numbers of %u object(s) were corrected, so the game shows the right models.", modelIds.differ);
	}

	if (abrReport.layoutsFixed != 0u)
	{
		Rld_Say("info", "abr", NULL, "%u texture layouts were marked as not transparent, so the track draws faster. The picture does not change.",
				abrReport.layoutsFixed);
		// Otherwise nobody notices that their exporter does not write the field.
		printf("\nNOTE: %u texture layouts carried a tpage whose ABR field was not 3.\n", abrReport.layoutsFixed);
		printf("  CTR reads ABR != 3 as semi-transparent. Such a face gets its own draw\n");
		printf("  split and is drawn TWICE, and no face around it can be batched with it.\n");
		printf("  Measured on one exported track: that was 35.5 ms of a 48.4 ms frame.\n");
		printf("  On the disc 0.0 to 8.3 percent of faces are semi-transparent - a few\n");
		printf("  really are, and those are meant to be. Here it was %u percent, and a\n", (abrReport.faces != 0u) ? ((abrReport.facesChanged * 100u) / abrReport.faces) : 0u);
		printf("  number near a hundred means the exporter never writes the field at all.\n");
		printf("  rldpack has set the field where no texel the face reads carries STP, so\n");
		printf("  the picture is unchanged. Fix it in the exporter and this note goes away.\n");
	}

	// What is written but needs a note (4.1). The aborts per
	// mode ran before writing (Rld_ModeCheck); here are only things with
	// which the track is still valid.
	if (((in.modes & RLD_MODE_RACE) != 0u) && (levModes.navPaths == 0))
	{
		printf("\nWARNING: --modes race, but the LEV carries no nav paths (LevNavTable) - bots will not drive.\n");
		// Without a path with more than one point BOTS_Driver_Init creates no bot at all
		// (BOTS.c:3116-3134; the game log then shows "[CTR Race] ... bots 0").
		Rld_Say("warning", "no-nav", "--modes race, but the LEV carries no nav paths (LevNavTable) - bots will not drive.",
				"The track has no nav paths (the lines the bots follow), so the game starts the race without bots - the player drives alone. Export the track with its nav paths to race against bots.");
	}

	// The eight start spots (only as a machine line, the report
	// stays the same): if drivers sit on the same point, they start inside each other.
	if (((in.modes & RLD_MODE_RACE) != 0u) && (levModes.startSpots < RLD_DRIVER_SPAWNS))
	{
		char technical[96];

		snprintf(technical, sizeof(technical), "DriverSpawn: %d distinct of 8, %d at 0,0,0", levModes.startSpots, levModes.startZero);
		Rld_Say("warning", "start-spots", technical,
		        "The 8 start spots give only %d different place(s) (%d sit at 0,0,0). Karts on the same place start inside each other. Export the track with all 8 start spots.",
		        levModes.startSpots, levModes.startZero);
	}

	if (parm.ambientState == RLD_PARM_SET)
	{
		int k;

		for (k = 0; k < 2; k++)
		{
			if ((parm.ambient[k] != 0u) && ((levModes.ambientCoords[k] < 0) || (levModes.ambientCoords[k] > 9)))
			{
				printf("\nWARNING: ambient sound %d (0x%x) has no place in this LEV - SpawnType2 slot %d with at most 9 points - it will not play.\n",
				       k + 1, parm.ambient[k], 5 + k);
				Rld_Say("warning", "ambient-place", NULL, "Ambient sound %d (0x%x) has no spot on this track, so it will not play.", k + 1, parm.ambient[k]);
			}
		}
	}

	// Declared, but not built yet in the game for containers: valid, and
	// said (Reload Studio shows the same note).
#define RLD_MODE_NOTE(bit, word, label, tag)                                                          \
	if (((in.modes & (bit)) != 0u) && (((bit) & RLD_MODES_PLAYABLE) == 0u))                          \
	{                                                                                                 \
		printf("\nnote: %s is not playable in CTR Reload yet.\n", (label));                            \
		Rld_Say("note", "not-playable", NULL, "%s is not playable in CTR Reload yet. It is stored in the container for later.", (label)); \
	}
	RLD_MODE_LIST(RLD_MODE_NOTE)
#undef RLD_MODE_NOTE

	Rld_WarnAboutLevTables(lev, levSize);
	Rld_WarnAboutSky(lev, levSize);
	Rld_WarnAboutVisibility(lev, levSize);
	free(parmChunk);

	if (job->check && job->failedBefore)
	{
		return 1;
	}

	{
		char bytes[24];

		snprintf(bytes, sizeof(bytes), "%llu", (unsigned long long)outSize);
		Rld_Emit("result", job->check ? "checked" : "ok", job->outPath, bytes, digestText, (const char *)NULL);
		s_machineResult = 1;
	}
	return 0;
}

static int Cmd_Build(int argc, char *argv[])
{
	struct RldPackJob job;
	int i;

	memset(&job, 0, sizeof(job));
	job.strings[0] = "";
	job.strings[1] = "";
	job.levFlag = "--lev ";
	job.vrmFlag = "--vrm ";

	for (i = 0; i < argc; i++)
	{
		const char *arg = argv[i];
		const char *value = ((i + 1) < argc) ? argv[i + 1] : NULL;

		if ((strcmp(arg, "--lev") == 0) && value) { job.levPath = argv[++i]; }
		else if ((strcmp(arg, "--vrm") == 0) && value) { job.vrmPath = argv[++i]; }
		else if ((strcmp(arg, "--sndb") == 0) && value) { job.sndbPath = argv[++i]; }
		else if ((strcmp(arg, "--out") == 0) && value) { job.outPath = argv[++i]; }
		else if ((strcmp(arg, "--name") == 0) && value) { job.strings[0] = argv[++i]; }
		else if ((strcmp(arg, "--author") == 0) && value) { job.strings[1] = argv[++i]; }
		else if ((strcmp(arg, "--track-version") == 0) && value) { job.trackVersionText = argv[++i]; }
		else if ((strcmp(arg, "--modes") == 0) && value) { job.modesText = argv[++i]; }
		else if ((strcmp(arg, "--reverb") == 0) && value) { job.reverbText = argv[++i]; }
		else if ((strcmp(arg, "--bots") == 0) && value) { job.botsText = argv[++i]; }
		else if ((strcmp(arg, "--ambient") == 0) && value) { job.ambientText = argv[++i]; }
		else if (strcmp(arg, "--abr-keep") == 0) { job.abrKeep = 1; }

		// Whatever is not listed here is NOT silently swallowed.
		//
		// Noticed when removing --thumb: the switch was gone, the build
		// still ran through, and an author with their old command line would have
		// believed their preview image was in it. The same holds for every
		// typo - `--licence` spelled the British way was simply discarded
		// and the track went out without a licence.
		//
		// The tree has learned this rule once before: unknown switches
		// also abort the game start.
		else if (strcmp(arg, "--thumb") == 0)
		{
			fprintf(stderr, "rldpack: --thumb is gone. THMB was taken out of the format because\n");
			fprintf(stderr, "         nothing on the game side can decode an image yet.\n");
			return 2;
		}

		// The same rule, the same reason: an author with their old command line
		// should not believe their track is still signed.
		else if (strcmp(arg, "--key") == 0)
		{
			fprintf(stderr, "rldpack: --key is gone. Containers are not signed any more - the\n");
			fprintf(stderr, "         SIGN chunk was taken out of the format.\n");
			fprintf(stderr, "         See the head of include/rldtrack.inc.\n");
			return 2;
		}
		else
		{
			fprintf(stderr, "rldpack: unknown argument \"%s\"\n\n", arg);
			Rld_Say("error", "usage", arg, "rldpack does not know the argument \"%s\".", arg);
			Rld_Usage();
			return 2;
		}
	}

	if ((job.levPath == NULL) || (job.vrmPath == NULL) || (job.outPath == NULL) || (job.strings[0][0] == '\0'))
	{
		// Not one squeezed line, but the same help as without
		// arguments. Whoever lands here does not know how to go on right now.
		fprintf(stderr, "rldpack: build needs --lev, --vrm, --name and --out.\n\n");
		Rld_Usage();
		return 2;
	}

	return Rld_Pack(&job);
}

//========================================================================================
// MAKE - A FOLDER BECOMES A CONTAINER
//========================================================================================
//
// rldpack make <folder>: LEV and VRM required, music (a .sca or a finished
// .sndb) optional, plus optionally track.txt. The shared packing path derives
// the menu map.
//
// THE MAKER WRITES NO LEVEL ID INTO THE CONTAINER. The game assigns it itself
// during the scan (track-ids.tsv in the tracks folder). No field,
// no placeholder, no format change - make builds the same container that
// build builds, only with less manual work.

// --- The music: Project Saphi .sca ----------------------------------------------------
//
// Ported from an earlier Python converter that no longer exists: turning a
// .sca into an SNDB is done only here.
//
// 'SCA' + version byte 1, then chunks [4CC, u32 size, body, padded to 4]:
//   BANK  HOWL bank: 0x800 header (s16 n, s16 spuIndex[n]), then SPU ADPCM
//   CSEQ  CseqPack as in KART.HWL
//   SIZE  u16 SPU size per sample, in 8-byte units, order as in the header
//   META  JSON (name, author) - for Baby T Park the MUSIC author, so only a hint
//
// PADDED SCA: newer exports pad
// BANK and CSEQ to whole sectors (2048), like the banks in KART.HWL. The
// BANK then carries up to 2047 bytes behind the last sample (in one test track: 32
// bytes, not zero), CSEQ zeros behind the song. This is accepted only if
// the chunk is padded EXACTLY to the next sector; the lengths then come
// from SIZE and the song header, the rest is not taken over. Rld_BuildSndb
// pads with zeros itself - the SNDB is the same as from an exact SCA.
#define RLD_SCA_BANK_HEAD 0x800u

struct RldSca
{
	const u8 *bank;
	size_t bankSize;
	const u8 *cseq;
	size_t cseqSize;
	const u8 *sizes;
	size_t sizesSize;
	const u8 *meta;
	size_t metaSize;
	int samples;
	size_t bankPad;
	size_t cseqPad;
};

static size_t Rld_ScaSectorUp(u64 size)
{
	return (size_t)(((size + RLD_HOWL_SECTOR - 1u) / RLD_HOWL_SECTOR) * RLD_HOWL_SECTOR);
}

static const char *Rld_ReadSca(const u8 *data, size_t size, struct RldSca *out)
{
	static char problem[200];
	size_t at = 4;
	u64 adpcm = 0;
	u32 song;
	int k;

	memset(out, 0, sizeof(*out));

	if ((size < 4u) || (memcmp(data, "SCA", 3) != 0) || (data[3] != 1u))
	{
		return "not an SCA file of version 1";
	}

	while (at < size)
	{
		u32 chunkSize;
		const u8 *body;

		if ((size - at) < 8u)
		{
			return "the chunk chain breaks off inside a chunk header";
		}

		chunkSize = Rld_ReadLE32At(data, at + 4u);
		if ((u64)chunkSize > (u64)(size - at - 8u))
		{
			return "a chunk runs past the end of the file";
		}

		body = &data[at + 8u];

		if (memcmp(&data[at], "BANK", 4) == 0) { out->bank = body; out->bankSize = chunkSize; }
		else if (memcmp(&data[at], "CSEQ", 4) == 0) { out->cseq = body; out->cseqSize = chunkSize; }
		else if (memcmp(&data[at], "SIZE", 4) == 0) { out->sizes = body; out->sizesSize = chunkSize; }
		else if (memcmp(&data[at], "META", 4) == 0) { out->meta = body; out->metaSize = chunkSize; }

		at += 8u + (size_t)chunkSize + ((4u - (chunkSize % 4u)) % 4u);
	}

	if (at != size)
	{
		return "the chunk chain does not end at the end of the file";
	}

	if ((out->bank == NULL) || (out->cseq == NULL) || (out->sizes == NULL))
	{
		return "BANK, CSEQ and SIZE are required - one of them is missing";
	}

	if (out->bankSize < RLD_SCA_BANK_HEAD)
	{
		return "BANK is shorter than its 0x800 byte header";
	}

	out->samples = (int)(s16)Rld_ReadLE16At(out->bank, 0);
	if ((out->samples < 1) || ((2u + 2u * (u32)out->samples) > RLD_SCA_BANK_HEAD))
	{
		return "the BANK header names no samples, or more than fit the header";
	}

	if ((out->sizesSize / 2u) != (size_t)out->samples)
	{
		return "SIZE does not carry one value per sample of the BANK header";
	}

	for (k = 0; k < out->samples; k++)
	{
		adpcm += (u64)Rld_ReadLE16At(out->sizes, (size_t)k * 2u) * 8u;
	}

	if ((adpcm != (u64)(out->bankSize - RLD_SCA_BANK_HEAD)) &&
	    (((u64)out->bankSize <= (RLD_SCA_BANK_HEAD + adpcm)) || (out->bankSize != Rld_ScaSectorUp(RLD_SCA_BANK_HEAD + adpcm))))
	{
		snprintf(problem, sizeof(problem), "the SIZE list gives %llu bytes of samples, but the BANK carries %llu bytes behind its header",
		         (unsigned long long)adpcm, (unsigned long long)(out->bankSize - RLD_SCA_BANK_HEAD));
		return problem;
	}

	out->bankPad = out->bankSize - (RLD_SCA_BANK_HEAD + (size_t)adpcm);
	out->bankSize = RLD_SCA_BANK_HEAD + (size_t)adpcm;

	song = (out->cseqSize >= 4u) ? Rld_ReadLE32At(out->cseq, 0) : 0u;
	if ((song < 4u) || ((song != out->cseqSize) && (((u64)song >= (u64)out->cseqSize) || (out->cseqSize != Rld_ScaSectorUp(song)))))
	{
		snprintf(problem, sizeof(problem), "the song says it is %lu bytes long, but its CSEQ block holds %llu bytes", (unsigned long)song,
		         (unsigned long long)out->cseqSize);
		return problem;
	}

	out->cseqPad = out->cseqSize - song;
	out->cseqSize = song;

	return NULL;
}

// A string from the JSON of the SCA META, only for the hint line. No
// JSON reader: it looks for "key", then ':', then a string in
// quotes. If that fails, the line is dropped - no abort.
static int Rld_ScaMetaString(const u8 *meta, size_t metaSize, const char *key, char *dst, size_t dstSize)
{
	const size_t keyLen = strlen(key);
	size_t at;

	for (at = 0; (at + keyLen + 2u) <= metaSize; at++)
	{
		size_t p;
		size_t n = 0;

		if ((meta[at] != '"') || (memcmp(&meta[at + 1u], key, keyLen) != 0) || (meta[at + 1u + keyLen] != '"'))
		{
			continue;
		}

		p = at + keyLen + 2u;
		while ((p < metaSize) && ((meta[p] == ' ') || (meta[p] == '\t') || (meta[p] == ':')))
		{
			p++;
		}

		if ((p >= metaSize) || (meta[p] != '"'))
		{
			return 0;
		}

		for (p++; (p < metaSize) && (meta[p] != '"') && ((n + 1u) < dstSize); p++)
		{
			dst[n++] = (char)meta[p];
		}

		dst[n] = '\0';
		return (p < metaSize) && (meta[p] == '"');
	}

	return 0;
}

// --- KART.HWL: the retail tables a custom bank is measured against -----------
//
// At 0x10 there are 5 x u32 nSpu, nOther, nEngine, nBanks, nSeq. At 0x28 follow
// nSpu x (u16 addr, u16 size), then 8 * nOther + 8 * nEngine bytes, then
// nBanks x u16 bank offsets in sectors. A bank starts at off * 2048 with
// s16 n and n x s16 sample IDs.
struct RldHwl
{
	const u8 *data;
	size_t size;
	u32 nSpu;
	u32 nBanks;
	u32 nSeq;
	size_t spuAt;
	size_t bankOffAt;
};

static const char *Rld_ReadHwl(const u8 *data, size_t size, struct RldHwl *out)
{
	u32 nOther;
	u32 nEngine;
	u64 bankOffAt;

	memset(out, 0, sizeof(*out));

	if (size < 0x28u)
	{
		return "too short for a KART.HWL header";
	}

	out->data = data;
	out->size = size;
	out->nSpu = Rld_ReadLE32At(data, 0x10);
	nOther = Rld_ReadLE32At(data, 0x14);
	nEngine = Rld_ReadLE32At(data, 0x18);
	out->nBanks = Rld_ReadLE32At(data, 0x1c);
	out->nSeq = Rld_ReadLE32At(data, 0x20);
	out->spuAt = 0x28;

	bankOffAt = 0x28u + (4u * (u64)out->nSpu) + (8u * (u64)nOther) + (8u * (u64)nEngine);

	if ((out->nSpu > 4096u) || (out->nBanks > 1024u) || ((bankOffAt + (2u * (u64)out->nBanks)) > size))
	{
		return "its tables do not fit the file - this is not a retail KART.HWL";
	}

	out->bankOffAt = (size_t)bankOffAt;
	return NULL;
}

static int Rld_HwlSpuSize(const struct RldHwl *hwl, u32 index)
{
	return Rld_ReadLE16At(hwl->data, hwl->spuAt + (4u * (size_t)index) + 2u);
}

// The sample IDs of a retail bank, or 0 if the bank is not in the file.
static int Rld_HwlBankIds(const struct RldHwl *hwl, u32 bank, size_t *idsAt, int *count)
{
	const u64 at = (u64)Rld_ReadLE16At(hwl->data, hwl->bankOffAt + (2u * (size_t)bank)) * RLD_HOWL_SECTOR;
	int n;

	if ((at + 2u) > hwl->size)
	{
		return 0;
	}

	n = (int)(s16)Rld_ReadLE16At(hwl->data, (size_t)at);
	if ((n < 0) || ((at + 2u + (2u * (u64)n)) > hwl->size))
	{
		return 0;
	}

	*idsAt = (size_t)at + 2u;
	*count = n;
	return 1;
}

// The banks every race or the driver select loads itself: 0 and 54..70.
// A custom bank there, or a new
// SPU size for a sample that one of them carries, hits every track.
static int Rld_HwlBankReserved(u32 bank)
{
	return (bank == 0u) || ((bank >= 54u) && (bank <= 70u));
}

// THE SNDB, byte-exact like the earlier Python converter wrote it.
//
// SPU rows only for samples whose size differs from the retail table, in
// the order of the bank header; spuAddr stays 0. Bank and song are padded to whole
// sectors. The caller has Rld_ReadSca and the index check behind it.
static u8 *Rld_BuildSndb(const struct RldSca *sca, const struct RldHwl *hwl, u32 bank, u32 song, size_t *sizeOut, u32 *rowsOut)
{
	const size_t bankPadded = ((sca->bankSize + RLD_HOWL_SECTOR - 1u) / RLD_HOWL_SECTOR) * RLD_HOWL_SECTOR;
	const size_t songPadded = ((sca->cseqSize + RLD_HOWL_SECTOR - 1u) / RLD_HOWL_SECTOR) * RLD_HOWL_SECTOR;
	u32 rows = 0;
	size_t payloadOffset;
	size_t at;
	u8 *out;
	int k;

	for (k = 0; k < sca->samples; k++)
	{
		const u32 index = (u32)Rld_ReadLE16At(sca->bank, 2u + (2u * (size_t)k));

		if (Rld_HwlSpuSize(hwl, index) != Rld_ReadLE16At(sca->sizes, 2u * (size_t)k))
		{
			rows++;
		}
	}

	payloadOffset = RLD_SNDB_HEADER_SIZE + (2u * RLD_SNDB_ENTRY_SIZE) + (rows * RLD_SNDB_SPUFIXUP_SIZE);
	*sizeOut = payloadOffset + bankPadded + songPadded;
	*rowsOut = rows;

	out = (u8 *)calloc(1, *sizeOut);
	if (out == NULL)
	{
		return NULL;
	}

	memcpy(&out[0x00], "SNDB", 4);
	Rld_WriteLE16(&out[0x04], RLD_SNDB_VERSION);
	Rld_WriteLE16(&out[0x06], 2u);
	Rld_WriteLE16(&out[0x08], rows);
	Rld_WriteLE32(&out[0x0c], (u32)payloadOffset);
	Rld_WriteLE16(&out[0x10], bank);
	Rld_WriteLE16(&out[0x12], song);

	at = RLD_SNDB_HEADER_SIZE;
	out[at + 0u] = (u8)RLD_SNDB_KIND_BANK;
	Rld_WriteLE16(&out[at + 2u], bank);
	Rld_WriteLE32(&out[at + 4u], 0u);
	Rld_WriteLE32(&out[at + 8u], (u32)bankPadded);
	at += RLD_SNDB_ENTRY_SIZE;
	out[at + 0u] = (u8)RLD_SNDB_KIND_SONG;
	Rld_WriteLE16(&out[at + 2u], song);
	Rld_WriteLE32(&out[at + 4u], (u32)bankPadded);
	Rld_WriteLE32(&out[at + 8u], (u32)songPadded);
	at += RLD_SNDB_ENTRY_SIZE;

	for (k = 0; k < sca->samples; k++)
	{
		const u32 index = (u32)Rld_ReadLE16At(sca->bank, 2u + (2u * (size_t)k));
		const u32 newSize = (u32)Rld_ReadLE16At(sca->sizes, 2u * (size_t)k);

		if ((u32)Rld_HwlSpuSize(hwl, index) != newSize)
		{
			Rld_WriteLE16(&out[at + 0u], index);
			Rld_WriteLE16(&out[at + 4u], newSize);
			at += RLD_SNDB_SPUFIXUP_SIZE;
		}
	}

	memcpy(&out[payloadOffset], sca->bank, sca->bankSize);
	memcpy(&out[payloadOffset + bankPadded], sca->cseq, sca->cseqSize);
	return out;
}

// The checks before the build: range, forbidden target bank and
// sample indices. Then, for every sample with a new SPU size, which OTHER
// retail banks carry the same sample ID: during the track they get
// the same new size, but carry their old samples. If a reserved one
// is among them, make aborts (reason in why); otherwise the list comes as text in
// others ("5 in [3, 4], 7 in [9]"), samples ascending.
static const char *Rld_MusicCheck(const struct RldSca *sca, const struct RldHwl *hwl, u32 bank, u32 song, char *why, size_t whySize, char *others,
                                  size_t othersSize)
{
	u32 changed[RLD_SCA_BANK_HEAD / 2u];
	int changedCount = 0;
	size_t used = 0;
	int k;

	others[0] = '\0';

	if ((bank >= hwl->nBanks) || (song >= hwl->nSeq))
	{
		snprintf(why, whySize, "bank %u / sequence %u do not exist in the host (%u banks, %u sequences)", bank, song, hwl->nBanks, hwl->nSeq);
		return why;
	}

	if (Rld_HwlBankReserved(bank))
	{
		snprintf(why, whySize, "bank %u is loaded by every race or by the driver select itself (0 and 54..70) - pick another with --bank", bank);
		return why;
	}

	for (k = 0; k < sca->samples; k++)
	{
		const int index = (int)(s16)Rld_ReadLE16At(sca->bank, 2u + (2u * (size_t)k));
		int j;

		if ((index < 0) || ((u32)index >= hwl->nSpu))
		{
			snprintf(why, whySize, "sample %d of the BANK names SPU row %d - KART.HWL has %u", k, index, hwl->nSpu);
			return why;
		}

		if ((u32)Rld_HwlSpuSize(hwl, (u32)index) == (u32)Rld_ReadLE16At(sca->sizes, 2u * (size_t)k))
		{
			continue;
		}

		// Sorted in ascending, duplicates once.
		for (j = changedCount; (j > 0) && (changed[j - 1] > (u32)index); j--)
		{
			changed[j] = changed[j - 1];
		}

		if ((j > 0) && (changed[j - 1] == (u32)index))
		{
			memmove(&changed[j], &changed[j + 1], (size_t)(changedCount - j) * sizeof(changed[0]));
			continue;
		}

		changed[j] = (u32)index;
		changedCount++;
	}

	for (k = 0; k < changedCount; k++)
	{
		int listed = 0;
		u32 b;

		for (b = 0; b < hwl->nBanks; b++)
		{
			size_t idsAt;
			int count;
			int j;

			if ((b == bank) || !Rld_HwlBankIds(hwl, b, &idsAt, &count))
			{
				continue;
			}

			for (j = 0; j < count; j++)
			{
				if ((u32)Rld_ReadLE16At(hwl->data, idsAt + (2u * (size_t)j)) == changed[k])
				{
					break;
				}
			}

			if (j == count)
			{
				continue;
			}

			if (Rld_HwlBankReserved(b))
			{
				snprintf(why, whySize,
				         "sample %u gets a new SPU size, and retail bank %u carries the same sample - bank %u is loaded by every race\n"
				         "         or by the driver select (0 and 54..70), so every track would play it at the wrong size",
				         changed[k], b, b);
				return why;
			}

			if ((used + 24u) < othersSize)
			{
				if (listed)
				{
					used += (size_t)snprintf(&others[used], othersSize - used, ", %u", b);
				}
				else
				{
					used += (size_t)snprintf(&others[used], othersSize - used, "%s%u in [%u", (used != 0u) ? ", " : "", changed[k], b);
				}
			}

			listed = 1;
		}

		if (listed && ((used + 2u) < othersSize))
		{
			others[used++] = ']';
			others[used] = '\0';
		}
	}

	return NULL;
}

// --- track.txt ---------------------------------------------------------------------
//
// Optional, in the track folder. The keys are named like the switches without --.
// UTF-8, a BOM at the start is skipped; a line is `key = value`,
// whitespace around both is dropped; empty lines and lines starting with #
// do not count; the value reaches to the end of the line, a # in it belongs to it.
// Abort on an unknown or duplicate key and on a line
// without = - the rule "unknown switch aborts" applies here just the same. Only
// author may be empty.
enum
{
	RLD_TXT_NAME,
	RLD_TXT_AUTHOR,
	RLD_TXT_MODES,
	RLD_TXT_TRACK_VERSION,
	RLD_TXT_BANK,
	RLD_TXT_SONG,
	RLD_TXT_REVERB,
	RLD_TXT_BOTS,
	RLD_TXT_AMBIENT,
	RLD_TXT_KEYS
};

// reverb, bots and ambient since 4.1: the values of the track (PARM).
static const char *const s_trackTxtKeys[RLD_TXT_KEYS] = {"name", "author", "modes", "track-version", "bank", "song", "reverb", "bots", "ambient"};

#define RLD_TXT_VALUE_MAX 256

struct RldTrackTxt
{
	char value[RLD_TXT_KEYS][RLD_TXT_VALUE_MAX];

	// The line in which the key stood; 0 means: not given.
	int line[RLD_TXT_KEYS];
};

static int Rld_IsBlank(char c)
{
	return (c == ' ') || (c == '\t');
}

static const char *Rld_ParseTrackTxt(const char *text, size_t size, struct RldTrackTxt *out, char *why, size_t whySize)
{
	size_t at = 0;
	int lineNo = 0;

	memset(out, 0, sizeof(*out));

	if ((size >= 3u) && ((u8)text[0] == 0xEFu) && ((u8)text[1] == 0xBBu) && ((u8)text[2] == 0xBFu))
	{
		at = 3;
	}

	while (at < size)
	{
		size_t end = at;
		size_t lineEnd;
		size_t keyEnd;
		size_t valueAt;
		size_t eq;
		int k;

		while ((end < size) && (text[end] != '\n'))
		{
			end++;
		}

		lineNo++;
		lineEnd = end;
		if ((lineEnd > at) && (text[lineEnd - 1u] == '\r'))
		{
			lineEnd--;
		}

		while ((at < lineEnd) && Rld_IsBlank(text[at]))
		{
			at++;
		}

		if ((at == lineEnd) || (text[at] == '#'))
		{
			at = end + 1u;
			continue;
		}

		eq = at;
		while ((eq < lineEnd) && (text[eq] != '='))
		{
			eq++;
		}

		if (eq == lineEnd)
		{
			snprintf(why, whySize, "track.txt line %d has no '=' - write key = value", lineNo);
			return why;
		}

		keyEnd = eq;
		while ((keyEnd > at) && Rld_IsBlank(text[keyEnd - 1u]))
		{
			keyEnd--;
		}

		valueAt = eq + 1u;
		while ((valueAt < lineEnd) && Rld_IsBlank(text[valueAt]))
		{
			valueAt++;
		}

		while ((lineEnd > valueAt) && Rld_IsBlank(text[lineEnd - 1u]))
		{
			lineEnd--;
		}

		for (k = 0; k < RLD_TXT_KEYS; k++)
		{
			if ((strlen(s_trackTxtKeys[k]) == (keyEnd - at)) && (memcmp(s_trackTxtKeys[k], &text[at], keyEnd - at) == 0))
			{
				break;
			}
		}

		if (k == RLD_TXT_KEYS)
		{
			snprintf(why, whySize, "track.txt line %d: unknown key \"%.*s\" - the keys are name, author, modes, track-version, bank, song, reverb, bots, ambient", lineNo,
			         (int)(keyEnd - at), &text[at]);
			return why;
		}

		if (out->line[k] != 0)
		{
			snprintf(why, whySize, "track.txt line %d: \"%s\" appears a second time (first in line %d)", lineNo, s_trackTxtKeys[k], out->line[k]);
			return why;
		}

		if ((valueAt == lineEnd) && (k != RLD_TXT_AUTHOR))
		{
			snprintf(why, whySize, "track.txt line %d: \"%s\" is empty - only author may be left empty", lineNo, s_trackTxtKeys[k]);
			return why;
		}

		if ((lineEnd - valueAt) >= RLD_TXT_VALUE_MAX)
		{
			snprintf(why, whySize, "track.txt line %d: the value of \"%s\" is longer than %d bytes", lineNo, s_trackTxtKeys[k], RLD_TXT_VALUE_MAX - 1);
			return why;
		}

		memcpy(out->value[k], &text[valueAt], lineEnd - valueAt);
		out->value[k][lineEnd - valueAt] = '\0';
		out->line[k] = lineNo;

		at = end + 1u;
	}

	return NULL;
}

// --- Paths --------------------------------------------------------------------------

#define RLD_PATH_MAX 1024

// The separator make joins paths with - the platform's, so that the
// report shows no half Windows paths.
#if defined(_WIN32)
#define RLD_SEP "\\"
#else
#define RLD_SEP "/"
#endif

static int Rld_IsSeparator(char c)
{
	return (c == '/') || (c == '\\');
}

static int Rld_FullPath(const char *path, char *dst, size_t dstSize)
{
#if defined(_WIN32)
	return _fullpath(dst, path, dstSize) != NULL;
#else
	char resolved[PATH_MAX];

	if (realpath(path, resolved) == NULL)
	{
		return 0;
	}

	return snprintf(dst, dstSize, "%s", resolved) < (int)dstSize;
#endif
}

// Leaves a root ("C:\", "/") in place.
static int Rld_IsRoot(const char *path, size_t n)
{
	return (n == 1u) || ((n == 3u) && (path[1] == ':'));
}

static void Rld_TrimSeparators(char *path)
{
	size_t n = strlen(path);

	while ((n > 1u) && Rld_IsSeparator(path[n - 1u]) && !Rld_IsRoot(path, n))
	{
		path[--n] = '\0';
	}
}

static const char *Rld_LastComponent(const char *path)
{
	const char *last = path;

	for (; *path != '\0'; path++)
	{
		if (Rld_IsSeparator(*path))
		{
			last = path + 1;
		}
	}

	return last;
}

// The folder above path, or 0 if there is none.
static int Rld_ParentDir(const char *path, char *dst, size_t dstSize)
{
	const char *last = Rld_LastComponent(path);
	size_t n = (size_t)(last - path);

	if ((n == 0u) || (last[0] == '\0'))
	{
		return 0;
	}

	while ((n > 1u) && Rld_IsSeparator(path[n - 1u]) && !Rld_IsRoot(path, n))
	{
		n--;
	}

	if (n >= dstSize)
	{
		return 0;
	}

	memcpy(dst, path, n);
	dst[n] = '\0';
	return 1;
}

static int Rld_FileExists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (file == NULL)
	{
		return 0;
	}

	fclose(file);
	return 1;
}

static int Rld_EndsWithNoCase(const char *name, const char *suffix)
{
	const size_t n = strlen(name);
	const size_t m = strlen(suffix);
	size_t i;

	if (n < m)
	{
		return 0;
	}

	for (i = 0; i < m; i++)
	{
		char a = name[n - m + i];
		char b = suffix[i];

		if ((a >= 'A') && (a <= 'Z'))
		{
			a = (char)(a - 'A' + 'a');
		}

		if ((b >= 'A') && (b <= 'Z'))
		{
			b = (char)(b - 'A' + 'a');
		}

		if (a != b)
		{
			return 0;
		}
	}

	return 1;
}

// The folder the running exe lies in (rldpack.exe, or ReloadStudio.exe with
// --rldpack) - not the one it was called from.
static int Rld_ExeDir(const char *argv0, char *dst, size_t dstSize)
{
	char self[RLD_PATH_MAX];
	char full[RLD_PATH_MAX];

#if defined(_MSC_VER)
	char *program = NULL;

	if ((_get_pgmptr(&program) != 0) || (program == NULL) || (program[0] == '\0'))
	{
		program = (char *)argv0;
	}

	snprintf(self, sizeof(self), "%s", program);
#elif defined(_WIN32)
	snprintf(self, sizeof(self), "%s", argv0);
#else
	{
		const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1u);

		if (n <= 0)
		{
			snprintf(self, sizeof(self), "%s", argv0);
		}
		else
		{
			self[n] = '\0';
		}
	}
#endif

	if (!Rld_FullPath(self, full, sizeof(full)))
	{
		return 0;
	}

	return Rld_ParentDir(full, dst, dstSize);
}

// THE RETAIL KART.HWL - searched the way the game searches its data
// (NativeAssets_Init): the folder of the running exe, its parent and
// grandparent folder. One counts if assets\ in it carries BIGFILE.BIG or ctr-u.bin.
// There, first a loose SOUNDS\KART.HWL, otherwise the same file from
// ctr-u.bin, read with the game's image reader. An author usually has only
// the image - the second path is there for that.
static u8 *Rld_FindHwl(const char *argv0, size_t *sizeOut, char *source, size_t sourceSize, char *why, size_t whySize)
{
	char dir[RLD_PATH_MAX];
	int level;

	*sizeOut = 0;

	if (!Rld_ExeDir(argv0, dir, sizeof(dir)))
	{
		snprintf(why, whySize, "cannot tell where this program lies - give KART.HWL with --hwl <file>, or leave the music out with --no-music");
		return NULL;
	}

	for (level = 0; level < 3; level++)
	{
		char bigfile[RLD_PATH_MAX + 32];
		char image[RLD_PATH_MAX + 32];
		char loose[RLD_PATH_MAX + 32];
		char parent[RLD_PATH_MAX];

		snprintf(bigfile, sizeof(bigfile), "%s" RLD_SEP "assets" RLD_SEP "BIGFILE.BIG", dir);
		snprintf(image, sizeof(image), "%s" RLD_SEP "assets" RLD_SEP "ctr-u.bin", dir);
		snprintf(loose, sizeof(loose), "%s" RLD_SEP "assets" RLD_SEP "SOUNDS" RLD_SEP "KART.HWL", dir);

		if (Rld_FileExists(bigfile) || Rld_FileExists(image))
		{
			u8 *data = NULL;
			size_t size = 0;

			if (Rld_ReadFile(loose, &data, &size))
			{
				snprintf(source, sourceSize, "%s", loose);
				*sizeOut = size;
				return data;
			}

			// Refused for its size and said so: no quiet second source.
			if (s_rldReadTooLarge)
			{
				snprintf(why, whySize, "%s is too large (at most 512 MiB)", loose);
				return NULL;
			}

			if (Rld_FileExists(image))
			{
				int imageSize = 0;

				if (!NativeDiscImage_OpenImagePath(image) || !NativeDiscImage_ReadFileBytes("SOUNDS/KART.HWL", 0, &data, &imageSize))
				{
					snprintf(why, whySize, "%s: SOUNDS/KART.HWL cannot be read out of the image", image);
					return NULL;
				}

				snprintf(source, sourceSize, "%s (SOUNDS/KART.HWL)", image);
				*sizeOut = (size_t)imageSize;
				return data;
			}

			snprintf(why, whySize, "%s" RLD_SEP "assets has BIGFILE.BIG but no SOUNDS/KART.HWL and no ctr-u.bin - give KART.HWL with --hwl <file>", dir);
			return NULL;
		}

		if (!Rld_ParentDir(dir, parent, sizeof(parent)))
		{
			break;
		}

		snprintf(dir, sizeof(dir), "%s", parent);
	}

	snprintf(why, whySize,
	         "no assets folder with BIGFILE.BIG or ctr-u.bin next to this program or up to two folders above it, the way the game\n"
	         "         looks for its data - give KART.HWL with --hwl <file>, or leave the music out with --no-music");
	return NULL;
}

// --- The folder ---------------------------------------------------------------------
//
// Only the top level, extensions without regard to upper and lower case.
// Exactly one .lev and one .vrm, at most one music file (.sca or .sndb), plus
// optionally track.txt. Everything else is not used, and the report says so. The Saphi names
// carry UUIDs (309fa756-..._v1.0.2.lev) - the search goes by the extension.
#define RLD_FOLDER_KEEP 8
#define RLD_NAME_MAX    260

struct RldFolderList
{
	char names[RLD_FOLDER_KEEP][RLD_NAME_MAX];
	int count;
};

struct RldFolder
{
	struct RldFolderList lev;
	struct RldFolderList vrm;
	struct RldFolderList sca;
	struct RldFolderList sndb;
	int hasTrackTxt;
	char trackTxt[RLD_NAME_MAX];
	char ignored[512];
};

static void Rld_FolderAdd(struct RldFolderList *list, const char *name)
{
	if (list->count < RLD_FOLDER_KEEP)
	{
		snprintf(list->names[list->count], sizeof(list->names[0]), "%s", name);
	}

	list->count++;
}

static void Rld_FolderSort(struct RldFolder *folder, const char *name)
{
	if (Rld_EndsWithNoCase(name, ".lev") && (strlen(name) > 4u))
	{
		Rld_FolderAdd(&folder->lev, name);
	}
	else if (Rld_EndsWithNoCase(name, ".vrm") && (strlen(name) > 4u))
	{
		Rld_FolderAdd(&folder->vrm, name);
	}
	else if (Rld_EndsWithNoCase(name, ".sca") && (strlen(name) > 4u))
	{
		Rld_FolderAdd(&folder->sca, name);
	}
	else if (Rld_EndsWithNoCase(name, ".sndb") && (strlen(name) > 5u))
	{
		Rld_FolderAdd(&folder->sndb, name);
	}
	else if ((strlen(name) == 9u) && Rld_EndsWithNoCase(name, "track.txt"))
	{
		folder->hasTrackTxt = 1;
		snprintf(folder->trackTxt, sizeof(folder->trackTxt), "%s", name);
	}
	else
	{
		const size_t used = strlen(folder->ignored);

		if ((used + strlen(name) + 8u) < sizeof(folder->ignored))
		{
			snprintf(&folder->ignored[used], sizeof(folder->ignored) - used, "%s%s", (used != 0u) ? ", " : "", name);
		}
		else if (((used + 6u) < sizeof(folder->ignored)) && (strstr(folder->ignored, ", ...") == NULL))
		{
			snprintf(&folder->ignored[used], sizeof(folder->ignored) - used, ", ...");
		}
	}
}

// Every file at the top level of a folder (no subfolders), in the order the
// system lists them; the callers sort or classify. 0 = the folder cannot be
// opened. make, and make-char --voices (which packs the folder as CVOI).
typedef void (*RldFileVisit)(void *context, const char *name);

static int Rld_EachFile(const char *path, RldFileVisit visit, void *context)
{
#if defined(_WIN32)
	{
		char pattern[RLD_PATH_MAX + 4];
		struct _finddata_t found;
		intptr_t handle;

		snprintf(pattern, sizeof(pattern), "%s" RLD_SEP "*", path);
		handle = _findfirst(pattern, &found);
		if (handle == -1)
		{
			return 0;
		}

		do
		{
			if ((found.attrib & _A_SUBDIR) == 0)
			{
				visit(context, found.name);
			}
		} while (_findnext(handle, &found) == 0);

		_findclose(handle);
	}
#else
	{
		DIR *dir = opendir(path);
		struct dirent *entry;

		if (dir == NULL)
		{
			return 0;
		}

		while ((entry = readdir(dir)) != NULL)
		{
			char full[RLD_PATH_MAX + RLD_NAME_MAX];
			struct stat info;

			snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
			if ((stat(full, &info) == 0) && S_ISREG(info.st_mode))
			{
				visit(context, entry->d_name);
			}
		}

		closedir(dir);
	}
#endif

	return 1;
}

static void Rld_FolderVisit(void *context, const char *name)
{
	Rld_FolderSort((struct RldFolder *)context, name);
}

static int Rld_ReadFolder(const char *path, struct RldFolder *folder)
{
	memset(folder, 0, sizeof(*folder));
	return Rld_EachFile(path, Rld_FolderVisit, folder);
}

static void Rld_PrintFolderList(const struct RldFolderList *list)
{
	int i;

	for (i = 0; (i < list->count) && (i < RLD_FOLDER_KEEP); i++)
	{
		fprintf(stderr, "           %s\n", list->names[i]);
	}

	if (list->count > RLD_FOLDER_KEEP)
	{
		fprintf(stderr, "           ... and %d more\n", list->count - RLD_FOLDER_KEEP);
	}
}

static int Rld_ParseSmallNumber(const char *text, u32 *out)
{
	char *end = NULL;
	unsigned long value;

	if ((text[0] < '0') || (text[0] > '9'))
	{
		return 0;
	}

	value = strtoul(text, &end, 10);
	if ((*end != '\0') || (value > 0xffffuL))
	{
		return 0;
	}

	*out = (u32)value;
	return 1;
}

// @file for Reload Studio: kind, state, name and size of a file in the folder.
static void Rld_EmitFile(const char *kind, const char *state, const char *folderPath, const char *name)
{
	char path[RLD_PATH_MAX + RLD_NAME_MAX];
	char bytes[24] = "0";

	if (!s_machine)
	{
		return;
	}

	if ((name != NULL) && (name[0] != '\0'))
	{
		FILE *file;

		snprintf(path, sizeof(path), "%s" RLD_SEP "%s", folderPath, name);
		file = fopen(path, "rb");
		if (file != NULL)
		{
			if (fseek(file, 0, SEEK_END) == 0)
			{
				const long size = ftell(file);

				if (size >= 0)
				{
					snprintf(bytes, sizeof(bytes), "%ld", size);
				}
			}
			fclose(file);
		}
	}

	Rld_Emit("file", kind, state, (name != NULL) ? name : "", bytes, (const char *)NULL);
}

static void Rld_EmitFolderList(const char *kind, const struct RldFolderList *list, const char *folderPath, int unused)
{
	int i;

	if (list->count == 0)
	{
		Rld_EmitFile(kind, "missing", folderPath, NULL);
		return;
	}

	for (i = 0; (i < list->count) && (i < RLD_FOLDER_KEEP); i++)
	{
		Rld_EmitFile(kind, (list->count > 1) ? "extra" : (unused ? "unused" : "ok"), folderPath, list->names[i]);
	}
}

// What a folder has too much or too little of, for authors.
static void Rld_SayFolderCount(const char *code, const char *what, const char *ending, int count, int atMost)
{
	if (count == 0)
	{
		Rld_Say("error", code, NULL, "The folder has no %s (%s file). Put exactly one into it.", what, ending);
	}
	else if (atMost)
	{
		Rld_Say("error", code, NULL, "The folder has %d %s files (%s). Keep one, or none for no music.", count, what, ending);
	}
	else
	{
		Rld_Say("error", code, NULL, "The folder has %d %s files (%s). Keep exactly one.", count, what, ending);
	}
}

// --- The command ---------------------------------------------------------------------

static int Cmd_Make(int argc, char *argv[], const char *argv0)
{
	static const char *const sources[3] = {"switch", "track.txt", "default"};
	static char folderPath[RLD_PATH_MAX];
	static char levPath[RLD_PATH_MAX + RLD_NAME_MAX];
	static char vrmPath[RLD_PATH_MAX + RLD_NAME_MAX];
	static char outPath[RLD_PATH_MAX + 16];
	static struct RldFolder folder;
	static struct RldTrackTxt txt;
	const char *folderArg = NULL;
	const char *outArg = NULL;
	const char *hwlArg = NULL;
	const char *switchValue[RLD_TXT_KEYS] = {NULL};
	const char *value[RLD_TXT_KEYS];
	const char *source[RLD_TXT_KEYS];
	int noMusic = 0;
	int keepModelIds = 0;
	int abrKeep = 0;
	int check = 0;
	int musicFailed = 0;
	int result;
	u32 bank = 14u;
	u32 song = 13u;
	u8 *sndb = NULL;
	size_t sndbSize = 0;
	struct RldPackJob job;
	char why[768];
	int i;

	for (i = 0; i < argc; i++)
	{
		const char *arg = argv[i];
		const char *next = ((i + 1) < argc) ? argv[i + 1] : NULL;

		if ((strcmp(arg, "--name") == 0) && next) { switchValue[RLD_TXT_NAME] = argv[++i]; }
		else if ((strcmp(arg, "--author") == 0) && next) { switchValue[RLD_TXT_AUTHOR] = argv[++i]; }
		else if ((strcmp(arg, "--modes") == 0) && next) { switchValue[RLD_TXT_MODES] = argv[++i]; }
		else if ((strcmp(arg, "--track-version") == 0) && next) { switchValue[RLD_TXT_TRACK_VERSION] = argv[++i]; }
		else if ((strcmp(arg, "--bank") == 0) && next) { switchValue[RLD_TXT_BANK] = argv[++i]; }
		else if ((strcmp(arg, "--song") == 0) && next) { switchValue[RLD_TXT_SONG] = argv[++i]; }
		else if ((strcmp(arg, "--reverb") == 0) && next) { switchValue[RLD_TXT_REVERB] = argv[++i]; }
		else if ((strcmp(arg, "--bots") == 0) && next) { switchValue[RLD_TXT_BOTS] = argv[++i]; }
		else if ((strcmp(arg, "--ambient") == 0) && next) { switchValue[RLD_TXT_AMBIENT] = argv[++i]; }
		else if ((strcmp(arg, "--out") == 0) && next) { outArg = argv[++i]; }
		else if ((strcmp(arg, "--hwl") == 0) && next) { hwlArg = argv[++i]; }
		else if (strcmp(arg, "--no-music") == 0) { noMusic = 1; }
		else if (strcmp(arg, "--keep-model-ids") == 0) { keepModelIds = 1; }
		else if (strcmp(arg, "--abr-keep") == 0) { abrKeep = 1; }
		else if (strcmp(arg, "--check") == 0) { check = 1; }
		else if (strcmp(arg, "--sndb") == 0)
		{
			fprintf(stderr, "rldpack: make takes its music from the folder: a .sca, or a finished .sndb\n");
			fprintf(stderr, "         file. Put the .sndb into the track folder.\n");
			Rld_Say("error", "usage", "--sndb", "make takes the music from the track folder. Put the .sndb file into the folder.");
			return 2;
		}
		else if ((arg[0] != '-') && (folderArg == NULL))
		{
			folderArg = arg;
		}
		else
		{
			fprintf(stderr, "rldpack: unknown argument \"%s\"\n\n", arg);
			Rld_Say("error", "usage", arg, "rldpack does not know the argument \"%s\".", arg);
			Rld_Usage();
			return 2;
		}
	}

	if (folderArg == NULL)
	{
		fprintf(stderr, "rldpack: make needs the track folder.\n\n");
		Rld_Say("error", "usage", NULL, "No track folder was given.");
		Rld_Usage();
		return 2;
	}

	if (!Rld_FullPath(folderArg, folderPath, sizeof(folderPath)))
	{
		fprintf(stderr, "rldpack: cannot open the folder %s\n", folderArg);
		Rld_Say("error", "folder-open", folderArg, "The folder cannot be opened.");
		return 1;
	}

	Rld_TrimSeparators(folderPath);

	if (!Rld_ReadFolder(folderPath, &folder))
	{
		fprintf(stderr, "rldpack: cannot open the folder %s\n", folderArg);
		Rld_Say("error", "folder-open", folderArg, "The folder cannot be opened.");
		return 1;
	}

	Rld_EmitFolderList("lev", &folder.lev, folderPath, 0);
	Rld_EmitFolderList("vrm", &folder.vrm, folderPath, 0);
	// Music from a .sca OR a finished .sndb.
	// The line @file sndb only comes if a .sndb lies in the folder; so
	// an older Reload Studio still sees exactly one music line.
	if ((folder.sca.count > 0) || (folder.sndb.count == 0))
	{
		Rld_EmitFolderList("sca", &folder.sca, folderPath, noMusic);
	}
	if (folder.sndb.count > 0)
	{
		Rld_EmitFolderList("sndb", &folder.sndb, folderPath, noMusic);
	}
	Rld_EmitFile("tracktxt", folder.hasTrackTxt ? "ok" : "missing", folderPath, folder.hasTrackTxt ? folder.trackTxt : NULL);

	if ((folder.lev.count != 1) || (folder.vrm.count != 1) || ((folder.sca.count + folder.sndb.count) > 1))
	{
		if (folder.lev.count != 1)
		{
			fprintf(stderr, "rldpack: %s: make needs exactly one .lev, found %d\n", folderPath, folder.lev.count);
			Rld_PrintFolderList(&folder.lev);
			Rld_SayFolderCount("folder-lev", "track geometry", ".lev", folder.lev.count, 0);
		}

		if (folder.vrm.count != 1)
		{
			fprintf(stderr, "rldpack: %s: make needs exactly one .vrm, found %d\n", folderPath, folder.vrm.count);
			Rld_PrintFolderList(&folder.vrm);
			Rld_SayFolderCount("folder-vrm", "texture", ".vrm", folder.vrm.count, 0);
		}

		if ((folder.sca.count > 1) && (folder.sndb.count == 0))
		{
			fprintf(stderr, "rldpack: %s: make takes at most one .sca, found %d\n", folderPath, folder.sca.count);
			Rld_PrintFolderList(&folder.sca);
			Rld_SayFolderCount("folder-sca", "music", ".sca", folder.sca.count, 1);
		}
		else if ((folder.sca.count + folder.sndb.count) > 1)
		{
			fprintf(stderr, "rldpack: %s: make takes at most one music file (.sca or .sndb), found %d\n", folderPath, folder.sca.count + folder.sndb.count);
			Rld_PrintFolderList(&folder.sca);
			Rld_PrintFolderList(&folder.sndb);
			Rld_Say("error", "folder-music", NULL, "The folder has %d music files (%d .sca, %d .sndb). Keep one, or none for no music.",
			        folder.sca.count + folder.sndb.count, folder.sca.count, folder.sndb.count);
		}

		return 1;
	}

	snprintf(levPath, sizeof(levPath), "%s" RLD_SEP "%s", folderPath, folder.lev.names[0]);
	snprintf(vrmPath, sizeof(vrmPath), "%s" RLD_SEP "%s", folderPath, folder.vrm.names[0]);

	memset(&txt, 0, sizeof(txt));

	if (folder.hasTrackTxt)
	{
		char txtPath[RLD_PATH_MAX + RLD_NAME_MAX];
		u8 *text = NULL;
		size_t textSize = 0;

		snprintf(txtPath, sizeof(txtPath), "%s" RLD_SEP "%s", folderPath, folder.trackTxt);

		if (!Rld_ReadFile(txtPath, &text, &textSize))
		{
			if (!s_rldReadTooLarge) // said already, with its reason
			{
				fprintf(stderr, "rldpack: cannot read %s\n", txtPath);
				Rld_Say("error", "tracktxt-read", txtPath, "track.txt cannot be read.");
			}
			return 1;
		}

		if (Rld_ParseTrackTxt((const char *)text, textSize, &txt, why, sizeof(why)) != NULL)
		{
			fprintf(stderr, "rldpack: %s\n", why);
			Rld_Say("error", "tracktxt", why, "track.txt has a mistake: %s.", why);
			return 1;
		}

		free(text);
	}

	// SWITCH BEFORE track.txt BEFORE DEFAULT. The default for the
	// name is the folder name, as it stands in the file system.
	for (i = 0; i < RLD_TXT_KEYS; i++)
	{
		// "default" for reverb, bots and ambient: do not set, even if
		// track.txt has a value. That way a GUI can choose "game default"
		// without touching track.txt.
		if ((switchValue[i] != NULL) && ((i == RLD_TXT_REVERB) || (i == RLD_TXT_BOTS) || (i == RLD_TXT_AMBIENT)) &&
		    (strcmp(switchValue[i], "default") == 0))
		{
			value[i] = NULL;
			source[i] = sources[0];
		}
		else if (switchValue[i] != NULL)
		{
			value[i] = switchValue[i];
			source[i] = sources[0];
		}
		else if (txt.line[i] != 0)
		{
			value[i] = txt.value[i];
			source[i] = sources[1];
		}
		else
		{
			value[i] = NULL;
			source[i] = sources[2];
		}
	}

	if (value[RLD_TXT_NAME] == NULL)
	{
		value[RLD_TXT_NAME] = Rld_LastComponent(folderPath);
		source[RLD_TXT_NAME] = "folder name";
	}

	if (value[RLD_TXT_AUTHOR] == NULL)
	{
		value[RLD_TXT_AUTHOR] = "";
	}

	// Name and author here and not only in Rld_Build: there the message would say
	// "--name", and with a folder name it helps to know WHERE it came from.
	if ((value[RLD_TXT_NAME][0] == '\0') || (strlen(value[RLD_TXT_NAME]) > s_metaStringLimits[0]) || !Rld_IsUtf8(value[RLD_TXT_NAME]))
	{
		fprintf(stderr, "rldpack: the name \"%s\" (%s) is empty, longer than %u bytes or not UTF-8.\n", value[RLD_TXT_NAME], source[RLD_TXT_NAME],
		        (unsigned int)s_metaStringLimits[0]);
		fprintf(stderr, "         Give it with --name \"...\" or as name = ... in track.txt.\n");
		Rld_Say("error", "name", value[RLD_TXT_NAME], "The track name is empty, longer than 64 bytes, or not valid text.");
		return 1;
	}

	if ((strlen(value[RLD_TXT_AUTHOR]) > s_metaStringLimits[1]) || !Rld_IsUtf8(value[RLD_TXT_AUTHOR]))
	{
		fprintf(stderr, "rldpack: the author (%s) is longer than %u bytes or not UTF-8\n", source[RLD_TXT_AUTHOR], (unsigned int)s_metaStringLimits[1]);
		Rld_Say("error", "author", value[RLD_TXT_AUTHOR], "The author name is longer than 64 bytes, or not valid text.");
		return 1;
	}

	if ((value[RLD_TXT_MODES] != NULL) && (Rld_ParseModes(value[RLD_TXT_MODES]) == 0u))
	{
		fprintf(stderr, "rldpack: modes \"%s\" (%s): a comma separated list of race, ctr, time, crystal (battle is reserved)\n", value[RLD_TXT_MODES],
		        source[RLD_TXT_MODES]);
		Rld_Say("error", "modes", value[RLD_TXT_MODES], "The list of modes (from %s) is not valid. The modes are race, ctr, time and crystal.", source[RLD_TXT_MODES]);
		return 1;
	}

	// The track values already here, so that the message says WHERE a
	// wrong value came from. Rld_Pack parses them once more with the same function.
	{
		struct RldParm probe;
		const char *parmWhy = Rld_ParseParmValues(value[RLD_TXT_REVERB], value[RLD_TXT_BOTS], value[RLD_TXT_AMBIENT], &probe);

		if (parmWhy != NULL)
		{
			fprintf(stderr, "rldpack: %s\n", parmWhy);
			fprintf(stderr, "         (reverb %s, bots %s, ambient %s)\n", source[RLD_TXT_REVERB], source[RLD_TXT_BOTS], source[RLD_TXT_AMBIENT]);
			if (strncmp(parmWhy, "--reverb", 8) == 0)
			{
				Rld_Say("error", "reverb", parmWhy, "The reverb \"%s\" (from %s) is not valid. Use a step from 0 to 4, or off.", value[RLD_TXT_REVERB], source[RLD_TXT_REVERB]);
			}
			else if (strncmp(parmWhy, "--bots", 6) == 0)
			{
				Rld_Say("error", "bots", parmWhy, "The bot strength \"%s\" (from %s) is not valid. Use a retail track row from 0 to 17.", value[RLD_TXT_BOTS], source[RLD_TXT_BOTS]);
			}
			else
			{
				Rld_Say("error", "ambient", parmWhy, "The ambient sound \"%s\" (from %s) is not valid. Use one or two sound numbers, for example 0x83.",
						value[RLD_TXT_AMBIENT], source[RLD_TXT_AMBIENT]);
			}
			return 1;
		}
	}

	if (value[RLD_TXT_TRACK_VERSION] != NULL)
	{
		const char *text = value[RLD_TXT_TRACK_VERSION];
		char *end = NULL;
		const unsigned long version = strtoul(text, &end, 10);

		if ((text[0] < '0') || (text[0] > '9') || (*end != '\0') || (version < 1uL) || (version > 0xffffffffuL))
		{
			fprintf(stderr, "rldpack: track-version \"%s\" (%s): a whole number, 1 or higher\n", text, source[RLD_TXT_TRACK_VERSION]);
			Rld_Say("error", "track-version", text, "The version \"%s\" (from %s) must be a whole number, 1 or higher.", text, source[RLD_TXT_TRACK_VERSION]);
			return 1;
		}
	}

	if ((value[RLD_TXT_BANK] != NULL) && !Rld_ParseSmallNumber(value[RLD_TXT_BANK], &bank))
	{
		fprintf(stderr, "rldpack: bank \"%s\" (%s): a whole number\n", value[RLD_TXT_BANK], source[RLD_TXT_BANK]);
		Rld_Say("error", "bank", value[RLD_TXT_BANK], "The music bank \"%s\" (from %s) must be a whole number.", value[RLD_TXT_BANK], source[RLD_TXT_BANK]);
		return 1;
	}

	if ((value[RLD_TXT_SONG] != NULL) && !Rld_ParseSmallNumber(value[RLD_TXT_SONG], &song))
	{
		fprintf(stderr, "rldpack: song \"%s\" (%s): a whole number\n", value[RLD_TXT_SONG], source[RLD_TXT_SONG]);
		Rld_Say("error", "song", value[RLD_TXT_SONG], "The music sequence \"%s\" (from %s) must be a whole number.", value[RLD_TXT_SONG], source[RLD_TXT_SONG]);
		return 1;
	}

	if (outArg != NULL)
	{
		snprintf(outPath, sizeof(outPath), "%s", outArg);
	}
	else
	{
		// Next to the folder, not into it. make installs nothing - the author
		// puts the file into tracks\ themselves.
		snprintf(outPath, sizeof(outPath), "%s.rldtrack", folderPath);
	}

	Rld_Emit("value", "name", value[RLD_TXT_NAME], (source[RLD_TXT_NAME][0] == 'f') ? "folder" : source[RLD_TXT_NAME], (const char *)NULL);
	Rld_Emit("value", "author", value[RLD_TXT_AUTHOR], source[RLD_TXT_AUTHOR], (const char *)NULL);
	Rld_Emit("value", "track_version", (value[RLD_TXT_TRACK_VERSION] != NULL) ? value[RLD_TXT_TRACK_VERSION] : "1", source[RLD_TXT_TRACK_VERSION], (const char *)NULL);
	Rld_Emit("value", "modes", (value[RLD_TXT_MODES] != NULL) ? value[RLD_TXT_MODES] : "race", source[RLD_TXT_MODES], (const char *)NULL);
	Rld_Emit("value", "reverb", (value[RLD_TXT_REVERB] != NULL) ? value[RLD_TXT_REVERB] : "", source[RLD_TXT_REVERB], (const char *)NULL);
	Rld_Emit("value", "bots", (value[RLD_TXT_BOTS] != NULL) ? value[RLD_TXT_BOTS] : "", source[RLD_TXT_BOTS], (const char *)NULL);
	Rld_Emit("value", "ambient", (value[RLD_TXT_AMBIENT] != NULL) ? value[RLD_TXT_AMBIENT] : "", source[RLD_TXT_AMBIENT], (const char *)NULL);
	Rld_Emit("value", "music", ((folder.sca.count + folder.sndb.count) == 0) ? "none" : (noMusic ? "off" : "on"), noMusic ? "switch" : "default", (const char *)NULL);
	Rld_Emit("value", "out", outPath, (outArg != NULL) ? "switch" : "default", (const char *)NULL);

	printf("rldpack make %s\n", folderPath);
	printf("  folder        lev %s\n", folder.lev.names[0]);
	printf("                vrm %s\n", folder.vrm.names[0]);
	if (folder.sca.count == 1)
	{
		printf("                sca %s\n", folder.sca.names[0]);
	}
	if (folder.sndb.count == 1)
	{
		printf("                sndb %s\n", folder.sndb.names[0]);
	}
	printf("                track.txt %s\n", folder.hasTrackTxt ? "yes" : "no");
	if (folder.ignored[0] != '\0')
	{
		printf("  ignored       %s\n", folder.ignored);
	}
	printf("  name          \"%s\" (%s)\n", value[RLD_TXT_NAME], source[RLD_TXT_NAME]);
	printf("  author        \"%s\" (%s)\n", value[RLD_TXT_AUTHOR], source[RLD_TXT_AUTHOR]);
	printf("  modes         %s (%s)\n", (value[RLD_TXT_MODES] != NULL) ? value[RLD_TXT_MODES] : "race", source[RLD_TXT_MODES]);
	printf("  track version %s (%s)\n", (value[RLD_TXT_TRACK_VERSION] != NULL) ? value[RLD_TXT_TRACK_VERSION] : "1", source[RLD_TXT_TRACK_VERSION]);
	printf("  reverb        %s (%s)\n", (value[RLD_TXT_REVERB] != NULL) ? value[RLD_TXT_REVERB] : "not set, the game uses 2", source[RLD_TXT_REVERB]);
	printf("  bots          %s (%s)\n", (value[RLD_TXT_BOTS] != NULL) ? value[RLD_TXT_BOTS] : "not set, the game uses row 0", source[RLD_TXT_BOTS]);
	printf("  ambient       %s (%s)\n", (value[RLD_TXT_AMBIENT] != NULL) ? value[RLD_TXT_AMBIENT] : "not set, no ambient sound", source[RLD_TXT_AMBIENT]);

	// THE MUSIC. Only with a .sca or .sndb and without --no-music; otherwise the
	// container gets no SNDB, and the track plays the music of its slot.
	//
	// With --check an error here does not end the run: make keeps checking without music,
	// so that Reload Studio can still show every mode, and
	// ends with 1 at the end.
#define RLD_MUSIC_FAILED()                                                      \
	if (!check)                                                                \
	{                                                                          \
		return 1;                                                              \
	}                                                                          \
	musicFailed = 1;                                                           \
	goto musicDone

	// A FINISHED SNDB goes into the container unchanged. It is checked
	// here with the same function the game runs when loading
	// (Rld_ParseSndb) - in advance, so that --check keeps running on an error as
	// with the .sca. This path does not need KART.HWL.
	if ((folder.sndb.count == 1) && !noMusic)
	{
		char sndbPath[RLD_PATH_MAX + RLD_NAME_MAX];
		struct RldSndb probe;
		const char *problem;

		snprintf(sndbPath, sizeof(sndbPath), "%s" RLD_SEP "%s", folderPath, folder.sndb.names[0]);

		if (!Rld_ReadFile(sndbPath, &sndb, &sndbSize))
		{
			if (!s_rldReadTooLarge) // said already, with its reason
			{
				fprintf(stderr, "rldpack: cannot read %s\n", sndbPath);
				Rld_Say("error", "sndb-read", sndbPath, "The music file %s cannot be read.", folder.sndb.names[0]);
			}
			RLD_MUSIC_FAILED();
		}

		if (sndbSize > RLD_LIMIT_SNDB)
		{
			fprintf(stderr, "rldpack: %s is %llu bytes - the format allows 4 MB at most\n", folder.sndb.names[0], (unsigned long long)sndbSize);
			Rld_Say("error", "sndb-size", NULL, "The music file %s is %llu bytes. A container allows 4 MB at most.", folder.sndb.names[0],
			        (unsigned long long)sndbSize);
			RLD_MUSIC_FAILED();
		}

		problem = Rld_ParseSndb(&probe, sndb, sndbSize);
		if (problem != NULL)
		{
			fprintf(stderr, "rldpack: %s: %s\n", folder.sndb.names[0], problem);
			Rld_Say("error", "sndb-bad", problem, "The music file %s cannot be used: %s.", folder.sndb.names[0], problem);
			RLD_MUSIC_FAILED();
		}

		printf("  music         taken as it is from %s (%llu bytes)\n", folder.sndb.names[0], (unsigned long long)sndbSize);
		Rld_Emit("music", "ok", "The track plays its own music from the finished sound file (.sndb).", (const char *)NULL);
	}
	else if ((folder.sca.count == 1) && !noMusic)
	{
		char scaPath[RLD_PATH_MAX + RLD_NAME_MAX];
		char hwlSource[RLD_PATH_MAX + 64];
		char others[512];
		u8 *scaData = NULL;
		size_t scaSize = 0;
		u8 *hwlData = NULL;
		size_t hwlSize = 0;
		struct RldSca sca;
		struct RldHwl hwl;
		u32 rows = 0;
		const char *problem;

		snprintf(scaPath, sizeof(scaPath), "%s" RLD_SEP "%s", folderPath, folder.sca.names[0]);

		if (!Rld_ReadFile(scaPath, &scaData, &scaSize))
		{
			if (!s_rldReadTooLarge) // said already, with its reason
			{
				fprintf(stderr, "rldpack: cannot read %s\n", scaPath);
				Rld_Say("error", "sca-read", scaPath, "The music file (.sca) cannot be read.");
			}
			RLD_MUSIC_FAILED();
		}

		problem = Rld_ReadSca(scaData, scaSize, &sca);
		if (problem != NULL)
		{
			fprintf(stderr, "rldpack: %s: %s\n", folder.sca.names[0], problem);
			Rld_Say("error", "sca-bad", problem, "The music file %s cannot be used: %s.", folder.sca.names[0], problem);
			RLD_MUSIC_FAILED();
		}

		if ((sca.bankPad != 0u) || (sca.cseqPad != 0u))
		{
			printf("  sca padding   BANK +%llu, CSEQ +%llu bytes up to whole sectors, left out\n", (unsigned long long)sca.bankPad,
			       (unsigned long long)sca.cseqPad);
		}

		{
			char metaName[80];
			char metaAuthor[80];

			if ((sca.meta != NULL) && Rld_ScaMetaString(sca.meta, sca.metaSize, "name", metaName, sizeof(metaName)) &&
			    Rld_ScaMetaString(sca.meta, sca.metaSize, "author", metaAuthor, sizeof(metaAuthor)))
			{
				printf("  sca meta      name \"%s\", author \"%s\" (not used)\n", metaName, metaAuthor);
			}
		}

		if (hwlArg != NULL)
		{
			if (!Rld_ReadFile(hwlArg, &hwlData, &hwlSize))
			{
				if (!s_rldReadTooLarge) // said already, with its reason
				{
					fprintf(stderr, "rldpack: cannot read %s\n", hwlArg);
					Rld_Say("error", "hwl-read", hwlArg, "The game's sound table (KART.HWL) cannot be read.");
				}
				RLD_MUSIC_FAILED();
			}

			snprintf(hwlSource, sizeof(hwlSource), "%s (--hwl)", hwlArg);
		}
		else
		{
			hwlData = Rld_FindHwl(argv0, &hwlSize, hwlSource, sizeof(hwlSource), why, sizeof(why));

			if (hwlData == NULL)
			{
				fprintf(stderr, "rldpack: the music needs the retail KART.HWL: %s\n", why);
				Rld_Say("error", "hwl-missing", why,
						"The music needs the game's sound table, and the game's assets were not found. Put the tool next to the game, or turn the music off.");
				RLD_MUSIC_FAILED();
			}
		}

		problem = Rld_ReadHwl(hwlData, hwlSize, &hwl);
		if (problem != NULL)
		{
			fprintf(stderr, "rldpack: KART.HWL from %s: %s\n", hwlSource, problem);
			Rld_Say("error", "hwl-bad", problem, "The game's sound table (KART.HWL) cannot be used: %s.", problem);
			RLD_MUSIC_FAILED();
		}

		if (Rld_MusicCheck(&sca, &hwl, bank, song, why, sizeof(why), others, sizeof(others)) != NULL)
		{
			fprintf(stderr, "rldpack: %s\n", why);
			Rld_Say("error", "music", why, "The music does not fit the game's sound banks. Details below.");
			RLD_MUSIC_FAILED();
		}

		sndb = Rld_BuildSndb(&sca, &hwl, bank, song, &sndbSize, &rows);
		if (sndb == NULL)
		{
			fprintf(stderr, "rldpack: out of memory building the SNDB\n");
			Rld_Say("error", "memory", NULL, "rldpack ran out of memory while building the music.");
			RLD_MUSIC_FAILED();
		}

		printf("  music         bank %u (%s), sequence %u (%s), %u SPU size row(s)\n", bank, source[RLD_TXT_BANK], song, source[RLD_TXT_SONG], rows);
		printf("                KART.HWL from %s\n", hwlSource);
		printf("                same sample ids in other retail banks: %s\n", (others[0] != '\0') ? others : "none");
		{
			char text[160];

			snprintf(text, sizeof(text), "The track plays its own music (bank %u, sequence %u).", bank, song);
			Rld_Emit("music", "ok", text, (const char *)NULL);
		}
	}
	else if ((folder.sca.count + folder.sndb.count) == 1)
	{
		printf("  music         none - %s left out (--no-music), the track plays its seat's music\n",
		       (folder.sca.count == 1) ? folder.sca.names[0] : folder.sndb.names[0]);
		Rld_Emit("music", "off", "The music in the folder is left out; the track plays the music of its seat.", (const char *)NULL);
	}
	else
	{
		printf("  music         none - no .sca or .sndb in the folder, the track plays its seat's music\n");
		Rld_Emit("music", "none", "No music file in the folder; the track plays the music of its seat.", (const char *)NULL);
	}

musicDone:
#undef RLD_MUSIC_FAILED
	if (musicFailed)
	{
		Rld_Emit("music", "error", "The music cannot be built - see the error.", (const char *)NULL);
		sndb = NULL;
		sndbSize = 0;
	}

	printf("  output        %s\n\n", Rld_FileExists(outPath) ? "an existing file is replaced" : "a new file");

	memset(&job, 0, sizeof(job));
	job.levPath = levPath;
	job.vrmPath = vrmPath;
	job.outPath = outPath;
	job.levFlag = "";
	job.vrmFlag = "";
	job.sndbData = sndb;
	job.sndbSize = sndbSize;
	job.trackVersionText = value[RLD_TXT_TRACK_VERSION];
	job.modesText = value[RLD_TXT_MODES];
	job.reverbText = value[RLD_TXT_REVERB];
	job.botsText = value[RLD_TXT_BOTS];
	job.ambientText = value[RLD_TXT_AMBIENT];
	job.strings[0] = value[RLD_TXT_NAME];
	job.strings[1] = value[RLD_TXT_AUTHOR];
	job.abrKeep = abrKeep;
	job.make = 1;
	job.keepModelIds = keepModelIds;
	job.check = check;
	job.failedBefore = musicFailed;

	result = Rld_Pack(&job);
	return ((result == 0) && musicFailed) ? 1 : result;
}

// The values from PARM as one line, and a warning per invalid value - with
// what the game takes instead.
static void Rld_PrintParm(const struct RldParm *parm)
{
	printf("  PARM          version %u -", parm->version);
	if (parm->reverbState == RLD_PARM_SET)
	{
		if (parm->reverb == RLD_PARM_REVERB_OFF)
		{
			printf(" reverb off");
		}
		else
		{
			printf(" reverb %u", parm->reverb);
		}
	}
	if (parm->botsState == RLD_PARM_SET)
	{
		printf(" bots row %u", parm->bots);
	}
	if (parm->ambientState == RLD_PARM_SET)
	{
		printf(" ambient 0x%x/0x%x", parm->ambient[0], parm->ambient[1]);
	}
	if ((parm->reverbState != RLD_PARM_SET) && (parm->botsState != RLD_PARM_SET) && (parm->ambientState != RLD_PARM_SET))
	{
		printf(" no known value set");
	}
	printf("\n");

	if (parm->reverbState == RLD_PARM_INVALID)
	{
		printf("  WARNING       PARM reverb %u is not 0..4 or off (0xff) - the game uses 2\n", parm->reverb);
	}
	if (parm->botsState == RLD_PARM_INVALID)
	{
		printf("  WARNING       PARM bots %u is not a row 0..17 - the game uses row 0\n", parm->bots);
	}
	if (parm->ambientState == RLD_PARM_INVALID)
	{
		printf("  WARNING       PARM ambient has the wrong length - the game plays no ambient sound\n");
	}
	if (parm->unknownCount != 0u)
	{
		printf("                %u key(s) this rldpack does not know, first %u - skipped, as the game does\n", parm->unknownCount,
		       parm->firstUnknown);
	}
}

// info and verify choose the container type by its first 8 bytes, never by the
// name. Only the RLDCHAR magic (with its NUL) leads to the character reader;
// a track, a file shorter than 8 bytes or a foreign one takes the track path
// exactly as before, with the same texts.
static int Rld_IsCharFile(const char *path)
{
	u8 magic[8];
	FILE *file = fopen(path, "rb");
	int isChar = 0;

	if (file != NULL)
	{
		isChar = (fread(magic, 1, sizeof(magic), file) == sizeof(magic)) && (memcmp(magic, RLDCHAR_MAGIC, 8) == 0);
		fclose(file);
	}

	return isChar;
}

static int Cmd_Info(int argc, char *argv[], int full)
{
	struct RldReader reader;
	struct RldMeta meta;
	const char *error;
	u8 *metaData;
	size_t metaSize;
	int metaIndex = -1;
	int i;
	int failed = 0;

	if (argc < 1)
	{
		fprintf(stderr, "rldpack %s <file.rldtrack>\n", full ? "verify" : "info");
		return 2;
	}

	if (Rld_IsCharFile(argv[0]))
	{
		return full ? RldChar_VerifyCommand(argv[0]) : RldChar_InfoCommand(argv[0], 0);
	}

	error = Rld_Open(&reader, argv[0]);
	if (error != NULL)
	{
		fprintf(stderr, "rldpack: %s\n", error);
		if ((reader.refusal == RLD_REFUSAL_NEWER) || (reader.refusal == RLD_REFUSAL_OLDER))
		{
			fprintf(stderr, "         container format %u.%u, this rldpack reads %u.x", reader.major, reader.minor, reader.format->major);
			if (reader.unknownFlags != 0u)
			{
				fprintf(stderr, ", unknown feature bits 0x%08x", reader.unknownFlags);
			}
			fprintf(stderr, "\n");
		}
		return 1;
	}

	// Rld_Open has already demanded the required chunks; so META is there.
	Rld_FindEntry(&reader, "META", &metaIndex);

	metaData = Rld_ReadChunk(&reader, metaIndex, &metaSize, &error);
	if (metaData == NULL)
	{
		fprintf(stderr, "rldpack: META: %s\n", error);
		Rld_Close(&reader);
		return 1;
	}

	error = Rld_ParseMeta(&meta, metaData, metaSize);
	if (error != NULL)
	{
		fprintf(stderr, "rldpack: META: %s\n", error);
		Rld_Close(&reader);
		return 1;
	}

	printf("%s\n", argv[0]);
	printf("  format        %u.%u%s\n", reader.major, reader.minor, (reader.minor == 0u) ? "   (4.0, before the freeze - loads)" : "");
	printf("  track         %s\n", meta.strings[0]);
	// Empty fields are NAMED, not left out. Otherwise a missing author name
	// in a list is only noticed by whoever downloads the track.
	printf("  author        %s\n", (meta.strings[1][0] != '\0') ? meta.strings[1] : "(not set)");
	printf("  track_version %u\n", meta.trackVersion);
	printf("  modes         %s\n", Rld_ModesText(meta.modes));
	printf("  meta_version  %u%s\n", meta.metaVersion, (meta.metaVersion == 0) ? "   DRAFT" : ((meta.metaVersion == 1) ? "   frozen" : ""));

	// The chunks, and for each one this reader does not read, why: a
	// blocked name from an earlier version or a type from tomorrow.
	printf("  chunks        ");
	for (i = 0; i < reader.chunkCount; i++)
	{
		const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];

		printf("%.4s%s%s", (const char *)entry, (Rld_ChunkLimit(entry) != 0u) ? "" : " (skipped)", (i + 1 < reader.chunkCount) ? " " : "\n");
	}

	// PARM is small and is read here - LEVD and VRMD still stay
	// untouched, as promised below.
	{
		int parmIndex = -1;

		if (Rld_FindEntry(&reader, "PARM", &parmIndex) == NULL)
		{
			printf("  PARM          none - the game uses its defaults (reverb 2, bots row 0, no ambient sound)\n");
		}
		else
		{
			struct RldParm parm;
			size_t parmSize = 0;
			const char *parmError = NULL;
			u8 *parmData = Rld_ReadChunk(&reader, parmIndex, &parmSize, &parmError);

			if (parmData != NULL)
			{
				parmError = Rld_ParseParm(&parm, parmData, parmSize);
				free(parmData);
			}

			if (parmError != NULL)
			{
				printf("  WARNING       PARM unusable: %s - the game uses its defaults\n", parmError);
			}
			else
			{
				Rld_PrintParm(&parm);
			}
		}
	}

	if (!full)
	{
		// LEVD and VRMD stay untouched. The menu scan in the
		// game reads only header, directory and META for dozens of files, and
		// `info` goes the same way, so that it is tested from here too.
		printf("\nLEVD and VRMD were not touched.\n");
		Rld_Close(&reader);
		return 0;
	}

	for (i = 0; i < reader.chunkCount; i++)
	{
		const u8 *entry = &reader.directory[i * RLD_DIR_ENTRY_SIZE];
		u8 *data;
		size_t size;

		if (i == metaIndex)
		{
			printf("  ok     %.4s   %llu bytes\n", (const char *)entry, (unsigned long long)metaSize);
			continue;
		}

		// A type this reader does not know is not read - as in the game.
		if (Rld_ChunkLimit(entry) == 0u)
		{
			printf("  skip   %.4s   not a chunk type of this format - left alone, as the game does\n", (const char *)entry);
			continue;
		}

		data = Rld_ReadChunk(&reader, i, &size, &error);
		if (data == NULL)
		{
			printf("  FAILED %.4s   %s\n", (const char *)entry, error);
			failed = 1;
			continue;
		}

		// The optional chunks with the same reader the game runs. A
		// hash that matches only says the bytes are intact - not that
		// the game can take them. Unusable is not a load error there, only a
		// loss: hence a warning and not FAILED.
		if (memcmp(entry, "SNDB", 4) == 0)
		{
			struct RldSndb checkedSound;
			const char *why = Rld_ParseSndb(&checkedSound, data, size);

			if (why != NULL)
			{
				printf("  WARN   SNDB   %s - the game plays the seat's disc sound instead\n", why);
				free(data);
				continue;
			}
		}

		if (memcmp(entry, "PARM", 4) == 0)
		{
			struct RldParm checkedParm;
			const char *why = Rld_ParseParm(&checkedParm, data, size);

			if (why != NULL)
			{
				printf("  WARN   PARM   %s - the game uses its defaults\n", why);
				free(data);
				continue;
			}
		}

		printf("  ok     %.4s   %llu bytes\n", (const char *)entry, (unsigned long long)size);

		// The declared modes against the track - only a warning, as with
		// SNDB and PARM: the container is intact, only its META promises too much.
		if (memcmp(entry, "LEVD", 4) == 0)
		{
			struct RldSpawn spawn;
			struct RldModelIds ids;
			struct RldLevModes levModes;

			Rld_SpawnRead(data, size, &spawn);
			Rld_ModelIds(data, size, 0, &ids);
			Rld_LevModesRead(data, size, &levModes);
			Rld_ModesDeclaredWarn(NULL, meta.modes, &spawn, &levModes, &ids);
		}
		free(data);
	}

	// There is only ONE statement: does every chunk read match its hash in the
	// directory. The overall verdict is at the bottom, because the last line is what gets read.
	if (failed)
	{
		printf("\nTHIS CONTAINER IS NOT SOUND. The line above says which part.\n");
		printf("Do not load it, do not pass it on.\n");
	}
	else
	{
		printf("\nEvery chunk matches its hash.\n");
	}

	Rld_Close(&reader);
	return failed;
}

//========================================================================================
// SELF-TEST
//========================================================================================

static int Rld_Check(const char *what, const u8 *got, const u8 *want, size_t size)
{
	if (memcmp(got, want, size) == 0)
	{
		printf("  ok   %s\n", what);
		return 0;
	}

	printf("  FAIL %s\n       got  ", what);
	Rld_PrintHex(got, size);
	printf("\n       want ");
	Rld_PrintHex(want, size);
	printf("\n");
	return 1;
}

// GOLDEN HASHES: the SHA-256 of a whole container built from fixed synthetic
// inputs. Determinism shows that two builds agree with each other; these show
// that they still agree with the bytes this packer wrote before its envelope
// code was shared with other container types. A different value means a
// .rldtrack is no longer written the same way - never update it to make the
// test pass.
#define RLD_GOLDEN_3_CHUNKS "489b8b3a20846502c5372ab9d0ce0e6a3946acced40cab96eed3aa7e6e4ccc00"
#define RLD_GOLDEN_5_CHUNKS "5d8c5b474136b44717da5463d1aa8a0f75e07050af751ed8915807ae83285489"

static int Rld_CheckGolden(const char *name, const u8 *data, size_t size, const char *wantHex)
{
	u8 digest[32];
	u8 want[32];

	if (data == NULL)
	{
		printf("  FAIL golden hash %s: no container\n", name);
		return 1;
	}

	Sha256(data, size, digest);
	if (!Rld_HexToBytes(wantHex, want, 32) || (memcmp(digest, want, 32) != 0))
	{
		printf("  FAIL golden hash %s: expected %s, got ", name, wantHex);
		Rld_PrintHex(digest, 32);
		printf("\n");
		return 1;
	}

	printf("  ok   golden hash %s, %llu bytes\n", name, (unsigned long long)size);
	return 0;
}

// --- Self-test for make -------------------------------------------------------------
//
// All synthetic, no real track data: a LEV in miniature, a
// mini SCA against a mini KART.HWL, and a few track.txt lines.

// The LEV in miniature, body-relative: header up to 0x200, instances from 0x200,
// models from 0x300, spawn table from 0x400, pointer map from 0x480.
#define RLD_TEST_LEV_SIZE (4u + 0x600u)

struct RldTestInstance
{
	int modelId;
	int declared;
	int pointerInMap;
};

static void Rld_TestLev(u8 *lev, const struct RldTestInstance *inst, int instances, int spawnCount, const int *spawnSet)
{
	u32 slots[32];
	int n = 0;
	int i;

	memset(lev, 0, RLD_TEST_LEV_SIZE);
	Rld_WriteLE32(&lev[0], 0x480u);
	Rld_WriteLE32(&lev[4u + 0x0cu], (u32)instances);
	Rld_WriteLE32(&lev[4u + 0x10u], 0x200u);
	slots[n++] = 0x10u;

	for (i = 0; i < instances; i++)
	{
		const u32 record = 0x200u + ((u32)i * 0x40u);
		const u32 model = 0x300u + ((u32)i * 0x20u);

		Rld_WriteLE32(&lev[4u + record + 0x10u], model);
		Rld_WriteLE32(&lev[4u + record + 0x3cu], (u32)inst[i].declared);
		Rld_WriteLE16(&lev[4u + model + 0x10u], (u32)inst[i].modelId);

		if (inst[i].pointerInMap)
		{
			slots[n++] = record + 0x10u;
		}
	}

	if (spawnCount >= 0)
	{
		int k;

		Rld_WriteLE32(&lev[4u + 0x134u], 0x400u);
		slots[n++] = 0x134u;
		Rld_WriteLE32(&lev[4u + 0x400u], (u32)spawnCount);

		for (k = 0; k < spawnCount; k++)
		{
			if (spawnSet[k])
			{
				Rld_WriteLE32(&lev[4u + 0x404u + (4u * (u32)k)], 0x100u);
				slots[n++] = 0x404u + (4u * (u32)k);
			}
		}
	}

	Rld_WriteLE32(&lev[4u + 0x480u], (u32)n * 4u);
	for (i = 0; i < n; i++)
	{
		Rld_WriteLE32(&lev[4u + 0x484u + (4u * (u32)i)], slots[i]);
	}
}

static int Rld_SelftestMake(void)
{
	int failed = 0;
	char why[512];

	// MODEL ID: differing -> corrected, pointer not in the map ->
	// untouched, negative Model.id -> sign-extended, --keep-model-ids ->
	// counted and not written, no instances -> nothing.
	{
		static const struct RldTestInstance inst[4] = {{0x94, 0x94, 1}, {0x95, 0x94, 1}, {0x95, 0x94, 0}, {-1, 0x10, 1}};
		u8 lev[RLD_TEST_LEV_SIZE];
		u8 kept[RLD_TEST_LEV_SIZE];
		struct RldModelIds report;
		int bad = 0;

		Rld_TestLev(lev, inst, 4, -1, NULL);
		memcpy(kept, lev, sizeof(lev));

		Rld_ModelIds(kept, sizeof(kept), 0, &report);
		if ((report.problem != NULL) || (report.differ != 2u) || (memcmp(kept, lev, sizeof(lev)) != 0))
		{
			bad |= 1;
		}

		Rld_ModelIds(lev, sizeof(lev), 1, &report);
		if ((report.problem != NULL) || (report.instances != 4u) || (report.checked != 3u) || (report.differ != 2u))
		{
			bad |= 2;
		}

		if ((Rld_ReadLE32At(lev, 4u + 0x240u + 0x3cu) != 0x95u) || (Rld_ReadLE32At(lev, 4u + 0x280u + 0x3cu) != 0x94u) ||
		    (Rld_ReadLE32At(lev, 4u + 0x2c0u + 0x3cu) != 0xffffffffu) || (Rld_ReadLE32At(lev, 4u + 0x200u + 0x3cu) != 0x94u))
		{
			bad |= 4;
		}

		if ((report.letters[0] != 0u) || (report.letters[1] != 1u) || (report.letters[2] != 1u))
		{
			bad |= 8;
		}

		Rld_TestLev(lev, inst, 0, -1, NULL);
		Rld_ModelIds(lev, sizeof(lev), 1, &report);
		if ((report.problem != NULL) || (report.differ != 0u))
		{
			bad |= 16;
		}

		if (bad != 0)
		{
			printf("  FAIL model ids: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   model ids: 2 of 4 corrected, one outside the pointer map left alone, -1 sign-extended, keep and empty\n");
		}
	}

	// LETTERS BY NAME ("CTR Test"): t carries 0x95, r 0x94 -> swapped,
	// InstDef.modelID follows; "tr" with 0x94 stays; with apply 0 only reported.
	{
		static const struct RldTestInstance inst[4] = {{0x93, 0x93, 1}, {0x95, 0x95, 1}, {0x94, 0x94, 1}, {0x94, 0x94, 1}};
		static const char *const names[4] = {"c", "t", "R", "tr"};
		u8 lev[RLD_TEST_LEV_SIZE];
		u8 kept[RLD_TEST_LEV_SIZE];
		struct RldModelIds report;
		int bad = 0;
		int i;

		Rld_TestLev(lev, inst, 4, -1, NULL);
		for (i = 0; i < 4; i++)
		{
			memcpy(&lev[4u + 0x300u + ((u32)i * 0x20u)], names[i], strlen(names[i]));
		}
		memcpy(kept, lev, sizeof(lev));

		Rld_ModelIds(kept, sizeof(kept), 0, &report);
		if ((report.letterFixes != 2u) || (report.differ != 0u) || (memcmp(kept, lev, sizeof(lev)) != 0))
		{
			bad |= 1;
		}

		Rld_ModelIds(lev, sizeof(lev), 1, &report);
		if ((report.problem != NULL) || (report.letterFixes != 2u) || (report.differ != 2u) || (report.letterFix[0].before != 0x95) ||
		    (report.letterFix[0].after != 0x94) || (report.letterFix[1].before != 0x94) || (report.letterFix[1].after != 0x95))
		{
			bad |= 2;
		}

		if ((Rld_ReadLE16At(lev, 4u + 0x320u + 0x10u) != 0x94) || (Rld_ReadLE16At(lev, 4u + 0x340u + 0x10u) != 0x95) ||
		    (Rld_ReadLE16At(lev, 4u + 0x360u + 0x10u) != 0x94) || (Rld_ReadLE32At(lev, 4u + 0x240u + 0x3cu) != 0x94u) ||
		    (Rld_ReadLE32At(lev, 4u + 0x280u + 0x3cu) != 0x95u) || (Rld_ReadLE32At(lev, 4u + 0x2c0u + 0x3cu) != 0x94u))
		{
			bad |= 4;
		}

		if ((report.letters[0] != 1u) || (report.letters[1] != 2u) || (report.letters[2] != 1u))
		{
			bad |= 8;
		}

		// A second run finds nothing any more.
		Rld_ModelIds(lev, sizeof(lev), 1, &report);
		if ((report.letterFixes != 0u) || (report.differ != 0u))
		{
			bad |= 16;
		}

		if (bad != 0)
		{
			printf("  FAIL letter ids: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   letter ids: t/R swapped back by name, InstDef follows, \"tr\" left alone, keep only reports\n");
		}
	}

	// SPAWN TABLE: count 2 + time -> refused; count 6 with 101111 + time ->
	// accepted; count 4 with NULL at slot 3 -> refused; count 2 without time
	// -> accepted.
	{
		static const int two[2] = {1, 1};
		static const int six[6] = {1, 0, 1, 1, 1, 1};
		static const int four[4] = {1, 0, 1, 0};
		u8 lev[RLD_TEST_LEV_SIZE];
		struct RldSpawn spawn;
		int bad = 0;

		Rld_TestLev(lev, NULL, 0, 2, two);
		Rld_SpawnRead(lev, sizeof(lev), &spawn);
		if (Rld_SpawnCheck(&spawn, RLD_MODE_RACE | RLD_MODE_TIME_TRIAL, why, sizeof(why)) == NULL)
		{
			bad |= 1;
		}

		if (Rld_SpawnCheck(&spawn, RLD_MODE_RACE, why, sizeof(why)) != NULL)
		{
			bad |= 2;
		}

		Rld_TestLev(lev, NULL, 0, 6, six);
		Rld_SpawnRead(lev, sizeof(lev), &spawn);
		if (Rld_SpawnCheck(&spawn, RLD_MODE_RACE | RLD_MODE_TIME_TRIAL, why, sizeof(why)) != NULL)
		{
			bad |= 4;
		}

		Rld_TestLev(lev, NULL, 0, 4, four);
		Rld_SpawnRead(lev, sizeof(lev), &spawn);
		if (Rld_SpawnCheck(&spawn, RLD_MODE_RACE, why, sizeof(why)) == NULL)
		{
			bad |= 8;
		}

		if (bad != 0)
		{
			printf("  FAIL spawn table: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   spawn table: time refused at count 2, taken at count 6 (101111), fly-in NULL refused at count 4\n");
		}
	}

	// SCA -> SNDB, against a mini KART.HWL with three banks. Sample 1 keeps
	// its size, sample 3 gets a new one: exactly one SPU row. Bank 1
	// carries sample 3 too - that is the hint list. If bank 0 carries it, that is
	// an abort, and bank 0 as target likewise.
	{
		static u8 sca[4u + 8u + 0x818u + 8u + 8u + 8u + 4u + 8u + 32u];
		static u8 hwlBytes[4u * 2048u];
		static u8 want[52u + 4096u + 2048u];
		static const char meta[] = "{\"name\":\"Mini\",\"author\":\"Test\"}";
		struct RldSca parsed;
		struct RldHwl hwl;
		struct RldSndb check;
		char others[128];
		u8 *sndb;
		size_t at = 4;
		size_t sndbSize = 0;
		u32 rows = 0;
		int bad = 0;
		int i;

		memset(sca, 0, sizeof(sca));
		memcpy(sca, "SCA\x01", 4);

		memcpy(&sca[at], "BANK", 4);
		Rld_WriteLE32(&sca[at + 4u], 0x818u);
		Rld_WriteLE16(&sca[at + 8u], 2u);
		Rld_WriteLE16(&sca[at + 10u], 1u);
		Rld_WriteLE16(&sca[at + 12u], 3u);
		for (i = 0; i < 24; i++)
		{
			sca[at + 8u + 0x800u + (size_t)i] = (u8)(0xa0 + i);
		}
		at += 8u + 0x818u;

		memcpy(&sca[at], "CSEQ", 4);
		Rld_WriteLE32(&sca[at + 4u], 8u);
		Rld_WriteLE32(&sca[at + 8u], 8u);
		sca[at + 12u] = 1u;
		sca[at + 13u] = 2u;
		sca[at + 14u] = 3u;
		sca[at + 15u] = 4u;
		at += 16u;

		memcpy(&sca[at], "SIZE", 4);
		Rld_WriteLE32(&sca[at + 4u], 4u);
		Rld_WriteLE16(&sca[at + 8u], 2u);
		Rld_WriteLE16(&sca[at + 10u], 1u);
		at += 12u;

		memcpy(&sca[at], "META", 4);
		Rld_WriteLE32(&sca[at + 4u], (u32)(sizeof(meta) - 1u));
		memcpy(&sca[at + 8u], meta, sizeof(meta) - 1u);
		at += 8u + ((sizeof(meta) - 1u + 3u) & ~(size_t)3u);

		memset(hwlBytes, 0, sizeof(hwlBytes));
		Rld_WriteLE32(&hwlBytes[0x10], 4u);
		Rld_WriteLE32(&hwlBytes[0x1c], 3u);
		Rld_WriteLE32(&hwlBytes[0x20], 1u);
		Rld_WriteLE16(&hwlBytes[0x28 + 2], 7u);
		Rld_WriteLE16(&hwlBytes[0x2c + 2], 2u);
		Rld_WriteLE16(&hwlBytes[0x30 + 2], 7u);
		Rld_WriteLE16(&hwlBytes[0x34 + 2], 5u);
		Rld_WriteLE16(&hwlBytes[0x38], 1u);
		Rld_WriteLE16(&hwlBytes[0x3a], 2u);
		Rld_WriteLE16(&hwlBytes[0x3c], 3u);
		Rld_WriteLE16(&hwlBytes[2048], 1u);
		Rld_WriteLE16(&hwlBytes[2050], 2u);
		Rld_WriteLE16(&hwlBytes[4096], 1u);
		Rld_WriteLE16(&hwlBytes[4098], 3u);
		Rld_WriteLE16(&hwlBytes[6144], 2u);
		Rld_WriteLE16(&hwlBytes[6146], 1u);
		Rld_WriteLE16(&hwlBytes[6148], 3u);

		memset(want, 0, sizeof(want));
		memcpy(want, "SNDB", 4);
		Rld_WriteLE16(&want[4], 2u);
		Rld_WriteLE16(&want[6], 2u);
		Rld_WriteLE16(&want[8], 1u);
		Rld_WriteLE32(&want[12], 52u);
		Rld_WriteLE16(&want[16], 2u);
		Rld_WriteLE16(&want[18], 0u);
		want[20] = 0u;
		Rld_WriteLE16(&want[22], 2u);
		Rld_WriteLE32(&want[24], 0u);
		Rld_WriteLE32(&want[28], 4096u);
		want[32] = 1u;
		Rld_WriteLE16(&want[34], 0u);
		Rld_WriteLE32(&want[36], 4096u);
		Rld_WriteLE32(&want[40], 2048u);
		Rld_WriteLE16(&want[44], 3u);
		Rld_WriteLE16(&want[48], 1u);
		memcpy(&want[52], &sca[12], 0x818u);
		memcpy(&want[52u + 4096u], &sca[4u + 8u + 0x818u + 8u], 8u);

		if ((Rld_ReadSca(sca, at, &parsed) != NULL) || (parsed.samples != 2) || (Rld_ReadHwl(hwlBytes, sizeof(hwlBytes), &hwl) != NULL))
		{
			bad |= 1;
		}
		else
		{
			char name[16];

			if (Rld_MusicCheck(&parsed, &hwl, 2u, 0u, why, sizeof(why), others, sizeof(others)) != NULL)
			{
				bad |= 2;
			}
			else if (strcmp(others, "3 in [1]") != 0)
			{
				bad |= 4;
			}

			sndb = Rld_BuildSndb(&parsed, &hwl, 2u, 0u, &sndbSize, &rows);
			if ((sndb == NULL) || (rows != 1u) || (sndbSize != sizeof(want)) || (memcmp(sndb, want, sizeof(want)) != 0) ||
			    (Rld_ParseSndb(&check, sndb, sndbSize) != NULL))
			{
				bad |= 8;
			}
			free(sndb);

			if (!Rld_ScaMetaString(parsed.meta, parsed.metaSize, "author", name, sizeof(name)) || (strcmp(name, "Test") != 0))
			{
				bad |= 16;
			}

			if (Rld_MusicCheck(&parsed, &hwl, 0u, 0u, why, sizeof(why), others, sizeof(others)) == NULL)
			{
				bad |= 32;
			}

			Rld_WriteLE16(&hwlBytes[2050], 3u);
			if (Rld_MusicCheck(&parsed, &hwl, 2u, 0u, why, sizeof(why), others, sizeof(others)) == NULL)
			{
				bad |= 64;
			}
		}

		// The same SCA, BANK and CSEQ padded to whole sectors (as newer
		// exports write it): the same SNDB. One sector more than needed is refused.
		{
			const size_t tail = at - 0x834u;
			size_t bankChunk;

			for (bankChunk = 0x1000u; (bad == 0) && (bankChunk <= 0x1800u); bankChunk += 0x800u)
			{
				const size_t padSize = 4u + 8u + bankChunk + 8u + 2048u + tail;
				u8 *padded = (u8 *)calloc(1, padSize);
				size_t p = 4;

				if (padded == NULL)
				{
					bad |= 128;
					break;
				}

				memcpy(padded, sca, 4);
				memcpy(&padded[p], "BANK", 4);
				Rld_WriteLE32(&padded[p + 4u], (u32)bankChunk);
				memcpy(&padded[p + 8u], &sca[12], 0x818u);
				memset(&padded[p + 8u + 0x818u], 0xee, bankChunk - 0x818u);
				p += 8u + bankChunk;
				memcpy(&padded[p], "CSEQ", 4);
				Rld_WriteLE32(&padded[p + 4u], 2048u);
				memcpy(&padded[p + 8u], &sca[0x82cu], 8u);
				p += 8u + 2048u;
				memcpy(&padded[p], &sca[0x834u], tail);

				if (bankChunk == 0x1000u)
				{
					sndb = NULL;
					if ((Rld_ReadSca(padded, padSize, &parsed) != NULL) || (parsed.bankPad != (0x1000u - 0x818u)) || (parsed.cseqPad != 2040u))
					{
						bad |= 256;
					}
					else
					{
						sndb = Rld_BuildSndb(&parsed, &hwl, 2u, 0u, &sndbSize, &rows);
						if ((sndb == NULL) || (sndbSize != sizeof(want)) || (memcmp(sndb, want, sizeof(want)) != 0))
						{
							bad |= 512;
						}
					}
					free(sndb);
				}
				else if (Rld_ReadSca(padded, padSize, &parsed) == NULL)
				{
					bad |= 1024;
				}

				free(padded);
			}
		}

		if (bad != 0)
		{
			printf("  FAIL sca -> sndb: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   sca -> sndb: exact bytes, one SPU row, other banks listed, reserved bank 0 refused as target and as neighbour\n");
			printf("  ok   sca padded to whole sectors: same SNDB; one sector too many refused\n");
		}
	}

	// track.txt: BOM, comment, CRLF, empty author, # in the value; refused
	// are an unknown and a duplicate key, a line without = and
	// an empty value except for the author.
	{
		static const char good[] = "\xEF\xBB\xBF# comment\r\nname = Mini # Track \r\nauthor =\r\n\r\nbank=7\n";
		static const char *const refused[4] = {"nmae = X\n", "name = A\nname = B\n", "name A\n", "modes =\n"};
		static struct RldTrackTxt txt;
		int bad = 0;
		int k;

		if ((Rld_ParseTrackTxt(good, sizeof(good) - 1u, &txt, why, sizeof(why)) != NULL) || (strcmp(txt.value[RLD_TXT_NAME], "Mini # Track") != 0) ||
		    (txt.line[RLD_TXT_AUTHOR] != 3) || (txt.value[RLD_TXT_AUTHOR][0] != '\0') || (strcmp(txt.value[RLD_TXT_BANK], "7") != 0))
		{
			bad |= 1;
		}

		for (k = 0; k < 4; k++)
		{
			if (Rld_ParseTrackTxt(refused[k], strlen(refused[k]), &txt, why, sizeof(why)) == NULL)
			{
				bad |= 2 << k;
			}
		}

		if (bad != 0)
		{
			printf("  FAIL track.txt: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   track.txt: BOM, comment, CRLF, empty author; unknown, twice, no '=' and empty value refused\n");
		}
	}

	return failed;
}

static int Cmd_Selftest(void)
{
	int failed = 0;

	printf("rldpack self-test\n");

	{
		u8 digest[32];
		u8 want[32];

		Sha256("abc", 3, digest);
		Rld_HexToBytes("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", want, 32);
		failed |= Rld_Check("SHA-256, RFC 6234", digest, want, 32);
	}

	{
		// DETERMINISM. Built twice from the same inputs
		// must give the same byte for byte - otherwise it can no longer be
		// proven later that two containers hold the same track.
		struct RldBuildInput in;
		u8 lev[777], vrm[333];
		u8 *a, *b;
		size_t sizeA = 0, sizeB = 0;
		const char *error = NULL;
		struct RldMeta back;
		int i;

		memset(&back, 0, sizeof(back));
		for (i = 0; i < (int)sizeof(lev); i++)
		{
			lev[i] = (u8)(i * 7 + 3);
		}
		for (i = 0; i < (int)sizeof(vrm); i++)
		{
			vrm[i] = (u8)(i * 11 + 5);
		}

		memset(&in, 0, sizeof(in));
		in.lev = lev;
		in.levSize = sizeof(lev);
		in.vrm = vrm;
		in.vrmSize = sizeof(vrm);
		in.trackVersion = 3;
		in.modes = RLD_MODE_RACE | RLD_MODE_TIME_TRIAL;
		in.strings[0] = "self-test";
		in.strings[1] = "nobody";

		a = Rld_Build(&in, &sizeA, &error);
		b = Rld_Build(&in, &sizeB, &error);

		if ((a == NULL) || (b == NULL) || (sizeA != sizeB) || (memcmp(a, b, sizeA) != 0))
		{
			printf("  FAIL determinism: two builds, two different files\n");
			failed = 1;
		}
		else
		{
			printf("  ok   determinism, %llu bytes identical twice\n", (unsigned long long)sizeA);
		}

		// GOLDEN HASH, three chunks: META LEVD VRMD from the inputs above.
		failed |= Rld_CheckGolden("3 chunks", a, sizeA, RLD_GOLDEN_3_CHUNKS);

		// META THERE AND BACK.
		//
		// It checks the field positions of META. A swapped offset between
		// writer and reader would otherwise only have been noticed by an author whose
		// track carries the wrong name in the menu.
		//
		// When signing was removed, publicKey dropped out of the list of checked fields,
		// but its 32 bytes at 0x04 did NOT drop out of the layout. That is exactly
		// why this test matters more now than before: it is the place
		// where it would show that someone closes the gap and so shifts every field
		// behind it.
		if ((a != NULL) && (sizeA > RLD_HEADER_SIZE))
		{
			const u64 dirAt = Rld_ReadLE64(&a[0x18]);
			const u32 count = Rld_ReadLE32(&a[0x10]);
			const char *error2 = "META not found in the built container";
			u32 c;

			for (c = 0; c < count; c++)
			{
				const u8 *entry = &a[(size_t)dirAt + (size_t)c * RLD_DIR_ENTRY_SIZE];

				if (memcmp(entry, "META", 4) == 0)
				{
					const u64 at = Rld_ReadLE64(&entry[0x08]);
					const u64 raw = Rld_ReadLE64(&entry[0x18]);

					error2 = Rld_ParseMeta(&back, &a[(size_t)at], (size_t)raw);
					break;
				}
			}

			if (error2 != NULL)
			{
				printf("  FAIL META round trip: %s\n", error2);
				failed = 1;
			}
			else if ((strcmp(back.strings[0], in.strings[0]) != 0) || (strcmp(back.strings[1], in.strings[1]) != 0) ||
			         (back.trackVersion != in.trackVersion) || (back.modes != in.modes))
			{
				printf("  FAIL META round trip: a field did not come back the way it went in\n");
				failed = 1;
			}
			else
			{
				printf("  ok   META round trip: name, author, version, modes\n");
			}
		}

		// THE CHUNK SET, since signing was removed.
		//
		// Three without sound, four with - and in NO case a SIGN. That is the
		// one promise a build has to keep after this rework, and it is
		// here because a chunk accidentally written again would otherwise only
		// be noticed when loading.
		if (a != NULL)
		{
			const u64 dirAt = Rld_ReadLE64(&a[0x18]);
			const u32 count = Rld_ReadLE32(&a[0x10]);
			int sawSign = 0;
			u32 c;

			for (c = 0; c < count; c++)
			{
				if (memcmp(&a[(size_t)dirAt + (size_t)c * RLD_DIR_ENTRY_SIZE], "SIGN", 4) == 0)
				{
					sawSign = 1;
				}
			}

			if ((count != 3u) || sawSign)
			{
				printf("  FAIL chunk set: %u chunks%s, expected 3 and no SIGN\n", count, sawSign ? " including SIGN" : "");
				failed = 1;
			}
			else
			{
				printf("  ok   chunk set: META LEVD VRMD, no SIGN\n");
			}

			// 4.1: the header carries 4.1 and no required features, META the 1.
			if ((Rld_ReadLE16(&a[0x08]) != 4u) || (Rld_ReadLE16(&a[0x0a]) != 1u) || (Rld_ReadLE32(&a[0x0c]) != 0u) || (back.metaVersion != 1u))
			{
				printf("  FAIL format 4.1: header %u.%u, flags 0x%x, meta_version %u\n", Rld_ReadLE16(&a[0x08]), Rld_ReadLE16(&a[0x0a]),
				       Rld_ReadLE32(&a[0x0c]), back.metaVersion);
				failed = 1;
			}
			else
			{
				printf("  ok   format 4.1: header 4.1, no feature bits, meta_version 1\n");
			}
		}

		free(a);
		free(b);

		// THE SAME BUILD WITH SOUND. The chunk is optional, so both have to
		// work: without it three entries, with it four, and it sits at the end.
		//
		// The chunk here is the SMALLEST that Rld_ParseSndb lets through - one
		// bank and one sequence, one sector each, because playBank and playSong both
		// must have a payload in the chunk. A dummy value would have passed the build
		// too (Rld_Build does not check the content), but then
		// this test would check a chunk the game rejects.
		{
			const u32 payloadAt = RLD_SNDB_HEADER_SIZE + 2u * RLD_SNDB_ENTRY_SIZE;
			u8 sound[RLD_SNDB_HEADER_SIZE + 2u * RLD_SNDB_ENTRY_SIZE + 2u * RLD_HOWL_SECTOR];
			struct RldSndb parsed;
			const char *why;
			u8 *withSound;
			size_t soundSize = 0;

			memset(sound, 0, sizeof(sound));
			memcpy(&sound[0], "SNDB", 4);
			Rld_WriteLE16(&sound[0x04], RLD_SNDB_VERSION);
			Rld_WriteLE16(&sound[0x06], 2);          // entryCount
			Rld_WriteLE16(&sound[0x08], 0);          // spuFixupCount
			Rld_WriteLE32(&sound[0x0c], payloadAt);  // payloadOffset
			Rld_WriteLE16(&sound[0x10], 5);          // playBank
			Rld_WriteLE16(&sound[0x12], 0);          // playSong

			sound[payloadAt - 2u * RLD_SNDB_ENTRY_SIZE] = (u8)RLD_SNDB_KIND_BANK;
			Rld_WriteLE16(&sound[payloadAt - 2u * RLD_SNDB_ENTRY_SIZE + 0x02], 5);
			Rld_WriteLE32(&sound[payloadAt - 2u * RLD_SNDB_ENTRY_SIZE + 0x04], 0);
			Rld_WriteLE32(&sound[payloadAt - 2u * RLD_SNDB_ENTRY_SIZE + 0x08], RLD_HOWL_SECTOR);

			sound[payloadAt - RLD_SNDB_ENTRY_SIZE] = (u8)RLD_SNDB_KIND_SONG;
			Rld_WriteLE16(&sound[payloadAt - RLD_SNDB_ENTRY_SIZE + 0x02], 0);
			Rld_WriteLE32(&sound[payloadAt - RLD_SNDB_ENTRY_SIZE + 0x04], RLD_HOWL_SECTOR);
			Rld_WriteLE32(&sound[payloadAt - RLD_SNDB_ENTRY_SIZE + 0x08], RLD_HOWL_SECTOR);

			why = Rld_ParseSndb(&parsed, sound, sizeof(sound));
			if (why != NULL)
			{
				printf("  FAIL SNDB build: the test chunk itself does not parse: %s\n", why);
				failed = 1;
			}

			in.sndb = sound;
			in.sndbSize = sizeof(sound);

			withSound = Rld_Build(&in, &soundSize, &error);

			if (withSound == NULL)
			{
				printf("  FAIL SNDB build: %s\n", (error != NULL) ? error : "no container");
				failed = 1;
			}
			else
			{
				const u64 dirAt = Rld_ReadLE64(&withSound[0x18]);
				const u32 count = Rld_ReadLE32(&withSound[0x10]);
				const u8 *last = &withSound[(size_t)dirAt + (size_t)(count - 1u) * RLD_DIR_ENTRY_SIZE];

				if ((count != 4u) || (memcmp(last, "SNDB", 4) != 0))
				{
					printf("  FAIL SNDB build: %u chunks, last is %.4s\n", count, (const char *)last);
					failed = 1;
				}
				else if (why == NULL)
				{
					printf("  ok   SNDB build: META LEVD VRMD SNDB, bank %u sequence %u\n", parsed.playBank, parsed.playSong);
				}
			}

			free(withSound);
			in.sndb = NULL;
			in.sndbSize = 0;
		}

		// GOLDEN HASH, five chunks: META LEVD VRMD SNDB PARM, so the order of the
		// optional chunks and PARM through Rld_Build are covered too. SNDB and PARM
		// are filler, not valid chunks: Rld_Build checks only their sizes and
		// copies them, and this case is about the envelope.
		{
			u8 sound[200];
			u8 values[24];
			u8 *five;
			size_t fiveSize = 0;

			for (i = 0; i < (int)sizeof(sound); i++)
			{
				sound[i] = (u8)(i * 13 + 1);
			}
			for (i = 0; i < (int)sizeof(values); i++)
			{
				values[i] = (u8)(i * 17 + 9);
			}

			in.sndb = sound;
			in.sndbSize = sizeof(sound);
			in.parm = values;
			in.parmSize = sizeof(values);

			five = Rld_Build(&in, &fiveSize, &error);
			failed |= Rld_CheckGolden("5 chunks", five, fiveSize, RLD_GOLDEN_5_CHUNKS);

			free(five);
			in.sndb = NULL;
			in.sndbSize = 0;
			in.parm = NULL;
			in.parmSize = 0;
		}
	}


	// THE ABR PASS, on constructed cases.
	//
	// The rule is "when in doubt, do not set", and exactly that can
	// be nailed down here: an STP bit in an entry the face does NOT
	// touch must not stop it - one in an entry it
	// touches must stop it. And whatever lies outside what was written
	// stays unknown instead of free.
	{
		unsigned short *pixels = NULL;
		u8 *written = NULL;
		u8 layout[RLD_TEXLAYOUT];
		int step = 0;

		pixels = (unsigned short *)calloc((size_t)RLD_VRAM_W * RLD_VRAM_H, sizeof(unsigned short));
		written = (u8 *)calloc((size_t)RLD_VRAM_W * RLD_VRAM_H, 1);

		if ((pixels == NULL) || (written == NULL))
		{
			printf("  FAIL ABR verdict: out of memory\n");
			failed = 1;
		}
		else
		{
			// Page 0 lies at x 0..63, y 0..255. A 4-bit image whose texels
			// all carry index 1: one word 0x1111 covers four texels.
			int i;

			for (i = 0; i < 64; i++)
			{
				pixels[i] = 0x1111;
				written[i] = 1;
			}

			// The CLUT at x=256, y=1. Entry 1 without STP, entry 2 WITH.
			for (i = 0; i < 16; i++)
			{
				pixels[1 * RLD_VRAM_W + 256 + i] = (unsigned short)(0x1000 + i);
				written[1 * RLD_VRAM_W + 256 + i] = 1;
			}
			pixels[1 * RLD_VRAM_W + 256 + 2] |= 0x8000u;

			memset(layout, 0, sizeof(layout));
			layout[0x00] = 0; layout[0x01] = 0;   // u0 v0
			layout[0x04] = 3; layout[0x05] = 0;   // u1 v1
			layout[0x08] = 0; layout[0x09] = 0;   // u2 v2
			layout[0x0a] = 3; layout[0x0b] = 0;   // u3 v3
			Rld_WriteLE16(&layout[0x02], (unsigned short)((1u << 6) | (256u >> 4)));
			Rld_WriteLE16(&layout[0x06], 0);      // page 0, 4 bit, ABR 0

			// 1. The STP bit sits in entry 2, the face only uses 1.
			step |= (Rld_AbrVerdict(layout, pixels, written) == RLD_ABR_SET) ? 0 : 1;

			// 2. If it moves to entry 1, the face must stay.
			pixels[1 * RLD_VRAM_W + 256 + 1] |= 0x8000u;
			step |= (Rld_AbrVerdict(layout, pixels, written) == RLD_ABR_STP) ? 0 : 2;
			pixels[1 * RLD_VRAM_W + 256 + 1] &= 0x7FFFu;

			// 3. If the entry is not written, the answer is unknown -
			//    and not "no zero, so free".
			written[1 * RLD_VRAM_W + 256 + 1] = 0;
			step |= (Rld_AbrVerdict(layout, pixels, written) == RLD_ABR_UNKNOWN) ? 0 : 4;
			written[1 * RLD_VRAM_W + 256 + 1] = 1;

			// 4. 16 bit is not decided at all.
			Rld_WriteLE16(&layout[0x06], 2u << 7);
			step |= (Rld_AbrVerdict(layout, pixels, written) == RLD_ABR_UNKNOWN) ? 0 : 8;

			// 5. If ABR is already 3, it stays that way.
			Rld_WriteLE16(&layout[0x06], 0x60u);
			step |= (Rld_AbrVerdict(layout, pixels, written) == RLD_ABR_ALREADY) ? 0 : 16;

			// 6. An implausible tpage - the same bits that
			//    DrawLevelOvr1P_IsPlausibleTextureLayout rejects.
			Rld_WriteLE16(&layout[0x06], 0x0200u);
			step |= (Rld_AbrVerdict(layout, pixels, written) == RLD_ABR_UNKNOWN) ? 0 : 32;

			if (step != 0)
			{
				printf("  FAIL ABR verdict: case mask %d\n", step);
				failed = 1;
			}
			else
			{
				printf("  ok   ABR verdict: used entry, unused entry, unwritten, 16 bit, already, implausible\n");
			}
		}

		free(pixels);
		free(written);
	}

	// And the VRAM reconstruction itself: a chained block must land at its
	// RECT position, and everything else must stay UNWRITTEN.
	{
		// 4-byte chain marker, 4-byte block size, 0x14 header, then 2x2 texels.
		u8 vrm[4 + 4 + 0x14 + 8];
		unsigned short *pixels = NULL;
		u8 *written = NULL;
		int bad = 0;

		memset(vrm, 0, sizeof(vrm));
		Rld_WriteLE32(&vrm[0], 0x20u);       // chain
		Rld_WriteLE32(&vrm[4], 0x14u + 8u);  // block size from the header on
		Rld_WriteLE16(&vrm[8 + 0x0c], 5);    // x
		Rld_WriteLE16(&vrm[8 + 0x0e], 7);    // y
		Rld_WriteLE16(&vrm[8 + 0x10], 2);    // w
		Rld_WriteLE16(&vrm[8 + 0x12], 2);    // h
		Rld_WriteLE16(&vrm[8 + 0x14 + 0], 0xdead);
		Rld_WriteLE16(&vrm[8 + 0x14 + 2], 0xbeef);
		Rld_WriteLE16(&vrm[8 + 0x14 + 4], 0x1234);
		Rld_WriteLE16(&vrm[8 + 0x14 + 6], 0x5678);

		if (!Rld_VramBuild(vrm, sizeof(vrm), &pixels, &written))
		{
			printf("  FAIL VRAM rebuild: out of memory\n");
			failed = 1;
		}
		else
		{
			bad |= (pixels[7 * RLD_VRAM_W + 5] == 0xdead) ? 0 : 1;
			bad |= (pixels[7 * RLD_VRAM_W + 6] == 0xbeef) ? 0 : 2;
			bad |= (pixels[8 * RLD_VRAM_W + 5] == 0x1234) ? 0 : 4;
			bad |= (pixels[8 * RLD_VRAM_W + 6] == 0x5678) ? 0 : 8;
			bad |= (written[7 * RLD_VRAM_W + 5] != 0) ? 0 : 16;
			bad |= (written[7 * RLD_VRAM_W + 4] == 0) ? 0 : 32;
			bad |= (written[9 * RLD_VRAM_W + 5] == 0) ? 0 : 64;

			if (bad != 0)
			{
				printf("  FAIL VRAM rebuild: case mask %d\n", bad);
				failed = 1;
			}
			else
			{
				printf("  ok   VRAM rebuild: rect placed, everything else stays unwritten\n");
			}
		}

		free(pixels);
		free(written);
	}

	// THE MENU MAP (shrunk in the game since 4.1): shrink it,
	// and shrunk twice must give the same byte for byte.
	//
	// An 8-bit map, drawn 95x43 texels like Inferno Island, both
	// halves equal: left transparent (index 0, color 0), right white
	// (index 1), with a red block in it (index 2). Index 3 is in the table
	// but is never used - it must not appear afterwards either.
	{
		struct RldMapHalf source[2];
		struct RldMapHalf small[2];
		struct RldMapHalf again[2];
		const char *why;
		int bad = 0;
		int k;
		int x;
		int y;

		memset(source, 0, sizeof(source));
		memset(small, 0, sizeof(small));
		memset(again, 0, sizeof(again));

		for (k = 0; k < 2; k++)
		{
			source[k].depth = 1;
			source[k].layout[4] = 95;
			source[k].layout[9] = 43;
			source[k].layout[10] = 95;
			source[k].layout[11] = 43;
			Rld_WriteLE16(&source[k].layout[6], 0x00e0u); // 8 bit, ABR 3
			source[k].w = 48;                              // u 0..95 = 96 texels
			source[k].h = 44;                              // v 0..43
			source[k].clutW = 256;
			source[k].clut[1] = 0x7fffu;
			source[k].clut[2] = 0x001fu;
			source[k].clut[3] = 0x03e0u;
			source[k].texels = (unsigned short *)calloc((size_t)source[k].w * (size_t)source[k].h, sizeof(unsigned short));

			if (source[k].texels == NULL)
			{
				bad |= 1;
				continue;
			}

			for (y = 0; y < source[k].h; y++)
			{
				for (x = 0; x < (source[k].w * 2); x++)
				{
					const u32 index = (x < 40) ? 0u : (((x >= 60) && (x < 70) && (y >= 10) && (y < 20)) ? 2u : 1u);

					source[k].texels[(y * source[k].w) + (x / 2)] |= (unsigned short)(index << ((x % 2) * 8));
				}
			}
		}

		why = (bad == 0) ? Rld_ScaleMap(source, small) : "out of memory";

		if (why != NULL)
		{
			printf("  FAIL menu map: %s\n", why);
			failed = 1;
		}
		else
		{
			int seen[4] = {0, 0, 0, 0};

			for (k = 0; k < 2; k++)
			{
				if ((small[k].layout[4] != 68) || (small[k].layout[9] != 31) || (small[k].w != 35) || (small[k].h != 32))
				{
					bad |= 2;
				}

				for (y = 0; y < small[k].h; y++)
				{
					for (x = 0; x < (small[k].w * 2); x++)
					{
						const u32 index = (small[k].texels[(y * small[k].w) + (x / 2)] >> ((x % 2) * 8)) & 0xffu;

						if (index > 3u)
						{
							bad |= 4;
						}
						else
						{
							seen[index] = 1;
						}
					}
				}

				// Left stays transparent, right white.
				if (((small[k].texels[0] & 0xffu) != 0u) || (((small[k].texels[(5 * small[k].w) + 33] >> 8) & 0xffu) != 1u))
				{
					bad |= 8;
				}
			}

			if (seen[3] || !seen[0] || !seen[1] || !seen[2])
			{
				bad |= 16;
			}

			// Shrunk twice, equal field by field - the game shrinks anew on
			// every start and must show the same map every time.
			if (Rld_ScaleMap(source, again) != NULL)
			{
				bad |= 32;
			}
			else
			{
				for (k = 0; k < 2; k++)
				{
					if ((memcmp(again[k].layout, small[k].layout, sizeof(small[k].layout)) != 0) || (again[k].w != small[k].w) ||
					    (again[k].h != small[k].h) || (again[k].depth != small[k].depth) || (again[k].clutW != small[k].clutW) ||
					    (memcmp(again[k].clut, small[k].clut, sizeof(small[k].clut)) != 0) ||
					    (memcmp(again[k].texels, small[k].texels, (size_t)small[k].w * (size_t)small[k].h * sizeof(unsigned short)) != 0))
					{
						bad |= 64;
					}
				}
			}

			if (bad != 0)
			{
				printf("  FAIL menu map: case mask %d\n", bad);
				failed = 1;
			}
			else
			{
				printf("  ok   menu map: 95x43 scaled to 68x31, only the map's own colors, two runs identical\n");
			}
		}

		Rld_FreeMap(source);
		Rld_FreeMap(small);
		Rld_FreeMap(again);
	}

	// PARM (4.1): parse switches, write, read - and what the reader
	// rejects or replaces with the default.
	{
		struct RldParm set;
		struct RldParm back;
		u8 *chunk;
		size_t size = 0;
		int bad = 0;

		if ((Rld_ParseParmValues("off", "17", "0x83,5", &set) != NULL) || (set.reverb != RLD_PARM_REVERB_OFF) || (set.bots != 17u) ||
		    (set.ambient[0] != 0x83u) || (set.ambient[1] != 5u))
		{
			bad |= 1;
		}

		chunk = Rld_BuildParm(&set, &size);
		if ((chunk == NULL) || (Rld_ParseParm(&back, chunk, size) != NULL) || (back.reverbState != RLD_PARM_SET) ||
		    (back.reverb != RLD_PARM_REVERB_OFF) || (back.botsState != RLD_PARM_SET) || (back.bots != 17u) ||
		    (back.ambientState != RLD_PARM_SET) || (back.ambient[0] != 0x83u) || (back.ambient[1] != 5u))
		{
			bad |= 2;
		}

		// The packer does not even take values outside the range.
		if ((Rld_ParseParmValues("5", NULL, NULL, &set) == NULL) || (Rld_ParseParmValues(NULL, "18", NULL, &set) == NULL) ||
		    (Rld_ParseParmValues(NULL, NULL, "0x10000", &set) == NULL) || (Rld_ParseParmValues(NULL, "-1", NULL, &set) == NULL))
		{
			bad |= 4;
		}

		// No value set: no chunk.
		memset(&set, 0, sizeof(set));
		if (Rld_BuildParm(&set, &size) != NULL)
		{
			bad |= 8;
		}

		// By hand: bots 18 (invalid -> default), an unknown key 9
		// (skipped), then the same key twice (completely broken).
		if (chunk != NULL)
		{
			u8 raw[32];

			memset(raw, 0, sizeof(raw));
			Rld_WriteLE16(&raw[0], RLD_PARM_VERSION);
			Rld_WriteLE16(&raw[2], 2u);
			Rld_WriteLE16(&raw[4], RLD_PARM_KEY_BOTS);
			Rld_WriteLE16(&raw[6], 1u);
			raw[8] = 18u;
			Rld_WriteLE16(&raw[9], 9u);
			Rld_WriteLE16(&raw[11], 2u);

			if ((Rld_ParseParm(&back, raw, 15u) != NULL) || (back.botsState != RLD_PARM_INVALID) || (back.unknownCount != 1u) ||
			    (back.firstUnknown != 9u))
			{
				bad |= 16;
			}

			Rld_WriteLE16(&raw[9], RLD_PARM_KEY_BOTS);
			if (Rld_ParseParm(&back, raw, 15u) == NULL)
			{
				bad |= 32;
			}

			Rld_WriteLE16(&raw[0], 2u);
			if (Rld_ParseParm(&back, raw, 15u) == NULL)
			{
				bad |= 64;
			}
		}

		free(chunk);

		if (bad != 0)
		{
			printf("  FAIL PARM: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   PARM: switches, round trip, out of range refused, bad value -> default, twice or foreign version refused\n");
		}
	}

	// THE MODES (4.1): battle is reserved and is refused, and a track
	// without restart points may not be a lap mode.
	{
		struct RldLevModes data;
		struct RldModelIds ids;
		char why[512];
		int bad = 0;

		memset(&data, 0, sizeof(data));
		memset(&ids, 0, sizeof(ids));

		if (Rld_ModeCheck(RLD_MODE_BATTLE, &data, &ids, why, sizeof(why)) == NULL)
		{
			bad |= 1;
		}
		if (Rld_ModeCheck(RLD_MODE_RACE, &data, &ids, why, sizeof(why)) == NULL)
		{
			bad |= 2;
		}
		data.restartPoints = 12;
		if (Rld_ModeCheck(RLD_MODE_RACE, &data, &ids, why, sizeof(why)) != NULL)
		{
			bad |= 4;
		}
		if (Rld_ModeCheck(RLD_MODE_CRYSTAL_CHALLENGE, &data, &ids, why, sizeof(why)) == NULL)
		{
			bad |= 8;
		}
		ids.crystals = 20;
		ids.letters[0] = 1;
		ids.letters[1] = 1;
		if ((Rld_ModeCheck(RLD_MODE_CRYSTAL_CHALLENGE, &data, &ids, why, sizeof(why)) != NULL) ||
		    (Rld_ModeCheck(RLD_MODE_CTR_CHALLENGE, &data, &ids, why, sizeof(why)) == NULL))
		{
			bad |= 16;
		}
		ids.letters[2] = 1;
		if (Rld_ModeCheck(RLD_MODE_CTR_CHALLENGE, &data, &ids, why, sizeof(why)) != NULL)
		{
			bad |= 32;
		}
		ids.letters[0] = 2;
		if (Rld_ModeCheck(RLD_MODE_CTR_CHALLENGE, &data, &ids, why, sizeof(why)) == NULL)
		{
			bad |= 64;
		}

		if (bad != 0)
		{
			printf("  FAIL modes: case mask %d\n", bad);
			failed = 1;
		}
		else
		{
			printf("  ok   modes: battle reserved, race needs restart points, crystal needs a crystal, ctr each letter exactly once\n");
		}
	}

	// MAKE: model ID, spawn table, SCA -> SNDB, track.txt.
	failed |= Rld_SelftestMake();

	// MAKE-CHAR: PLY and OBJ, the chain, the model rules, CHRI (tools/rldpack_char.inc).
	// It counts its failed cases; here only "any" matters.
	failed |= (RldChar_SelfTest() != 0);

	// CHAR-POSES and CHAR-WHEEL (tools/rldpack_anim.inc, tools/rldpack_wheel.inc).
	failed |= (RldAnim_SelfTest() != 0);
	failed |= (RldWheel_SelfTest() != 0);

	printf(failed ? "\nSelf-test FAILED.\n" : "\nSelf-test passed.\n");
	return failed;
}

//========================================================================================

// The help names EVERY argument build and make know. An argument an author
// can only learn about from the source is no argument - an early version of
// this help left out --author, and the first track built consequently had an
// empty author field.
static void Rld_Usage(void)
{
#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.7.5 Beta"
#endif
#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif
	// The same version and build ID as the game and Reload Studio. The build ID stays
	// an argument of its own: the packaging script looks for it in ReloadStudio.exe as a
	// string that ends there (tools/package/package.sh), not in this help.
	printf("rldpack - packer for .rldtrack track containers (format 4.1) and .rldchar character containers - CTR Reload %s (%s)\n\n",
	       CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);

	printf("  The same program runs inside Reload Studio: where this help says\n");
	printf("  \"rldpack <command>\", \"ReloadStudio.exe --rldpack <command>\" works the same.\n\n");

	printf("COMMANDS\n");
	printf("  make     <folder>           build a container from a track folder, see below\n");
	printf("  build    ...                build a container from single files, see below\n");
	printf("  make-char ...               build a character (.rldchar) from a PLY or OBJ model,\n");
	printf("                              see below\n");
	printf("  char-poses ...              check pose PLYs against a character model and\n");
	printf("                              write them for the preview, see below\n");
	printf("  char-wheel ...              the preview of a wheel model (OBJ or PLY) for\n");
	printf("                              Reload Studio, see below (the wheel of a character:\n");
	printf("                              make-char --wheel-model, preview)\n");
	printf("  info     <file>             show the format, META and PARM, leaves LEVD/VRMD untouched\n");
	printf("  verify   <file>             check every chunk against its hash, SNDB and PARM\n");
	printf("                              with the game's own reader, and warn if the track\n");
	printf("                              lacks the data of a declared ctr or crystal mode\n");
	printf("                              info and verify also read .rldchar files (chosen\n");
	printf("                              by the first bytes, not by the name) and check\n");
	printf("                              the model, the portrait, the own mask and the\n");
	printf("                              voices with the game's own rules\n");
	printf("  selftest                    check SHA-256 and the build against fixed cases\n");
	printf("  make-native-tests [--obj] <folder>\n");
	printf("                              write the test characters of the native model\n");
	printf("                              (CNET, CTXT; preview) into a folder of a CMake\n");
	printf("                              build, for the game's --char-native-selftest;\n");
	printf("                              --obj: those of the self-test's mini OBJ\n");
	printf("                              (make-char --native-model on)\n\n");
	printf("  --machine                   with any command: also print lines for a program\n");
	printf("                              (Reload Studio), see tools/reloadstudio/reloadstudio.h.\n");
	printf("                              info --machine takes several files and also reads\n");
	printf("                              the LEVD of each\n\n");

	printf("MAKE\n");
	printf("  rldpack make <folder> [switches]\n");
	printf("  The folder holds exactly one .lev and one .vrm, at most one music file\n");
	printf("  (a .sca, or a finished .sndb that goes in unchanged; optional) and\n");
	printf("  optionally track.txt. Everything else is left alone.\n");
	printf("  The container lands NEXT TO the folder as <folder>.rldtrack. make fixes\n");
	printf("  the model ids of the instances; everything else it checks as build does.\n");
	printf("  It writes no level id: the game gives every track its id itself.\n");
	printf("\n");
	printf("  --name, --author, --modes, --track-version, --reverb, --bots, --ambient\n");
	printf("                              as with build; they win over track.txt, which\n");
	printf("                              wins over the defaults (name: the folder name)\n");
	printf("  --bank <n> --song <n>       where the music of a .sca plays (default 14 and\n");
	printf("                              13); banks 0 and 54..70 are refused\n");
	printf("  --no-music                  leave the music file in the folder out\n");
	printf("  --hwl <file>                the retail KART.HWL, needed for a .sca; without\n");
	printf("                              it make looks for assets with BIGFILE.BIG or\n");
	printf("                              ctr-u.bin next to the program and up to two\n");
	printf("                              folders above, like the game\n");
	printf("  --out <file>                somewhere else than next to the folder\n");
	printf("  --keep-model-ids            leave InstDef.modelID as the exporter wrote it\n");
	printf("                              (and the ids of the letter models c, t, r)\n");
	printf("  --abr-keep                  as with build\n");
	printf("  --check                     run every check and build in memory, write nothing\n");
	printf("  --reverb/--bots/--ambient default\n");
	printf("                              not set, even if track.txt sets it\n");
	printf("\n");
	printf("  track.txt, one key = value per line, the switch names without --\n");
	printf("  (name, author, modes, track-version, bank, song, reverb, bots, ambient);\n");
	printf("  a line starting with # is a comment:\n");
	printf("    name = My Track\n");
	printf("    author = Your Name\n");
	printf("    modes = race\n");
	printf("    reverb = 2\n");
	printf("\n");

	RldChar_Usage(stdout);
	RldAnim_Usage(stdout);
	RldWheel_Usage(stdout);

	printf("BUILD - REQUIRED\n");
	printf("  --lev <file>                track geometry (.lev), 16 MB at most\n");
	printf("  --vrm <file>                textures (.vrm), 4 MB at most\n");
	printf("  --name \"<text>\"             name shown in the menu, 64 bytes at most\n");
	printf("  --out <file>                output file, .rldtrack by convention\n\n");

	printf("BUILD - OPTIONAL\n");
	printf("  --author \"<text>\"           your name or handle, 64 bytes at most\n");
	printf("                              left out, the menu shows an empty field\n");
	printf("  --sndb <file>               the track's own sound chunk, 4 MB at most\n");
	printf("                              left out, the track plays its host slot's sound\n");
	printf("  --track-version <n>         counts your own revisions, 1 or higher (default 1)\n");
	printf("  --modes <list>              which modes the track offers (default race)\n");
	printf("                              race, ctr, time, crystal - comma separated;\n");
	printf("                              battle is reserved. Each mode needs its data,\n");
	printf("                              see MODES below\n");
	printf("  --reverb <0..4|off>         the reverb step (the game's default: 2)\n");
	printf("  --bots <0..17>              the bots drive as on this retail track\n");
	printf("                              (0 Dingo Canyon .. 17 Turbo Track, default 0)\n");
	printf("  --ambient <n>[,<n>]         ambient sound numbers at the LEV's SpawnType2\n");
	printf("                              slots 5 and 6, decimal or 0x..., up to 0xffff,\n");
	printf("                              0 = none (default: none)\n");
	printf("  --abr-keep                  leave the tpage ABR field as the exporter wrote it\n\n");

	printf("MODES\n");
	printf("  A mode may only be declared when the LEV carries its data - otherwise\n");
	printf("  build and make stop before writing anything:\n");
	printf("    every mode   spawn table: slot 2 (end camera) at count 3 and more,\n");
	printf("                 slot 3 (fly-in) at count 4 and more\n");
	printf("    race         at least one restart point; without nav paths the bots\n");
	printf("                 do not drive (a warning)\n");
	printf("    time         race, and the N. Tropy / N. Oxide ghosts at slots 4 and 5\n");
	printf("    ctr          race, and the letters C, T and R, each exactly once\n");
	printf("    crystal      at least one crystal\n");
	printf("  race, crystal and ctr are playable in CTR Reload today; time is written\n");
	printf("  with a note \"not playable in CTR Reload yet\".\n\n");

	printf("MENU MAP\n");
	printf("  The menu has a strip of 32 rows for the track map. If the map halves of\n");
	printf("  the LEV (icons 3 and 4) are higher, or wider than their texture page slot,\n");
	printf("  the game scales them down itself when the row is first shown; build and\n");
	printf("  make report what it will show. The race keeps drawing the map of the LEV.\n\n");

	printf("METADATA\n");
	printf("  Name and author are all the metadata there is. License, description,\n");
	printf("  tool version and the track UUID were dropped: the reader never needed\n");
	printf("  one of them. All text is UTF-8. Nothing in a container is compressed.\n\n");

	printf("EXAMPLE\n");
	printf("  rldpack build --lev track.lev --vrm track.vrm \\\n");
	printf("                --name \"Warp Pad 1\" --author \"Your Name\" \\\n");
	printf("                --modes race --reverb 1 --out warppad1.rldtrack\n");
	printf("  rldpack verify warppad1.rldtrack\n");
	printf("  rldpack make MyTrack --author \"Your Name\"\n\n");

	printf("Drop the finished file into tracks/. That is all - no installing.\n\n");

	printf("Containers are NOT signed. The SIGN chunk, Ed25519 and the key files\n");
	printf("were taken out of the format - see include/rldtrack.inc.\n");
	printf("Older containers that still carry a SIGN or MMAP chunk keep\n");
	printf("loading; the game skips both. A container says who its author is, it does\n");
	printf("not prove it.\n");
}

// info --machine: one block per container for Reload Studio (cup editor and
// test page). Unlike info, this reads the LEVD - the cup editor has to know whether
// a track has nav paths and restart points, and that is only in there.
// The checks are the same as when building; nothing is changed.
static int Cmd_InfoMachine(int argc, char *argv[])
{
	int f;

	if (argc < 1)
	{
		fprintf(stderr, "rldpack info --machine <file.rldtrack> [<file.rldtrack> ...]\n");
		return 2;
	}

	for (f = 0; f < argc; f++)
	{
		struct RldReader reader;
		struct RldMeta meta;
		const char *error;
		u8 *data;
		size_t size = 0;
		int index = -1;
		char number[24];
		char format[16];

		// A character speaks for itself, in its own lines.
		if (Rld_IsCharFile(argv[f]))
		{
			RldChar_InfoCommand(argv[f], 1);
			continue;
		}

		error = Rld_Open(&reader, argv[f]);
		if (error != NULL)
		{
			Rld_Emit("container", argv[f], "refused", error, (const char *)NULL);
			continue;
		}

		Rld_FindEntry(&reader, "META", &index);
		data = Rld_ReadChunk(&reader, index, &size, &error);
		if ((data == NULL) || ((error = Rld_ParseMeta(&meta, data, size)) != NULL))
		{
			char text[256];

			snprintf(text, sizeof(text), "META: %s", (error != NULL) ? error : "cannot be read");
			Rld_Emit("container", argv[f], "refused", text, (const char *)NULL);
			free(data);
			Rld_Close(&reader);
			continue;
		}

		Rld_Emit("container", argv[f], "ok", "", (const char *)NULL);
		Rld_Emit("value", "name", meta.strings[0], "container", (const char *)NULL);
		Rld_Emit("value", "author", meta.strings[1], "container", (const char *)NULL);
		snprintf(number, sizeof(number), "%u", meta.trackVersion);
		Rld_Emit("value", "track_version", number, "container", (const char *)NULL);
		Rld_Emit("value", "modes", Rld_ModesWords(meta.modes), "container", (const char *)NULL);
		snprintf(format, sizeof(format), "%u.%u", reader.major, reader.minor);
		Rld_Emit("value", "format", format, "container", (const char *)NULL);
		free(data);

		index = -1;
		Rld_FindEntry(&reader, "LEVD", &index);
		data = Rld_ReadChunk(&reader, index, &size, &error);
		if (data == NULL)
		{
			Rld_Say("warning", "levd", error, "The track data in %s cannot be read (%s).", argv[f], (error != NULL) ? error : "unknown");
		}
		else
		{
			struct RldSpawn spawn;
			struct RldModelIds ids;
			struct RldLevModes levModes;

			Rld_SpawnRead(data, size, &spawn);
			Rld_ModelIds(data, size, 0, &ids);
			Rld_LevModesRead(data, size, &levModes);
			Rld_EmitModes(meta.modes, &spawn, &levModes, &ids);
			Rld_ModesDeclaredWarn(argv[f], meta.modes, &spawn, &levModes, &ids);
			free(data);
		}

		Rld_Close(&reader);
	}

	return 0;
}

// CHARACTERS: make-char, and info, verify, self-test and help for .rldchar. Here,
// after every helper it uses (s_machine, Rld_Emit, Rld_Say, Rld_ReadFile, Rld_IsUtf8,
// Rld_AddChunk, RLD_PATH_MAX, Rld_EndsWithNoCase); its entry points are declared at
// the head of COMMANDS. The reader and the model check are include/rldchar.inc.
#include "rldpack_char.inc"

// char-poses uses the PLY reader and the chain of make-char; char-wheel uses the
// PLY and OBJ readers of make-char.
#include "rldpack_anim.inc"
#include "rldpack_wheel.inc"

// Searched for --machine and removed before a command sees the arguments:
// otherwise make would report an unknown argument before the machine is on.
static int Rld_MainCommand(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	int result;
	int i;
	int kept = 0;

	for (i = 0; i < argc; i++)
	{
		if ((i > 0) && (strcmp(argv[i], "--machine") == 0))
		{
			s_machine = 1;
			continue;
		}
		argv[kept++] = argv[i];
	}
	argc = kept;
	argv[argc] = NULL;

	if (!s_machine)
	{
		return Rld_MainCommand(argc, argv);
	}

	// Unbuffered, so that report, errors and machine lines arrive in the
	// order in which they are created - stdout and stderr share
	// one pipe in Reload Studio.
	setvbuf(stdout, NULL, _IONBF, 0);
	Rld_Emit("rldpack", "1", (argc >= 2) ? argv[1] : "", (const char *)NULL);

	if ((argc >= 2) && (strcmp(argv[1], "info") == 0))
	{
		result = Cmd_InfoMachine(argc - 2, &argv[2]);
	}
	else
	{
		result = Rld_MainCommand(argc, argv);
	}

	if ((result != 0) && !s_machineResult && (argc >= 2) &&
	    ((strcmp(argv[1], "make") == 0) || (strcmp(argv[1], "build") == 0) || (strcmp(argv[1], "make-char") == 0)))
	{
		Rld_Emit("result", "failed", "", "0", "", (const char *)NULL);
	}

	{
		char code[16];

		snprintf(code, sizeof(code), "%d", result);
		Rld_Emit("end", code, (const char *)NULL);
	}
	return result;
}

static int Rld_MainCommand(int argc, char *argv[])
{
	if (argc < 2)
	{
		Rld_Usage();
		return 2;
	}

	// `keygen` was here while containers were signed. It gets its own message
	// instead of the general help: whoever types it has old instructions
	// in front of them and would otherwise look for a typo.
	if (strcmp(argv[1], "keygen") == 0)
	{
		fprintf(stderr, "rldpack: keygen is gone. Containers are not signed any more -\n");
		fprintf(stderr, "         the SIGN chunk was taken out of the format.\n");
		fprintf(stderr, "         See the head of include/rldtrack.inc. You need no key.\n");
		return 2;
	}
	if (strcmp(argv[1], "build") == 0)
	{
		return Cmd_Build(argc - 2, &argv[2]);
	}
	if (strcmp(argv[1], "make") == 0)
	{
		return Cmd_Make(argc - 2, &argv[2], argv[0]);
	}
	if (strcmp(argv[1], "make-char") == 0)
	{
		return RldChar_MakeCommand(argc - 2, &argv[2]);
	}
	if (strcmp(argv[1], "char-poses") == 0)
	{
		return RldAnim_PosesCommand(argc - 2, &argv[2]);
	}
	if (strcmp(argv[1], "char-wheel") == 0)
	{
		return RldWheel_Command(argc - 2, &argv[2]);
	}
	if (strcmp(argv[1], "info") == 0)
	{
		return Cmd_Info(argc - 2, &argv[2], 0);
	}
	if (strcmp(argv[1], "verify") == 0)
	{
		return Cmd_Info(argc - 2, &argv[2], 1);
	}
	if (strcmp(argv[1], "selftest") == 0)
	{
		return Cmd_Selftest();
	}
	if (strcmp(argv[1], "make-native-tests") == 0)
	{
		return RldChar_NativeTestsCommand(argc - 2, &argv[2]);
	}

	Rld_Usage();
	return 2;
}
