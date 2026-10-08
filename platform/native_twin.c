// ===========================================================================
// THE RETAIL TWIN (renderer plan D.4 step 4d, stage Z0). See
// include/platform/native_twin.h for what it is for and what it rebuilds.
//
// THE RULES, as for the GPU set: nothing here touches the game - no Instance,
// Driver, GameTracker, scratchpad, MEMPACK, primMem or otMem, no coprocessor,
// no random numbers, no clock. The input is a model in memory (read only) and,
// for the textures, the VRAM words the caller hands over (read only); the
// output is the source it fills, in host memory.
//
// Line numbers "RB:" are game/RenderBucket/RenderBucket_QueueExecute.c at
// renderer 5240eda.
// ===========================================================================

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common.h>
#include <macros.h>
#include <platform.h>

#include <rldtrack.inc>
#include <rldchar.inc>

#include "platform/native_char_gpu.h"
#include "platform/native_probe.h"
#include "platform/native_renderer_types.h"
#include "platform/native_tex.h"
#include "platform/native_twin.h"

CTR_STATIC_ASSERT(sizeof(struct ModelAnim) == 0x18);
CTR_STATIC_ASSERT(sizeof(struct ModelFrame) == 0x1C);
CTR_STATIC_ASSERT(sizeof(struct TextureLayout) == 12);
CTR_STATIC_ASSERT(sizeof(struct AnimTex) == 0xC);
CTR_STATIC_ASSERT(sizeof(struct NativeProbeVertex) == 24);
CTR_STATIC_ASSERT(NATIVE_TWIN_TILES_MAX <= NATIVE_TWIN_TILES_PER_TEXTURE * RLDCHAR_TEX_COUNT_MAX);

// The mask of a packed vertex (RB:425-426): bits 16-18 cleared after the << 2,
// bit 18 (bit 0 of the up sum) kept for a full-height custom model.
#define NATIVE_TWIN_MASK_XY 0xfff8ffffu
#define NATIVE_TWIN_MASK_XY_FULL_HEIGHT 0xfffcffffu

// The scratchpad words from 0x140 (RB:709-717): the colour cache is word i,
// the packed vertex of slot k words 2k and 2k + 1.
#define NATIVE_TWIN_CACHE_WORDS (NATIVE_TWIN_SLOTS * 2)

// Command bits (include/rldchar.inc, THE COMMAND WORD).
#define NATIVE_TWIN_CMD_NEW_STRIP 0x80u
#define NATIVE_TWIN_CMD_FAN 0x40u
#define NATIVE_TWIN_CMD_FLIP 0x20000000u
#define NATIVE_TWIN_CMD_CULL 0x10000000u
#define NATIVE_TWIN_CMD_COLOUR_CACHE 0x08000000u
#define NATIVE_TWIN_CMD_CACHED 0x04u

enum
{
	NATIVE_TWIN_EVENT_SKIP = 0, // a texture index without a table: nothing is drawn (RB:3031-3035)
	NATIVE_TWIN_EVENT_G3 = 1,
	NATIVE_TWIN_EVENT_GT3 = 2,
};

// One place where the retail loop projects a triangle (RB:4689-4718).
struct NativeTwinEvent
{
	u32 record[3];   // corner 0, 1, 2 = SXY0, SXY1, SXY2
	u32 colour[3];   // RGB0..2 before DPCT
	u32 command;     // the command whose bits and texture index decide
	const struct TextureLayout *layout;
	u8 kind;
	u8 tile;
	u8 material;
};

internal int NativeTwin_Fail(char *why, size_t whySize, const char *format, ...)
{
	va_list args;

	if ((why != NULL) && (whySize > 0u))
	{
		va_start(args, format);
		vsnprintf(why, whySize, format, args);
		va_end(args);
	}
	return 0;
}

internal void NativeTwin_Put16(u8 *at, u32 value)
{
	at[0] = (u8)value;
	at[1] = (u8)(value >> 8);
}

internal void NativeTwin_Put32(u8 *at, u32 value)
{
	at[0] = (u8)value;
	at[1] = (u8)(value >> 8);
	at[2] = (u8)(value >> 16);
	at[3] = (u8)(value >> 24);
}

internal int NativeTwin_S16(u32 value)
{
	value &= 0xffffu;
	return (value >= 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

internal int NativeTwin_S8(int value)
{
	value &= 0xff;
	return (value >= 0x80) ? (value - 0x100) : value;
}

internal int NativeTwin_SignExtend(u32 value, int bits)
{
	const u32 sign = 1u << (bits - 1);
	const u32 low = value & ((1u << bits) - 1u);

	return (low & sign) ? ((int)low - (int)(1u << bits)) : (int)low;
}

// RenderBucket_GetSignedBits (RB:498-511): MSB first inside little-endian
// 32-bit words, sign-extended.
internal int NativeTwin_Bits(const u8 *bytes, int *bitIndex, int bits)
{
	const int b = *bitIndex >> 5;
	const int s = (32 - bits) - (*bitIndex & 31);
	const u32 word = Rld_ReadLE32(bytes + ((size_t)b * 4u));
	const u32 ret = (s < 0) ? ((word << -s) | (Rld_ReadLE32(bytes + ((size_t)(b + 1) * 4u)) >> (32 + s))) : (word >> s);

	*bitIndex += bits;
	return NativeTwin_SignExtend(ret, bits);
}

// RenderBucket_ReadDeltaComponentFromStream (RB:2592-2606): an 8-bit field is
// absolute, any shorter one adds to the running value and the temporal base.
internal int NativeTwin_DeltaComponent(const u8 *bytes, int *bitIndex, int bits, int base, int *accum)
{
	const int value = NativeTwin_Bits(bytes, bitIndex, bits + 1);

	*accum = (bits == 7) ? NativeTwin_S8(value) : NativeTwin_S8(*accum + value + base);
	return *accum;
}

// The records of one stored frame as the stack bytes (x, y, z) of
// RenderBucketVertex: raw (RB:2656-2663) or the delta stream with its temporal
// words (RB:2636-2655: the second field lands in .z, the third in .y). The
// records start vertexOffset bytes into the frame - for the next frame of a
// half frame the offset of the CURRENT frame, as retail takes it (RB:5585).
internal void NativeTwin_DecodeFrame(const struct ModelFrame *mf, int vertexOffset, const u32 *delta, u32 records, u8 (*out)[3])
{
	const u8 *bytes = (const u8 *)mf + vertexOffset;
	u32 k;

	if (delta == NULL)
	{
		for (k = 0; k < records; k++)
		{
			out[k][0] = bytes[(k * 3u) + 0u];
			out[k][1] = bytes[(k * 3u) + 1u];
			out[k][2] = bytes[(k * 3u) + 2u];
		}
		return;
	}

	{
		int bitIndex = 0;
		int a = 0;
		int b = 0;
		int c = 0;

		for (k = 0; k < records; k++)
		{
			const u32 temporal = Rld_ReadLE32((const u8 *)delta + ((size_t)k * 4u));
			const int xBits = (int)((temporal >> 6) & 7u);
			const int zBits = (int)((temporal >> 3) & 7u);
			const int yBits = (int)(temporal & 7u);
			const int bx = NativeTwin_SignExtend(temporal >> 25, 7) * 2;
			const int bz = NativeTwin_SignExtend(temporal >> 17, 8);
			const int by = NativeTwin_SignExtend(temporal >> 9, 8);

			NativeTwin_DeltaComponent(bytes, &bitIndex, xBits, bx, &a);
			NativeTwin_DeltaComponent(bytes, &bitIndex, zBits, bz, &b);
			NativeTwin_DeltaComponent(bytes, &bitIndex, yBits, by, &c);
			out[k][0] = (u8)a;
			out[k][1] = (u8)c;
			out[k][2] = (u8)b;
		}
	}
}

// The GTE input of a record of a full frame (RB:2468-2480, 2500-2503).
internal void NativeTwin_PackFull(const struct ModelFrame *mf, const u8 r[3], u32 mask, int g[3])
{
	const u32 origin = (u32)(u16)(mf->pos.x & 0x7fff) | ((u32)(u16)mf->pos.y << 16);
	const u32 xz = (u32)r[0] | ((u32)r[2] << 16);
	const u32 word = ((xz + origin) << 2) & mask;

	g[0] = NativeTwin_S16(word);
	g[1] = NativeTwin_S16(word >> 16);
	g[2] = NativeTwin_S16((u32)((int)mf->pos.z + (int)r[1]) << 2);
}

// The in-between pose of a half frame (RB:2483-2499, 2505-2513): current and
// next record and both origins summed, shifted by one, always the retail mask.
internal void NativeTwin_PackHalf(const struct ModelFrame *mf, const struct ModelFrame *nf, const u8 c[3], const u8 n[3], int g[3])
{
	const u32 origin = (u32)(u16)(mf->pos.x + nf->pos.x) | ((u32)(u16)(mf->pos.y + nf->pos.y) << 16);
	const u32 cxz = (u32)c[0] | ((u32)c[2] << 16);
	const u32 nxz = (u32)n[0] | ((u32)n[2] << 16);
	const u32 word = ((cxz + nxz + origin) << 1) & NATIVE_TWIN_MASK_XY;
	const int z = (int)c[1] + (int)n[1] + (int)mf->pos.z + (int)nf->pos.z;

	g[0] = NativeTwin_S16(word);
	g[1] = NativeTwin_S16(word >> 16);
	g[2] = NativeTwin_S16((u32)z << 1);
}

internal const struct ModelFrame *NativeTwin_StoredFrame(const struct ModelAnim *anim, u32 stored)
{
	return (const struct ModelFrame *)((const u8 *)anim + sizeof(struct ModelAnim) + ((size_t)stored * (u16)anim->frameSize));
}

// One pose: which stored frame(s) of which animation (RenderBucket_GetFrame,
// RB:2176-2240).
struct NativeTwinPose
{
	const struct ModelFrame *frame;
	const struct ModelFrame *next; // NULL: a full frame
	const u32 *delta;
};

// The GTE input of every record of a pose.
internal void NativeTwin_PoseInput(const struct NativeTwinPose *pose, u32 records, int fullHeight, u8 (*curr)[3], u8 (*next)[3], int (*g)[3])
{
	u32 k;

	NativeTwin_DecodeFrame(pose->frame, pose->frame->vertexOffset, pose->delta, records, curr);
	if (pose->next != NULL)
	{
		NativeTwin_DecodeFrame(pose->next, pose->frame->vertexOffset, pose->delta, records, next);
		for (k = 0; k < records; k++)
		{
			NativeTwin_PackHalf(pose->frame, pose->next, curr[k], next[k], g[k]);
		}
		return;
	}
	for (k = 0; k < records; k++)
	{
		NativeTwin_PackFull(pose->frame, curr[k], fullHeight ? NATIVE_TWIN_MASK_XY_FULL_HEIGHT : NATIVE_TWIN_MASK_XY, g[k]);
	}
}

// The poses of header 0: every logical frame of every animation in order, or
// the one still frame. 0 with why.
internal int NativeTwin_Poses(const struct ModelHeader *mh, struct NativeTwinSource *out, struct NativeTwinPose *poses, u32 *poseCount, char *why,
                              size_t whySize)
{
	u32 a;
	u32 count = 0;

	if (mh->ptrAnimations == NULL)
	{
		if (mh->ptrFrameData == NULL)
		{
			return NativeTwin_Fail(why, whySize, "header 0 has neither animations nor a still frame");
		}
		poses[0].frame = mh->ptrFrameData;
		poses[0].next = NULL;
		poses[0].delta = (const u32 *)(uintptr_t)mh->unk3;
		*poseCount = 1;
		out->animCount = 0;
		return 1;
	}

	if ((mh->numAnimations == 0u) || (mh->numAnimations > NATIVE_TWIN_ANIMS_MAX))
	{
		return NativeTwin_Fail(why, whySize, "header 0 has %u animations, 1 to %d are handled", (unsigned)mh->numAnimations, NATIVE_TWIN_ANIMS_MAX);
	}

	out->animCount = mh->numAnimations;
	for (a = 0; a < mh->numAnimations; a++)
	{
		const struct ModelAnim *anim = mh->ptrAnimations[a];
		u32 logical;
		u32 last;
		u32 f;
		int half;

		if (anim == NULL)
		{
			return NativeTwin_Fail(why, whySize, "animation %u is missing", (unsigned)a);
		}
		logical = anim->numFrames & 0x7fffu;
		half = ((anim->numFrames & 0x8000u) != 0u) ? 1 : 0;
		last = (logical > 0u) ? (logical - 1u) : 0u;
		if (half)
		{
			last >>= 1;
		}
		out->animFrames[a] = (u16)logical;
		out->poseBase[a] = (u16)count;
		out->animHalf[a] = (u8)half;
		if ((count + logical) > NATIVE_TWIN_POSES_MAX)
		{
			return NativeTwin_Fail(why, whySize, "more than %d poses", NATIVE_TWIN_POSES_MAX);
		}

		for (f = 0; f < logical; f++)
		{
			struct NativeTwinPose *p = &poses[count++];
			u32 stored = half ? (f >> 1) : f;
			int hasNext = half ? (int)(f & 1u) : 0;

			if (stored > last)
			{
				stored = last;
			}
			p->frame = NativeTwin_StoredFrame(anim, stored);
			p->next = NULL;
			p->delta = anim->ptrDeltaArray;
			if (hasNext)
			{
				// The retail decoder reads stored frame + 1 (RB:2236-2239). An even
				// half-frame count names one past the last stored frame for its
				// last logical frame: what lies there is not known here, so the
				// stored frame stands alone and it is counted.
				if (stored + 1u > last)
				{
					out->count.halfPastEnd++;
				}
				else
				{
					p->next = NativeTwin_StoredFrame(anim, stored + 1u);
				}
			}
		}
	}

	if (count == 0u)
	{
		return NativeTwin_Fail(why, whySize, "every animation of header 0 has 0 frames");
	}
	*poseCount = count;
	out->standardPoses = (out->animCount == RLDCHAR_ANIM_COUNT) && (count == RLDCHAR_NET_POSES);
	for (a = 0; out->standardPoses && (a < RLDCHAR_ANIM_COUNT); a++)
	{
		out->standardPoses = (out->animFrames[a] == s_rldCharFrames[a]);
	}
	return 1;
}

// --- The command list (RenderBucket_DrawFunc_Normal, RB:4621-4718) -----------

struct NativeTwinWalk
{
	const u32 *colours;
	u32 colourCount;            // the words copied to the cache (word 0 of the list, RB:2541-2555)
	u8 overwritten[NATIVE_TWIN_CACHE_WORDS];
	int slotRecord[NATIVE_TWIN_SLOTS];
	u32 fifoRecord[4];
	u32 fifoColour[4];
	u32 stripLength;
	u32 primCommand;
	u32 records;
};

// A colour: from the table, or (cache) from the scratchpad copy - which a slot
// write may have overwritten (include/rldchar.inc, model-slot-color). Such a
// colour would be part of a packed vertex and change with the pose: refused.
internal int NativeTwin_Colour(struct NativeTwinWalk *w, u32 byteOffset, int cache, u32 commandIndex, u32 *out, struct NativeTwinCount *count, char *why,
                               size_t whySize)
{
	const u32 index = byteOffset / 4u;

	if (cache)
	{
		count->cacheColours++;
		if (index >= w->colourCount)
		{
			return NativeTwin_Fail(why, whySize, "command %u reads colour %u from the cache, which holds %u", (unsigned)commandIndex, (unsigned)index,
			                       (unsigned)w->colourCount);
		}
		if (w->overwritten[index])
		{
			return NativeTwin_Fail(why, whySize, "command %u reads colour %u from the cache after slot %u overwrote it", (unsigned)commandIndex,
			                       (unsigned)index, (unsigned)(index / 2u));
		}
	}
	*out = Rld_ReadLE32((const u8 *)w->colours + byteOffset);
	return 1;
}

// The events of the list, in order. 0 with why.
internal int NativeTwin_WalkList(const struct ModelHeader *mh, struct NativeTwinEvent *events, u32 eventMax, struct NativeTwinSource *out, char *why,
                                 size_t whySize)
{
	const u32 *list = (const u32 *)(uintptr_t)mh->ptrCommandList;
	struct NativeTwinWalk *w = (struct NativeTwinWalk *)calloc(1, sizeof(struct NativeTwinWalk));
	struct NativeTwinCount *count = &out->count;
	u32 index;
	int ok = 0;

	if (w == NULL)
	{
		return NativeTwin_Fail(why, whySize, "memory");
	}
	w->colours = mh->ptrColors;
	w->colourCount = list[0];
	for (index = 0; index < NATIVE_TWIN_SLOTS; index++)
	{
		w->slotRecord[index] = -1;
	}

	for (index = 1; list[index] != 0xffffffffu; index++)
	{
		const u32 command = list[index];
		const u32 flags = (command >> 24) & 0xffu;
		const u32 slot = (command >> 16) & 0xffu;
		u32 record;
		u32 colour;
		u32 drawCommand;
		int useRtps;
		int reuse;

		if (index > NATIVE_TWIN_COMMANDS_MAX)
		{
			NativeTwin_Fail(why, whySize, "the list has more than %d commands", NATIVE_TWIN_COMMANDS_MAX);
			goto done;
		}
		count->commands++;

		// A colour-only command (RB:2580-2590).
		if ((command >> 16) == 0u)
		{
			u32 a;
			u32 b;

			count->colourOnly++;
			if (!NativeTwin_Colour(w, (command >> 7) & 0x1fcu, (int)(command & 1u), index, &a, count, why, whySize) ||
			    !NativeTwin_Colour(w, command & 0x1fcu, (int)(command & 2u), index, &b, count, why, whySize))
			{
				goto done;
			}
			w->fifoColour[1] = a;
			w->fifoColour[2] = a;
			w->fifoColour[3] = b;
			continue;
		}

		// The vertex: a new record into its slot, or the slot's cached one
		// (RB:2615-2624).
		if ((flags & NATIVE_TWIN_CMD_CACHED) != 0u)
		{
			if (w->slotRecord[slot] < 0)
			{
				NativeTwin_Fail(why, whySize, "command %u reuses cache slot %u before any command wrote it", (unsigned)index, (unsigned)slot);
				goto done;
			}
			record = (u32)w->slotRecord[slot];
			count->cachedVertices++;
		}
		else
		{
			if (w->records >= NATIVE_TWIN_STREAM_MAX)
			{
				NativeTwin_Fail(why, whySize, "more than %d vertex records", NATIVE_TWIN_STREAM_MAX);
				goto done;
			}
			record = w->records++;
			w->slotRecord[slot] = (int)record;
			w->overwritten[slot * 2u] = 1;
			w->overwritten[(slot * 2u) + 1u] = 1;
		}
		if (!NativeTwin_Colour(w, (command >> 7) & 0x1fcu, (command & NATIVE_TWIN_CMD_COLOUR_CACHE) != 0u, index, &colour, count, why, whySize))
		{
			goto done;
		}

		w->fifoRecord[0] = w->fifoRecord[1];
		w->fifoRecord[1] = w->fifoRecord[2];
		w->fifoRecord[2] = w->fifoRecord[3];
		w->fifoRecord[3] = record;
		w->fifoColour[0] = w->fifoColour[1];
		w->fifoColour[1] = w->fifoColour[2];
		w->fifoColour[2] = w->fifoColour[3];
		w->fifoColour[3] = colour;

		if (((flags & NATIVE_TWIN_CMD_NEW_STRIP) != 0u) || (w->stripLength == 0u))
		{
			w->primCommand = command;
		}
		if ((flags & NATIVE_TWIN_CMD_NEW_STRIP) != 0u)
		{
			w->stripLength = 0;
			count->strips++;
		}

		useRtps = (w->stripLength > 2u);
		reuse = useRtps && ((flags & NATIVE_TWIN_CMD_FAN) != 0u);
		if (reuse)
		{
			w->fifoRecord[1] = w->fifoRecord[0];
			w->fifoColour[1] = w->fifoColour[0];
		}

		if (w->stripLength < 2u)
		{
			w->stripLength++;
			continue;
		}

		drawCommand = (w->stripLength == 2u) ? w->primCommand : command;
		if (count->events >= eventMax)
		{
			NativeTwin_Fail(why, whySize, "more than %u triangles", (unsigned)eventMax);
			goto done;
		}

		{
			struct NativeTwinEvent *e = &events[count->events++];
			const u32 texIndex = drawCommand & 0x1ffu;
			int c;

			for (c = 0; c < 3; c++)
			{
				e->record[c] = w->fifoRecord[1 + c];
				e->colour[c] = w->fifoColour[1 + c];
			}
			e->command = drawCommand;
			e->layout = NULL;
			e->kind = NATIVE_TWIN_EVENT_G3;
			if (texIndex != 0u)
			{
				// A null table draws nothing; a null entry draws a POLY_G3
				// (RB:3020-3041).
				if (mh->ptrTexLayout == NULL)
				{
					e->kind = NATIVE_TWIN_EVENT_SKIP;
				}
				else
				{
					e->layout = mh->ptrTexLayout[texIndex - 1u];
					e->kind = (e->layout != NULL) ? NATIVE_TWIN_EVENT_GT3 : NATIVE_TWIN_EVENT_G3;
				}
			}
		}

		w->stripLength++;
	}

	out->streamVertices = w->records;
	count->records = w->records;
	ok = 1;

done:
	free(w);
	return ok;
}

// --- Animated textures --------------------------------------------------------------

#define NATIVE_TWIN_LAYOUT_SLOTS 512 // texture index 1..511 names entry index - 1
#define NATIVE_TWIN_ANIMTEX_MAX 256

// The layout table entries the header's animtex cycles (CTR_CycleTex_Model,
// game/CTR/CTR_CycleTex.c: *ptrActiveTex = ptrArray[frame], the next AnimTex
// after its ptrArray, the list ends at a word equal to the first AnimTex),
// only where CTR_CycleTex_AllModels cycles at all (animtex set, flags bit 1
// clear). The table has no length of its own: a slot counts as in it when its
// index is one the list names (below entries, the highest texture index of a
// triangle). Read only.
internal void NativeTwin_AnimatedSlots(const struct ModelHeader *mh, u32 entries, u8 animated[NATIVE_TWIN_LAYOUT_SLOTS], struct NativeTwinCount *count)
{
	const struct AnimTex *first = mh->animtex;
	const struct AnimTex *cur = first;
	const u8 *table = (const u8 *)mh->ptrTexLayout;
	u32 n;

	memset(animated, 0, NATIVE_TWIN_LAYOUT_SLOTS);
	if ((first == NULL) || ((mh->flags & 2u) != 0u))
	{
		return;
	}
	for (n = 0; (n < NATIVE_TWIN_ANIMTEX_MAX) && (*(const u32 *)cur != (u32)(uintptr_t)first); n++)
	{
		const u8 *slot = (const u8 *)cur->ptrActiveTex;
		const int frames = (int)cur->numFrames;

		if ((table != NULL) && (slot >= table) && (((size_t)(slot - table) % sizeof(struct TextureLayout *)) == 0u) &&
		    (((size_t)(slot - table) / sizeof(struct TextureLayout *)) < entries))
		{
			const size_t index = (size_t)(slot - table) / sizeof(struct TextureLayout *);

			if (!animated[index])
			{
				animated[index] = 1;
				count->animatedLayouts++;
			}
		}
		else
		{
			count->animatedOutside++;
		}
		if ((frames <= 0) || (frames > 4096))
		{
			// A list the game would walk differently: not followed further.
			count->animatedOutside++;
			break;
		}
		cur = (const struct AnimTex *)((const u8 *)cur + sizeof(struct AnimTex) + ((size_t)frames * sizeof(struct IconGroup4 *)));
	}
}

// --- Tiles and materials ---------------------------------------------------------

internal int NativeTwin_TileOf(struct NativeTwinSource *out, const struct TextureLayout *layout, char *why, size_t whySize)
{
	const u32 w0 = Rld_ReadLE32(&layout->u0);
	const u32 w1 = Rld_ReadLE32(&layout->u1);
	const u16 tpage = (u16)(w1 >> 16);
	u32 depth = ((u32)tpage >> 7) & 3u;
	u16 clut = (u16)(w0 >> 16);
	u32 t;

	// Depth 3 is reserved on the PS1 GPU and reads as 15 bits.
	if (depth == 3u)
	{
		depth = 2u;
	}
	if (depth == 2u)
	{
		clut = 0;
	}

	for (t = 0; t < out->tileCount; t++)
	{
		const struct NativeTwinTile *tile = &out->tile[t];

		if ((tile->pageX == (u16)((tpage & 0xfu) * 64u)) && (tile->pageY == (u16)(((tpage >> 4) & 1u) * 256u)) && (tile->depth == depth) &&
		    (tile->clut == clut))
		{
			return (int)t;
		}
	}

	if (out->tileCount >= NATIVE_TWIN_TILES_MAX)
	{
		NativeTwin_Fail(why, whySize, "more than %d texture pages with their CLUTs", NATIVE_TWIN_TILES_MAX);
		return -1;
	}

	{
		struct NativeTwinTile *tile = &out->tile[out->tileCount];
		const u32 inTexture = out->tileCount % NATIVE_TWIN_TILES_PER_TEXTURE;

		memset(tile, 0, sizeof(*tile));
		tile->tpage = tpage;
		tile->clut = clut;
		tile->depth = (u8)depth;
		tile->texture = (u8)(out->tileCount / NATIVE_TWIN_TILES_PER_TEXTURE);
		tile->cellX = (u8)(inTexture % 4u);
		tile->cellY = (u8)(inTexture / 4u);
		tile->pageX = (u16)((tpage & 0xfu) * 64u);
		tile->pageY = (u16)(((tpage >> 4) & 1u) * 256u);
		tile->clutX = (u16)((clut & 0x3fu) * 16u);
		tile->clutY = (u16)((clut >> 6) & 0x1ffu);
	}
	return (int)out->tileCount++;
}

internal u32 NativeTwin_Pow2Cells(u32 cells)
{
	return (cells <= 1u) ? 1u : ((cells <= 2u) ? 2u : 4u);
}

// The edges of texture t: 4 tiles across at most, rows as needed, powers of two.
internal void NativeTwin_TextureEdges(const struct NativeTwinSource *src, u32 t, u32 *width, u32 *height)
{
	const u32 first = t * NATIVE_TWIN_TILES_PER_TEXTURE;
	u32 tiles = src->tileCount - first;

	if (tiles > NATIVE_TWIN_TILES_PER_TEXTURE)
	{
		tiles = NATIVE_TWIN_TILES_PER_TEXTURE;
	}
	*width = NATIVE_TWIN_TILE_EDGE * NativeTwin_Pow2Cells((tiles < 4u) ? tiles : 4u);
	*height = NATIVE_TWIN_TILE_EDGE * NativeTwin_Pow2Cells((tiles + 3u) / 4u);
}

internal int NativeTwin_MaterialOf(struct NativeTwinSource *out, s16 texture, u8 semi, u8 dither, u8 textured, u8 animated, char *why, size_t whySize)
{
	u32 m;

	for (m = 0; m < out->native.materialCount; m++)
	{
		const struct NativeTwinMaterial *mat = &out->material[m];
		const s16 have = (s16)Rld_ReadLE16(&out->native.materials[(m * RLDCHAR_NET_MATERIAL_BYTES) + 4u]);

		if ((have == texture) && (mat->semi == semi) && (mat->dither == dither) && (mat->textured == textured) && (mat->animated == animated))
		{
			return (int)m;
		}
	}
	if (out->native.materialCount >= RLDCHAR_NET_MATERIALS_MAX)
	{
		NativeTwin_Fail(why, whySize, "more than %u materials", (unsigned)RLDCHAR_NET_MATERIALS_MAX);
		return -1;
	}

	{
		u8 *mat = (u8 *)&out->native.materials[(size_t)out->native.materialCount * RLDCHAR_NET_MATERIAL_BYTES];

		memset(mat, 0, RLDCHAR_NET_MATERIAL_BYTES);
		mat[0] = 255;
		mat[1] = 255;
		mat[2] = 255;
		mat[3] = 255;
		NativeTwin_Put16(&mat[4], (u32)(u16)texture);
		mat[6] = textured ? 1u : 0u;                 // mask: the clear texel is not drawn; untextured opaque
		mat[7] = RLDCHAR_NET_MATERIAL_NEAREST;
		out->material[out->native.materialCount].textured = textured;
		out->material[out->native.materialCount].semi = semi;
		out->material[out->native.materialCount].dither = dither;
		out->material[out->native.materialCount].modulation = textured ? 2u : 1u;
		out->material[out->native.materialCount].animated = animated;
		out->count.animatedMaterials += animated ? 1u : 0u;
	}
	return (int)out->native.materialCount++;
}

internal void NativeTwin_PutF32(u8 *at, float value)
{
	u32 bits;

	memcpy(&bits, &value, sizeof(bits));
	NativeTwin_Put32(at, bits);
}

// --- The entry -------------------------------------------------------------------

void NativeTwin_Free(struct NativeTwinSource *src)
{
	u32 t;

	if (src == NULL)
	{
		return;
	}
	for (t = 0; t < RLDCHAR_TEX_COUNT_MAX; t++)
	{
		free(src->native.texture[t].rgba);
	}
	free(src->block);
	memset(src, 0, sizeof(*src));
}

int NativeTwin_FromModel(const struct Model *model, int fullHeight, struct NativeTwinSource *out, char *why, size_t whySize)
{
	if ((model == NULL) || (model->numHeaders < 1) || (model->headers == NULL))
	{
		memset(out, 0, sizeof(*out));
		return NativeTwin_Fail(why, whySize, "no model or no header");
	}
	if (!NativeTwin_FromHeader(&model->headers[0], fullHeight, out, why, whySize))
	{
		return 0;
	}
	memcpy(out->model, model->name, sizeof(model->name));
	out->model[sizeof(out->model) - 1u] = '\0';
	return 1;
}

int NativeTwin_FromHeader(const struct ModelHeader *mh, int fullHeight, struct NativeTwinSource *out, char *why, size_t whySize)
{
	struct NativeTwinEvent *events = NULL;
	struct NativeTwinPose *poses = NULL;
	u8 (*curr)[3] = NULL;
	u8 (*next)[3] = NULL;
	int (*g)[3] = NULL;
	u32 poseCount = 0;
	u32 triangles = 0;
	u32 e;
	u32 t;
	u32 p;
	u32 i;
	u32 N;
	size_t posesBytes;
	size_t total;
	u8 *at;
	u16 *uvTexel = NULL; // per twin vertex: texel u, v inside its tile, then the tile
	u8 animated[NATIVE_TWIN_LAYOUT_SLOTS];
	int ok = 0;

	memset(out, 0, sizeof(*out));
	out->fullHeight = fullHeight ? 1 : 0;
	if ((mh == NULL) || (mh->ptrCommandList == 0u) || (mh->ptrColors == NULL))
	{
		return NativeTwin_Fail(why, whySize, "header 0 has no command list or no colours");
	}
	memcpy(out->model, mh->name, 16u);
	out->model[16] = '\0';
	out->unit[0] = (double)mh->scale.x / 16384.0;
	out->unit[1] = (double)mh->scale.y / 16384.0;
	out->unit[2] = (double)(u16)mh->scale.z / 16384.0;

	events = (struct NativeTwinEvent *)calloc(NATIVE_TWIN_COMMANDS_MAX, sizeof(struct NativeTwinEvent));
	poses = (struct NativeTwinPose *)calloc(NATIVE_TWIN_POSES_MAX, sizeof(struct NativeTwinPose));
	if ((events == NULL) || (poses == NULL))
	{
		NativeTwin_Fail(why, whySize, "memory");
		goto done;
	}

	if (!NativeTwin_Poses(mh, out, poses, &poseCount, why, whySize) || !NativeTwin_WalkList(mh, events, NATIVE_TWIN_COMMANDS_MAX, out, why, whySize))
	{
		goto done;
	}
	{
		u32 entries = 0;

		for (e = 0; e < out->count.events; e++)
		{
			entries = ((events[e].command & 0x1ffu) > entries) ? (events[e].command & 0x1ffu) : entries;
		}
		NativeTwin_AnimatedSlots(mh, entries, animated, &out->count);
	}

	// The triangles: one per one-sided event, two per two-sided one.
	for (e = 0; e < out->count.events; e++)
	{
		const struct NativeTwinEvent *ev = &events[e];

		if (ev->kind == NATIVE_TWIN_EVENT_SKIP)
		{
			out->count.noTextureTable++;
			continue;
		}
		if ((ev->command & NATIVE_TWIN_CMD_CULL) != 0u)
		{
			out->count.oneSided++;
			triangles += 1u;
		}
		else
		{
			out->count.twoSided++;
			triangles += 2u;
		}
	}
	if (triangles == 0u)
	{
		NativeTwin_Fail(why, whySize, "the list draws no triangle");
		goto done;
	}
	N = triangles * 3u;
	if ((N > RLDCHAR_NET_VERTICES_MAX) || (triangles > RLDCHAR_NET_TRIANGLES_MAX))
	{
		NativeTwin_Fail(why, whySize, "%u triangles make %u corners, at most %u", (unsigned)triangles, (unsigned)N, (unsigned)RLDCHAR_NET_VERTICES_MAX);
		goto done;
	}
	if (out->streamVertices == 0u)
	{
		NativeTwin_Fail(why, whySize, "the list decodes no vertex");
		goto done;
	}

	// One block: poses, UV00, COL0, indices, materials, triangle materials, the
	// twin's own arrays.
	posesBytes = (size_t)poseCount * N * RLDCHAR_NET_VERTEX_BYTES;
	total = posesBytes + ((size_t)N * 8u) + ((size_t)N * 4u) + ((size_t)triangles * 6u) + (RLDCHAR_NET_MATERIALS_MAX * RLDCHAR_NET_MATERIAL_BYTES) +
	        ((size_t)triangles * 2u) + ((size_t)N * 4u) + ((size_t)triangles * 4u) + (size_t)triangles + ((size_t)triangles * 7u) + 64u;
	out->block = (u8 *)calloc(1, total);
	uvTexel = (u16 *)calloc((size_t)N * 3u, sizeof(u16));
	curr = (u8(*)[3])calloc(out->streamVertices, 3u);
	next = (u8(*)[3])calloc(out->streamVertices, 3u);
	g = (int(*)[3])calloc(out->streamVertices, sizeof(int[3]));
	if ((out->block == NULL) || (uvTexel == NULL) || (curr == NULL) || (next == NULL) || (g == NULL))
	{
		NativeTwin_Fail(why, whySize, "memory");
		goto done;
	}

	at = out->block;
	out->native.poses = at;
	at += posesBytes;
	out->native.uv = at;
	at += (size_t)N * 8u;
	out->native.colors = at;
	at += (size_t)N * 4u;
	out->native.indices = at;
	at += (size_t)triangles * 6u;
	out->native.materials = at;
	at += RLDCHAR_NET_MATERIALS_MAX * RLDCHAR_NET_MATERIAL_BYTES;
	out->native.triangleMaterials = at;
	at += (size_t)triangles * 2u;
	at = out->block + ((((size_t)(at - out->block)) + 3u) & ~(size_t)3u);
	out->cornerVertex = (u32 *)at;
	at += (size_t)N * 4u;
	out->triangleEvent = (u32 *)at;
	at += (size_t)triangles * 4u;
	out->triangleSide = at;
	at += triangles;
	out->semiUv = (u8(*)[7])at;

	out->native.cnet = out->block;
	out->native.cnetSize = total;
	out->native.vertexCount = N;
	out->native.triangleCount = triangles;
	out->native.indexSize = 2;
	out->native.poseCount = (poseCount > 1u) ? poseCount : 0u;
	out->native.wheel = NULL;

	// Corners, UV, colours, materials.
	t = 0;
	for (e = 0; e < out->count.events; e++)
	{
		struct NativeTwinEvent *ev = &events[e];
		static const u8 kept[3] = {0, 1, 2};
		static const u8 reversed[3] = {0, 2, 1};
		const u8 *orders[2];
		int copies;
		int copy;
		int tile = -1;
		s16 texture = -1;
		u8 semi = NATIVE_TWIN_SEMI_NONE;
		u8 dither = 1;
		u8 uv[3][2];
		u8 cycled = 0;
		int material;

		if (ev->kind == NATIVE_TWIN_EVENT_SKIP)
		{
			continue;
		}
		memset(uv, 0, sizeof(uv));
		if ((ev->colour[0] >> 24) != 0u)
		{
			out->count.colourCodeBits++;
		}

		if (ev->kind == NATIVE_TWIN_EVENT_GT3)
		{
			const u32 w0 = Rld_ReadLE32(&ev->layout->u0);
			const u32 w1 = Rld_ReadLE32(&ev->layout->u1);
			const u32 w2 = Rld_ReadLE32(&ev->layout->u2);
			const u16 tpage = (u16)(w1 >> 16);

			tile = NativeTwin_TileOf(out, ev->layout, why, whySize);
			if (tile < 0)
			{
				goto done;
			}
			texture = (s16)out->tile[tile].texture;
			// Code 0x34 for page mode 3, else 0x36 (RB:3116-3117).
			semi = ((w1 & 0x00600000u) == 0x00600000u) ? (u8)NATIVE_TWIN_SEMI_NONE : (u8)(NATIVE_TWIN_SEMI_MODE0 + ((tpage >> 5) & 3u));
			dither = (u8)((tpage >> 9) & 1u);
			uv[0][0] = (u8)w0;
			uv[0][1] = (u8)(w0 >> 8);
			uv[1][0] = (u8)w1;
			uv[1][1] = (u8)(w1 >> 8);
			uv[2][0] = (u8)w2;
			uv[2][1] = (u8)(w2 >> 8);
			out->count.textured++;
			if (semi != NATIVE_TWIN_SEMI_NONE)
			{
				u8 *tri = out->semiUv[out->semiCount++];
				int c;

				out->count.semi++;
				tri[0] = (u8)tile;
				for (c = 0; c < 3; c++)
				{
					tri[1 + (c * 2)] = uv[c][0];
					tri[2 + (c * 2)] = uv[c][1];
				}
			}
		}
		else
		{
			out->count.untextured++;
		}

		// A cycled table entry (a null one too: the cycle may make it a layout).
		if (((ev->command & 0x1ffu) != 0u) && animated[(ev->command & 0x1ffu) - 1u])
		{
			cycled = 1;
		}

		material = NativeTwin_MaterialOf(out, texture, semi, dither, (ev->kind == NATIVE_TWIN_EVENT_GT3) ? 1u : 0u, cycled, why, whySize);
		if (material < 0)
		{
			goto done;
		}
		ev->tile = (u8)((tile < 0) ? 0 : tile);
		ev->material = (u8)material;

		// The winding: retail keeps a one-sided triangle by the sign of NCLIP
		// over (SXY0, SXY1, SXY2): NCLIP > 0 with bit 29 clear, NCLIP < 0 with it
		// set (RB:2909-2932, instance flag REVERSE_CULL_DIRECTION clear). The native cull keeps NCLIP < 0 in the
		// same screen frame (counter-clockwise as seen, NATIVE_GFX_CULL_BACK):
		// bit 29 clear is written reversed, bit 29 set as listed.
		if ((ev->command & NATIVE_TWIN_CMD_CULL) != 0u)
		{
			orders[0] = ((ev->command & NATIVE_TWIN_CMD_FLIP) != 0u) ? kept : reversed;
			copies = 1;
		}
		else
		{
			orders[0] = kept;
			orders[1] = reversed;
			copies = 2;
		}

		for (copy = 0; copy < copies; copy++)
		{
			int c;

			for (c = 0; c < 3; c++)
			{
				const u32 v = (t * 3u) + (u32)c;
				const u8 corner = orders[copy][c];
				u8 *col = (u8 *)&out->native.colors[(size_t)v * 4u];

				out->cornerVertex[v] = ev->record[corner];
				uvTexel[(v * 3u) + 0u] = uv[corner][0];
				uvTexel[(v * 3u) + 1u] = uv[corner][1];
				uvTexel[(v * 3u) + 2u] = (u16)((tile < 0) ? 0xffffu : (u32)tile);
				col[0] = (u8)ev->colour[corner];
				col[1] = (u8)(ev->colour[corner] >> 8);
				col[2] = (u8)(ev->colour[corner] >> 16);
				col[3] = 255;
				NativeTwin_Put16((u8 *)&out->native.indices[(size_t)v * 2u], v);
			}
			NativeTwin_Put16((u8 *)&out->native.triangleMaterials[(size_t)t * 2u], (u32)material);
			out->count.animatedTriangles += cycled;
			out->triangleEvent[t] = e;
			out->triangleSide[t] = (u8)((copies == 1) ? 0 : (1 + copy));
			if (tile >= 0)
			{
				out->tile[tile].triangles++;
				out->tile[tile].semiTriangles += (semi != NATIVE_TWIN_SEMI_NONE) ? 1u : 0u;
			}
			t++;
		}
	}

	// UV in the texture of the tile: (cell x 256 + u) / width - the texel is
	// floor(u) as on the PSX path (native_shaders.inc, samplePSX), so no half
	// texel is added. Untextured corners get 0.
	out->native.textureCount = (out->tileCount + NATIVE_TWIN_TILES_PER_TEXTURE - 1u) / NATIVE_TWIN_TILES_PER_TEXTURE;
	for (i = 0; i < N; i++)
	{
		const u16 tile = uvTexel[(i * 3u) + 2u];
		float u = 0.0f;
		float v = 0.0f;

		if (tile != 0xffffu)
		{
			const struct NativeTwinTile *tl = &out->tile[tile];
			u32 width;
			u32 height;

			NativeTwin_TextureEdges(out, tl->texture, &width, &height);
			u = (float)((double)((tl->cellX * NATIVE_TWIN_TILE_EDGE) + uvTexel[i * 3u]) / (double)width);
			v = (float)((double)((tl->cellY * NATIVE_TWIN_TILE_EDGE) + uvTexel[(i * 3u) + 1u]) / (double)height);
		}
		NativeTwin_PutF32((u8 *)&out->native.uv[(size_t)i * 8u], u);
		NativeTwin_PutF32((u8 *)&out->native.uv[((size_t)i * 8u) + 4u], v);
	}
	for (i = 0; i < out->native.textureCount; i++)
	{
		struct RldCharTexture *tex = &out->native.texture[i];

		NativeTwin_TextureEdges(out, i, &tex->width, &tex->height);
		tex->flags = RLDCHAR_TEX_LINEAR | RLDCHAR_TEX_NEAREST | (1u << RLDCHAR_TEX_WRAP_U_SHIFT) | (1u << RLDCHAR_TEX_WRAP_V_SHIFT) |
		             (1u << RLDCHAR_TEX_ALPHA_SHIFT);
		tex->rgba = NULL;
		out->native.textureBytes += (u64)tex->width * tex->height * 4u;
	}

	// Every pose: the GTE input of its records, each twin vertex its record's
	// point in model units (normal 0: not used, the retail driver has no light).
	for (i = 0; i < 3u; i++)
	{
		out->native.hullMin[i] = 3.0e38f;
		out->native.hullMax[i] = -3.0e38f;
	}
	for (p = 0; p < poseCount; p++)
	{
		NativeTwin_PoseInput(&poses[p], out->streamVertices, out->fullHeight, curr, next, g);
		for (i = 0; i < N; i++)
		{
			const u32 k = out->cornerVertex[i];
			u8 *v = (u8 *)&out->native.poses[(((size_t)p * N) + i) * RLDCHAR_NET_VERTEX_BYTES];
			int a;

			for (a = 0; a < 3; a++)
			{
				const float value = (float)((double)g[k][a] * out->unit[a]);

				NativeTwin_PutF32(&v[a * 4], value);
				out->native.hullMin[a] = (value < out->native.hullMin[a]) ? value : out->native.hullMin[a];
				out->native.hullMax[a] = (value > out->native.hullMax[a]) ? value : out->native.hullMax[a];
			}
		}
	}

	out->native.state = (out->native.textureCount == 0u) ? RLDCHAR_NATIVE_READY : RLDCHAR_NATIVE_NONE;
	out->texturesDecoded = (out->native.textureCount == 0u);
	ok = 1;

done:
	free(events);
	free(poses);
	free(uvTexel);
	free(curr);
	free(next);
	free(g);
	if (!ok)
	{
		char keep[192];

		snprintf(keep, sizeof(keep), "%s", (why != NULL) ? why : "");
		NativeTwin_Free(out);
		if (why != NULL)
		{
			snprintf(why, whySize, "%s", keep);
		}
	}
	return ok;
}

int NativeTwin_PoseIndex(const struct NativeTwinSource *src, int anim, int frame)
{
	int count;

	if (src->animCount == 0u)
	{
		return 0;
	}
	if ((anim < 0) || ((u32)anim >= src->animCount) || (frame < 0) || (src->animFrames[anim] == 0u))
	{
		return -1;
	}
	count = (int)src->animFrames[anim];
	if (frame >= count)
	{
		// Retail holds a frame past the end at the last one (RB:2228-2231). For
		// half frames that is the last stored frame for an even frame; an odd
		// one would pair it with the frame behind it - not known here.
		if (!src->animHalf[anim])
		{
			frame = count - 1;
		}
		else if ((frame & 1) == 0)
		{
			frame = ((count - 1) >> 1) << 1;
		}
		else
		{
			return -1;
		}
	}
	return (int)src->poseBase[anim] + frame;
}

// --- The paint order ---------------------------------------------------------------

internal int NativeTwin_PaintKeyDescending(const void *a, const void *b)
{
	const u64 ka = *(const u64 *)a;
	const u64 kb = *(const u64 *)b;

	return (ka < kb) ? 1 : ((ka > kb) ? -1 : 0);
}

u32 NativeTwin_PaintBin(int sz0, int sz1, int sz2)
{
	const int sz[3] = {sz0, sz1, sz2};
	u32 sum = 0;
	int c;

	// SZ1..SZ3 are limited to 0..0xffff by the coprocessor; MAC0 = ZSF3 x the
	// sum stays below 2^31 (0x555 x 3 x 0xffff).
	for (c = 0; c < 3; c++)
	{
		sum += (u32)((sz[c] < 0) ? 0 : ((sz[c] > 0xffff) ? 0xffff : sz[c]));
	}
	return (NATIVE_TWIN_ZSF3 * sum) >> 17;
}

u32 NativeTwin_PaintOrder(const struct NativeTwinSource *src, u32 pose, const float screenFromModel[16], u64 *keys, u32 keyMax)
{
	u32 count[RLDCHAR_NET_MATERIALS_MAX];
	u32 start[RLDCHAR_NET_MATERIALS_MAX];
	const u32 T = (src != NULL) ? src->native.triangleCount : 0u;
	const u32 N = (src != NULL) ? src->native.vertexCount : 0u;
	const u32 poses = (src != NULL) ? ((src->native.poseCount == 0u) ? 1u : src->native.poseCount) : 0u;
	double depthScale;
	u32 next = 0;
	u32 m;
	u32 t;
	int pass;

	if ((src == NULL) || (screenFromModel == NULL) || (keys == NULL) || (T == 0u) || (T > keyMax) || (T > NATIVE_TWIN_PAINT_PLACE_MASK) ||
	    (N != (T * 3u)) || (pose >= poses) || (src->native.poses == NULL) || (src->native.triangleMaterials == NULL) ||
	    (src->native.materials == NULL) || (src->native.materialCount == 0u) || (src->native.materialCount > RLDCHAR_NET_MATERIALS_MAX))
	{
		return 0;
	}

	// The place of each triangle in the index buffer of the GPU set: sorted by
	// material, stable, first the opaque materials in ascending order, then the
	// masked ones (alpha mode 1) - the order NativeCharGpu_Build writes (the
	// self-test holds the two together).
	memset(count, 0, sizeof(count));
	memset(start, 0, sizeof(start));
	for (t = 0; t < T; t++)
	{
		const u32 material = (u32)Rld_ReadLE16(&src->native.triangleMaterials[(size_t)t * 2u]);

		if (material >= src->native.materialCount)
		{
			return 0;
		}
		count[material]++;
	}
	for (pass = 0; pass < 2; pass++)
	{
		for (m = 0; m < src->native.materialCount; m++)
		{
			const int masked = (src->native.materials[((size_t)m * RLDCHAR_NET_MATERIAL_BYTES) + 6u] == 1u) ? 1 : 0;

			if (masked != pass)
			{
				continue;
			}
			start[m] = next;
			next += count[m];
		}
	}

	// The depth the coprocessor gives a corner. Row w of the screen matrix is
	// the view depth in true view units (NativeRenderLayer_CharItemMatrix
	// divides by 2^shift), its last entry that of the model origin, the view
	// depth the queue tests: nearer than 0x1000 it loads the matrix 4 times
	// larger (RB:1614-1616, depthShift) and the translation with it, so SZ is
	// 4 w there and w beyond.
	depthScale = ((double)screenFromModel[15] < 4096.0) ? 4.0 : 1.0;

	for (t = 0; t < T; t++)
	{
		const u32 material = (u32)Rld_ReadLE16(&src->native.triangleMaterials[(size_t)t * 2u]);
		int sz[3];
		int c;

		for (c = 0; c < 3; c++)
		{
			const u8 *v = (const u8 *)&src->native.poses[(((size_t)pose * N) + ((size_t)t * 3u) + (u32)c) * RLDCHAR_NET_VERTEX_BYTES];
			float p[3];
			double w;
			int a;

			for (a = 0; a < 3; a++)
			{
				const u32 bits = Rld_ReadLE32(&v[a * 4]);

				memcpy(&p[a], &bits, sizeof(bits));
			}
			// SZ = MAC3, the view depth rounded down (the >> 12 of RTPT).
			w = ((double)screenFromModel[3] * (double)p[0]) + ((double)screenFromModel[7] * (double)p[1]) + ((double)screenFromModel[11] * (double)p[2]) +
			    (double)screenFromModel[15];
			w = floor(w * depthScale);
			sz[c] = (w < 0.0) ? 0 : ((w > 65535.0) ? 0xffff : (int)w);
		}
		keys[t] = ((u64)NativeTwin_PaintBin(sz[0], sz[1], sz[2]) << 40) | ((u64)t << 20) | (u64)start[material]++;
	}

	qsort(keys, T, sizeof(u64), NativeTwin_PaintKeyDescending);
	return T;
}

u32 NativeTwin_TextureFlags(void)
{
	return NATIVE_TEX_FLAG_LINEAR_DATA | NATIVE_TEX_FLAG_ALWAYS_NEAREST | NATIVE_TEX_FLAG_ONE_LEVEL;
}

// --- The VRAM ---------------------------------------------------------------------

// One texel of a tile through its CLUT: the 15-bit word (VRAM wraps at its
// edges, x at 1024 words, y at 512 rows).
internal u16 NativeTwin_Texel(const u16 *vram, const struct NativeTwinTile *tile, u32 u, u32 v)
{
	const u32 row = ((u32)tile->pageY + v) & (VRAM_HEIGHT - 1);

	if (tile->depth == 0u)
	{
		const u16 word = vram[(row * VRAM_WIDTH) + (((u32)tile->pageX + (u >> 2)) & (VRAM_WIDTH - 1))];
		const u32 index = ((u32)word >> ((u & 3u) * 4u)) & 0xfu;

		return vram[((u32)tile->clutY * VRAM_WIDTH) + (((u32)tile->clutX + index) & (VRAM_WIDTH - 1))];
	}
	if (tile->depth == 1u)
	{
		const u16 word = vram[(row * VRAM_WIDTH) + (((u32)tile->pageX + (u >> 1)) & (VRAM_WIDTH - 1))];
		const u32 index = ((u32)word >> ((u & 1u) * 8u)) & 0xffu;

		return vram[((u32)tile->clutY * VRAM_WIDTH) + (((u32)tile->clutX + index) & (VRAM_WIDTH - 1))];
	}
	return vram[(row * VRAM_WIDTH) + (((u32)tile->pageX + u) & (VRAM_WIDTH - 1))];
}

// The texels of a tile whose cell [u, u+1) x [v, v+1) the open UV triangle
// overlaps (separating axes: the two of the cell, the three edge normals) -
// the texels a 0x36 triangle can sample. tri: tile, u0, v0, u1, v1, u2, v2.
internal void NativeTwin_MarkTriangle(const u8 tri[7], u8 *mark)
{
	const int x[3] = {tri[1], tri[3], tri[5]};
	const int y[3] = {tri[2], tri[4], tri[6]};
	int lo[2] = {x[0], y[0]};
	int hi[2] = {x[0], y[0]};
	int u;
	int v;
	int c;

	for (c = 1; c < 3; c++)
	{
		lo[0] = (x[c] < lo[0]) ? x[c] : lo[0];
		lo[1] = (y[c] < lo[1]) ? y[c] : lo[1];
		hi[0] = (x[c] > hi[0]) ? x[c] : hi[0];
		hi[1] = (y[c] > hi[1]) ? y[c] : hi[1];
	}
	for (v = (lo[1] > 0) ? (lo[1] - 1) : 0; (v <= hi[1]) && (v < NATIVE_TWIN_TILE_EDGE); v++)
	{
		for (u = (lo[0] > 0) ? (lo[0] - 1) : 0; (u <= hi[0]) && (u < NATIVE_TWIN_TILE_EDGE); u++)
		{
			int apart = (hi[0] <= u) || (lo[0] >= (u + 1)) || (hi[1] <= v) || (lo[1] >= (v + 1));
			int e;

			for (e = 0; !apart && (e < 3); e++)
			{
				const int nx = -(y[(e + 1) % 3] - y[e]);
				const int ny = x[(e + 1) % 3] - x[e];
				int tLo = (nx * x[0]) + (ny * y[0]);
				int tHi = tLo;
				int bLo = (nx * u) + (ny * v);
				int bHi = bLo;
				int k;

				for (k = 1; k < 3; k++)
				{
					const int d = (nx * x[k]) + (ny * y[k]);

					tLo = (d < tLo) ? d : tLo;
					tHi = (d > tHi) ? d : tHi;
				}
				for (k = 1; k < 4; k++)
				{
					const int d = (nx * (u + (k & 1))) + (ny * (v + (k >> 1)));

					bLo = (d < bLo) ? d : bLo;
					bHi = (d > bHi) ? d : bHi;
				}
				apart = (tHi <= bLo) || (tLo >= bHi);
			}
			if (!apart)
			{
				mark[(v * NATIVE_TWIN_TILE_EDGE) + u] = 1;
			}
		}
	}
}

int NativeTwin_DecodeVram(const u16 *vram, struct NativeTwinSource *src)
{
	struct Sha256 sha;
	u32 t;
	u32 i;

	if ((vram == NULL) || (src == NULL) || (src->block == NULL))
	{
		return 0;
	}

	for (t = 0; t < src->native.textureCount; t++)
	{
		struct RldCharTexture *tex = &src->native.texture[t];

		free(tex->rgba);
		tex->rgba = (u8 *)calloc((size_t)tex->width * tex->height, 4u);
		if (tex->rgba == NULL)
		{
			return 0;
		}
	}

	for (i = 0; i < src->tileCount; i++)
	{
		struct NativeTwinTile *tile = &src->tile[i];
		struct RldCharTexture *tex = &src->native.texture[tile->texture];
		u8 *stpMark = (u8 *)calloc(NATIVE_TWIN_TILE_EDGE * NATIVE_TWIN_TILE_EDGE, 1u);
		u32 b;
		u32 u;
		u32 v;

		if (stpMark == NULL)
		{
			return 0;
		}
		for (b = 0; b < src->semiCount; b++)
		{
			if (src->semiUv[b][0] == (u8)i)
			{
				NativeTwin_MarkTriangle(src->semiUv[b], stpMark);
			}
		}

		tile->stpTexels = 0;
		tile->clearTexels = 0;
		for (v = 0; v < NATIVE_TWIN_TILE_EDGE; v++)
		{
			for (u = 0; u < NATIVE_TWIN_TILE_EDGE; u++)
			{
				const u16 c = NativeTwin_Texel(vram, tile, u, v);
				u8 *o = &tex->rgba[((((size_t)tile->cellY * NATIVE_TWIN_TILE_EDGE) + v) * tex->width + ((size_t)tile->cellX * NATIVE_TWIN_TILE_EDGE) + u) * 4u];

				if (c == 0u)
				{
					tile->clearTexels++;
					continue; // stays 0, 0, 0, 0
				}
				o[0] = (u8)((c & 31u) << 3);
				o[1] = (u8)(((c >> 5) & 31u) << 3);
				o[2] = (u8)(((c >> 10) & 31u) << 3);
				o[3] = 255;
				if (((c & 0x8000u) != 0u) && stpMark[(v * NATIVE_TWIN_TILE_EDGE) + u])
				{
					tile->stpTexels++;
				}
			}
		}
		free(stpMark);
	}

	Sha256_Init(&sha);
	for (t = 0; t < src->native.textureCount; t++)
	{
		const struct RldCharTexture *tex = &src->native.texture[t];

		Sha256_Update(&sha, tex->rgba, (size_t)tex->width * tex->height * 4u);
	}
	Sha256_Final(&sha, src->rgbaHash);
	src->texturesDecoded = 1;
	src->native.state = RLDCHAR_NATIVE_READY;
	return 1;
}

void NativeTwin_Describe(const struct NativeTwinSource *src, char *line, size_t size)
{
	const struct NativeTwinCount *c = &src->count;
	char poses[96];
	char hash[24];
	u32 stp = 0;
	u32 i;

	if (src->animCount == 0u)
	{
		snprintf(poses, sizeof(poses), "still");
	}
	else
	{
		size_t used = 0;

		poses[0] = '\0';
		for (i = 0; (i < src->animCount) && (used < sizeof(poses)); i++)
		{
			used += (size_t)snprintf(&poses[used], sizeof(poses) - used, "%s%u%s", (i == 0u) ? "" : "/", (unsigned)src->animFrames[i], src->animHalf[i] ? "h" : "");
		}
	}
	for (i = 0; i < src->tileCount; i++)
	{
		stp += src->tile[i].stpTexels;
	}
	if (src->texturesDecoded && (src->native.textureCount > 0u))
	{
		snprintf(hash, sizeof(hash), "%02x%02x%02x%02x%02x%02x%02x%02x", src->rgbaHash[0], src->rgbaHash[1], src->rgbaHash[2], src->rgbaHash[3], src->rgbaHash[4],
		         src->rgbaHash[5], src->rgbaHash[6], src->rgbaHash[7]);
	}
	else
	{
		snprintf(hash, sizeof(hash), "%s", (src->native.textureCount > 0u) ? "not decoded" : "none");
	}

	snprintf(line, size,
	         "[CTR Twin] %s: %u commands (%u colour only), %u records, %u triangles (%u one-sided, %u two-sided drawn twice, %u without a table), "
	         "%u untextured, %u textured (%u semi-transparent, STP texels under them %u), %u vertices x %u pose(s) (%s), %u material(s), "
	         "%u page(s) in %u texture(s), colours with code bits %u, half frames past the end %u, animated layouts %u (%u outside the table; "
	         "%u triangle(s) in %u material(s) keep the layout of the build), pages %s",
	         src->model, (unsigned)c->commands, (unsigned)c->colourOnly, (unsigned)c->records, (unsigned)src->native.triangleCount, (unsigned)c->oneSided,
	         (unsigned)c->twoSided, (unsigned)c->noTextureTable, (unsigned)c->untextured, (unsigned)c->textured, (unsigned)c->semi, (unsigned)stp,
	         (unsigned)src->native.vertexCount, (unsigned)((src->native.poseCount == 0u) ? 1u : src->native.poseCount), poses,
	         (unsigned)src->native.materialCount, (unsigned)src->tileCount, (unsigned)src->native.textureCount, (unsigned)c->colourCodeBits,
	         (unsigned)c->halfPastEnd, (unsigned)c->animatedLayouts, (unsigned)c->animatedOutside, (unsigned)c->animatedTriangles,
	         (unsigned)c->animatedMaterials, hash);
}

// --- The self-test -------------------------------------------------------------
//
// --native-twin-selftest <folder> (ctest native_twin_selftest), no window, no
// game data, no device.
//
// CASE 1, a retail model made in memory (NativeTwin_TestModel), 12 records,
// 15 commands, 8 triangle places, 5 texture layouts (4-bit, 8-bit, 15-bit
// semi-transparent on a page across the VRAM edge, a null entry, a second CLUT
// on the first page), two animations: raw (2 frames, x negative, y odd) and
// delta with half frames (0x8003: 3 logical frames from 2 stored ones), and a
// VRAM made in memory (NativeTwin_TestVramWord). Checked: the counts, the
// pose index, the vertex bytes of every pose and the RGBA8 against goldens
// computed apart (in Python, from the same tables), the winding against a
// second walk of the list kept with the GTE register FIFO (RTPT, RTPS, the
// SXY0 -> SXY1 copy of a fan) and the retail sign test under two integer
// projections with the identity matrix, NativeCharGpu_Build on the result.
// Two refusals: a cached slot read before any write, a cache colour read after
// a slot write overwrote it. Full height: the same model with fullHeight 1
// (odd heights kept in full frames, the half frame unchanged; golden of its
// vertex bytes). Animated textures: an animtex list over table entry 1 and a
// slot outside the table - counted, a material of its own, the same vertices;
// with header flags bit 1 nothing counted.
// CASE 2, old_plain.rldchar of <folder> (rldpack make-native-tests), relocated
// as the game does: 47 standard poses, every point of every pose equal to
// (pos + byte) x scale / 4096 of its record (height bit 0 cleared) and inside
// RldChar_FrameHulls, the winding as above, NativeCharGpu_Build.

internal void NativeTwin_Expect(int *checks, int *failures, int ok, const char *name, const char *what)
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		printf("native twin selftest FAILED: %s: %s\n", name, what);
	}
}

internal u16 NativeTwin_TestVramWord(u32 x, u32 y)
{
	if ((((x * 3u) + (y * 5u)) % 11u) == 0u)
	{
		return 0;
	}
	return (u16)(((x * 0x9E37u) ^ (y * 0x79B9u) ^ (x * y * 7u)) & 0xffffu);
}

#define NATIVE_TWIN_TEST_RECORDS 12u
#define NATIVE_TWIN_TEST_COLOURS 32u

// The commands of the made model: flags, slot, colour, texture - or a raw word
// (colour only) where flags is 0xFFFF.
internal const u16 s_ntTestCommands[][4] = {
    {0x90, 0, 0, 1},   // new strip, one-sided
    {0x10, 1, 1, 1},
    {0x10, 2, 2, 1},   // event 0, keyed by the first command
    {0x30, 3, 3, 2},   // event 1, bit 29, texture 2
    {0x18, 4, 30, 1},  // event 2, colour 30 from the cache
    {0xFFFF, 5, 6, 0}, // colour only: A = colour 5, B = colour 6 from the table
    {0x50, 5, 7, 3},   // fan: event 3 (records 2, 4, 5), texture 3 (semi-transparent)
    {0x34, 1, 0, 5},   // cached slot 1, bit 29: event 4, texture 5
    {0x80, 6, 1, 4},   // new strip, two-sided, texture 4 = null entry: POLY_G3
    {0x00, 7, 2, 0},
    {0x00, 8, 3, 0},   // event 5
    {0x04, 0, 4, 2},   // cached slot 0: event 6, two-sided, texture 2
    {0xB0, 2, 6, 0},   // new strip, one-sided, bit 29, slot 2 written again
    {0x10, 9, 5, 0},
    {0x10, 10, 7, 0},  // event 7
};

// The texture layouts: u0 v0 clut | u1 v1 tpage | u2 v2 u3 v3.
internal const u16 s_ntTestLayouts[4][8] = {
    {10, 20, 0x7814, 40, 22, 0x0262, 25, 60}, // 4-bit page x 128, CLUT (320, 480), opaque, dither bit
    {0, 0, 0x7D00, 255, 10, 0x00F8, 128, 255}, // 8-bit page (512, 256), CLUT (0, 500)
    {5, 5, 0x0000, 200, 30, 0x013F, 100, 250}, // 15-bit page (960, 256) across the VRAM edge, mode 1 (0x36)
    {0, 0, 0x7855, 15, 0, 0x0062, 0, 15},      // the 4-bit page again, CLUT (336, 481)
};

struct NativeTwinTestModel
{
	struct Model model;
	struct ModelHeader header;
	struct ModelAnim *anims[2];
	u8 *animBytes[2];
	struct TextureLayout layouts[4];
	struct TextureLayout *layoutTable[5];
	u32 commands[64];
	u32 colours[NATIVE_TWIN_TEST_COLOURS];
	u32 delta[NATIVE_TWIN_TEST_RECORDS];
	u32 animtex[16];
};

internal void NativeTwin_TestPutBits(u8 *stream, u32 *bitIndex, int value, int bits)
{
	int i;

	for (i = bits - 1; i >= 0; i--)
	{
		const u32 bit = ((u32)value >> (u32)i) & 1u;
		const u32 word = *bitIndex >> 5;
		const u32 inWord = 31u - (*bitIndex & 31u);
		u32 w = Rld_ReadLE32(&stream[word * 4u]);

		w |= bit << inWord;
		NativeTwin_Put32(&stream[word * 4u], w);
		(*bitIndex)++;
	}
}

internal void NativeTwin_TestDeltaBits(u32 k, int bits[3])
{
	if ((k == 0u) || (k == 6u))
	{
		bits[0] = 7;
		bits[1] = 7;
		bits[2] = 7;
		return;
	}
	bits[0] = (int)(k % 5u) + 1;
	bits[1] = (int)((k * 2u) % 6u) + 1;
	bits[2] = (int)((k * 3u) % 4u) + 2;
}

// variant 0: the model; 1: command 2 reuses slot 9 unwritten; 2: command 4
// reads colour 2 from the cache (slot 1 overwrote words 2 and 3); 3: an animtex
// list cycling table entry 1 (texture 2) and one slot outside the table; 4: the
// same with header flags bit 1 (CTR_CycleTex_AllModels leaves it alone).
internal int NativeTwin_TestBuild(struct NativeTwinTestModel *m, int variant)
{
	static const s16 rawPos[2][3] = {{-100, -51, 20}, {-90, -40, 30}};
	static const s16 deltaPos[2][3] = {{10, 4, -20}, {14, 8, -30}};
	const u32 rawFrameSize = (u32)sizeof(struct ModelFrame) + (NATIVE_TWIN_TEST_RECORDS * 3u);
	u32 deltaWords = 0;
	u32 deltaFrameSize;
	u32 i;
	u32 k;
	int f;

	memset(m, 0, sizeof(*m));
	memcpy(m->model.name, "twin-test", 10);
	m->model.id = 0;
	m->model.numHeaders = 1;
	m->model.headers = &m->header;
	memcpy(m->header.name, "twin-test_hi", 13);
	m->header.scale.x = 4096;
	m->header.scale.y = 3072;
	m->header.scale.z = 3277;

	for (i = 0; i < NATIVE_TWIN_TEST_COLOURS; i++)
	{
		m->colours[i] = ((i * 37u + 11u) & 0xffu) | (((i * 91u + 7u) & 0xffu) << 8) | (((i * 53u + 200u) & 0xffu) << 16) | ((i == 6u) ? 0x02000000u : 0u);
	}

	m->commands[0] = NATIVE_TWIN_TEST_COLOURS;
	for (i = 0; i < (u32)(sizeof(s_ntTestCommands) / sizeof(s_ntTestCommands[0])); i++)
	{
		const u16 *c = s_ntTestCommands[i];

		m->commands[1u + i] = (c[0] == 0xFFFFu) ? (((u32)c[1] << 9) | ((u32)c[2] << 2)) : (((u32)c[0] << 24) | ((u32)c[1] << 16) | ((u32)c[2] << 9) | c[3]);
	}
	m->commands[1u + i] = 0xffffffffu;
	if (variant == 1)
	{
		m->commands[2] = (0x14u << 24) | (9u << 16) | (1u << 9) | 1u;
	}
	else if (variant == 2)
	{
		m->commands[4] = (0x38u << 24) | (3u << 16) | (2u << 9) | 2u;
	}

	for (i = 0; i < 4u; i++)
	{
		const u16 *l = s_ntTestLayouts[i];

		m->layouts[i].u0 = (u8)l[0];
		m->layouts[i].v0 = (u8)l[1];
		m->layouts[i].clut = l[2];
		m->layouts[i].u1 = (u8)l[3];
		m->layouts[i].v1 = (u8)l[4];
		m->layouts[i].tpage = l[5];
		m->layouts[i].u2 = (u8)l[6];
		m->layouts[i].v2 = (u8)l[7];
	}
	m->layoutTable[0] = &m->layouts[0];
	m->layoutTable[1] = &m->layouts[1];
	m->layoutTable[2] = &m->layouts[2];
	m->layoutTable[3] = NULL;
	m->layoutTable[4] = &m->layouts[3];

	// The delta temporal words and the stream length.
	for (k = 0; k < NATIVE_TWIN_TEST_RECORDS; k++)
	{
		int bits[3];
		const int fx = (int)((k * 11u) % 32u) - 16;
		const int bz = (int)((k * 13u) % 40u) - 20;
		const int by = (int)((k * 17u) % 50u) - 25;

		NativeTwin_TestDeltaBits(k, bits);
		m->delta[k] = (((u32)fx & 0x7fu) << 25) | (((u32)bz & 0xffu) << 17) | (((u32)by & 0xffu) << 9) | ((u32)bits[0] << 6) | ((u32)bits[1] << 3) |
		              (u32)bits[2];
		deltaWords += (u32)(bits[0] + bits[1] + bits[2] + 3);
	}
	deltaWords = (deltaWords + 31u) / 32u;
	deltaFrameSize = (u32)sizeof(struct ModelFrame) + ((deltaWords + 1u) * 4u);

	// Animation 0: raw, 2 frames.
	m->animBytes[0] = (u8 *)calloc(1, sizeof(struct ModelAnim) + (2u * rawFrameSize) + 4u);
	m->animBytes[1] = (u8 *)calloc(1, sizeof(struct ModelAnim) + (2u * deltaFrameSize) + 4u);
	if ((m->animBytes[0] == NULL) || (m->animBytes[1] == NULL))
	{
		return 0;
	}
	m->anims[0] = (struct ModelAnim *)m->animBytes[0];
	memcpy(m->anims[0]->name, "raw", 4);
	m->anims[0]->numFrames = 2;
	m->anims[0]->frameSize = (s16)rawFrameSize;
	m->anims[0]->ptrDeltaArray = NULL;
	for (f = 0; f < 2; f++)
	{
		struct ModelFrame *mf = (struct ModelFrame *)(m->animBytes[0] + sizeof(struct ModelAnim) + ((u32)f * rawFrameSize));
		u8 *bytes = (u8 *)mf + sizeof(struct ModelFrame);

		mf->pos.x = rawPos[f][0];
		mf->pos.y = rawPos[f][1];
		mf->pos.z = rawPos[f][2];
		mf->vertexOffset = (int)sizeof(struct ModelFrame);
		for (k = 0; k < NATIVE_TWIN_TEST_RECORDS; k++)
		{
			int j;

			for (j = 0; j < 3; j++)
			{
				bytes[(k * 3u) + (u32)j] = (u8)(((k * 29u) + ((u32)j * 71u) + ((u32)f * 13u) + 5u) & 0xffu);
			}
		}
	}

	// Animation 1: delta, half frames, 3 logical frames from 2 stored ones.
	m->anims[1] = (struct ModelAnim *)m->animBytes[1];
	memcpy(m->anims[1]->name, "delta", 6);
	m->anims[1]->numFrames = 0x8003;
	m->anims[1]->frameSize = (s16)deltaFrameSize;
	m->anims[1]->ptrDeltaArray = m->delta;
	for (f = 0; f < 2; f++)
	{
		struct ModelFrame *mf = (struct ModelFrame *)(m->animBytes[1] + sizeof(struct ModelAnim) + ((u32)f * deltaFrameSize));
		u8 *stream = (u8 *)mf + sizeof(struct ModelFrame);
		u32 bitIndex = 0;

		mf->pos.x = deltaPos[f][0];
		mf->pos.y = deltaPos[f][1];
		mf->pos.z = deltaPos[f][2];
		mf->vertexOffset = (int)sizeof(struct ModelFrame);
		for (k = 0; k < NATIVE_TWIN_TEST_RECORDS; k++)
		{
			int bits[3];
			int comp;

			NativeTwin_TestDeltaBits(k, bits);
			for (comp = 0; comp < 3; comp++)
			{
				const int width = bits[comp] + 1;
				const int value = (int)((((u32)f * 7u) + (k * 5u) + ((u32)comp * 3u) + 1u) % (1u << width)) - (1 << bits[comp]);

				NativeTwin_TestPutBits(stream, &bitIndex, value, width);
			}
		}
	}

	m->header.ptrCommandList = (u32)(uintptr_t)m->commands;
	m->header.ptrFrameData = NULL;
	m->header.ptrTexLayout = m->layoutTable;
	m->header.ptrColors = m->colours;
	m->header.numAnimations = 2;
	m->header.ptrAnimations = m->anims;

	if (variant >= 3)
	{
		u32 *w = m->animtex;

		w[0] = (u32)(uintptr_t)&m->layoutTable[1]; // ptrActiveTex: the entry of texture 2
		w[1] = 2u;                                  // numFrames 2, frameOffset 0
		w[2] = 0u;                                  // frameSkip 0, frameCurr 0
		w[3] = (u32)(uintptr_t)&m->layouts[1];
		w[4] = (u32)(uintptr_t)&m->layouts[3];
		w[5] = (u32)(uintptr_t)&m->colours[0];      // a slot outside the table
		w[6] = 1u;
		w[7] = 0u;
		w[8] = (u32)(uintptr_t)&m->layouts[0];
		w[9] = (u32)(uintptr_t)&m->animtex[0];      // the end: a word equal to the first AnimTex
		m->header.animtex = (struct AnimTex *)(void *)m->animtex;
		m->header.flags = (variant == 4) ? 2u : 0u;
	}
	return 1;
}

internal void NativeTwin_TestFree(struct NativeTwinTestModel *m)
{
	free(m->animBytes[0]);
	free(m->animBytes[1]);
}

// THE SECOND WALK: the list again, kept the way the GTE keeps it - RTPT loads
// SXY0..2 from the last three vertices, RTPS shifts the FIFO and, for a fan,
// first copies SXY0 into SXY1 (RB:2863-2880) - with an integer projection of
// the identity matrix: proj 0 (x, y), proj 1 (x, z). The retail sign test
// (RB:2909-2932, instance flags 0) decides each place. Then every twin triangle
// of that place must face the way the native cull reads it: NCLIP < 0 kept.
// Returns the mismatches; *drawn counts the places retail keeps.
internal u32 NativeTwin_TestWinding(const struct ModelHeader *mh, const struct NativeTwinSource *src, int (*g)[3], int proj, u32 *drawn, u32 *front)
{
	const u32 *list = (const u32 *)(uintptr_t)mh->ptrCommandList;
	int slot[NATIVE_TWIN_SLOTS][3];
	int tp[4][3];
	s64 sxy[3][2];
	u8 *keep = (u8 *)calloc(src->count.events + 1u, 1u);
	s64 *nclip = (s64 *)calloc(src->count.events + 1u, sizeof(s64));
	u32 strip = 0;
	u32 prim = 0;
	u32 record = 0;
	u32 event = 0;
	u32 bad = 0;
	u32 i;
	u32 t;

	*drawn = 0;
	*front = 0;
	if ((keep == NULL) || (nclip == NULL))
	{
		free(keep);
		free(nclip);
		return 1;
	}
	memset(slot, 0, sizeof(slot));
	memset(tp, 0, sizeof(tp));
	memset(sxy, 0, sizeof(sxy));

	for (i = 1; list[i] != 0xffffffffu; i++)
	{
		const u32 command = list[i];
		const u32 flags = command >> 24;
		const u32 s = (command >> 16) & 0xffu;
		u32 drawCommand;
		int useRtps;
		int reuse;
		int c;

		if ((command >> 16) == 0u)
		{
			continue;
		}
		if ((flags & NATIVE_TWIN_CMD_CACHED) == 0u)
		{
			memcpy(slot[s], g[record++], sizeof(slot[s]));
		}
		memcpy(tp[0], tp[1], sizeof(tp[0]));
		memcpy(tp[1], tp[2], sizeof(tp[0]));
		memcpy(tp[2], tp[3], sizeof(tp[0]));
		memcpy(tp[3], slot[s], sizeof(tp[0]));
		if (((flags & NATIVE_TWIN_CMD_NEW_STRIP) != 0u) || (strip == 0u))
		{
			prim = command;
		}
		if ((flags & NATIVE_TWIN_CMD_NEW_STRIP) != 0u)
		{
			strip = 0;
		}
		useRtps = (strip > 2u);
		reuse = useRtps && ((flags & NATIVE_TWIN_CMD_FAN) != 0u);
		if (strip < 2u)
		{
			strip++;
			continue;
		}
		drawCommand = (strip == 2u) ? prim : command;

		if (!useRtps)
		{
			for (c = 0; c < 3; c++)
			{
				sxy[c][0] = tp[1 + c][0];
				sxy[c][1] = tp[1 + c][(proj == 0) ? 1 : 2];
			}
		}
		else
		{
			if (reuse)
			{
				sxy[1][0] = sxy[0][0];
				sxy[1][1] = sxy[0][1];
			}
			sxy[0][0] = sxy[1][0];
			sxy[0][1] = sxy[1][1];
			sxy[1][0] = sxy[2][0];
			sxy[1][1] = sxy[2][1];
			sxy[2][0] = tp[3][0];
			sxy[2][1] = tp[3][(proj == 0) ? 1 : 2];
		}

		nclip[event] = (sxy[0][0] * sxy[1][1]) + (sxy[1][0] * sxy[2][1]) + (sxy[2][0] * sxy[0][1]) - (sxy[0][0] * sxy[2][1]) - (sxy[1][0] * sxy[0][1]) -
		               (sxy[2][0] * sxy[1][1]);
		if ((drawCommand & NATIVE_TWIN_CMD_CULL) != 0u)
		{
			const s32 opZ = (s32)nclip[event];
			const s32 cullXor = (s32)(s16)0 ^ (s32)(drawCommand << 2);

			keep[event] = (opZ != 0) && ((s32)((u32)opZ ^ (u32)cullXor) > 0);
		}
		else
		{
			keep[event] = 1;
		}
		event++;
		strip++;
	}

	bad += (event != src->count.events) ? 1u : 0u;
	for (t = 0; t < src->native.triangleCount; t++)
	{
		const u32 e = src->triangleEvent[t];
		s64 q[3][2];
		s64 n;
		int c;

		for (c = 0; c < 3; c++)
		{
			const int *v = g[src->cornerVertex[(t * 3u) + (u32)c]];

			q[c][0] = v[0];
			q[c][1] = v[(proj == 0) ? 1 : 2];
		}
		n = ((q[1][0] - q[0][0]) * (q[2][1] - q[0][1])) - ((q[2][0] - q[0][0]) * (q[1][1] - q[0][1]));
		if (n < 0)
		{
			(*front)++;
		}
		if ((e >= event) || (nclip[e] == 0))
		{
			continue;
		}
		if (src->triangleSide[t] == 0u)
		{
			// One-sided: kept by retail exactly when the twin order faces front.
			bad += (keep[e] != (n < 0)) ? 1u : 0u;
			*drawn += keep[e];
		}
		else
		{
			// Two-sided: the two copies face opposite ways; retail always keeps it.
			bad += (keep[e] != 1u) ? 1u : 0u;
			bad += ((n == 0) || ((n < 0) != (src->triangleSide[t] == ((nclip[e] < 0) ? 1u : 2u)))) ? 1u : 0u;
			*drawn += (src->triangleSide[t] == 1u) ? 1u : 0u;
		}
	}

	free(keep);
	free(nclip);
	return bad;
}

// The GTE input of every pose of src (for the winding and the point checks).
internal int NativeTwin_TestPoseInput(const struct ModelHeader *mh, const struct NativeTwinSource *src, u32 pose, int (*g)[3], u8 (*curr)[3], u8 (*next)[3])
{
	struct NativeTwinPose *poses = (struct NativeTwinPose *)calloc(NATIVE_TWIN_POSES_MAX, sizeof(struct NativeTwinPose));
	struct NativeTwinSource scratch;
	u32 count = 0;
	char why[96];
	int ok;

	if (poses == NULL)
	{
		return 0;
	}
	memset(&scratch, 0, sizeof(scratch));
	ok = NativeTwin_Poses(mh, &scratch, poses, &count, why, sizeof(why)) && (pose < count);
	if (ok)
	{
		NativeTwin_PoseInput(&poses[pose], src->streamVertices, src->fullHeight, curr, next, g);
	}
	free(poses);
	return ok;
}

// The paint order against the GPU set cpu built from src: with one depth for
// every corner all triangles share a bin, so the order is the command list
// backwards and every place the one NativeCharGpu_Build gave the triangle;
// with w = z of the model (far, then near with its scale of 4) every bin is
// the one worked out apart from the pose bytes, and no bin follows a nearer
// one.
internal void NativeTwin_TestPaint(const struct NativeTwinSource *src, const struct NativeCharGpuCpu *cpu, const char *name, int *checks, int *failures)
{
	const u32 T = src->native.triangleCount;
	const u32 N = src->native.vertexCount;
	const u32 pose = (src->native.poseCount > 1u) ? (src->native.poseCount - 1u) : 0u;
	u64 *keys = (u64 *)calloc((size_t)T + 1u, sizeof(u64));
	float s[16];
	u32 i;
	int round;
	int flat = 1;
	int sorted = 1;
	int bins = 1;

	if (keys == NULL)
	{
		NativeTwin_Expect(checks, failures, 0, name, "memory");
		return;
	}

	// MAC0 >> 17: 0x555 x 96 = 131040 is bin 0, 0x555 x 99 bin 1; SZ is
	// limited to 0..0xffff before the sum (0x555 x 0xffff >> 17 = 682).
	NativeTwin_Expect(checks, failures,
	                  (NativeTwin_PaintBin(32, 32, 32) == 0u) && (NativeTwin_PaintBin(33, 33, 33) == 1u) && (NativeTwin_PaintBin(-1, 0x10000, 0) == 682u), name,
	                  "the paint bin is not ZSF3 x (SZ1 + SZ2 + SZ3) >> 17");

	memset(s, 0, sizeof(s));
	s[15] = 5000.0f;
	// Refused: too few keys, a pose past the last one.
	flat = (NativeTwin_PaintOrder(src, pose, s, keys, T - 1u) == 0u) && (NativeTwin_PaintOrder(src, pose + 1u, s, keys, T) == 0u) &&
	       (NativeTwin_PaintOrder(src, pose, s, keys, T) == T);
	for (i = 0; flat && (i < T); i++)
	{
		const u32 t = (u32)((keys[i] >> 20) & NATIVE_TWIN_PAINT_PLACE_MASK);
		const u32 place = NATIVE_TWIN_PAINT_PLACE(keys[i]);

		flat = (t == (T - 1u - i)) && ((keys[i] >> 40) == (u64)NativeTwin_PaintBin(5000, 5000, 5000)) && (place < T) && (cpu->triangleOrder[place] == t);
	}
	NativeTwin_Expect(checks, failures, flat, name, "one bin: the paint order is not the command list backwards on the places of the GPU set");

	for (round = 0; round < 2; round++)
	{
		const double scale = (round == 0) ? 1.0 : 4.0;

		memset(s, 0, sizeof(s));
		s[11] = 1.0f;
		s[15] = (round == 0) ? 4096.0f : 4000.0f;
		if (NativeTwin_PaintOrder(src, pose, s, keys, T) != T)
		{
			bins = 0;
			break;
		}
		for (i = 0; i < T; i++)
		{
			const u32 t = (u32)((keys[i] >> 20) & NATIVE_TWIN_PAINT_PLACE_MASK);
			int sz[3];
			int c;

			for (c = 0; c < 3; c++)
			{
				const u8 *v = (const u8 *)&src->native.poses[(((size_t)pose * N) + ((size_t)t * 3u) + (u32)c) * RLDCHAR_NET_VERTEX_BYTES];
				const u32 bits = Rld_ReadLE32(&v[8]);
				float z;

				memcpy(&z, &bits, sizeof(z));
				sz[c] = (int)floor(((double)z + (double)s[15]) * scale);
			}
			bins &= ((keys[i] >> 40) == (u64)NativeTwin_PaintBin(sz[0], sz[1], sz[2]));
			sorted &= (i == 0u) || ((keys[i] >> 40) <= (keys[i - 1u] >> 40));
		}
	}
	NativeTwin_Expect(checks, failures, bins && sorted, name, "depth: a paint bin differs from MAC0 >> 17 of the corners, or a far bin follows a nearer one");
	free(keys);
}

// The checks every case runs on a converted model: the GPU set takes it, the
// winding holds in every pose under both projections. Returns 1 when the build
// was accepted.
internal void NativeTwin_TestCommon(const struct ModelHeader *mh, const struct NativeTwinSource *src, const char *name, int *checks, int *failures)
{
	struct NativeCharGpuCpu cpu;
	const u32 poses = (src->native.poseCount == 0u) ? 1u : src->native.poseCount;
	int (*g)[3] = (int(*)[3])calloc(src->streamVertices, sizeof(int[3]));
	u8 (*curr)[3] = (u8(*)[3])calloc(src->streamVertices, 3u);
	u8 (*next)[3] = (u8(*)[3])calloc(src->streamVertices, 3u);
	u32 bad = 0;
	u32 drawn[2] = {0, 0};
	u32 front[2] = {0, 0};
	u32 p;
	char what[192];
	int proj;

	memset(&cpu, 0, sizeof(cpu));
	if ((g == NULL) || (curr == NULL) || (next == NULL))
	{
		NativeTwin_Expect(checks, failures, 0, name, "memory");
		goto done;
	}

	// The same GPU set as step 4c.
	if (!NativeCharGpu_Build(&src->native, &cpu))
	{
		snprintf(what, sizeof(what), "NativeCharGpu_Build refused the twin: %s", cpu.why);
		NativeTwin_Expect(checks, failures, 0, name, what);
	}
	else
	{
		u32 r;
		u32 sum = 0;
		int masks = 1;

		for (r = 0; r < cpu.rangeCount; r++)
		{
			sum += cpu.range[r].indexCount;
			masks &= (cpu.range[r].mask == src->material[cpu.range[r].material].textured);
		}
		NativeTwin_Expect(checks, failures, (sum == src->native.triangleCount * 3u) && (cpu.poseCount == poses) && (cpu.vertexCount == src->native.vertexCount),
		                  name, "the GPU set does not hold every triangle, pose and vertex");
		NativeTwin_Expect(checks, failures, masks && !cpu.hasWheels, name, "a textured range is not masked, an untextured one is, or wheels appeared");
		NativeTwin_TestPaint(src, &cpu, name, checks, failures);
	}

	for (p = 0; p < poses; p++)
	{
		if (!NativeTwin_TestPoseInput(mh, src, p, g, curr, next))
		{
			bad++;
			continue;
		}
		for (proj = 0; proj < 2; proj++)
		{
			u32 d = 0;
			u32 f = 0;

			bad += NativeTwin_TestWinding(mh, src, g, proj, &d, &f);
			drawn[proj] += d;
			front[proj] += f;
		}
	}
	snprintf(what, sizeof(what), "the winding differs from the retail sign test in %u place(s)", (unsigned)bad);
	NativeTwin_Expect(checks, failures, bad == 0u, name, what);
	NativeTwin_Expect(checks, failures, (drawn[0] == front[0]) && (drawn[1] == front[1]), name,
	                  "the front twin triangles are not the triangles retail keeps");
	printf("native twin selftest: %s: winding held over %u pose(s): projection xy %u of %u kept, xz %u kept\n", name, (unsigned)poses, (unsigned)drawn[0],
	       (unsigned)(poses * src->native.triangleCount), (unsigned)drawn[1]);

done:
	NativeCharGpu_FreeCpu(&cpu);
	free(g);
	free(curr);
	free(next);
}

internal void NativeTwin_HashVertices(const struct NativeTwinSource *src, struct Sha256 *sha)
{
	const u32 poses = (src->native.poseCount == 0u) ? 1u : src->native.poseCount;
	struct NativeProbeVertex *v = (struct NativeProbeVertex *)malloc((size_t)src->native.vertexCount * sizeof(struct NativeProbeVertex));
	u32 p;

	if (v == NULL)
	{
		return;
	}
	for (p = 0; p < poses; p++)
	{
		u32 i;

		NativeCharGpu_PoseVertices(&src->native, p, 0, src->native.vertexCount, v);
		for (i = 0; i < src->native.vertexCount; i++)
		{
			u8 bytes[24];
			int c;

			for (c = 0; c < 3; c++)
			{
				NativeTwin_PutF32(&bytes[c * 4], v[i].position[c]);
			}
			NativeTwin_PutF32(&bytes[12], v[i].texcoord[0]);
			NativeTwin_PutF32(&bytes[16], v[i].texcoord[1]);
			memcpy(&bytes[20], v[i].color, 4u);
			Sha256_Update(sha, bytes, sizeof(bytes));
		}
	}
	free(v);
}

internal void NativeTwin_Hex(const u8 digest[32], char out[65])
{
	int i;

	for (i = 0; i < 32; i++)
	{
		snprintf(&out[i * 2], 3, "%02x", digest[i]);
	}
}

internal void NativeTwin_TestMade(int *checks, int *failures, char vertexHex[65], char rgbaHex[65], char fullHex[65])
{
	struct NativeTwinTestModel *m = (struct NativeTwinTestModel *)calloc(1, sizeof(struct NativeTwinTestModel));
	u16 *vram = (u16 *)malloc((size_t)VRAM_WIDTH * VRAM_HEIGHT * sizeof(u16));
	struct NativeTwinSource src;
	struct Sha256 sha;
	u8 digest[32];
	char why[192];
	char line[768];
	u32 x;
	u32 y;
	int variant;
	const char *name = "made model";

	memset(&src, 0, sizeof(src));
	vertexHex[0] = '\0';
	rgbaHex[0] = '\0';
	fullHex[0] = '\0';
	if ((m == NULL) || (vram == NULL) || !NativeTwin_TestBuild(m, 0))
	{
		NativeTwin_Expect(checks, failures, 0, name, "memory");
		goto done;
	}
	for (y = 0; y < VRAM_HEIGHT; y++)
	{
		for (x = 0; x < VRAM_WIDTH; x++)
		{
			vram[(y * VRAM_WIDTH) + x] = NativeTwin_TestVramWord(x, y);
		}
	}

	if (!NativeTwin_FromModel(&m->model, 0, &src, why, sizeof(why)))
	{
		NativeTwin_Expect(checks, failures, 0, name, why);
		goto done;
	}
	NativeTwin_Expect(checks, failures, src.native.state == RLDCHAR_NATIVE_NONE, name, "ready before the textures are decoded");
	NativeTwin_Expect(checks, failures, NativeTwin_DecodeVram(vram, &src) && (src.native.state == RLDCHAR_NATIVE_READY), name, "the VRAM was not decoded");
	NativeTwin_Describe(&src, line, sizeof(line));
	printf("native twin selftest: %s\n", line);

	{
		const struct NativeTwinCount *c = &src.count;

		NativeTwin_Expect(checks, failures,
		                  (c->commands == 15u) && (c->colourOnly == 1u) && (c->records == NATIVE_TWIN_TEST_RECORDS) && (c->cachedVertices == 2u) && (c->strips == 3u) &&
		                      (c->events == 8u) && (c->oneSided == 6u) && (c->twoSided == 2u) && (c->noTextureTable == 0u) && (c->untextured == 2u) &&
		                      (c->textured == 6u) && (c->semi == 1u) && (c->cacheColours == 1u) && (c->colourCodeBits == 2u) && (c->halfPastEnd == 0u),
		                  name, "the counts of the list are not 15 commands, 12 records, 8 places (6 one-sided, 2 two-sided)");
		NativeTwin_Expect(checks, failures,
		                  (src.native.triangleCount == 10u) && (src.native.vertexCount == 30u) && (src.native.poseCount == 5u) &&
		                      (src.native.materialCount == 4u) && (src.tileCount == 4u) && (src.native.textureCount == 1u) &&
		                      (src.native.texture[0].width == 1024u) && (src.native.texture[0].height == 256u),
		                  name, "not 10 triangles, 30 vertices, 5 poses, 4 materials, 4 pages in one 1024 x 256 texture");
		NativeTwin_Expect(checks, failures,
		                  (src.material[0].textured == 1u) && (src.material[0].dither == 1u) && (src.material[1].dither == 0u) &&
		                      (src.material[2].semi == (NATIVE_TWIN_SEMI_MODE0 + 1u)) && (src.material[3].textured == 0u) &&
		                      (src.material[3].modulation == 1u) && (src.material[0].modulation == 2u),
		                  name, "the material looks (texture, page mode, dither, modulation) are wrong");
		NativeTwin_Expect(checks, failures,
		                  (src.tile[0].depth == 0u) && (src.tile[0].pageX == 128u) && (src.tile[0].clutX == 320u) && (src.tile[0].clutY == 480u) &&
		                      (src.tile[1].depth == 1u) && (src.tile[1].pageY == 256u) && (src.tile[2].depth == 2u) && (src.tile[2].pageX == 960u) &&
		                      (src.tile[3].clutX == 336u) && (src.tile[3].cellX == 3u),
		                  name, "the page, depth or CLUT of a tile is wrong");
		NativeTwin_Expect(checks, failures, (NativeTwin_PoseIndex(&src, 0, 1) == 1) && (NativeTwin_PoseIndex(&src, 0, 5) == 1) &&
		                                        (NativeTwin_PoseIndex(&src, 1, 0) == 2) && (NativeTwin_PoseIndex(&src, 1, 2) == 4) &&
		                                        (NativeTwin_PoseIndex(&src, 1, 3) == -1) && (NativeTwin_PoseIndex(&src, 1, 4) == 4) &&
		                                        (NativeTwin_PoseIndex(&src, 2, 0) == -1) && (NativeTwin_PoseIndex(&src, 0, -1) == -1) && !src.standardPoses,
		                  name, "the pose index is wrong");
		NativeTwin_Expect(checks, failures, (NativeTwin_TextureFlags() & NATIVE_TEX_FLAG_ONE_LEVEL) != 0u, name, "the texture flags miss one level");
	}

	NativeTwin_TestCommon(&m->header, &src, name, checks, failures);

	Sha256_Init(&sha);
	NativeTwin_HashVertices(&src, &sha);
	Sha256_Final(&sha, digest);
	NativeTwin_Hex(digest, vertexHex);
	NativeTwin_Hex(src.rgbaHash, rgbaHex);
	printf("native twin selftest: %s: stp texels %u, clear texels %u %u %u %u, hull %.4f %.4f %.4f to %.4f %.4f %.4f\n", name, (unsigned)src.tile[2].stpTexels,
	       (unsigned)src.tile[0].clearTexels, (unsigned)src.tile[1].clearTexels, (unsigned)src.tile[2].clearTexels, (unsigned)src.tile[3].clearTexels,
	       src.native.hullMin[0], src.native.hullMin[1], src.native.hullMin[2], src.native.hullMax[0], src.native.hullMax[1], src.native.hullMax[2]);

	// FULL HEIGHT (a custom CMDL that keeps odd heights, Z2): the same model
	// with fullHeight 1. A full frame keeps bit 0 of pos.y + byte (mask
	// 0xfffcffff); the half frame keeps the retail mask either way (RB:2498).
	{
		struct NativeTwinSource full;
		u32 odd = 0;
		u32 off = 0;
		u32 i;
		int halfSame;

		memset(&full, 0, sizeof(full));
		if (!NativeTwin_FromModel(&m->model, 1, &full, why, sizeof(why)))
		{
			NativeTwin_Expect(checks, failures, 0, name, why);
		}
		else
		{
			const size_t poseBytes = (size_t)full.native.vertexCount * RLDCHAR_NET_VERTEX_BYTES;

			for (i = 0; i < full.native.vertexCount; i++)
			{
				const u32 k = full.cornerVertex[i];
				const int up = -51 + (int)(((k * 29u) + (2u * 71u) + 5u) & 0xffu); // raw frame 0: pos.y -51, byte z
				const double expect = ((double)up * 3072.0) / 4096.0;
				const double have = (double)RldChar_F32(&full.native.poses[((size_t)i * RLDCHAR_NET_VERTEX_BYTES) + 4u]);
				const double retail = (double)RldChar_F32(&src.native.poses[((size_t)i * RLDCHAR_NET_VERTEX_BYTES) + 4u]);

				odd += ((up & 1) != 0) ? 1u : 0u;
				off += (have != expect) ? 1u : 0u;
				off += (retail != (((double)(up & ~1) * 3072.0) / 4096.0)) ? 1u : 0u;
			}
			halfSame = (memcmp(&full.native.poses[3u * poseBytes], &src.native.poses[3u * poseBytes], poseBytes) == 0);
			snprintf(why, sizeof(why), "full height: %u height(s) of pose 0 differ from pos.y + byte (%u odd), the half frame %s", (unsigned)off, (unsigned)odd,
			         halfSame ? "kept" : "changed");
			NativeTwin_Expect(checks, failures, (off == 0u) && (odd > 0u) && halfSame, name, why);

			Sha256_Init(&sha);
			NativeTwin_HashVertices(&full, &sha);
			Sha256_Final(&sha, digest);
			NativeTwin_Hex(digest, fullHex);
			printf("native twin selftest: %s: full height held, %u odd height(s) in pose 0 kept, the half frame unchanged\n", name, (unsigned)odd);
		}
		NativeTwin_Free(&full);
	}
	NativeTwin_Free(&src);

	// ANIMATED TEXTURES: the cycled entry is counted and kept in a material of
	// its own; the vertices stay the same. With flags bit 1 nothing is cycled.
	for (variant = 3; variant <= 4; variant++)
	{
		char hex[65];
		int ok;

		NativeTwin_TestFree(m);
		if (!NativeTwin_TestBuild(m, variant) || !NativeTwin_FromModel(&m->model, 0, &src, why, sizeof(why)))
		{
			NativeTwin_Expect(checks, failures, 0, name, "the animtex case was refused");
			NativeTwin_Free(&src);
			continue;
		}
		Sha256_Init(&sha);
		NativeTwin_HashVertices(&src, &sha);
		Sha256_Final(&sha, digest);
		NativeTwin_Hex(digest, hex);
		if (variant == 3)
		{
			ok = (src.count.animatedLayouts == 1u) && (src.count.animatedOutside == 1u) && (src.count.animatedTriangles == 3u) &&
			     (src.count.animatedMaterials == 1u) && (src.native.materialCount == 5u) && (src.material[1].animated == 1u) &&
			     (src.material[0].animated == 0u) && (src.material[3].animated == 0u);
		}
		else
		{
			ok = (src.count.animatedLayouts == 0u) && (src.count.animatedOutside == 0u) && (src.count.animatedTriangles == 0u) &&
			     (src.native.materialCount == 4u);
		}
		NativeTwin_Expect(checks, failures, ok && (strcmp(hex, vertexHex) == 0), name,
		                  (variant == 3) ? "animtex: not 1 cycled entry (1 outside), 3 triangles in 1 material of its own, same vertices"
		                                 : "animtex with flags bit 1: something counted as cycled");
		NativeTwin_Describe(&src, line, sizeof(line));
		printf("native twin selftest: %s variant %d: %s\n", name, variant, line);
		NativeTwin_Free(&src);
	}

	// The refusals.
	for (variant = 1; variant <= 2; variant++)
	{
		static const char *expect[3] = {"", "reuses cache slot 9 before any command wrote it", "reads colour 2 from the cache after slot 1 overwrote it"};
		int refused;

		NativeTwin_TestFree(m);
		if (!NativeTwin_TestBuild(m, variant))
		{
			NativeTwin_Expect(checks, failures, 0, name, "memory");
			goto done;
		}
		why[0] = '\0';
		refused = !NativeTwin_FromModel(&m->model, 0, &src, why, sizeof(why));
		NativeTwin_Expect(checks, failures, refused && (strstr(why, expect[variant]) != NULL) && (src.block == NULL), name, expect[variant]);
		printf("native twin selftest: %s variant %d refused: %s\n", name, variant, why);
		NativeTwin_Free(&src);
	}

done:
	if (m != NULL)
	{
		NativeTwin_TestFree(m);
	}
	free(m);
	free(vram);
}

// Case 2: old_plain.rldchar, read and relocated the way the game does
// (NativeChar_ReadFile): envelope, CHRI, CMDL, RldChar_CheckModel, the pointer
// map through LOAD_RunPtrMap.
internal void NativeTwin_TestFile(const char *dir, int *checks, int *failures, int *files)
{
	const char *name = "old_plain";
	char path[1024];
	char why[192];
	char line[768];
	struct RldReader reader;
	struct RldCharInfo info;
	struct RldCharFinding finding;
	struct RldCharModelFacts facts;
	struct RldCharHull hulls[RLDCHAR_NET_POSES];
	struct NativeTwinSource src;
	const char *error;
	u8 *infoBytes = NULL;
	u8 *cmdl = NULL;
	size_t infoSize = 0;
	size_t cmdlSize = 0;
	int infoIndex = -1;
	int modelIndex = -1;
	u32 bodyBytes;
	u32 mapBytes;
	const struct Model *model;
	int fullHeight;

	memset(&src, 0, sizeof(src));
	snprintf(path, sizeof(path), "%s/%s.rldchar", dir, name);
	error = Rld_OpenAs(&reader, path, &s_rldCharFormat);
	if (error != NULL)
	{
		snprintf(why, sizeof(why), "cannot open %s: %s", path, error);
		NativeTwin_Expect(checks, failures, 0, name, why);
		return;
	}
	Rld_FindEntry(&reader, "CHRI", &infoIndex);
	Rld_FindEntry(&reader, "CMDL", &modelIndex);
	if ((infoIndex >= 0) && (modelIndex >= 0))
	{
		infoBytes = Rld_ReadChunk(&reader, infoIndex, &infoSize, &error);
		cmdl = Rld_ReadChunk(&reader, modelIndex, &cmdlSize, &error);
	}
	Rld_Close(&reader);
	if ((infoBytes == NULL) || (cmdl == NULL) || (RldChar_ParseInfo(&info, infoBytes, infoSize) != NULL) ||
	    (RldChar_CheckModel(cmdl, cmdlSize, &finding, NULL, NULL, &facts) != RLDCHAR_VERDICT_OK))
	{
		NativeTwin_Expect(checks, failures, 0, name, "CHRI or CMDL is missing or not OK");
		goto done;
	}
	fullHeight = ((info.flags & RLDCHAR_FLAG_FULL_HEIGHT) != 0u) ? 1 : 0;

	bodyBytes = Rld_ReadLE32(&cmdl[0]);
	mapBytes = Rld_ReadLE32(&cmdl[4u + bodyBytes]);
	if (LOAD_RunPtrMap((char *)&cmdl[4], (int)bodyBytes, (int *)&cmdl[8u + bodyBytes], (int)(mapBytes / 4u)) == 0)
	{
		NativeTwin_Expect(checks, failures, 0, name, "LOAD_RunPtrMap refused the pointer map");
		goto done;
	}
	(*files)++;
	model = (const struct Model *)&cmdl[4];

	if (!NativeTwin_FromModel(model, fullHeight, &src, why, sizeof(why)))
	{
		NativeTwin_Expect(checks, failures, 0, name, why);
		goto done;
	}
	NativeTwin_Describe(&src, line, sizeof(line));
	printf("native twin selftest: %s\n", line);
	NativeTwin_Expect(checks, failures, src.standardPoses && (src.native.poseCount == RLDCHAR_NET_POSES), name, "not the 47 standard poses");
	NativeTwin_Expect(checks, failures, (src.count.events == facts.triangles), name, "the triangle places differ from RldChar_CheckModel");
	{
		int same = 1;
		int a;
		int f;

		for (a = 0; a < RLDCHAR_ANIM_COUNT; a++)
		{
			for (f = 0; f < (int)s_rldCharFrames[a]; f++)
			{
				same &= (NativeTwin_PoseIndex(&src, a, f) == NativeCharGpu_PoseIndex(RLDCHAR_NET_POSES, a, f));
			}
			// Past the end retail holds the last frame (raw animations).
			same &= (NativeTwin_PoseIndex(&src, a, f) == NativeCharGpu_PoseIndex(RLDCHAR_NET_POSES, a, f - 1));
		}
		NativeTwin_Expect(checks, failures, same, name, "the pose index differs from NativeCharGpu_PoseIndex");
	}

	// Every point of every pose against its record and the hull of its frame.
	if (!RldChar_FrameHulls(&cmdl[4], bodyBytes, (u32)(uintptr_t)&cmdl[4], hulls))
	{
		NativeTwin_Expect(checks, failures, 0, name, "RldChar_FrameHulls failed");
	}
	else
	{
		const struct ModelHeader *mh = &model->headers[0];
		const double scale[3] = {(double)mh->scale.x, (double)mh->scale.y, (double)(u16)mh->scale.z};
		u32 off = 0;
		u32 outside = 0;
		double worst = 0.0;
		u32 pose = 0;
		u32 a;

		for (a = 0; a < mh->numAnimations; a++)
		{
			const struct ModelAnim *anim = mh->ptrAnimations[a];
			u32 f;

			for (f = 0; f < (anim->numFrames & 0x7fffu); f++, pose++)
			{
				const struct ModelFrame *mf = NativeTwin_StoredFrame(anim, f);
				const u8 *bytes = (const u8 *)mf + mf->vertexOffset;
				u32 i;

				for (i = 0; i < src.native.vertexCount; i++)
				{
					const u32 k = src.cornerVertex[i];
					const u8 *v = &src.native.poses[(((size_t)pose * src.native.vertexCount) + i) * RLDCHAR_NET_VERTEX_BYTES];
					int up = (int)mf->pos.y + (int)bytes[(k * 3u) + 2u];
					double expect[3];
					int c;

					if (!fullHeight)
					{
						up &= ~1;
					}
					expect[0] = ((double)((int)mf->pos.x + (int)bytes[k * 3u]) * scale[0]) / 4096.0;
					expect[1] = ((double)up * scale[1]) / 4096.0;
					expect[2] = ((double)((int)mf->pos.z + (int)bytes[(k * 3u) + 1u]) * scale[2]) / 4096.0;
					for (c = 0; c < 3; c++)
					{
						const double have = (double)RldChar_F32(&v[c * 4]);
						const double diff = (have > expect[c]) ? (have - expect[c]) : (expect[c] - have);
						const double room = 1.0e-6 * ((expect[c] < 0.0) ? -expect[c] : expect[c]) + 1.0e-6;

						worst = (diff > worst) ? diff : worst;
						off += (diff > room) ? 1u : 0u;
						outside += ((have < hulls[pose].lo[c] - 1.0e-4) || (have > hulls[pose].hi[c] + 1.0e-4)) ? 1u : 0u;
					}
				}
			}
		}
		snprintf(why, sizeof(why), "%u coordinate(s) differ from (pos + byte) x scale / 4096 (worst %.9f), %u outside the frame hull", (unsigned)off, worst,
		         (unsigned)outside);
		NativeTwin_Expect(checks, failures, (off == 0u) && (outside == 0u) && (pose == RLDCHAR_NET_POSES), name, why);
		printf("native twin selftest: %s: %u poses x %u points equal to their records (worst %.9f), all inside the frame hulls\n", name, (unsigned)pose,
		       (unsigned)src.native.vertexCount, worst);
	}

	NativeTwin_TestCommon(&model->headers[0], &src, name, checks, failures);

	// The absolute side: the signed volume of the twin's front faces (right-hand
	// normals, the faces the native cull keeps under det(mvp) >= 0) over pose 0.
	// Positive = the faces retail keeps point out of a closed body.
	{
		double volume = 0.0;
		u32 t;

		for (t = 0; t < src.native.triangleCount; t++)
		{
			double q[3][3];
			int c;
			int a;

			for (c = 0; c < 3; c++)
			{
				const u8 *v = &src.native.poses[((size_t)(t * 3u) + (u32)c) * RLDCHAR_NET_VERTEX_BYTES];

				for (a = 0; a < 3; a++)
				{
					q[c][a] = (double)RldChar_F32(&v[a * 4]);
				}
			}
			volume += ((q[0][0] * ((q[1][1] * q[2][2]) - (q[1][2] * q[2][1]))) - (q[0][1] * ((q[1][0] * q[2][2]) - (q[1][2] * q[2][0]))) +
			           (q[0][2] * ((q[1][0] * q[2][1]) - (q[1][1] * q[2][0])))) /
			          6.0;
		}
		snprintf(why, sizeof(why), "the front faces enclose a volume of %.3f (model units cubed) - not out of a closed body", volume);
		NativeTwin_Expect(checks, failures, volume > 0.0, name, why);
		printf("native twin selftest: %s: front faces enclose %.3f model units cubed (pose 0)\n", name, volume);
	}

done:
	NativeTwin_Free(&src);
	free(infoBytes);
	free(cmdl);
}

int NativeTwin_SelfTest(const char *dir)
{
	int checks = 0;
	int failures = 0;
	int files = 0;
	char vertexHex[65];
	char rgbaHex[65];
	char fullHex[65];

	NativeTwin_TestMade(&checks, &failures, vertexHex, rgbaHex, fullHex);
	if ((dir != NULL) && (dir[0] != '\0'))
	{
		NativeTwin_TestFile(dir, &checks, &failures, &files);
	}

	if (failures == 0)
	{
		printf("native twin selftest passed: %d of 1 file read, %d checks, 0 failures, sha256 vertices %s rgba %s full height %s\n", files, checks, vertexHex,
		       rgbaHex, fullHex);
		return 0;
	}
	printf("native twin selftest FAILED: %d of %d checks\n", failures, checks);
	return 1;
}
