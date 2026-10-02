// rldchar.inc - the .rldchar format (custom characters), once, for both sides
//
// AN INCLUDE, NOT A MODULE, always pulled in AFTER include/rldtrack.inc. The game
// includes it in platform/native_chars.c; rldpack in tools/rldpack.c.
//
//   tools/rldpack.c           the packer (make-char, info, verify)
//   platform/native_chars.c   the game; it only reads and checks
//
// The envelope is the one of rldtrack.inc - header, directory, SHA-256 per
// chunk, the position checks - opened with Rld_OpenAs and s_rldCharFormat below.
// What this file says is what a character IS: the required chunks CHRI and
// CMDL, the name rule, and RldChar_CheckModel, the check every model passes
// before it is relocated or drawn. Shared for the reason given at the head of
// rldtrack.inc: the packer must never write a model the game refuses.
//
// docs/CONTAINER_FORMAT.md describes this format; change it in the same commit.
//
// ONLY READING, PARSING AND CHECKING. The writing side is tools/rldpack_char.inc.
// Nothing here prints: findings go back as return values and through a
// callback, and the including side decides where they appear.
//
// THE MODEL IS THE NATIVE ONE. CMDL holds a model file exactly as the loader
// reads it from the BIGFILE (LOAD_DramFileCallback, game/LOAD/LOAD_File.c:116-185):
// the renderer already draws that format, so there is no second encoder in the
// game - but a model from a stranger reaches the renderer's unchecked paths
// (vertex cache, color cache, pointer relocation). RldChar_CheckModel is the
// gate in front of them.
//
// "RB" below is game/RenderBucket/RenderBucket_QueueExecute.c.
//
// u8, u16, u32 and u64 come from the including file: the packer defines them
// itself, the game has them from include/macros.h.

#ifndef RLDCHAR_INC
#define RLDCHAR_INC

#include <stdarg.h>

//========================================================================================
// THE CONTAINER
//========================================================================================

#define RLDCHAR_MAGIC "RLDCHAR"            // 7 letters + NUL = the 8 magic bytes
#define RLDCHAR_VERSION_MAJOR 1
#define RLDCHAR_VERSION_MINOR 0            // 0 = draft until the format is frozen
#define RLDCHAR_HEADER_FLAGS_KNOWN 0u
#define RLDCHAR_LIMIT_CHRI (4u * 1024u)
#define RLDCHAR_LIMIT_CMDL (256u * 1024u)
#define RLDCHAR_LIMIT_CICN (4u * 1024u)    // the portrait, exactly RLDCHAR_ICON_BYTES (CICN-3)
#define RLDCHAR_LIMIT_CPRM (4u * 1024u)    // known type, not written yet
#define RLDCHAR_LIMIT_CMSK (256u * 1024u)  // the own mask; the envelope bound only - version 1 holds CMSK-3 (16 KiB)
#define RLDCHAR_INFO_FIXED_SIZE 0x1Cu
#define RLDCHAR_CHRI_FLAGS_OFFSET 0x1Cu    // u32 flags, present when fixedSize >= RLDCHAR_CHRI_SIZE_FLAGS
#define RLDCHAR_CHRI_SIZE_FLAGS 0x20u
#define RLDCHAR_FLAG_NO_WHEELS 0x1u        // CHRI flags bit 0: the game draws no kart wheels for this driver
#define RLDCHAR_FLAG_MASK_BITS 0x6u        // CHRI flags bits 1-2: the mask the driver wears, RLDCHAR_MASK_*
#define RLDCHAR_FLAG_MASK_SHIFT 1u
#define RLDCHAR_MASK_LIKE 0u               // like the template - every file from before the field
#define RLDCHAR_MASK_AKU 1u                // Aku Aku
#define RLDCHAR_MASK_UKA 2u                // Uka Uka; 3 is reserved and read as RLDCHAR_MASK_LIKE
#define RLDCHAR_FLAGS_KNOWN (RLDCHAR_FLAG_NO_WHEELS | RLDCHAR_FLAG_MASK_BITS)   // the CHRI flag bits this reader understands
#define RLDCHAR_NAME_FIELD 20
#define RLDCHAR_NAME_MAX 17
#define RLDCHAR_AUTHOR_MAX 64
#define RLDCHAR_TEMPLATE_MAX 14            // 15 (N. Oxide) is never a template
// The templates that wear Aku Aku in retail: Crash, Coco, Polar, Pura, Penta
// (MASK_GOOD_GUY_CHARACTER_BITS, game/Vehicle/VehPickupItem.c:39). The HUD icon
// keeps its own table without Penta (game/UI/UI_Weapon.c:22).
#define RLDCHAR_TEMPLATE_AKU_BITS 0x20C9u
#define RLDCHAR_TEMPLATE_WEARS_AKU(t) ((((u32)(t)) <= RLDCHAR_TEMPLATE_MAX) && (((RLDCHAR_TEMPLATE_AKU_BITS >> (u32)(t)) & 1u) != 0u))
#define RLDCHAR_CLASS_MAX 3
#define RLDCHAR_TNT_HEIGHT_MIN 16
#define RLDCHAR_TNT_HEIGHT_MAX 96
#define RLDCHAR_DRAW_BYTES_MAX 22560u      // 564 x 0x28 (oxide_hi)
#define RLDCHAR_DRAW_BYTES_G3 28u          // RB:3063-3070
#define RLDCHAR_DRAW_BYTES_GT3 40u         // RB:3088-3090
#define RLDCHAR_SLOT_MAX 87
#define RLDCHAR_COLORS_MAX 128
#define RLDCHAR_LOD_MIN 8192
#define RLDCHAR_ANIM_COUNT 4

// The frame counts of the four animations, by index: turn, reverse, bump,
// jump. The game logic counts them from header 0 with the mask 0x7fff
// (game/Vehicle/VehFrame.c:83) and ties penalties to two of them, so a
// character has exactly the counts every template has.
static const u16 s_rldCharFrames[RLDCHAR_ANIM_COUNT] = {21, 7, 15, 4};

// The limit of a KNOWN type, 0 for any other - the same contract as
// Rld_ChunkLimit, but its own table: a CMDL inside a .rldtrack stays an unknown
// chunk that is skipped, and a META inside a .rldchar the same.
static u64 RldChar_ChunkLimit(const u8 *type)
{
	if (memcmp(type, "CHRI", 4) == 0)
	{
		return RLDCHAR_LIMIT_CHRI;
	}
	if (memcmp(type, "CMDL", 4) == 0)
	{
		return RLDCHAR_LIMIT_CMDL;
	}
	if (memcmp(type, "CICN", 4) == 0)
	{
		return RLDCHAR_LIMIT_CICN;
	}
	if (memcmp(type, "CPRM", 4) == 0)
	{
		return RLDCHAR_LIMIT_CPRM;
	}
	if (memcmp(type, "CMSK", 4) == 0)
	{
		return RLDCHAR_LIMIT_CMSK;
	}
	return 0;
}

// OPTIONAL: CICN (portrait, RldChar_CheckIcon below) is written by rldpack
// make-char --icon; the game shows it in the driver select grid
// (platform/native_chars.c). CMSK (the own mask, RldChar_CheckMask below) is
// written by rldpack make-char --mask-model; the game draws it in place of
// the Aku Aku / Uka Uka model. CPRM (values) has a size limit above, but
// rldpack does not write it and nothing interprets it yet.
// RESERVED NAMES: CVOI (voices) and CTEX (texture data) are planned; until then
// they have no limit here and are skipped like any unknown chunk.

// The required chunks: CHRI and CMDL. CICN and CPRM are optional - a required
// chunk could never become optional again within major 1.
static const char *const s_rldCharRequired[2] = {"CHRI", "CMDL"};
static const char *const s_rldCharMissing[2] = {"CHRI is missing - required chunk", "CMDL is missing - required chunk"};

static const struct RldFormat s_rldCharFormat = {
    RLDCHAR_MAGIC,                                                    // magic
    "no RLDCHAR magic - the extension says nothing, the magic does",  // noMagicText
    RLDCHAR_VERSION_MAJOR,                                            // major
    RLDCHAR_VERSION_MINOR,                                            // minorWritten
    RLDCHAR_HEADER_FLAGS_KNOWN,                                       // flagsKnown
    2,                                                                // minChunks
    "chunk_count is outside 2..64",                                   // countText
    2,                                                                // requiredCount
    s_rldCharRequired,                                                // required
    s_rldCharMissing,                                                 // missingText
    RldChar_ChunkLimit,                                               // chunkLimit
};

//========================================================================================
// CHRI - TEMPLATE, CLASS, NAME
//========================================================================================
//
// 0x00 u16 fixedSize    length of the numeric part, at least 0x1C, a multiple of 4
// 0x02 u8  template     retail character 0..14 the character stands in for
// 0x03 u8  class        engine class 0..3
// 0x04 char[20] name    the display name, see RldChar_NameValid
// 0x18 u32 charVersion  the version for humans; the game tells versions apart by the CMDL hash
// 0x1C u32 flags        only when fixedSize >= 0x20, else 0; bit 0 RLDCHAR_FLAG_NO_WHEELS,
//                       bits 1-2 the mask (RLDCHAR_MASK_*, RldChar_Mask)
// fixedSize: u32 stringCount, then per string u32 length + bytes; string 0 is the author
//
// A new field only ever lengthens fixedSize, a new string is only ever
// appended: an older reader skips both. META had no length field and paid for
// it with a major bump. flags is the first such field: the packer writes
// fixedSize 0x20 only when a bit is set, so a file without flags keeps the
// bytes it had before, and a reader from before flags skips them and draws
// the wheels.

// The menu font: A-Z, 0-9, space and ! % ' + , - . / : < = > ? _
// (font_characterIconID, game/zGlobal_DATA.c:2597-2703). Upper case only -
// the list shows names in capitals.
static int RldChar_NameCharAllowed(u8 c)
{
	if (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')))
	{
		return 1;
	}

	return (c != 0) && (strchr(" !%'+,-./:<=>?_", c) != NULL);
}

enum
{
	RLDCHAR_NAME_OK = 0,
	RLDCHAR_NAME_EMPTY,
	RLDCHAR_NAME_LONG,
	RLDCHAR_NAME_CHARACTER,
	RLDCHAR_NAME_TAIL
};

#define RLDCHAR_NAME_TEXT_EMPTY     "the name is empty - it needs 1 to 17 characters"
#define RLDCHAR_NAME_TEXT_LONG      "the name is longer than 17 characters"
#define RLDCHAR_NAME_TEXT_CHARACTER "the name has a character outside A-Z 0-9 space ! % ' + , - . / : < = > ? _"
#define RLDCHAR_NAME_TEXT_TAIL      "the name field is not all zero after the name"

// The same failure twice: plain for the packer's name check, with the rule in
// front for CHRI. Both fixed, so whoever keeps the pointer keeps valid text.
static const char *const s_rldCharNameText[] = {NULL, RLDCHAR_NAME_TEXT_EMPTY, RLDCHAR_NAME_TEXT_LONG, RLDCHAR_NAME_TEXT_CHARACTER,
                                                RLDCHAR_NAME_TEXT_TAIL};
static const char *const s_rldCharNameRuleText[] = {NULL, "CHRI-4: " RLDCHAR_NAME_TEXT_EMPTY, "CHRI-4: " RLDCHAR_NAME_TEXT_LONG,
                                                    "CHRI-4: " RLDCHAR_NAME_TEXT_CHARACTER, "CHRI-4: " RLDCHAR_NAME_TEXT_TAIL};

static int RldChar_NameCheck(const u8 field[RLDCHAR_NAME_FIELD])
{
	int length = 0;
	int i;

	// RLDCHAR_NAME_MAX < RLDCHAR_NAME_FIELD, so the loop stops at a NUL or at
	// the 18th character, never at the end of the field.
	while (field[length] != 0)
	{
		if (length >= RLDCHAR_NAME_MAX)
		{
			return RLDCHAR_NAME_LONG;
		}
		if (!RldChar_NameCharAllowed(field[length]))
		{
			return RLDCHAR_NAME_CHARACTER;
		}
		length++;
	}

	if (length == 0)
	{
		return RLDCHAR_NAME_EMPTY;
	}

	// Zero after the NUL: one name, one byte form.
	for (i = length + 1; i < RLDCHAR_NAME_FIELD; i++)
	{
		if (field[i] != 0)
		{
			return RLDCHAR_NAME_TAIL;
		}
	}

	return RLDCHAR_NAME_OK;
}

// ONE NAME RULE FOR BOTH SIDES: 1..17 allowed characters, then NUL, all bytes
// after it 0. The packer runs it on the name the author typed (written into a
// 20-byte field first) and stops; the game refuses the file as DAMAGED. So the
// packer cannot write a name the game refuses. NULL = valid.
static const char *RldChar_NameValid(const u8 field[RLDCHAR_NAME_FIELD])
{
	return s_rldCharNameText[RldChar_NameCheck(field)];
}

struct RldCharInfo
{
	u32 fixedSize;
	u32 templateId;
	u32 classId;
	char name[RLDCHAR_NAME_FIELD + 1];
	u32 charVersion;
	u32 flags;                             // 0 when absent (fixedSize < 0x20); unknown bits kept as stored
	u32 stringCount;
	char author[RLDCHAR_AUTHOR_MAX + 1];   // "" when absent
};

// The mask of CHRI flags: RLDCHAR_MASK_LIKE, _AKU or _UKA. The reserved value 3
// reads as RLDCHAR_MASK_LIKE, so a newer packer never refuses a file here.
static u32 RldChar_Mask(u32 flags)
{
	const u32 mask = (flags & RLDCHAR_FLAG_MASK_BITS) >> RLDCHAR_FLAG_MASK_SHIFT;

	return (mask > RLDCHAR_MASK_UKA) ? RLDCHAR_MASK_LIKE : mask;
}

// CHRI-1..5. NULL = valid, else a fixed text that starts with the rule; the
// caller refuses with DAMAGED. UTF-8 of the author is checked by the packer
// only - the game shows the author nowhere but in the log.
static const char *RldChar_ParseInfo(struct RldCharInfo *out, const u8 *bytes, size_t size)
{
	size_t at;
	u32 i;
	int name;

	memset(out, 0, sizeof(*out));

	if ((bytes == NULL) || (size < (RLDCHAR_INFO_FIXED_SIZE + 4u)))
	{
		return "CHRI-1: CHRI is shorter than 0x20 bytes (fixed part and string count)";
	}

	out->fixedSize = Rld_ReadLE16(&bytes[0x00]);
	if ((out->fixedSize < RLDCHAR_INFO_FIXED_SIZE) || ((out->fixedSize & 3u) != 0u) || ((size_t)out->fixedSize > (size - 4u)))
	{
		return "CHRI-1: fixedSize is below 0x1C, not a multiple of 4, or leaves no room for the string count";
	}

	out->templateId = bytes[0x02];
	if (out->templateId > RLDCHAR_TEMPLATE_MAX)
	{
		return "CHRI-2: template is outside 0..14";
	}

	out->classId = bytes[0x03];
	if (out->classId > RLDCHAR_CLASS_MAX)
	{
		return "CHRI-3: class is outside 0..3";
	}

	name = RldChar_NameCheck(&bytes[0x04]);
	if (name != RLDCHAR_NAME_OK)
	{
		return s_rldCharNameRuleText[name];
	}
	memcpy(out->name, &bytes[0x04], RLDCHAR_NAME_FIELD);
	out->name[RLDCHAR_NAME_FIELD] = '\0';

	out->charVersion = Rld_ReadLE32(&bytes[0x18]);

	// flags has no rule of its own. CHRI-1 has already placed fixedSize inside
	// the chunk, so the 4 bytes lie in it. A bit outside RLDCHAR_FLAGS_KNOWN is
	// no finding either - a CHRI finding refuses the file, and a bit a newer
	// packer sets must not refuse it (forward compatible). It stays in
	// out->flags as stored; whoever reads flags tests only the bits it knows.
	if (out->fixedSize >= RLDCHAR_CHRI_SIZE_FLAGS)
	{
		out->flags = Rld_ReadLE32(&bytes[RLDCHAR_CHRI_FLAGS_OFFSET]);
	}

	out->stringCount = Rld_ReadLE32(&bytes[out->fixedSize]);

	// Every entry takes at least its 4 length bytes, so even a stringCount of
	// four billion runs out of chunk after size / 4 rounds.
	at = (size_t)out->fixedSize + 4u;
	for (i = 0; i < out->stringCount; i++)
	{
		u32 length;

		if (at > (size - 4u))
		{
			return "CHRI-5: the string table runs past the end of CHRI";
		}

		length = Rld_ReadLE32(&bytes[at]);
		at += 4u;

		if (length > (size - at))
		{
			return "CHRI-5: a string runs past the end of CHRI";
		}

		if (i == 0u)
		{
			if (length > RLDCHAR_AUTHOR_MAX)
			{
				return "CHRI-5: the author is longer than 64 bytes";
			}

			memcpy(out->author, &bytes[at], length);
			out->author[length] = '\0';
		}

		at += length;
	}

	return NULL;
}

//========================================================================================
// CICN - THE PORTRAIT
//========================================================================================
//
// 0x00 u16 version      1
// 0x02 u16 depth        4 (bits per texel)
// 0x04 u16 width        44
// 0x06 u16 height       26
// 0x08 u16[16] clut     RGB555 + STP bit; 0x0000 is transparent (the PS1 rule),
//                       so black is written as 0x0421
// 0x28 u16[26][11]      the texel words exactly as they lie in VRAM: 4 bits per
//                       texel, the lowest nibble is the leftmost texel
//
// 612 bytes, nothing else: the retail portrait format (44 wide, game/222.c:497-500;
// 26 high, game/native_uidecl.c:366). The game uploads it into a strip of
// VRAM of its own and draws it on the character's tile in the driver select
// grid (NativeChar_EntryPortrait, include/platform/native_chars.h); the race
// HUD, the results and the cup standings keep the template's portrait. A
// broken CICN never refuses the file - the game shows the template's
// portrait instead. Another depth or size would
// come as version 2, which an older reader drops the same way.

#define RLDCHAR_ICON_VERSION 1u
#define RLDCHAR_ICON_DEPTH 4u
#define RLDCHAR_ICON_WIDTH 44u
#define RLDCHAR_ICON_HEIGHT 26u
#define RLDCHAR_ICON_COLORS 16u
#define RLDCHAR_ICON_WORDS_AT 0x28u                        // offset of the texel words
#define RLDCHAR_ICON_ROW_WORDS (RLDCHAR_ICON_WIDTH / 4u)   // 11
#define RLDCHAR_ICON_BYTES (RLDCHAR_ICON_WORDS_AT + 2u * RLDCHAR_ICON_ROW_WORDS * RLDCHAR_ICON_HEIGHT)   // 612
#define RLDCHAR_ICON_TRANSPARENT 0x0000u
#define RLDCHAR_ICON_BLACK 0x0421u

// CICN-1..3. NULL = usable, else a fixed text that starts with the rule; the
// caller drops only the portrait. Size first: below 8 bytes there is no header.
static const char *RldChar_CheckIcon(const u8 *bytes, size_t size)
{
	if ((bytes == NULL) || (size < 8u))
	{
		return "CICN-3: CICN is not 612 bytes";
	}

	if ((Rld_ReadLE16(&bytes[0x00]) != RLDCHAR_ICON_VERSION) || (Rld_ReadLE16(&bytes[0x02]) != RLDCHAR_ICON_DEPTH))
	{
		return "CICN-1: CICN is not version 1 with 4 bits per texel";
	}

	if ((Rld_ReadLE16(&bytes[0x04]) != RLDCHAR_ICON_WIDTH) || (Rld_ReadLE16(&bytes[0x06]) != RLDCHAR_ICON_HEIGHT))
	{
		return "CICN-2: the portrait is not 44 x 26";
	}

	if (size != (size_t)RLDCHAR_ICON_BYTES)
	{
		return "CICN-3: CICN is not 612 bytes";
	}

	return NULL;
}

//========================================================================================
// CMDL - THE MODEL CHECK
//========================================================================================
//
// The chunk exactly as stored:
//
//   +0      u32 G               body size
//   +4      G bytes             body, offset 0 = struct Model
//   +4+G    u32 mapBytes        a multiple of 4
//   +8+G    mapBytes/4 x u32    body offsets of the pointer fields (the pointer map)
//
// The check runs on these bytes BEFORE relocation: a pointer field holds a body
// offset, 0 is NULL. LOAD_RunPtrMap adds the body address to every listed place
// without asking - to a 0 as well, and twice to a place listed twice
// (game/LOAD/LOAD_Assets.c:98-105). Its own check (:67-89) catches neither case
// and can be switched off (--ptr-map-unchecked); this one always runs.
//
// THE RULES, in the order they are checked. The first finding decides the
// verdict; the packer goes on and reports all of them.
//
//   before    model-size          CMDL <= 256 KiB (DAMAGED; the envelope refuses it first)
//   stage 1   model-bounds        structures lie in the body, the chunk ends right behind the map
//             model-map           the pointer map is exact
//             model-head          Model: id -1, name 1..12 characters and zero after, one header
//             model-headers       command list, colors and animations are set
//             model-zero          fields 1.0 does not use are 0
//             model-lod           maxDistanceLOD >= 8192
//             model-anims         4 animations, raw frames, no half frames
//   stage 2   model-term          the command list ends inside the body      (the list)
//             model-colors-max    word 0 (color count N) <= 128             (the list)
//             model-colors-range  the N colors lie in the body              (the list)
//             model-colors-range  color index < N                          (per command)
//             model-colorcmd      no color-only command                     (per command)
//             model-tex           texture index 0                            (per command)
//             model-cache-order   a cache read follows a write of that slot  (per command)
//             model-slots         slot <= 87                                 (per command)
//             model-slot-color    bit 27 anywhere: written slot >= ceil(N/2) (per command)
//   stage 3   model-frame-size    frame size B = 0x1C + 3 x records, rounded up to 4, fits 16 bits (once)
//             model-frame-layout  frameSize == B                             (per animation)
//             model-anim-frames   21 / 7 / 15 / 4 frames                     (per animation)
//             model-verts         the frames lie in the body                 (per animation)
//             model-frame-layout  vertexOffset == 0x1C                       (per frame)
//             model-verts         the frame carries all records              (per frame)
//             model-coords        -8192 <= pos and pos + 255 <= 8191         (per frame and axis)
//   stage 4   model-draw          draw memory of the model <= 22560 bytes
//
// The own mask (CMSK) runs the same rules with its own profile: 1 animation
// of 1 frame, at most 1960 bytes of draw memory (RldChar_CheckMask).
//
// A model whose command list has no end word has no known record count:
// everything that needs it (stage 3 sizes, stage 4) is not checked then.
// Stage 3 covers animations 0..3, the ones the game uses.

enum RldCharVerdict
{
	RLDCHAR_VERDICT_OK = 0,
	RLDCHAR_VERDICT_BAD_MODEL,     // shown as "BAD MODEL"
	RLDCHAR_VERDICT_MODEL_LIMIT,   // shown as "MODEL LIMIT"
	RLDCHAR_VERDICT_DAMAGED        // only model-size; the envelope refuses it first anyway
};

static const char *RldChar_VerdictWord(enum RldCharVerdict verdict)
{
	switch (verdict)
	{
	case RLDCHAR_VERDICT_OK:
		return "OK";
	case RLDCHAR_VERDICT_BAD_MODEL:
		return "BAD MODEL";
	case RLDCHAR_VERDICT_MODEL_LIMIT:
		return "MODEL LIMIT";
	case RLDCHAR_VERDICT_DAMAGED:
		return "DAMAGED";
	}

	// Not a value of the enum: a caller bug. Refusing is the safe word.
	return "DAMAGED";
}

struct RldCharFinding
{
	const char *rule;              // fixed id: "model-map", "model-slots", ...
	enum RldCharVerdict verdict;
	char detail[160];              // English, with the numbers
};

// Called for every violation in check order. Return 0 to stop (the game stops
// at the first), 1 to go on (the packer).
typedef int (*RldCharReport)(void *context, const struct RldCharFinding *finding);

struct RldCharModelFacts           // filled as far as the check got
{
	u32 bodySize, mapEntries, colorCount, commandCount, records, triangles, drawBytes;
	u32 frameSize[RLDCHAR_ANIM_COUNT];
	u32 frames[RLDCHAR_ANIM_COUNT];   // numFrames & 0x7fff
	u32 scale[3];                     // ModelHeader.scale as stored (16 bits each)
};

// The structures as body offsets (include/namespace_Instance.h:304-421) and not
// as C structs: the packer may run as a 64-bit program, the game has 32-bit
// pointers, and the file has them as 32-bit offsets either way.

// struct Model, include/namespace_Instance.h:401-417
#define RLDCHAR_MODEL_BYTES       0x18u
#define RLDCHAR_MODEL_NAME_END    12u     // at most 12 characters, then NUL, the rest of the 16 bytes 0
#define RLDCHAR_MODEL_NAME_BYTES  0x10u
#define RLDCHAR_MODEL_ID          0x10u   // s16
#define RLDCHAR_MODEL_NUM_HEADERS 0x12u   // s16
#define RLDCHAR_MODEL_HEADERS     0x14u   // struct ModelHeader *

// struct ModelHeader, include/namespace_Instance.h:337-385
#define RLDCHAR_HEADER_BYTES      0x40u
#define RLDCHAR_HEADER_LOD        0x14u   // maxDistanceLOD, s16 but compared as u16 (RB:1372)
#define RLDCHAR_HEADER_FLAGS      0x16u   // u16
#define RLDCHAR_HEADER_SCALE      0x18u   // 3 x s16
#define RLDCHAR_HEADER_COMMANDS   0x20u   // ptrCommandList
#define RLDCHAR_HEADER_FRAME_DATA 0x24u   // ptrFrameData, the static frame
#define RLDCHAR_HEADER_TEX_LAYOUT 0x28u   // ptrTexLayout
#define RLDCHAR_HEADER_COLORS     0x2cu   // ptrColors, vertex colors (not a CLUT)
#define RLDCHAR_HEADER_UNK3       0x30u   // delta table of the static frame (RB:2160-2164)
#define RLDCHAR_HEADER_NUM_ANIMS  0x34u   // u32
#define RLDCHAR_HEADER_ANIMS      0x38u   // ptrAnimations, numAnimations x struct ModelAnim *
#define RLDCHAR_HEADER_ANIMTEX    0x3cu   // animtex

// struct ModelAnim, include/namespace_Instance.h:316-335. The frames follow it
// directly (MODELANIM_GETFRAME).
#define RLDCHAR_ANIM_BYTES        0x18u
#define RLDCHAR_ANIM_NUM_FRAMES   0x10u   // u16, bit 15 = half frames (RB:2190-2195)
#define RLDCHAR_ANIM_FRAME_SIZE   0x12u   // s16 but read as u16 (RB:2203)
#define RLDCHAR_ANIM_DELTA        0x14u   // ptrDeltaArray, 0 = raw frames (RB:2619-2626)

// struct ModelFrame, include/namespace_Instance.h:304-314, then 3 bytes per
// record (RenderBucketCompVertex, RB:76-81).
#define RLDCHAR_FRAME_BYTES       0x1cu
#define RLDCHAR_FRAME_VERTEX_OFF  0x18u   // vertexOffset, s32
#define RLDCHAR_RECORD_BYTES      3u
#define RLDCHAR_FRAME_SIZE_MAX    0xffffu // frameSize is read as u16 (RB:2203)

// A vertex is (frame.pos + byte) << 2 in 16 bits (RB:2437-2447, 2464-2467),
// so pos and pos + 255 have to stay inside -8192..8191.
#define RLDCHAR_COORD_MIN         (-8192)
#define RLDCHAR_COORD_MAX         8191
#define RLDCHAR_RECORD_SPAN       255

// THE COMMAND WORD (RB:4596-4700 reads it):
//   bit 31  new strip                           RB:4635-4644
//   bit 30  reuse the strip's first vertex (fan) RB:4646-4654, RB:2826-2838
//   bit 29  cull side, XOR the instance flags   RB:2898-2899
//   bit 28  back-face test (NCLIP) on           RB:2884-2903
//   bit 27  color from the scratchpad cache    RB:2525-2530
//   bit 26  vertex from its cache slot, no new record   RB:2588-2596, 4616-4619
//   23-16   cache slot                          RB:4600, RB:678-681
//   15-9    color index                        RB:2523 ((cmd >> 7) & 0x1fc is index x 4)
//   8-0     texture index, 0 = no texture (G3)  RB:2985-2992
// The upper 16 bits all zero make a color-only command (RB:4608-4612).
// Word 0 of the list is the color count (RB:2509), the list ends with
// 0xFFFFFFFF (RB:4596).
#define RLDCHAR_CMD_END           0xffffffffu
#define RLDCHAR_CMD_NEW_STRIP     0x80000000u
#define RLDCHAR_CMD_COLOR_CACHE   0x08000000u
#define RLDCHAR_CMD_CACHED        0x04000000u
#define RLDCHAR_CMD_SLOT(command)    (((command) >> 16) & 0xffu)
#define RLDCHAR_CMD_COLOR(command)   (((command) >> 9) & 0x7fu)
#define RLDCHAR_CMD_TEXTURE(command) ((command) & 0x1ffu)

// One bit per body word, in the layout of struct RldLevView (rldtrack.inc), so
// that Rld_LevPointer answers "is this field listed" for models too.
// Static, not calloc'ed: the size is the CMDL limit and not a number from the
// file, and a check has no "out of memory" verdict. Not reentrant - both
// sides check on one thread.
#define RLDCHAR_WORD_BITS_BYTES ((RLDCHAR_LIMIT_CMDL >> 5) + 1u)
static u8 s_rldCharListed[RLDCHAR_WORD_BITS_BYTES];
static u8 s_rldCharKnown[RLDCHAR_WORD_BITS_BYTES];

// What a model is checked against: a character (CMDL) or the own mask (CMSK).
// Every other rule is the same for both. The texts of the character profile
// are the ones the check always had.
struct RldCharProfile
{
	const char *chunk;      // the chunk the model frame lies in, for the texts
	const char *what;       // "a character" / "a mask"
	u32 sizeMax;            // model-size
	u32 animCount;          // model-anims: exactly this many
	const u16 *frames;      // model-anim-frames, per animation
	const char *framesWho;  // model-anim-frames: whose counts these are
	u32 drawMax;            // model-draw
};

static const struct RldCharProfile s_rldCharProfileModel = {
    "CMDL",                  // chunk
    "a character",           // what
    RLDCHAR_LIMIT_CMDL,      // sizeMax
    RLDCHAR_ANIM_COUNT,      // animCount
    s_rldCharFrames,         // frames
    "every template has",    // framesWho
    RLDCHAR_DRAW_BYTES_MAX,  // drawMax
};

struct RldCharCheck
{
	const struct RldCharProfile *profile;

	const u8 *body;
	u32 bodyBytes;          // G
	const u8 *map;          // the first map entry
	u32 mapCount;

	u32 header;             // body offset of header 0, 0 = none
	u32 commands;           // ptrCommandList, 0 = none
	u32 colors;             // ptrColors, 0 = none
	u32 anims;              // ptrAnimations, 0 = none
	u32 animCount;          // numAnimations, 0 while ptrAnimations is 0

	u32 colorCount;         // N, word 0 of the command list
	int listComplete;       // the command list ends inside the body: records and triangles are known
	u32 records;            // R
	u32 trianglesG3;
	u32 trianglesGT3;

	struct RldCharFinding *first;
	RldCharReport report;
	void *context;
	struct RldCharModelFacts *facts;
	enum RldCharVerdict verdict;   // of the first finding, OK while there is none
	int stop;
};

static int RldChar_SignedHalf(u32 value)
{
	return (value >= 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

static u32 RldChar_Word(const struct RldCharCheck *check, u32 offset)
{
	return Rld_ReadLE32(&check->body[offset]);
}

// Records one violation. The first one sets the verdict and *first; without a
// listener there is nobody to tell the rest, and a listener that returns 0 has
// heard enough - both end the check.
static void RldChar_Find(struct RldCharCheck *check, const char *rule, enum RldCharVerdict verdict, const char *format, ...)
{
	struct RldCharFinding finding;
	va_list args;

	if (check->stop)
	{
		return;
	}

	finding.rule = rule;
	finding.verdict = verdict;
	va_start(args, format);
	vsnprintf(finding.detail, sizeof(finding.detail), format, args);
	va_end(args);
	finding.detail[sizeof(finding.detail) - 1] = '\0';

	if (check->verdict == RLDCHAR_VERDICT_OK)
	{
		check->verdict = verdict;
		if (check->first != NULL)
		{
			*check->first = finding;
		}
	}

	if ((check->report == NULL) || (check->report(check->context, &finding) == 0))
	{
		check->stop = 1;
	}
}

//----------------------------------------------------------------------------------------
// Stage 1

// model-bounds. Everything stage 1 reads lies in the body: Model, header 0,
// the animation table as numAnimations declares it, each ModelAnim, and the
// first word of the command list (the color count, RB:2509). The color table
// and the frames only have to START in the body here - colors start where
// ptrColors says, the frames right behind their ModelAnim (MODELANIM_GETFRAME);
// how far they reach needs counts from stage 2 and is model-colors-range,
// model-term and model-verts.
//
// A NULL pointer is not a bounds question: Model.headers is model-head, the
// header's pointers model-headers, an empty animation slot model-anims.
//
// EVERYTHING ON 4 BYTES: G and every pointer target (header, command list,
// colors, animation table, ModelAnim). A pointer field can only be listed in
// the map on a multiple of 4 (game/LOAD/LOAD_Assets.c:69, :100 round the
// entry down), and the renderer reads commands, colors and frame heads as
// 32-bit words. The frames follow suit: ModelAnim + 0x18 is aligned when the
// ModelAnim is, and frameSize is a multiple of 4 (model-frame-layout).
//
// 0 = stop. Every later rule reads through these structures; after a finding
// here nothing further can be read safely, so the check ends.
static int RldChar_CheckBounds(struct RldCharCheck *check, const u8 *cmdl, size_t size)
{
	const char *const rule = "model-bounds";
	u32 bodyBytes;
	u32 mapBytes;
	u32 header;
	u32 i;

	// The framing of LOAD_DramFileCallback (game/LOAD/LOAD_File.c:123-128,
	// :153-168): G, the body, then the map, and nothing behind it.
	if ((cmdl == NULL) || (size < 8u))
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "%s is %u bytes - shorter than its two length words", check->profile->chunk, (unsigned)size);
		return 0;
	}

	bodyBytes = Rld_ReadLE32(&cmdl[0]);
	if ((u64)bodyBytes > ((u64)size - 8u))
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the body is %u bytes, %s (%u bytes) has room for %u", (unsigned)bodyBytes, check->profile->chunk,
		             (unsigned)size, (unsigned)(size - 8u));
		return 0;
	}

	mapBytes = Rld_ReadLE32(&cmdl[4u + (size_t)bodyBytes]);
	if ((mapBytes & 3u) != 0u)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the pointer map is %u bytes - not whole 4-byte entries", (unsigned)mapBytes);
		return 0;
	}

	if ((8u + (u64)bodyBytes + (u64)mapBytes) != (u64)size)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "body and pointer map end at %llu, %s is %u bytes - it must end right behind the map",
		             (unsigned long long)(8u + (u64)bodyBytes + (u64)mapBytes), check->profile->chunk, (unsigned)size);
		return 0;
	}

	if (bodyBytes < RLDCHAR_MODEL_BYTES)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the body is %u bytes, shorter than struct Model (0x18)", (unsigned)bodyBytes);
		return 0;
	}

	if ((bodyBytes & 3u) != 0u)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the body is %u bytes - not a multiple of 4", (unsigned)bodyBytes);
		return 0;
	}

	check->body = &cmdl[4];
	check->bodyBytes = bodyBytes;
	check->map = &cmdl[8u + (size_t)bodyBytes];
	check->mapCount = mapBytes / 4u;
	check->facts->bodySize = bodyBytes;
	check->facts->mapEntries = check->mapCount;

	header = RldChar_Word(check, RLDCHAR_MODEL_HEADERS);
	if (header == 0u)
	{
		return 1;
	}

	// Only header 0: format 1.0 has exactly one (model-head), and a second one
	// is nothing this check or the game reads.
	if ((header & 3u) != 0u)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "Model.headers is %u - not a multiple of 4", (unsigned)header);
		return 0;
	}

	if (((u64)header + RLDCHAR_HEADER_BYTES) > (u64)bodyBytes)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "Model.headers is %u - a header (0x40 bytes) there ends past the body (%u)", (unsigned)header,
		             (unsigned)bodyBytes);
		return 0;
	}

	check->header = header;
	check->commands = RldChar_Word(check, header + RLDCHAR_HEADER_COMMANDS);
	check->colors = RldChar_Word(check, header + RLDCHAR_HEADER_COLORS);
	check->anims = RldChar_Word(check, header + RLDCHAR_HEADER_ANIMS);
	check->animCount = (check->anims != 0u) ? RldChar_Word(check, header + RLDCHAR_HEADER_NUM_ANIMS) : 0u;
	for (i = 0; i < 3u; i++)
	{
		check->facts->scale[i] = Rld_ReadLE16(&check->body[header + RLDCHAR_HEADER_SCALE + (i * 2u)]);
	}

	if (((check->commands | check->colors | check->anims) & 3u) != 0u)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "ptrCommandList %u, ptrColors %u, ptrAnimations %u - each must be a multiple of 4",
		             (unsigned)check->commands, (unsigned)check->colors, (unsigned)check->anims);
		return 0;
	}

	if ((check->commands != 0u) && (((u64)check->commands + 4u) > (u64)bodyBytes))
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the command list at %u has no room for its word 0 inside the body (%u)",
		             (unsigned)check->commands, (unsigned)bodyBytes);
		return 0;
	}

	if (check->colors > bodyBytes)
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the color table starts at %u, past the body (%u)", (unsigned)check->colors, (unsigned)bodyBytes);
		return 0;
	}

	if ((check->anims != 0u) && (((u64)check->anims + ((u64)check->animCount * 4u)) > (u64)bodyBytes))
	{
		RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the animation table at %u with %u entries ends past the body (%u)", (unsigned)check->anims,
		             (unsigned)check->animCount, (unsigned)bodyBytes);
		return 0;
	}

	for (i = 0; i < check->animCount; i++)
	{
		const u32 anim = RldChar_Word(check, check->anims + (i * 4u));

		if ((anim & 3u) != 0u)
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "animation %u at %u - not a multiple of 4", (unsigned)i, (unsigned)anim);
			return 0;
		}

		if ((anim != 0u) && (((u64)anim + RLDCHAR_ANIM_BYTES) > (u64)bodyBytes))
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "animation %u at %u: struct ModelAnim (0x18 bytes) ends past the body (%u)", (unsigned)i,
			             (unsigned)anim, (unsigned)bodyBytes);
			return 0;
		}
	}

	return 1;
}

#define RLDCHAR_NO_FIELD 0xffffffffu

// The pointer fields of the known structures, one per index: 0 Model.headers,
// 1..7 those of header 0, then each animation table entry, then
// ModelAnim.ptrDeltaArray per entry (RLDCHAR_NO_FIELD for a NULL entry). Known
// means: a structure the format defines and model-bounds has placed in the
// body. Fields 1.0 wants 0 (ptrFrameData, ptrTexLayout, unk3, animtex,
// ptrDeltaArray) are pointers all the same - if one is not 0, it has to be
// listed like any other, and model-zero / model-anims then say why it must not
// be set. 0 = no more fields.
static int RldChar_PointerField(const struct RldCharCheck *check, u32 index, u32 *site, const char **name)
{
	static const u32 headerFields[7] = {RLDCHAR_HEADER_COMMANDS, RLDCHAR_HEADER_FRAME_DATA, RLDCHAR_HEADER_TEX_LAYOUT, RLDCHAR_HEADER_COLORS,
	                                    RLDCHAR_HEADER_UNK3,     RLDCHAR_HEADER_ANIMS,      RLDCHAR_HEADER_ANIMTEX};
	static const char *const headerNames[7] = {"ptrCommandList", "ptrFrameData", "ptrTexLayout", "ptrColors", "unk3", "ptrAnimations", "animtex"};

	if (index == 0u)
	{
		*site = RLDCHAR_MODEL_HEADERS;
		*name = "Model.headers";
		return 1;
	}
	index--;

	if (check->header == 0u)
	{
		return 0;
	}

	if (index < 7u)
	{
		*site = check->header + headerFields[index];
		*name = headerNames[index];
		return 1;
	}
	index -= 7u;

	if (index < check->animCount)
	{
		*site = check->anims + (index * 4u);
		*name = "an animation table entry";
		return 1;
	}
	index -= check->animCount;

	if (index < check->animCount)
	{
		const u32 anim = RldChar_Word(check, check->anims + (index * 4u));

		*site = (anim != 0u) ? (anim + RLDCHAR_ANIM_DELTA) : RLDCHAR_NO_FIELD;
		*name = "ModelAnim.ptrDeltaArray";
		return 1;
	}

	return 0;
}

static int RldChar_WordBit(const u8 *bits, u32 site)
{
	return (bits[site >> 5] & (1u << ((site >> 2) & 7u))) != 0u;
}

static void RldChar_SetWordBit(u8 *bits, u32 site, int on)
{
	if (on)
	{
		bits[site >> 5] |= (u8)(1u << ((site >> 2) & 7u));
	}
	else
	{
		bits[site >> 5] &= (u8)~(1u << ((site >> 2) & 7u));
	}
}

// model-map. The map is exact: every entry is a pointer field of a known
// structure inside the body (a multiple of 4, below 0x80000000), listed once,
// holding a body offset 1..G; and every pointer field that is not 0 is listed.
//
// Why each part (game/LOAD/LOAD_Assets.c:51-108):
//   - a NULL field in the map becomes the address of the body: the game would
//     follow a pointer that should not exist (:98-105 adds to any value);
//   - a place listed twice gets the body address added twice;
//   - a field that is not 0 but not listed reaches the game as a small number
//     it uses as an address;
//   - an entry on anything else turns data into an address.
// The game rounds an entry down to a multiple of 4 (:69, :100); here a
// misaligned entry is refused, as in Rld_CheckLev.
static void RldChar_CheckMap(struct RldCharCheck *check)
{
	const char *const rule = "model-map";
	const u32 bitBytes = (check->bodyBytes >> 5) + 1u;
	struct RldLevView view;
	const char *name = NULL;
	u32 site = 0;
	u32 target = 0;
	u32 i;

	memset(s_rldCharListed, 0, bitBytes);
	memset(s_rldCharKnown, 0, bitBytes);

	// After model-bounds every structure, and with it every field, lies on a
	// multiple of 4. The test stays: a misaligned site would index the bit maps
	// wrongly, and Rld_LevPointer treats such a field as unlisted.
	for (i = 0; RldChar_PointerField(check, i, &site, &name); i++)
	{
		if ((site != RLDCHAR_NO_FIELD) && ((site & 3u) == 0u))
		{
			RldChar_SetWordBit(s_rldCharKnown, site, 1);
		}
	}

	for (i = 0; (i < check->mapCount) && !check->stop; i++)
	{
		const u32 entry = Rld_ReadLE32(&check->map[(size_t)i * 4u]);
		u32 value;

		if ((entry >= 0x80000000u) || ((entry & 3u) != 0u) || (entry > (check->bodyBytes - 4u)))
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "map entry %u is %u - not a multiple of 4 inside the body (0..%u)", (unsigned)i,
			             (unsigned)entry, (unsigned)(check->bodyBytes - 4u));
			continue;
		}

		if (RldChar_WordBit(s_rldCharListed, entry))
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "map entry %u lists %u a second time - it would be relocated twice", (unsigned)i,
			             (unsigned)entry);
			continue;
		}
		RldChar_SetWordBit(s_rldCharListed, entry, 1);

		if (!RldChar_WordBit(s_rldCharKnown, entry))
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "map entry %u lists %u, which is not a pointer field of the model", (unsigned)i,
			             (unsigned)entry);
			continue;
		}

		value = RldChar_Word(check, entry);
		if (value == 0u)
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "map entry %u lists the NULL field at %u - relocation would make it point at the model",
			             (unsigned)i, (unsigned)entry);
			continue;
		}

		if (value > check->bodyBytes)
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "the pointer at %u (map entry %u) holds %u, outside the body 0..%u", (unsigned)entry,
			             (unsigned)i, (unsigned)value, (unsigned)check->bodyBytes);
		}
	}

	// Every known field that is not 0 must be listed. Rld_LevPointer
	// (rldtrack.inc, the same question for a LEV) answers it from the listed
	// bits; the known bit is cleared on the way, so a ModelAnim that two table
	// entries share is reported once.
	view.body = check->body;
	view.bodyBytes = check->bodyBytes;
	view.pointer = s_rldCharListed;
	for (i = 0; RldChar_PointerField(check, i, &site, &name) && !check->stop; i++)
	{
		if (site == RLDCHAR_NO_FIELD)
		{
			continue;
		}

		if ((site & 3u) == 0u)
		{
			if (!RldChar_WordBit(s_rldCharKnown, site))
			{
				continue;
			}
			RldChar_SetWordBit(s_rldCharKnown, site, 0);
		}

		if (Rld_LevPointer(&view, site, &target) < 0)
		{
			RldChar_Find(check, rule, RLDCHAR_VERDICT_BAD_MODEL, "%s at %u holds %u but is not in the pointer map", name, (unsigned)site,
			             (unsigned)RldChar_Word(check, site));
		}
	}
}

// model-head, model-headers, model-zero, model-lod, model-anims: the Model, its
// one header and the animation table. 0 = stop: without a header the rest has
// nothing to check.
static int RldChar_CheckHead(struct RldCharCheck *check)
{
	const u32 header = check->header;
	u32 i;

	// model-head. The id: LOAD_GlobalModelPtrs_MPK stores every model with an
	// id other than -1 in gGT->modelPtr[id] without a bound
	// (game/LOAD/LOAD_ModelPtrs.c:9-21). The name: 1..12 characters, then NUL,
	// and every byte after the first NUL up to byte 15 is 0. The model is
	// found by comparing all 16 bytes (game/Vehicle/VehBirth.c:491-502), and
	// LOAD_GlobalModelPtrs_MPK runs in load stage 5
	// (game/LOAD/LOAD_TenStages.c:519-525), while an extra model slot still
	// holds the file start, 4 bytes before the model - there it takes name
	// bytes 12..13 for the id.
	{
		const int id = RldChar_SignedHalf(Rld_ReadLE16(&check->body[RLDCHAR_MODEL_ID]));
		const int numHeaders = RldChar_SignedHalf(Rld_ReadLE16(&check->body[RLDCHAR_MODEL_NUM_HEADERS]));
		u32 nameLength = 0;

		if (id != -1)
		{
			RldChar_Find(check, "model-head", RLDCHAR_VERDICT_BAD_MODEL, "Model.id is %d - %s model has -1", id, check->profile->what);
		}

		while ((nameLength < RLDCHAR_MODEL_NAME_BYTES) && (check->body[nameLength] != 0))
		{
			nameLength++;
		}

		if (nameLength == 0u)
		{
			RldChar_Find(check, "model-head", RLDCHAR_VERDICT_BAD_MODEL, "Model.name is empty - it has 1 to 12 characters");
		}
		else if (nameLength > RLDCHAR_MODEL_NAME_END)
		{
			RldChar_Find(check, "model-head", RLDCHAR_VERDICT_BAD_MODEL, "Model.name has more than 12 characters before its NUL");
		}
		else
		{
			for (i = nameLength + 1u; i < RLDCHAR_MODEL_NAME_BYTES; i++)
			{
				if (check->body[i] != 0)
				{
					RldChar_Find(check, "model-head", RLDCHAR_VERDICT_BAD_MODEL, "Model.name byte %u after the NUL is not 0 - bytes up to 15 are 0",
					             (unsigned)i);
					break;
				}
			}
		}

		// The renderer walks numHeaders headers (RB:1318-1325, 1363-1372);
		// format 1.0 has exactly one.
		if (numHeaders != 1)
		{
			RldChar_Find(check, "model-head", RLDCHAR_VERDICT_BAD_MODEL, "Model.numHeaders is %d - format 1.0 has exactly 1", numHeaders);
		}

		if (header == 0u)
		{
			RldChar_Find(check, "model-head", RLDCHAR_VERDICT_BAD_MODEL, "Model.headers is NULL - the model has no header");
			return 0;
		}
	}

	// model-headers. Without a command list or colors the instance is not
	// drawn (RB:5461-5466); without animations the renderer draws the static
	// frame (RB:2160-2164), which 1.0 does not have.
	if (check->commands == 0u)
	{
		RldChar_Find(check, "model-headers", RLDCHAR_VERDICT_BAD_MODEL, "ModelHeader.ptrCommandList is NULL - the renderer draws nothing");
	}
	if (check->colors == 0u)
	{
		RldChar_Find(check, "model-headers", RLDCHAR_VERDICT_BAD_MODEL, "ModelHeader.ptrColors is NULL - the renderer draws nothing");
	}
	if (check->anims == 0u)
	{
		RldChar_Find(check, "model-headers", RLDCHAR_VERDICT_BAD_MODEL, "ModelHeader.ptrAnimations is NULL - %s needs its %u animation%s",
		             check->profile->what, (unsigned)check->profile->animCount, (check->profile->animCount == 1u) ? "" : "s");
	}

	// model-zero. The static frame and its delta table (RB:2160-2164), the
	// texture table (RB:2994-3003; 1.0 has vertex colors only), the texture
	// animation the game turns every frame (game/CTR/CTR_CycleTex.c:93-95), and
	// the header flags (1 = always point north, 2 = face the camera,
	// include/namespace_Instance.h:350-354).
	{
		static const u32 zeroFields[5] = {RLDCHAR_HEADER_FRAME_DATA, RLDCHAR_HEADER_TEX_LAYOUT, RLDCHAR_HEADER_UNK3, RLDCHAR_HEADER_ANIMTEX,
		                                  RLDCHAR_HEADER_FLAGS};
		static const char *const zeroNames[5] = {"ptrFrameData", "ptrTexLayout", "unk3", "animtex", "flags"};

		for (i = 0; i < 5u; i++)
		{
			const u32 value = (zeroFields[i] == RLDCHAR_HEADER_FLAGS) ? Rld_ReadLE16(&check->body[header + zeroFields[i]])
			                                                          : RldChar_Word(check, header + zeroFields[i]);

			if (value != 0u)
			{
				RldChar_Find(check, "model-zero", RLDCHAR_VERDICT_BAD_MODEL, "ModelHeader.%s is %u - format 1.0 has it 0", zeroNames[i], (unsigned)value);
			}
		}
	}

	// model-lod. The renderer takes the first header whose maxDistanceLOD, as
	// u16, is above the projected distance; with none it draws nothing
	// (RB:1362-1372). 8192 is what the retail character models have.
	{
		const u32 lod = Rld_ReadLE16(&check->body[header + RLDCHAR_HEADER_LOD]);

		if (lod < RLDCHAR_LOD_MIN)
		{
			RldChar_Find(check, "model-lod", RLDCHAR_VERDICT_BAD_MODEL, "ModelHeader.maxDistanceLOD is %u - at least 8192, or the model vanishes at a distance",
			             (unsigned)lod);
		}
	}

	// model-anims. The game addresses the animations by index 0..3 only
	// (game/Vehicle/VehFrame.c:3-8); an empty slot is not drawn (RB:2171-2174).
	// Raw frames: 1.0 has no delta tables - the renderer would read one delta
	// word per record wherever ptrDeltaArray points (RB:2599-2601), and a
	// delta-coded model is reserved for a required-feature bit. No half
	// frames: a clamped odd frame reads one frame past the end (RB:2190-2209).
	{
		const u32 numAnimations = RldChar_Word(check, header + RLDCHAR_HEADER_NUM_ANIMS);

		if (numAnimations != check->profile->animCount)
		{
			RldChar_Find(check, "model-anims", RLDCHAR_VERDICT_BAD_MODEL, "ModelHeader.numAnimations is %u - %s has exactly %u",
			             (unsigned)numAnimations, check->profile->what, (unsigned)check->profile->animCount);
		}

		for (i = 0; i < check->animCount; i++)
		{
			const u32 anim = RldChar_Word(check, check->anims + (i * 4u));

			if (anim == 0u)
			{
				RldChar_Find(check, "model-anims", RLDCHAR_VERDICT_BAD_MODEL, "animation %u is NULL", (unsigned)i);
				continue;
			}

			if (RldChar_Word(check, anim + RLDCHAR_ANIM_DELTA) != 0u)
			{
				RldChar_Find(check, "model-anims", RLDCHAR_VERDICT_BAD_MODEL, "animation %u has a delta table - format 1.0 has raw frames (ptrDeltaArray 0)",
				             (unsigned)i);
			}

			if ((Rld_ReadLE16(&check->body[anim + RLDCHAR_ANIM_NUM_FRAMES]) & 0x8000u) != 0u)
			{
				RldChar_Find(check, "model-anims", RLDCHAR_VERDICT_BAD_MODEL, "animation %u has bit 15 of numFrames set (half frames)", (unsigned)i);
			}
		}
	}

	return 1;
}

//----------------------------------------------------------------------------------------
// Stage 2: the command list, as RenderBucket_DrawFunc_Normal walks it (RB:4584-4700)

static void RldChar_CheckCommands(struct RldCharCheck *check)
{
	const u32 list = check->commands;
	const u32 count = RldChar_Word(check, list);
	u8 written[256];
	u32 end = 0;
	u32 at;
	u32 stripLength = 0;
	u32 primCommand = 0;
	u32 colorHalf;
	int colorCacheUsed = 0;

	check->colorCount = count;
	check->facts->colorCount = count;

	// model-term. The renderer loops until 0xFFFFFFFF without any length
	// (RB:4596): without the end word inside the body it reads on through
	// foreign memory.
	for (at = list + 4u; at <= (check->bodyBytes - 4u); at += 4u)
	{
		if (RldChar_Word(check, at) == RLDCHAR_CMD_END)
		{
			end = at;
			break;
		}
	}

	if (end == 0u)
	{
		RldChar_Find(check, "model-term", RLDCHAR_VERDICT_BAD_MODEL, "the command list at %u has no end word 0xFFFFFFFF before the end of the body (%u)",
		             (unsigned)list, (unsigned)check->bodyBytes);
	}

	// model-colors-max / model-colors-range. Word 0 is how many color words
	// the renderer copies to scratchpad 0x140 before drawing, without a bound
	// (RB:2504-2518); a command indexes them with 7 bits (RB:2523), so 128 is
	// all it can ever reach. Above 128 the range is not asked: the count itself
	// is the finding.
	if (count > RLDCHAR_COLORS_MAX)
	{
		RldChar_Find(check, "model-colors-max", RLDCHAR_VERDICT_MODEL_LIMIT, "word 0 of the command list asks for %u colors, at most 128", (unsigned)count);
	}
	else if ((check->colors != 0u) && (((u64)check->colors + ((u64)count * 4u)) > (u64)check->bodyBytes))
	{
		RldChar_Find(check, "model-colors-range", RLDCHAR_VERDICT_BAD_MODEL, "the %u colors at %u end past the body (%u)", (unsigned)count,
		             (unsigned)check->colors, (unsigned)check->bodyBytes);
	}

	// The commands themselves only with an end word: the list is what lies
	// between word 0 and it.
	if (end == 0u)
	{
		return;
	}

	check->facts->commandCount = (end - list - 4u) / 4u;
	memset(written, 0, sizeof(written));

	// ceil(N / 2) without overflow: the slots below it share their bytes with
	// the cached colors (slot k = colors 2k and 2k+1, RB:678-686).
	colorHalf = (count >> 1) + (count & 1u);

	// Does any vertex command read its color from the cache (bit 27)? Asked
	// before the walk, because model-slot-color then holds for every command,
	// also for those before the first one with bit 27.
	for (at = list + 4u; at < end; at += 4u)
	{
		const u32 command = RldChar_Word(check, at);

		if (((command >> 16) != 0u) && ((command & RLDCHAR_CMD_COLOR_CACHE) != 0u))
		{
			colorCacheUsed = 1;
			break;
		}
	}

	for (at = list + 4u; (at < end) && !check->stop; at += 4u)
	{
		const u32 index = (at - list - 4u) / 4u;
		const u32 command = RldChar_Word(check, at);
		const u32 slot = RLDCHAR_CMD_SLOT(command);
		u32 drawCommand;

		// model-colors-range: the color index, from the cache or straight from
		// ptrColors (RB:2520-2531), is one of the N colors.
		if ((count <= RLDCHAR_COLORS_MAX) && (RLDCHAR_CMD_COLOR(command) >= count))
		{
			RldChar_Find(check, "model-colors-range", RLDCHAR_VERDICT_BAD_MODEL, "command %u uses color %u, the list has %u", (unsigned)index,
			             (unsigned)RLDCHAR_CMD_COLOR(command), (unsigned)count);
		}

		// model-colorcmd. Upper 16 bits zero is a color-only command, no vertex
		// (RB:4608-4612): a vertex with flags 0 in slot 0 would be read as one.
		// The renderer skips the rest for it, and so does the check.
		if ((command >> 16) == 0u)
		{
			RldChar_Find(check, "model-colorcmd", RLDCHAR_VERDICT_BAD_MODEL, "command %u (0x%08X) is a color-only command - its upper 16 bits are 0",
			             (unsigned)index, (unsigned)command);
			continue;
		}

		// model-tex. 1.0 draws vertex colors only; index 0 is the G3 path
		// (RB:2989-2992). A texture table is a later required feature.
		if (RLDCHAR_CMD_TEXTURE(command) != 0u)
		{
			RldChar_Find(check, "model-tex", RLDCHAR_VERDICT_BAD_MODEL, "command %u uses texture %u - format 1.0 has texture index 0 only", (unsigned)index,
			             (unsigned)RLDCHAR_CMD_TEXTURE(command));
		}

		// model-cache-order. Bit 26 takes the vertex from the scratchpad slot
		// and nothing else (RB:2588-2596); a slot this list has not written holds
		// whatever the previous model left there. Every other vertex command
		// reads the next record and writes its slot (RB:2628, 2479-2488).
		if ((command & RLDCHAR_CMD_CACHED) != 0u)
		{
			if (!written[slot])
			{
				RldChar_Find(check, "model-cache-order", RLDCHAR_VERDICT_BAD_MODEL, "command %u reads slot %u from the cache before any command wrote it",
				             (unsigned)index, (unsigned)slot);
			}
		}
		else
		{
			written[slot] = 1;
			check->records++;
		}

		// model-slots. Slot k lives at scratchpad 0x140 + 8k (RB:678-681); the
		// scratchpad is 0x400 bytes (include/ctr_scratchpad.h:7), so 87 is the
		// last slot. Nothing checks it at run time.
		if (slot > RLDCHAR_SLOT_MAX)
		{
			RldChar_Find(check, "model-slots", RLDCHAR_VERDICT_MODEL_LIMIT, "command %u uses slot %u, at most 87", (unsigned)index, (unsigned)slot);
		}

		// model-slot-color. The color cache and the vertex cache share the
		// scratchpad from 0x140: colors at 4 bytes each are copied there once per
		// instance (RB:2504-2518), a vertex at 8 bytes per slot is written there
		// by every command without bit 26 (RB:2628 -> 2479-2488, slot address
		// RB:678-681). A write to slot k overwrites colors 2k and 2k+1, and every
		// later command with bit 27 reads the cache, not ptrColors (RB:2520-2532).
		// So once ANY command uses bit 27, no command may write below ceil(N/2) -
		// whether it carries bit 27 itself or not. Without bit 27 anywhere the
		// cache is never read and every slot is free.
		if (colorCacheUsed && ((command & RLDCHAR_CMD_CACHED) == 0u) && (slot < colorHalf))
		{
			RldChar_Find(check, "model-slot-color", RLDCHAR_VERDICT_MODEL_LIMIT,
			             "command %u writes slot %u, below %u where %u cached colors lie (the list uses bit 27)", (unsigned)index, (unsigned)slot,
			             (unsigned)colorHalf, (unsigned)count);
		}

		// The triangles, exactly as RB:4635-4699 emits them: a strip starts at
		// bit 31 or at the first command, its first two vertices draw nothing,
		// every further one draws one triangle. The first triangle takes its
		// flags and texture from the strip's first command (RB:4636-4639, 4665).
		// Texture 0 is a POLY_G3 of 0x1C bytes (RB:3061-3070), anything else a
		// POLY_GT3 of 0x28 (RB:3072-3090) - an upper bound, since an invalid
		// texture draws nothing (RB:4681-4687). Back-face culling is left out:
		// it depends on the view.
		if (((command & RLDCHAR_CMD_NEW_STRIP) != 0u) || (stripLength == 0u))
		{
			primCommand = command;
		}
		if ((command & RLDCHAR_CMD_NEW_STRIP) != 0u)
		{
			stripLength = 0;
		}
		if (stripLength < 2u)
		{
			stripLength++;
			continue;
		}

		drawCommand = (stripLength == 2u) ? primCommand : command;
		if (RLDCHAR_CMD_TEXTURE(drawCommand) == 0u)
		{
			check->trianglesG3++;
		}
		else
		{
			check->trianglesGT3++;
		}
		stripLength++;
	}

	check->facts->records = check->records;
	check->facts->triangles = check->trianglesG3 + check->trianglesGT3;
	check->facts->drawBytes = (check->trianglesG3 * RLDCHAR_DRAW_BYTES_G3) + (check->trianglesGT3 * RLDCHAR_DRAW_BYTES_GT3);
	check->listComplete = !check->stop;
}

//----------------------------------------------------------------------------------------
// Stage 3: animations and frames
//
// Animations 0..3 only - the ones the game sets (game/Vehicle/VehFrame.c:3-8).
// A table with more entries is already a model-anims finding, and stopping at
// 4 bounds the work: frameSize 0 lets numFrames frames sit on one spot, and
// thousands of table entries may share one ModelAnim.

static void RldChar_CheckFrames(struct RldCharCheck *check)
{
	static const char axisNames[3] = {'x', 'y', 'z'};
	const u64 records = check->records;
	const int recordsKnown = check->listComplete;
	const u32 animCount = (check->animCount < check->profile->animCount) ? check->animCount : check->profile->animCount;
	u64 frameBytes = 0;
	u32 a;

	// model-frame-size. One frame is its 0x1C-byte head and 3 bytes per record,
	// rounded up to whole words; the renderer steps by frameSize read as u16
	// (RB:2203). The same for every animation, so it is said once.
	if (recordsKnown)
	{
		frameBytes = (RLDCHAR_FRAME_BYTES + (records * RLDCHAR_RECORD_BYTES) + 3u) & ~(u64)3u;
		if (frameBytes > RLDCHAR_FRAME_SIZE_MAX)
		{
			RldChar_Find(check, "model-frame-size", RLDCHAR_VERDICT_MODEL_LIMIT,
			             "a frame of %u records needs %llu bytes - frameSize has 16 bits (at most 65535)",
			             (unsigned)records, (unsigned long long)frameBytes);
		}
	}

	for (a = 0; (a < animCount) && !check->stop; a++)
	{
		const u32 anim = RldChar_Word(check, check->anims + (a * 4u));
		u32 numFrames;
		u32 frameSize;
		u32 first;
		u32 f;

		// model-anims has said it.
		if (anim == 0u)
		{
			continue;
		}

		numFrames = Rld_ReadLE16(&check->body[anim + RLDCHAR_ANIM_NUM_FRAMES]) & 0x7fffu;
		frameSize = Rld_ReadLE16(&check->body[anim + RLDCHAR_ANIM_FRAME_SIZE]);
		first = anim + RLDCHAR_ANIM_BYTES;
		check->facts->frames[a] = numFrames;
		check->facts->frameSize[a] = frameSize;

		// model-frame-layout: the stride is exactly one frame, so frame i is
		// where the renderer looks for it (RB:2202-2209).
		if (recordsKnown && (frameBytes <= RLDCHAR_FRAME_SIZE_MAX) && ((u64)frameSize != frameBytes))
		{
			RldChar_Find(check, "model-frame-layout", RLDCHAR_VERDICT_BAD_MODEL, "animation %u: frameSize is %u, %u records need %u", (unsigned)a,
			             (unsigned)frameSize, (unsigned)records, (unsigned)frameBytes);
		}

		// model-anim-frames, counted like the game counts them (mask 0x7fff,
		// game/Vehicle/VehFrame.c:83).
		if (numFrames != check->profile->frames[a])
		{
			RldChar_Find(check, "model-anim-frames", RLDCHAR_VERDICT_MODEL_LIMIT, "animation %u has %u frames - %s %u", (unsigned)a,
			             (unsigned)numFrames, check->profile->framesWho, (unsigned)check->profile->frames[a]);
		}

		// model-verts: all frames of the animation lie in the body. The frame
		// index is clamped to numFrames - 1 (RB:2197-2200), never to the body.
		if (((u64)first + ((u64)numFrames * frameSize)) > (u64)check->bodyBytes)
		{
			RldChar_Find(check, "model-verts", RLDCHAR_VERDICT_BAD_MODEL, "animation %u: %u frames of %u bytes from %u end past the body (%u)", (unsigned)a,
			             (unsigned)numFrames, (unsigned)frameSize, (unsigned)first, (unsigned)check->bodyBytes);
			continue;
		}

		for (f = 0; (f < numFrames) && !check->stop; f++)
		{
			const u32 frame = first + (f * frameSize);
			u32 vertexOffset;
			u32 axis;

			// Only a frameSize below 0x1C gets here: a stride shorter than the
			// frame head.
			if (((u64)frame + RLDCHAR_FRAME_BYTES) > (u64)check->bodyBytes)
			{
				RldChar_Find(check, "model-verts", RLDCHAR_VERDICT_BAD_MODEL, "animation %u frame %u: its 0x1C-byte head ends past the body", (unsigned)a,
				             (unsigned)f);
				break;
			}

			// model-frame-layout: the records start right behind the head. The
			// next frame is read with THIS frame's vertexOffset (RB:5529-5534),
			// so one value for all.
			vertexOffset = RldChar_Word(check, frame + RLDCHAR_FRAME_VERTEX_OFF);
			if (recordsKnown && (frameBytes <= RLDCHAR_FRAME_SIZE_MAX) && (vertexOffset != RLDCHAR_FRAME_BYTES))
			{
				RldChar_Find(check, "model-frame-layout", RLDCHAR_VERDICT_BAD_MODEL, "animation %u frame %u: vertexOffset is %d, it must be 0x1C", (unsigned)a,
				             (unsigned)f, (int)vertexOffset);
			}

			// model-verts: the renderer reads one 3-byte record per command
			// without bit 26, record after record, without a bound (RB:2619-2626,
			// 4616-4619).
			if (recordsKnown && ((vertexOffset >= 0x80000000u) || (((u64)vertexOffset + (records * RLDCHAR_RECORD_BYTES)) > (u64)frameSize)))
			{
				RldChar_Find(check, "model-verts", RLDCHAR_VERDICT_BAD_MODEL, "animation %u frame %u: %u records from offset %d do not fit its %u bytes",
				             (unsigned)a, (unsigned)f, (unsigned)records, (int)vertexOffset, (unsigned)frameSize);
			}

			// model-coords, per axis: (pos + byte) << 2 must stay a 16-bit value.
			for (axis = 0; axis < 3u; axis++)
			{
				const int pos = RldChar_SignedHalf(Rld_ReadLE16(&check->body[frame + (axis * 2u)]));

				if ((pos < RLDCHAR_COORD_MIN) || ((pos + RLDCHAR_RECORD_SPAN) > RLDCHAR_COORD_MAX))
				{
					RldChar_Find(check, "model-coords", RLDCHAR_VERDICT_BAD_MODEL, "animation %u frame %u: pos.%c is %d - it must lie in -8192..7936",
					             (unsigned)a, (unsigned)f, axisNames[axis], pos);
				}
			}
		}
	}
}

//----------------------------------------------------------------------------------------
// THE CHECK

// A model frame (u32 G, body, u32 mapBytes, map) against a profile; see
// RldChar_CheckModel for the contract.
static enum RldCharVerdict RldChar_CheckProfile(const struct RldCharProfile *profile, const u8 *cmdl, size_t size, struct RldCharFinding *first,
                                                RldCharReport report, void *context, struct RldCharModelFacts *facts)
{
	struct RldCharCheck check;
	struct RldCharModelFacts localFacts;

	memset(&check, 0, sizeof(check));
	check.profile = profile;
	check.first = first;
	check.report = report;
	check.context = context;
	check.facts = (facts != NULL) ? facts : &localFacts;
	check.verdict = RLDCHAR_VERDICT_OK;
	memset(check.facts, 0, sizeof(*check.facts));

	// No finding: rule NULL, verdict OK, detail "".
	if (first != NULL)
	{
		memset(first, 0, sizeof(*first));
	}

	// model-size. The envelope refuses a larger CMDL before anything is read
	// (Rld_OpenAs with RldChar_ChunkLimit); here it bounds every offset below
	// and the static bit maps (sized for CMDL, which no profile exceeds).
	if (size > profile->sizeMax)
	{
		RldChar_Find(&check, "model-size", RLDCHAR_VERDICT_DAMAGED, "%s is %llu bytes, at most %u", profile->chunk, (unsigned long long)size,
		             (unsigned)profile->sizeMax);
		return check.verdict;
	}

	// Stage 1.
	if (!RldChar_CheckBounds(&check, cmdl, size))
	{
		return check.verdict;
	}

	RldChar_CheckMap(&check);

	if (!RldChar_CheckHead(&check) || check.stop)
	{
		return check.verdict;
	}

	// Stage 2.
	if (check.commands != 0u)
	{
		RldChar_CheckCommands(&check);
	}

	// Stage 3.
	if ((check.anims != 0u) && !check.stop)
	{
		RldChar_CheckFrames(&check);
	}

	// Stage 4: model-draw. The draw memory of one frame is shared by HUD,
	// particles, every instance and the track (game/MAIN/MainInit.c:83-141); a
	// character costs at most what retail's largest, oxide_hi, costs, the own
	// mask what the retail mask akumouth costs.
	if (check.listComplete && !check.stop && (check.facts->drawBytes > profile->drawMax))
	{
		RldChar_Find(&check, "model-draw", RLDCHAR_VERDICT_MODEL_LIMIT, "%u triangles (%u G3 x 28 + %u GT3 x 40) cost %u bytes of draw memory, at most %u",
		             (unsigned)check.facts->triangles, (unsigned)check.trianglesG3, (unsigned)check.trianglesGT3, (unsigned)check.facts->drawBytes,
		             (unsigned)profile->drawMax);
	}

	return check.verdict;
}

// The CMDL chunk exactly as stored in the file. The FIRST violation decides
// the verdict and is copied to *first (may be NULL). report is called for
// every violation in check order (may be NULL: then the check ends at the
// first). facts (may be NULL) is filled as far as the check got.
//
// NOT REENTRANT: the pointer-map check works in the static bit maps
// s_rldCharListed and s_rldCharKnown. Never run two checks at once (two
// threads, or a check from inside the report callback).
static enum RldCharVerdict RldChar_CheckModel(const u8 *cmdl, size_t size, struct RldCharFinding *first, RldCharReport report, void *context,
                                              struct RldCharModelFacts *facts)
{
	return RldChar_CheckProfile(&s_rldCharProfileModel, cmdl, size, first, report, context, facts);
}

//========================================================================================
// CMSK - THE OWN MASK
//========================================================================================
//
// 0x00 u16 version      1
// 0x02 u16 flags        0 (reserved, e.g. for a textured mask later)
// 0x04 ...              the model in the frame of CMDL: u32 G, body, u32 mapBytes, map
//
// OPTIONAL. In a race the game draws this model instead of the Aku Aku / Uka
// Uka model of the bound seat's mask item; everything else of the mask - which
// of the two it is (CHRI flags bits 1-2), the beam, the sound, the music, the
// invincibility - stays (game/Vehicle/VehPickupItem.c). The retail mask is
// rigid in a race (its "speak" animation only moves in the adventure hints),
// so the own mask has one animation of one frame.
//
// THE RULES, in the order they are checked:
//
//   CMSK-1   version 1 (a reader that does not know the version drops CMSK)
//   CMSK-2   flags 0
//   CMSK-3   CMSK <= 16 KiB
//   then     every model-* rule of CMDL on the model frame at 0x04, with
//            these differences: exactly 1 animation of 1 frame, and the draw
//            memory at most 1960 bytes - what the retail mask akumouth costs
//            (49 textured triangles x 40), so 70 triangles with vertex colors.
//
// A finding NEVER refuses the file: it costs only the own mask, and the game
// shows the retail mask (like a broken CICN). The envelope bound of CMSK
// (RLDCHAR_LIMIT_CMSK) is wider than CMSK-3 on purpose: a later, larger
// version is dropped as a mask, not as a file.

#define RLDCHAR_MASK_VERSION 1u
#define RLDCHAR_MASK_HEAD_BYTES 4u                 // version and flags, then the model frame
#define RLDCHAR_MASK_BYTES_MAX (16u * 1024u)       // CMSK-3
#define RLDCHAR_MASK_DRAW_BYTES_MAX 1960u          // 49 x 0x28 (akumouth)
#define RLDCHAR_MASK_ANIM_COUNT 1

static const u16 s_rldCharMaskFrames[RLDCHAR_MASK_ANIM_COUNT] = {1};

static const struct RldCharProfile s_rldCharProfileMask = {
    "CMSK",                       // chunk
    "a mask",                     // what
    RLDCHAR_MASK_BYTES_MAX,       // sizeMax
    RLDCHAR_MASK_ANIM_COUNT,      // animCount
    s_rldCharMaskFrames,          // frames
    "a mask has",                 // framesWho
    RLDCHAR_MASK_DRAW_BYTES_MAX,  // drawMax
};

// The CMSK chunk exactly as stored in the file; the contract of
// RldChar_CheckModel, and NOT REENTRANT the same way. CMSK-1..3 come as
// findings with that rule (verdict BAD MODEL, CMSK-3 MODEL LIMIT) before the
// model rules. facts describe the model frame.
static enum RldCharVerdict RldChar_CheckMask(const u8 *cmsk, size_t size, struct RldCharFinding *first, RldCharReport report, void *context,
                                             struct RldCharModelFacts *facts)
{
	struct RldCharFinding finding;
	const char *rule = NULL;
	enum RldCharVerdict verdict = RLDCHAR_VERDICT_BAD_MODEL;

	memset(&finding, 0, sizeof(finding));
	if (facts != NULL)
	{
		memset(facts, 0, sizeof(*facts));
	}

	if ((cmsk == NULL) || (size < RLDCHAR_MASK_HEAD_BYTES) || (Rld_ReadLE16(&cmsk[0x00]) != RLDCHAR_MASK_VERSION))
	{
		rule = "CMSK-1";
		snprintf(finding.detail, sizeof(finding.detail), "CMSK is not version 1 (%u bytes)", (unsigned)size);
	}
	else if (Rld_ReadLE16(&cmsk[0x02]) != 0u)
	{
		rule = "CMSK-2";
		snprintf(finding.detail, sizeof(finding.detail), "CMSK flags are 0x%04x - version 1 has 0", (unsigned)Rld_ReadLE16(&cmsk[0x02]));
	}
	else if (size > RLDCHAR_MASK_BYTES_MAX)
	{
		rule = "CMSK-3";
		verdict = RLDCHAR_VERDICT_MODEL_LIMIT;
		snprintf(finding.detail, sizeof(finding.detail), "CMSK is %llu bytes, at most %u", (unsigned long long)size, (unsigned)RLDCHAR_MASK_BYTES_MAX);
	}

	// The head decides alone: without it the model frame is not where 1.0
	// says. One finding, then the check ends - for the packer as well.
	if (rule != NULL)
	{
		finding.rule = rule;
		finding.verdict = verdict;
		if (first != NULL)
		{
			*first = finding;
		}
		if (report != NULL)
		{
			(void)report(context, &finding);
		}
		return verdict;
	}

	return RldChar_CheckProfile(&s_rldCharProfileMask, &cmsk[RLDCHAR_MASK_HEAD_BYTES], size - RLDCHAR_MASK_HEAD_BYTES, first, report, context, facts);
}

#endif // RLDCHAR_INC
