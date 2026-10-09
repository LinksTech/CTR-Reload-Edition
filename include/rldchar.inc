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
#define RLDCHAR_LIMIT_CMDL (4u * 1024u * 1024u)   // room for RLDCHAR_DRAW_BYTES_MAX at 3 records per triangle (47 raw frames)
#define RLDCHAR_LIMIT_CICN (4u * 1024u)    // the portrait, exactly RLDCHAR_ICON_BYTES (CICN-3)
#define RLDCHAR_LIMIT_CPRM (4u * 1024u)    // known type, not written yet
#define RLDCHAR_LIMIT_CMSK (256u * 1024u)  // the own mask; the envelope bound only - version 1 holds CMSK-3 (16 KiB)
#define RLDCHAR_LIMIT_CVOI (6u * 1024u * 1024u)   // the voices; CVOI-1..4 allow at most 5293176 bytes (RLDCHAR_VOICE_BYTES_MAX)
#define RLDCHAR_INFO_FIXED_SIZE 0x1Cu
#define RLDCHAR_CHRI_FLAGS_OFFSET 0x1Cu    // u32 flags, present when fixedSize >= RLDCHAR_CHRI_SIZE_FLAGS
#define RLDCHAR_CHRI_SIZE_FLAGS 0x20u
#define RLDCHAR_FLAG_NO_WHEELS 0x1u        // CHRI flags bit 0: the game draws no kart wheels for this driver
#define RLDCHAR_FLAG_MASK_BITS 0x6u        // CHRI flags bits 1-2: the mask the driver wears, RLDCHAR_MASK_*
#define RLDCHAR_FLAG_MASK_SHIFT 1u
#define RLDCHAR_MASK_LIKE 0u               // like the template - every file from before the field
#define RLDCHAR_MASK_AKU 1u                // Aku Aku
#define RLDCHAR_MASK_UKA 2u                // Uka Uka; 3 is reserved and read as RLDCHAR_MASK_LIKE
#define RLDCHAR_FLAG_MAP_COLOR 0x8u        // CHRI flags bit 3: the minimap marker has the color at 0x20 (RldChar_MapColor)
#define RLDCHAR_FLAG_FULL_HEIGHT 0x10u     // CHRI flags bit 4: the up axis uses odd values too, the game keeps bit 0 of the height
#define RLDCHAR_FLAGS_KNOWN (RLDCHAR_FLAG_NO_WHEELS | RLDCHAR_FLAG_MASK_BITS | RLDCHAR_FLAG_MAP_COLOR | RLDCHAR_FLAG_FULL_HEIGHT)   // the CHRI flag bits this reader understands
#define RLDCHAR_CHRI_MAP_COLOR_OFFSET 0x20u   // u8 r, g, b, 0 - present when fixedSize >= RLDCHAR_CHRI_SIZE_MAP_COLOR
#define RLDCHAR_CHRI_SIZE_MAP_COLOR 0x24u
#define RLDCHAR_CHRI_LOOK_OFFSET 0x24u     // the look (shadow and exhaust, RldChar_ParseLook) - present when fixedSize >= RLDCHAR_CHRI_SIZE_LOOK
#define RLDCHAR_CHRI_SIZE_LOOK 0x3Cu
#define RLDCHAR_CHRI_SHADOW_MODE 0x24u     // u8 RLDCHAR_LOOK_RETAIL / _AUTO / _OFF
#define RLDCHAR_CHRI_EXHAUST_MODE 0x25u    // u8 RLDCHAR_LOOK_RETAIL / _CUSTOM / _OFF
#define RLDCHAR_CHRI_EXHAUST_COUNT 0x26u   // u8 1 or 2 with custom, else 0; 0x27 is reserved (0)
#define RLDCHAR_CHRI_SHADOW_QUAD 0x28u     // s16 xMin, xMax, zMin, zMax
#define RLDCHAR_CHRI_EXHAUST_POINTS 0x30u  // s16 x, y, z of point 0, then of point 1
#define RLDCHAR_LOOK_RETAIL 0u             // shadowMode / exhaustMode 0: what retail draws - every file from before the field
#define RLDCHAR_LOOK_AUTO 1u               // shadowMode 1: the quad at 0x28
#define RLDCHAR_LOOK_CUSTOM 1u             // exhaustMode 1: the points at 0x30
#define RLDCHAR_LOOK_OFF 2u                // shadowMode / exhaustMode 2: none drawn
#define RLDCHAR_LOOK_COORD_MAX 4096        // |value| of a look coordinate in 1/16 model units (256 model units, as RLDCHAR_WHEEL_POINT_MAX)
#define RLDCHAR_LOOK_Y_MIN (-256)          // the lowest exhaust height, 1/16 model units (16 model units below the origin)
#define RLDCHAR_LOOK_EDGE_MIN 16           // the shortest side of the shadow quad, 1/16 model units (1 model unit)
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
#define RLDCHAR_DRAW_BYTES_MAX 201600u     // 7200 x 0x1C: the most triangles any mesh can be built at (frameSize, 3 records each); reserved per bound seat (NativeChar_DrawReserve)
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
// chunk that is skipped, and a META inside a .rldchar the same. CNET and CTXT
// are not in it on purpose: the envelope treats them as unknown chunks, and
// only the reader of the native part (RldChar_ReadNative, behind
// --native-preview) checks them, with RldChar_NativeLimit.
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
	if (memcmp(type, "CVOI", 4) == 0)
	{
		return RLDCHAR_LIMIT_CVOI;
	}
	return 0;
}

// OPTIONAL: CICN (portrait, RldChar_CheckIcon below) is written by rldpack
// make-char --icon; the game shows it in the driver select grid
// (platform/native_chars.c). CMSK (the own mask, RldChar_CheckMask below) is
// written by rldpack make-char --mask-model; the game draws it in place of
// the Aku Aku / Uka Uka model. CVOI (the voices, RldChar_CheckVoices below) is
// written by rldpack make-char --voices; the game plays its clips for the
// bound seat. CPRM (values) has a size limit above, but rldpack does not
// write it and nothing interprets it yet.
// PREVIEW: CNET (the native model) and CTXT (its textures), see the section
// CNET AND CTXT at the end of this file. RESERVED NAMES: CTEX (once planned
// for texture data; the textures of the native model are CTXT) and CWHL
// (own wheels; the native wheels are a part of CNET) - both stay unassigned
// and are skipped like any unknown chunk.

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
//                       bits 1-2 the mask (RLDCHAR_MASK_*, RldChar_Mask),
//                       bit 3 RLDCHAR_FLAG_MAP_COLOR, bit 4 RLDCHAR_FLAG_FULL_HEIGHT
// 0x20 u8[4] mapColor   only when fixedSize >= 0x24: red, green, blue of the
//                       minimap marker, then 0; used only with bit 3 set
// The look, only when fixedSize >= 0x3C (RldChar_ParseLook):
// 0x24 u8  shadowMode   RLDCHAR_LOOK_RETAIL 0, _AUTO 1 (the quad at 0x28), _OFF 2
// 0x25 u8  exhaustMode  RLDCHAR_LOOK_RETAIL 0, _CUSTOM 1 (the points at 0x30), _OFF 2
// 0x26 u8  exhaustCount 1 or 2 with custom, else 0
// 0x27 u8  reserved     written 0, not checked
// 0x28 s16[4] shadowQuad    xMin, xMax, zMin, zMax, 1/16 model units; only with shadow auto
// 0x30 s16[3] exhaust0      x, y, z, 1/16 model units; in place of retail source 0 (+X, left)
// 0x36 s16[3] exhaust1      x, y, z; in place of retail source 1 (-X, right), only with count 2, else 0
// fixedSize: u32 stringCount, then per string u32 length + bytes; string 0 is the author
//
// A new field only ever lengthens fixedSize, a new string is only ever
// appended: an older reader skips both. META had no length field and paid for
// it with a major bump. flags is the first such field: the packer writes
// fixedSize 0x20 only when a bit is set, so a file without flags keeps the
// bytes it had before, and a reader from before flags skips them and draws
// the wheels. mapColor is the second: fixedSize 0x24 only when a color is
// chosen (flags then holds RLDCHAR_FLAG_MAP_COLOR), and a reader from before
// it accepts the file (bit 3 is an unknown bit there, the 4 bytes lie behind
// the fields it reads) and shows the template's color.
//
// The look is the third: the ground shadow and the exhaust of the driver,
// fitted to the model. fixedSize 0x3C only when a mode is not retail (the
// 4 bytes of mapColor are then written as 0 without bit 3), so a file
// without a look keeps its bytes, and a reader from before the look skips
// the 24 bytes and draws the retail shadow and exhaust. The look has NO
// CHRI rule - a CHRI finding refuses the whole file. An unknown mode or a
// value outside the limits only makes that one effect retail (RldChar_ParseLook
// names the reason in look.why). The space is the model's own: model units
// (CMDL without the instance scale = CNET, Blender 1 m = 64), origin = the
// model's origin, +X left seen from behind, +Y up, +Z forward. The game
// keys the look on the model, so CMDL and native drawing read the same values.
// Retail in these units: exhaust smoke (+-288, 896, -896), shadow quad
// x -800..800, z -820..1040.
//
// RLDCHAR_FLAG_FULL_HEIGHT (bit 4) is about the bytes of CMDL and CMSK. The
// renderer packs the up axis as (pos.y + byte) << 2 and the retail path clears
// bit 0 of that sum (mask 0xfff8ffff, RenderBucket_PackModelVertexXY), so a
// retail model has 128 heights, every one even. With the bit, the game draws
// the models of this file with mask 0xfffcffff: odd heights stay, and the Y
// bytes can take all 256 values at the same scale. The contract of the bit:
// pos.y even in every frame, so the bounding box (RenderBucket_ProjectFrameBounds,
// which still clears bit 0 of pos.y and spans 255 from there) holds every
// vertex. Half frames, the only other path that clears the bit, are refused by
// model-anims anyway. A reader from before the bit draws an odd height one
// value lower.

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
	u32 mapColor;                          // the 4 bytes at 0x20 as a little-endian u32, 0 when absent (fixedSize < 0x24)
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

// The color of the minimap marker: 1 with *color = 0x00BBGGRR - the byte order
// of the file is the order of the game's color words (data.colors,
// game/zGlobal_DATA.c), so the bytes are used as they are; byte 3 is not
// read. 0 (and *color untouched) without RLDCHAR_FLAG_MAP_COLOR or without
// the field: the template's color. Neither case is a finding.
static int RldChar_MapColor(const struct RldCharInfo *info, u32 *color)
{
	if (((info->flags & RLDCHAR_FLAG_MAP_COLOR) == 0u) || (info->fixedSize < RLDCHAR_CHRI_SIZE_MAP_COLOR))
	{
		return 0;
	}

	*color = info->mapColor & 0x00ffffffu;
	return 1;
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

	// mapColor the same way: inside the chunk once CHRI-1 holds, no rule.
	if (out->fixedSize >= RLDCHAR_CHRI_SIZE_MAP_COLOR)
	{
		out->mapColor = Rld_ReadLE32(&bytes[RLDCHAR_CHRI_MAP_COLOR_OFFSET]);
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

// The look of CHRI (0x24..0x3B), as RldChar_ParseLook read it. shadow is
// RLDCHAR_LOOK_RETAIL, _AUTO or _OFF; exhaust RLDCHAR_LOOK_RETAIL, _CUSTOM or
// _OFF; count 1 or 2 with custom, else 0. quad (xMin, xMax, zMin, zMax) only
// with auto, point[0] with custom, point[1] with custom and count 2; every
// unused value is 0. All in 1/16 model units. why: NULL, or the fixed text
// of the first effect that fell back to retail (shadow before exhaust).
struct RldCharLook
{
	u32 shadow;
	u32 exhaust;
	u32 count;
	s16 quad[4];
	s16 point[2][3];
	const char *why;
};

static int RldChar_LookValue(const u8 *chri, u32 offset)
{
	const u32 value = Rld_ReadLE16(&chri[offset]);

	return (value >= 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

// The look of a CHRI that RldChar_ParseInfo has accepted (info is its
// result, chri and size the same chunk). Without the field (fixedSize below
// 0x3C) everything is retail and why stays NULL. NO RULE: an unknown mode or
// a value outside the limits makes only that effect retail and names it in
// why, the file stays valid - wheels hidden or not.
//   shadow auto: every |value| <= RLDCHAR_LOOK_COORD_MAX, xMin < xMax and
//                zMin < zMax, each side at least RLDCHAR_LOOK_EDGE_MIN.
//   exhaust custom: count 1 or 2; per used point |x|, |z| <= RLDCHAR_LOOK_COORD_MAX
//                and RLDCHAR_LOOK_Y_MIN <= y <= RLDCHAR_LOOK_COORD_MAX.
// The reserved byte 0x27, the count without custom and the unused values are
// not checked.
static void RldChar_ParseLook(const struct RldCharInfo *info, const u8 *chri, size_t size, struct RldCharLook *out)
{
	u32 shadow;
	u32 exhaust;
	u32 count;
	int v[4];
	int p[2][3];
	int i;
	int k;

	memset(out, 0, sizeof(*out));

	if ((info == NULL) || (chri == NULL) || (info->fixedSize < RLDCHAR_CHRI_SIZE_LOOK) || (size < (size_t)RLDCHAR_CHRI_SIZE_LOOK))
	{
		return;
	}

	shadow = chri[RLDCHAR_CHRI_SHADOW_MODE];
	exhaust = chri[RLDCHAR_CHRI_EXHAUST_MODE];
	count = chri[RLDCHAR_CHRI_EXHAUST_COUNT];

	if (shadow == RLDCHAR_LOOK_AUTO)
	{
		int beyond = 0;

		for (i = 0; i < 4; i++)
		{
			v[i] = RldChar_LookValue(chri, RLDCHAR_CHRI_SHADOW_QUAD + (2u * (u32)i));
			if ((v[i] < -RLDCHAR_LOOK_COORD_MAX) || (v[i] > RLDCHAR_LOOK_COORD_MAX))
			{
				beyond = 1;
			}
		}

		if (beyond)
		{
			out->why = "shadow quad beyond 256 model units";
		}
		else if (((v[1] - v[0]) < RLDCHAR_LOOK_EDGE_MIN) || ((v[3] - v[2]) < RLDCHAR_LOOK_EDGE_MIN))
		{
			out->why = "shadow quad narrower or shorter than 1 model unit";
		}
		else
		{
			out->shadow = RLDCHAR_LOOK_AUTO;
			for (i = 0; i < 4; i++)
			{
				out->quad[i] = (s16)v[i];
			}
		}
	}
	else if (shadow == RLDCHAR_LOOK_OFF)
	{
		out->shadow = RLDCHAR_LOOK_OFF;
	}
	else if (shadow != RLDCHAR_LOOK_RETAIL)
	{
		out->why = "shadow mode unknown";
	}

	if (exhaust == RLDCHAR_LOOK_CUSTOM)
	{
		int bad = 0;

		if ((count < 1u) || (count > 2u))
		{
			if (out->why == NULL)
			{
				out->why = "exhaust count not 1 or 2";
			}
			return;
		}

		for (k = 0; k < (int)count; k++)
		{
			for (i = 0; i < 3; i++)
			{
				p[k][i] = RldChar_LookValue(chri, RLDCHAR_CHRI_EXHAUST_POINTS + (6u * (u32)k) + (2u * (u32)i));
			}

			if ((p[k][0] < -RLDCHAR_LOOK_COORD_MAX) || (p[k][0] > RLDCHAR_LOOK_COORD_MAX) || (p[k][1] < RLDCHAR_LOOK_Y_MIN) ||
			    (p[k][1] > RLDCHAR_LOOK_COORD_MAX) || (p[k][2] < -RLDCHAR_LOOK_COORD_MAX) || (p[k][2] > RLDCHAR_LOOK_COORD_MAX))
			{
				bad = 1;
			}
		}

		if (bad)
		{
			if (out->why == NULL)
			{
				out->why = "exhaust point beyond the limits";
			}
			return;
		}

		out->exhaust = RLDCHAR_LOOK_CUSTOM;
		out->count = count;
		for (k = 0; k < (int)count; k++)
		{
			for (i = 0; i < 3; i++)
			{
				out->point[k][i] = (s16)p[k][i];
			}
		}
	}
	else if (exhaust == RLDCHAR_LOOK_OFF)
	{
		out->exhaust = RLDCHAR_LOOK_OFF;
	}
	else if ((exhaust != RLDCHAR_LOOK_RETAIL) && (out->why == NULL))
	{
		out->why = "exhaust mode unknown";
	}
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
//                       so black is written as 0x0421. Every portrait is drawn
//                       semi-transparent: a word with the STP bit (0x8000)
//                       halves what lies behind it, one without is opaque
// 0x28 u16[26][11]      the texel words exactly as they lie in VRAM: 4 bits per
//                       texel, the lowest nibble is the leftmost texel
//
// 612 bytes, nothing else: the retail portrait format (44 wide, game/222.c:497-500;
// 26 high, game/native_uidecl.c:366). The game uploads it into a strip of
// VRAM of its own and draws it where the template's portrait would be: on
// the character's tile in the driver select grid (NativeChar_EntryPortrait)
// and for the seat in the race HUD, the results and the cup standings
// (NativeChar_SeatPortrait, include/platform/native_chars.h). A broken CICN
// never refuses the file - the game shows the template's portrait instead. Another depth or size would
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
//   before    model-size          CMDL <= 4 MiB (DAMAGED; the envelope refuses it first)
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
//   stage 4   model-draw          draw memory of the model <= RLDCHAR_DRAW_BYTES_MAX
//
// The own mask (CMSK) runs the same rules with its own profile: 1 animation
// of 1 frame, at most RLDCHAR_MASK_DRAW_BYTES_MAX bytes of draw memory (RldChar_CheckMask).
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
	u32 sizeMax;            // model-size; at most RLDCHAR_LIMIT_CMDL (the bit maps)
	u32 animCount;          // model-anims: exactly this many; at most RLDCHAR_ANIM_COUNT (the facts)
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
		// (RB:2989-2992). A texture table INSIDE CMDL (texture indices here)
		// would still be a later required feature: a reader of 1.0 refuses
		// such a model right here. The textures of the native model are
		// another thing - CTXT, an optional chunk, no feature bit.
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
	const u32 profileAnims = (check->profile->animCount < RLDCHAR_ANIM_COUNT) ? check->profile->animCount : RLDCHAR_ANIM_COUNT;
	const u32 animCount = (check->animCount < profileAnims) ? check->animCount : profileAnims;
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
	// particles, every instance and the track (game/MAIN/MainInit.c:83-141).
	// A custom model does not take from that share: the game adds what the
	// bound seats may draw on top of it (NativeChar_DrawReserve), so the bound
	// here is a budget of the PC, not one of retail's models.
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
//            memory at most RLDCHAR_MASK_DRAW_BYTES_MAX - 400 triangles with vertex colors,
//            reserved on top like the model (NativeChar_DrawReserve).
//
// A finding NEVER refuses the file: it costs only the own mask, and the game
// shows the retail mask (like a broken CICN). The envelope bound of CMSK
// (RLDCHAR_LIMIT_CMSK) is wider than CMSK-3 on purpose: a later, larger
// version is dropped as a mask, not as a file. The envelope rules (size above
// RLDCHAR_LIMIT_CMSK, compression, size_stored != size_raw) still refuse the
// file, as for every known chunk.

#define RLDCHAR_MASK_VERSION 1u
#define RLDCHAR_MASK_HEAD_BYTES 4u                 // version and flags, then the model frame
#define RLDCHAR_MASK_BYTES_MAX (16u * 1024u)       // CMSK-3
#define RLDCHAR_MASK_DRAW_BYTES_MAX 11200u         // 400 x 0x1C
#define RLDCHAR_MASK_ANIM_COUNT 1

static const u16 s_rldCharMaskFrames[RLDCHAR_MASK_ANIM_COUNT] = {1};

// What every profile has to keep: the facts hold RLDCHAR_ANIM_COUNT
// animations, and the static bit maps of the map check are sized for
// RLDCHAR_LIMIT_CMDL. A profile beyond either does not compile.
typedef char RldChar_MaskAnimsFitTheFacts[(RLDCHAR_MASK_ANIM_COUNT <= RLDCHAR_ANIM_COUNT) ? 1 : -1];
typedef char RldChar_MaskFitsTheBitMaps[(RLDCHAR_MASK_BYTES_MAX <= RLDCHAR_LIMIT_CMDL) ? 1 : -1];

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

//========================================================================================
// CVOI - THE VOICES
//========================================================================================
//
// 0x00 u32 version      1
// 0x04 u32 rate         22050, the sample rate of every clip (mono, s16)
// 0x08 u32 clipCount    1..40
// 0x0C u32 reserved     0
// 0x10 10 x { u8 first; u8 count; u16 reserved }    the events, in the order of
//                       s_rldCharVoiceEvents: clips [first, first + count)
// 0x38 clipCount x 28   the clips:
//        +0x00 char[16] name     printable ASCII (0x20..0x7E), 1..15 characters,
//                                then NUL up to the end (for the log only)
//        +0x10 u32 offset        from the start of the chunk
//        +0x14 u32 frames        samples of the clip
//        +0x18 u16 volume        0..256, 256 = as recorded
//        +0x1A u16 reserved      0
// then the samples, s16 little-endian, clip after clip in table order
//
// OPTIONAL. Events 0..7 are the retail voice sets by index (data.voiceID[],
// game/zGlobal_DATA.c), events 8 and 9 the two short sounds the retail driver
// plays at once (OtherFX, game/HOWL/HOWL_Voiceline.c). The game picks a clip
// of an event the way retail picks an XA line of the set; an event without
// clips is silent - never the template's voice.
//
// THE RULES, in the order they are checked:
//
//   CVOI-1   the head: at least 16 bytes, version 1, rate 22050, clipCount
//            1..40, reserved 0
//   CVOI-2   the event table: complete, count 0..4, reserved 0, and first is
//            the sum of the counts before it (an empty event too) - so the
//            events share the clips 0..clipCount-1 in order, each clip in
//            exactly one event, and the counts add up to clipCount
//   CVOI-3   the clip table: complete; name as above; offset even and not
//            inside the tables; frames 1..77175 (3.5 s) for events 0..7,
//            1..22050 (1.0 s) for events 8 and 9; volume 0..256; reserved 0
//   CVOI-4   the samples: the first clip starts at the end of the clip table,
//            every next one where the one before ends, and the last one ends
//            at the end of the chunk
//
// A finding NEVER refuses the file: it costs only the voices, and the driver
// is silent (like a broken CICN). The envelope rules (size above
// RLDCHAR_LIMIT_CVOI, compression, size_stored != size_raw) still refuse the
// file, as for every known chunk. The envelope bound is a little wider than
// the largest CVOI the rules allow (RLDCHAR_VOICE_BYTES_MAX).

#define RLDCHAR_VOICE_VERSION 1u
#define RLDCHAR_VOICE_RATE 22050u
#define RLDCHAR_VOICE_EVENTS 10
#define RLDCHAR_VOICE_PER_EVENT 4
#define RLDCHAR_VOICE_CLIPS_MAX 40
#define RLDCHAR_VOICE_LINE_FRAMES 77175u           // 3.5 s at 22050 Hz
#define RLDCHAR_VOICE_SHORT_FRAMES 22050u          // 1.0 s at 22050 Hz
#define RLDCHAR_VOICE_SHORT_FIRST 8                // events 8 and 9 are the short sounds
#define RLDCHAR_VOICE_VOLUME_FULL 256u
#define RLDCHAR_VOICE_NAME_BYTES 16u
#define RLDCHAR_VOICE_HEAD_BYTES 0x10u
#define RLDCHAR_VOICE_EVENT_BYTES 4u
#define RLDCHAR_VOICE_CLIPS_AT (RLDCHAR_VOICE_HEAD_BYTES + RLDCHAR_VOICE_EVENTS * RLDCHAR_VOICE_EVENT_BYTES)   // 0x38
#define RLDCHAR_VOICE_CLIP_BYTES 28u
#define RLDCHAR_VOICE_BYTES_MAX                                                                                                         \
	(RLDCHAR_VOICE_CLIPS_AT + RLDCHAR_VOICE_CLIPS_MAX * RLDCHAR_VOICE_CLIP_BYTES +                                                      \
	 2u * RLDCHAR_VOICE_PER_EVENT * (RLDCHAR_VOICE_SHORT_FIRST * RLDCHAR_VOICE_LINE_FRAMES +                                             \
	                                 (RLDCHAR_VOICE_EVENTS - RLDCHAR_VOICE_SHORT_FIRST) * RLDCHAR_VOICE_SHORT_FRAMES))   // 5293176

// The largest CVOI the rules allow fits the envelope bound, and every event
// can hold its 4 clips. A change beyond either does not compile.
typedef char RldChar_VoicesFitTheLimit[(RLDCHAR_VOICE_BYTES_MAX <= RLDCHAR_LIMIT_CVOI) ? 1 : -1];
typedef char RldChar_VoiceClipsFitTheEvents[(RLDCHAR_VOICE_CLIPS_MAX == RLDCHAR_VOICE_EVENTS * RLDCHAR_VOICE_PER_EVENT) ? 1 : -1];

// The event keys, by event number (rldpack --voice, the Studio, the log).
static const char *const s_rldCharVoiceEvents[RLDCHAR_VOICE_EVENTS] = {"boost", "hit",     "spin", "bigair",    "drop",
                                                                       "shield", "passing", "fire", "short-yes", "short-hit"};

struct RldCharVoiceClip
{
	char name[RLDCHAR_VOICE_NAME_BYTES + 1u];   // NUL-terminated
	u32 offset;                                 // from the start of the chunk; the samples are bytes + offset, s16 LE
	u32 frames;
	u16 volume;                                 // 0..256
};

struct RldCharVoices
{
	u32 clipCount;
	u8 first[RLDCHAR_VOICE_EVENTS];
	u8 count[RLDCHAR_VOICE_EVENTS];
	struct RldCharVoiceClip clip[RLDCHAR_VOICE_CLIPS_MAX];
};

// CVOI-1..4 on the chunk exactly as stored. NULL = usable (out filled), else a
// fixed text that starts with the rule ("CVOI-2: ...") and out is all zero;
// the caller drops only the voices. out may be NULL for the check alone.
static const char *RldChar_CheckVoices(const u8 *bytes, u64 size, struct RldCharVoices *out)
{
	struct RldCharVoices voices;
	u32 clipCount;
	u32 tableEnd;
	u32 sum = 0u;
	u64 at;
	u32 e;
	u32 c;

	memset(&voices, 0, sizeof(voices));
	if (out != NULL)
	{
		memset(out, 0, sizeof(*out));
	}

	if ((bytes == NULL) || (size < (u64)RLDCHAR_VOICE_HEAD_BYTES))
	{
		return "CVOI-1: CVOI is shorter than its 16-byte head";
	}
	if (Rld_ReadLE32(&bytes[0x00]) != RLDCHAR_VOICE_VERSION)
	{
		return "CVOI-1: CVOI is not version 1";
	}
	if (Rld_ReadLE32(&bytes[0x04]) != RLDCHAR_VOICE_RATE)
	{
		return "CVOI-1: the sample rate is not 22050";
	}
	clipCount = Rld_ReadLE32(&bytes[0x08]);
	if ((clipCount < 1u) || (clipCount > (u32)RLDCHAR_VOICE_CLIPS_MAX))
	{
		return "CVOI-1: the clip count is outside 1..40";
	}
	if (Rld_ReadLE32(&bytes[0x0C]) != 0u)
	{
		return "CVOI-1: the reserved head field is not 0";
	}

	if (size < (u64)RLDCHAR_VOICE_CLIPS_AT)
	{
		return "CVOI-2: the event table is cut off";
	}
	for (e = 0u; e < (u32)RLDCHAR_VOICE_EVENTS; e++)
	{
		const u8 *event = &bytes[RLDCHAR_VOICE_HEAD_BYTES + e * RLDCHAR_VOICE_EVENT_BYTES];

		if (event[1] > (u8)RLDCHAR_VOICE_PER_EVENT)
		{
			return "CVOI-2: an event has more than 4 clips";
		}
		if (Rld_ReadLE16(&event[2]) != 0u)
		{
			return "CVOI-2: the reserved field of an event is not 0";
		}
		if ((u32)event[0] != sum)
		{
			return "CVOI-2: the events do not share the clips in order without a gap";
		}
		voices.first[e] = event[0];
		voices.count[e] = event[1];
		sum += event[1];
	}
	if (sum != clipCount)
	{
		return "CVOI-2: the clips of the events do not add up to the clip count";
	}

	tableEnd = RLDCHAR_VOICE_CLIPS_AT + clipCount * RLDCHAR_VOICE_CLIP_BYTES;
	if (size < (u64)tableEnd)
	{
		return "CVOI-3: the clip table is cut off";
	}
	for (e = 0u; e < (u32)RLDCHAR_VOICE_EVENTS; e++)
	{
		const u32 framesMax = (e >= (u32)RLDCHAR_VOICE_SHORT_FIRST) ? RLDCHAR_VOICE_SHORT_FRAMES : RLDCHAR_VOICE_LINE_FRAMES;

		for (c = voices.first[e]; c < (u32)voices.first[e] + voices.count[e]; c++)
		{
			const u8 *clip = &bytes[RLDCHAR_VOICE_CLIPS_AT + c * RLDCHAR_VOICE_CLIP_BYTES];
			u32 length = 0u;
			u32 i;

			while ((length < RLDCHAR_VOICE_NAME_BYTES) && (clip[length] != 0u))
			{
				if ((clip[length] < 0x20u) || (clip[length] > 0x7Eu))
				{
					return "CVOI-3: a clip name has a character outside printable ASCII";
				}
				length++;
			}
			if (length == 0u)
			{
				return "CVOI-3: a clip name is empty";
			}
			if (length == RLDCHAR_VOICE_NAME_BYTES)
			{
				return "CVOI-3: a clip name is not NUL-terminated";
			}
			for (i = length; i < RLDCHAR_VOICE_NAME_BYTES; i++)
			{
				if (clip[i] != 0u)
				{
					return "CVOI-3: a clip name field is not all zero after the name";
				}
			}
			memcpy(voices.clip[c].name, clip, length);
			voices.clip[c].offset = Rld_ReadLE32(&clip[0x10]);
			voices.clip[c].frames = Rld_ReadLE32(&clip[0x14]);
			voices.clip[c].volume = (u16)Rld_ReadLE16(&clip[0x18]);
			if (((voices.clip[c].offset & 1u) != 0u) || (voices.clip[c].offset < tableEnd))
			{
				return "CVOI-3: a clip offset is odd or inside the tables";
			}
			if ((voices.clip[c].frames < 1u) || (voices.clip[c].frames > framesMax))
			{
				return "CVOI-3: a clip is empty or longer than its event allows";
			}
			if (voices.clip[c].volume > RLDCHAR_VOICE_VOLUME_FULL)
			{
				return "CVOI-3: a clip volume is above 256";
			}
			if (Rld_ReadLE16(&clip[0x1A]) != 0u)
			{
				return "CVOI-3: the reserved field of a clip is not 0";
			}
		}
	}

	at = tableEnd;
	for (c = 0u; c < clipCount; c++)
	{
		if ((u64)voices.clip[c].offset != at)
		{
			return "CVOI-4: the samples of a clip do not follow the one before";
		}
		at += 2u * (u64)voices.clip[c].frames;
		if (at > size)
		{
			return "CVOI-4: the samples of a clip run past the end of the chunk";
		}
	}
	if (at != size)
	{
		return "CVOI-4: there are bytes behind the last clip";
	}

	voices.clipCount = clipCount;
	if (out != NULL)
	{
		*out = voices;
	}
	return NULL;
}

//========================================================================================
// CNET AND CTXT - THE NATIVE MODEL (PREVIEW, PLANNED)
//========================================================================================
//
// Two optional chunks for the native render path: CNET, a mesh in floats
// (positions, normals, UV, materials, texture indices, optional wheels), and
// CTXT, the table of its textures (PNG, decoded to RGBA8). PREVIEW: the game
// reads and draws them only with the native preview (NATIVE DRIVERS set to
// PREVIEW on the GRAPHICS page, or --native-preview) at a resolution of 2X or
// more; otherwise it draws the CMDL.
//
// NOT KNOWN TO THE ENVELOPE. RldChar_ChunkLimit above does not name CNET and
// CTXT, so Rld_OpenAs treats them as unknown chunks: it checks only their
// position, a doubled type and an overlap (envelope steps 14, 18, 19) - the
// same an older reader checks. A file with CNET/CTXT therefore opens, is
// refused or loads exactly as it would without them, in this build and in
// every build before it. Only those three generic findings refuse the whole
// file (DAMAGED); everything below - compression, sizes, the limits
// RLDCHAR_LIMIT_CNET/CTXT, the hashes, every rule CNET-1..13 and CTXT-1..8 -
// costs only the native part: the driver keeps its CMDL. This replaces the
// earlier plan of a required feature bit for a texture table.
//
// THE UNITS are those of CMDL without the instance scale: game units
// ((pos + byte) x ModelHeader.scale / 4096), +X left seen from behind, +Y up,
// +Z forward, triangles wound counter-clockwise seen from outside. Every
// number little-endian; floats are IEEE 754 binary32.
//
// RldChar_ReadNative is the one entry: the game (platform/native_chars.c,
// only for a bound seat and only with --native-preview) and rldpack (info,
// verify, the self-test) run it. Nothing here prints.
//
// CNET version 1:
//   0x00 u16 version          1
//   0x02 u16 flags            0
//   0x04 u16 headerBytes      >= 0x40, a multiple of 4; the section table follows
//   0x06 u16 sectionCount     1..16
//   0x08 u32 vertexCount N    3..RLDCHAR_NET_VERTICES_MAX
//   0x0C u32 triangleCount T  1..RLDCHAR_NET_TRIANGLES_MAX
//   0x10 u16 indexSize        2 or 4 (bytes per index)
//   0x12 u16 materialCount M  1..RLDCHAR_NET_MATERIALS_MAX
//   0x14 u16 poseCount        0 (one still pose for every frame) or 47 (21/7/15/4, the CMDL frames in order)
//   0x16 u16 poseEncoding     1 = float position and float normal per vertex (24 bytes)
//   0x18 f32[3] hullMin, 0x24 f32[3] hullMax - every pose position lies inside
//   0x30 u16 textureCount     the entries CTXT must have; 0 = no CTXT
//   0x32 u16 reserved 0, 0x34 u32[3] reserved 0
// Section table at headerBytes, sectionCount x 16 bytes:
//   char[4] tag, u16 version, u16 stride, u32 offset (from the chunk start, a multiple of 4), u32 size
// Sections of version 1 (an unknown tag is skipped):
//   POSN required  stride 24  max(1, poseCount) x N x {f32 position[3], f32 normal[3]}
//   TRIS required  stride 3 x indexSize, T x 3 indices
//   TMAT required  stride 2   T x u16 material
//   MATL required  stride 16  M x {u8 rgba[4], s16 texture (-1 none), u8 alphaMode 0 opaque 1 mask 2 blend,
//                             u8 flags (bit 0: always nearest), u8 reserved[8] 0}
//   UV00 optional  stride 8   N x f32[2]; required when a material has a texture
//   COL0 optional  stride 4   N x RGBA8
//   WHLS optional  stride 0   the wheels (C.7.4), see RLDCHAR_WHEEL_*; required unless CHRI hides the wheels.
//                             Its own version: 1 the test wheel, 2 an author's wheel (rule CNET-12), 3 an
//                             author's wheel with a rear wheel, axle offsets or "always draw"
//   MRPH optional  stride 0   the shape keys win and lose as deltas of the neutral pose (rule CNET-13);
//                             another version than 1 is skipped like an unknown tag
//
// CTXT version 1:
//   0x00 u16 version 1, 0x02 u16 flags 0, 0x04 u16 count 1..16, 0x06 u16 entryBytes 0x40, 0x08 u32[2] reserved 0
//   0x10 count x 0x40 entries:
//     0x00 u8 format 1 (PNG), 0x01 u8 reserved 0, 0x02 u16 flags (RLDCHAR_TEX_*),
//     0x04 u16 width, 0x06 u16 height (powers of two 16..2048),
//     0x08 u32 offset, 0x0C u32 size (the PNG bytes, from the chunk start),
//     0x10 u8[32] SHA-256 of those bytes, 0x30 u8[16] reserved 0

#include <rldpng.inc>

#define RLDCHAR_LIMIT_CNET (32u * 1024u * 1024u)   // the bound of the chunk; a larger one costs only the native part
#define RLDCHAR_LIMIT_CTXT (32u * 1024u * 1024u)
#define RLDCHAR_NET_VERSION 1u
#define RLDCHAR_NET_HEAD_BYTES 0x40u
#define RLDCHAR_NET_SECTION_BYTES 16u
#define RLDCHAR_NET_SECTIONS_MAX 16u
#define RLDCHAR_NET_VERTICES_MAX 20000u            // budgets of the PC (renderer concept C.10), start values
#define RLDCHAR_NET_TRIANGLES_MAX 30000u
#define RLDCHAR_NET_MATERIALS_MAX 64u
#define RLDCHAR_NET_POSES 47u                      // 21 + 7 + 15 + 4, the frames of a character
#define RLDCHAR_NET_NEUTRAL_POSE 10u               // turn frame 10, no steering: the base pose of MRPH (a still model: its one pose)
#define RLDCHAR_NET_POSE_FLOAT 1u                  // poseEncoding 1
#define RLDCHAR_NET_VERTEX_BYTES 24u
#define RLDCHAR_NET_MATERIAL_BYTES 16u
#define RLDCHAR_NET_ALPHA_MAX 2u                   // 0 opaque, 1 mask, 2 blend
#define RLDCHAR_NET_MATERIAL_NEAREST 0x1u
#define RLDCHAR_NET_NORMAL_MIN 0.98                // |normal|^2 of a unit normal, with float rounding room
#define RLDCHAR_NET_NORMAL_MAX 1.02

#define RLDCHAR_WHEEL_HEAD_BYTES 0x30u
#define RLDCHAR_WHEEL_VERTEX_BYTES 32u             // f32 position[3], f32 normal[3], f32 uv[2]
#define RLDCHAR_WHEEL_VERTICES_MAX 2048u
#define RLDCHAR_WHEEL_TRIANGLES_MAX 1024u          // one mesh for all four wheels (renderer concept C.10)
#define RLDCHAR_WHEEL_RADIUS_MAX 64.0              // model units
#define RLDCHAR_WHEEL_POINT_MAX 256.0              // |x|, |y|, |z| of a wheel point, model units
#define RLDCHAR_WHEEL_SLACK 1.0e-4                 // relative and absolute room of the cylinder test
#define RLDCHAR_WHEEL_VERSION_TEST 1u              // WHLS version 1: the test wheel (self-tests, --native-probe)
#define RLDCHAR_WHEEL_VERSION_USER 2u              // WHLS version 2: an author's wheel (make-char --wheel-model), rule CNET-12
#define RLDCHAR_WHEEL_VERSION_USER3 3u             // WHLS version 3: version 2 and its extension (rear wheel, axles, flags)
#define RLDCHAR_WHEEL_TEXTURE_EDGE_MAX 1024u       // CNET-12: each side of the texture of a version 2 or 3 wheel
#define RLDCHAR_WHEEL_ALWAYS_DRAW 0x1u             // WHLS 3 flags bit 0: drawn at every level of detail
#define RLDCHAR_WHEEL_REAR_MESH 0x2u               // WHLS 3 flags bit 1: the rear wheels have a mesh of their own
#define RLDCHAR_WHEEL_FLAGS_KNOWN 0x3u
#define RLDCHAR_WHEEL_EXTRA_BYTES 0x30u            // the head of the WHLS 3 extension
#define RLDCHAR_AXLE_DZ_MIN (-32.0)                // WHLS 3 axle offsets, model units (the retail wheel has radius 16)
#define RLDCHAR_AXLE_DZ_MAX 32.0
#define RLDCHAR_AXLE_DY_MIN (-16.0)
#define RLDCHAR_AXLE_DY_MAX 32.0
#define RLDCHAR_AXLE_TRACK_MIN (-32.0)             // the change of the whole track: each wheel moves by half of it
#define RLDCHAR_AXLE_TRACK_MAX 64.0

#define RLDCHAR_MORPH_VERSION 1u
#define RLDCHAR_MORPH_HEAD_BYTES 0x10u
#define RLDCHAR_MORPH_WIN 0                        // the targets of MRPH, in their order in the section
#define RLDCHAR_MORPH_LOSE 1
#define RLDCHAR_MORPH_TARGETS 2u
#define RLDCHAR_MORPH_NORMALS 0x1u                 // MRPH flags bit 0: normal deltas follow the position deltas

#define RLDCHAR_TEX_VERSION 1u
#define RLDCHAR_TEX_HEAD_BYTES 0x10u
#define RLDCHAR_TEX_ENTRY_BYTES 0x40u
#define RLDCHAR_TEX_COUNT_MAX 16u
#define RLDCHAR_TEX_EDGE_MIN 16u
#define RLDCHAR_TEX_EDGE_MAX 2048u
#define RLDCHAR_TEX_GPU_BYTES_MAX (64u * 1024u * 1024u)   // RGBA8 with every mip level, all textures of a file
#define RLDCHAR_TEX_FORMAT_PNG 1u
#define RLDCHAR_TEX_LINEAR 0x1u                    // flags bit 0: linear; clear = sRGB
#define RLDCHAR_TEX_WRAP_U_SHIFT 1u                // bits 1-2: 0 repeat, 1 clamp, 2 mirror
#define RLDCHAR_TEX_WRAP_V_SHIFT 3u                // bits 3-4: the same for V
#define RLDCHAR_TEX_NEAREST 0x20u                  // bit 5: always nearest
#define RLDCHAR_TEX_ALPHA_SHIFT 6u                 // bits 6-7: 0 opaque, 1 mask, 2 blend
#define RLDCHAR_TEX_FLAGS_KNOWN 0xFFu

// What RldChar_ReadNative found.
enum
{
	RLDCHAR_NATIVE_NONE = 0,    // no CNET: nothing native (a CTXT alone is not read)
	RLDCHAR_NATIVE_READY = 1,   // CNET and CTXT checked, the textures decoded
	RLDCHAR_NATIVE_REFUSED = 2, // rule and detail say why; the driver keeps its CMDL
};

// The hull of one CMDL frame in model units, as the renderer bounds it: per
// axis pos..pos + 255 (pos.y with bit 0 cleared) times scale / 4096.
struct RldCharHull
{
	double lo[3];
	double hi[3];
};

struct RldCharTexture
{
	u32 width;
	u32 height;
	u32 flags;                  // RLDCHAR_TEX_*
	u8 *rgba;                   // width x height x 4, rows from the top, straight alpha; owned
};

// The checked mesh: counts and pointers into cnet (the chunk as stored,
// little-endian, 4-byte aligned sections).
struct RldCharNative
{
	int state;                  // RLDCHAR_NATIVE_*
	const char *rule;           // REFUSED: "CNET-7", "CTXT-4", "native-chunk", ...
	char detail[192];           // REFUSED: English, with the numbers

	u8 *cnet;                   // owned
	size_t cnetSize;
	u32 vertexCount;
	u32 triangleCount;
	u32 indexSize;
	u32 materialCount;
	u32 poseCount;              // 0 or RLDCHAR_NET_POSES
	u32 textureCount;
	float hullMin[3];
	float hullMax[3];
	const u8 *poses;            // max(1, poseCount) x vertexCount x 24 bytes
	const u8 *uv;               // vertexCount x 8 bytes, or NULL
	const u8 *colors;           // vertexCount x 4 bytes, or NULL
	const u8 *indices;          // triangleCount x 3 x indexSize bytes
	const u8 *triangleMaterials;// triangleCount x 2 bytes
	const u8 *materials;        // materialCount x 16 bytes
	const u8 *wheel;            // the WHLS section, or NULL (none, or hidden by CHRI)
	u32 wheelVersion;           // its version: RLDCHAR_WHEEL_VERSION_TEST (1), _USER (2) or _USER3 (3); 0 = no wheel
	u32 wheelVertexCount;
	u32 wheelTriangleCount;
	u32 wheelFlags;             // WHLS 3: RLDCHAR_WHEEL_ALWAYS_DRAW, RLDCHAR_WHEEL_REAR_MESH; 0 for versions 1 and 2
	const u8 *wheelExtra;       // WHLS 3: its extension (the head at offset E), else NULL
	u32 wheelRearVertexCount;   // WHLS 3 with RLDCHAR_WHEEL_REAR_MESH, else 0
	u32 wheelRearTriangleCount;
	float wheelAxle[2][3];      // WHLS 3: front, rear {dz, dy, dtrack} as given - already part of the wheel points; else 0
	const u8 *morph;            // the MRPH section (version 1, CNET-13 held), or NULL
	u32 morphTargets;           // bit (1 << RLDCHAR_MORPH_*) per target present; 0 = no MRPH
	u32 morphNormals;           // 1 = MRPH has normal deltas
	u32 morphBasePose;          // the POSN slot the deltas apply to

	struct RldCharTexture texture[RLDCHAR_TEX_COUNT_MAX];
	u64 textureBytes;           // sum of width x height x 4 over the decoded textures
};

static float RldChar_F32(const u8 *at)
{
	const u32 bits = Rld_ReadLE32(at);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int RldChar_F32Finite(const u8 *at)
{
	return (Rld_ReadLE32(at) & 0x7F800000u) != 0x7F800000u;
}

static int RldChar_NativeFail(struct RldCharNative *out, const char *rule, const char *format, ...)
{
	va_list args;

	out->state = RLDCHAR_NATIVE_REFUSED;
	out->rule = rule;
	va_start(args, format);
	vsnprintf(out->detail, sizeof(out->detail), format, args);
	va_end(args);
	out->detail[sizeof(out->detail) - 1u] = '\0';
	return 0;
}

static void RldChar_FreeNative(struct RldCharNative *n)
{
	u32 t;

	free(n->cnet);
	for (t = 0; t < RLDCHAR_TEX_COUNT_MAX; t++)
	{
		free(n->texture[t].rgba);
	}
	memset(n, 0, sizeof(*n));
}

// The bound of CNET and CTXT, 0 for any other type. Apart from
// RldChar_ChunkLimit on purpose: see the head of this section.
static u64 RldChar_NativeLimit(const u8 *type)
{
	if (memcmp(type, "CNET", 4) == 0)
	{
		return RLDCHAR_LIMIT_CNET;
	}
	if (memcmp(type, "CTXT", 4) == 0)
	{
		return RLDCHAR_LIMIT_CTXT;
	}
	return 0;
}

// Rld_ReadChunk for CNET and CTXT, with their bound: compression 0,
// size_stored == size_raw and the bound before the malloc, then the bytes and
// their hash. NULL with a fixed text in *errorOut.
static u8 *RldChar_ReadNativeChunk(struct RldReader *reader, int index, size_t *sizeOut, const char **errorOut)
{
	const u8 *entry = &reader->directory[index * RLD_DIR_ENTRY_SIZE];
	const u64 offset = Rld_ReadLE64(&entry[0x08]);
	const u64 stored = Rld_ReadLE64(&entry[0x10]);
	const u64 raw = Rld_ReadLE64(&entry[0x18]);
	const u32 method = Rld_ReadLE32(&entry[0x04]) & 0x0fu;
	const u64 limit = RldChar_NativeLimit(entry);
	u8 digest[32];
	u8 *bytes;

	*sizeOut = 0;
	*errorOut = NULL;

	if (limit == 0u)
	{
		*errorOut = "not a native chunk";
		return NULL;
	}
	if (method != RLD_COMPRESSION_NONE)
	{
		*errorOut = "the chunk is compressed - compression is not part of the format";
		return NULL;
	}
	if (stored != raw)
	{
		*errorOut = "size_stored and size_raw of the chunk differ";
		return NULL;
	}
	if (raw > limit)
	{
		*errorOut = "the chunk is larger than its bound (32 MiB)";
		return NULL;
	}

	bytes = (u8 *)malloc((size_t)raw ? (size_t)raw : 1u);
	if (bytes == NULL)
	{
		*errorOut = "out of memory";
		return NULL;
	}
	if (!Rld_ReadAt(reader, offset, bytes, (size_t)raw))
	{
		free(bytes);
		*errorOut = "the chunk cannot be read - the file is shorter than its own directory says";
		return NULL;
	}

	Sha256(bytes, (size_t)raw, digest);
	if (memcmp(digest, &entry[RLD_DIR_HASH_OFFSET], 32) != 0)
	{
		free(bytes);
		*errorOut = "hash mismatch - the chunk is altered or damaged";
		return NULL;
	}

	*sizeOut = (size_t)raw;
	return bytes;
}

// The hulls of the 47 frames of a model that passed RldChar_CheckModel (so
// every offset below lies in the body). body is the model body (4 bytes into
// CMDL), base what a pointer field holds for body offset 0: 0 before the
// relocation (the packer), the address of the body after it (the game, whose
// pointers are 32 bits). 1 = all 47 written.
static int RldChar_FrameHulls(const u8 *body, u32 bodyBytes, u32 base, struct RldCharHull hulls[RLDCHAR_NET_POSES])
{
	u32 header;
	u32 anims;
	int scale[3];
	u32 slot = 0;
	u32 a;
	int axis;

	if (bodyBytes < RLDCHAR_MODEL_BYTES)
	{
		return 0;
	}
	header = Rld_ReadLE32(&body[RLDCHAR_MODEL_HEADERS]) - base;
	if (((u64)header + RLDCHAR_HEADER_BYTES) > bodyBytes)
	{
		return 0;
	}
	anims = Rld_ReadLE32(&body[header + RLDCHAR_HEADER_ANIMS]) - base;
	if (((u64)anims + (4u * RLDCHAR_ANIM_COUNT)) > bodyBytes)
	{
		return 0;
	}
	for (axis = 0; axis < 3; axis++)
	{
		scale[axis] = RldChar_SignedHalf(Rld_ReadLE16(&body[header + RLDCHAR_HEADER_SCALE + (2u * (u32)axis)]));
	}

	for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
	{
		const u32 anim = Rld_ReadLE32(&body[anims + (4u * a)]) - base;
		u32 numFrames;
		u32 frameSize;
		u32 f;

		if (((u64)anim + RLDCHAR_ANIM_BYTES) > bodyBytes)
		{
			return 0;
		}
		numFrames = Rld_ReadLE16(&body[anim + RLDCHAR_ANIM_NUM_FRAMES]) & 0x7fffu;
		frameSize = Rld_ReadLE16(&body[anim + RLDCHAR_ANIM_FRAME_SIZE]);
		if (numFrames != s_rldCharFrames[a])
		{
			return 0;
		}

		for (f = 0; f < numFrames; f++)
		{
			const u64 frame = (u64)anim + RLDCHAR_ANIM_BYTES + ((u64)f * frameSize);

			if ((frame + RLDCHAR_FRAME_BYTES) > bodyBytes)
			{
				return 0;
			}
			for (axis = 0; axis < 3; axis++)
			{
				int pos = RldChar_SignedHalf(Rld_ReadLE16(&body[(size_t)frame + (2u * (u32)axis)]));
				double one;
				double two;

				if (axis == 1)
				{
					pos &= ~1;
				}
				one = ((double)pos * (double)scale[axis]) / 4096.0;
				two = ((double)(pos + RLDCHAR_RECORD_SPAN) * (double)scale[axis]) / 4096.0;
				hulls[slot].lo[axis] = (one < two) ? one : two;
				hulls[slot].hi[axis] = (one < two) ? two : one;
			}
			slot++;
		}
	}

	return slot == RLDCHAR_NET_POSES;
}

// A section of the table, by tag. 1 = found (and *at its entry).
static int RldChar_NetSection(const u8 *cnet, u32 table, u32 count, const char tag[4], const u8 **at)
{
	u32 s;

	for (s = 0; s < count; s++)
	{
		const u8 *entry = &cnet[table + (s * RLDCHAR_NET_SECTION_BYTES)];

		if (memcmp(entry, tag, 4) == 0)
		{
			*at = entry;
			return 1;
		}
	}
	return 0;
}

// The fixed layout of a known section: version 1, its stride and its size.
// 1 = the section is there and fits (*dataOut set), 0 = absent (*dataOut NULL), -1 =
// a finding (said).
static int RldChar_NetKnown(const u8 *cnet, u32 table, u32 count, const char tag[4], u32 stride, u64 size, struct RldCharNative *out, const u8 **dataOut)
{
	const u8 *entry = NULL;

	*dataOut = NULL;
	if (!RldChar_NetSection(cnet, table, count, tag, &entry))
	{
		return 0;
	}
	if (Rld_ReadLE16(&entry[4]) != 1u)
	{
		RldChar_NativeFail(out, "CNET-5", "section %.4s has version %u - this reader knows 1", tag, (unsigned)Rld_ReadLE16(&entry[4]));
		return -1;
	}
	if (Rld_ReadLE16(&entry[6]) != stride)
	{
		RldChar_NativeFail(out, "CNET-5", "section %.4s has stride %u, not %u", tag, (unsigned)Rld_ReadLE16(&entry[6]), (unsigned)stride);
		return -1;
	}
	if ((u64)Rld_ReadLE32(&entry[12]) != size)
	{
		RldChar_NativeFail(out, "CNET-5", "section %.4s has %u bytes, its counts need %llu", tag, (unsigned)Rld_ReadLE32(&entry[12]),
		                   (unsigned long long)size);
		return -1;
	}

	*dataOut = &cnet[Rld_ReadLE32(&entry[8])];
	return 1;
}

// The wheels (WHLS, renderer concept C.7.4): rule CNET-10.
//   0x00 u32 vertexCount Nw 3..2048, 0x04 u32 triangleCount Tw 1..1024,
//   0x08 u16 material (< M), 0x0A u16 flags (0; version 3: below),
//   0x0C f32 radius (0..64), 0x10 f32 halfWidth (0..radius), 0x14 f32[3]
//   front, 0x20 f32[3] rear (the wheel centers on +X, model units), 0x2C u32
//   reserved 0, 0x30 Nw x {f32 position[3], f32 normal[3], f32 uv[2]}
//   (center at the origin, axle along X, outside +X), then Tw x 3 u16
//   indices. Size exactly 0x30 + 32 Nw + 6 Tw (versions 1 and 2).
// The version of the section entry tells the kinds apart; the layout of
// versions 1 and 2 is the same byte for byte:
//   1  the test wheel (the self-test's files, --native-probe): the -X wheels
//      are the same mesh turned 180 degrees about Y, never mirrored.
//   2  an author's wheel (make-char --wheel-model): the -X wheels are its
//      mirror image (x negated, the normals mirrored, the winding turned), so
//      that the outer side is outside on both sides. Rule CNET-12 below
//      holds its texture to RLDCHAR_WHEEL_TEXTURE_EDGE_MAX.
//   3  an author's wheel as 2, with what version 2 cannot say: the flags at
//      0x0A (RLDCHAR_WHEEL_ALWAYS_DRAW, RLDCHAR_WHEEL_REAR_MESH), and behind
//      the mesh, at E = 0x30 + 32 Nw + 6 Tw rounded up to 4 (the bytes between
//      0), an extension:
//        E+0x00 f32[3] axleFront {dz, dy, dtrack}, E+0x0C f32[3] axleRear -
//               the offsets the author gave, in model units; the wheel
//               points at 0x14 and 0x20 hold them already (a reader never
//               adds them again)
//        E+0x18 u32 rearVertexCount Nr, E+0x1C u32 rearTriangleCount Tr,
//        E+0x20 u16 rearMaterial, E+0x22 u16 reserved 0, E+0x24 f32
//               rearRadius, E+0x28 f32 rearHalfWidth, E+0x2C u32 reserved 0 -
//               with RLDCHAR_WHEEL_REAR_MESH as the front wheel's fields
//               (3..2048, 1..1024, < M, 0..64, 0..radius), else all 0
//        E+0x30 Nr x 32 bytes of the rear mesh (as the front one), then
//               Tr x 3 u16 indices
//      Size exactly E + 0x30 + 32 Nr + 6 Tr. Without a rear mesh the rear
//      wheels are drawn from the front mesh, as in version 2; either mesh
//      is mirrored for the -X wheels. RLDCHAR_AXLE_* bound the offsets.
// A reader before version 2 refuses a version 2 or 3 section by CNET-10
// (version), a reader before version 3 a version 3 section, and the driver
// keeps its CMDL. Drawing any version is the game's part.

// The vertices and triangles of one wheel mesh (front, or the rear one of
// version 3) against CNET-10. which: "WHLS" or "WHLS rear". 1 = passed.
static int RldChar_CheckWheelMesh(const u8 *mesh, u32 nw, u32 tw, double radius, double halfWidth, const char *which, struct RldCharNative *out)
{
	u32 i;

	for (i = 0; i < nw; i++)
	{
		const u8 *v = &mesh[i * RLDCHAR_WHEEL_VERTEX_BYTES];
		const double x = RldChar_F32(&v[0]);
		const double y = RldChar_F32(&v[4]);
		const double z = RldChar_F32(&v[8]);
		const double nx = RldChar_F32(&v[12]);
		const double ny = RldChar_F32(&v[16]);
		const double nz = RldChar_F32(&v[20]);
		const double n2 = (nx * nx) + (ny * ny) + (nz * nz);
		const double r = (radius * (1.0 + RLDCHAR_WHEEL_SLACK)) + RLDCHAR_WHEEL_SLACK;
		const double h = (halfWidth * (1.0 + RLDCHAR_WHEEL_SLACK)) + RLDCHAR_WHEEL_SLACK;
		u32 c;

		for (c = 0; c < 8u; c++)
		{
			if (!RldChar_F32Finite(&v[4u * c]))
			{
				return RldChar_NativeFail(out, "CNET-10", "%s vertex %u is not a finite number", which, (unsigned)i);
			}
		}
		if ((x < -h) || (x > h) || (((y * y) + (z * z)) > (r * r)))
		{
			return RldChar_NativeFail(out, "CNET-10", "%s vertex %u (%.3f %.3f %.3f) lies outside the wheel's cylinder", which, (unsigned)i, x, y, z);
		}
		if ((n2 < RLDCHAR_NET_NORMAL_MIN) || (n2 > RLDCHAR_NET_NORMAL_MAX))
		{
			return RldChar_NativeFail(out, "CNET-10", "%s vertex %u: the normal is not of length 1", which, (unsigned)i);
		}
	}
	for (i = 0; i < tw; i++)
	{
		const u8 *t = &mesh[(nw * RLDCHAR_WHEEL_VERTEX_BYTES) + (i * 6u)];
		const u32 a = Rld_ReadLE16(&t[0]);
		const u32 b = Rld_ReadLE16(&t[2]);
		const u32 c = Rld_ReadLE16(&t[4]);

		if ((a >= nw) || (b >= nw) || (c >= nw) || (a == b) || (b == c) || (a == c))
		{
			return RldChar_NativeFail(out, "CNET-10", "%s triangle %u (%u %u %u): an index >= %u or a vertex twice", which, (unsigned)i, (unsigned)a,
			                          (unsigned)b, (unsigned)c, (unsigned)nw);
		}
	}
	return 1;
}

// The extension of WHLS version 3 at w + extra (size: the section's bytes):
// its fill bytes, axle offsets, flags and the rear mesh. 1 = passed (out filled).
static int RldChar_CheckWheelExtra(const u8 *w, u32 size, u32 meshEnd, u32 flags, struct RldCharNative *out)
{
	static const double lo[3] = {RLDCHAR_AXLE_DZ_MIN, RLDCHAR_AXLE_DY_MIN, RLDCHAR_AXLE_TRACK_MIN};
	static const double hi[3] = {RLDCHAR_AXLE_DZ_MAX, RLDCHAR_AXLE_DY_MAX, RLDCHAR_AXLE_TRACK_MAX};
	const u32 extra = (meshEnd + 3u) & ~3u;
	const u8 *x = &w[extra];
	u32 nr;
	u32 tr;
	u32 i;
	int k;

	if (((u64)extra + RLDCHAR_WHEEL_EXTRA_BYTES) > size)
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS version 3 has %u bytes, its extension needs %u", (unsigned)size,
		                          (unsigned)(extra + RLDCHAR_WHEEL_EXTRA_BYTES));
	}
	for (i = meshEnd; i < extra; i++)
	{
		if (w[i] != 0u)
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS version 3: a fill byte in front of the extension is not 0");
		}
	}
	for (k = 0; k < 6; k++)
	{
		double value;

		if (!RldChar_F32Finite(&x[4u * (u32)k]))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS version 3: an axle offset is not a finite number");
		}
		value = RldChar_F32(&x[4u * (u32)k]);
		if ((value < lo[k % 3]) || (value > hi[k % 3]))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS version 3: the %s axle's %s is %.3f - it may be %.0f..%.0f model units", (k < 3) ? "front" : "rear",
			                          ((k % 3) == 0) ? "dz" : (((k % 3) == 1) ? "dy" : "dtrack"), value, lo[k % 3], hi[k % 3]);
		}
	}
	nr = Rld_ReadLE32(&x[0x18]);
	tr = Rld_ReadLE32(&x[0x1C]);
	if ((Rld_ReadLE16(&x[0x22]) != 0u) || (Rld_ReadLE32(&x[0x2C]) != 0u))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS version 3: a reserved field of the extension is not 0");
	}
	if ((flags & RLDCHAR_WHEEL_REAR_MESH) == 0u)
	{
		if ((nr != 0u) || (tr != 0u) || (Rld_ReadLE16(&x[0x20]) != 0u) || (Rld_ReadLE32(&x[0x24]) != 0u) || (Rld_ReadLE32(&x[0x28]) != 0u) ||
		    (size != (extra + RLDCHAR_WHEEL_EXTRA_BYTES)))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS version 3 without a rear mesh: its rear fields are 0 and nothing follows the extension");
		}
	}
	else
	{
		double radius;
		double halfWidth;

		if ((nr < 3u) || (nr > RLDCHAR_WHEEL_VERTICES_MAX) || (tr < 1u) || (tr > RLDCHAR_WHEEL_TRIANGLES_MAX))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS rear wheel has %u vertices and %u triangles - it may have 3..%u and 1..%u", (unsigned)nr, (unsigned)tr,
			                          (unsigned)RLDCHAR_WHEEL_VERTICES_MAX, (unsigned)RLDCHAR_WHEEL_TRIANGLES_MAX);
		}
		if ((u64)size != ((u64)extra + RLDCHAR_WHEEL_EXTRA_BYTES + ((u64)nr * RLDCHAR_WHEEL_VERTEX_BYTES) + ((u64)tr * 6u)))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS version 3 has %u bytes, its rear wheel of %u vertices and %u triangles needs %u", (unsigned)size,
			                          (unsigned)nr, (unsigned)tr, (unsigned)(extra + RLDCHAR_WHEEL_EXTRA_BYTES + (nr * RLDCHAR_WHEEL_VERTEX_BYTES) + (tr * 6u)));
		}
		if (Rld_ReadLE16(&x[0x20]) >= out->materialCount)
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS rear wheel names material %u - there are %u", (unsigned)Rld_ReadLE16(&x[0x20]),
			                          (unsigned)out->materialCount);
		}
		if (!RldChar_F32Finite(&x[0x24]) || !RldChar_F32Finite(&x[0x28]))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS rear wheel: radius or width is not a finite number");
		}
		radius = RldChar_F32(&x[0x24]);
		halfWidth = RldChar_F32(&x[0x28]);
		if ((radius <= 0.0) || (radius > RLDCHAR_WHEEL_RADIUS_MAX) || (halfWidth <= 0.0) || (halfWidth > radius))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS rear wheel: radius %.3f and half width %.3f - the radius may be 0..%.0f, the half width 0..radius",
			                          radius, halfWidth, RLDCHAR_WHEEL_RADIUS_MAX);
		}
		if (!RldChar_CheckWheelMesh(&x[RLDCHAR_WHEEL_EXTRA_BYTES], nr, tr, radius, halfWidth, "WHLS rear", out))
		{
			return 0;
		}
		out->wheelRearVertexCount = nr;
		out->wheelRearTriangleCount = tr;
	}

	for (k = 0; k < 6; k++)
	{
		out->wheelAxle[k / 3][k % 3] = RldChar_F32(&x[4u * (u32)k]);
	}
	out->wheelExtra = x;
	return 1;
}

static int RldChar_CheckWheels(const u8 *cnet, const u8 *entry, struct RldCharNative *out)
{
	const u32 size = Rld_ReadLE32(&entry[12]);
	const u32 version = Rld_ReadLE16(&entry[4]);
	const u8 *w = &cnet[Rld_ReadLE32(&entry[8])];
	u32 nw;
	u32 tw;
	u32 flags;
	u32 meshEnd;
	double radius;
	double halfWidth;
	double point[2][3];
	u32 i;
	int k;
	int axis;

	if ((version != RLDCHAR_WHEEL_VERSION_TEST) && (version != RLDCHAR_WHEEL_VERSION_USER) && (version != RLDCHAR_WHEEL_VERSION_USER3))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS has version %u - this reader knows 1, 2 and 3", (unsigned)version);
	}
	if (Rld_ReadLE16(&entry[6]) != 0u)
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS has stride %u, not 0 (its layout is its own)", (unsigned)Rld_ReadLE16(&entry[6]));
	}
	if (size < RLDCHAR_WHEEL_HEAD_BYTES)
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS has %u bytes, its head alone needs %u", (unsigned)size, (unsigned)RLDCHAR_WHEEL_HEAD_BYTES);
	}
	nw = Rld_ReadLE32(&w[0x00]);
	tw = Rld_ReadLE32(&w[0x04]);
	if ((nw < 3u) || (nw > RLDCHAR_WHEEL_VERTICES_MAX) || (tw < 1u) || (tw > RLDCHAR_WHEEL_TRIANGLES_MAX))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS has %u vertices and %u triangles - it may have 3..%u and 1..%u", (unsigned)nw, (unsigned)tw,
		                          (unsigned)RLDCHAR_WHEEL_VERTICES_MAX, (unsigned)RLDCHAR_WHEEL_TRIANGLES_MAX);
	}
	meshEnd = RLDCHAR_WHEEL_HEAD_BYTES + (nw * RLDCHAR_WHEEL_VERTEX_BYTES) + (tw * 6u);
	if (((version != RLDCHAR_WHEEL_VERSION_USER3) && (size != meshEnd)) || ((version == RLDCHAR_WHEEL_VERSION_USER3) && (size < meshEnd)))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS has %u bytes, %u vertices and %u triangles need %u", (unsigned)size, (unsigned)nw, (unsigned)tw,
		                          (unsigned)meshEnd);
	}
	if (Rld_ReadLE16(&w[0x08]) >= out->materialCount)
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS names material %u - there are %u", (unsigned)Rld_ReadLE16(&w[0x08]), (unsigned)out->materialCount);
	}
	flags = Rld_ReadLE16(&w[0x0A]);
	if ((Rld_ReadLE32(&w[0x2C]) != 0u) || ((version != RLDCHAR_WHEEL_VERSION_USER3) && (flags != 0u)) || ((flags & ~RLDCHAR_WHEEL_FLAGS_KNOWN) != 0u))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS flags or reserved field not 0");
	}
	for (i = 0; i < 9u; i++)
	{
		if (!RldChar_F32Finite(&w[0x0C + (4u * i)]))
		{
			return RldChar_NativeFail(out, "CNET-10", "WHLS: radius, width or a wheel point is not a finite number");
		}
	}
	radius = RldChar_F32(&w[0x0C]);
	halfWidth = RldChar_F32(&w[0x10]);
	if ((radius <= 0.0) || (radius > RLDCHAR_WHEEL_RADIUS_MAX) || (halfWidth <= 0.0) || (halfWidth > radius))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS: radius %.3f and half width %.3f - the radius may be 0..%.0f, the half width 0..radius", radius, halfWidth,
		                          RLDCHAR_WHEEL_RADIUS_MAX);
	}
	for (k = 0; k < 2; k++)
	{
		for (axis = 0; axis < 3; axis++)
		{
			point[k][axis] = RldChar_F32(&w[0x14 + (12u * (u32)k) + (4u * (u32)axis)]);
			if ((point[k][axis] < -RLDCHAR_WHEEL_POINT_MAX) || (point[k][axis] > RLDCHAR_WHEEL_POINT_MAX))
			{
				return RldChar_NativeFail(out, "CNET-10", "WHLS: a wheel point lies beyond %.0f model units", RLDCHAR_WHEEL_POINT_MAX);
			}
		}
	}
	if ((point[0][0] <= 0.0) || (point[1][0] <= 0.0) || (point[0][2] <= point[1][2]))
	{
		return RldChar_NativeFail(out, "CNET-10", "WHLS: the wheel points are the +X wheels, front before rear (front x %.3f z %.3f, rear x %.3f z %.3f)",
		                          point[0][0], point[0][2], point[1][0], point[1][2]);
	}

	if (!RldChar_CheckWheelMesh(&w[RLDCHAR_WHEEL_HEAD_BYTES], nw, tw, radius, halfWidth, "WHLS", out))
	{
		return 0;
	}
	if ((version == RLDCHAR_WHEEL_VERSION_USER3) && !RldChar_CheckWheelExtra(w, size, meshEnd, flags, out))
	{
		return 0;
	}

	out->wheel = w;
	out->wheelVersion = version;
	out->wheelVertexCount = nw;
	out->wheelTriangleCount = tw;
	out->wheelFlags = (version == RLDCHAR_WHEEL_VERSION_USER3) ? flags : 0u;
	return 1;
}

// MRPH, the shape keys win and lose (rule CNET-13), version 1:
//   0x00 u16 targets (bit 0 win, bit 1 lose; 1..3), 0x02 u16 flags (bit 0:
//   normal deltas follow), 0x04 u32 vertexCount (N of the head), 0x08 u16
//   basePose (RLDCHAR_NET_NEUTRAL_POSE with 47 poses, 0 for a still model),
//   0x0A u16 reserved 0, 0x0C u32 reserved 0, then per target present, win
//   before lose: N x f32[3] position deltas and, with flags bit 0, N x f32[3]
//   normal deltas. Size exactly 0x10 + targets x N x 12 x (1 + normals).
// The pose of a target is POSN[basePose] + delta, its normal POSN[basePose]
// normal + delta (of length 1). Every such position lies inside the head's
// hull and inside the box of the 47 CMDL frames (hulls), the room the driver
// takes in its own model: the game shows win and lose at any CMDL frame and
// culls and sorts by that frame. A reader before this one skips MRPH as an
// unknown tag (no win, no lose), and so does this reader with a version
// other than 1. 1 = passed (out->morph set for version 1).
static int RldChar_CheckMorph(const u8 *cnet, const u8 *entry, const struct RldCharHull hulls[RLDCHAR_NET_POSES], struct RldCharNative *out)
{
	const u32 size = Rld_ReadLE32(&entry[12]);
	const u8 *m = &cnet[Rld_ReadLE32(&entry[8])];
	const u32 base = (out->poseCount == 0u) ? 0u : RLDCHAR_NET_NEUTRAL_POSE;
	const u8 *pose = &out->poses[(size_t)base * out->vertexCount * RLDCHAR_NET_VERTEX_BYTES];
	double box[2][3];
	u32 targets;
	u32 normals;
	u32 count;
	u32 block;
	u32 t;
	u32 i;
	u32 c;

	if (Rld_ReadLE16(&entry[4]) != RLDCHAR_MORPH_VERSION)
	{
		return 1;
	}
	if (Rld_ReadLE16(&entry[6]) != 0u)
	{
		return RldChar_NativeFail(out, "CNET-13", "MRPH has stride %u, not 0 (its layout is its own)", (unsigned)Rld_ReadLE16(&entry[6]));
	}
	if (size < RLDCHAR_MORPH_HEAD_BYTES)
	{
		return RldChar_NativeFail(out, "CNET-13", "MRPH has %u bytes, its head alone needs %u", (unsigned)size, (unsigned)RLDCHAR_MORPH_HEAD_BYTES);
	}
	targets = Rld_ReadLE16(&m[0x00]);
	normals = Rld_ReadLE16(&m[0x02]);
	if ((targets < 1u) || (targets > 3u) || ((normals & ~RLDCHAR_MORPH_NORMALS) != 0u) || (Rld_ReadLE16(&m[0x0A]) != 0u) || (Rld_ReadLE32(&m[0x0C]) != 0u))
	{
		return RldChar_NativeFail(out, "CNET-13", "MRPH targets 0x%x, flags 0x%x - targets bit 0 win and bit 1 lose, flags bit 0 only, reserved fields 0",
		                          (unsigned)targets, (unsigned)normals);
	}
	if ((Rld_ReadLE32(&m[0x04]) != out->vertexCount) || (Rld_ReadLE16(&m[0x08]) != base))
	{
		return RldChar_NativeFail(out, "CNET-13", "MRPH is made for %u vertices and pose %u - the model has %u vertices and its base pose is %u",
		                          (unsigned)Rld_ReadLE32(&m[0x04]), (unsigned)Rld_ReadLE16(&m[0x08]), (unsigned)out->vertexCount, (unsigned)base);
	}
	count = (targets & 1u) + ((targets >> 1) & 1u);
	block = out->vertexCount * 12u * ((normals != 0u) ? 2u : 1u);
	if ((u64)size != ((u64)RLDCHAR_MORPH_HEAD_BYTES + ((u64)count * block)))
	{
		return RldChar_NativeFail(out, "CNET-13", "MRPH has %u bytes, %u targets of %u vertices need %llu", (unsigned)size, (unsigned)count,
		                          (unsigned)out->vertexCount, (unsigned long long)((u64)RLDCHAR_MORPH_HEAD_BYTES + ((u64)count * block)));
	}

	// The box of the 47 frames.
	for (c = 0; c < 3u; c++)
	{
		box[0][c] = hulls[0].lo[c];
		box[1][c] = hulls[0].hi[c];
		for (i = 1; i < RLDCHAR_NET_POSES; i++)
		{
			box[0][c] = (hulls[i].lo[c] < box[0][c]) ? hulls[i].lo[c] : box[0][c];
			box[1][c] = (hulls[i].hi[c] > box[1][c]) ? hulls[i].hi[c] : box[1][c];
		}
	}
	for (t = 0; t < count; t++)
	{
		const u8 *delta = &m[RLDCHAR_MORPH_HEAD_BYTES + ((size_t)t * block)];
		const u8 *normal = &delta[(size_t)out->vertexCount * 12u];
		const char *name = ((t == 0u) && ((targets & 1u) != 0u)) ? "win" : "lose";

		for (i = 0; i < (block / 4u); i++)
		{
			if (!RldChar_F32Finite(&delta[4u * i]))
			{
				return RldChar_NativeFail(out, "CNET-13", "MRPH %s: vertex %u is not a finite number", name, (unsigned)((i / 3u) % out->vertexCount));
			}
		}
		for (i = 0; i < out->vertexCount; i++)
		{
			const u8 *b = &pose[i * RLDCHAR_NET_VERTEX_BYTES];
			double n2 = 0.0;

			for (c = 0; c < 3u; c++)
			{
				const double p = (double)RldChar_F32(&b[4u * c]) + (double)RldChar_F32(&delta[(i * 12u) + (4u * c)]);

				if ((p < out->hullMin[c]) || (p > out->hullMax[c]) || (p < box[0][c]) || (p > box[1][c]))
				{
					return RldChar_NativeFail(out, "CNET-13", "MRPH %s: vertex %u lies outside the hull (axis %u: %.3f, head %.3f..%.3f, frames %.3f..%.3f)", name,
					                          (unsigned)i, (unsigned)c, p, (double)out->hullMin[c], (double)out->hullMax[c], box[0][c], box[1][c]);
				}
				if (normals != 0u)
				{
					const double n = (double)RldChar_F32(&b[12u + (4u * c)]) + (double)RldChar_F32(&normal[(i * 12u) + (4u * c)]);

					n2 += n * n;
				}
			}
			if ((normals != 0u) && ((n2 < RLDCHAR_NET_NORMAL_MIN) || (n2 > RLDCHAR_NET_NORMAL_MAX)))
			{
				return RldChar_NativeFail(out, "CNET-13", "MRPH %s: vertex %u: the normal is not of length 1 (length^2 %.4f)", name, (unsigned)i, n2);
			}
		}
	}

	out->morph = m;
	out->morphTargets = targets;
	out->morphNormals = (normals != 0u) ? 1u : 0u;
	out->morphBasePose = base;
	return 1;
}

// CNET-1..10 and 13 on the chunk as stored; fills the counts and pointers of out.
// chriFlags: RLDCHAR_FLAG_NO_WHEELS makes WHLS irrelevant (not checked, not
// used). hulls: the 47 CMDL frames (RldChar_FrameHulls). 1 = passed.
static int RldChar_CheckNet(const u8 *cnet, size_t size, u32 chriFlags, const struct RldCharHull hulls[RLDCHAR_NET_POSES], struct RldCharNative *out)
{
	u32 headerBytes;
	u32 sectionCount;
	u32 table;
	u32 tableEnd;
	u32 poseEncoding;
	u32 slots;
	u32 i;
	u32 j;
	int axis;
	int textured = 0;
	const u8 *entry = NULL;
	int r;

	// CNET-1: a head this reader knows.
	if ((size < RLDCHAR_NET_HEAD_BYTES) || (Rld_ReadLE16(&cnet[0x00]) != RLDCHAR_NET_VERSION))
	{
		return RldChar_NativeFail(out, "CNET-1", "%s", (size < RLDCHAR_NET_HEAD_BYTES) ? "CNET is shorter than its 64-byte head" : "CNET has a version this reader does not know (1)");
	}

	// CNET-2: the fixed fields.
	headerBytes = Rld_ReadLE16(&cnet[0x04]);
	poseEncoding = Rld_ReadLE16(&cnet[0x16]);
	if ((Rld_ReadLE16(&cnet[0x02]) != 0u) || (Rld_ReadLE16(&cnet[0x32]) != 0u) || (Rld_ReadLE32(&cnet[0x34]) != 0u) || (Rld_ReadLE32(&cnet[0x38]) != 0u) ||
	    (Rld_ReadLE32(&cnet[0x3C]) != 0u))
	{
		return RldChar_NativeFail(out, "CNET-2", "flags or a reserved field of the head is not 0");
	}
	if ((headerBytes < RLDCHAR_NET_HEAD_BYTES) || ((headerBytes & 3u) != 0u) || (headerBytes > size))
	{
		return RldChar_NativeFail(out, "CNET-2", "headerBytes is %u - at least 64, a multiple of 4, inside the chunk", (unsigned)headerBytes);
	}
	out->indexSize = Rld_ReadLE16(&cnet[0x10]);
	if ((poseEncoding != RLDCHAR_NET_POSE_FLOAT) || ((out->indexSize != 2u) && (out->indexSize != 4u)))
	{
		return RldChar_NativeFail(out, "CNET-2", "pose encoding %u and index size %u - this reader knows encoding 1 and sizes 2 and 4", (unsigned)poseEncoding,
		                          (unsigned)out->indexSize);
	}

	// CNET-3: counts and budget.
	out->vertexCount = Rld_ReadLE32(&cnet[0x08]);
	out->triangleCount = Rld_ReadLE32(&cnet[0x0C]);
	out->materialCount = Rld_ReadLE16(&cnet[0x12]);
	out->poseCount = Rld_ReadLE16(&cnet[0x14]);
	out->textureCount = Rld_ReadLE16(&cnet[0x30]);
	if ((out->vertexCount < 3u) || (out->vertexCount > RLDCHAR_NET_VERTICES_MAX) || (out->triangleCount < 1u) ||
	    (out->triangleCount > RLDCHAR_NET_TRIANGLES_MAX) || (out->materialCount < 1u) || (out->materialCount > RLDCHAR_NET_MATERIALS_MAX))
	{
		return RldChar_NativeFail(out, "CNET-3", "%u vertices, %u triangles, %u materials - allowed 3..%u, 1..%u, 1..%u", (unsigned)out->vertexCount,
		                          (unsigned)out->triangleCount, (unsigned)out->materialCount, (unsigned)RLDCHAR_NET_VERTICES_MAX,
		                          (unsigned)RLDCHAR_NET_TRIANGLES_MAX, (unsigned)RLDCHAR_NET_MATERIALS_MAX);
	}
	if (((out->poseCount != 0u) && (out->poseCount != RLDCHAR_NET_POSES)) || (out->textureCount > RLDCHAR_TEX_COUNT_MAX))
	{
		return RldChar_NativeFail(out, "CNET-3", "%u poses and %u textures - a character has 0 or 47 poses and at most %u textures", (unsigned)out->poseCount,
		                          (unsigned)out->textureCount, (unsigned)RLDCHAR_TEX_COUNT_MAX);
	}
	if ((out->indexSize == 2u) && (out->vertexCount > 65536u))
	{
		return RldChar_NativeFail(out, "CNET-3", "%u vertices do not fit 16-bit indices", (unsigned)out->vertexCount);
	}

	// CNET-4: the section table.
	sectionCount = Rld_ReadLE16(&cnet[0x06]);
	table = headerBytes;
	tableEnd = table + (sectionCount * RLDCHAR_NET_SECTION_BYTES);
	if ((sectionCount < 1u) || (sectionCount > RLDCHAR_NET_SECTIONS_MAX) || (tableEnd > size))
	{
		return RldChar_NativeFail(out, "CNET-4", "%u sections - 1..%u, and the table inside the chunk", (unsigned)sectionCount, (unsigned)RLDCHAR_NET_SECTIONS_MAX);
	}
	for (i = 0; i < sectionCount; i++)
	{
		const u8 *e = &cnet[table + (i * RLDCHAR_NET_SECTION_BYTES)];
		const u32 offset = Rld_ReadLE32(&e[8]);
		const u32 bytes = Rld_ReadLE32(&e[12]);

		if (((offset & 3u) != 0u) || (offset < tableEnd) || (offset > size) || (bytes > (size - offset)))
		{
			return RldChar_NativeFail(out, "CNET-4", "section %.4s lies at %u with %u bytes - 4-aligned, behind the table, inside the chunk", (const char *)e,
			                          (unsigned)offset, (unsigned)bytes);
		}
		for (j = 0; j < i; j++)
		{
			const u8 *o = &cnet[table + (j * RLDCHAR_NET_SECTION_BYTES)];
			const u32 otherOffset = Rld_ReadLE32(&o[8]);
			const u32 otherBytes = Rld_ReadLE32(&o[12]);

			if (memcmp(e, o, 4) == 0)
			{
				return RldChar_NativeFail(out, "CNET-4", "section %.4s appears twice", (const char *)e);
			}
			if ((bytes != 0u) && (otherBytes != 0u) && (offset < (otherOffset + otherBytes)) && (otherOffset < (offset + bytes)))
			{
				return RldChar_NativeFail(out, "CNET-4", "sections %.4s and %.4s overlap", (const char *)o, (const char *)e);
			}
		}
	}

	// CNET-5: the known sections, their layout and the required ones.
	slots = (out->poseCount == 0u) ? 1u : out->poseCount;
	if (((r = RldChar_NetKnown(cnet, table, sectionCount, "POSN", RLDCHAR_NET_VERTEX_BYTES, (u64)slots * out->vertexCount * RLDCHAR_NET_VERTEX_BYTES, out,
	                           &out->poses)) < 0) ||
	    ((r == 0) && !RldChar_NativeFail(out, "CNET-5", "the required section POSN is missing")))
	{
		return 0;
	}
	if (((r = RldChar_NetKnown(cnet, table, sectionCount, "TRIS", 3u * out->indexSize, (u64)out->triangleCount * 3u * out->indexSize, out,
	                           &out->indices)) < 0) ||
	    ((r == 0) && !RldChar_NativeFail(out, "CNET-5", "the required section TRIS is missing")))
	{
		return 0;
	}
	if (((r = RldChar_NetKnown(cnet, table, sectionCount, "TMAT", 2u, (u64)out->triangleCount * 2u, out, &out->triangleMaterials)) < 0) ||
	    ((r == 0) && !RldChar_NativeFail(out, "CNET-5", "the required section TMAT is missing")))
	{
		return 0;
	}
	if (((r = RldChar_NetKnown(cnet, table, sectionCount, "MATL", RLDCHAR_NET_MATERIAL_BYTES, (u64)out->materialCount * RLDCHAR_NET_MATERIAL_BYTES, out,
	                           &out->materials)) < 0) ||
	    ((r == 0) && !RldChar_NativeFail(out, "CNET-5", "the required section MATL is missing")))
	{
		return 0;
	}
	if ((RldChar_NetKnown(cnet, table, sectionCount, "UV00", 8u, (u64)out->vertexCount * 8u, out, &out->uv) < 0) ||
	    (RldChar_NetKnown(cnet, table, sectionCount, "COL0", 4u, (u64)out->vertexCount * 4u, out, &out->colors) < 0))
	{
		return 0;
	}

	// CNET-6: numbers. The head's hull, then every pose position inside it,
	// every normal of length 1, every UV finite.
	for (axis = 0; axis < 3; axis++)
	{
		if (!RldChar_F32Finite(&cnet[0x18 + (4 * axis)]) || !RldChar_F32Finite(&cnet[0x24 + (4 * axis)]))
		{
			return RldChar_NativeFail(out, "CNET-6", "the hull of the head is not a finite number");
		}
		out->hullMin[axis] = RldChar_F32(&cnet[0x18 + (4 * axis)]);
		out->hullMax[axis] = RldChar_F32(&cnet[0x24 + (4 * axis)]);
		if (out->hullMin[axis] > out->hullMax[axis])
		{
			return RldChar_NativeFail(out, "CNET-6", "the hull of the head is turned inside out on axis %d", axis);
		}
	}
	for (i = 0; i < (slots * out->vertexCount); i++)
	{
		const u8 *v = &out->poses[i * RLDCHAR_NET_VERTEX_BYTES];
		double n2 = 0.0;
		u32 c;

		for (c = 0; c < 6u; c++)
		{
			if (!RldChar_F32Finite(&v[4u * c]))
			{
				return RldChar_NativeFail(out, "CNET-6", "pose %u vertex %u is not a finite number", (unsigned)(i / out->vertexCount),
				                          (unsigned)(i % out->vertexCount));
			}
		}
		for (axis = 0; axis < 3; axis++)
		{
			const float p = RldChar_F32(&v[4 * axis]);
			const double n = RldChar_F32(&v[12 + (4 * axis)]);

			if ((p < out->hullMin[axis]) || (p > out->hullMax[axis]))
			{
				return RldChar_NativeFail(out, "CNET-6", "pose %u vertex %u lies outside the hull of the head", (unsigned)(i / out->vertexCount),
				                          (unsigned)(i % out->vertexCount));
			}
			n2 += n * n;
		}
		if ((n2 < RLDCHAR_NET_NORMAL_MIN) || (n2 > RLDCHAR_NET_NORMAL_MAX))
		{
			return RldChar_NativeFail(out, "CNET-6", "pose %u vertex %u: the normal is not of length 1 (length^2 %.4f)", (unsigned)(i / out->vertexCount),
			                          (unsigned)(i % out->vertexCount), n2);
		}
	}
	if (out->uv != NULL)
	{
		for (i = 0; i < (2u * out->vertexCount); i++)
		{
			if (!RldChar_F32Finite(&out->uv[4u * i]))
			{
				return RldChar_NativeFail(out, "CNET-6", "the UV of vertex %u is not a finite number", (unsigned)(i / 2u));
			}
		}
	}

	// CNET-7: topology.
	for (i = 0; i < out->triangleCount; i++)
	{
		u32 idx[3];
		u32 c;

		for (c = 0; c < 3u; c++)
		{
			const u8 *at = &out->indices[((i * 3u) + c) * out->indexSize];

			idx[c] = (out->indexSize == 2u) ? Rld_ReadLE16(at) : Rld_ReadLE32(at);
		}
		if ((idx[0] >= out->vertexCount) || (idx[1] >= out->vertexCount) || (idx[2] >= out->vertexCount))
		{
			return RldChar_NativeFail(out, "CNET-7", "triangle %u names vertex %u - there are %u", (unsigned)i,
			                          (unsigned)((idx[0] >= out->vertexCount) ? idx[0] : ((idx[1] >= out->vertexCount) ? idx[1] : idx[2])),
			                          (unsigned)out->vertexCount);
		}
		if ((idx[0] == idx[1]) || (idx[1] == idx[2]) || (idx[0] == idx[2]))
		{
			return RldChar_NativeFail(out, "CNET-7", "triangle %u names a vertex twice", (unsigned)i);
		}
		if (Rld_ReadLE16(&out->triangleMaterials[2u * i]) >= out->materialCount)
		{
			return RldChar_NativeFail(out, "CNET-7", "triangle %u has material %u - there are %u", (unsigned)i,
			                          (unsigned)Rld_ReadLE16(&out->triangleMaterials[2u * i]), (unsigned)out->materialCount);
		}
	}

	// CNET-8: materials.
	for (i = 0; i < out->materialCount; i++)
	{
		const u8 *m = &out->materials[i * RLDCHAR_NET_MATERIAL_BYTES];
		const int texture = RldChar_SignedHalf(Rld_ReadLE16(&m[4]));

		for (j = 8; j < RLDCHAR_NET_MATERIAL_BYTES; j++)
		{
			if (m[j] != 0u)
			{
				return RldChar_NativeFail(out, "CNET-8", "material %u: a reserved byte is not 0", (unsigned)i);
			}
		}
		if ((m[6] > RLDCHAR_NET_ALPHA_MAX) || ((m[7] & ~RLDCHAR_NET_MATERIAL_NEAREST) != 0u))
		{
			return RldChar_NativeFail(out, "CNET-8", "material %u: alpha mode %u, flags 0x%02x - modes 0 opaque, 1 mask, 2 blend; flag bit 0 only", (unsigned)i,
			                          (unsigned)m[6], (unsigned)m[7]);
		}
		if ((texture < -1) || (texture >= (int)out->textureCount))
		{
			return RldChar_NativeFail(out, "CNET-8", "material %u names texture %d - the head names %u", (unsigned)i, texture, (unsigned)out->textureCount);
		}
		textured |= (texture >= 0);
	}
	if (textured && (out->uv == NULL))
	{
		return RldChar_NativeFail(out, "CNET-8", "a material has a texture, but there is no UV00 section");
	}

	// CNET-9: every pose inside the hull of its CMDL frame - a still pose
	// inside every frame. Else the renderer culls too early at the screen's
	// edge and the ordering range is wrong (renderer concept C.4.3).
	for (i = 0; i < RLDCHAR_NET_POSES; i++)
	{
		const u8 *pose = &out->poses[((out->poseCount == 0u) ? 0u : i) * out->vertexCount * RLDCHAR_NET_VERTEX_BYTES];

		for (j = 0; j < out->vertexCount; j++)
		{
			for (axis = 0; axis < 3; axis++)
			{
				const double p = RldChar_F32(&pose[(j * RLDCHAR_NET_VERTEX_BYTES) + (4u * (u32)axis)]);

				if ((p < hulls[i].lo[axis]) || (p > hulls[i].hi[axis]))
				{
					return RldChar_NativeFail(out, "CNET-9", "pose %u vertex %u lies outside CMDL frame %u (axis %d: %.3f, frame %.3f..%.3f)",
					                          (unsigned)((out->poseCount == 0u) ? 0u : i), (unsigned)j, (unsigned)i, axis, p, hulls[i].lo[axis],
					                          hulls[i].hi[axis]);
				}
			}
		}
	}

	// CNET-10: the wheels. Hidden by CHRI: not looked at. Else required - a
	// native body is never drawn with the sprite wheels (renderer concept C.12).
	if ((chriFlags & RLDCHAR_FLAG_NO_WHEELS) == 0u)
	{
		if (!RldChar_NetSection(cnet, table, sectionCount, "WHLS", &entry))
		{
			return RldChar_NativeFail(out, "CNET-10", "no WHLS section, and CHRI does not hide the wheels");
		}
		if (!RldChar_CheckWheels(cnet, entry, out))
		{
			return 0;
		}
	}

	// CNET-13: the shape keys win and lose, when there is an MRPH.
	if (RldChar_NetSection(cnet, table, sectionCount, "MRPH", &entry) && !RldChar_CheckMorph(cnet, entry, hulls, out))
	{
		return 0;
	}

	return 1;
}

// A power of two in RLDCHAR_TEX_EDGE_MIN..MAX.
static int RldChar_TexEdge(u32 edge)
{
	return (edge >= RLDCHAR_TEX_EDGE_MIN) && (edge <= RLDCHAR_TEX_EDGE_MAX) && ((edge & (edge - 1u)) == 0u);
}

// RGBA8 bytes of a texture with every mip level (max(1, w >> n) x max(1, h >> n)).
static u64 RldChar_TexMipBytes(u32 width, u32 height)
{
	u64 sum = 0;

	for (;;)
	{
		sum += (u64)width * height * 4u;
		if ((width == 1u) && (height == 1u))
		{
			return sum;
		}
		width = (width > 1u) ? (width >> 1) : 1u;
		height = (height > 1u) ? (height >> 1) : 1u;
	}
}

// CTXT-1..8 on the chunk as stored, then every texture decoded into
// out->texture[] (one after the other; a PNG is freed right after its
// pixels exist). 1 = passed.
static int RldChar_CheckTextures(const u8 *ctxt, size_t size, struct RldCharNative *out)
{
	u32 count;
	u32 tableEnd;
	u64 budget = 0;
	u32 i;
	u32 j;

	// CTXT-1
	if ((size < RLDCHAR_TEX_HEAD_BYTES) || (Rld_ReadLE16(&ctxt[0x00]) != RLDCHAR_TEX_VERSION))
	{
		return RldChar_NativeFail(out, "CTXT-1", "%s", (size < RLDCHAR_TEX_HEAD_BYTES) ? "CTXT is shorter than its 16-byte head" : "CTXT has a version this reader does not know (1)");
	}

	// CTXT-2
	count = Rld_ReadLE16(&ctxt[0x04]);
	tableEnd = RLDCHAR_TEX_HEAD_BYTES + (count * RLDCHAR_TEX_ENTRY_BYTES);
	if ((Rld_ReadLE16(&ctxt[0x02]) != 0u) || (Rld_ReadLE16(&ctxt[0x06]) != RLDCHAR_TEX_ENTRY_BYTES) || (Rld_ReadLE32(&ctxt[0x08]) != 0u) ||
	    (Rld_ReadLE32(&ctxt[0x0C]) != 0u))
	{
		return RldChar_NativeFail(out, "CTXT-2", "flags, entry size (64) or a reserved field of the head");
	}
	if ((count < 1u) || (count > RLDCHAR_TEX_COUNT_MAX) || (tableEnd > size))
	{
		return RldChar_NativeFail(out, "CTXT-2", "%u entries - 1..%u, and the table inside the chunk", (unsigned)count, (unsigned)RLDCHAR_TEX_COUNT_MAX);
	}

	for (i = 0; i < count; i++)
	{
		const u8 *e = &ctxt[RLDCHAR_TEX_HEAD_BYTES + (i * RLDCHAR_TEX_ENTRY_BYTES)];
		const u32 flags = Rld_ReadLE16(&e[0x02]);
		const u32 width = Rld_ReadLE16(&e[0x04]);
		const u32 height = Rld_ReadLE16(&e[0x06]);
		const u32 offset = Rld_ReadLE32(&e[0x08]);
		const u32 bytes = Rld_ReadLE32(&e[0x0C]);

		// CTXT-3
		for (j = 0x30; j < RLDCHAR_TEX_ENTRY_BYTES; j++)
		{
			if (e[j] != 0u)
			{
				return RldChar_NativeFail(out, "CTXT-3", "texture %u: a reserved byte is not 0", (unsigned)i);
			}
		}
		if ((e[0x00] != RLDCHAR_TEX_FORMAT_PNG) || (e[0x01] != 0u))
		{
			return RldChar_NativeFail(out, "CTXT-3", "texture %u has format %u - this reader knows 1 (PNG)", (unsigned)i, (unsigned)e[0x00]);
		}
		if (((flags & ~RLDCHAR_TEX_FLAGS_KNOWN) != 0u) || (((flags >> RLDCHAR_TEX_WRAP_U_SHIFT) & 3u) == 3u) || (((flags >> RLDCHAR_TEX_WRAP_V_SHIFT) & 3u) == 3u) ||
		    (((flags >> RLDCHAR_TEX_ALPHA_SHIFT) & 3u) == 3u))
		{
			return RldChar_NativeFail(out, "CTXT-3", "texture %u has flags 0x%04x - an unknown bit or a reserved wrap or alpha value", (unsigned)i, (unsigned)flags);
		}

		// CTXT-4
		if (!RldChar_TexEdge(width) || !RldChar_TexEdge(height))
		{
			return RldChar_NativeFail(out, "CTXT-4", "texture %u is %u x %u - each side a power of two from %u to %u", (unsigned)i, (unsigned)width,
			                          (unsigned)height, (unsigned)RLDCHAR_TEX_EDGE_MIN, (unsigned)RLDCHAR_TEX_EDGE_MAX);
		}

		// CTXT-5
		if ((offset < tableEnd) || (offset > size) || (bytes < 8u) || (bytes > (size - offset)))
		{
			return RldChar_NativeFail(out, "CTXT-5", "texture %u: %u bytes at %u - behind the table and inside the chunk", (unsigned)i, (unsigned)bytes,
			                          (unsigned)offset);
		}
		for (j = 0; j < i; j++)
		{
			const u8 *o = &ctxt[RLDCHAR_TEX_HEAD_BYTES + (j * RLDCHAR_TEX_ENTRY_BYTES)];
			const u32 otherOffset = Rld_ReadLE32(&o[0x08]);
			const u32 otherBytes = Rld_ReadLE32(&o[0x0C]);

			if ((offset < (otherOffset + otherBytes)) && (otherOffset < (offset + bytes)))
			{
				return RldChar_NativeFail(out, "CTXT-5", "the bytes of textures %u and %u overlap", (unsigned)j, (unsigned)i);
			}
		}

		// CTXT-6
		budget += RldChar_TexMipBytes(width, height);
		if (budget > RLDCHAR_TEX_GPU_BYTES_MAX)
		{
			return RldChar_NativeFail(out, "CTXT-6", "the textures up to %u need %llu bytes with their mip levels - at most %u", (unsigned)i,
			                          (unsigned long long)budget, (unsigned)RLDCHAR_TEX_GPU_BYTES_MAX);
		}
	}

	// CTXT-7
	for (i = 0; i < count; i++)
	{
		const u8 *e = &ctxt[RLDCHAR_TEX_HEAD_BYTES + (i * RLDCHAR_TEX_ENTRY_BYTES)];
		u8 digest[32];

		Sha256(&ctxt[Rld_ReadLE32(&e[0x08])], Rld_ReadLE32(&e[0x0C]), digest);
		if (memcmp(digest, &e[0x10], 32) != 0)
		{
			return RldChar_NativeFail(out, "CTXT-7", "texture %u: hash mismatch - its bytes are altered or damaged", (unsigned)i);
		}
	}

	// CTXT-8: decode, one after the other. First the PNG's own head (the
	// signature, IHDR as the first chunk: width, height, depth, color type)
	// against the entry, so that a PNG larger than its entry is refused
	// before a byte is inflated: the decoder alone takes up to 4096 x 4096 at
	// 16 bits, far beyond the budget CTXT-6 counted from the entries.
	for (i = 0; i < count; i++)
	{
		static const u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
		const u8 *e = &ctxt[RLDCHAR_TEX_HEAD_BYTES + (i * RLDCHAR_TEX_ENTRY_BYTES)];
		const u32 width = Rld_ReadLE16(&e[0x04]);
		const u32 height = Rld_ReadLE16(&e[0x06]);
		const u8 *png = &ctxt[Rld_ReadLE32(&e[0x08])];
		const u32 pngBytes = Rld_ReadLE32(&e[0x0C]);
		struct RldPngImage image;
		const char *why = NULL;
		int result;

		if ((pngBytes < 33u) || (memcmp(png, signature, 8u) != 0) || (RldPng_BE32(&png[8]) != 13u) || (memcmp(&png[12], "IHDR", 4u) != 0))
		{
			return RldChar_NativeFail(out, "CTXT-8", "texture %u: not a PNG that starts with its IHDR", (unsigned)i);
		}
		if ((RldPng_BE32(&png[16]) != width) || (RldPng_BE32(&png[20]) != height) || (png[24] != 8u) || (png[25] != 6u))
		{
			return RldChar_NativeFail(out, "CTXT-8",
			                          "texture %u: the PNG head says %u x %u, color type %u, depth %u - the entry says %u x %u, and CTXT holds 8-bit RGBA "
			                          "(not decoded)",
			                          (unsigned)i, (unsigned)RldPng_BE32(&png[16]), (unsigned)RldPng_BE32(&png[20]), (unsigned)png[25],
			                          (unsigned)png[24], (unsigned)width, (unsigned)height);
		}

		memset(&image, 0, sizeof(image));
		result = RldPng_Decode(&ctxt[Rld_ReadLE32(&e[0x08])], Rld_ReadLE32(&e[0x0C]), &image, &why);
		if (result != RLDPNG_OK)
		{
			RldPng_Free(&image);
			return RldChar_NativeFail(out, "CTXT-8", "texture %u: %s", (unsigned)i, (why != NULL) ? why : "the PNG cannot be read");
		}
		if ((image.width != width) || (image.height != height) || (image.bitDepth != 8u) || (image.colorType != 6u))
		{
			RldChar_NativeFail(out, "CTXT-8", "texture %u: the PNG is %u x %u, color type %u, depth %u - the entry says %u x %u, and CTXT holds 8-bit RGBA",
			                   (unsigned)i, (unsigned)image.width, (unsigned)image.height, (unsigned)image.colorType, (unsigned)image.bitDepth,
			                   (unsigned)width, (unsigned)height);
			RldPng_Free(&image);
			return 0;
		}

		out->texture[i].width = width;
		out->texture[i].height = height;
		out->texture[i].flags = Rld_ReadLE16(&e[0x02]);
		out->texture[i].rgba = image.rgba;
		out->textureBytes += (u64)width * height * 4u;
	}

	return (int)count;
}

// A refusal keeps only its rule and detail: everything held is let go.
static void RldChar_NativeDrop(struct RldCharNative *out)
{
	const char *rule = out->rule;
	char detail[sizeof(out->detail)];

	memcpy(detail, out->detail, sizeof(detail));
	RldChar_FreeNative(out);
	out->state = RLDCHAR_NATIVE_REFUSED;
	out->rule = rule;
	memcpy(out->detail, detail, sizeof(detail));
}

// Step 1 of the native part: out->cnet (owned, as stored) against CNET-1..10 and 13.
// 1 = passed; else out is REFUSED and holds nothing.
static int RldChar_NativeNet(struct RldCharNative *out, u32 chriFlags, const struct RldCharHull hulls[RLDCHAR_NET_POSES])
{
	if (!RldChar_CheckNet(out->cnet, out->cnetSize, chriFlags, hulls, out))
	{
		RldChar_NativeDrop(out);
		return 0;
	}
	return 1;
}

// CNET-12, an author's wheel (WHLS version 2 or 3) only: the material of a
// mesh (the field of WHLS; version 3 with a rear mesh: its field too) has no
// texture - one color - or a texture whose sides are at most
// RLDCHAR_WHEEL_TEXTURE_EDGE_MAX. The material and the texture may be the
// body's as well; the bound then holds for that texture. Run once the
// textures are decoded (their sides are known). 1 = passed.
static int RldChar_CheckUserWheel(struct RldCharNative *out)
{
	u32 k;

	if ((out->wheel == NULL) || ((out->wheelVersion != RLDCHAR_WHEEL_VERSION_USER) && (out->wheelVersion != RLDCHAR_WHEEL_VERSION_USER3)))
	{
		return 1;
	}
	for (k = 0; k < 2u; k++)
	{
		const u8 *m;
		int texture;

		if ((k == 1u) && (out->wheelRearVertexCount == 0u))
		{
			break;
		}
		m = &out->materials[(size_t)Rld_ReadLE16((k == 0u) ? &out->wheel[0x08] : &out->wheelExtra[0x20]) * RLDCHAR_NET_MATERIAL_BYTES];
		texture = RldChar_SignedHalf(Rld_ReadLE16(&m[4]));
		if ((texture >= 0) && ((out->texture[texture].width > RLDCHAR_WHEEL_TEXTURE_EDGE_MAX) || (out->texture[texture].height > RLDCHAR_WHEEL_TEXTURE_EDGE_MAX)))
		{
			return RldChar_NativeFail(out, "CNET-12", "the %swheel (WHLS version %u) has texture %d of %u x %u - a wheel's texture may have %u on each side",
			                          (k == 0u) ? "" : "rear ", (unsigned)out->wheelVersion, texture, (unsigned)out->texture[texture].width,
			                          (unsigned)out->texture[texture].height, (unsigned)RLDCHAR_WHEEL_TEXTURE_EDGE_MAX);
		}
	}
	return 1;
}

// Step 2: CNET-11 (the textures CNET names are the ones CTXT holds), then
// CTXT-1..8 on the chunk as stored (the caller's; present 0 = no CTXT), then
// CNET-12 (an author's wheel and its texture).
// out READY, or REFUSED and holding nothing.
static void RldChar_NativeTextures(struct RldCharNative *out, const u8 *ctxt, size_t size, int present)
{
	if ((out->textureCount == 0u) && !present)
	{
		// no texture at all: an author's wheel is one color, CNET-12 holds
		out->state = RLDCHAR_NATIVE_READY;
		return;
	}
	if ((out->textureCount == 0u) || !present)
	{
		RldChar_NativeFail(out, "CNET-11", "CNET names %u textures, %s", (unsigned)out->textureCount, present ? "but there is a CTXT" : "but there is no CTXT");
		RldChar_NativeDrop(out);
		return;
	}
	if ((size >= RLDCHAR_TEX_HEAD_BYTES) && (Rld_ReadLE16(&ctxt[0x00]) == RLDCHAR_TEX_VERSION) && (Rld_ReadLE16(&ctxt[0x04]) != out->textureCount))
	{
		RldChar_NativeFail(out, "CNET-11", "CNET names %u textures, CTXT holds %u", (unsigned)out->textureCount, (unsigned)Rld_ReadLE16(&ctxt[0x04]));
		RldChar_NativeDrop(out);
		return;
	}
	if ((RldChar_CheckTextures(ctxt, size, out) == 0) || !RldChar_CheckUserWheel(out))
	{
		RldChar_NativeDrop(out);
		return;
	}
	out->state = RLDCHAR_NATIVE_READY;
}

// THE WHEELS AND THE SHAPE KEYS OF A READY PART, for the game and the views.
// Pure reads of what the checks above let through; nothing is checked again.

// One wheel mesh of WHLS: front, or the rear one (version 3 with
// RLDCHAR_WHEEL_REAR_MESH; else the rear wheels draw the front mesh, and so
// does this). vertices: count x 32 bytes {f32 position[3], f32 normal[3],
// f32 uv[2]}; indices: triangles x 3 u16.
struct RldCharWheelMesh
{
	const u8 *vertices;
	const u8 *indices;
	u32 vertexCount;
	u32 triangleCount;
	u32 material;               // < materialCount
	float radius;               // model units
	float halfWidth;
	int own;                    // 1 = the rear wheel's own mesh (version 3)
};

// 1 = an author's wheel (WHLS version 2 or 3), drawn mirrored on the -X side.
static int RldChar_WheelIsOwn(const struct RldCharNative *n)
{
	return (n->wheel != NULL) && ((n->wheelVersion == RLDCHAR_WHEEL_VERSION_USER) || (n->wheelVersion == RLDCHAR_WHEEL_VERSION_USER3));
}

// The mesh of the front (rear 0) or rear (rear 1) wheels. 0 = no wheel.
static int RldChar_WheelMesh(const struct RldCharNative *n, int rear, struct RldCharWheelMesh *out)
{
	memset(out, 0, sizeof(*out));
	if (n->wheel == NULL)
	{
		return 0;
	}
	if (rear && (n->wheelExtra != NULL) && (n->wheelRearVertexCount != 0u))
	{
		const u8 *x = n->wheelExtra;

		out->vertices = &x[RLDCHAR_WHEEL_EXTRA_BYTES];
		out->indices = &x[RLDCHAR_WHEEL_EXTRA_BYTES + (n->wheelRearVertexCount * RLDCHAR_WHEEL_VERTEX_BYTES)];
		out->vertexCount = n->wheelRearVertexCount;
		out->triangleCount = n->wheelRearTriangleCount;
		out->material = Rld_ReadLE16(&x[0x20]);
		out->radius = RldChar_F32(&x[0x24]);
		out->halfWidth = RldChar_F32(&x[0x28]);
		out->own = 1;
		return 1;
	}
	out->vertices = &n->wheel[RLDCHAR_WHEEL_HEAD_BYTES];
	out->indices = &n->wheel[RLDCHAR_WHEEL_HEAD_BYTES + (n->wheelVertexCount * RLDCHAR_WHEEL_VERTEX_BYTES)];
	out->vertexCount = n->wheelVertexCount;
	out->triangleCount = n->wheelTriangleCount;
	out->material = Rld_ReadLE16(&n->wheel[0x08]);
	out->radius = RldChar_F32(&n->wheel[0x0C]);
	out->halfWidth = RldChar_F32(&n->wheel[0x10]);
	return 1;
}

// The deltas of an MRPH target (RLDCHAR_MORPH_WIN or _LOSE): vertexCount x
// f32[3] each, little-endian bytes in cnet; *normals NULL without normal
// deltas. 0 = the part has no such target (nothing set).
static int RldChar_MorphTarget(const struct RldCharNative *n, int target, const u8 **positions, const u8 **normals)
{
	const u32 block = n->vertexCount * 12u * ((n->morphNormals != 0u) ? 2u : 1u);
	u32 index;

	if ((n->morph == NULL) || (target < 0) || (target >= (int)RLDCHAR_MORPH_TARGETS) || ((n->morphTargets & (1u << (u32)target)) == 0u))
	{
		return 0;
	}
	index = ((target == RLDCHAR_MORPH_LOSE) && ((n->morphTargets & 1u) != 0u)) ? 1u : 0u;
	*positions = &n->morph[RLDCHAR_MORPH_HEAD_BYTES + ((size_t)index * block)];
	*normals = (n->morphNormals != 0u) ? &(*positions)[(size_t)n->vertexCount * 12u] : NULL;
	return 1;
}

// Vertex v of the blend from the base pose (morphBasePose; without MRPH the
// neutral pose) toward a target at weight t (0 the base pose, 1 the target):
// position = base + t x delta, normal = base normal + t x normal delta, not
// normalized (the native mesh is not lit). A target the part does not have
// gives the base pose. Floats only, no state: the same numbers everywhere.
static void RldChar_MorphVertex(const struct RldCharNative *n, int target, float t, u32 v, float position[3], float normal[3])
{
	const u32 base = (n->poseCount == 0u) ? 0u : RLDCHAR_NET_NEUTRAL_POSE;
	const u8 *p = &n->poses[(((size_t)base * n->vertexCount) + v) * RLDCHAR_NET_VERTEX_BYTES];
	const u8 *positions = NULL;
	const u8 *normals = NULL;
	const int has = RldChar_MorphTarget(n, target, &positions, &normals);
	int c;

	for (c = 0; c < 3; c++)
	{
		position[c] = RldChar_F32(&p[4 * c]);
		normal[c] = RldChar_F32(&p[12 + (4 * c)]);
		if (has)
		{
			position[c] += t * RldChar_F32(&positions[((size_t)v * 12u) + (4u * (u32)c)]);
			if (normals != NULL)
			{
				normal[c] += t * RldChar_F32(&normals[((size_t)v * 12u) + (4u * (u32)c)]);
			}
		}
	}
}

// THE ENTRY. reader is open (Rld_OpenAs with s_rldCharFormat); chriFlags the
// flags of its CHRI; hulls the 47 frames of its CMDL (RldChar_FrameHulls).
// out is written in full: NONE without CNET, READY, or REFUSED with the rule.
// A REFUSED out holds nothing; a READY one owns cnet and the decoded
// textures (RldChar_FreeNative). CTXT is read only after CNET passed, and
// only when there is one.
static void RldChar_ReadNative(struct RldReader *reader, u32 chriFlags, const struct RldCharHull hulls[RLDCHAR_NET_POSES], struct RldCharNative *out)
{
	const char *why = NULL;
	size_t size = 0;
	int netIndex = -1;
	int texIndex = -1;
	u8 *ctxt = NULL;

	memset(out, 0, sizeof(*out));
	out->state = RLDCHAR_NATIVE_NONE;

	if ((Rld_FindEntry(reader, "CNET", &netIndex) == NULL) || (netIndex < 0))
	{
		return;
	}

	out->cnet = RldChar_ReadNativeChunk(reader, netIndex, &out->cnetSize, &why);
	if (out->cnet == NULL)
	{
		RldChar_NativeFail(out, "native-chunk", "CNET: %s", (why != NULL) ? why : "cannot be read");
		return;
	}
	if (!RldChar_NativeNet(out, chriFlags, hulls))
	{
		return;
	}

	if ((Rld_FindEntry(reader, "CTXT", &texIndex) != NULL) && (texIndex >= 0) && (out->textureCount != 0u))
	{
		ctxt = RldChar_ReadNativeChunk(reader, texIndex, &size, &why);
		if (ctxt == NULL)
		{
			RldChar_NativeFail(out, "native-chunk", "CTXT: %s", (why != NULL) ? why : "cannot be read");
			RldChar_NativeDrop(out);
			return;
		}
	}

	RldChar_NativeTextures(out, ctxt, size, texIndex >= 0);
	free(ctxt);
}

#endif // RLDCHAR_INC
