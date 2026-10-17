// rldhires.inc - the hi-res driver of a .rldchar (chunk CHRG), once, for every side
//
// AN INCLUDE, NOT A MODULE, header-only (static functions), like
// include/rldchar.inc. Three programs pull it in and run the SAME rules:
//
//   the game        (main.c unity build; the engine's hi-res driver)
//   tools/rldpack.c (hires-check, hires-add; also inside Reload Studio through
//                    tools/reloadstudio/rs_rldpack.c)
//   Reload Studio   (its "Hi-Res (Preview)" page)
//
// so that the packer never stores a model the game refuses, and the Studio
// says exactly what the game will do. u8, u16, u32 and u64 come from the
// including file. It includes <cgltf.h> but never defines
// CGLTF_IMPLEMENTATION: the game compiles cgltf's implementation in
// platform/engine_static_load.c, rldpack in tools/rldpack.c.
//
// THE CHUNK. CHRG is one optional chunk of the .rldchar envelope
// (include/rldtrack.inc, major 1, no header flag). A 16-byte header and the GLB
// file, byte for byte as the author exported it:
//
//   0x00 u16 version      1. Another version: only the hi-res part is dropped.
//   0x02 u16 headerBytes  >= 16, a multiple of 4; a newer, longer header is
//                         allowed, readers skip the rest.
//   0x04 u32 flags        0. An unknown bit: only the hi-res part is dropped.
//   0x08 u32 glbOffset    = headerBytes, from the start of the chunk.
//   0x0C u32 glbSize      glbOffset + glbSize = the chunk's size.
//
// Stored uncompressed, SHA-256 like every chunk, at most RLDHIRES_CHUNK_LIMIT.
// CHRG is NOT a known type of the envelope (RldChar_ChunkLimit answers 0, see
// the comment there): the envelope skips it like any unknown chunk, exactly as
// every older build does (include/rldtrack.inc, Rld_OpenAs: compression, size
// and limit are checked only for known types). Its envelope checks run here,
// in RldHires_ReadChunk, so a broken CHRG drops the hi-res part only - the
// driver still loads with its classic model (CHRI + CMDL).
//
// THE MODEL is glTF 2.0 binary with its BIN chunk embedded, no outside files.
// Clips are animations found by NAME from a fixed list (RLDHIRES_CLIP_*), the
// four wheels are nodes found by name (wheel_fl, wheel_fr, wheel_rl, wheel_rr).
// Axes and units are the glTF ones: +Y up, +Z the kart's front, +X the kart's
// left seen from behind, 1 metre = 64 kart model units, origin on the ground
// under the kart.
//
// THE RULES are HRG-1 .. HRG-31 (s_rldHiresRules below), each with a fixed
// level: an ERROR refuses the hi-res part (the classic model is drawn), a
// WARNING means the driver runs but something falls back to a standard (a clip,
// the wheels, flat normals), a NOTE says what is ignored. The texts are short
// and say what to do; they are the same in the game's log, rldpack hires-check
// and the Studio.
//
// NOTHING HERE PRINTS. Findings go into an RldHiresReport.
//
// Format version 1, "preview": until it is accepted, this file is the binding
// description; docs/CONTAINER_FORMAT.md gets the section when it is frozen.

#ifndef RLDHIRES_INC
#define RLDHIRES_INC

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Only when cgltf.h is not in yet: its implementation part stands OUTSIDE its
// include guard, so a second include after CGLTF_IMPLEMENTATION (tools/rldpack.c
// includes this file after compiling cgltf) would compile cgltf twice.
#if !defined(CGLTF_H_INCLUDED__)
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

//========================================================================================
// THE CHUNK CHRG
//========================================================================================

#define RLDHIRES_CHUNK_TYPE "CHRG"
#define RLDHIRES_CHUNK_VERSION 1u
#define RLDHIRES_CHUNK_HEADER_BYTES 16u           // the header this version writes; a reader accepts more
#define RLDHIRES_CHUNK_FLAGS_KNOWN 0u
#define RLDHIRES_CHUNK_LIMIT (64u * 1024u * 1024u) // the whole chunk, header included

// What RldHires_ParseChunk read. glb points INTO the chunk: the chunk must stay
// alive as long as glb is used (and as long as an RldHiresModel opened on it).
typedef struct RldHiresChunk
{
	u32 version;
	u32 headerBytes;
	u32 flags;
	u32 glbOffset;
	u32 glbSize;
	const u8 *glb;
} RldHiresChunk;

//========================================================================================
// THE CLIPS (a fixed list, the playback kind is the game's, not the file's)
//========================================================================================

enum
{
	RLDHIRES_CLIP_IDLE = 0,
	RLDHIRES_CLIP_STEER_LEFT,
	RLDHIRES_CLIP_STEER_RIGHT,
	RLDHIRES_CLIP_JUMP,
	RLDHIRES_CLIP_DRIFT_LEFT,
	RLDHIRES_CLIP_DRIFT_RIGHT,
	RLDHIRES_CLIP_HIT,
	RLDHIRES_CLIP_PODIUM_1,
	RLDHIRES_CLIP_PODIUM_2,
	RLDHIRES_CLIP_PODIUM_3,
	RLDHIRES_CLIP_LOSE,
	RLDHIRES_CLIP_MENU,
	RLDHIRES_CLIP_COUNT // 12
};

// How the game plays a clip.
//   LOOP  repeats.
//   POSE  is no motion but a scrub: time = steer share x duration, blended over
//         idle with the steer share (steer_left, steer_right).
//   ONCE  plays once from the entry into the state and holds its last pose
//         while the state lasts (jump, hit).
enum
{
	RLDHIRES_PLAY_LOOP = 0,
	RLDHIRES_PLAY_POSE,
	RLDHIRES_PLAY_ONCE
};

// The names, exactly as written in the file (lower case), by RLDHIRES_CLIP_*.
static const char *const s_rldHiresClipName[RLDHIRES_CLIP_COUNT] = {
    "idle", "steer_left", "steer_right", "jump", "drift_left", "drift_right",
    "hit", "podium_1", "podium_2", "podium_3", "lose", "menu"};

// The playback kind, by RLDHIRES_CLIP_*.
static const u8 s_rldHiresClipPlay[RLDHIRES_CLIP_COUNT] = {
    RLDHIRES_PLAY_LOOP, // idle
    RLDHIRES_PLAY_POSE, // steer_left
    RLDHIRES_PLAY_POSE, // steer_right
    RLDHIRES_PLAY_ONCE, // jump
    RLDHIRES_PLAY_LOOP, // drift_left
    RLDHIRES_PLAY_LOOP, // drift_right
    RLDHIRES_PLAY_ONCE, // hit
    RLDHIRES_PLAY_LOOP, // podium_1
    RLDHIRES_PLAY_LOOP, // podium_2
    RLDHIRES_PLAY_LOOP, // podium_3
    RLDHIRES_PLAY_LOOP, // lose
    RLDHIRES_PLAY_LOOP  // menu
};

// The fallback of a missing clip: the next clip of its chain, -1 = the rest
// pose (the node TRS of the file). Every chain ends at the rest pose, so no
// state can end in a crash or an invisible driver:
//   idle -> rest; steer_left, steer_right, jump, hit, podium_1, lose, menu -> idle;
//   drift_left -> steer_left AT FULL LOCK; drift_right -> steer_right AT FULL LOCK;
//   podium_2 -> podium_1; podium_3 -> podium_2.
static const signed char s_rldHiresClipFallback[RLDHIRES_CLIP_COUNT] = {
    -1,                        // idle
    RLDHIRES_CLIP_IDLE,        // steer_left
    RLDHIRES_CLIP_IDLE,        // steer_right
    RLDHIRES_CLIP_IDLE,        // jump
    RLDHIRES_CLIP_STEER_LEFT,  // drift_left (full lock)
    RLDHIRES_CLIP_STEER_RIGHT, // drift_right (full lock)
    RLDHIRES_CLIP_IDLE,        // hit
    RLDHIRES_CLIP_IDLE,        // podium_1
    RLDHIRES_CLIP_PODIUM_1,    // podium_2
    RLDHIRES_CLIP_PODIUM_2,    // podium_3
    RLDHIRES_CLIP_IDLE,        // lose
    RLDHIRES_CLIP_IDLE         // menu
};

// The name of a clip, "?" outside the list.
static const char *RldHires_ClipName(int clip)
{
	return ((clip >= 0) && (clip < RLDHIRES_CLIP_COUNT)) ? s_rldHiresClipName[clip] : "?";
}

// The playback kind of a clip (RLDHIRES_PLAY_*); LOOP outside the list.
static int RldHires_ClipPlay(int clip)
{
	return ((clip >= 0) && (clip < RLDHIRES_CLIP_COUNT)) ? (int)s_rldHiresClipPlay[clip] : RLDHIRES_PLAY_LOOP;
}

// The next clip of the chain, -1 = rest pose (also for a clip outside the list).
static int RldHires_ClipFallback(int clip)
{
	return ((clip >= 0) && (clip < RLDHIRES_CLIP_COUNT)) ? (int)s_rldHiresClipFallback[clip] : -1;
}

// The clip of a name, -1 for any other name (also NULL). Exact, case counts.
static int RldHires_ClipByName(const char *name)
{
	int c;

	if (name == NULL)
	{
		return -1;
	}
	for (c = 0; c < RLDHIRES_CLIP_COUNT; c++)
	{
		if (strcmp(name, s_rldHiresClipName[c]) == 0)
		{
			return c;
		}
	}
	return -1;
}

//========================================================================================
// THE WHEELS (four nodes by name, all or none)
//========================================================================================
//
// f/r = front/rear, l/r = left/right of the kart. The node's origin is the
// wheel middle, its local X axis the axle. Each must be a node WITH a mesh and
// WITHOUT a skin. Only all four or none: when one is missing, the ones found are
// not drawn either and the engine puts its standard 3D wheels at the retail
// wheel points (HRG-20). The subtree of a wheel node is never part of the body.

enum
{
	RLDHIRES_WHEEL_FL = 0,
	RLDHIRES_WHEEL_FR,
	RLDHIRES_WHEEL_RL,
	RLDHIRES_WHEEL_RR,
	RLDHIRES_WHEEL_COUNT // 4
};

static const char *const s_rldHiresWheelName[RLDHIRES_WHEEL_COUNT] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};

// The wheel of a node name, -1 for any other name (also NULL).
static int RldHires_WheelByName(const char *name)
{
	int w;

	if (name == NULL)
	{
		return -1;
	}
	for (w = 0; w < RLDHIRES_WHEEL_COUNT; w++)
	{
		if (strcmp(name, s_rldHiresWheelName[w]) == 0)
		{
			return w;
		}
	}
	return -1;
}

//========================================================================================
// LIMITS AND THE BUDGET
//========================================================================================

#define RLDHIRES_JOINTS_MAX 64              // per skin (format limit)
#define RLDHIRES_SKINS_MAX 1
#define RLDHIRES_TEXTURE_EDGE_MAX 2048u     // format limit of a PNG edge (the game's image path, EngineStatic_ReadImage)
#define RLDHIRES_TEXTURE_EDGE_MIN 16u       // the smallest edge the game uploads (RldHires_UpEdge)
#define RLDHIRES_BOUNDS_MAX_M 4.0f          // the bind pose, per axis (max - min), metres
#define RLDHIRES_UNITS_PER_METRE 64.0f      // kart model units per glTF metre

// THE BUDGET of one hi-res driver: measured in the load run of this round (H5);
// placeholder until then (the triangles set high enough that the load run can
// load every variant it measures). Over it, the driver falls back to its classic model
// (HRG-27, RldHires_CheckBudget), with a line in the log and the Studio.
#define RLDHIRES_BUDGET_TRIANGLES 400000u                     // all drawn primitives, wheels included
#define RLDHIRES_BUDGET_TEXTURE_EDGE 2048u                    // the largest edge after the round-up (RldHires_UpEdge)
#define RLDHIRES_BUDGET_TEXTURE_BYTES (64u * 1024u * 1024u)   // RGBA8 level 0 of every image, summed

//========================================================================================
// THE REPORT
//========================================================================================

enum
{
	RLDHIRES_NOTE = 0,    // something in the file is ignored
	RLDHIRES_WARNING = 1, // the driver runs, but something falls back to a standard
	RLDHIRES_ERROR = 2    // the hi-res part is refused, the classic model is drawn
};

#define RLDHIRES_REPORT_MAX 64
#define RLDHIRES_TEXT_MAX 200

typedef struct RldHiresMessage
{
	int level;                   // RLDHIRES_NOTE / _WARNING / _ERROR
	char rule[8];                // "HRG-7"
	char text[RLDHIRES_TEXT_MAX];
} RldHiresMessage;

// Messages in the order they were found (the order is fixed: the same file
// gives the same report). Past RLDHIRES_REPORT_MAX a message is only counted
// (dropped); errors, warnings and notes count every message.
typedef struct RldHiresReport
{
	int count;
	int dropped;
	int errors;
	int warnings;
	int notes;
	RldHiresMessage msg[RLDHIRES_REPORT_MAX];
} RldHiresReport;

// Every rule: its id, its level, and what it is about (for a help list and the
// self-test - the message texts themselves are made where the rule fires).
typedef struct RldHiresRule
{
	const char *id;
	int level;
	const char *about;
} RldHiresRule;

#define RLDHIRES_RULE_COUNT 31

static const RldHiresRule s_rldHiresRules[RLDHIRES_RULE_COUNT] = {
    {"HRG-1", RLDHIRES_ERROR, "CHRG version is not 1"},
    {"HRG-2", RLDHIRES_ERROR, "CHRG header size below 16, not a multiple of 4 or past the chunk"},
    {"HRG-3", RLDHIRES_ERROR, "CHRG flags with an unknown bit"},
    {"HRG-4", RLDHIRES_ERROR, "CHRG glbOffset is not headerBytes, or glbOffset + glbSize is not the chunk size"},
    {"HRG-5", RLDHIRES_ERROR, "CHRG in the envelope: compressed, sizes differ, over 64 MiB, hash"},
    {"HRG-6", RLDHIRES_ERROR, "GLB header: magic, version 2, length"},
    {"HRG-7", RLDHIRES_ERROR, "the glTF cannot be read or is damaged (cgltf parse, buffers, validate)"},
    {"HRG-8", RLDHIRES_ERROR, "extensionsRequired other than KHR_materials_unlit"},
    {"HRG-9", RLDHIRES_ERROR, "a buffer or image refers to an outside file or a data URI"},
    {"HRG-10", RLDHIRES_ERROR, "a drawn primitive is not triangles"},
    {"HRG-11", RLDHIRES_ERROR, "a drawn primitive has no POSITION, or not float VEC3"},
    {"HRG-12", RLDHIRES_ERROR, "more than one skin"},
    {"HRG-13", RLDHIRES_ERROR, "more than 64 joints, or too few inverse bind matrices"},
    {"HRG-14", RLDHIRES_ERROR, "JOINTS_0 / WEIGHTS_0 missing on a skinned primitive, wrong type, or a joint past the skin"},
    {"HRG-15", RLDHIRES_WARNING, "NORMAL missing: flat normals"},
    {"HRG-16", RLDHIRES_ERROR, "a texture is not PNG (JPEG, WebP, KTX2, damaged)"},
    {"HRG-17", RLDHIRES_ERROR, "a texture edge above 2048"},
    {"HRG-18", RLDHIRES_ERROR, "bind pose larger than 4 m on an axis, or not finite"},
    {"HRG-19", RLDHIRES_WARNING, "a clip is missing: its fallback is used"},
    {"HRG-20", RLDHIRES_WARNING, "wheels: not all four wheel nodes usable, standard wheels"},
    {"HRG-21", RLDHIRES_NOTE, "an animation name is not a clip name, or a clip name twice: ignored"},
    {"HRG-22", RLDHIRES_NOTE, "CUBICSPLINE is read linear between the key values"},
    {"HRG-23", RLDHIRES_NOTE, "morph targets are ignored"},
    {"HRG-24", RLDHIRES_NOTE, "cameras and lights are ignored"},
    {"HRG-25", RLDHIRES_NOTE, "alphaMode BLEND is drawn as MASK with cutoff 0.5"},
    {"HRG-26", RLDHIRES_NOTE, "metallicRoughness, occlusion and emissive are ignored"},
    {"HRG-27", RLDHIRES_ERROR, "over budget: triangles, texture edge, texture bytes"},
    {"HRG-28", RLDHIRES_ERROR, "nothing to draw: no body mesh in the scene"},
    {"HRG-29", RLDHIRES_ERROR, "a sparse accessor in geometry, skin or clips"},
    {"HRG-30", RLDHIRES_NOTE, "a PNG is not 8-bit RGB or RGBA: read as RGBA8"},
    {"HRG-31", RLDHIRES_NOTE, "wheel node details: a name twice, child nodes not drawn"},
};

// The level of a rule id, -1 for an unknown id.
static int RldHires_RuleLevel(const char *id)
{
	int i;

	for (i = 0; i < RLDHIRES_RULE_COUNT; i++)
	{
		if (strcmp(s_rldHiresRules[i].id, id) == 0)
		{
			return s_rldHiresRules[i].level;
		}
	}
	return -1;
}

static void RldHires_ReportClear(RldHiresReport *rep)
{
	if (rep != NULL)
	{
		memset(rep, 0, sizeof(*rep));
	}
}

// Adds a message under a rule; the level is the rule's. rep may be NULL.
static void RldHires_Say(RldHiresReport *rep, const char *rule, const char *format, ...)
{
	int level = RldHires_RuleLevel(rule);
	va_list list;

	if (rep == NULL)
	{
		return;
	}
	if (level < 0)
	{
		level = RLDHIRES_ERROR; // a rule id outside the table is a bug of this file; never weaker than an error
	}
	if (level == RLDHIRES_ERROR)
	{
		rep->errors++;
	}
	else if (level == RLDHIRES_WARNING)
	{
		rep->warnings++;
	}
	else
	{
		rep->notes++;
	}
	if (rep->count >= RLDHIRES_REPORT_MAX)
	{
		rep->dropped++;
		return;
	}
	{
		RldHiresMessage *m = &rep->msg[rep->count++];

		m->level = level;
		snprintf(m->rule, sizeof(m->rule), "%s", rule);
		va_start(list, format);
		vsnprintf(m->text, sizeof(m->text), format, list);
		va_end(list);
	}
}

// 1 when the report holds a message of this rule (counted ones included only
// as far as they are held).
static int RldHires_ReportHas(const RldHiresReport *rep, const char *rule)
{
	int i;

	for (i = 0; i < rep->count; i++)
	{
		if (strcmp(rep->msg[i].rule, rule) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static const char *RldHires_LevelName(int level)
{
	return (level == RLDHIRES_ERROR) ? "error" : (level == RLDHIRES_WARNING) ? "warning" : "note";
}

// A name from the file, safe for a message: at most 40 characters, control
// characters and quotes become '?', NULL or empty becomes "#<index>".
static void RldHires_Name(char *dst, size_t size, const char *name, size_t index)
{
	size_t at = 0;

	if ((name == NULL) || (name[0] == '\0'))
	{
		snprintf(dst, size, "#%u", (unsigned)index);
		return;
	}
	while ((name[at] != '\0') && (at < 40u) && ((at + 1u) < size))
	{
		const unsigned char c = (unsigned char)name[at];

		dst[at] = ((c < 0x20u) || (c == 0x7fu) || (c == '\'') || (c == '"')) ? '?' : (char)c;
		at++;
	}
	dst[at] = '\0';
}

//========================================================================================
// THE CHUNK: READ, PARSE, BUILD
//========================================================================================

static u32 RldHires_LE16(const u8 *at)
{
	return (u32)at[0] | ((u32)at[1] << 8);
}

static u32 RldHires_LE32(const u8 *at)
{
	return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) | ((u32)at[3] << 24);
}

static void RldHires_PutLE16(u8 *at, u32 value)
{
	at[0] = (u8)(value & 0xffu);
	at[1] = (u8)((value >> 8) & 0xffu);
}

static void RldHires_PutLE32(u8 *at, u32 value)
{
	at[0] = (u8)(value & 0xffu);
	at[1] = (u8)((value >> 8) & 0xffu);
	at[2] = (u8)((value >> 16) & 0xffu);
	at[3] = (u8)((value >> 24) & 0xffu);
}

// The header of a CHRG chunk (the chunk's bytes, after the envelope's hash).
// 1 = out filled, glb points into chunk. 0 = refused: *rule the rule id
// (HRG-1..4), detail a short English text. rule and detail may be NULL.
static int RldHires_ParseChunk(const u8 *chunk, size_t size, RldHiresChunk *out, const char **rule, char *detail, int detailSize)
{
	const char *r = NULL;
	char text[RLDHIRES_TEXT_MAX];

	text[0] = '\0';
	if (out != NULL)
	{
		memset(out, 0, sizeof(*out));
	}

	if ((chunk == NULL) || (size < RLDHIRES_CHUNK_HEADER_BYTES))
	{
		r = "HRG-2";
		snprintf(text, sizeof(text), "CHRG is %u bytes - shorter than its 16-byte header", (unsigned)size);
	}
	else if (size > RLDHIRES_CHUNK_LIMIT)
	{
		r = "HRG-5";
		snprintf(text, sizeof(text), "CHRG is %u bytes - the limit is 64 MiB", (unsigned)size);
	}
	else
	{
		const u32 version = RldHires_LE16(&chunk[0x00]);
		const u32 headerBytes = RldHires_LE16(&chunk[0x02]);
		const u32 flags = RldHires_LE32(&chunk[0x04]);
		const u32 glbOffset = RldHires_LE32(&chunk[0x08]);
		const u32 glbSize = RldHires_LE32(&chunk[0x0C]);

		if (version != RLDHIRES_CHUNK_VERSION)
		{
			r = "HRG-1";
			snprintf(text, sizeof(text), "CHRG version %u is not known to this game (it reads version 1) - the classic model is used", (unsigned)version);
		}
		else if ((headerBytes < RLDHIRES_CHUNK_HEADER_BYTES) || ((headerBytes & 3u) != 0u) || ((size_t)headerBytes > size))
		{
			r = "HRG-2";
			snprintf(text, sizeof(text), "CHRG header size %u is wrong - it must be 16 or more, a multiple of 4, inside the chunk", (unsigned)headerBytes);
		}
		else if ((flags & ~RLDHIRES_CHUNK_FLAGS_KNOWN) != 0u)
		{
			r = "HRG-3";
			snprintf(text, sizeof(text), "CHRG flags 0x%08x have a bit this game does not know - the classic model is used", (unsigned)flags);
		}
		else if ((glbOffset != headerBytes) || ((u64)glbOffset + (u64)glbSize != (u64)size))
		{
			r = "HRG-4";
			snprintf(text, sizeof(text), "CHRG says the GLB is %u bytes at %u, but the chunk is %u bytes with a %u-byte header",
			         (unsigned)glbSize, (unsigned)glbOffset, (unsigned)size, (unsigned)headerBytes);
		}
		else if (out != NULL)
		{
			out->version = version;
			out->headerBytes = headerBytes;
			out->flags = flags;
			out->glbOffset = glbOffset;
			out->glbSize = glbSize;
			out->glb = chunk + glbOffset;
		}
	}

	if (rule != NULL)
	{
		*rule = r;
	}
	if ((detail != NULL) && (detailSize > 0))
	{
		snprintf(detail, (size_t)detailSize, "%s", text);
	}
	return r == NULL;
}

// The bytes of a CHRG chunk for a GLB: the 16-byte header of version 1 and the
// GLB unchanged. malloc'ed (the caller frees), NULL when the chunk would pass
// RLDHIRES_CHUNK_LIMIT or memory runs out. The same GLB gives the same bytes.
static u8 *RldHires_BuildChunk(const u8 *glb, u32 glbSize, size_t *sizeOut)
{
	u8 *chunk;

	if (sizeOut != NULL)
	{
		*sizeOut = 0;
	}
	if ((glb == NULL) || ((u64)glbSize + RLDHIRES_CHUNK_HEADER_BYTES > (u64)RLDHIRES_CHUNK_LIMIT))
	{
		return NULL;
	}
	chunk = (u8 *)malloc((size_t)glbSize + RLDHIRES_CHUNK_HEADER_BYTES);
	if (chunk == NULL)
	{
		return NULL;
	}
	RldHires_PutLE16(&chunk[0x00], RLDHIRES_CHUNK_VERSION);
	RldHires_PutLE16(&chunk[0x02], RLDHIRES_CHUNK_HEADER_BYTES);
	RldHires_PutLE32(&chunk[0x04], 0u);
	RldHires_PutLE32(&chunk[0x08], RLDHIRES_CHUNK_HEADER_BYTES);
	RldHires_PutLE32(&chunk[0x0C], glbSize);
	memcpy(&chunk[RLDHIRES_CHUNK_HEADER_BYTES], glb, glbSize);
	if (sizeOut != NULL)
	{
		*sizeOut = (size_t)glbSize + RLDHIRES_CHUNK_HEADER_BYTES;
	}
	return chunk;
}

#if defined(RLDTRACK_INC)

// The envelope checks of a CHRG directory entry, the ones the envelope only
// runs for known types (include/rldtrack.inc, Rld_OpenAs): not compressed,
// size_stored == size_raw, at most RLDHIRES_CHUNK_LIMIT. NULL = fine, else a
// fixed text (rule HRG-5). Pure, so that the self-test needs no file.
static const char *RldHires_EntryCheck(const u8 *entry)
{
	const u64 stored = Rld_ReadLE64(&entry[0x10]);
	const u64 raw = Rld_ReadLE64(&entry[0x18]);
	const u32 method = Rld_ReadLE32(&entry[0x04]) & 0x0fu;

	if (memcmp(entry, RLDHIRES_CHUNK_TYPE, 4) != 0)
	{
		return "not a CHRG entry";
	}
	if (method != RLD_COMPRESSION_NONE)
	{
		return "CHRG is compressed - it must be stored";
	}
	if (stored != raw)
	{
		return "size_stored and size_raw of CHRG differ";
	}
	if (raw > RLDHIRES_CHUNK_LIMIT)
	{
		return "CHRG is larger than 64 MiB";
	}
	return NULL;
}

// The index of CHRG in an opened reader's directory, -1 = none (then the
// driver is read exactly as without this file).
static int RldHires_FindChunk(const struct RldReader *reader)
{
	int index = -1;

	if (Rld_FindEntry(reader, RLDHIRES_CHUNK_TYPE, &index) == NULL)
	{
		return -1;
	}
	return index;
}

// Rld_ReadChunk for CHRG: RldHires_EntryCheck before the malloc, then the
// bytes and their SHA-256. malloc'ed (the caller frees); NULL with a fixed text
// in *errorOut (rule HRG-5).
static u8 *RldHires_ReadChunk(struct RldReader *reader, int index, size_t *sizeOut, const char **errorOut)
{
	const u8 *entry;
	u8 digest[32];
	u8 *bytes;
	u64 offset;
	u64 raw;
	const char *why;

	*sizeOut = 0;
	*errorOut = NULL;
	if ((index < 0) || (index >= reader->chunkCount))
	{
		*errorOut = "no CHRG";
		return NULL;
	}
	entry = &reader->directory[index * RLD_DIR_ENTRY_SIZE];
	why = RldHires_EntryCheck(entry);
	if (why != NULL)
	{
		*errorOut = why;
		return NULL;
	}
	offset = Rld_ReadLE64(&entry[0x08]);
	raw = Rld_ReadLE64(&entry[0x18]);

	bytes = (u8 *)malloc((size_t)raw ? (size_t)raw : 1u);
	if (bytes == NULL)
	{
		*errorOut = "out of memory";
		return NULL;
	}
	if (!Rld_ReadAt(reader, offset, bytes, (size_t)raw))
	{
		free(bytes);
		*errorOut = "CHRG cannot be read - the file is shorter than its own directory says";
		return NULL;
	}
	Sha256(bytes, (size_t)raw, digest);
	if (memcmp(digest, &entry[RLD_DIR_HASH_OFFSET], 32) != 0)
	{
		free(bytes);
		*errorOut = "CHRG hash mismatch - the chunk is altered or damaged";
		return NULL;
	}
	*sizeOut = (size_t)raw;
	return bytes;
}

#endif // RLDTRACK_INC

//========================================================================================
// THE MODEL
//========================================================================================

// One image of the GLB (data->images[index]), read from its PNG header only -
// nothing is decoded here.
typedef struct RldHiresImage
{
	int index;          // into data->images
	int used;           // 1 = a material uses it as base colour or normal map. Only used images are
	                    // checked, counted for the budget and loaded; the rest stays in the file, unread.
	int isPng;          // 1 = PNG signature and a readable IHDR
	const u8 *png;      // the image's bytes inside the GLB's BIN chunk (NULL if none)
	u32 pngSize;
	u32 fileWidth;      // IHDR
	u32 fileHeight;
	u32 colorType;      // IHDR: 0 gray, 2 RGB, 3 palette, 4 gray + alpha, 6 RGBA
	u32 bitDepth;       // IHDR
	u32 width;          // after RldHires_UpEdge; 0 when an edge is above 2048
	u32 height;
	u64 mip0Bytes;      // width x height x 4: what the game holds as level 0
} RldHiresImage;

// An opened hi-res model. data and every pointer of it point into the GLB that
// was opened: the GLB bytes must stay alive until RldHires_Close.
typedef struct RldHiresModel
{
	cgltf_data *data;
	const u8 *glb;
	u32 glbSize;
	int scene;                              // the scene drawn: data->scene, else 0; -1 = no scenes, every root node

	int clipAnim[RLDHIRES_CLIP_COUNT];      // animation index of each clip, -1 = not in the file
	float clipSeconds[RLDHIRES_CLIP_COUNT]; // its duration (the largest input time), 0 without

	int wheelNode[RLDHIRES_WHEEL_COUNT];    // node index of each wheel; all -1 unless all four are usable
	float wheelRadius[RLDHIRES_WHEEL_COUNT];// max distance of its mesh's positions from the node's X axis, in the node's own space (its scale not applied)

	int skin;                               // -1 = none, else 0
	u32 joints;                             // joints of the skin, 0 without

	int *drawNodes;                         // the body: nodes with a mesh in the drawn scene, depth first in node order;
	int drawNodeCount;                      // wheel nodes and their subtrees are never in it
	int skinnedNodeCount;                   // of drawNodes, those with a skin

	u64 triangles;                          // every drawn primitive, the four wheels included when they are used
	u64 vertices;
	u32 primitives;

	RldHiresImage *images;                  // one per data->images
	int imageCount;
	u64 textureBytes;                       // sum of mip0Bytes
	u32 textureEdgeMax;                     // the largest width or height after the round-up

	float boundsMin[3];                     // bind pose, metres: rigid nodes with their world matrix, skinned
	float boundsMax[3];                     // meshes as they stand in mesh space (glTF ignores their node matrix)

	int usable;                             // 1 = RldHires_Open found no error
	int wheelsFound;                        // wheel nodes found and usable (0..4) - for the reports; wheelNode needs all four
} RldHiresModel;

// The edge the game makes of an image edge: the next power of two, at least
// 16; 0 for an edge of 0 or one whose power of two passes 2048. The rule of
// EngineStatic_UpEdge (platform/engine_static_load.c), kept pure here.
static u32 RldHires_UpEdge(u32 edge)
{
	u32 e = RLDHIRES_TEXTURE_EDGE_MIN;

	if (edge == 0u)
	{
		return 0u;
	}
	while (e < edge)
	{
		e *= 2u;
		if (e > RLDHIRES_TEXTURE_EDGE_MAX)
		{
			return 0u;
		}
	}
	return e;
}

static void RldHires_Close(RldHiresModel *m)
{
	if (m == NULL)
	{
		return;
	}
	if (m->data != NULL)
	{
		cgltf_free(m->data);
	}
	free(m->drawNodes);
	free(m->images);
	memset(m, 0, sizeof(*m));
}

// The first clip of the chain of clip that the file has, -1 = rest pose.
// viaFallback (may be NULL): 0 = the clip itself, 1 = a fallback.
// fullLock (may be NULL): 1 when the resolved clip is a steer clip standing in
// for a drift clip - then the engine samples it at its END (full lock), not by
// the steer share.
static int RldHires_ClipResolve(const RldHiresModel *m, int clip, int *viaFallback, int *fullLock)
{
	int c = clip;
	int lock = 0;
	int steps = 0;

	if (viaFallback != NULL)
	{
		*viaFallback = 0;
	}
	if (fullLock != NULL)
	{
		*fullLock = 0;
	}
	while ((c >= 0) && (c < RLDHIRES_CLIP_COUNT) && (steps <= RLDHIRES_CLIP_COUNT))
	{
		if ((m != NULL) && (m->clipAnim[c] >= 0))
		{
			if (viaFallback != NULL)
			{
				*viaFallback = (c != clip);
			}
			if (fullLock != NULL)
			{
				*fullLock = lock && (RldHires_ClipPlay(c) == RLDHIRES_PLAY_POSE);
			}
			return c;
		}
		if ((c == RLDHIRES_CLIP_DRIFT_LEFT) || (c == RLDHIRES_CLIP_DRIFT_RIGHT))
		{
			lock = 1;
		}
		c = RldHires_ClipFallback(c);
		steps++;
	}
	if (viaFallback != NULL)
	{
		*viaFallback = 1;
	}
	return -1;
}

// The bytes of a buffer view, NULL when it lies outside its buffer (cgltf_validate
// checked that already; this is the second line).
static const u8 *RldHires_ViewBytes(const cgltf_buffer_view *view, u32 *sizeOut)
{
	*sizeOut = 0;
	if ((view == NULL) || (view->buffer == NULL) || (view->buffer->data == NULL))
	{
		return NULL;
	}
	if ((view->offset > view->buffer->size) || (view->size > view->buffer->size - view->offset))
	{
		return NULL;
	}
	*sizeOut = (u32)view->size;
	return (const u8 *)view->buffer->data + view->offset;
}

// The bytes of an image inside the GLB, NULL when it has none (an outside or
// data URI is refused before, HRG-9).
static const u8 *RldHires_ImageBytes(const RldHiresModel *m, int image, u32 *sizeOut)
{
	*sizeOut = 0;
	if ((m == NULL) || (m->data == NULL) || (image < 0) || ((cgltf_size)image >= m->data->images_count))
	{
		return NULL;
	}
	return RldHires_ViewBytes(m->data->images[image].buffer_view, sizeOut);
}

// cgltf never opens a file for this reader: a GLB with an outside buffer is
// refused before cgltf_load_buffers (HRG-9), and this callback is the second line.
static cgltf_result RldHires_NoFile(const struct cgltf_memory_options *memory, const struct cgltf_file_options *file, const char *path, cgltf_size *size, void **out)
{
	(void)memory;
	(void)file;
	(void)path;
	(void)size;
	(void)out;
	return cgltf_result_file_not_found;
}

static const char *RldHires_CgltfText(cgltf_result r)
{
	switch (r)
	{
	case cgltf_result_data_too_short:
		return "data too short";
	case cgltf_result_unknown_format:
		return "unknown format";
	case cgltf_result_invalid_json:
		return "invalid JSON";
	case cgltf_result_invalid_gltf:
		return "invalid glTF";
	case cgltf_result_out_of_memory:
		return "out of memory";
	case cgltf_result_legacy_gltf:
		return "glTF 1.0";
	default:
		return "cannot be read";
	}
}

// 1 = the accessor is float VEC3 (POSITION, NORMAL).
static int RldHires_IsFloat3(const cgltf_accessor *a)
{
	return (a != NULL) && (a->type == cgltf_type_vec3) && (a->component_type == cgltf_component_type_r_32f);
}

// The checks and counts of one mesh node (a body node or a wheel). world: the
// node's world matrix, NULL for a skinned node (bind pose = mesh space).
// Grows the bounds. what: "mesh 'x'" for the messages.
static void RldHires_CheckMeshNode(RldHiresModel *m, RldHiresReport *rep, const cgltf_node *node, const float *world,
                                   int *seenNormalMissing, int *seenMorph, int *seenSparse, int *seenNonFinite)
{
	const cgltf_mesh *mesh = node->mesh;
	char meshName[48];
	cgltf_size p;

	RldHires_Name(meshName, sizeof(meshName), mesh->name, cgltf_mesh_index(m->data, mesh));

	for (p = 0; p < mesh->primitives_count; p++)
	{
		const cgltf_primitive *prim = &mesh->primitives[p];
		const cgltf_accessor *pos = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
		const cgltf_accessor *nrm = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
		const cgltf_accessor *joints = cgltf_find_accessor(prim, cgltf_attribute_type_joints, 0);
		const cgltf_accessor *weights = cgltf_find_accessor(prim, cgltf_attribute_type_weights, 0);
		cgltf_size v;
		cgltf_size n;

		m->primitives++;

		if (prim->type != cgltf_primitive_type_triangles)
		{
			RldHires_Say(rep, "HRG-10", "mesh '%s' primitive %u is not triangles (mode %d) - export triangles", meshName, (unsigned)p,
			             (int)prim->type - 1);
			continue;
		}
		if (!RldHires_IsFloat3(pos))
		{
			RldHires_Say(rep, "HRG-11", (pos == NULL) ? "mesh '%s' primitive %u has no POSITION" : "mesh '%s' primitive %u: POSITION is not float VEC3",
			             meshName, (unsigned)p);
			continue;
		}
		n = (prim->indices != NULL) ? prim->indices->count : pos->count;
		if ((n % 3u) != 0u)
		{
			RldHires_Say(rep, "HRG-10", "mesh '%s' primitive %u has %u indices - not a whole number of triangles", meshName, (unsigned)p, (unsigned)n);
			continue;
		}
		if (pos->is_sparse || ((prim->indices != NULL) && prim->indices->is_sparse) || ((nrm != NULL) && nrm->is_sparse) ||
		    ((joints != NULL) && joints->is_sparse) || ((weights != NULL) && weights->is_sparse))
		{
			if (!*seenSparse)
			{
				RldHires_Say(rep, "HRG-29", "mesh '%s' uses a sparse accessor - export without sparse data", meshName);
			}
			*seenSparse = 1;
			continue;
		}

		m->triangles += (u64)(n / 3u);
		m->vertices += (u64)pos->count;

		if ((nrm == NULL) || !RldHires_IsFloat3(nrm))
		{
			if (!*seenNormalMissing)
			{
				RldHires_Say(rep, "HRG-15", "mesh '%s' has no normals - the game draws it with flat normals", meshName);
			}
			*seenNormalMissing = 1;
		}
		if ((prim->targets_count != 0u) && !*seenMorph)
		{
			RldHires_Say(rep, "HRG-23", "mesh '%s' has morph targets (shape keys) - they are ignored", meshName);
			*seenMorph = 1;
		}

		if (node->skin != NULL)
		{
			const cgltf_size jointCount = node->skin->joints_count;
			int okJ = (joints != NULL) && (joints->type == cgltf_type_vec4) &&
			          ((joints->component_type == cgltf_component_type_r_8u) || (joints->component_type == cgltf_component_type_r_16u));
			int okW = (weights != NULL) && (weights->type == cgltf_type_vec4) &&
			          ((weights->component_type == cgltf_component_type_r_32f) ||
			           (weights->normalized && ((weights->component_type == cgltf_component_type_r_8u) || (weights->component_type == cgltf_component_type_r_16u))));

			if (!okJ || !okW)
			{
				RldHires_Say(rep, "HRG-14", "mesh '%s' is skinned but %s - export with skin weights (4 per vertex)", meshName,
				             (joints == NULL) ? "has no JOINTS_0" : (weights == NULL) ? "has no WEIGHTS_0" : !okJ ? "JOINTS_0 is not u8/u16 VEC4" : "WEIGHTS_0 is not float or normalized u8/u16 VEC4");
			}
			else
			{
				for (v = 0; v < joints->count; v++)
				{
					cgltf_uint j[4] = {0, 0, 0, 0};
					int k;

					cgltf_accessor_read_uint(joints, v, j, 4);
					for (k = 0; k < 4; k++)
					{
						if ((cgltf_size)j[k] >= jointCount)
						{
							RldHires_Say(rep, "HRG-14", "mesh '%s': vertex %u uses joint %u, the skin has %u", meshName, (unsigned)v, (unsigned)j[k], (unsigned)jointCount);
							v = joints->count;
							break;
						}
					}
				}
			}
		}

		// The bind pose bounds, every position.
		for (v = 0; v < pos->count; v++)
		{
			float q[3] = {0.0f, 0.0f, 0.0f};
			float w[3];
			int k;

			cgltf_accessor_read_float(pos, v, q, 3);
			if (world != NULL)
			{
				for (k = 0; k < 3; k++)
				{
					w[k] = world[k] * q[0] + world[4 + k] * q[1] + world[8 + k] * q[2] + world[12 + k];
				}
			}
			else
			{
				w[0] = q[0];
				w[1] = q[1];
				w[2] = q[2];
			}
			for (k = 0; k < 3; k++)
			{
				if (!isfinite(w[k]))
				{
					*seenNonFinite = 1; // HRG-18
					continue;
				}
				if (w[k] < m->boundsMin[k])
				{
					m->boundsMin[k] = w[k];
				}
				if (w[k] > m->boundsMax[k])
				{
					m->boundsMax[k] = w[k];
				}
			}
		}
	}
}

// The PNG header of an image (no decoding). Fills img; messages for HRG-16/17/30.
static void RldHires_CheckImage(RldHiresModel *m, RldHiresReport *rep, int index, RldHiresImage *img)
{
	static const u8 pngSig[8] = {0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
	const cgltf_image *gi = &m->data->images[index];
	char name[48];
	u32 size = 0;
	const u8 *bytes;

	memset(img, 0, sizeof(*img));
	img->index = index;
	RldHires_Name(name, sizeof(name), gi->name, (size_t)index);

	if (gi->uri != NULL)
	{
		RldHires_Say(rep, "HRG-9", "texture %d ('%s') %s - embed textures in the .glb", index, name,
		             (strncmp(gi->uri, "data:", 5) == 0) ? "is a data URI" : "refers to an outside file");
		return;
	}
	bytes = RldHires_ViewBytes(gi->buffer_view, &size);
	if (bytes == NULL)
	{
		RldHires_Say(rep, "HRG-16", "texture %d ('%s') has no image data - export textures as PNG", index, name);
		return;
	}
	img->png = bytes;
	img->pngSize = size;
	if ((size >= 3u) && (bytes[0] == 0xFFu) && (bytes[1] == 0xD8u) && (bytes[2] == 0xFFu))
	{
		RldHires_Say(rep, "HRG-16", "texture %d ('%s') is JPEG - export textures as PNG", index, name);
		return;
	}
	if ((size < 8u) || (memcmp(bytes, pngSig, 8) != 0))
	{
		RldHires_Say(rep, "HRG-16", "texture %d ('%s') is not a PNG - export textures as PNG", index, name);
		return;
	}
	if ((size < 33u) || (RldHires_LE32(&bytes[8]) != 0x0D000000u) || (memcmp(&bytes[12], "IHDR", 4) != 0))
	{
		RldHires_Say(rep, "HRG-16", "texture %d ('%s') is a damaged PNG (no IHDR) - export it again", index, name);
		return;
	}
	img->fileWidth = ((u32)bytes[16] << 24) | ((u32)bytes[17] << 16) | ((u32)bytes[18] << 8) | (u32)bytes[19];
	img->fileHeight = ((u32)bytes[20] << 24) | ((u32)bytes[21] << 16) | ((u32)bytes[22] << 8) | (u32)bytes[23];
	img->bitDepth = bytes[24];
	img->colorType = bytes[25];
	if ((img->fileWidth == 0u) || (img->fileHeight == 0u))
	{
		RldHires_Say(rep, "HRG-16", "texture %d ('%s') is a damaged PNG (size 0) - export it again", index, name);
		return;
	}
	img->isPng = 1;
	if ((img->fileWidth > RLDHIRES_TEXTURE_EDGE_MAX) || (img->fileHeight > RLDHIRES_TEXTURE_EDGE_MAX))
	{
		RldHires_Say(rep, "HRG-17", "texture %d ('%s') is %ux%u - the largest edge is 2048", index, name, (unsigned)img->fileWidth,
		             (unsigned)img->fileHeight);
		return;
	}
	if ((img->bitDepth != 8u) || ((img->colorType != 2u) && (img->colorType != 6u)))
	{
		RldHires_Say(rep, "HRG-30", "texture %d ('%s') is a %u-bit PNG of colour type %u - read as RGBA8; export 8-bit RGB or RGBA", index, name,
		             (unsigned)img->bitDepth, (unsigned)img->colorType);
	}
	img->width = RldHires_UpEdge(img->fileWidth);
	img->height = RldHires_UpEdge(img->fileHeight);
	img->mip0Bytes = (u64)img->width * (u64)img->height * 4u;
}

// Every buffer view inside its buffer and every accessor inside its view,
// computed in 64 bits. cgltf_size is 32 bits in an x86 build, and
// cgltf_validate computes offset + stride * (count - 1) without an overflow
// check (cgltf.h, cgltf_validate) - a hostile count could wrap past it and
// let the reads below (and cgltf_validate's own index bound scan) run off the
// buffer. So this runs BEFORE cgltf_validate. An accessor without a buffer
// view (zeros by the glTF rules) may hold at most 16M elements. NULL = fine,
// else a fixed text (rule HRG-7).
static const char *RldHires_RangesOk(const cgltf_data *gd)
{
	cgltf_size i;

	for (i = 0; i < gd->buffer_views_count; i++)
	{
		const cgltf_buffer_view *v = &gd->buffer_views[i];

		if ((v->buffer == NULL) || ((u64)v->offset + (u64)v->size > (u64)v->buffer->size))
		{
			return "a buffer view lies outside its buffer";
		}
	}
	for (i = 0; i < gd->accessors_count; i++)
	{
		const cgltf_accessor *a = &gd->accessors[i];
		const u64 element = (u64)cgltf_calc_size(a->type, a->component_type);

		if (a->buffer_view != NULL)
		{
			if ((a->count != 0u) && ((u64)a->offset + (u64)a->stride * (u64)(a->count - 1u) + element > (u64)a->buffer_view->size))
			{
				return "an accessor lies outside its buffer view";
			}
		}
		else if (a->count > 0x1000000u)
		{
			return "an accessor without data has more than 16M elements";
		}
		if (a->is_sparse)
		{
			const cgltf_accessor_sparse *sp = &a->sparse;

			if ((sp->indices_buffer_view == NULL) || (sp->values_buffer_view == NULL) ||
			    ((u64)sp->indices_byte_offset + (u64)cgltf_component_size(sp->indices_component_type) * (u64)sp->count > (u64)sp->indices_buffer_view->size) ||
			    ((u64)sp->values_byte_offset + element * (u64)sp->count > (u64)sp->values_buffer_view->size))
			{
				return "a sparse accessor lies outside its buffer views";
			}
		}
	}
	return NULL;
}

// Opens a GLB from memory and runs every rule of the model (HRG-6..26, 28..31).
// 1 = usable as hi-res (no error), 0 = refused. out is always written; after a
// 1 it owns cgltf data, close it with RldHires_Close (after a 0 too - it is
// safe on any out). rep (may be NULL) gets every finding, it is NOT cleared
// here. The GLB must stay alive until RldHires_Close. The budget is a check of
// its own: a 1 here is not yet "draw it" - every loader calls
// RldHires_CheckBudget after it (rldpack hires-check and hires-add, the game).
static int RldHires_Open(const u8 *glb, u32 size, RldHiresModel *out, RldHiresReport *rep)
{
	RldHiresModel *m = out;
	const int errorsBefore = (rep != NULL) ? rep->errors : 0;
	int localErrors = 0;
	cgltf_options options;
	cgltf_result r;
	cgltf_data *gd = NULL;
	cgltf_size i;
	const cgltf_scene *scene = NULL;
	int *order = NULL;   // the visited nodes, depth first
	int orderCount = 0;
	u8 *inWheel = NULL;  // 1 = in the subtree of a wheel node
	int *nodeStack = NULL;
	int found[RLDHIRES_WHEEL_COUNT];
	int wheelOk = 1;
	int seenNormal = 0;
	int seenMorph = 0;
	int seenSparse = 0;
	int seenNonFinite = 0;
	int outsideBuffer = 0;
	RldHiresReport scratch;

	if (rep == NULL)
	{
		RldHires_ReportClear(&scratch);
		rep = &scratch;
	}
	if (m == NULL)
	{
		return 0;
	}
	memset(m, 0, sizeof(*m));
	m->scene = -1;
	m->skin = -1;
	for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
	{
		m->clipAnim[i] = -1;
	}
	for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
	{
		m->wheelNode[i] = -1;
		found[i] = -1;
	}
	m->glb = glb;
	m->glbSize = size;
	m->boundsMin[0] = m->boundsMin[1] = m->boundsMin[2] = 1e30f;
	m->boundsMax[0] = m->boundsMax[1] = m->boundsMax[2] = -1e30f;

	// HRG-6: the GLB header - magic "glTF", version 2, length = the size given.
	if ((glb == NULL) || (size < 20u))
	{
		RldHires_Say(rep, "HRG-6", "the model is %u bytes - too short for a .glb", (unsigned)size);
		return 0;
	}
	if (RldHires_LE32(glb) != 0x46546C67u)
	{
		RldHires_Say(rep, "HRG-6", "the model is not a .glb (no 'glTF' magic) - export glTF Binary (.glb)");
		return 0;
	}
	if (RldHires_LE32(&glb[4]) != 2u)
	{
		RldHires_Say(rep, "HRG-6", "the .glb is version %u - only glTF 2.0 is read", (unsigned)RldHires_LE32(&glb[4]));
		return 0;
	}
	if (RldHires_LE32(&glb[8]) != size)
	{
		RldHires_Say(rep, "HRG-6", "the .glb says it is %u bytes, but it is %u", (unsigned)RldHires_LE32(&glb[8]), (unsigned)size);
		return 0;
	}

	// HRG-7: parse (GLB only, never a file).
	memset(&options, 0, sizeof(options));
	options.type = cgltf_file_type_glb;
	options.file.read = RldHires_NoFile;
	r = cgltf_parse(&options, glb, size, &gd);
	if (r != cgltf_result_success)
	{
		RldHires_Say(rep, "HRG-7", "the .glb cannot be read (%s) - export it again", RldHires_CgltfText(r));
		return 0;
	}
	m->data = gd;

	// HRG-8: only KHR_materials_unlit may be required.
	for (i = 0; i < gd->extensions_required_count; i++)
	{
		const char *ext = gd->extensions_required[i];

		if ((ext == NULL) || (strcmp(ext, "KHR_materials_unlit") != 0))
		{
			char name[48];

			RldHires_Name(name, sizeof(name), ext, i);
			RldHires_Say(rep, "HRG-8", "the model requires the extension '%s' - export without it (no Draco or mesh compression)", name);
		}
	}

	// HRG-9: buffers only from the GLB's BIN chunk.
	for (i = 0; i < gd->buffers_count; i++)
	{
		const cgltf_buffer *b = &gd->buffers[i];

		if (b->uri != NULL)
		{
			outsideBuffer = 1;
			RldHires_Say(rep, "HRG-9", "buffer %u %s - export one .glb (glTF Binary) with everything inside", (unsigned)i,
			             (strncmp(b->uri, "data:", 5) == 0) ? "is a data URI" : "refers to an outside file");
		}
	}
	if (outsideBuffer)
	{
		// An outside buffer is never loaded, so nothing below could be read.
		// (An unknown required extension does not stop the checks: the rest of
		// the report still tells the author what else to fix.)
		return 0;
	}

	r = cgltf_load_buffers(&options, gd, NULL);
	if (r != cgltf_result_success)
	{
		RldHires_Say(rep, "HRG-7", "the buffer of the .glb cannot be loaded (%s) - export it again", RldHires_CgltfText(r));
		return 0;
	}
	for (i = 0; i < gd->buffers_count; i++)
	{
		if ((gd->buffers[i].size != 0u) && (gd->buffers[i].data == NULL))
		{
			RldHires_Say(rep, "HRG-7", "buffer %u of the .glb has no data - export it again", (unsigned)i);
			return 0;
		}
	}
	{
		const char *why = RldHires_RangesOk(gd);

		if (why != NULL)
		{
			RldHires_Say(rep, "HRG-7", "the .glb is damaged (%s) - export it again", why);
			return 0;
		}
	}
	r = cgltf_validate(gd);
	if (r != cgltf_result_success)
	{
		RldHires_Say(rep, "HRG-7", "the .glb is damaged (%s) - export it again", RldHires_CgltfText(r));
		return 0;
	}

	// The scene: scene, else scenes[0], else every root node.
	if (gd->scene != NULL)
	{
		scene = gd->scene;
	}
	else if (gd->scenes_count > 0u)
	{
		scene = &gd->scenes[0];
	}
	m->scene = (scene != NULL) ? (int)cgltf_scene_index(gd, scene) : -1;

	// Depth first, node order; every node at most once (validate refused cycles).
	if (gd->nodes_count > 0u)
	{
		// The stack holds every push there can be: each root and each child
		// entry once (a node listed under two parents is pushed twice and
		// visited once).
		cgltf_size pushes = gd->nodes_count;
		cgltf_size n;

		for (n = 0; n < gd->nodes_count; n++)
		{
			pushes += gd->nodes[n].children_count;
		}
		if (scene != NULL)
		{
			pushes += scene->nodes_count;
		}
		order = (int *)malloc(gd->nodes_count * sizeof(int));
		nodeStack = (int *)malloc(pushes * sizeof(int));
		inWheel = (u8 *)calloc(gd->nodes_count, 1);
		m->drawNodes = (int *)malloc(gd->nodes_count * sizeof(int));
		if ((order == NULL) || (nodeStack == NULL) || (inWheel == NULL) || (m->drawNodes == NULL))
		{
			free(order);
			free(nodeStack);
			free(inWheel);
			RldHires_Say(rep, "HRG-7", "out of memory reading the nodes");
			return 0;
		}
		{
			u8 *seen = inWheel; // borrowed as the visited mark, cleared below
			const cgltf_size roots = (scene != NULL) ? scene->nodes_count : gd->nodes_count;
			cgltf_size root;

			for (root = 0; root < roots; root++)
			{
				const cgltf_node *start = (scene != NULL) ? scene->nodes[root] : &gd->nodes[root];
				int top = 0;

				if ((scene == NULL) && (start->parent != NULL))
				{
					continue;
				}
				nodeStack[top++] = (int)cgltf_node_index(gd, start);
				while (top > 0)
				{
					const int ni = nodeStack[--top];
					const cgltf_node *node = &gd->nodes[ni];
					cgltf_size c;

					if (seen[ni])
					{
						continue;
					}
					seen[ni] = 1;
					order[orderCount++] = ni;
					for (c = node->children_count; c > 0u; c--)
					{
						const int ci = (int)cgltf_node_index(gd, node->children[c - 1u]);

						if (!seen[ci])
						{
							nodeStack[top++] = ci;
						}
					}
				}
			}
			memset(seen, 0, gd->nodes_count);
		}
	}

	// The wheels: the first node of each name in that order.
	for (i = 0; i < (cgltf_size)orderCount; i++)
	{
		const cgltf_node *node = &gd->nodes[order[i]];
		const int w = RldHires_WheelByName(node->name);

		if (w < 0)
		{
			continue;
		}
		if (found[w] >= 0)
		{
			RldHires_Say(rep, "HRG-31", "the node name '%s' appears twice - the first one (node %d) is used", s_rldHiresWheelName[w], found[w]);
			continue;
		}
		found[w] = order[i];
	}
	{
		char missing[96];
		int have = 0;

		missing[0] = '\0';
		for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
		{
			const cgltf_node *node = (found[i] >= 0) ? &gd->nodes[found[i]] : NULL;
			const char *why = (node == NULL) ? "missing" : (node->mesh == NULL) ? "has no mesh" : (node->skin != NULL) ? "is skinned" : NULL;

			if (why == NULL)
			{
				have++;
				continue;
			}
			snprintf(missing + strlen(missing), sizeof(missing) - strlen(missing), "%s%s %s", (missing[0] != '\0') ? ", " : "",
			         s_rldHiresWheelName[i], why);
		}
		m->wheelsFound = have;
		if (have == 0)
		{
			RldHires_Say(rep, "HRG-20", "no wheel nodes (wheel_fl, wheel_fr, wheel_rl, wheel_rr) - standard wheels are used");
			wheelOk = 0;
		}
		else if (have < RLDHIRES_WHEEL_COUNT)
		{
			RldHires_Say(rep, "HRG-20", "only %d of 4 wheel nodes usable (%s) - standard wheels are used, the found ones are not drawn", have, missing);
			wheelOk = 0;
		}
	}

	// Mark the subtrees of the wheel nodes (found ones, used or not): never body.
	for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
	{
		int top = 0;

		if (found[i] < 0)
		{
			continue;
		}
		if (gd->nodes[found[i]].children_count != 0u)
		{
			RldHires_Say(rep, "HRG-31", "wheel node '%s' has child nodes - they are not drawn", s_rldHiresWheelName[i]);
		}
		nodeStack[top++] = found[i];
		while (top > 0)
		{
			const int ni = nodeStack[--top];
			const cgltf_node *node = &gd->nodes[ni];
			cgltf_size c;

			if (inWheel[ni])
			{
				continue;
			}
			inWheel[ni] = 1;
			for (c = 0; c < node->children_count; c++)
			{
				const int ci = (int)cgltf_node_index(gd, node->children[c]);

				if (!inWheel[ci])
				{
					nodeStack[top++] = ci;
				}
			}
		}
	}

	// HRG-12, HRG-13: the skin.
	if (gd->skins_count > RLDHIRES_SKINS_MAX)
	{
		RldHires_Say(rep, "HRG-12", "the model has %u skins - one armature only", (unsigned)gd->skins_count);
	}
	else if (gd->skins_count == 1u)
	{
		const cgltf_skin *skin = &gd->skins[0];

		m->skin = 0;
		m->joints = (u32)skin->joints_count;
		if (skin->joints_count > RLDHIRES_JOINTS_MAX)
		{
			RldHires_Say(rep, "HRG-13", "the skin has %u joints - at most 64", (unsigned)skin->joints_count);
		}
		if ((skin->inverse_bind_matrices != NULL) &&
		    ((skin->inverse_bind_matrices->type != cgltf_type_mat4) || (skin->inverse_bind_matrices->component_type != cgltf_component_type_r_32f) ||
		     (skin->inverse_bind_matrices->count < skin->joints_count)))
		{
			RldHires_Say(rep, "HRG-13", "the skin's inverse bind matrices are not one float MAT4 per joint - export the armature again");
		}
		if ((skin->inverse_bind_matrices != NULL) && skin->inverse_bind_matrices->is_sparse)
		{
			RldHires_Say(rep, "HRG-29", "the skin's inverse bind matrices are sparse - export without sparse data");
			seenSparse = 1;
		}
	}

	// The body and the wheels: checks, counts, bounds.
	for (i = 0; i < (cgltf_size)orderCount; i++)
	{
		const cgltf_node *node = &gd->nodes[order[i]];
		float world[16];

		if ((node->mesh == NULL) || inWheel[order[i]])
		{
			continue;
		}
		m->drawNodes[m->drawNodeCount++] = order[i];
		if (node->skin != NULL)
		{
			m->skinnedNodeCount++;
			RldHires_CheckMeshNode(m, rep, node, NULL, &seenNormal, &seenMorph, &seenSparse, &seenNonFinite);
		}
		else
		{
			cgltf_node_transform_world(node, world);
			RldHires_CheckMeshNode(m, rep, node, world, &seenNormal, &seenMorph, &seenSparse, &seenNonFinite);
		}
	}
	if (m->drawNodeCount == 0)
	{
		RldHires_Say(rep, "HRG-28", "the scene has no body mesh - nothing would be drawn");
	}
	else if (m->triangles == 0u)
	{
		RldHires_Say(rep, "HRG-28", "the body meshes have no triangle to draw");
	}
	if (wheelOk)
	{
		for (i = 0; i < RLDHIRES_WHEEL_COUNT; i++)
		{
			const cgltf_node *node = &gd->nodes[found[i]];
			const cgltf_mesh *mesh = node->mesh;
			float world[16];
			float radius = 0.0f;
			cgltf_size p;
			cgltf_size v;

			m->wheelNode[i] = found[i];
			cgltf_node_transform_world(node, world);
			RldHires_CheckMeshNode(m, rep, node, world, &seenNormal, &seenMorph, &seenSparse, &seenNonFinite);
			for (p = 0; p < mesh->primitives_count; p++)
			{
				const cgltf_accessor *pos = cgltf_find_accessor(&mesh->primitives[p], cgltf_attribute_type_position, 0);

				if (!RldHires_IsFloat3(pos) || pos->is_sparse)
				{
					continue;
				}
				for (v = 0; v < pos->count; v++)
				{
					float q[3] = {0.0f, 0.0f, 0.0f};
					float d;

					cgltf_accessor_read_float(pos, v, q, 3);
					d = sqrtf(q[1] * q[1] + q[2] * q[2]);
					if (d > radius)
					{
						radius = d;
					}
				}
			}
			m->wheelRadius[i] = radius;
		}
	}

	// HRG-18: the bind pose, at most 4 m on each axis.
	if (seenNonFinite)
	{
		RldHires_Say(rep, "HRG-18", "the model has a position that is not a finite number - export it again");
	}
	if ((m->vertices != 0u) && (m->boundsMin[0] <= m->boundsMax[0]) && (m->boundsMin[1] <= m->boundsMax[1]) && (m->boundsMin[2] <= m->boundsMax[2]))
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			const float extent = m->boundsMax[k] - m->boundsMin[k];

			if (!(extent <= RLDHIRES_BOUNDS_MAX_M))
			{
				RldHires_Say(rep, "HRG-18", "the model is %.2f m along %c - at most 4 m (1 m = 64 kart units; check the export scale)",
				             (double)extent, "XYZ"[k]);
				break;
			}
		}
	}
	else
	{
		m->boundsMin[0] = m->boundsMin[1] = m->boundsMin[2] = 0.0f;
		m->boundsMax[0] = m->boundsMax[1] = m->boundsMax[2] = 0.0f;
	}

	// HRG-25, HRG-26: materials.
	{
		int blend = 0;
		int ignored = 0;
		size_t firstBlend = 0;
		size_t firstIgnored = 0;

		for (i = 0; i < gd->materials_count; i++)
		{
			const cgltf_material *mat = &gd->materials[i];
			const int hasIgnored = (mat->pbr_metallic_roughness.metallic_roughness_texture.texture != NULL) ||
			                       (mat->occlusion_texture.texture != NULL) || (mat->emissive_texture.texture != NULL) ||
			                       (mat->emissive_factor[0] != 0.0f) || (mat->emissive_factor[1] != 0.0f) || (mat->emissive_factor[2] != 0.0f);

			if (mat->alpha_mode == cgltf_alpha_mode_blend)
			{
				if (blend++ == 0)
				{
					firstBlend = i;
				}
			}
			if (hasIgnored)
			{
				if (ignored++ == 0)
				{
					firstIgnored = i;
				}
			}
		}
		if (blend != 0)
		{
			char name[48];

			RldHires_Name(name, sizeof(name), gd->materials[firstBlend].name, firstBlend);
			RldHires_Say(rep, "HRG-25", "%d material(s) use alpha BLEND ('%s' first) - drawn as MASK with cutoff 0.5", blend, name);
		}
		if (ignored != 0)
		{
			char name[48];

			RldHires_Name(name, sizeof(name), gd->materials[firstIgnored].name, firstIgnored);
			RldHires_Say(rep, "HRG-26", "%d material(s) use metallic/roughness, occlusion or emissive ('%s' first) - ignored, only base colour and normal map are drawn",
			             ignored, name);
		}
	}

	// HRG-9, HRG-16, HRG-17, HRG-30: images (headers only).
	if (gd->images_count > 0u)
	{
		m->images = (RldHiresImage *)calloc(gd->images_count, sizeof(RldHiresImage));
		if (m->images == NULL)
		{
			RldHires_Say(rep, "HRG-7", "out of memory reading the textures");
		}
		else
		{
			m->imageCount = (int)gd->images_count;
			for (i = 0; i < gd->materials_count; i++)
			{
				const cgltf_texture *base = gd->materials[i].pbr_metallic_roughness.base_color_texture.texture;
				const cgltf_texture *normal = gd->materials[i].normal_texture.texture;

				if ((base != NULL) && (base->image != NULL))
				{
					m->images[cgltf_image_index(gd, base->image)].used = 1;
				}
				if ((normal != NULL) && (normal->image != NULL))
				{
					m->images[cgltf_image_index(gd, normal->image)].used = 1;
				}
			}
			for (i = 0; i < gd->images_count; i++)
			{
				RldHiresImage *img = &m->images[i];

				if (!img->used)
				{
					img->index = (int)i;
					continue;
				}
				RldHires_CheckImage(m, rep, (int)i, img);
				img->used = 1;
				m->textureBytes += img->mip0Bytes;
				if (img->width > m->textureEdgeMax)
				{
					m->textureEdgeMax = img->width;
				}
				if (img->height > m->textureEdgeMax)
				{
					m->textureEdgeMax = img->height;
				}
			}
		}
	}
	for (i = 0; i < gd->materials_count; i++)
	{
		const cgltf_texture *slot[2];
		int k;

		slot[0] = gd->materials[i].pbr_metallic_roughness.base_color_texture.texture;
		slot[1] = gd->materials[i].normal_texture.texture;
		for (k = 0; k < 2; k++)
		{
			if ((slot[k] != NULL) && (slot[k]->image == NULL))
			{
				char name[48];

				RldHires_Name(name, sizeof(name), gd->materials[i].name, i);
				RldHires_Say(rep, "HRG-16", "the %s of material '%s' has no PNG image (WebP or KTX2 are not read) - export textures as PNG",
				             (k == 0) ? "base colour" : "normal map", name);
			}
		}
	}

	// HRG-19, HRG-21, HRG-22, HRG-23, HRG-29: the clips.
	{
		int unknown = 0;
		int cubic = 0;
		size_t firstUnknown = 0;

		for (i = 0; i < gd->animations_count; i++)
		{
			const cgltf_animation *anim = &gd->animations[i];
			const int c = RldHires_ClipByName(anim->name);
			float seconds = 0.0f;
			cgltf_size s;

			if (c < 0)
			{
				if (unknown++ == 0)
				{
					firstUnknown = i;
				}
				continue;
			}
			if (anim->channels_count == 0u)
			{
				continue; // counts as missing (HRG-19 below)
			}
			if (m->clipAnim[c] >= 0)
			{
				RldHires_Say(rep, "HRG-21", "the clip '%s' appears twice - the first one (animation %d) is used", s_rldHiresClipName[c], m->clipAnim[c]);
				continue;
			}
			for (s = 0; s < anim->samplers_count; s++)
			{
				const cgltf_animation_sampler *smp = &anim->samplers[s];
				float t = 0.0f;

				if (smp->input->is_sparse || smp->output->is_sparse)
				{
					if (!seenSparse)
					{
						RldHires_Say(rep, "HRG-29", "the clip '%s' uses a sparse accessor - export without sparse data", s_rldHiresClipName[c]);
					}
					seenSparse = 1;
					continue;
				}
				if ((smp->input->type != cgltf_type_scalar) || (smp->input->component_type != cgltf_component_type_r_32f))
				{
					RldHires_Say(rep, "HRG-7", "the clip '%s' has key times that are not float numbers - export it again", s_rldHiresClipName[c]);
					continue;
				}
				if (smp->interpolation == cgltf_interpolation_type_cubic_spline)
				{
					cubic++;
				}
				if ((smp->input->count > 0u) && cgltf_accessor_read_float(smp->input, smp->input->count - 1u, &t, 1) && (t > seconds))
				{
					seconds = t;
				}
			}
			for (s = 0; s < anim->channels_count; s++)
			{
				if ((anim->channels[s].target_path == cgltf_animation_path_type_weights) && !seenMorph)
				{
					RldHires_Say(rep, "HRG-23", "the clip '%s' animates morph targets (shape keys) - they are ignored", s_rldHiresClipName[c]);
					seenMorph = 1;
				}
			}
			m->clipAnim[c] = (int)i;
			m->clipSeconds[c] = isfinite(seconds) ? seconds : 0.0f;
		}
		if (unknown != 0)
		{
			char name[48];

			RldHires_Name(name, sizeof(name), gd->animations[firstUnknown].name, firstUnknown);
			RldHires_Say(rep, "HRG-21", "%d animation(s) have no clip name ('%s' first) - ignored; the names are idle, steer_left, steer_right, jump, drift_left, drift_right, hit, podium_1..3, lose, menu",
			             unknown, name);
		}
		if (cubic != 0)
		{
			RldHires_Say(rep, "HRG-22", "%d clip sampler(s) use CUBICSPLINE - read linear between the keys", cubic);
		}
		for (i = 0; i < RLDHIRES_CLIP_COUNT; i++)
		{
			int via = 0;
			int lock = 0;
			const int got = RldHires_ClipResolve(m, (int)i, &via, &lock);

			if (m->clipAnim[i] >= 0)
			{
				continue;
			}
			if (got < 0)
			{
				RldHires_Say(rep, "HRG-19", "clip '%s' is missing - the kart keeps its rest pose", s_rldHiresClipName[i]);
			}
			else
			{
				RldHires_Say(rep, "HRG-19", "clip '%s' is missing - the kart uses '%s'%s instead", s_rldHiresClipName[i], s_rldHiresClipName[got],
				             lock ? " at full lock" : "");
			}
		}
	}

	// HRG-24: cameras and lights.
	if (gd->cameras_count != 0u)
	{
		RldHires_Say(rep, "HRG-24", "%u camera(s) in the model - ignored", (unsigned)gd->cameras_count);
	}
	if (gd->lights_count != 0u)
	{
		RldHires_Say(rep, "HRG-24", "%u light(s) in the model - ignored", (unsigned)gd->lights_count);
	}

	free(order);
	free(nodeStack);
	free(inWheel);

	localErrors = rep->errors - errorsBefore;
	m->usable = (localErrors == 0);
	return m->usable;
}

// HRG-27: the budget of a model RldHires_Open read. 1 = within, 0 = over (an
// error per limit passed into rep, may be NULL).
static int RldHires_CheckBudget(const RldHiresModel *m, RldHiresReport *rep)
{
	int over = 0;

	if (m == NULL)
	{
		return 0;
	}
	if (m->triangles > RLDHIRES_BUDGET_TRIANGLES)
	{
		RldHires_Say(rep, "HRG-27", "over budget: %llu triangles, the limit is %u", (unsigned long long)m->triangles, (unsigned)RLDHIRES_BUDGET_TRIANGLES);
		over = 1;
	}
	if (m->textureEdgeMax > RLDHIRES_BUDGET_TEXTURE_EDGE)
	{
		RldHires_Say(rep, "HRG-27", "over budget: a texture edge of %u, the limit is %u", (unsigned)m->textureEdgeMax, (unsigned)RLDHIRES_BUDGET_TEXTURE_EDGE);
		over = 1;
	}
	if (m->textureBytes > RLDHIRES_BUDGET_TEXTURE_BYTES)
	{
		RldHires_Say(rep, "HRG-27", "over budget: %llu texture bytes (RGBA8), the limit is %u", (unsigned long long)m->textureBytes,
		             (unsigned)RLDHIRES_BUDGET_TEXTURE_BYTES);
		over = 1;
	}
	return !over;
}

#endif // RLDHIRES_INC
