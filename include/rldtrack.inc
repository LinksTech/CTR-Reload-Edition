// rldtrack.inc - the .rldtrack format, once, for both sides
//
// AN INCLUDE, NOT A MODULE. This file is not compiled on its own; it is
// pulled into exactly two translation units:
//
//   tools/rldpack.c           the packer, with RLDTRACK_WITH_BUILDER
//   platform/native_assets.c  the game, which only reads and checks
//
// This is the same construction as platform/native_shaders.inc: source that two
// sides share without it becoming a library of its own.
//
// WHY SHARED AND NOT COPIED. This is where it says what a container IS: header,
// directory, limits, META, SNDB, the reader. The same fact in two places
// is the pattern that has already caught this tree several times - one falls
// behind, and then the packer writes what the game cannot read.
//
// THE SIGNATURE WAS TAKEN OUT OF THE FORMAT ON 2026-09-19.
//
// This is where SHA-512 and a complete Ed25519 implementation used to be, about
// 860 lines, plus Rld_BuildSignedBuffer and Rld_VerifySignature. No SIGN,
// no key, no signing path on either of the two sides.
//
// VERSION 4.1, FIXED ON 2026-09-28.
// A container consists of META, LEVD and VRMD, optionally SNDB (sound) and
// PARM (reverb, bot strength, ambient sound). MMAP, the scaled-down
// menu map from 09-28, has been dropped again: the game scales down itself
// (Rld_ScaleMap, below at the map), an MMAP in old containers is
// skipped like SIGN.
//
// The reason, in short: the signature was the only remaining hurdle, and it
// never solved the case authors care about - it proves who PACKED, not who
// BUILT. The countermeasure for that (the author registry) was never built.
// What remained was key management as the price for a promise nobody could
// keep.
//
// SHA-256 STAYS. It is not a signature but the per-chunk integrity check
// - it catches the truncated download and the flipped byte, and it
// already did that on its own before. Exactly that has not
// changed.
//
// ONE SWITCH:
//   RLDTRACK_WITH_BUILDER   the writing side (packer only)
// Without it, what remains is what is needed to READ and CHECK, and nothing
// more.
//
// EXISTING CONTAINERS WITH SIGN STILL LOAD. The chunk is skipped like
// any unknown type - it is no longer mandatory,
// but it is not an error either. That is why RLD_VERSION_MAJOR stays at 4: a
// bump would reject exactly the containers that are supposed to keep working.
//
// u8, u32, u64 and s64 come from the including file. The packer defines them itself,
// the game has them from include/macros.h.

#ifndef RLDTRACK_INC
#define RLDTRACK_INC

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//========================================================================================
// SHA-256
//========================================================================================

struct Sha256
{
	u32 state[8];
	u64 length;
	u8 block[64];
	u32 blockLength;
};

static const u32 s_sha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu,
    0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau,
    0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u,
    0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static u32 Sha256_Ror(u32 value, int bits)
{
	return (value >> bits) | (value << (32 - bits));
}

static void Sha256_Compress(struct Sha256 *sha, const u8 *block)
{
	u32 w[64];
	u32 a, b, c, d, e, f, g, h;
	int i;

	for (i = 0; i < 16; i++)
	{
		w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16) | ((u32)block[i * 4 + 2] << 8) | (u32)block[i * 4 + 3];
	}
	for (i = 16; i < 64; i++)
	{
		u32 s0 = Sha256_Ror(w[i - 15], 7) ^ Sha256_Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
		u32 s1 = Sha256_Ror(w[i - 2], 17) ^ Sha256_Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = sha->state[0];
	b = sha->state[1];
	c = sha->state[2];
	d = sha->state[3];
	e = sha->state[4];
	f = sha->state[5];
	g = sha->state[6];
	h = sha->state[7];

	for (i = 0; i < 64; i++)
	{
		u32 s1 = Sha256_Ror(e, 6) ^ Sha256_Ror(e, 11) ^ Sha256_Ror(e, 25);
		u32 ch = (e & f) ^ ((~e) & g);
		u32 t1 = h + s1 + ch + s_sha256K[i] + w[i];
		u32 s0 = Sha256_Ror(a, 2) ^ Sha256_Ror(a, 13) ^ Sha256_Ror(a, 22);
		u32 maj = (a & b) ^ (a & c) ^ (b & c);
		u32 t2 = s0 + maj;

		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	sha->state[0] += a;
	sha->state[1] += b;
	sha->state[2] += c;
	sha->state[3] += d;
	sha->state[4] += e;
	sha->state[5] += f;
	sha->state[6] += g;
	sha->state[7] += h;
}

static void Sha256_Init(struct Sha256 *sha)
{
	sha->state[0] = 0x6a09e667u;
	sha->state[1] = 0xbb67ae85u;
	sha->state[2] = 0x3c6ef372u;
	sha->state[3] = 0xa54ff53au;
	sha->state[4] = 0x510e527fu;
	sha->state[5] = 0x9b05688cu;
	sha->state[6] = 0x1f83d9abu;
	sha->state[7] = 0x5be0cd19u;
	sha->length = 0;
	sha->blockLength = 0;
}

static void Sha256_Update(struct Sha256 *sha, const void *bytes, size_t size)
{
	const u8 *at = (const u8 *)bytes;

	sha->length += (u64)size * 8u;

	while (size != 0)
	{
		size_t room = 64u - sha->blockLength;
		size_t take = (size < room) ? size : room;

		memcpy(&sha->block[sha->blockLength], at, take);
		sha->blockLength += (u32)take;
		at += take;
		size -= take;

		if (sha->blockLength == 64)
		{
			Sha256_Compress(sha, sha->block);
			sha->blockLength = 0;
		}
	}
}

static void Sha256_Final(struct Sha256 *sha, u8 out[32])
{
	u64 bits = sha->length;
	u8 pad = 0x80;
	u8 zero = 0x00;
	u8 tail[8];
	int i;

	Sha256_Update(sha, &pad, 1);
	sha->length = bits; // padding is not message length
	while (sha->blockLength != 56)
	{
		Sha256_Update(sha, &zero, 1);
		sha->length = bits;
	}

	for (i = 0; i < 8; i++)
	{
		tail[i] = (u8)(bits >> (56 - i * 8));
	}
	Sha256_Update(sha, tail, 8);

	for (i = 0; i < 8; i++)
	{
		out[i * 4 + 0] = (u8)(sha->state[i] >> 24);
		out[i * 4 + 1] = (u8)(sha->state[i] >> 16);
		out[i * 4 + 2] = (u8)(sha->state[i] >> 8);
		out[i * 4 + 3] = (u8)(sha->state[i]);
	}
}

static void Sha256(const void *bytes, size_t size, u8 out[32])
{
	struct Sha256 sha;

	Sha256_Init(&sha);
	Sha256_Update(&sha, bytes, size);
	Sha256_Final(&sha, out);
}

// SHA-512 AND ED25519 USED TO BE HERE - 860 lines, removed on 2026-09-19.
//
// Sha512/Sha512_Init/Update/Final, the field arithmetic (fe_*), the scalars
// (sc_*), the points (ge_*), Ed25519_PublicKey, Ed25519_Sign and
// Ed25519_Verify. They had exactly one consumer: the SIGN chunk.
//
// They do not come back halfway. Whoever needs them again first needs an
// answer to the question they failed on the first time:
// the signature proves who packed, not who built. Without the
// author registry it does not make a theft visible but more
// credible - and the registry was never built.
//
// SHA-256 is above and stays. It checks each chunk's data against its hash
// in the directory, and that never had anything to do with
// authorship.
//========================================================================================
// THE CONTAINER FORMAT
//========================================================================================

#define RLD_MAGIC "RLDTRACK"

// 3, since the evening of 2026-08-29: THE OBFUSCATION IS GONE.
//
// The container is an envelope around binary data. Directory and chunks are
// in plain text, there is no key in the binary, no keystream and no
// nonce any more.
//
// WHY: obfuscation gives track authors no real protection - data can be
// pulled out of any patch or container with little effort, so the format
// does not pretend to offer one. What remains holds up: one file instead of two plus
// patch instructions, and no ISO patching. The signature stood here as a third
// point and was removed on 2026-09-19 - see the head of this file.
//
// That three real bugs were found in this layer on the same day - a
// keystream identical across all containers, a two-time pad between directory
// and META, and a keystream shared between two versions of a track -
// does not make the decision, but it shows what it cost.
//
// The bump to 3 instead of a flag: a version 2 container is
// obfuscated, and without a bump a version 3 reader would run its directory
// as noise through the offset checks and report damage that
// does not exist. What applies is: unknown major version.
//
// 4.1, since 2026-09-28: THE FORMAT IS FROZEN.
// The major version stays 4, the bytes of the
// envelope are the same. New is the meaning of three fields:
//
//   minor version 1  means "frozen". The reader takes any minor version,
//                    names it and never refuses because of it - 4.0 still loads.
//   header flags     are REQUIRED FEATURES. A bit means: without this feature
//                    the track is wrong. A bit the reader does not know
//                    it refuses and names. None is assigned; a reader
//                    of 4.0 refuses every bit anyway, and that is exactly the
//                    effect a required feature needs.
//   metaVersion 1    means "frozen", 0 "draft". Both load.
#define RLD_VERSION_MAJOR 4
#define RLD_VERSION_MINOR 1
#define RLD_HEADER_SIZE    40
#define RLD_DIR_ENTRY_SIZE 64
#define RLD_CHUNK_MAX      64
#define RLD_META_VERSION   1

// The required features this build knows: none.
#define RLD_HEADER_FLAGS_KNOWN 0u

// Why Rld_Open refused. The text comes back as the return value; this kind tells the
// game which keyword the list shows, and the
// numbers for it are in the reader (major, minor, unknownFlags).
enum RldRefusal
{
	RLD_REFUSAL_NONE = 0,
	RLD_REFUSAL_NEWER,   // major version > 4 or unknown required feature
	RLD_REFUSAL_OLDER,   // major version < 4
	RLD_REFUSAL_DAMAGED, // everything else: position, size, duplicate, required chunk ...
};

// Where in the directory entry the chunk's SHA-256 is. Rld_ReadChunk checks
// every fetched chunk against it; the track list reads the LEVD hash from
// here as a version stamp, without touching the chunk
// (NativeTrack_ReadForList).
#define RLD_DIR_HASH_OFFSET 0x20

// Compression is not part of the format (4.1: Deflate dropped). A
// known chunk carries 0; for an unknown one the value is skipped,
// so that an old reader skips a future chunk instead of rejecting the whole
// container.
#define RLD_COMPRESSION_NONE 0

// Every limit applies when writing AND when reading, there before
// allocating memory (Rld_ChunkLimit).
#define RLD_LIMIT_LEVD (16u * 1024u * 1024u)
#define RLD_LIMIT_VRMD (4u * 1024u * 1024u)
#define RLD_LIMIT_META (64u * 1024u)

// SNDB, the sound chunk. The same limit as VRMD, and for the same reason:
// measured, not guessed.
//
// The game's whole sound bank (SOUNDS/KART.HWL) is 11.7 MiB. What a
// track actually replaces of it is a fraction - for Inferno Island four
// of 71 banks and four of 33 sequences, together 1,220,608 bytes. A
// limit above the whole file invites carrying all of it along;
// that would be wrong, because the bank is GLOBAL and a container that
// replaces it completely also replaces the sound of the menu and of every other
// track. 4 MiB allow three times the measured need.
#define RLD_LIMIT_SNDB (4u * 1024u * 1024u)

// PARM, the values of a track (4.1). Three keys of at most four
// bytes each; 4 KiB leave room for any future key without a
// stranger deciding with a field in the file how much memory gets allocated.
#define RLD_LIMIT_PARM (4u * 1024u)

// BLOCKED NAMES. Never assign again, not even for something else:
//   SIGN  signature until 2026-09-19 - in old containers, skipped
//   MMAP  menu map until 4.0         - in old containers, skipped
//   THMB  preview image, dropped 2026-08-28
//   CHAL  challenges, reserved, dropped with 4.1
//   AUDD  music, reserved, replaced by SNDB

// The limit of a KNOWN type, 0 for any other. Known means: this
// build reads it. The reader skips everything else.
static u64 Rld_ChunkLimit(const u8 *type)
{
	if (memcmp(type, "META", 4) == 0)
	{
		return RLD_LIMIT_META;
	}
	if (memcmp(type, "LEVD", 4) == 0)
	{
		return RLD_LIMIT_LEVD;
	}
	if (memcmp(type, "VRMD", 4) == 0)
	{
		return RLD_LIMIT_VRMD;
	}
	if (memcmp(type, "SNDB", 4) == 0)
	{
		return RLD_LIMIT_SNDB;
	}
	if (memcmp(type, "PARM", 4) == 0)
	{
		return RLD_LIMIT_PARM;
	}
	return 0;
}

// WHICH MODES THE TRACK OFFERS.
//
// In META and not in the path: four folders would be the same track four times, and
// a track that supports three modes would then be on disk three times, with three
// chances for two of them to drift apart.
//
// A bit means: "the track offers this mode". The game offers it
// to the player only if it is also BUILT for containers - never one
// the container does not declare (the list of what is
// built is in platform/native_assets.c). Which donor slot a mode
// needs is decided by the game: today the crystal bit picks Nitro Court.
//
// Battle is RESERVED: the bit has its meaning, but what an arena needs in
// its data is not fixed yet - the packer rejects it.
// The game skips unknown bits.
#define RLD_MODE_RACE              0x1u
#define RLD_MODE_CTR_CHALLENGE     0x2u
#define RLD_MODE_TIME_TRIAL        0x4u
#define RLD_MODE_CRYSTAL_CHALLENGE 0x8u
#define RLD_MODE_BATTLE            0x10u

// What is BUILT in the game for containers: Race,
// as NITRO RACE and in the NITRO CUP, and since 2026-09-29 Crystal Challenge
// (NITRO-PIT -> CRYSTAL) and CTR Challenge (NITRO-PIT -> CTR). The one place for both sides - the game
// offers only what is here AND declared; the packer says for everything
// else "not playable in CTR Reload yet".
#define RLD_MODES_PLAYABLE (RLD_MODE_RACE | RLD_MODE_CRYSTAL_CHALLENGE | RLD_MODE_CTR_CHALLENGE)

// ONE LIST, THREE USES: the bit, the word an author types, and the
// name a player reads. The packer parses --modes with it, both sides
// display with it. Two lists would be a switch the display does not know.
// The fourth column is a short tag for narrow displays. Nothing reads it at
// the moment; it stays because every X macro over this list takes four
// arguments.
#define RLD_MODE_LIST(X)                                                   \
	X(RLD_MODE_RACE, "race", "Race", "ARC")                                    \
	X(RLD_MODE_CTR_CHALLENGE, "ctr", "CTR Challenge", "CTR")                   \
	X(RLD_MODE_TIME_TRIAL, "time", "Time Trial", "TT")                         \
	X(RLD_MODE_CRYSTAL_CHALLENGE, "crystal", "Crystal Challenge", "CRY")      \
	X(RLD_MODE_BATTLE, "battle", "Battle", "BAT")

// The modes as a readable line. Never NULL, and not empty for 0: an empty cell
// looks like a field nobody filled in, and not like a track
// that declares no mode.
static const char *Rld_ModesText(u32 modes)
{
	static char text[96];
	size_t at = 0;

	text[0] = '\0';

#define RLD_MODE_APPEND(bit, word, label, tag)                                    \
	if ((modes & (bit)) != 0u)                                                 \
	{                                                                          \
		if (at != 0u)                                                            \
		{                                                                        \
			memcpy(&text[at], ", ", 2);                                            \
			at += 2u;                                                              \
		}                                                                        \
		memcpy(&text[at], (label), strlen(label));                               \
		at += strlen(label);                                                     \
		text[at] = '\0';                                                         \
	}
	RLD_MODE_LIST(RLD_MODE_APPEND)
#undef RLD_MODE_APPEND

	return (at != 0u) ? text : "(none declared)";
}

// HOW MUCH MEMORY THIS TRACK NEEDS - and why that is a number in the container
// and not a constant in the game.
//
// MEASURED ON 2026-08-29, and the finding was not the expected one. The
// test data does NOT blow the MEMPACK: its LEVs, at 339 KB to 2.1 MB, are
// in the range of the disc (238 KB to 780 KB), except for three outliers. Their VRMs
// are byte for byte the same layout as every disc track - 458,808 bytes,
// 2 blocks, 229,376 texels, 14 pages, the same for all 21.
//
// What they blow is the DRAWING MEMORY, and the reason is in their
// BSP tree:
//
//   quadblocks per BSP node:  disc 1.2 to 2.8     test data 3.3 to 9.2
//
// The renderer culls per BSP leaf, so more stays per visible leaf
// - with a mesh of NORMAL size.
//
// TWO NUMBERS FROM HERE WERE WITHDRAWN ON 2026-08-29. This said
// "1,868 to 3,222 draw commands per frame against 139 to 488 on the disc". No
// log of this tree carries the first number, and the second came from a
// single run; across all disc runs it goes up to 1,187. Measured
// instead: Dingo Canyon 127 to 693 per frame, Test 1 1611 to 5895, Test 2
// 1844 to 7041.
//
// And the reason for that was NOT the BSP tree. The leaf boxes are in
// world units just as big as those of the disc (824 to 2008 against 868 to 2585);
// what is coarse is not the box but what lies in it. The dominant item is
// the ABR field - see Rld_FixAbr further below.
//
// If the drawing memory is not enough,
// DrawLevelOvr1P_HasBucketPrimReserve says what happens: the pass gives up and
// the rest of the bucket is not drawn. Silently, with a hole in the image.
//
// THE NUMBER, and what it is NOT. numQuadBlock * 4 * 0x34 takes every
// quadblock as four quads of one POLY_GT4 each. That is the MIDDLE
// level of detail and not the worst case - this used to say "the worst case",
// and that was wrong: the engine reserves before EVERY single quadblock
// DRAW_LEVEL_OVR1P_BUCKET_RESERVE_DEFAULT, 0x1a00 bytes, so
// thirty-two times as much. It accounts for subdivision and clipping.
//
// The number is enough for what was measured: over three runs on 2026-08-29
// CTR_Lod_NotePrimReserveFail did not count in a single window. It is
// a measured limit and not a proven one.
//
// Twice, because MainInit_PrimMem allocates two buffers, db[0] and db[1].
//
// HERE AND NOT TWICE. The packer writes the number into META, the game reads
// it when listing and reserves it before MEMPACK_Init. Both compute it
// with THESE lines - two versions would be two opinions on how much
// memory a track needs, and that only shows up in the image.
#define RLD_QUADBLOCK_BYTES  0x5cu
#define RLD_POLY_GT4_BYTES   0x34u
#define RLD_QUADS_PER_BLOCK  4u
#define RLD_SKY_POLY_BYTES   28u

// THE CLIP BUFFER, WITH THE SAME RULE. Every face with a vertex
// on the near side of the near plane is not drawn in the track renderer but
// stored as a clip record (struct DrawLevelOvr1PClipRecord, GT4 = 0x3c bytes)
// and only split at the end of the frame. Until 2026-09-15 the buffer for it came from
// MainDB_GetClipSize by LEVEL ID - for a container, that is, from the
// table of the host slot: 3000 words, 200 records per frame. On Crash Cove at
// 43:18 that already was not enough (1,470 records discarded in 700 frames),
// and a track with a three to five times coarser BSP
// stores correspondingly more near faces per frame. What found no room
// was dropped without a trace - the road under the kart.
//
// The same number as above, one record instead of one quad: four records per
// quadblock. A measured limit like primBytes, not a proven one; the peak value
// per frame has been in the [CTR Clip] exit line since then.
#define RLD_CLIP_RECORD_GT4_BYTES 0x3cu

struct RldMemNeed
{
	u32 primBytes; // per draw buffer, without the sky
	u32 skyBytes;  // per draw buffer, only the sky
	u32 clipBytes; // per player: clip records for the near plane
	u32 levBytes;  // the LEV itself in the pack
	u32 total;     // levBytes + 2 * (primBytes + skyBytes) - WITHOUT clipBytes, META stays readable
};

// Computed from primBytes and not once more from the LEV: META carries only
// primBytes, and the reserve before MEMPACK_Init must come from META. Exact,
// because primBytes is a multiple of RLD_POLY_GT4_BYTES.
static u32 Rld_ClipBytesForPrimBytes(u32 primBytes)
{
	return (primBytes / RLD_POLY_GT4_BYTES) * RLD_CLIP_RECORD_GT4_BYTES;
}

// Reads mesh_info and the sky header from an unpacked LEV. The pointer map
// has not run here yet, so all references are file offsets.
// Forward, because the three readers are further below. Without these lines
// the compiler assumed "extern int Rld_ReadLE32" - C4013 - and the computation ran
// through a wrong return type.
static u32 Rld_ReadLE16(const u8 *at);
static u32 Rld_ReadLE32(const u8 *at);

static void Rld_MemNeed(struct RldMemNeed *out, const u8 *lev, size_t size)
{
	const size_t body = 4;
	u32 at;

	memset(out, 0, sizeof(*out));

	if ((lev == NULL) || (size < (body + 0x200u)))
	{
		return;
	}

	out->levBytes = (u32)size;

	// mesh_info is at struct Level + 0x00, numQuadBlock at mesh_info + 0x00.
	at = Rld_ReadLE32(&lev[body + 0x00]);
	if ((at != 0u) && ((body + at + 8u) <= size))
	{
		const u32 blocks = Rld_ReadLE32(&lev[body + at + 0x00]);

		// A number from the file, so held against the file: there cannot be more quadblocks
		// than the LEV has room for.
		if (blocks <= ((u32)size / RLD_QUADBLOCK_BYTES))
		{
			out->primBytes = blocks * RLD_QUADS_PER_BLOCK * RLD_POLY_GT4_BYTES;
			out->clipBytes = Rld_ClipBytesForPrimBytes(out->primBytes);
		}
	}

	// The sky: the four largest of the eight segments, because DrawSky_Full
	// draws four per frame.
	at = Rld_ReadLE32(&lev[body + 0x04]);
	if ((at != 0u) && ((body + at + 56u) <= size))
	{
		u32 sorted[8];
		int a;
		int b;

		for (a = 0; a < 8; a++)
		{
			const u8 *p = &lev[body + at + 8u + (u32)a * 2u];

			sorted[a] = (u32)p[0] | ((u32)p[1] << 8);
		}

		for (a = 0; a < 8; a++)
		{
			for (b = a + 1; b < 8; b++)
			{
				if (sorted[b] > sorted[a])
				{
					const u32 swap = sorted[a];

					sorted[a] = sorted[b];
					sorted[b] = swap;
				}
			}
		}

		out->skyBytes = (sorted[0] + sorted[1] + sorted[2] + sorted[3]) * RLD_SKY_POLY_BYTES;
	}

	out->total = out->levBytes + 2u * (out->primBytes + out->skyBytes);
}

// Track name and author. META carries nothing more.
//
// License, description, tool version and UUID were removed on 2026-08-29:
// the reader never needed any of them. They were
// in the concept, were displayed, and each of them was a field an author
// has to maintain without it carrying a decision anywhere.
#define RLD_STRING_COUNT 2

struct RldChunk
{
	char type[5];
	u32 flags;
	u64 offset;
	u64 sizeStored;
	u64 sizeRaw;
	u8 hash[32];

	u8 *stored;
};

struct RldContainer
{
	struct RldChunk chunks[RLD_CHUNK_MAX];
	int chunkCount;
};

//----------------------------------------------------------------------------------------
// Small helpers

static void Rld_WriteLE16(u8 *at, u32 value)
{
	at[0] = (u8)(value);
	at[1] = (u8)(value >> 8);
}

#if defined(RLDTRACK_WITH_BUILDER)
static void Rld_WriteLE32(u8 *at, u32 value)
{
	at[0] = (u8)(value);
	at[1] = (u8)(value >> 8);
	at[2] = (u8)(value >> 16);
	at[3] = (u8)(value >> 24);
}
#endif

static void Rld_WriteLE64(u8 *at, u64 value)
{
	int i;

	for (i = 0; i < 8; i++)
	{
		at[i] = (u8)(value >> (i * 8));
	}
}

static u32 Rld_ReadLE16(const u8 *at)
{
	return (u32)at[0] | ((u32)at[1] << 8);
}

static u32 Rld_ReadLE32(const u8 *at)
{
	return (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) | ((u32)at[3] << 24);
}

static u64 Rld_ReadLE64(const u8 *at)
{
	u64 value = 0;
	int i;

	for (i = 7; i >= 0; i--)
	{
		value = (value << 8) | (u64)at[i];
	}

	return value;
}

//========================================================================================
// READING
//========================================================================================

// The reader keeps the file OPEN and fetches what it needs at the moment.
//
// It used to read the whole file into memory in one piece. In the packer that does
// not show - it looks at one file on request. In the game it matters: the
// track list reads header, directory and META for dozens of containers, and
// it must not touch more. A list that drags along a whole
// megabyte of LEVD each time does exactly what that rule forbids.
//
// Every reference is checked against the REAL file size before jumping
// anywhere.

struct RldReader
{
	FILE *file;
	u64 fileSize;

	int chunkCount;
	u8 directory[RLD_CHUNK_MAX * RLD_DIR_ENTRY_SIZE];
	u64 dirOffset;

	// From the header, set as soon as it is read - even if Rld_Open refuses
	// afterwards. The returned text is always a fixed text; whoever wants to name the numbers
	// (the game in the log, rldpack info) takes them from here.
	u32 major;
	u32 minor;
	u32 unknownFlags;
	enum RldRefusal refusal;
};

static void Rld_Close(struct RldReader *reader)
{
	if (reader->file != NULL)
	{
		fclose(reader->file);
		reader->file = NULL;
	}
}

// Reads at a position that was checked before. A short fread is an
// error here and not a special case: the file itself said in the header how
// big it is, and that was held against reality when opening.
static int Rld_ReadAt(struct RldReader *reader, u64 offset, void *dst, size_t size)
{
	if ((offset > reader->fileSize) || ((u64)size > (reader->fileSize - offset)))
	{
		return 0;
	}

	if (fseek(reader->file, (long)offset, SEEK_SET) != 0)
	{
		return 0;
	}

	return (size == 0u) || (fread(dst, 1, size, reader->file) == size);
}

// Closes, records the kind of refusal and returns the text. Every text
// is fixed - whoever keeps it (NativeTrackEntry.problem) keeps a pointer
// that stays valid across every further scan.
static const char *Rld_Refuse(struct RldReader *reader, enum RldRefusal refusal, const char *text)
{
	Rld_Close(reader);
	reader->refusal = refusal;
	return text;
}

// Every check runs BEFORE jumping, and each has its own text. A
// generic "invalid file" is the message that helps nobody.
//
// Since 4.1 Rld_Open enforces the format's rules itself,
// the same for game and packer: position, overlap, duplicate, compression and
// limit per known type, and the three required chunks. The track list thereby
// refuses what until 4.0 only failed when the track was picked.
static const char *Rld_Open(struct RldReader *reader, const char *path)
{
	u8 header[RLD_HEADER_SIZE];
	long measured;
	u64 declaredSize;
	u32 flags;
	u32 pad;
	int i;
	int j;

	memset(reader, 0, sizeof(*reader));

	reader->file = fopen(path, "rb");
	if (reader->file == NULL)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "cannot be opened");
	}

	if ((fseek(reader->file, 0, SEEK_END) != 0) || ((measured = ftell(reader->file)) < 0))
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "cannot be measured");
	}

	reader->fileSize = (u64)measured;

	if (reader->fileSize < RLD_HEADER_SIZE)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "shorter than a header");
	}

	if (!Rld_ReadAt(reader, 0, header, sizeof(header)))
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "the header cannot be read");
	}

	if (memcmp(header, RLD_MAGIC, 8) != 0)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "no RLDTRACK magic - the extension says nothing, the magic does");
	}

	// Both directions have their own text: a newer container needs a
	// newer game, an older one a new packing run. The numbers are in the reader.
	reader->major = Rld_ReadLE16(&header[0x08]);
	reader->minor = Rld_ReadLE16(&header[0x0a]);
	if (reader->major > RLD_VERSION_MAJOR)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_NEWER, "needs a newer version of CTR Reload - the container format is newer than this build");
	}
	if (reader->major < RLD_VERSION_MAJOR)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_OLDER, "old format - pack it again with the Alpha-Maker");
	}

	// Since 4.1 the header flags are required features: a bit this build does not
	// know means "this track needs something that is missing here". At 0x14 version 2
	// had the salt; the word is reserved and stays 0.
	flags = Rld_ReadLE32(&header[0x0c]);
	pad = Rld_ReadLE32(&header[0x14]);
	reader->unknownFlags = flags & ~RLD_HEADER_FLAGS_KNOWN;
	if (reader->unknownFlags != 0)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_NEWER, "needs a newer version of CTR Reload - the container needs a feature this build does not know");
	}
	if (pad != 0)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "a reserved header field is not zero");
	}

	declaredSize = Rld_ReadLE64(&header[0x20]);
	if (declaredSize != reader->fileSize)
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "file_size does not match the real size - truncated download");
	}

	reader->chunkCount = (int)Rld_ReadLE32(&header[0x10]);
	// Lower bound 3 since 2026-09-19, 4 before: the required chunks are META,
	// LEVD and VRMD. An existing container with SIGN passes through unchanged.
	if ((reader->chunkCount < 3) || (reader->chunkCount > RLD_CHUNK_MAX))
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "chunk_count is outside 3..64");
	}

	reader->dirOffset = Rld_ReadLE64(&header[0x18]);
	if ((reader->dirOffset < RLD_HEADER_SIZE) || (reader->dirOffset > reader->fileSize) ||
	    (((u64)reader->chunkCount * RLD_DIR_ENTRY_SIZE) > (reader->fileSize - reader->dirOffset)))
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "the directory does not lie inside the file");
	}

	if (!Rld_ReadAt(reader, reader->dirOffset, reader->directory, (size_t)reader->chunkCount * RLD_DIR_ENTRY_SIZE))
	{
		return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "the directory cannot be read");
	}

	for (i = 0; i < reader->chunkCount; i++)
	{
		const u8 *entry = &reader->directory[i * RLD_DIR_ENTRY_SIZE];
		u64 offset = Rld_ReadLE64(&entry[0x08]);
		u64 stored = Rld_ReadLE64(&entry[0x10]);
		u64 raw = Rld_ReadLE64(&entry[0x18]);
		u32 method = Rld_ReadLE32(&entry[0x04]) & 0x0fu;
		u64 limit = Rld_ChunkLimit(entry);

		// The position check applies to EVERY entry, including an unknown one.
		if ((offset < RLD_HEADER_SIZE) || (offset > reader->dirOffset) || (stored > (reader->dirOffset - offset)))
		{
			return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "a chunk lies outside the data area");
		}

		// Everything else only for known types. An unknown one is skipped,
		// its compression value included - otherwise a future
		// chunk would lock out every old reader instead of being skipped.
		if (limit != 0u)
		{
			if (method != RLD_COMPRESSION_NONE)
			{
				return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "a chunk is compressed - compression is not part of the format");
			}
			if (stored != raw)
			{
				return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "size_stored and size_raw of a chunk differ");
			}
			if (raw > limit)
			{
				return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "a chunk is larger than the format allows for its type");
			}
		}

		// No type twice, and no two chunks in the same range. At most 64
		// entries, so pairwise. An empty chunk overlaps nothing.
		for (j = 0; j < i; j++)
		{
			const u8 *other = &reader->directory[j * RLD_DIR_ENTRY_SIZE];
			u64 otherOffset = Rld_ReadLE64(&other[0x08]);
			u64 otherStored = Rld_ReadLE64(&other[0x10]);

			if (memcmp(entry, other, 4) == 0)
			{
				return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "a chunk type appears twice");
			}

			if ((stored != 0u) && (otherStored != 0u) && (offset < (otherOffset + otherStored)) && (otherOffset < (offset + stored)))
			{
				return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, "two chunks overlap");
			}
		}
	}

	// The required chunks, already here: until 4.0 a line without LEVD looked
	// healthy in the list and only failed when it was picked.
	{
		static const char *const required[3] = {"META", "LEVD", "VRMD"};
		static const char *const missing[3] = {"META is missing - required chunk", "LEVD is missing - required chunk",
		                                       "VRMD is missing - required chunk"};

		for (j = 0; j < 3; j++)
		{
			int found = 0;

			for (i = 0; i < reader->chunkCount; i++)
			{
				if (memcmp(&reader->directory[i * RLD_DIR_ENTRY_SIZE], required[j], 4) == 0)
				{
					found = 1;
				}
			}

			if (!found)
			{
				return Rld_Refuse(reader, RLD_REFUSAL_DAMAGED, missing[j]);
			}
		}
	}

	return NULL;
}

static const u8 *Rld_FindEntry(const struct RldReader *reader, const char type[4], int *indexOut)
{
	int i;

	for (i = 0; i < reader->chunkCount; i++)
	{
		const u8 *entry = &reader->directory[i * RLD_DIR_ENTRY_SIZE];

		if (memcmp(entry, type, 4) == 0)
		{
			if (indexOut != NULL)
			{
				*indexOut = i;
			}
			return entry;
		}
	}

	return NULL;
}

// Fetches a chunk: read, then check the hash - the hash covers the
// bytes as they are in the file.
//
// Only KNOWN types. Nobody reads an unknown one (it is skipped):
// it has no limit that could apply here, and no consumer.
static u8 *Rld_ReadChunk(struct RldReader *reader, int index, size_t *sizeOut, const char **errorOut)
{
	const u8 *entry = &reader->directory[index * RLD_DIR_ENTRY_SIZE];
	u64 offset = Rld_ReadLE64(&entry[0x08]);
	u64 stored = Rld_ReadLE64(&entry[0x10]);
	u64 raw = Rld_ReadLE64(&entry[0x18]);
	u32 method = Rld_ReadLE32(&entry[0x04]) & 0x0fu;
	u64 limit = Rld_ChunkLimit(entry);
	u8 digest[32];
	u8 *bytes;

	*sizeOut = 0;
	*errorOut = NULL;

	if (limit == 0u)
	{
		*errorOut = "not a chunk type of this format - skipped, not read";
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

	// The type's limit applies here a second time, BEFORE the malloc. Rld_Open has
	// already checked it; the container comes from a stranger, and without this
	// line a field in the file would decide how much memory gets allocated.
	if (raw > limit)
	{
		*errorOut = "the chunk is larger than the format allows for its type";
		return NULL;
	}

	bytes = (u8 *)malloc((size_t)raw ? (size_t)raw : 1u);
	if (bytes == NULL)
	{
		*errorOut = "out of memory";
		return NULL;
	}

	if (!Rld_ReadAt(reader, offset, bytes, (size_t)stored))
	{
		free(bytes);
		*errorOut = "the chunk cannot be read - the file is shorter than its own directory says";
		return NULL;
	}

	// Read, and that is it. This is where the choice between three nonce derivations
	// and then the keystream used to be; now the bytes already are the bytes.
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

// WHAT META STILL CARRIES, AND WHY EXACTLY THAT.
//
// Three items, and each had to justify itself:
//
//   trackVersion  the version, for humans. The game recognizes a new version
//                 by the LEVD hash (RLD_DIR_HASH_OFFSET), not by this.
//   modes         which modes the track offers, see RLD_MODE_*.
//   name, author  the two strings.
//
// Removed on 2026-08-29: license, description, tool version and the
// track_uuid - neither the reader nor anyone else ever needed any of them,
// they were displayed and nothing else.
//
// REMOVED ON 2026-09-19: publicKey. It was the author identity
// and had exactly one consumer, Rld_VerifySignature. With the
// signature it goes too.
//
// ITS 32 BYTES AT 0x04 STAY AS A GAP, and that is intentional: had
// they been closed, trackVersion, modes, primBytes, memTotal and the
// whole string table would have moved by 32 bytes, and every existing
// container would have carried a wrong name on the next start. The
// packer now writes zeros there, the reader does not read them. A gap that
// costs nothing, against a field shift that breaks all existing tracks.
//
// What this gives up must be said: with publicKey and track_uuid the
// last handle is gone by which TWO containers could be recognized as the same track or as
// different authors. Two files with the same name in
// META cannot be told apart by the player. Since the level IDs the game tells them
// apart by FILE NAME (track-ids.tsv).
//
// metaVersion: 0 means draft (DRAFT), 1 means frozen (4.1, 2026-09-28;
// that question is closed: META stays small, new things go into optional
// chunks). Both load. The layout here never changes again - the numeric part
// has no length field, every new number would be a major bump (3 -> 4 on 08-29).
//
// primBytes and memTotal are BINDING: the game recomputes from the
// LEV when loading (Rld_MemNeed) and refuses a container that declares too little.
struct RldMeta
{
	u32 metaVersion;
	// 0x04..0x23 reserved: publicKey used to be here. Do not reuse without
	// checking what existing containers have there.
	u32 trackVersion;
	u32 modes;

	// What this track needs in memory, computed by the packer from the LEV -
	// see Rld_MemNeed. It is here so the list knows it WITHOUT touching
	// LEVD: the list must not do that, and without these two numbers
	// the start would have to read every LEV in the folder to know how much
	// to reserve.
	u32 primBytes;
	u32 memTotal;

	u32 stringCount;
	char strings[RLD_STRING_COUNT][513];
};

// The fixed part: 0x00 metaVersion, 0x04 reserved (32 bytes), 0x24
// trackVersion, 0x28 modes, 0x2c primBytes, 0x30 memTotal, 0x34 stringCount,
// from 0x38 the strings.
#define RLD_META_FIXED_SIZE 0x38

static const char *Rld_ParseMeta(struct RldMeta *out, const u8 *bytes, size_t size)
{
	size_t at;
	u32 i;

	memset(out, 0, sizeof(*out));

	if (size < RLD_META_FIXED_SIZE)
	{
		return "META is shorter than its fixed part";
	}

	out->metaVersion = Rld_ReadLE32(&bytes[0x00]);
	// 0x04..0x23 is skipped, see the gap above.
	out->trackVersion = Rld_ReadLE32(&bytes[0x24]);
	out->modes = Rld_ReadLE32(&bytes[0x28]);
	out->primBytes = Rld_ReadLE32(&bytes[0x2c]);
	out->memTotal = Rld_ReadLE32(&bytes[0x30]);
	out->stringCount = Rld_ReadLE32(&bytes[0x34]);

	at = RLD_META_FIXED_SIZE;
	for (i = 0; i < out->stringCount; i++)
	{
		u32 length;

		if ((at + 4u) > size)
		{
			return "the string table runs past the end";
		}

		length = Rld_ReadLE32(&bytes[at]);
		at += 4u;

		if (length > (size - at))
		{
			return "a string runs past the end";
		}

		// An older reader takes string_count entries and ignores the
		// rest - exactly for that reason new fields are only appended at the end.
		if (i < RLD_STRING_COUNT)
		{
			u32 copy = (length < 512u) ? length : 512u;

			memcpy(out->strings[i], &bytes[at], copy);
			out->strings[i][copy] = '\0';
		}

		at += length;
	}

	return NULL;
}

//----------------------------------------------------------------------------------------
// SNDB - THE SOUND BANK OF A TRACK
//----------------------------------------------------------------------------------------
//
// The first OPTIONAL chunk. Designed on 2026-08-31, with all
// measurements.
//
// WHAT IT SOLVES. SOUNDS/KART.HWL is a single file of 11.7 MiB that holds the
// sound of the whole game: 71 instrument banks and 33 sequences, each
// addressed through a u16 SECTOR NUMBER in the file's header. A
// community track brings its own sound by replacing individual ones of these
// entries and repacking the file. Measured on Inferno Island: four
// banks (3, 14, 15, 21) and four sequences (14, 26, 29, 32) have different
// CONTENT, together 1,220,608 bytes. The remaining 67 banks and 29 sequences are
// bit-identical and only MOVED - their offsets change, their content does not.
//
// That is why SNDB carries not the file but the entries. Whoever carries the whole
// bank carries 11.7 MiB for 1.2 MiB of content and on the side replaces the
// sound of the menu and of every other track too - the bank is global.
//
// ONLY FOR THE NITRO LOADER, AND BINDINGLY SO. This extension applies
// exclusively to container tracks. The disc loading path must not notice it
// anywhere, not today and not in any later loader. In the format
// this is already honored here, and without a single line of special case:
//
//   * SNDB is OPTIONAL. Rld_Open requires three chunks as the lower bound and up
//     to RLD_CHUNK_MAX; a container without SNDB is valid unchanged.
//   * A reader that does not know a type runs past it: for it
//     Rld_Open checks only the position (since 4.1 explicitly also without looking at
//     the compression value). Exactly that applies to SIGN and MMAP in
//     existing containers.
//   * Lookup goes through Rld_FindEntry, that is, by type and not by
//     position. A container without SNDB returns NULL there, and that is an
//     answer and not an error.
//   * Nothing here is called by Rld_Open or Rld_ReadChunk. The parser
//     below runs only when a caller calls it.
//
// NO VERSION BUMP. RLD_VERSION_MAJOR stays 4, and that applied to SNDB
// as to the removal of SIGN: Rld_Open rejects a foreign major version,
// and the existing containers would thereby be invalid. Instead SNDB carries
// its own small version number in its header.

// What the host has, measured on SOUNDS/KART.HWL of the NTSC-U disc via
// struct HowlHeader: numBanks 71, numSequences 33, numSpuAddrs 528. Here, because
// the reader has to check against them - a bank number 90 would be damage that
// only the game would notice.
#define RLD_HOWL_BANK_COUNT    71u
#define RLD_HOWL_SONG_COUNT    33u
#define RLD_HOWL_SPUADDR_COUNT 528u

// The game reads KART.HWL sector by sector. Every entry is therefore a multiple
// of it - any other alignment would have to be repacked again when inserting.
#define RLD_HOWL_SECTOR 2048u

// VERSION 2, since 2026-08-31: the header carries two more numbers, and the
// header is therefore 20 instead of 16 bytes long.
//
// WHY AT ALL. Until then an SNDB only said WHICH banks and sequences
// a patch replaces - not which of them the track plays. That is not enough,
// and it was measured: Inferno Island replaces four banks (3, 14, 15, 21) and
// three sequences (14, 26, 29), but sits on level 3, and there the game asks
// for bank 4 and sequence 3 - data.levBank_FX[level] and data.levBank_Song[level],
// game/zGlobal_DATA.c. None of the requested numbers occurs in the chunk. A
// replacement path that waits for the requested number never fires.
//
// WHY IT IS IN THE CHUNK AND NOT COMPUTED. It cannot be derived. The
// mod's mapping is in its rebuilt SCUS_944.26, and this port does
// not run that; the level-indexed tables are demonstrably unchanged in the
// patch. Which of the three sequences is Inferno Island's music no
// tool can decide - the packer's user says it, and this is where what he said is stored.
//
// WHY A BUMP AND NOT A FREE FIELD. At 0x0a there are two unused
// bytes that could have carried the two numbers without lengthening the header.
// That would be the worse choice: the zero that is there in every version 1 chunk
// is, as bank 0 and sequence 0, a VALID value - Dingo Canyon plays
// sequence 0. An old chunk would thereby look like a promise that was never
// made. With the version number Rld_ParseSndb rejects it, and that is the
// more honest answer. RLD_VERSION_MAJOR stays untouched at 4 by this, so
// all four-part containers stay valid; affected is only whoever itself
// already carries an SNDB.
#define RLD_SNDB_VERSION       2u
#define RLD_SNDB_HEADER_SIZE   20u
#define RLD_SNDB_ENTRY_SIZE    12u
#define RLD_SNDB_SPUFIXUP_SIZE 8u
#define RLD_SNDB_ENTRY_MAX     (RLD_HOWL_BANK_COUNT + RLD_HOWL_SONG_COUNT)

#define RLD_SNDB_KIND_BANK 0u
#define RLD_SNDB_KIND_SONG 1u

// An entry, unpacked. In the chunk it is 12 bytes:
// 0x00 kind (u8), 0x01 reserved (u8), 0x02 index (u16), 0x04 offset (u32),
// 0x08 size (u32). offset counts from payloadOffset, not from the chunk start.
struct RldSndbEntry
{
	u32 kind;
	u32 index;
	u32 offset;
	u32 size;
};

// A SpuAddrEntry correction, unpacked. In the chunk 8 bytes:
// 0x00 index (u16), 0x02 spuAddr (u16), 0x04 spuSize (u16), 0x06 reserved (u16).
//
// For Inferno Island it is 6 of 528 rows, and only spuSize changes.
struct RldSndbSpuFixup
{
	u32 index;
	u32 spuAddr;
	u32 spuSize;
};

// The header, and POINTERS instead of copies.
//
// Rld_ParseMeta copies its strings out, because they are small and
// needed right away. SNDB carries up to RLD_LIMIT_SNDB of payload; it is
// not copied here but named. The caller holds the bytes anyway -
// it comes from Rld_ReadChunk and owns them.
//
// In the chunk it is RLD_SNDB_HEADER_SIZE = 20 bytes:
// 0x00 magic 'SNDB', 0x04 version (u16), 0x06 entryCount (u16),
// 0x08 spuFixupCount (u16), 0x0a reserved (u16), 0x0c payloadOffset (u32),
// 0x10 playBank (u16), 0x12 playSong (u16).
struct RldSndb
{
	u32 version;
	u32 entryCount;
	u32 spuFixupCount;

	// WHAT THIS TRACK PLAYS, instead of data.levBank_FX[level] and
	// data.levBank_Song[level]. Both numbers are checked by the parser: they
	// lie in the host's range AND are in the entry table of this
	// chunk. Whoever got this far can use them without further
	// question - there is a payload for both.
	u32 playBank;
	u32 playSong;

	const u8 *entryTable;
	const u8 *spuTable;
	const u8 *payload;
	size_t payloadSize;
};

// Fetch an entry from the table. The index is NOT checked - it comes
// from a loop up to sndb->entryCount, and Rld_ParseSndb has proven the table
// complete beforehand.
static void Rld_SndbEntry(const struct RldSndb *sndb, u32 i, struct RldSndbEntry *out)
{
	const u8 *at = &sndb->entryTable[(size_t)i * RLD_SNDB_ENTRY_SIZE];

	out->kind = at[0];
	out->index = Rld_ReadLE16(&at[0x02]);
	out->offset = Rld_ReadLE32(&at[0x04]);
	out->size = Rld_ReadLE32(&at[0x08]);
}

static void Rld_SndbSpuFixup(const struct RldSndb *sndb, u32 i, struct RldSndbSpuFixup *out)
{
	const u8 *at = &sndb->spuTable[(size_t)i * RLD_SNDB_SPUFIXUP_SIZE];

	out->index = Rld_ReadLE16(&at[0x00]);
	out->spuAddr = Rld_ReadLE16(&at[0x02]);
	out->spuSize = Rld_ReadLE16(&at[0x04]);
}

// Read SNDB and check it completely.
//
// The same handling as Rld_ParseMeta: NULL means fine, otherwise the reason in
// plain text. And the same stance as in Rld_ReadChunk - the container comes from
// a stranger, so no field from the file decides where memory is accessed.
//
// Checked, in this order: header size, magic, version, the three
// counters against what the host has at all, that the two tables and
// the payload fit into the chunk, and then every entry individually - kind,
// number, sector alignment, position in the payload, and that no two
// entries occupy the same number or overlap.
static const char *Rld_ParseSndb(struct RldSndb *out, const u8 *bytes, size_t size)
{
	size_t entryBytes;
	size_t spuBytes;
	size_t payloadAt;
	u32 payloadOffset;
	u32 i;

	memset(out, 0, sizeof(*out));

	if (size < RLD_SNDB_HEADER_SIZE)
	{
		return "SNDB is shorter than its header";
	}

	if (memcmp(bytes, "SNDB", 4) != 0)
	{
		return "SNDB does not start with its own magic";
	}

	out->version = Rld_ReadLE16(&bytes[0x04]);
	out->entryCount = Rld_ReadLE16(&bytes[0x06]);
	out->spuFixupCount = Rld_ReadLE16(&bytes[0x08]);
	payloadOffset = Rld_ReadLE32(&bytes[0x0c]);
	out->playBank = Rld_ReadLE16(&bytes[0x10]);
	out->playSong = Rld_ReadLE16(&bytes[0x12]);

	// Unlike with the container's major version, this is NOT an abort
	// for the whole container - it is one for the sound. A reader that does
	// not know this version drives the track silent instead of rejecting it.
	if (out->version != RLD_SNDB_VERSION)
	{
		return "SNDB carries a version this reader does not know";
	}

	if ((out->entryCount == 0u) || (out->entryCount > RLD_SNDB_ENTRY_MAX))
	{
		return "SNDB names no entries, or more than the host has";
	}

	if (out->spuFixupCount > RLD_HOWL_SPUADDR_COUNT)
	{
		return "SNDB names more SPU rows than the host has";
	}

	entryBytes = (size_t)out->entryCount * RLD_SNDB_ENTRY_SIZE;
	spuBytes = (size_t)out->spuFixupCount * RLD_SNDB_SPUFIXUP_SIZE;
	payloadAt = (size_t)RLD_SNDB_HEADER_SIZE + entryBytes + spuBytes;

	if (((size_t)payloadOffset < payloadAt) || ((size_t)payloadOffset > size))
	{
		return "the SNDB payload does not begin after its two tables";
	}

	out->entryTable = &bytes[RLD_SNDB_HEADER_SIZE];
	out->spuTable = &bytes[RLD_SNDB_HEADER_SIZE + entryBytes];
	out->payload = &bytes[payloadOffset];
	out->payloadSize = size - (size_t)payloadOffset;

	for (i = 0; i < out->entryCount; i++)
	{
		struct RldSndbEntry entry;
		u32 j;

		Rld_SndbEntry(out, i, &entry);

		if ((entry.kind != RLD_SNDB_KIND_BANK) && (entry.kind != RLD_SNDB_KIND_SONG))
		{
			return "an SNDB entry is neither a bank nor a sequence";
		}

		if (entry.index >= ((entry.kind == RLD_SNDB_KIND_BANK) ? RLD_HOWL_BANK_COUNT : RLD_HOWL_SONG_COUNT))
		{
			return "an SNDB entry names a bank or sequence the host does not have";
		}

		if ((entry.size == 0u) || ((entry.size % RLD_HOWL_SECTOR) != 0u))
		{
			return "an SNDB entry is empty or not a whole number of sectors";
		}

		if (((entry.offset % RLD_HOWL_SECTOR) != 0u) || ((size_t)entry.offset > out->payloadSize) ||
		    ((size_t)entry.size > (out->payloadSize - (size_t)entry.offset)))
		{
			return "an SNDB entry lies outside its own payload";
		}

		// Two entries for the same number would be a question without an answer,
		// and two overlapping ranges have the same rank: both mean
		// that the writer could not decide.
		for (j = 0; j < i; j++)
		{
			struct RldSndbEntry other;

			Rld_SndbEntry(out, j, &other);

			if ((other.kind == entry.kind) && (other.index == entry.index))
			{
				return "SNDB names the same bank or sequence twice";
			}

			if ((entry.offset < (other.offset + other.size)) && (other.offset < (entry.offset + entry.size)))
			{
				return "two SNDB entries overlap in the payload";
			}
		}
	}

	// WHAT THE TRACK PLAYS must be included too.
	//
	// The range check alone would be too little. A bank number 4 that exists in the
	// host but has no payload in this chunk would be a
	// promise without cover: the loading path would expect the sectors of this bank from the
	// container and find nothing. Both are therefore checked here,
	// and only AFTER the entry loop - before it the table is not proven
	// complete and must not be read.
	if ((out->playBank >= RLD_HOWL_BANK_COUNT) || (out->playSong >= RLD_HOWL_SONG_COUNT))
	{
		return "SNDB names a bank or sequence to play that the host does not have";
	}

	{
		int haveBank = 0;
		int haveSong = 0;

		for (i = 0; i < out->entryCount; i++)
		{
			struct RldSndbEntry entry;

			Rld_SndbEntry(out, i, &entry);

			if ((entry.kind == RLD_SNDB_KIND_BANK) && (entry.index == out->playBank))
			{
				haveBank = 1;
			}

			if ((entry.kind == RLD_SNDB_KIND_SONG) && (entry.index == out->playSong))
			{
				haveSong = 1;
			}
		}

		if ((haveBank == 0) || (haveSong == 0))
		{
			return "SNDB names a bank or sequence to play that it does not carry";
		}
	}

	for (i = 0; i < out->spuFixupCount; i++)
	{
		struct RldSndbSpuFixup fixup;

		Rld_SndbSpuFixup(out, i, &fixup);

		if (fixup.index >= RLD_HOWL_SPUADDR_COUNT)
		{
			return "an SNDB SPU row is outside the host table";
		}
	}

	return NULL;
}

//----------------------------------------------------------------------------------------
// PARM - THE VALUES OF A TRACK (4.1, 2026-09-28)
//----------------------------------------------------------------------------------------
//
// What a foreign track borrowed from the donor slot until 4.0, because META cannot
// grow: reverb, bot strength, ambient sound.
// Optional like SNDB, looked up by type, its own version number.
//
//   0x00 u16 version = 1
//   0x02 u16 count
//   0x04 count x { u16 key, u16 length, length bytes of value }
//
// An unknown key is skipped - a new one is not a bump. A
// known one with the wrong length or outside its range counts as
// INVALID: the default applies to it, the caller reports it loudly. Broken as a
// whole (foreign version, truncated, a key twice, bytes after the
// last entry): all defaults. The game knows the defaults themselves, not
// the format - the chunk only says what the author set.
#define RLD_PARM_VERSION      1u
#define RLD_PARM_HEADER_SIZE  4u
#define RLD_PARM_ENTRY_HEADER 4u

#define RLD_PARM_KEY_REVERB  1u
#define RLD_PARM_KEY_BOTS    2u
#define RLD_PARM_KEY_AMBIENT 3u

// Reverb: level 0..4 like data.reverbParams (HOWL_Reverb.c), 0xFF = off.
#define RLD_PARM_REVERB_MAX 4u
#define RLD_PARM_REVERB_OFF 0xffu
// Bots: row of data.ArcadeDifficulty, 0 Dingo Canyon .. 17 Turbo Track.
#define RLD_PARM_BOTS_MAX 17u

// At most this many entries: each has a header of 4 bytes.
#define RLD_PARM_ENTRY_MAX (RLD_LIMIT_PARM / RLD_PARM_ENTRY_HEADER)

// State of a known key.
enum RldParmState
{
	RLD_PARM_ABSENT = 0, // not set - the default applies
	RLD_PARM_SET,        // set and valid
	RLD_PARM_INVALID,    // set, but wrong length or outside the range
};

struct RldParm
{
	u32 version;

	enum RldParmState reverbState;
	u32 reverb;

	enum RldParmState botsState;
	u32 bots;

	enum RldParmState ambientState;
	u32 ambient[2];

	// Skipped keys: how many, and the first one, for the log line.
	u32 unknownCount;
	u32 firstUnknown;
};

static const char *Rld_ParseParm(struct RldParm *out, const u8 *bytes, size_t size)
{
	unsigned short seen[RLD_PARM_ENTRY_MAX];
	size_t at = RLD_PARM_HEADER_SIZE;
	u32 count;
	u32 i;

	memset(out, 0, sizeof(*out));

	if ((size < RLD_PARM_HEADER_SIZE) || (size > RLD_LIMIT_PARM))
	{
		return "PARM is shorter than its header or larger than the format allows";
	}

	out->version = Rld_ReadLE16(&bytes[0x00]);
	if (out->version != RLD_PARM_VERSION)
	{
		return "PARM has a version this build does not read";
	}

	count = Rld_ReadLE16(&bytes[0x02]);
	if (count > RLD_PARM_ENTRY_MAX)
	{
		return "PARM claims more entries than fit";
	}

	for (i = 0; i < count; i++)
	{
		u32 key;
		u32 length;
		const u8 *value;
		u32 k;

		if ((size - at) < RLD_PARM_ENTRY_HEADER)
		{
			memset(out, 0, sizeof(*out));
			return "a PARM entry runs past the end";
		}

		key = Rld_ReadLE16(&bytes[at]);
		length = Rld_ReadLE16(&bytes[at + 2u]);
		at += RLD_PARM_ENTRY_HEADER;

		if (length > (size - at))
		{
			memset(out, 0, sizeof(*out));
			return "a PARM value runs past the end";
		}

		for (k = 0; k < i; k++)
		{
			if (seen[k] == key)
			{
				memset(out, 0, sizeof(*out));
				return "a PARM key appears twice";
			}
		}
		seen[i] = (unsigned short)key;

		value = &bytes[at];
		at += length;

		if (key == RLD_PARM_KEY_REVERB)
		{
			out->reverb = (length == 1u) ? value[0] : 0u;
			out->reverbState = ((length == 1u) && ((value[0] <= RLD_PARM_REVERB_MAX) || (value[0] == RLD_PARM_REVERB_OFF))) ? RLD_PARM_SET
			                                                                                                              : RLD_PARM_INVALID;
		}
		else if (key == RLD_PARM_KEY_BOTS)
		{
			out->bots = (length == 1u) ? value[0] : 0u;
			out->botsState = ((length == 1u) && (value[0] <= RLD_PARM_BOTS_MAX)) ? RLD_PARM_SET : RLD_PARM_INVALID;
		}
		else if (key == RLD_PARM_KEY_AMBIENT)
		{
			if (length == 4u)
			{
				out->ambient[0] = Rld_ReadLE16(&value[0]);
				out->ambient[1] = Rld_ReadLE16(&value[2]);
				out->ambientState = RLD_PARM_SET;
			}
			else
			{
				out->ambientState = RLD_PARM_INVALID;
			}
		}
		else
		{
			if (out->unknownCount == 0u)
			{
				out->firstUnknown = key;
			}
			out->unknownCount++;
		}
	}

	if (at != size)
	{
		memset(out, 0, sizeof(*out));
		return "PARM has bytes after its last entry";
	}

	return NULL;
}

//----------------------------------------------------------------------------------------
// THE MAP OF A TRACK
//----------------------------------------------------------------------------------------
//
// The track screen of ARCADE -> CUSTOM shows below the preview window
// the map of the selected container (game/230/MM_NativeTrackSelect.c). In the
// race it comes from icons 3 and 4 of the LEV (gGT->ptrIcons[3]/[4]), the
// texels and color tables from the VRM. In the menu the VRM is not loaded, and
// only a strip of 32 rows is free there - measured, not assumed.
// The maps of Inferno Island and Sunset
// Vista have halves of 44 rows and do not fit into it.
//
// Since 4.1 the GAME scales such a map down itself (Rld_ScaleMap, below).
// From 2026-09-28 until 4.1 rldpack did that and put the result as chunk MMAP
// into the container; the name is blocked, an MMAP in old files is
// skipped. Not chosen, then as now: borrowing the texel space of a
// retail map while the custom screen is up. Then every
// exit of the screen would have to put it back, and a forgotten one would show up as a broken
// retail map.
//
// The description is struct RldMapHalf. Rld_LevMap reads it from LEV and VRM,
// Rld_ScaleMap scales it down. Both for the game and for the packer, which
// shows in its report what the menu will show - a second copy would be exactly
// the pattern from the head of this file.

#define RLD_MAP_LAYOUT 12

// The LEV begins with a size word; struct Level follows it, and
// all pointers in it are offsets from there (Rld_MemNeed).
#define RLD_MAP_LEV_BODY 4u
#define RLD_MAP_LEV_TEXLOOKUP 0x3cu
#define RLD_MAP_ICON_SIZE 0x20u
#define RLD_MAP_ICON_INDEX 0x10u
#define RLD_MAP_ICON_LAYOUT 0x14u
#define RLD_MAP_ICON_TOP 3
#define RLD_MAP_ICON_BOTTOM 4
#define RLD_MAP_MAX_BLOCKS 256

// The free strip in the menu VRAM, as MM_NativeTrackSelect.c occupies it
// (MM_NATIVE_MAP_STRIP_H, MM_NATIVE_MAP_SLOT_W): 32 rows, per half a
// slot of 128 halfwords, and never beyond its own texture page.
// If something changes there, it must change here too.
#define RLD_MAP_MENU_ROWS 32
#define RLD_MAP_MENU_SLOT_W 128

struct RldMapHalf
{
	// TextureLayout: u0 v0 clut u1 v1 tpage u2 v2 u3 v3. From the LEV, as it
	// is there; after Rld_ScaleMap with the UVs from the corner of its own texels.
	u8 layout[RLD_MAP_LAYOUT];

	// 0 = 4 bit, 1 = 8 bit, 2 = 15 bit (tpage bits 7-8).
	int depth;

	// The rectangle of the texels in VRAM halfwords and the UV at its top left
	// corner. From the LEV, the smallest rectangle around all four UV corners and
	// its position in the track's VRAM; after Rld_ScaleMap 0/0.
	int srcX, srcY, w, h;
	int texelBaseU, texelBaseV;
	unsigned short *texels;

	// The color table: 16 entries at 4 bit, 256 at 8 bit, none at 15 bit.
	// From the LEV its position in the track's VRAM; after Rld_ScaleMap x 0 and y = half,
	// so that each half gets its own row.
	int clutX, clutY, clutW;
	unsigned short clut[256];
};

struct RldMapVrmBlock
{
	int x, y, w, h;
	size_t pixels;
};

static void Rld_FreeMap(struct RldMapHalf half[2])
{
	int k;

	for (k = 0; k < 2; k++)
	{
		free(half[k].texels);
		half[k].texels = NULL;
	}
}

// The blocks of a VRM, as LOAD_VramFileCallback uploads them
// (LOAD_File.c:243-273): either a single TIM - rectangle at +0x0c,
// pixels from +0x14 - or packed, first word 0x20, then per block a
// size word and a TIM.
static int Rld_MapVrmBlocks(const u8 *vrm, size_t size, struct RldMapVrmBlock *out, int max)
{
	int count = 0;

	if (size < 0x14u)
	{
		return 0;
	}

	if (Rld_ReadLE32(&vrm[0]) != 0x20u)
	{
		out[0].x = (int)(short)Rld_ReadLE16(&vrm[0x0c]);
		out[0].y = (int)(short)Rld_ReadLE16(&vrm[0x0e]);
		out[0].w = (int)(short)Rld_ReadLE16(&vrm[0x10]);
		out[0].h = (int)(short)Rld_ReadLE16(&vrm[0x12]);
		out[0].pixels = 0x14u;
		return 1;
	}

	{
		size_t at = 4u;

		while (((at + 4u) <= size) && (count < max))
		{
			const u32 blockSize = Rld_ReadLE32(&vrm[at]);
			const size_t head = at + 4u;

			if ((blockSize == 0u) || ((head + 0x14u) > size))
			{
				break;
			}

			out[count].x = (int)(short)Rld_ReadLE16(&vrm[head + 0x0c]);
			out[count].y = (int)(short)Rld_ReadLE16(&vrm[head + 0x0e]);
			out[count].w = (int)(short)Rld_ReadLE16(&vrm[head + 0x10]);
			out[count].h = (int)(short)Rld_ReadLE16(&vrm[head + 0x12]);
			out[count].pixels = head + 0x14u;
			count++;

			at = head + (blockSize & ~3u);
		}
	}

	return count;
}

// A rectangle from the track's VRAM, row by row from the block that
// contains the whole row. 0 if a row lies in no block.
static int Rld_MapCopyRect(const u8 *vrm, size_t size, const struct RldMapVrmBlock *blocks, int blockCount, int x, int y, int w, int h,
                           unsigned short *dst)
{
	int row;

	for (row = 0; row < h; row++)
	{
		int b;
		int found = 0;

		for (b = 0; b < blockCount; b++)
		{
			const struct RldMapVrmBlock *blk = &blocks[b];

			if ((blk->x <= x) && ((x + w) <= (blk->x + blk->w)) && (blk->y <= (y + row)) && ((y + row) < (blk->y + blk->h)))
			{
				const size_t from = blk->pixels + (((size_t)(y + row - blk->y) * (size_t)blk->w) + (size_t)(x - blk->x)) * 2u;
				int col;

				if ((from + ((size_t)w * 2u)) > size)
				{
					return 0;
				}

				for (col = 0; col < w; col++)
				{
					dst[(row * w) + col] = (unsigned short)Rld_ReadLE16(&vrm[from + ((size_t)col * 2u)]);
				}

				found = 1;
				break;
			}
		}

		if (!found)
		{
			return 0;
		}
	}

	return 1;
}

// The map from LEV and VRM: the icons with global_IconArray_Index 3 and 4 from
// the levTexLookup (that is how DecalGlobal_Store sorts them in the race into
// gGT->ptrIcons[3]/[4]), their texels and color tables from the VRM. Every offset
// and every size is held against the chunk size. NULL means read;
// otherwise the text says why not, and out is empty.
static const char *Rld_LevMap(const u8 *lev, size_t levSize, const u8 *vrm, size_t vrmSize, struct RldMapHalf out[2])
{
	struct RldMapVrmBlock blocks[RLD_MAP_MAX_BLOCKS];
	const size_t body = RLD_MAP_LEV_BODY;
	const char *error = NULL;
	u32 lookup = 0;
	u32 numIcon = 0;
	u32 firstIcon = 0;
	size_t found[2] = {0, 0};
	int blockCount = 0;
	int k;

	memset(out, 0, 2u * sizeof(*out));

	if (levSize < (body + RLD_MAP_LEV_TEXLOOKUP + 4u))
	{
		return "the LEV is too short";
	}

	lookup = Rld_ReadLE32(&lev[body + RLD_MAP_LEV_TEXLOOKUP]);

	if (lookup == 0u)
	{
		return "no icon table in the LEV (levTexLookup is 0)";
	}

	if ((body + (size_t)lookup + 8u) > levSize)
	{
		return "levTexLookup points outside the LEV";
	}

	numIcon = Rld_ReadLE32(&lev[body + lookup + 0u]);
	firstIcon = Rld_ReadLE32(&lev[body + lookup + 4u]);

	if ((numIcon > 4096u) || ((body + (size_t)firstIcon + ((size_t)numIcon * RLD_MAP_ICON_SIZE)) > levSize))
	{
		return "the icon table lies outside the LEV";
	}

	{
		u32 i;

		for (i = 0; i < numIcon; i++)
		{
			const size_t at = body + firstIcon + ((size_t)i * RLD_MAP_ICON_SIZE);
			const int global = (int)Rld_ReadLE32(&lev[at + RLD_MAP_ICON_INDEX]);

			if (global == RLD_MAP_ICON_TOP)
			{
				found[0] = at;
			}

			if (global == RLD_MAP_ICON_BOTTOM)
			{
				found[1] = at;
			}
		}
	}

	if ((found[0] == 0u) || (found[1] == 0u))
	{
		return "the LEV has no icons 3 and 4 (the two map halves)";
	}

	blockCount = Rld_MapVrmBlocks(vrm, vrmSize, blocks, RLD_MAP_MAX_BLOCKS);

	if (blockCount <= 0)
	{
		return "the VRM has no blocks";
	}

	for (k = 0; (k < 2) && (error == NULL); k++)
	{
		struct RldMapHalf *half = &out[k];
		const u8 *layout = &lev[found[k] + RLD_MAP_ICON_LAYOUT];
		const u32 tpage = Rld_ReadLE16(&layout[6]);
		const u32 clut = Rld_ReadLE16(&layout[2]);
		const int depth = (int)((tpage >> 7) & 3u);
		const int per = (depth == 0) ? 4 : ((depth == 1) ? 2 : 1);
		const int pageX = (int)(tpage & 0x0fu) * 64;
		const int pageY = ((tpage & 0x10u) != 0u) ? 256 : 0;
		const u8 us[4] = {layout[0], layout[4], layout[8], layout[10]};
		const u8 vs[4] = {layout[1], layout[5], layout[9], layout[11]};
		int umin = 255, umax = 0, vmin = 255, vmax = 0;
		int c;

		if ((depth == 3) || ((tpage & 0x800u) != 0u))
		{
			error = "a map half has a texture page outside the VRAM";
			break;
		}

		for (c = 0; c < 4; c++)
		{
			umin = (us[c] < umin) ? us[c] : umin;
			umax = (us[c] > umax) ? us[c] : umax;
			vmin = (vs[c] < vmin) ? vs[c] : vmin;
			vmax = (vs[c] > vmax) ? vs[c] : vmax;
		}

		memcpy(half->layout, layout, sizeof(half->layout));
		half->depth = depth;
		half->texelBaseU = (umin / per) * per;
		half->texelBaseV = vmin;
		half->srcX = pageX + (umin / per);
		half->srcY = pageY + vmin;
		half->w = (umax / per) - (umin / per) + 1;
		half->h = vmax - vmin + 1;
		half->texels = (unsigned short *)malloc((size_t)half->w * (size_t)half->h * sizeof(unsigned short));

		if ((half->texels == NULL) || !Rld_MapCopyRect(vrm, vrmSize, blocks, blockCount, half->srcX, half->srcY, half->w, half->h, half->texels))
		{
			error = "the texels of a map half are not in the VRM";
			break;
		}

		half->clutW = (depth == 0) ? 16 : ((depth == 1) ? 256 : 0);
		half->clutX = (int)(clut & 0x3fu) * 16;
		half->clutY = (int)((clut >> 6) & 0x1ffu);

		// THE COLOR TABLE MAY REACH BEYOND THE VRM. Inferno Island
		// places an 8-bit table at x 800, 256 entries long - up to
		// 1055, past the block (up to 895) and past the VRAM. So the texels
		// use only the first colors. What is read is what the VRM carries from
		// clutX in one piece; the rest stays 0.
		if (half->clutW > 0)
		{
			int b;
			int got = 0;

			for (b = 0; b < blockCount; b++)
			{
				const struct RldMapVrmBlock *blk = &blocks[b];

				if ((blk->x <= half->clutX) && (half->clutX < (blk->x + blk->w)) && (blk->y <= half->clutY) && (half->clutY < (blk->y + blk->h)))
				{
					const int avail = blk->x + blk->w - half->clutX;

					got = (avail < half->clutW) ? avail : half->clutW;
					break;
				}
			}

			if ((got <= 0) || !Rld_MapCopyRect(vrm, vrmSize, blocks, blockCount, half->clutX, half->clutY, got, 1, &half->clut[0]))
			{
				error = "the color table of a map half is not in the VRM";
				break;
			}
		}
	}

	if (error != NULL)
	{
		Rld_FreeMap(out);
	}

	return error;
}

// Does the map fit into the menu's strip? The same two rules as
// MM_NativeTrackSelect_MapPlace; NULL means yes.
static const char *Rld_MapWhyNotMenu(const struct RldMapHalf half[2])
{
	int k;

	for (k = 0; k < 2; k++)
	{
		const int per = (half[k].depth == 0) ? 4 : ((half[k].depth == 1) ? 2 : 1);
		const int pageW = 256 / per;
		const int maxW = (pageW < RLD_MAP_MENU_SLOT_W) ? pageW : RLD_MAP_MENU_SLOT_W;

		if (half[k].h > RLD_MAP_MENU_ROWS)
		{
			return "a map half is higher than the free VRAM strip (32 rows)";
		}

		if (half[k].w > maxW)
		{
			return "a map half is wider than its texture page slot";
		}
	}

	return NULL;
}

// SCALING THE MAP DOWN - Rld_ScaleMap
//
// If the LEV's map does not fit into the menu's free strip (32 rows,
// Rld_MapWhyNotMenu), the GAME scales it down when the row is first displayed
// (NativeTrack_ReadMinimap). Until 4.0 the packer did that and put the result
// as chunk MMAP into the container; since 4.1 MMAP is dropped:
// a menu constant does not belong in the file format,
// and derived data in the file is a second source of truth.
// Measured on 2026-09-28: 0.68 ms (Inferno Island) and 0.87 ms (Sunset Vista),
// the result byte-identical to the MMAP rldpack had built - it is
// the same code, moved here. The packer still calls it to show in its report
// what the menu will show.
//
// The race reads the LEV's map as it is.
//
// HOW IT IS SCALED DOWN:
//   * Both halves at the SAME scale, so the map keeps its shape. The
//     taller half becomes 31 texels high - 32 rows in VRAM, because as with every
//     map from the LEV the rectangle carries the edge v2 along. The widths follow
//     in the same ratio, rounded.
//   * Every new texel averages the area of the old ones it covers: a
//     box filter with exact shares, in integers - two runs are
//     byte-identical.
//   * What is averaged is the COLOR, not the index. Index 5 and index 6 have
//     nothing to do with each other in any table.
//   * The result is mapped back onto the track's colors: the index that the old
//     half uses and whose color (r, g, b and the STP bit) is closest to the
//     average. No color arises that the author did not choose,
//     and the color table stays the same word for word.
//   * A texel becomes transparent (0x0000) if less than half of its
//     area is covered.
//   * A line two texels wide becomes just under 1.5 texels wide at 31/43. It
//     does not break, it turns gray instead of white in some places.
//   * The result has UVs from the corner of its own texels, clut 0 and of the
//     tpage only color depth and blend mode; page and color table slot are assigned by the
//     menu, which knows the strip.
//
// The color depth stays. 15 bit is not scaled down - no map in tracks/
// has it. Then the menu shows no map.
static s64 Rld_Square(s64 value)
{
	return value * value;
}

static const char *Rld_ScaleMap(const struct RldMapHalf in[2], struct RldMapHalf out[2])
{
	int drawnW[2];
	int drawnH[2];
	int tallest = 0;
	int k;

	memset(out, 0, 2u * sizeof(*out));

	for (k = 0; k < 2; k++)
	{
		const u8 *l = in[k].layout;

		if (in[k].depth > 1)
		{
			return "a 15-bit map is not scaled";
		}

		// Upright, the way UI_Map_DrawMap draws (width u1 - u0, height
		// v2 - v0): u0 = u2 < u1 = u3 and v0 = v1 < v2 = v3.
		if ((l[0] != l[8]) || (l[4] != l[10]) || (l[1] != l[5]) || (l[9] != l[11]) || (l[4] <= l[0]) || (l[9] <= l[1]))
		{
			return "a map half is not an upright rectangle";
		}

		drawnW[k] = l[4] - l[0];
		drawnH[k] = l[9] - l[1];
		tallest = (drawnH[k] > tallest) ? drawnH[k] : tallest;
	}

	for (k = 0; k < 2; k++)
	{
		const struct RldMapHalf *src = &in[k];
		struct RldMapHalf *dst = &out[k];
		const int per = (src->depth == 0) ? 4 : 2;
		const int bits = 16 / per;
		const u32 mask = (1u << bits) - 1u;
		const int u0 = src->layout[0];
		const int v0 = src->layout[1];
		const int sw = drawnW[k];
		const int sh = drawnH[k];
		const int rows = RLD_MAP_MENU_ROWS - 1;
		int tw = ((sw * rows) + (tallest / 2)) / tallest;
		int th = ((sh * rows) + (tallest / 2)) / tallest;
		u8 used[256];
		u8 *index;
		int transparent = -1;
		int x, y, X, Y, i;

		tw = (tw < 1) ? 1 : tw;
		th = (th < 1) ? 1 : th;

		// Which indices the old half uses - the choice is made only among
		// them. An 8-bit table can reach beyond the VRM
		// (Rld_LevMap), its unread entries are 0 and not a color.
		memset(used, 0, sizeof(used));

		for (y = 0; y < sh; y++)
		{
			for (x = 0; x < sw; x++)
			{
				const int u = u0 + x - src->texelBaseU;
				const u32 word = src->texels[((v0 + y - src->texelBaseV) * src->w) + (u / per)];

				used[(word >> ((u % per) * bits)) & mask] = 1;
			}
		}

		for (i = 0; i < src->clutW; i++)
		{
			if (used[i] && (src->clut[i] == 0u) && (transparent < 0))
			{
				transparent = i;
			}
		}

		index = (u8 *)calloc((size_t)tw * (size_t)th, 1);
		if (index == NULL)
		{
			Rld_FreeMap(out);
			return "out of memory";
		}

		// New texel X covers the old x from X*sw to (X+1)*sw, measured in
		// parts of 1/tw; old texel x lies there at x*tw to (x+1)*tw.
		// The same vertically. The sum of all shares is sw*sh.
		for (Y = 0; Y < th; Y++)
		{
			for (X = 0; X < tw; X++)
			{
				s64 weight = 0, opaque = 0, r = 0, g = 0, b = 0, stp = 0;
				s64 best = -1;
				int pick = -1;

				for (y = (Y * sh) / th; (y < sh) && ((y * th) < ((Y + 1) * sh)); y++)
				{
					const int y0 = ((Y * sh) > (y * th)) ? (Y * sh) : (y * th);
					const int y1 = (((Y + 1) * sh) < ((y + 1) * th)) ? ((Y + 1) * sh) : ((y + 1) * th);

					for (x = (X * sw) / tw; (x < sw) && ((x * tw) < ((X + 1) * sw)); x++)
					{
						const int x0 = ((X * sw) > (x * tw)) ? (X * sw) : (x * tw);
						const int x1 = (((X + 1) * sw) < ((x + 1) * tw)) ? ((X + 1) * sw) : ((x + 1) * tw);
						const int u = u0 + x - src->texelBaseU;
						const u32 word = src->texels[((v0 + y - src->texelBaseV) * src->w) + (u / per)];
						const u32 color = src->clut[(word >> ((u % per) * bits)) & mask];
						const s64 part = (s64)(x1 - x0) * (s64)(y1 - y0);

						if (part <= 0)
						{
							continue;
						}

						weight += part;

						if (color != 0u)
						{
							opaque += part;
							r += part * (s64)(color & 31u);
							g += part * (s64)((color >> 5) & 31u);
							b += part * (s64)((color >> 10) & 31u);
							stp += part * (((color & 0x8000u) != 0u) ? 31 : 0);
						}
					}
				}

				if (((opaque * 2) < weight) && (transparent >= 0))
				{
					pick = transparent;
				}
				else
				{
					for (i = 0; i < src->clutW; i++)
					{
						const u32 c = src->clut[i];
						s64 d;

						if (!used[i] || (c == 0u))
						{
							continue;
						}

						d = Rld_Square(r - (opaque * (s64)(c & 31u))) + Rld_Square(g - (opaque * (s64)((c >> 5) & 31u))) +
						    Rld_Square(b - (opaque * (s64)((c >> 10) & 31u))) + Rld_Square(stp - (opaque * (((c & 0x8000u) != 0u) ? 31 : 0)));

						if ((best < 0) || (d < best))
						{
							best = d;
							pick = i;
						}
					}
				}

				index[(Y * tw) + X] = (u8)((pick >= 0) ? pick : 0);
			}
		}

		// Pack together, with the edge column and the edge row: they repeat
		// the last column and row, as the neighboring texel in VRAM would
		// on a map from the LEV.
		dst->depth = src->depth;
		dst->w = (tw + 1 + per - 1) / per;
		dst->h = th + 1;
		dst->texels = (unsigned short *)calloc((size_t)dst->w * (size_t)dst->h, sizeof(unsigned short));

		if (dst->texels == NULL)
		{
			free(index);
			Rld_FreeMap(out);
			return "out of memory";
		}

		for (Y = 0; Y < dst->h; Y++)
		{
			for (X = 0; X < (dst->w * per); X++)
			{
				const int sx = (X < tw) ? X : (tw - 1);
				const int sy = (Y < th) ? Y : (th - 1);

				dst->texels[(Y * dst->w) + (X / per)] |= (unsigned short)((u32)index[(sy * tw) + sx] << ((X % per) * bits));
			}
		}

		free(index);

		memset(dst->layout, 0, sizeof(dst->layout));
		dst->layout[4] = (u8)tw;
		dst->layout[9] = (u8)th;
		dst->layout[10] = (u8)tw;
		dst->layout[11] = (u8)th;
		Rld_WriteLE16(&dst->layout[6], Rld_ReadLE16(&src->layout[6]) & 0x01e0u);

		dst->clutW = src->clutW;
		memcpy(dst->clut, src->clut, sizeof(dst->clut));
		dst->clutX = 0;
		dst->clutY = k;
	}

	if (Rld_MapWhyNotMenu(out) != NULL)
	{
		const char *why = Rld_MapWhyNotMenu(out);

		Rld_FreeMap(out);
		return why;
	}

	return NULL;
}


#if defined(RLDTRACK_WITH_BUILDER)

//----------------------------------------------------------------------------------------
// THE ABR FIELD - why a track without a single image error runs at half
// speed.
//
// Measured on 2026-08-29 over three runs with --perf. On the container
// track Test 2, 35.5 ms of 48.4 ms of work per frame went into
// renderer_draw_triangles. The CPU path that turns BSP leaves into primitives
// cost 0.53 ms of that. It is not the geometry, it is the
// draw commands: 4791 per frame against 272 on Dingo Canyon, with only
// four times as many primitives.
//
// THE CHAIN, line by line:
//
//   DrawLevelOvr1P_SelectRawPrimitiveCode
//       ((uv1Word & 0x00600000) != 0x00600000) ? semi-transparent : opaque
//
//   uv1Word is the second word of the TextureLayout; bits 21..22 are the
//   tpage bits 5..6, the ABR field. If ABR is not 3, the face gets the
//   semi-transparent primitive code.
//
//   native_gpu.c  psxTexturedSemiTrans = semiTrans && textured
//   native_gpu.c  if (!psxTexturedSemiTrans && ...)   <- the merge test
//   native_gpu.c  if (split->psxTexturedSemiTrans)    <- draw twice
//
//   If the bit is set, the merge test is SKIPPED. Every
//   primitive gets its own split, and every split is drawn
//   twice. One primitive, one split, two draw commands - instead of many
//   primitives in one.
//
// MEASURED over all 25 disc tracks and all 21 test files: on the disc
// 0.0 to 8.3 percent of the quadblock faces are semi-transparent, median
// 1.6, and seven arenas have exactly none. In all 21 test files it is
// 100.0 percent, and the ABR field is 0 everywhere. The exporter never
// writes it.
//
// WHY THE IMAGE IS STILL RIGHT - and why it may be corrected here:
// the second pass blends ONLY texels whose sampled 16-bit word carries bit 15.
// If none of the texels this face touches carries this bit,
// the second pass paints nothing. It only costs.
//
// That applies to EVERY user of the texture, not only to quadblocks: even where a
// primitive is semi-transparent on its own account,
// GET_TPAGE_BLEND only decides HOW the second pass blends - and it still paints
// nothing. That is why no investigation is needed into who else uses
// the texture.
//
// WHEN IN DOUBT, DO NOT SET. One face too many on the slow path costs time;
// one too few costs image. Not touched is:
//   - what is not a 4- or 8-bit image. At 16 bit the texel itself is the
//     color, that would be a different test.
//   - what carries an implausible tpage (bits 9..15 set). Exactly that is
//     also discarded by DrawLevelOvr1P_IsPlausibleTextureLayout.
//   - what reaches into VRAM that THIS VRM never writes. A zero in the
//     reconstruction is no proof that there is no STP bit there.
//
// AND ONLY THE ENTRIES THAT ARE NEEDED. An 8-bit image with 256
// CLUT entries from x=800 runs past the right edge of the VRAM - but if
// its texels use only indices 0..63, all used entries lie
// in the image. First read the texels, then their entries. Without this step
// 2664 of 8244 faces on one test track remained undecided; with it,
// zero.
//----------------------------------------------------------------------------------------

#define RLD_VRAM_W     1024
#define RLD_VRAM_H     512
#define RLD_ICONGROUP4 0x30u
#define RLD_TEXLAYOUT  0x0cu

struct RldAbrReport
{
	u32 layouts;        // TextureLayout records reached by quadblocks
	u32 layoutsFixed;   // of these, corrected
	u32 layoutsStp;     // of these, with a real STP texel - stay as they are
	u32 layoutsUnknown; // of these, undecidable - stay as they are
	u32 layoutsAlready; // of these, already opaque
	u32 faces;          // quadblock faces in total
	u32 facesFixed;     // of these, draw opaque afterwards
	u32 facesChanged;   // of these, THIS pass changed something
};

// The VRAM, the way LOAD_VramFileCallback builds it.
//
// struct VramHeader is 0x14 in size: 0xC header, RECT at 0xC, pixels from 0x14.
// Either a single TIM (word 0 != 0x20) or a chain (word 0 == 0x20,
// then per block a size and after it the header).
//
// The mask is the more important of the two return values: it says WHERE this
// file wrote anything at all. Everything else is unknown and
// not empty.
static int Rld_SignedHalf(u32 value)
{
	return (value >= 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

static int Rld_VramBuild(const u8 *vrm, size_t size, unsigned short **pixelsOut, u8 **writtenOut)
{
	const size_t cells = (size_t)RLD_VRAM_W * (size_t)RLD_VRAM_H;
	unsigned short *pixels = (unsigned short *)calloc(cells, sizeof(unsigned short));
	u8 *written = (u8 *)calloc(cells, 1);
	int chained;
	size_t at;

	if ((pixels == NULL) || (written == NULL))
	{
		free(pixels);
		free(written);
		return 0;
	}

	*pixelsOut = pixels;
	*writtenOut = written;

	if (size < 0x14u)
	{
		return 1;
	}

	chained = (Rld_ReadLE32(&vrm[0]) == 0x20u);
	at = chained ? 4u : 0u;

	while ((at + 0x14u) <= size)
	{
		size_t header = at;
		size_t end = size;
		int x;
		int y;
		int w;
		int h;
		int row;

		if (chained)
		{
			const u32 blockSize = Rld_ReadLE32(&vrm[at]);

			if ((blockSize == 0u) || (blockSize > size))
			{
				break;
			}

			header = at + 4u;
			end = header + (size_t)(blockSize & ~3u);

			if (((header + 0x14u) > size) || (end > size))
			{
				break;
			}

			at = header + (size_t)(blockSize & ~3u);
		}

		// RECT carries four signed short words. No cast to the
		// narrow type, because this file gets its types from the includer.
		x = Rld_SignedHalf(Rld_ReadLE16(&vrm[header + 0x0cu]));
		y = Rld_SignedHalf(Rld_ReadLE16(&vrm[header + 0x0eu]));
		w = Rld_SignedHalf(Rld_ReadLE16(&vrm[header + 0x10u]));
		h = Rld_SignedHalf(Rld_ReadLE16(&vrm[header + 0x12u]));

		if ((w > 0) && (h > 0) && (x >= 0) && (y >= 0) && ((x + w) <= RLD_VRAM_W) && ((y + h) <= RLD_VRAM_H))
		{
			for (row = 0; row < h; row++)
			{
				const size_t line = header + 0x14u + (size_t)row * (size_t)w * 2u;
				int i;

				if ((line + (size_t)w * 2u) > end)
				{
					break;
				}

				for (i = 0; i < w; i++)
				{
					const size_t slot = (size_t)(y + row) * (size_t)RLD_VRAM_W + (size_t)(x + i);

					pixels[slot] = (unsigned short)Rld_ReadLE16(&vrm[line + (size_t)i * 2u]);
					written[slot] = 1;
				}
			}
		}

		if (!chained)
		{
			break;
		}
	}

	return 1;
}

enum RldAbrVerdict
{
	RLD_ABR_SET = 0,     // no STP texel reachable - may become opaque
	RLD_ABR_STP,         // an STP texel lies within - stays
	RLD_ABR_UNKNOWN,     // undecidable - stays
	RLD_ABR_ALREADY      // ABR is already 3
};

// A TextureLayout, held against the reconstructed VRAM.
//
// Only the texels in the face's UV rectangle are read, and after that only the
// CLUT entries these texels name. Both must have been written by THIS VRM,
// otherwise the answer is unknown.
static enum RldAbrVerdict Rld_AbrVerdict(const u8 *layout, const unsigned short *pixels, const u8 *written)
{
	const u32 tpage = Rld_ReadLE16(&layout[0x06]);
	const u32 clut = Rld_ReadLE16(&layout[0x02]);
	u32 depth;
	int pageX;
	int pageY;
	int clutX;
	int clutY;
	int u;
	int v;
	int uLo;
	int uHi;
	int vLo;
	int vHi;
	u8 seen[256];
	int i;

	if ((tpage & 0xfe00u) != 0u)
	{
		return RLD_ABR_UNKNOWN;
	}

	if (((tpage >> 5) & 3u) == 3u)
	{
		return RLD_ABR_ALREADY;
	}

	depth = (tpage >> 7) & 3u;
	if (depth > 1u)
	{
		return RLD_ABR_UNKNOWN;
	}

	pageX = (int)(tpage & 0xfu) * 64;
	pageY = (int)((tpage >> 4) & 1u) * 256;
	clutX = (int)(clut & 0x3fu) * 16;
	clutY = (int)(clut >> 6);

	if ((clutY < 0) || (clutY >= RLD_VRAM_H))
	{
		return RLD_ABR_UNKNOWN;
	}

	// The four corners of the face: u0v0 at 0x00, u1v1 at 0x04, u2v2 at 0x08,
	// u3v3 at 0x0a.
	uLo = uHi = layout[0x00];
	vLo = vHi = layout[0x01];

	{
		static const int corners[4] = {0x00, 0x04, 0x08, 0x0a};

		for (i = 0; i < 4; i++)
		{
			const int cu = layout[corners[i]];
			const int cv = layout[corners[i] + 1];

			if (cu < uLo) { uLo = cu; }
			if (cu > uHi) { uHi = cu; }
			if (cv < vLo) { vLo = cv; }
			if (cv > vHi) { vHi = cv; }
		}
	}

	memset(seen, 0, sizeof(seen));

	for (v = vLo; v <= vHi; v++)
	{
		const int py = pageY + v;

		if ((py < 0) || (py >= RLD_VRAM_H))
		{
			return RLD_ABR_UNKNOWN;
		}

		for (u = uLo; u <= uHi; u++)
		{
			const int px = pageX + ((depth == 0u) ? (u >> 2) : (u >> 1));
			size_t slot;
			unsigned short word;

			if ((px < 0) || (px >= RLD_VRAM_W))
			{
				return RLD_ABR_UNKNOWN;
			}

			slot = (size_t)py * (size_t)RLD_VRAM_W + (size_t)px;
			if (!written[slot])
			{
				return RLD_ABR_UNKNOWN;
			}

			word = pixels[slot];
			seen[(depth == 0u) ? ((word >> ((u & 3) * 4)) & 0xfu) : ((word >> ((u & 1) * 8)) & 0xffu)] = 1;
		}
	}

	for (i = 0; i < 256; i++)
	{
		size_t slot;

		if (!seen[i])
		{
			continue;
		}

		if ((clutX + i) >= RLD_VRAM_W)
		{
			return RLD_ABR_UNKNOWN;
		}

		slot = (size_t)clutY * (size_t)RLD_VRAM_W + (size_t)(clutX + i);
		if (!written[slot])
		{
			return RLD_ABR_UNKNOWN;
		}

		if ((pixels[slot] & 0x8000u) != 0u)
		{
			return RLD_ABR_STP;
		}
	}

	return RLD_ABR_SET;
}

// All IconGroup4 reachable from a quadblock.
//
// ptr_texture_mid[0..3] at 0x1c and ptr_texture_low at 0x40. If bit 0 of the
// reference is set, it points not to the IconGroup4 itself but to
// ptrActiveTex of an AnimTex - see DrawLevelOvr1P_ResolveTexturePointer. Then
// the whole frame cycle counts, because at run time any of them can be there.
static u32 Rld_AbrResolveGroups(const u8 *lev, size_t size, u32 pointer, u32 *out, u32 max)
{
	const size_t body = 4;
	u32 count = 0;

	if (pointer == 0u)
	{
		return 0;
	}

	if ((pointer & 1u) != 0u)
	{
		const u32 anim = pointer - 1u;
		u32 frames;
		u32 i;

		if ((body + anim + 12u) > size)
		{
			return 0;
		}

		frames = Rld_ReadLE16(&lev[body + anim + 4u]);
		if ((frames == 0u) || (frames > 4096u) || ((body + anim + 12u + frames * 4u) > size))
		{
			return 0;
		}

		for (i = 0; (i < frames) && (count < max); i++)
		{
			const u32 group = Rld_ReadLE32(&lev[body + anim + 12u + i * 4u]);

			if ((group != 0u) && ((body + group + RLD_ICONGROUP4) <= size))
			{
				out[count++] = group;
			}
		}

		return count;
	}

	if ((body + pointer + RLD_ICONGROUP4) > size)
	{
		return 0;
	}

	out[0] = pointer;
	return 1;
}

// The pass itself. Writes into the LEV and reports what it did.
static int Rld_FixAbr(u8 *lev, size_t levSize, const u8 *vrm, size_t vrmSize, struct RldAbrReport *out)
{
	const size_t body = 4;
	unsigned short *pixels = NULL;
	u8 *written = NULL;
	u32 *done = NULL;
	u8 *doneFlags = NULL;
	u32 doneCount = 0;
	u32 mesh;
	u32 blocks;
	u32 quadArray;
	u32 i;

	memset(out, 0, sizeof(*out));

	if ((lev == NULL) || (levSize < (body + 0x200u)))
	{
		return 0;
	}

	mesh = Rld_ReadLE32(&lev[body + 0x00]);
	if ((mesh == 0u) || ((body + mesh + 0x20u) > levSize))
	{
		return 0;
	}

	blocks = Rld_ReadLE32(&lev[body + mesh + 0x00]);
	quadArray = Rld_ReadLE32(&lev[body + mesh + 0x0cu]);

	if ((blocks == 0u) || (blocks > (levSize / RLD_QUADBLOCK_BYTES)) || ((body + quadArray + blocks * RLD_QUADBLOCK_BYTES) > levSize))
	{
		return 0;
	}

	if (!Rld_VramBuild(vrm, vrmSize, &pixels, &written))
	{
		return 0;
	}

	// Touch every IconGroup4 only once - it is shared by many quadblocks,
	// and a second pass would count it as "already opaque"
	// and cheat the report out of its own work.
	done = (u32 *)calloc((size_t)blocks * 5u, sizeof(u32));
	doneFlags = (u8 *)calloc((size_t)blocks * 5u, 1);
	if ((done == NULL) || (doneFlags == NULL))
	{
		free(done);
		free(doneFlags);
		free(pixels);
		free(written);
		return 0;
	}

	for (i = 0; i < blocks; i++)
	{
		const size_t quad = body + quadArray + (size_t)i * RLD_QUADBLOCK_BYTES;
		int face;

		for (face = 0; face < 5; face++)
		{
			const u32 pointer = (face < 4) ? Rld_ReadLE32(&lev[quad + 0x1cu + (u32)face * 4u]) : Rld_ReadLE32(&lev[quad + 0x40u]);
			u32 groups[4096];
			u32 groupCount;
			u32 g;
			int fixedHere = 0;
			int changedHere = 0;

			if (face < 4)
			{
				out->faces++;
			}

			groupCount = Rld_AbrResolveGroups(lev, levSize, pointer, groups, 4096u);

			for (g = 0; g < groupCount; g++)
			{
				int level;
				u32 seenBefore = 0;
				u32 slot = doneCount;
				u32 d;

				// An IconGroup4 is shared by many quadblocks, and on the SECOND
				// visit its ABR is already 3 - set by us. Without a memory
				// the counter would then have read that as "was already opaque" and cheated the
				// pass out of its own work.
				for (d = 0; d < doneCount; d++)
				{
					if (done[d] == groups[g])
					{
						seenBefore = 1;
						slot = d;
						break;
					}
				}

				if (seenBefore)
				{
					if ((doneFlags[slot] & 1u) != 0u) { fixedHere = 1; }
					if ((doneFlags[slot] & 2u) != 0u) { changedHere = 1; }
					continue;
				}

				for (level = 0; level < 4; level++)
				{
					u8 *layout = &lev[body + groups[g] + (u32)level * RLD_TEXLAYOUT];
					const enum RldAbrVerdict verdict = Rld_AbrVerdict(layout, pixels, written);

					out->layouts++;

					switch (verdict)
					{
					case RLD_ABR_SET:
						{
							const u32 tpage = Rld_ReadLE16(&layout[0x06]);

							Rld_WriteLE16(&layout[0x06], (unsigned short)(tpage | 0x60u));
						}
						out->layoutsFixed++;
						fixedHere = 1;
						changedHere = 1;
						break;

					case RLD_ABR_STP:
						out->layoutsStp++;
						break;

					case RLD_ABR_ALREADY:
						out->layoutsAlready++;
						fixedHere = 1;
						break;

					default:
						out->layoutsUnknown++;
						break;
					}
				}

				if (doneCount < ((u32)blocks * 5u))
				{
					doneFlags[doneCount] = (u8)((fixedHere ? 1u : 0u) | (changedHere ? 2u : 0u));
					done[doneCount++] = groups[g];
				}
			}

			if (face < 4)
			{
				if (fixedHere) { out->facesFixed++; }
				if (changedHere) { out->facesChanged++; }
			}
		}
	}

	free(done);
	free(doneFlags);
	free(pixels);
	free(written);
	return 1;
}

// WRITING PARM - the counterpart to Rld_ParseParm. Only the set
// keys, in fixed order 1, 2, 3; if none is set, there is no
// chunk (NULL, *sizeOut 0). The values must already be valid - the packer
// checks that when parsing the switches.
static u8 *Rld_BuildParm(const struct RldParm *in, size_t *sizeOut)
{
	size_t size = RLD_PARM_HEADER_SIZE;
	size_t at;
	u32 count = 0;
	u8 *out;

	*sizeOut = 0;

	if (in->reverbState == RLD_PARM_SET)
	{
		size += RLD_PARM_ENTRY_HEADER + 1u;
		count++;
	}
	if (in->botsState == RLD_PARM_SET)
	{
		size += RLD_PARM_ENTRY_HEADER + 1u;
		count++;
	}
	if (in->ambientState == RLD_PARM_SET)
	{
		size += RLD_PARM_ENTRY_HEADER + 4u;
		count++;
	}

	if (count == 0u)
	{
		return NULL;
	}

	out = (u8 *)calloc(1, size);
	if (out == NULL)
	{
		return NULL;
	}

	Rld_WriteLE16(&out[0x00], RLD_PARM_VERSION);
	Rld_WriteLE16(&out[0x02], count);
	at = RLD_PARM_HEADER_SIZE;

	if (in->reverbState == RLD_PARM_SET)
	{
		Rld_WriteLE16(&out[at], RLD_PARM_KEY_REVERB);
		Rld_WriteLE16(&out[at + 2u], 1u);
		out[at + 4u] = (u8)in->reverb;
		at += RLD_PARM_ENTRY_HEADER + 1u;
	}
	if (in->botsState == RLD_PARM_SET)
	{
		Rld_WriteLE16(&out[at], RLD_PARM_KEY_BOTS);
		Rld_WriteLE16(&out[at + 2u], 1u);
		out[at + 4u] = (u8)in->bots;
		at += RLD_PARM_ENTRY_HEADER + 1u;
	}
	if (in->ambientState == RLD_PARM_SET)
	{
		Rld_WriteLE16(&out[at], RLD_PARM_KEY_AMBIENT);
		Rld_WriteLE16(&out[at + 2u], 4u);
		Rld_WriteLE16(&out[at + 4u], in->ambient[0]);
		Rld_WriteLE16(&out[at + 6u], in->ambient[1]);
		at += RLD_PARM_ENTRY_HEADER + 4u;
	}

	*sizeOut = size;
	return out;
}

#endif // RLDTRACK_WITH_BUILDER

#endif // RLDTRACK_INC
