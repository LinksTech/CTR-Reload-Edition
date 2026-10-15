#include <platform/engine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE MODEL - retail (PS1 format) models to engine meshes (step 3). See
// include/platform/engine.h.
//
// The decoding is the retail twin's (platform/native_twin.c): NativeTwin_FromModel
// walks the command list of header 0 and decodes every pose of every
// animation (strip FIFO, fans, cached vertices, colour FIFO, winding of the
// one-sided triangles, half frames). This file only turns that source into an
// engine mesh:
//   - POSITIONS are the twin's model units: per axis g x scale / 16384, g the
//     GTE input value of the record ((pos + byte) x 4 for a full frame) and
//     scale {mh->scale.x, mh->scale.y, (u16)mh->scale.z}. The retail queue
//     applies the instance matrix (idpp->mvp) to g, so a draw with that
//     matrix has to multiply the positions by unitScale = 16384 / scale per
//     axis first (the layer's CharItemMatrix does exactly that).
//   - TEXCOORDS are page texels (u, v as the layout stores them, 0..255).
//   - COLOURS are the RGB of the colour table, alpha 255.
//   - Triangles keep the twin's winding: a one-sided one is written in the
//     order the engine's back-face cull keeps when retail keeps it; a
//     two-sided one is taken once (as listed) into a DOUBLE_SIDED material.
//   - One material per distinct (texture page + CLUT, semi-transparency,
//     two-sidedness); the twin's own material split (by 16-page texture) is
//     not used. A semi-transparent face is a textured one whose tpage word
//     has not abr 3 (retail packet code 0x36 against 0x34, RB:3116); its
//     material gets Engine_BlendFromAbr(abr) with ENGINE_MATERIAL_SEMI_STP.
//     The tpage word of the material is the layout's, with the abr bits of
//     the face (an opaque one: 3).
// Not followed: the full-height bit (NativeTwin fullHeight is 0: the odd
// height bit is dropped as for retail models).
//
// ANIMATED LAYOUTS (round of 2026-10-16): LAYOUT SOURCE = THE SLOT, READ AT
// DRAW TIME. Every frame CTR_CycleTex_AllModels (game/MAIN/MainFrame.c) writes
// into a slot of the header's layout table the layout of the animtex frame
// (game/CTR/CTR_CycleTex.c, CTR_CycleTex_Model: *ptrActiveTex = ptrArray[frame]);
// retail draws a triangle with the layout its slot holds at the draw
// (game/RenderBucket/RenderBucket_QueueExecute.c:3065, ptrTexLayout[texIndex - 1]
// per triangle). The import follows that one rule:
//   - per cycled slot (the walk of NativeTwin_AnimatedSlots) every distinct
//     layout it can hold - the one at the load first, then the animtex frames;
//   - the twin decodes the header once more per further layout, with that
//     layout in the slot (a copy of the header and its table in engine memory:
//     the game's model is not written); the triangles whose texture changes
//     between the layouts belong to the slot (Model_SlotsBuild);
//   - those triangles are made once per layout, keyed (material, range) per
//     layout, after the ranges of every other triangle;
//   - Engine_ModelRangeSkip, called by Engine_InstanceSubmit for every draw,
//     reads the pointer each slot holds now and leaves out the ranges of the
//     other layouts (EngineDraw.rangeSkip, honoured by the renderer, the live
//     vertices and the oracle). Nothing is written; all instances of a model
//     share its slots, as in retail.
// A slot whose layouts all draw the same (Papu Pyramid's plant) makes nothing.
// Limits (logged per model, the layout of the load then stays): 8 slots, 32
// layouts per slot, 64 decodes, 64 ranges (the bits of EngineDraw.rangeSkip).
//
// THE LIVE FRAME (round of 2026-10-15). A still model (no animations, an
// uncompressed frame) keeps, per mesh vertex, the vertex record it was decoded
// from (NativeTwinSource.cornerVertex). Engine_ModelLiveFrame rebuilds the
// mesh frame from the model's CURRENT data with it: position from the record's
// bytes in mh->ptrFrameData, colour from mh->ptrColors at the index the
// record's command names now. That is the state retail draws with for a model
// the game rewrites in place (see engine.h, Engine_ModelLiveFrame).

#define MODEL_ANIMS_MAX NATIVE_TWIN_ANIMS_MAX
#define MODEL_KEYS_MAX 256
// ANIMATED LAYOUTS (see above).
#define MODEL_SLOTS_MAX 8           // cycled slots whose layouts draw differently, per model
#define MODEL_VARIANTS_MAX 32       // distinct layouts one slot can hold
#define MODEL_DECODES_MAX 64        // twin decodes for them, per model
#define MODEL_SKIP_RANGES 64        // the bits of EngineDraw.rangeSkip
#define MODEL_ANIMTEX_MAX 256       // AnimTex entries followed (as NativeTwin_AnimatedSlots)
#define MODEL_ANIMTEX_FRAMES_MAX 4096
#define MODEL_TABLE_ENTRIES_MAX 512 // texture index 1..511 names table entry index - 1
// The x/y mask of a full frame without the full-height bit (native_twin.c
// NATIVE_TWIN_MASK_XY, RB:2468-2480): the importer decodes with fullHeight 0.
#define MODEL_LIVE_MASK_XY 0xfff8ffffu

// The frame table of an imported model, found by its mesh index.
typedef struct
{
	int used;
	int animCount;
	int frameTotal;
	int base[MODEL_ANIMS_MAX];
	int frames[MODEL_ANIMS_MAX];
	// A still model: the vertex record of every mesh vertex and the records per
	// frame; NULL and 0 for an animated one.
	u16 *record;
	u32 recordCount;
	u32 streamRecords;
	// ANIMATED LAYOUTS: the model and the mesh the slots were made for (a draw
	// of another mesh in this mesh slot finds other arrays), the slots, and the
	// skip of the load's layouts.
	const struct Model *model;
	const EngineVertex *meshVertices;
	const EngineRange *meshRanges;
	u32 meshRangeCount;
	int slotCount;
	struct ModelSlotTable *slots;
	u64 loadSkip;
	int strayLogged;
} ModelFrameTable;

// One cycled slot of an imported mesh (Engine_ModelRangeSkip): the pointers the
// slot can hold - the one at the load, then the animtex frames - the layout
// (variant) each stands for, and the ranges of each variant.
typedef struct ModelSlotTable
{
	int slot; // layout table index (texture index - 1)
	u32 pointerCount;
	uintptr_t *pointer;
	u8 *variantOf;
	int variantCount;
	u64 rangesOf[MODEL_VARIANTS_MAX];
	u64 ranges; // every range of the slot
} ModelSlotTable;

// What one twin triangle draws in one decode: the parts a material and the
// texture coordinates take.
typedef struct
{
	u8 textured; // 1 POLY_GT3, 0 POLY_G3
	u8 semi;     // NATIVE_TWIN_SEMI_*
	u16 tpage;   // its tile's page word
	u16 clut;    // its tile's clut word (0 for 15 bits)
	int tile;    // its tile in that decode, -1 untextured
	u8 texel[3][2];
} ModelLook;

// One cycled slot while importing (Model_SlotsBuild).
typedef struct
{
	int slot;
	int variantCount; // distinct layouts the slot can hold; variant 0 = the one at the load
	const struct TextureLayout *layout[MODEL_VARIANTS_MAX];
	u32 pointerCount;
	uintptr_t *pointer;
	u8 *variantOf;
	ModelLook *look; // variantCount x the twin's triangles
	u32 triangles;   // twin triangles of the slot (no second copies)
} ModelSlotBuild;

global_variable ModelFrameTable s_modelTables[ENGINE_MESHES_MAX];
// Engine_ModelLiveFrame: the colour index of every record of the current list.
global_variable u8 s_modelLiveColour[NATIVE_TWIN_STREAM_MAX];

// Why the last Engine_ModelImport failed ("" after a success), for the callers'
// log lines (the track's per-model report).
global_variable char s_modelWhy[256];

const char *Engine_ModelImportWhy(void)
{
	return s_modelWhy;
}

// What distinguishes one engine material of a model.
typedef struct
{
	int tile; // twin tile, -1 untextured (a slot key of another layout than the load's: -2)
	int semi; // NATIVE_TWIN_SEMI_*
	int doubleSided;
	// ANIMATED LAYOUTS: -1 for a key of the triangles that follow no slot; else
	// the slot (index into the build's slots) and its layout (variant).
	int slot;
	int variant;
	// What the material takes: textured, the tile's page and clut words.
	int textured;
	u16 tpage;
	u16 clut;
} ModelKey;

internal float Model_ReadF32(const u8 *at)
{
	float f;

	memcpy(&f, at, sizeof(f));
	return f;
}

// The texel of a UV the twin wrote for a tile: (cell x 256 + texel) / edge.
internal u32 Model_TexelOf(float uv, u32 edge)
{
	return (u32)((double)uv * (double)edge + 0.5);
}

// The tile of a textured twin triangle t, from the UV of its first corner.
internal int Model_TileOf(const struct NativeTwinSource *src, u32 t, int material)
{
	const int texture = (s16)Rld_ReadLE16(&src->native.materials[((size_t)material * RLDCHAR_NET_MATERIAL_BYTES) + 4u]);
	const u8 *uv = &src->native.uv[(size_t)t * 3u * 8u];
	u32 k;
	u32 cellX;
	u32 cellY;

	if ((texture < 0) || ((u32)texture >= src->native.textureCount))
	{
		return -1;
	}
	cellX = Model_TexelOf(Model_ReadF32(uv), src->native.texture[texture].width) / (u32)NATIVE_TWIN_TILE_EDGE;
	cellY = Model_TexelOf(Model_ReadF32(uv + 4), src->native.texture[texture].height) / (u32)NATIVE_TWIN_TILE_EDGE;
	for (k = 0; k < src->tileCount; k++)
	{
		if ((src->tile[k].texture == texture) && (src->tile[k].cellX == cellX) && (src->tile[k].cellY == cellY))
		{
			return (int)k;
		}
	}
	return -1;
}

// --------------------------------------------------------------------------
// ANIMATED LAYOUTS (see the top of the file).

// What twin triangle t of decode s draws.
internal void Model_LookOf(const struct NativeTwinSource *s, u32 t, ModelLook *out)
{
	const int m = (s16)Rld_ReadLE16(&s->native.triangleMaterials[(size_t)t * 2u]);
	int c;

	memset(out, 0, sizeof(*out));
	out->tile = -1;
	out->semi = NATIVE_TWIN_SEMI_NONE;
	if (!s->material[m].textured)
	{
		return;
	}
	out->textured = 1;
	out->semi = s->material[m].semi;
	out->tile = Model_TileOf(s, t, m);
	if (out->tile < 0)
	{
		return;
	}
	out->tpage = s->tile[out->tile].tpage;
	out->clut = s->tile[out->tile].clut;
	for (c = 0; c < 3; c++)
	{
		const u8 *uv = &s->native.uv[(((size_t)t * 3u) + (u32)c) * 8u];
		const struct NativeTwinTile *tile = &s->tile[out->tile];
		const u32 tex = tile->texture;

		out->texel[c][0] = (u8)(Model_TexelOf(Model_ReadF32(uv), s->native.texture[tex].width) - ((u32)tile->cellX * (u32)NATIVE_TWIN_TILE_EDGE));
		out->texel[c][1] = (u8)(Model_TexelOf(Model_ReadF32(uv + 4), s->native.texture[tex].height) - ((u32)tile->cellY * (u32)NATIVE_TWIN_TILE_EDGE));
	}
}

// 1 = a and b draw the same: textured, blend, page (x, y, depth), clut and texels.
internal int Model_LookSame(const ModelLook *a, const ModelLook *b)
{
	if ((a->textured != b->textured) || (a->semi != b->semi))
	{
		return 0;
	}
	if (!a->textured)
	{
		return 1;
	}
	return ((a->tpage & 0x019fu) == (b->tpage & 0x019fu)) && (a->clut == b->clut) && (memcmp(a->texel, b->texel, sizeof(a->texel)) == 0);
}

// 1 = two layouts a slot can hold are the same for a triangle: the fields it
// reads (u0 v0 clut u1 v1 tpage u2 v2, the first 10 bytes); NULL only equals NULL.
internal int Model_LayoutSame(const struct TextureLayout *a, const struct TextureLayout *b)
{
	if ((a == NULL) || (b == NULL))
	{
		return a == b;
	}
	return memcmp(a, b, 10u) == 0;
}

// The table entries the header's command list can name: the highest texture
// index (bits 0-8) of a vertex command. The commands the twin reads the table
// with (NativeTwin_WalkList, its events) are among them.
internal u32 Model_TableEntries(const struct ModelHeader *mh)
{
	const u32 *list = (const u32 *)(uintptr_t)mh->ptrCommandList;
	u32 entries = 0;
	u32 index;

	if (list == NULL)
	{
		return 0;
	}
	for (index = 1; (index <= (u32)NATIVE_TWIN_COMMANDS_MAX) && (list[index] != 0xffffffffu); index++)
	{
		const u32 command = list[index];

		if (((command >> 16) != 0u) && ((command & 0x1ffu) > entries))
		{
			entries = command & 0x1ffu;
		}
	}
	return entries;
}

internal void Model_SlotsFree(ModelSlotBuild *slots, int count)
{
	int s;

	for (s = 0; s < count; s++)
	{
		free(slots[s].pointer);
		free(slots[s].variantOf);
		free(slots[s].look);
		memset(&slots[s], 0, sizeof(slots[s]));
	}
}

// The slot of the animtex entry at cur, its pointers and distinct layouts.
// Returns 1 with *sb filled (variantCount 1: nothing changes), 0 when the slot
// is not in the table (not followed), -1 with why set.
internal int Model_SlotOfEntry(const struct ModelHeader *mh, const struct AnimTex *cur, u32 entries, ModelSlotBuild *sb, char *why, size_t whySize)
{
	const u8 *slotAt = (const u8 *)cur->ptrActiveTex;
	const u8 *tableAt = (const u8 *)mh->ptrTexLayout;
	const int frames = (int)cur->numFrames;
	struct TextureLayout *const *frame = (struct TextureLayout *const *)((const u8 *)cur + sizeof(struct AnimTex));
	size_t index;
	u32 p;

	memset(sb, 0, sizeof(*sb));
	if ((slotAt < tableAt) || ((((size_t)(slotAt - tableAt)) % sizeof(struct TextureLayout *)) != 0u) ||
	    ((((size_t)(slotAt - tableAt)) / sizeof(struct TextureLayout *)) >= entries))
	{
		return 0;
	}
	index = ((size_t)(slotAt - tableAt)) / sizeof(struct TextureLayout *);
	sb->slot = (int)index;
	sb->pointerCount = 1u + (u32)frames;
	sb->pointer = (uintptr_t *)calloc(sb->pointerCount, sizeof(uintptr_t));
	sb->variantOf = (u8 *)calloc(sb->pointerCount, 1u);
	if ((sb->pointer == NULL) || (sb->variantOf == NULL))
	{
		snprintf(why, whySize, "out of memory");
		return -1;
	}
	sb->pointer[0] = (uintptr_t)mh->ptrTexLayout[index];
	for (p = 1; p < sb->pointerCount; p++)
	{
		sb->pointer[p] = (uintptr_t)frame[p - 1u];
	}
	for (p = 0; p < sb->pointerCount; p++)
	{
		const struct TextureLayout *lay = (const struct TextureLayout *)sb->pointer[p];
		int v;

		for (v = 0; v < sb->variantCount; v++)
		{
			if (Model_LayoutSame(lay, sb->layout[v]))
			{
				break;
			}
		}
		if (v == sb->variantCount)
		{
			if (sb->variantCount >= MODEL_VARIANTS_MAX)
			{
				snprintf(why, whySize, "texture index %d holds more than %d layouts", sb->slot + 1, MODEL_VARIANTS_MAX);
				return -1;
			}
			sb->layout[sb->variantCount++] = lay;
		}
		sb->variantOf[p] = (u8)v;
	}
	return 1;
}

// The cycled slots of header 0 whose layouts draw differently, the walk of
// NativeTwin_AnimatedSlots (CTR_CycleTex_Model; only when CTR_CycleTex_AllModels
// cycles the header at all: animtex set, flags bit 1 clear). Per slot every
// layout it can hold (Model_SlotOfEntry) and every twin triangle as decoded
// with that layout in the slot: the load's from src, the others from a decode
// of a copy of the header whose table copy holds that layout (the game's model
// and table are only read). slotOfTri[t] = the slot whose layouts change what
// twin triangle t draws, -1 for none. Returns the slot count; 0 with why empty
// when there is none, 0 with why set when refused (slotOfTri all -1).
internal int Model_SlotsBuild(const struct Model *model, const struct NativeTwinSource *src, ModelSlotBuild *slots, int *slotOfTri, char *why, size_t whySize)
{
	const struct ModelHeader *mh = &model->headers[0];
	const struct AnimTex *first = mh->animtex;
	const struct AnimTex *cur = first;
	const u32 tris = src->native.triangleCount;
	struct TextureLayout **table = NULL;
	struct ModelHeader shadow;
	u32 entries;
	int count = 0;
	int decodes = 0;
	int kept = 0;
	int n;
	int s;
	u32 t;

	why[0] = '\0';
	for (t = 0; t < tris; t++)
	{
		slotOfTri[t] = -1;
	}
	if ((first == NULL) || ((mh->flags & 2u) != 0u) || (mh->ptrTexLayout == NULL))
	{
		return 0;
	}
	entries = Model_TableEntries(mh);
	if (entries == 0u)
	{
		return 0;
	}

	// The slots and their layouts.
	for (n = 0; (n < MODEL_ANIMTEX_MAX) && (*(const u32 *)cur != (u32)(uintptr_t)first); n++)
	{
		const int frames = (int)cur->numFrames;
		ModelSlotBuild sb;
		int got;

		if ((frames <= 0) || (frames > MODEL_ANIMTEX_FRAMES_MAX))
		{
			break; // a list the game would walk differently: not followed further (as the twin)
		}
		got = Model_SlotOfEntry(mh, cur, entries, &sb, why, whySize);
		if (got < 0)
		{
			Model_SlotsFree(&sb, 1);
			goto refuse;
		}
		for (s = 0; (got > 0) && (s < count); s++)
		{
			if (slots[s].slot == sb.slot)
			{
				snprintf(why, whySize, "texture index %d is cycled by two animtex entries", sb.slot + 1);
				Model_SlotsFree(&sb, 1);
				goto refuse;
			}
		}
		if ((got > 0) && (sb.variantCount > 1))
		{
			if (count >= MODEL_SLOTS_MAX)
			{
				snprintf(why, whySize, "more than %d cycled slots", MODEL_SLOTS_MAX);
				Model_SlotsFree(&sb, 1);
				goto refuse;
			}
			slots[count++] = sb;
		}
		else
		{
			Model_SlotsFree(&sb, 1);
		}
		cur = (const struct AnimTex *)((const u8 *)cur + sizeof(struct AnimTex) + ((size_t)frames * sizeof(struct TextureLayout *)));
	}
	if (count == 0)
	{
		return 0;
	}

	// Every triangle per layout of each slot.
	table = (struct TextureLayout **)calloc(MODEL_TABLE_ENTRIES_MAX, sizeof(*table));
	if (table == NULL)
	{
		snprintf(why, whySize, "out of memory");
		goto refuse;
	}
	memcpy(table, mh->ptrTexLayout, (size_t)entries * sizeof(*table));
	shadow = *mh;
	shadow.ptrTexLayout = table;
	shadow.animtex = NULL; // the copy is decoded as it is, no slot cycled
	for (s = 0; s < count; s++)
	{
		ModelSlotBuild *sb = &slots[s];
		int v;

		sb->look = (ModelLook *)calloc((size_t)sb->variantCount * tris, sizeof(ModelLook));
		if (sb->look == NULL)
		{
			snprintf(why, whySize, "out of memory");
			goto refuse;
		}
		for (t = 0; t < tris; t++)
		{
			Model_LookOf(src, t, &sb->look[t]);
		}
		for (v = 1; v < sb->variantCount; v++)
		{
			struct NativeTwinSource vs;
			char vwhy[192];

			if (++decodes > MODEL_DECODES_MAX)
			{
				snprintf(why, whySize, "more than %d decodes", MODEL_DECODES_MAX);
				goto refuse;
			}
			table[sb->slot] = (struct TextureLayout *)sb->layout[v];
			if (!NativeTwin_FromHeader(&shadow, 0, &vs, vwhy, sizeof(vwhy)))
			{
				snprintf(why, whySize, "texture index %d layout %d: decoder: %s", sb->slot + 1, v, vwhy);
				goto refuse;
			}
			if (vs.native.triangleCount != tris)
			{
				snprintf(why, whySize, "texture index %d layout %d: %u triangles, the load %u", sb->slot + 1, v, (unsigned)vs.native.triangleCount, (unsigned)tris);
				NativeTwin_Free(&vs);
				goto refuse;
			}
			for (t = 0; t < tris; t++)
			{
				Model_LookOf(&vs, t, &sb->look[((size_t)v * tris) + t]);
			}
			NativeTwin_Free(&vs);
		}
		table[sb->slot] = mh->ptrTexLayout[sb->slot];

		// The slot's triangles: what they draw changes with the layout.
		for (t = 0; t < tris; t++)
		{
			if (src->triangleSide[t] == 2u)
			{
				continue;
			}
			for (v = 1; v < sb->variantCount; v++)
			{
				if (!Model_LookSame(&sb->look[t], &sb->look[((size_t)v * tris) + t]))
				{
					break;
				}
			}
			if (v == sb->variantCount)
			{
				continue;
			}
			if (slotOfTri[t] >= 0)
			{
				snprintf(why, whySize, "triangle %u changes with texture indices %d and %d", (unsigned)t, slots[slotOfTri[t]].slot + 1, sb->slot + 1);
				goto refuse;
			}
			slotOfTri[t] = s;
			sb->triangles++;
		}
	}
	free(table);
	table = NULL;

	// One layout of a slot is one texture for all its triangles (the key of its range).
	for (s = 0; s < count; s++)
	{
		const ModelSlotBuild *sb = &slots[s];
		int v;

		for (v = 0; v < sb->variantCount; v++)
		{
			const ModelLook *ref = NULL;

			for (t = 0; t < tris; t++)
			{
				const ModelLook *look = &sb->look[((size_t)v * tris) + t];

				if (slotOfTri[t] != s)
				{
					continue;
				}
				if (look->textured && (look->tile < 0))
				{
					snprintf(why, whySize, "texture index %d layout %d: a textured triangle without a tile", sb->slot + 1, v);
					goto refuse;
				}
				if (ref == NULL)
				{
					ref = look;
				}
				else if ((look->textured != ref->textured) || (look->semi != ref->semi) || (look->tpage != ref->tpage) || (look->clut != ref->clut))
				{
					snprintf(why, whySize, "texture index %d layout %d: its triangles differ in page, clut or blend", sb->slot + 1, v);
					goto refuse;
				}
			}
		}
	}

	// Slots without triangles (their layouts differ in nothing a triangle reads) go.
	for (s = 0; s < count; s++)
	{
		if (slots[s].triangles == 0u)
		{
			Model_SlotsFree(&slots[s], 1);
			continue;
		}
		if (kept != s)
		{
			slots[kept] = slots[s];
			memset(&slots[s], 0, sizeof(slots[s]));
			for (t = 0; t < tris; t++)
			{
				slotOfTri[t] = (slotOfTri[t] == s) ? kept : slotOfTri[t];
			}
		}
		kept++;
	}
	return kept;

refuse:
	free(table);
	Model_SlotsFree(slots, count);
	for (t = 0; t < tris; t++)
	{
		slotOfTri[t] = -1;
	}
	return 0;
}

// The material keys of the twin triangles that follow no slot (slotOfTri NULL
// or -1), in order of first use: triKey[t] the key, -1 for the dropped second
// copy of a two-sided triangle and for a slot's triangle. Returns the kept
// triangles, -1 with s_modelWhy set.
internal int Model_BaseKeys(const struct NativeTwinSource *src, const int *slotOfTri, ModelKey *keys, int *keyCount, int *triKey)
{
	int kept = 0;
	u32 t;
	int k;

	*keyCount = 0;
	// The second copy of a two-sided triangle (triangleSide 2) is dropped.
	for (t = 0; t < src->native.triangleCount; t++)
	{
		const int m = (s16)Rld_ReadLE16(&src->native.triangleMaterials[(size_t)t * 2u]);
		ModelKey key;

		triKey[t] = -1;
		if ((src->triangleSide[t] == 2u) || ((slotOfTri != NULL) && (slotOfTri[t] >= 0)))
		{
			continue;
		}
		memset(&key, 0, sizeof(key));
		key.tile = src->material[m].textured ? Model_TileOf(src, t, m) : -1;
		key.semi = src->material[m].textured ? src->material[m].semi : NATIVE_TWIN_SEMI_NONE;
		key.doubleSided = (src->triangleSide[t] == 1u) ? 1 : 0;
		key.slot = -1;
		key.variant = -1;
		if (src->material[m].textured && (key.tile < 0))
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "textured triangle %u without a tile", (unsigned)t);
			return -1;
		}
		key.textured = src->material[m].textured ? 1 : 0;
		key.tpage = (key.tile >= 0) ? src->tile[key.tile].tpage : 0u;
		key.clut = (key.tile >= 0) ? src->tile[key.tile].clut : 0u;
		for (k = 0; k < *keyCount; k++)
		{
			if ((keys[k].tile == key.tile) && (keys[k].semi == key.semi) && (keys[k].doubleSided == key.doubleSided))
			{
				break;
			}
		}
		if (k == *keyCount)
		{
			if (*keyCount >= MODEL_KEYS_MAX)
			{
				snprintf(s_modelWhy, sizeof(s_modelWhy), "more than %d materials", MODEL_KEYS_MAX);
				return -1;
			}
			keys[(*keyCount)++] = key;
		}
		triKey[t] = k;
		kept++;
	}
	return kept;
}

// 1 = twin triangle t is drawn in the range of key k (k = its index).
internal int Model_KeyHolds(const ModelKey *key, int k, const struct NativeTwinSource *src, const int *triKey, const int *slotOfTri, u32 t)
{
	if (key->slot < 0)
	{
		return triKey[t] == k;
	}
	return (slotOfTri[t] == key->slot) && (src->triangleSide[t] != 2u) && (((src->triangleSide[t] == 1u) ? 1 : 0) == key->doubleSided);
}

// The keys of the slots' triangles, after the others: per slot, layout and
// sidedness one. A triangle's triKey becomes the key of the load's layout (its
// tile is one of src: the vertex fill writes the load's texels, Model_SlotTexels
// the others'). Returns the triangle copies they hold, -1 with s_modelWhy set.
internal int Model_SlotKeys(const struct NativeTwinSource *src, const ModelSlotBuild *slots, int slotCount, const int *slotOfTri, ModelKey *keys,
                            int *keyCount, int *triKey)
{
	const u32 tris = src->native.triangleCount;
	int copies = 0;
	int s;

	for (s = 0; s < slotCount; s++)
	{
		const ModelSlotBuild *sb = &slots[s];
		int v;

		for (v = 0; v < sb->variantCount; v++)
		{
			int ds;

			for (ds = 0; ds < 2; ds++)
			{
				ModelKey key;
				u32 held = 0;
				u32 t;

				memset(&key, 0, sizeof(key));
				key.doubleSided = ds;
				key.slot = s;
				key.variant = v;
				for (t = 0; t < tris; t++)
				{
					if (Model_KeyHolds(&key, 0, src, NULL, slotOfTri, t))
					{
						const ModelLook *look = &sb->look[((size_t)v * tris) + t];

						if (held == 0u)
						{
							key.tile = (v == 0) ? look->tile : (look->textured ? -2 : -1);
							key.semi = look->semi;
							key.textured = look->textured;
							key.tpage = look->tpage;
							key.clut = look->clut;
						}
						held++;
					}
				}
				if (held == 0u)
				{
					continue;
				}
				if (*keyCount >= MODEL_KEYS_MAX)
				{
					snprintf(s_modelWhy, sizeof(s_modelWhy), "more than %d materials", MODEL_KEYS_MAX);
					return -1;
				}
				if (v == 0)
				{
					for (t = 0; t < tris; t++)
					{
						if (Model_KeyHolds(&key, 0, src, NULL, slotOfTri, t))
						{
							triKey[t] = *keyCount;
						}
					}
				}
				keys[(*keyCount)++] = key;
				copies += (int)held;
			}
		}
	}
	return copies;
}

// The texels of the slot copies of another layout than the load's (entry e of
// the mesh is twin triangle order[e] in the range of key entryKey[e]), in every
// frame: the vertex fill wrote the load's.
internal void Model_SlotTexels(EngineVertex *vertices, u32 frames, u32 perFrame, u32 entries, const u32 *order, const int *entryKey, const ModelKey *keys,
                               const ModelSlotBuild *slots, u32 tris)
{
	u32 e;
	u32 f;

	for (e = 0; e < entries; e++)
	{
		const ModelKey *key = &keys[entryKey[e]];
		const ModelLook *look;
		int c;

		if ((key->slot < 0) || (key->variant == 0))
		{
			continue;
		}
		look = &slots[key->slot].look[((size_t)key->variant * tris) + order[e]];
		for (f = 0; f < frames; f++)
		{
			for (c = 0; c < 3; c++)
			{
				EngineVertex *v = &vertices[((size_t)f * perFrame) + (e * 3u) + (u32)c];

				v->texcoord[0] = look->textured ? (float)look->texel[c][0] : 0.0f;
				v->texcoord[1] = look->textured ? (float)look->texel[c][1] : 0.0f;
			}
		}
	}
}

// A mesh slot's table back to empty (an earlier mesh's records and slots go).
internal void Model_TableFree(ModelFrameTable *table)
{
	int s;

	free(table->record);
	for (s = 0; s < table->slotCount; s++)
	{
		free(table->slots[s].pointer);
		free(table->slots[s].variantOf);
	}
	free(table->slots);
	memset(table, 0, sizeof(*table));
}

int Engine_ModelImport(const struct Model *model, const char *name, EngineModelImport *out)
{
	struct NativeTwinSource src;
	char why[192];
	ModelKey keys[MODEL_KEYS_MAX];
	int keyCount = 0;
	int baseKeys = 0;
	int *triKey = NULL;
	u32 *order = NULL;
	int *entryKey = NULL;
	ModelSlotBuild slots[MODEL_SLOTS_MAX];
	ModelSlotTable *slotTables = NULL;
	int slotCount = 0;
	int *slotOfTri = NULL;
	char slotWhy[192];
	EngineVertex *vertices = NULL;
	u16 *indices = NULL;
	EngineRange *ranges = NULL;
	EngineMaterial *materials = NULL;
	u16 *record = NULL;
	u32 frames = 0;
	u32 perFrame = 0;
	u32 keptTris = 0;
	u32 t;
	u32 f;
	u32 a;
	int k;
	int firstMaterial = -1;
	int ok = 0;

	s_modelWhy[0] = '\0';
	slotWhy[0] = '\0';
	memset(slots, 0, sizeof(slots));
	if (out == NULL)
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "no output");
		return 0;
	}
	memset(out, 0, sizeof(*out));
	out->mesh = -1;
	if ((model == NULL) || (model->numHeaders < 1) || (model->headers == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "no model header");
		return 0;
	}
	// A failed NativeTwin_FromModel leaves src freed and zeroed.
	if (!NativeTwin_FromModel(model, 0, &src, why, sizeof(why)))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "decoder: %s", why);
		return 0;
	}

	frames = (src.animCount == 0u) ? 1u : 0u;
	for (a = 0; a < src.animCount; a++)
	{
		frames += src.animFrames[a];
	}
	triKey = (int *)calloc(src.native.triangleCount, sizeof(int));
	slotOfTri = (int *)calloc(src.native.triangleCount, sizeof(int));
	if ((frames == 0u) || (triKey == NULL) || (slotOfTri == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "%s", (frames == 0u) ? "no frame" : "out of memory");
		goto done;
	}

	// ANIMATED LAYOUTS: the cycled slots and their triangles (Model_SlotsBuild).
	slotCount = Model_SlotsBuild(model, &src, slots, slotOfTri, slotWhy, sizeof(slotWhy));

	// The kept triangles and their material keys, in order of first use; then
	// the keys of the slots' triangles, one per layout. A model with more ranges
	// than EngineDraw.rangeSkip has bits keeps the layout of the load.
	for (;;)
	{
		int base = Model_BaseKeys(&src, slotOfTri, keys, &keyCount, triKey);
		int copies = 0;

		if (base < 0)
		{
			goto done;
		}
		baseKeys = keyCount;
		if (slotCount > 0)
		{
			copies = Model_SlotKeys(&src, slots, slotCount, slotOfTri, keys, &keyCount, triKey);
			if ((copies < 0) || (keyCount > MODEL_SKIP_RANGES))
			{
				snprintf(slotWhy, sizeof(slotWhy), "%s", (copies < 0) ? s_modelWhy : "more ranges than a draw can leave out");
				s_modelWhy[0] = '\0';
				Model_SlotsFree(slots, slotCount);
				slotCount = 0;
				for (t = 0; t < src.native.triangleCount; t++)
				{
					slotOfTri[t] = -1;
				}
				continue;
			}
		}
		keptTris = (u32)(base + copies);
		break;
	}
	perFrame = keptTris * 3u;
	if ((keptTris == 0u) || (perFrame > 65535u) || ((u64)perFrame * frames > (1u << 20)))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "%u triangle(s) x %u frame(s) outside the mesh limits", (unsigned)keptTris, (unsigned)frames);
		goto done;
	}

	// Stable order by material: entry e of the mesh is twin triangle order[e] in
	// the range of key entryKey[e] (a slot's triangle once per layout).
	order = (u32 *)calloc(keptTris, sizeof(u32));
	entryKey = (int *)calloc(keptTris, sizeof(int));
	if ((order == NULL) || (entryKey == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "out of memory");
		goto done;
	}
	{
		u32 n = 0;

		for (k = 0; k < keyCount; k++)
		{
			for (t = 0; t < src.native.triangleCount; t++)
			{
				if (Model_KeyHolds(&keys[k], k, &src, triKey, slotOfTri, t) && (n < keptTris))
				{
					order[n] = t;
					entryKey[n++] = k;
				}
			}
		}
	}

	vertices = (EngineVertex *)calloc((size_t)perFrame * frames, sizeof(EngineVertex));
	indices = (u16 *)calloc(perFrame, sizeof(u16));
	ranges = (EngineRange *)calloc((size_t)keyCount, sizeof(EngineRange));
	materials = (EngineMaterial *)calloc((size_t)keyCount, sizeof(EngineMaterial));
	if ((vertices == NULL) || (indices == NULL) || (ranges == NULL) || (materials == NULL))
	{
		snprintf(s_modelWhy, sizeof(s_modelWhy), "out of memory");
		goto done;
	}
	// A still model with an uncompressed frame: the record of every mesh
	// vertex, for Engine_ModelLiveFrame (an allocation failure only leaves the
	// model without it).
	if ((src.animCount == 0u) && (model->headers[0].unk3 == 0u) && (src.streamVertices <= (u32)NATIVE_TWIN_STREAM_MAX))
	{
		record = (u16 *)calloc(perFrame, sizeof(u16));
		if (record != NULL)
		{
			for (t = 0; t < perFrame; t++)
			{
				record[t] = (u16)src.cornerVertex[(order[t / 3u] * 3u) + (t % 3u)];
			}
		}
	}

	for (f = 0; f < frames; f++)
	{
		for (t = 0; t < keptTris; t++)
		{
			const u32 tri = order[t];
			const int tile = keys[triKey[tri]].tile;
			int c;

			for (c = 0; c < 3; c++)
			{
				const u32 sv = (tri * 3u) + (u32)c; // twin vertex
				const u32 dv = (t * 3u) + (u32)c;
				EngineVertex *v = &vertices[((size_t)f * perFrame) + dv];
				const u8 *pos = &src.native.poses[(((size_t)f * src.native.vertexCount) + sv) * RLDCHAR_NET_VERTEX_BYTES];

				v->position[0] = Model_ReadF32(pos);
				v->position[1] = Model_ReadF32(pos + 4);
				v->position[2] = Model_ReadF32(pos + 8);
				if (tile >= 0)
				{
					const u8 *uv = &src.native.uv[(size_t)sv * 8u];
					const u32 tex = src.tile[tile].texture;

					v->texcoord[0] = (float)(Model_TexelOf(Model_ReadF32(uv), src.native.texture[tex].width) - ((u32)src.tile[tile].cellX * (u32)NATIVE_TWIN_TILE_EDGE));
					v->texcoord[1] = (float)(Model_TexelOf(Model_ReadF32(uv + 4), src.native.texture[tex].height) - ((u32)src.tile[tile].cellY * (u32)NATIVE_TWIN_TILE_EDGE));
				}
				memcpy(v->color, &src.native.colors[(size_t)sv * 4u], 4);
				indices[dv] = (u16)dv;
			}
			// The UV box of the triangle (engine.h, THE UV BOX): the box of its three
			// corners when textured, the whole page when not.
			if (tile >= 0)
			{
				Engine_VertexBoxOfCorners(&vertices[((size_t)f * perFrame) + ((size_t)t * 3u)], 3);
			}
			else
			{
				Engine_VertexBoxFull(&vertices[((size_t)f * perFrame) + ((size_t)t * 3u)], 3);
			}
		}
	}
	// ANIMATED LAYOUTS: the slot copies of the other layouts take their texels.
	Model_SlotTexels(vertices, frames, perFrame, keptTris, order, entryKey, keys, slots, src.native.triangleCount);

	{
		u32 at = 0;

		for (k = 0; k < keyCount; k++)
		{
			ranges[k].firstIndex = at * 3u;
			while ((at < keptTris) && (entryKey[at] == k))
			{
				at++;
			}
			ranges[k].indexCount = (at * 3u) - ranges[k].firstIndex;
		}
	}

	// The materials. They can not be unregistered: a failed import after this
	// point leaves them registered and unused.
	for (k = 0; k < keyCount; k++)
	{
		EngineMaterial *m = &materials[k];
		const int semi = (keys[k].semi != NATIVE_TWIN_SEMI_NONE);
		int index;

		m->tint[0] = m->tint[1] = m->tint[2] = m->tint[3] = 1.0f;
		m->flags = (u16)((keys[k].doubleSided ? ENGINE_MATERIAL_DOUBLE_SIDED : 0u) | (semi ? ENGINE_MATERIAL_SEMI_STP : 0u));
		m->blend = (u8)ENGINE_BLEND_OPAQUE;
		// The key holds its tile's page and clut words (the tile of the decode of its layout).
		if (keys[k].textured)
		{
			const u32 abr = semi ? (u32)(keys[k].semi - NATIVE_TWIN_SEMI_MODE0) : 3u;

			m->texSource = ENGINE_TEX_VRAM;
			m->tpage = (u16)((keys[k].tpage & ~0x60u) | (abr << 5));
			m->clut = keys[k].clut;
			if (semi)
			{
				m->blend = (u8)Engine_BlendFromAbr((int)abr);
			}
		}
		else
		{
			m->texSource = ENGINE_TEX_NONE;
		}
		index = Engine_MaterialRegister(m);
		if ((index < 0) || ((k > 0) && (index != (firstMaterial + k))))
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "%s", (index < 0) ? "material table full" : "materials not consecutive");
			goto done;
		}
		if (k == 0)
		{
			firstMaterial = index;
		}
		ranges[k].material = (u16)index;
	}

	// ANIMATED LAYOUTS: the slots of the mesh (before it is made: no mesh without them).
	if (slotCount > 0)
	{
		slotTables = (ModelSlotTable *)calloc((size_t)slotCount, sizeof(ModelSlotTable));
		if (slotTables == NULL)
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "out of memory");
			goto done;
		}
		for (k = baseKeys; k < keyCount; k++)
		{
			ModelSlotTable *st = &slotTables[keys[k].slot];

			st->rangesOf[keys[k].variant] |= (u64)1u << (u32)k;
			st->ranges |= (u64)1u << (u32)k;
		}
	}

	{
		EngineMeshDesc desc;
		ModelFrameTable *table;
		int mesh;
		int base = 0;

		memset(&desc, 0, sizeof(desc));
		desc.vertices = vertices;
		desc.verticesPerFrame = perFrame;
		desc.frames = frames;
		desc.indices = indices;
		desc.indexCount = perFrame;
		desc.ranges = ranges;
		desc.rangeCount = (u32)keyCount;
		desc.name = (name != NULL) ? name : "model";
		mesh = Engine_MeshCreate(&desc);
		if (mesh < 0)
		{
			snprintf(s_modelWhy, sizeof(s_modelWhy), "mesh table full or mesh refused");
			goto done;
		}

		table = &s_modelTables[mesh];
		// The slot of a mesh of an earlier level: its records and slots go with it.
		Model_TableFree(table);
		table->used = 1;
		if (slotTables != NULL)
		{
			const EngineMeshDesc *made = Engine_MeshDesc(mesh);
			int s;

			for (s = 0; s < slotCount; s++)
			{
				ModelSlotTable *st = &slotTables[s];

				st->slot = slots[s].slot;
				st->variantCount = slots[s].variantCount;
				st->pointerCount = slots[s].pointerCount;
				st->pointer = slots[s].pointer;
				st->variantOf = slots[s].variantOf;
				slots[s].pointer = NULL;
				slots[s].variantOf = NULL;
				table->loadSkip |= st->ranges & ~st->rangesOf[0];
			}
			table->model = model;
			table->meshVertices = (made != NULL) ? made->vertices : NULL;
			table->meshRanges = (made != NULL) ? made->ranges : NULL;
			table->meshRangeCount = (made != NULL) ? made->rangeCount : 0u;
			table->slots = slotTables;
			table->slotCount = slotCount;
			slotTables = NULL;
		}
		table->animCount = (int)src.animCount;
		table->frameTotal = (int)frames;
		for (a = 0; a < src.animCount; a++)
		{
			table->base[a] = base;
			table->frames[a] = (int)src.animFrames[a];
			base += (int)src.animFrames[a];
		}
		if (record != NULL)
		{
			table->record = record;
			table->recordCount = perFrame;
			table->streamRecords = src.streamVertices;
			record = NULL;
		}
		// The engine oracle (--engine-oracle only): which mesh triangles come from a
		// cycled (animtex) table slot, for anim=1 of its E lines.
		if ((g_cfg_engineOracle[0] != '\0') && (src.count.animatedTriangles > 0u))
		{
			u8 *triAnimated = (u8 *)calloc(keptTris, 1);

			if (triAnimated != NULL)
			{
				for (t = 0; t < keptTris; t++)
				{
					const int m = (s16)Rld_ReadLE16(&src.native.triangleMaterials[(size_t)order[t] * 2u]);

					triAnimated[t] = src.material[m].animated;
				}
				Engine_OracleNoteMeshAnimated(mesh, triAnimated, keptTris);
				free(triAnimated);
			}
		}
		Engine_ModelNoteTriangles(mesh, model, &src, order, keptTris);
		out->mesh = mesh;
		out->firstMaterial = firstMaterial;
		out->materialCount = keyCount;
		out->headerCount = 1;
		out->animCount = (int)src.animCount;
		out->frameTotal = (int)frames;
		if (slotCount > 0)
		{
			char list[160];
			size_t used = 0;
			u32 slotTris = 0;
			int s;

			list[0] = '\0';
			for (s = 0; (s < slotCount) && (used < sizeof(list)); s++)
			{
				int wrote = snprintf(list + used, sizeof(list) - used, "%s%d:%d", (s > 0) ? " " : "", slots[s].slot + 1, slots[s].variantCount);

				used += (wrote > 0) ? (size_t)wrote : 0u;
				slotTris += slots[s].triangles;
			}
			Platform_Log("[CTR Engine] model '%s': animated layouts follow their slot when drawn: texture index:layouts %s; %u triangle(s) in %d "
			             "range(s) after %d other range(s)\n",
			             desc.name, list, (unsigned)slotTris, keyCount - baseKeys, baseKeys);
		}
		else if (slotWhy[0] != '\0')
		{
			Platform_Log("[CTR Engine] model '%s': animated layouts stay at the load: %s\n", desc.name, slotWhy);
		}
	}
	ok = 1;

done:
	free(triKey);
	free(order);
	free(entryKey);
	free(slotOfTri);
	free(slotTables);
	Model_SlotsFree(slots, slotCount);
	free(vertices);
	free(indices);
	free(ranges);
	free(materials);
	free(record);
	NativeTwin_Free(&src);
	if (!ok)
	{
		memset(out, 0, sizeof(*out));
		out->mesh = -1;
	}
	return ok;
}

// THE TILE LINES of a model (the track's per-model report): the twin's counts
// and, per texture page + CLUT, its triangles and the box of their page texels -
// the VRAM a draw of the import reads. Reads only.
void Engine_ModelLogTiles(const struct Model *model, const char *name)
{
	struct NativeTwinSource src;
	char why[192];
	u32 box[NATIVE_TWIN_TILES_MAX][4];
	u32 t;
	u32 k;

	if (!NativeTwin_FromModel(model, 0, &src, why, sizeof(why)))
	{
		Platform_Log("[CTR Engine] model '%s' tiles: decoder: %s\n", name, why);
		return;
	}
	Platform_Log("[CTR Engine] model '%s' tiles: %u, events textured %u untextured %u semi %u, animtex layouts %u (outside %u) triangles %u\n", name,
	             (unsigned)src.tileCount, (unsigned)src.count.textured, (unsigned)src.count.untextured, (unsigned)src.count.semi,
	             (unsigned)src.count.animatedLayouts, (unsigned)src.count.animatedOutside, (unsigned)src.count.animatedTriangles);
	for (k = 0; k < src.tileCount; k++)
	{
		box[k][0] = box[k][1] = 255u;
		box[k][2] = box[k][3] = 0u;
	}
	for (t = 0; t < src.native.triangleCount; t++)
	{
		const int m = (s16)Rld_ReadLE16(&src.native.triangleMaterials[(size_t)t * 2u]);
		const int tile = src.material[m].textured ? Model_TileOf(&src, t, m) : -1;
		u32 c;

		if (tile < 0)
		{
			continue;
		}
		for (c = 0; c < 3u; c++)
		{
			const u8 *uv = &src.native.uv[(((size_t)t * 3u) + c) * 8u];
			const u32 tex = src.tile[tile].texture;
			const u32 u = Model_TexelOf(Model_ReadF32(uv), src.native.texture[tex].width) - ((u32)src.tile[tile].cellX * (u32)NATIVE_TWIN_TILE_EDGE);
			const u32 v = Model_TexelOf(Model_ReadF32(uv + 4), src.native.texture[tex].height) - ((u32)src.tile[tile].cellY * (u32)NATIVE_TWIN_TILE_EDGE);

			box[tile][0] = (u < box[tile][0]) ? u : box[tile][0];
			box[tile][1] = (v < box[tile][1]) ? v : box[tile][1];
			box[tile][2] = (u > box[tile][2]) ? u : box[tile][2];
			box[tile][3] = (v > box[tile][3]) ? v : box[tile][3];
		}
	}
	for (k = 0; k < src.tileCount; k++)
	{
		const struct NativeTwinTile *tl = &src.tile[k];

		Platform_Log("[CTR Engine] model '%s' tile %u: tpage 0x%04x clut 0x%04x depth %u, page %u,%u clut at %u,%u, triangles %u, texels u %u-%u v %u-%u\n", name,
		             (unsigned)k, (unsigned)tl->tpage, (unsigned)tl->clut, (unsigned)tl->depth, (unsigned)tl->pageX, (unsigned)tl->pageY, (unsigned)tl->clutX,
		             (unsigned)tl->clutY, (unsigned)tl->triangles, (unsigned)box[k][0], (unsigned)box[k][2], (unsigned)box[k][1], (unsigned)box[k][3]);
	}
	NativeTwin_Free(&src);
}

int Engine_ModelFrame(const EngineModelImport *imp, int anim, int frame)
{
	const ModelFrameTable *table;
	const EngineMeshDesc *desc;

	if ((imp == NULL) || (imp->mesh < 0) || (imp->mesh >= ENGINE_MESHES_MAX) || (frame < 0))
	{
		return -1;
	}
	table = &s_modelTables[imp->mesh];
	desc = Engine_MeshDesc(imp->mesh);
	if (!table->used || (desc == NULL) || (desc->frames != (u32)table->frameTotal) || (table->animCount != imp->animCount))
	{
		return -1;
	}
	if (table->animCount == 0)
	{
		return ((anim == 0) && (frame == 0)) ? 0 : -1;
	}
	if ((anim < 0) || (anim >= table->animCount) || (frame >= table->frames[anim]))
	{
		return -1;
	}
	return table->base[anim] + frame;
}

u64 Engine_ModelRangeSkip(int mesh, const struct Model *model)
{
	ModelFrameTable *table;
	const EngineMeshDesc *desc;
	const struct ModelHeader *mh;
	u64 skip = 0;
	int s;

	if ((mesh < 0) || (mesh >= ENGINE_MESHES_MAX))
	{
		return 0;
	}
	table = &s_modelTables[mesh];
	if (!table->used || (table->slotCount <= 0))
	{
		return 0;
	}
	// The mesh the slots were made for (another mesh in this mesh slot has other arrays).
	desc = Engine_MeshDesc(mesh);
	if ((desc == NULL) || (desc->vertices != table->meshVertices) || (desc->ranges != table->meshRanges) || (desc->rangeCount != table->meshRangeCount))
	{
		return 0;
	}
	// Only the imported model's own slots say which layout it shows.
	if ((model == NULL) || (model != table->model) || (model->numHeaders < 1) || (model->headers == NULL) || (model->headers[0].ptrTexLayout == NULL))
	{
		return table->loadSkip;
	}
	mh = &model->headers[0];
	for (s = 0; s < table->slotCount; s++)
	{
		const ModelSlotTable *st = &table->slots[s];
		const uintptr_t now = (uintptr_t)mh->ptrTexLayout[st->slot];
		int variant = 0;
		u32 p;

		for (p = 0; p < st->pointerCount; p++)
		{
			if (st->pointer[p] == now)
			{
				variant = (int)st->variantOf[p];
				break;
			}
		}
		if ((p == st->pointerCount) && !table->strayLogged)
		{
			// Not a pointer the slot can hold (its pointer at the load or an animtex frame): the load's layout.
			table->strayLogged = 1;
			Platform_Log("[CTR Engine] model '%.16s': texture index %d holds a layout outside its animtex list, drawn with the layout of the load\n", model->name,
			             st->slot + 1);
		}
		skip |= st->ranges & ~st->rangesOf[variant];
	}
	return skip;
}

internal int Model_S16(u32 value)
{
	value &= 0xffffu;
	return (value >= 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

u32 Engine_ModelLiveFrame(const EngineModelImport *imp, const struct Model *model, EngineVertex *out, u32 outMax)
{
	const ModelFrameTable *table;
	const EngineMeshDesc *desc;
	const struct ModelHeader *mh;
	const struct ModelFrame *mf;
	const u32 *list;
	const u8 *bytes;
	double unit[3];
	u32 origin;
	u32 records = 0;
	u32 index;
	u32 v;

	if ((imp == NULL) || (imp->mesh < 0) || (imp->mesh >= ENGINE_MESHES_MAX) || (out == NULL) || (model == NULL) || (model->numHeaders < 1) ||
	    (model->headers == NULL))
	{
		return 0;
	}
	table = &s_modelTables[imp->mesh];
	desc = Engine_MeshDesc(imp->mesh);
	mh = &model->headers[0];
	if (!table->used || (table->record == NULL) || (desc == NULL) || (desc->vertices == NULL) || (desc->frames != 1u) ||
	    (desc->verticesPerFrame != table->recordCount) || (table->recordCount > outMax) || (mh->ptrAnimations != NULL) || (mh->ptrFrameData == NULL) ||
	    (mh->unk3 != 0u) || (mh->ptrColors == NULL) || (mh->ptrCommandList == 0u))
	{
		return 0;
	}

	// The colour index of every record as the list names it NOW: the command
	// that decodes the record (bit 26 clear), byte offset (command >> 7) & 0x1fc
	// (RB:2568-2579). A cached command (bit 26) takes the colour of its record.
	// Colour-only commands (upper half 0) are not followed. A list whose record
	// count is not the one of the import is refused.
	list = (const u32 *)(uintptr_t)mh->ptrCommandList;
	for (index = 1; (index <= (u32)NATIVE_TWIN_COMMANDS_MAX) && (list[index] != 0xffffffffu); index++)
	{
		const u32 command = list[index];

		if (((command >> 16) == 0u) || ((command & 0x04000000u) != 0u))
		{
			continue;
		}
		if (records >= table->streamRecords)
		{
			return 0;
		}
		s_modelLiveColour[records++] = (u8)((command >> 9) & 0x7fu);
	}
	if (records != table->streamRecords)
	{
		return 0;
	}

	// The GTE input of a record of a full frame, as the importer's decoder
	// takes it (native_twin.c NativeTwin_PackFull), in model units.
	mf = mh->ptrFrameData;
	bytes = (const u8 *)mf + mf->vertexOffset;
	origin = (u32)(u16)(mf->pos.x & 0x7fff) | ((u32)(u16)mf->pos.y << 16);
	unit[0] = (double)mh->scale.x / 16384.0;
	unit[1] = (double)mh->scale.y / 16384.0;
	unit[2] = (double)(u16)mh->scale.z / 16384.0;
	for (v = 0; v < table->recordCount; v++)
	{
		const u32 r = table->record[v];
		const u8 *b = &bytes[(size_t)r * 3u];
		const u32 word = ((((u32)b[0] | ((u32)b[2] << 16)) + origin) << 2) & MODEL_LIVE_MASK_XY;
		const u32 colour = mh->ptrColors[s_modelLiveColour[r]];
		EngineVertex *o = &out[v];

		// Texcoords and the UV box stay those of the import.
		*o = desc->vertices[v];
		o->position[0] = (float)((double)Model_S16(word) * unit[0]);
		o->position[1] = (float)((double)Model_S16(word >> 16) * unit[1]);
		o->position[2] = (float)((double)Model_S16((u32)((int)mf->pos.z + (int)b[1]) << 2) * unit[2]);
		o->color[0] = (u8)colour;
		o->color[1] = (u8)(colour >> 8);
		o->color[2] = (u8)(colour >> 16);
		o->color[3] = 255;
	}
	return table->recordCount;
}

// --------------------------------------------------------------------------
// THE TRIANGLE SIDES (round 18, for platform/engine_inst_light.c). Per mesh
// triangle of an imported model (mesh order): ENGINE_MODEL_TRI_TWO_SIDED when
// its command has no cull bit (the twin's triangleSide 1, the import keeps it
// once as listed), ENGINE_MODEL_TRI_FLIP when the command it is drawn with
// has bit 29. Those two decide which side of a two-sided triangle retail's
// lit and key writers call the front: signedTest = NCLIP ^ instance bit 15 ^
// command bit 29 (RenderBucket_QueueExecute.c:3199, :3419).

typedef struct
{
	u8 *bits;                     // one per mesh triangle, NULL = none
	u32 count;                    // mesh triangles
	u32 generation;               // Engine_ModelNoteTriangles call that wrote it
	const EngineVertex *vertices; // the mesh's CPU copy it belongs to
} ModelTriangleTable;

global_variable ModelTriangleTable s_modelTriangles[ENGINE_MESHES_MAX];
global_variable u32 s_modelTriangleGeneration = 0;

// The draw command of every event of header 0's list, in event order: the
// walk of the twin (native_twin.c NativeTwin_WalkList; RB:4697-4727 in DrawFunc_Normal) without
// the vertices. A triangle is projected from the third vertex of a strip on;
// the first one of a strip is drawn with the strip's first command, every
// later one with its own. Colour-only commands (upper half 0) are skipped.
// Returns the event count (outMax + 1 when there are more).
internal u32 Model_EventCommands(const struct ModelHeader *mh, u32 *out, u32 outMax)
{
	const u32 *list;
	u32 events = 0;
	u32 stripLength = 0;
	u32 primCommand = 0;
	u32 index;

	if ((mh == NULL) || (mh->ptrCommandList == 0u))
	{
		return 0;
	}
	list = (const u32 *)(uintptr_t)mh->ptrCommandList;
	for (index = 1; (index <= (u32)NATIVE_TWIN_COMMANDS_MAX) && (list[index] != 0xffffffffu); index++)
	{
		const u32 command = list[index];
		const u32 flags = (command >> 24) & 0xffu;

		if ((command >> 16) == 0u)
		{
			continue;
		}
		if (((flags & NATIVE_TWIN_CMD_NEW_STRIP) != 0u) || (stripLength == 0u))
		{
			primCommand = command;
		}
		if ((flags & NATIVE_TWIN_CMD_NEW_STRIP) != 0u)
		{
			stripLength = 0;
		}
		if (stripLength < 2u)
		{
			stripLength++;
			continue;
		}
		if (events >= outMax)
		{
			return outMax + 1u;
		}
		out[events++] = (stripLength == 2u) ? primCommand : command;
		stripLength++;
	}
	return events;
}

void Engine_ModelNoteTriangles(int mesh, const struct Model *model, const struct NativeTwinSource *src, const u32 *order, u32 keptTris)
{
	ModelTriangleTable *table;
	const EngineMeshDesc *desc;
	u32 *commands = NULL;
	u8 *bits = NULL;
	u32 t;

	if ((mesh < 0) || (mesh >= ENGINE_MESHES_MAX))
	{
		return;
	}
	table = &s_modelTriangles[mesh];
	free(table->bits);
	memset(table, 0, sizeof(*table));
	table->generation = ++s_modelTriangleGeneration;
	desc = Engine_MeshDesc(mesh);
	if ((desc == NULL) || (model == NULL) || (model->numHeaders < 1) || (model->headers == NULL) || (src == NULL) || (order == NULL) ||
	    (keptTris == 0u) || (src->count.events == 0u) || (desc->verticesPerFrame != keptTris * 3u))
	{
		return;
	}
	commands = (u32 *)calloc(src->count.events, sizeof(u32));
	bits = (u8 *)calloc(keptTris, 1);
	if ((commands == NULL) || (bits == NULL) || (Model_EventCommands(&model->headers[0], commands, src->count.events) != src->count.events))
	{
		free(commands);
		free(bits);
		return;
	}
	for (t = 0; t < keptTris; t++)
	{
		const u32 tri = order[t];
		const u32 e = src->triangleEvent[tri];

		if ((tri >= src->native.triangleCount) || (e >= src->count.events))
		{
			free(commands);
			free(bits);
			return;
		}
		bits[t] = (u8)(((src->triangleSide[tri] == 1u) ? ENGINE_MODEL_TRI_TWO_SIDED : 0u) |
		               (((commands[e] & NATIVE_TWIN_CMD_FLIP) != 0u) ? ENGINE_MODEL_TRI_FLIP : 0u));
	}
	free(commands);
	table->bits = bits;
	table->count = keptTris;
	table->vertices = desc->vertices;
}

const u8 *Engine_ModelTriangleBits(int mesh, u32 *count, u32 *generation)
{
	const ModelTriangleTable *table;
	const EngineMeshDesc *desc;

	if ((mesh < 0) || (mesh >= ENGINE_MESHES_MAX))
	{
		return NULL;
	}
	table = &s_modelTriangles[mesh];
	desc = Engine_MeshDesc(mesh);
	if ((table->bits == NULL) || (desc == NULL) || (desc->vertices != table->vertices) || (desc->verticesPerFrame != table->count * 3u))
	{
		return NULL;
	}
	if (count != NULL)
	{
		*count = table->count;
	}
	if (generation != NULL)
	{
		*generation = table->generation;
	}
	return table->bits;
}

// --------------------------------------------------------------------------
// SELFTEST: a made-up model in memory (no disc data): 1 header, 2 animations of
// 2 and 3 raw frames, 4 triangles of 3 records each.
//   tri 0: textured, layout 1, tpage A (abr 3 = opaque) clut X, one-sided, bit 29 clear (listed reversed)
//   tri 1: textured, layout 2, tpage A clut X, one-sided with bit 29 (listed as is)
//   tri 2: textured, layout 3, tpage B (abr 1) clut Y, two-sided
//   tri 3: untextured, one-sided, reversed
// Record r of pose f (global number) of animation a: bytes x = 10 + 5r (+ f for
// r = 0), y = 3 + 4r, z = 8 + 2r; origin (100a, 0, 10a); scale (4096, 4096,
// 8192) so the position is (x + ox, z + oy, 2 (oz + y)).

#define MODEL_TEST_TRIS 4
#define MODEL_TEST_RECORDS 12
#define MODEL_TEST_TPAGE_A 0x0062u
#define MODEL_TEST_CLUT_X 0x7814u
#define MODEL_TEST_TPAGE_B 0x00A5u
#define MODEL_TEST_CLUT_Y 0x7D00u

#define MODEL_CHECK(cond, ...)                                                                                                                       \
	do                                                                                                                                           \
	{                                                                                                                                            \
		(*checks)++;                                                                                                                         \
		if (!(cond))                                                                                                                         \
		{                                                                                                                                    \
			failures++;                                                                                                                  \
			printf("engine selftest: model: " __VA_ARGS__);                                                                              \
			printf("\n");                                                                                                                \
		}                                                                                                                                    \
	} while (0)

typedef struct
{
	struct Model model;
	struct ModelHeader header;
	struct ModelAnim *anims[2];
	u8 *animBytes[2];
	struct TextureLayout layouts[3];
	struct TextureLayout *layoutTable[3];
	u32 commands[1 + MODEL_TEST_RECORDS + 1];
	u32 colours[MODEL_TEST_RECORDS];
} ModelTest;

internal u32 ModelTest_Colour(int r)
{
	return (u32)(r * 10 + 1) | ((u32)(r * 20 + 2) << 8) | ((u32)(200 - r) << 16);
}

internal void ModelTest_Layout(struct TextureLayout *l, int u0, int v0, int u1, int v1, int u2, int v2, u16 clut, u16 tpage)
{
	l->u0 = (u8)u0;
	l->v0 = (u8)v0;
	l->clut = clut;
	l->u1 = (u8)u1;
	l->v1 = (u8)v1;
	l->tpage = tpage;
	l->u2 = (u8)u2;
	l->v2 = (u8)v2;
	l->u3 = 0;
	l->v3 = 0;
}

internal int ModelTest_Build(ModelTest *m)
{
	static const int frameCount[2] = {2, 3};
	static const u32 firstFlags[MODEL_TEST_TRIS] = {0x90u, 0xB0u, 0x80u, 0x90u};
	const u32 frameSize = (u32)sizeof(struct ModelFrame) + (MODEL_TEST_RECORDS * 3u);
	int r;
	int a;
	int f;
	int pose = 0;

	memset(m, 0, sizeof(*m));
	memcpy(m->model.name, "model-test", 11);
	m->model.numHeaders = 1;
	m->model.headers = &m->header;
	memcpy(m->header.name, "model-test_hi", 14);
	m->header.scale.x = 4096;
	m->header.scale.y = 4096;
	m->header.scale.z = 8192;

	for (r = 0; r < MODEL_TEST_RECORDS; r++)
	{
		m->colours[r] = ModelTest_Colour(r);
	}
	m->commands[0] = MODEL_TEST_RECORDS;
	for (r = 0; r < MODEL_TEST_RECORDS; r++)
	{
		const int tri = r / 3;
		const u32 flags = ((r % 3) == 0) ? firstFlags[tri] : 0x10u;
		const u32 tex = (tri < 3) ? (u32)(tri + 1) : 0u;

		m->commands[1 + r] = (flags << 24) | ((u32)r << 16) | ((u32)r << 9) | tex;
	}
	m->commands[1 + MODEL_TEST_RECORDS] = 0xffffffffu;

	ModelTest_Layout(&m->layouts[0], 10, 20, 40, 22, 25, 60, MODEL_TEST_CLUT_X, MODEL_TEST_TPAGE_A);
	ModelTest_Layout(&m->layouts[1], 0, 0, 255, 10, 128, 255, MODEL_TEST_CLUT_X, MODEL_TEST_TPAGE_A);
	ModelTest_Layout(&m->layouts[2], 5, 5, 200, 30, 100, 250, MODEL_TEST_CLUT_Y, MODEL_TEST_TPAGE_B);
	for (r = 0; r < 3; r++)
	{
		m->layoutTable[r] = &m->layouts[r];
	}

	for (a = 0; a < 2; a++)
	{
		m->animBytes[a] = (u8 *)calloc(1, sizeof(struct ModelAnim) + ((size_t)frameCount[a] * frameSize) + 4u);
		if (m->animBytes[a] == NULL)
		{
			return 0;
		}
		m->anims[a] = (struct ModelAnim *)m->animBytes[a];
		memcpy(m->anims[a]->name, a ? "second" : "first", a ? 7u : 6u);
		m->anims[a]->numFrames = (u16)frameCount[a];
		m->anims[a]->frameSize = (s16)frameSize;
		m->anims[a]->ptrDeltaArray = NULL;
		for (f = 0; f < frameCount[a]; f++)
		{
			struct ModelFrame *mf = (struct ModelFrame *)(m->animBytes[a] + sizeof(struct ModelAnim) + ((u32)f * frameSize));
			u8 *bytes = (u8 *)mf + sizeof(struct ModelFrame);

			mf->pos.x = (s16)(100 * a);
			mf->pos.y = 0;
			mf->pos.z = (s16)(10 * a);
			mf->vertexOffset = (int)sizeof(struct ModelFrame);
			for (r = 0; r < MODEL_TEST_RECORDS; r++)
			{
				bytes[(r * 3) + 0] = (u8)(10 + (5 * r) + ((r == 0) ? pose : 0));
				bytes[(r * 3) + 1] = (u8)(3 + (4 * r));
				bytes[(r * 3) + 2] = (u8)(8 + (2 * r));
			}
			pose++;
		}
	}

	m->header.ptrCommandList = (u32)(uintptr_t)m->commands;
	m->header.ptrFrameData = NULL;
	m->header.ptrTexLayout = m->layoutTable;
	m->header.ptrColors = m->colours;
	m->header.numAnimations = 2;
	m->header.ptrAnimations = m->anims;
	return 1;
}

// ANIMATED LAYOUTS on the test model: an animtex list in memory as the level
// file has it (AnimTex, its frame pointers, the next AnimTex, ..., a word equal
// to the first AnimTex). Entry e cycles table slot slot[e] through count[e]
// frames. Returns the first AnimTex.
internal struct AnimTex *ModelTest_Animtex(u8 *buffer, ModelTest *m, int entries, const int *slot, const int *count,
                                           const struct TextureLayout *const *const *frames)
{
	u8 *at = buffer;
	int e;

	for (e = 0; e < entries; e++)
	{
		struct AnimTex *a = (struct AnimTex *)at;

		memset(a, 0, sizeof(*a));
		a->ptrActiveTex = (int *)&m->layoutTable[slot[e]];
		a->numFrames = (s16)count[e];
		at += sizeof(struct AnimTex);
		memcpy(at, frames[e], (size_t)count[e] * sizeof(struct TextureLayout *));
		at += (size_t)count[e] * sizeof(struct TextureLayout *);
	}
	*(u32 *)at = (u32)(uintptr_t)buffer;
	return (struct AnimTex *)buffer;
}

// Triangle 1 (texture index 2 = table slot 1, one-sided, listed as is) is cycled
// through the load's layout (another copy), B (other texels and clut) and C
// (another page, abr 1); slot 0 through two copies of its load layout (nothing
// changes, like Papu Pyramid's plant).
internal int ModelTest_AnimatedLayouts(int *checks)
{
	static const int frameAnim[5] = {0, 0, 1, 1, 1};
	static const int recordOf[3] = {3, 4, 5};
	int failures = 0;
	ModelTest *m = (ModelTest *)malloc(sizeof(ModelTest));
	u32 buffer[64];
	struct TextureLayout load1;
	struct TextureLayout b;
	struct TextureLayout c;
	struct TextureLayout load0a;
	struct TextureLayout load0b;
	EngineModelImport imp;
	EngineModelImport plain;

	if ((m == NULL) || !ModelTest_Build(m))
	{
		MODEL_CHECK(0, "animated layouts: test model not built");
		if (m != NULL)
		{
			free(m->animBytes[0]);
			free(m->animBytes[1]);
			free(m);
		}
		return failures;
	}
	load1 = m->layouts[1];
	ModelTest_Layout(&b, 50, 60, 70, 61, 52, 90, 0x7a40u, MODEL_TEST_TPAGE_A);
	ModelTest_Layout(&c, 1, 2, 30, 3, 4, 40, 0x7b00u, 0x0023u); // page 3, abr 1
	load0a = m->layouts[0];
	load0b = m->layouts[0];

	// (a) the plain model: no slots, every range drawn.
	memset(&plain, 0, sizeof(plain));
	MODEL_CHECK(Engine_ModelImport(&m->model, "model-test plain", &plain) && (plain.materialCount == 3) &&
	                (Engine_ModelRangeSkip(plain.mesh, &m->model) == 0u),
	            "animated layouts: a model without animtex has slot ranges (materials %d)", plain.materialCount);

	// (b) only slot 0, whose frames are all its load layout: nothing changes, nothing made.
	{
		const struct TextureLayout *frames0[2] = {&load0a, &load0b};
		const struct TextureLayout *const *frames[1] = {frames0};
		const int slot[1] = {0};
		const int count[1] = {2};
		EngineModelImport same;

		m->header.animtex = ModelTest_Animtex((u8 *)buffer, m, 1, slot, count, frames);
		memset(&same, 0, sizeof(same));
		MODEL_CHECK(Engine_ModelImport(&m->model, "model-test same", &same) && (same.materialCount == 3) &&
		                (Engine_ModelRangeSkip(same.mesh, &m->model) == 0u),
		            "animated layouts: a slot whose layouts are all alike made ranges (materials %d)", same.materialCount);
		Engine_MeshDestroy(same.mesh);
	}

	// (c) slot 1 through the load layout, B and C; slot 0 as in (b).
	{
		const struct TextureLayout *frames1[3] = {&load1, &b, &c};
		const struct TextureLayout *frames0[2] = {&load0a, &load0b};
		const struct TextureLayout *const *frames[2] = {frames1, frames0};
		const int slot[2] = {1, 0};
		const int count[2] = {3, 2};
		const EngineMeshDesc *d;

		m->header.animtex = ModelTest_Animtex((u8 *)buffer, m, 2, slot, count, frames);
		memset(&imp, 0, sizeof(imp));
		if (!Engine_ModelImport(&m->model, "model-test animated", &imp))
		{
			MODEL_CHECK(0, "animated layouts: import failed: %s", Engine_ModelImportWhy());
		}
		else
		{
			d = Engine_MeshDesc(imp.mesh);
			MODEL_CHECK((imp.materialCount == 6) && (d != NULL) && (d->rangeCount == 6) && (d->verticesPerFrame == 18) && (d->frames == 5),
			            "animated layouts: materials %d ranges %u vertices %u", imp.materialCount, d ? d->rangeCount : 0u, d ? d->verticesPerFrame : 0u);
			if ((d != NULL) && (d->rangeCount == 6) && (d->verticesPerFrame == 18) && (d->frames == 5))
			{
				static const int texel[3][3][2] = {{{0, 0}, {255, 10}, {128, 255}}, {{50, 60}, {70, 61}, {52, 90}}, {{1, 2}, {30, 3}, {4, 40}}};
				const EngineMaterial *mat[3];
				int bad = 0;
				int k;
				int fi;

				for (k = 0; k < 6; k++)
				{
					bad += (d->ranges[k].firstIndex != (u32)(3 * k)) || (d->ranges[k].indexCount != 3u) || (d->ranges[k].material != imp.firstMaterial + k);
				}
				MODEL_CHECK(bad == 0, "animated layouts: ranges not one triangle each in key order");
				for (k = 0; k < 3; k++)
				{
					mat[k] = Engine_Material(imp.firstMaterial + 3 + k);
				}
				MODEL_CHECK((mat[0] != NULL) && (mat[1] != NULL) && (mat[2] != NULL), "animated layouts: slot materials registered");
				if ((mat[0] != NULL) && (mat[1] != NULL) && (mat[2] != NULL))
				{
					MODEL_CHECK((mat[0]->tpage == MODEL_TEST_TPAGE_A) && (mat[0]->clut == MODEL_TEST_CLUT_X) && (mat[0]->blend == ENGINE_BLEND_OPAQUE) &&
					                (mat[0]->flags == 0u),
					            "animated layouts: the load's material tpage 0x%x clut 0x%x", mat[0]->tpage, mat[0]->clut);
					MODEL_CHECK((mat[1]->tpage == MODEL_TEST_TPAGE_A) && (mat[1]->clut == 0x7a40u) && (mat[1]->blend == ENGINE_BLEND_OPAQUE),
					            "animated layouts: B's material tpage 0x%x clut 0x%x", mat[1]->tpage, mat[1]->clut);
					MODEL_CHECK((mat[2]->tpage == 0x0023u) && (mat[2]->clut == 0x7b00u) && (mat[2]->blend == ENGINE_BLEND_PSX_ADD) &&
					                (mat[2]->flags == ENGINE_MATERIAL_SEMI_STP),
					            "animated layouts: C's material tpage 0x%x clut 0x%x blend %d flags 0x%x", mat[2]->tpage, mat[2]->clut, mat[2]->blend,
					            mat[2]->flags);
				}
				bad = 0;
				for (fi = 0; fi < 5; fi++)
				{
					int v;

					for (v = 0; v < 3; v++)
					{
						int cc;

						for (cc = 0; cc < 3; cc++)
						{
							const EngineVertex *ev = &d->vertices[((u32)fi * 18u) + 9u + ((u32)v * 3u) + (u32)cc];
							const int r = recordOf[cc];
							const u32 col = ModelTest_Colour(r);

							bad += (ev->texcoord[0] != (float)texel[v][cc][0]) || (ev->texcoord[1] != (float)texel[v][cc][1]);
							bad += (ev->position[0] != (float)(10 + (5 * r) + (100 * frameAnim[fi]))) || (ev->position[1] != (float)(8 + (2 * r))) ||
							       (ev->position[2] != (float)(2 * ((10 * frameAnim[fi]) + 3 + (4 * r))));
							bad += (ev->color[0] != (u8)col) || (ev->color[1] != (u8)(col >> 8)) || (ev->color[2] != (u8)(col >> 16));
						}
					}
				}
				MODEL_CHECK(bad == 0, "animated layouts: %d wrong texel/position/colour value(s) in the slot copies", bad);
			}

			// The draw reads the slot: the load's pointer, B, C, the frame copy of the load's layout, a stray pointer, another model.
			MODEL_CHECK(Engine_ModelRangeSkip(imp.mesh, &m->model) == 0x30u, "animated layouts: skip at the load 0x%llx",
			            (unsigned long long)Engine_ModelRangeSkip(imp.mesh, &m->model));
			m->layoutTable[1] = &b;
			MODEL_CHECK(Engine_ModelRangeSkip(imp.mesh, &m->model) == 0x28u, "animated layouts: skip with B 0x%llx",
			            (unsigned long long)Engine_ModelRangeSkip(imp.mesh, &m->model));
			m->layoutTable[1] = &c;
			MODEL_CHECK(Engine_ModelRangeSkip(imp.mesh, &m->model) == 0x18u, "animated layouts: skip with C 0x%llx",
			            (unsigned long long)Engine_ModelRangeSkip(imp.mesh, &m->model));
			m->layoutTable[1] = &load1;
			MODEL_CHECK(Engine_ModelRangeSkip(imp.mesh, &m->model) == 0x30u, "animated layouts: the frame copy of the load's layout");
			m->layoutTable[1] = &m->layouts[2];
			MODEL_CHECK(Engine_ModelRangeSkip(imp.mesh, &m->model) == 0x30u, "animated layouts: a stray pointer is not the load's layout");
			m->layoutTable[1] = &m->layouts[1];
			MODEL_CHECK((Engine_ModelRangeSkip(imp.mesh, NULL) == 0x30u) && (Engine_ModelRangeSkip(plain.mesh, &m->model) == 0u) &&
			                (Engine_ModelRangeSkip(-1, &m->model) == 0u),
			            "animated layouts: another model or mesh");
			MODEL_CHECK(Engine_RangeSkipped(0x30u, 4u) && !Engine_RangeSkipped(0x30u, 3u) && !Engine_RangeSkipped(~(u64)0, 64u),
			            "animated layouts: Engine_RangeSkipped");
			Engine_MeshDestroy(imp.mesh);
			MODEL_CHECK(Engine_ModelRangeSkip(imp.mesh, &m->model) == 0u, "animated layouts: skip after destroy");
		}
	}

	// (d) refused (slot 1 cycled by two entries): the layout of the load stays, as before.
	{
		const struct TextureLayout *frames1[3] = {&load1, &b, &c};
		const struct TextureLayout *const *frames[2] = {frames1, frames1};
		const int slot[2] = {1, 1};
		const int count[2] = {3, 3};
		EngineModelImport refused;

		m->header.animtex = ModelTest_Animtex((u8 *)buffer, m, 2, slot, count, frames);
		memset(&refused, 0, sizeof(refused));
		MODEL_CHECK(Engine_ModelImport(&m->model, "model-test refused", &refused) && (refused.materialCount == 3) &&
		                (Engine_ModelRangeSkip(refused.mesh, &m->model) == 0u),
		            "animated layouts: a refused model is not imported as before (materials %d)", refused.materialCount);
		Engine_MeshDestroy(refused.mesh);
	}

	Engine_MeshDestroy(plain.mesh);
	m->header.animtex = NULL;
	free(m->animBytes[0]);
	free(m->animBytes[1]);
	free(m);
	return failures;
}

int Engine_SelfTestModel(int *checks)
{
	static const int frameAnim[5] = {0, 0, 1, 1, 1};
	// Corner c of kept triangle t is record recordOf[t][c] (tri 0 and 3 reversed).
	static const int recordOf[MODEL_TEST_TRIS][3] = {{0, 2, 1}, {3, 4, 5}, {6, 7, 8}, {9, 11, 10}};
	// The texels of the layouts in the order the corners are listed.
	static const int texelOf[3][3][2] = {{{10, 20}, {25, 60}, {40, 22}}, {{0, 0}, {255, 10}, {128, 255}}, {{5, 5}, {200, 30}, {100, 250}}};
	int failures = 0;
	ModelTest *m = (ModelTest *)malloc(sizeof(ModelTest));
	EngineModelImport imp;
	EngineModelImport none;
	struct Model empty;
	const EngineMeshDesc *d;

	if (m == NULL)
	{
		MODEL_CHECK(0, "no memory");
		return failures;
	}
	memset(&imp, 0, sizeof(imp));
	imp.mesh = 7;
	MODEL_CHECK(!Engine_ModelImport(NULL, "x", &imp) && (imp.mesh == -1), "NULL model refused");
	MODEL_CHECK(!Engine_ModelImport(&m->model, "x", NULL), "NULL out refused");
	memset(&empty, 0, sizeof(empty));
	MODEL_CHECK(!Engine_ModelImport(&empty, "x", &imp) && (imp.mesh == -1), "model without headers refused");

	if (!ModelTest_Build(m))
	{
		MODEL_CHECK(0, "test model not built");
		free(m->animBytes[0]);
		free(m->animBytes[1]);
		free(m);
		return failures;
	}

	memset(&imp, 0, sizeof(imp));
	if (!Engine_ModelImport(&m->model, "model-test", &imp))
	{
		MODEL_CHECK(0, "import failed");
	}
	else
	{
		int tri;
		int c;
		int fi;

		MODEL_CHECK((imp.mesh >= 0) && (imp.headerCount == 1) && (imp.animCount == 2) && (imp.frameTotal == 5) && (imp.materialCount == 3),
		            "import result mesh %d headers %d anims %d frames %d materials %d", imp.mesh, imp.headerCount, imp.animCount, imp.frameTotal,
		            imp.materialCount);
		d = Engine_MeshDesc(imp.mesh);
		MODEL_CHECK((d != NULL) && (d->frames == 5) && (d->verticesPerFrame == 12) && (d->indexCount == 12) && (d->rangeCount == 3),
		            "mesh shape frames %u vertices %u indices %u ranges %u", d ? d->frames : 0u, d ? d->verticesPerFrame : 0u, d ? d->indexCount : 0u,
		            d ? d->rangeCount : 0u);
		if ((d != NULL) && (d->frames == 5) && (d->verticesPerFrame == 12) && (d->rangeCount == 3))
		{
			const EngineMaterial *mat[3];
			int k;

			for (k = 0; k < 3; k++)
			{
				mat[k] = Engine_Material(imp.firstMaterial + k);
			}
			MODEL_CHECK((mat[0] != NULL) && (mat[1] != NULL) && (mat[2] != NULL), "materials registered");
			if ((mat[0] != NULL) && (mat[1] != NULL) && (mat[2] != NULL))
			{
				MODEL_CHECK((mat[0]->texSource == ENGINE_TEX_VRAM) && (mat[0]->blend == ENGINE_BLEND_OPAQUE) && (mat[0]->tpage == MODEL_TEST_TPAGE_A) &&
				                (mat[0]->clut == MODEL_TEST_CLUT_X) && (mat[0]->flags == 0u),
				            "material 0 (opaque page A) source %d blend %d tpage 0x%x clut 0x%x flags 0x%x", mat[0]->texSource, mat[0]->blend, mat[0]->tpage,
				            mat[0]->clut, mat[0]->flags);
				MODEL_CHECK((mat[1]->texSource == ENGINE_TEX_VRAM) && (mat[1]->blend == ENGINE_BLEND_PSX_ADD) && (mat[1]->tpage == MODEL_TEST_TPAGE_B) &&
				                (mat[1]->clut == MODEL_TEST_CLUT_Y) && (mat[1]->flags == (ENGINE_MATERIAL_SEMI_STP | ENGINE_MATERIAL_DOUBLE_SIDED)),
				            "material 1 (semi page B) source %d blend %d tpage 0x%x clut 0x%x flags 0x%x", mat[1]->texSource, mat[1]->blend, mat[1]->tpage,
				            mat[1]->clut, mat[1]->flags);
				MODEL_CHECK((mat[2]->texSource == ENGINE_TEX_NONE) && (mat[2]->blend == ENGINE_BLEND_OPAQUE) && (mat[2]->flags == 0u),
				            "material 2 (untextured) source %d blend %d flags 0x%x", mat[2]->texSource, mat[2]->blend, mat[2]->flags);
			}
			MODEL_CHECK((d->ranges[0].material == imp.firstMaterial) && (d->ranges[0].firstIndex == 0) && (d->ranges[0].indexCount == 6) &&
			                (d->ranges[1].material == imp.firstMaterial + 1) && (d->ranges[1].firstIndex == 6) && (d->ranges[1].indexCount == 3) &&
			                (d->ranges[2].material == imp.firstMaterial + 2) && (d->ranges[2].firstIndex == 9) && (d->ranges[2].indexCount == 3),
			            "ranges by material");

			for (fi = 0; fi < 5; fi++)
			{
				for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
				{
					for (c = 0; c < 3; c++)
					{
						const EngineVertex *v = &d->vertices[((u32)fi * 12u) + ((u32)tri * 3u) + (u32)c];
						const int r = recordOf[tri][c];
						const int a = frameAnim[fi];
						const float ex = (float)(10 + (5 * r) + ((r == 0) ? fi : 0) + (100 * a));
						const float ey = (float)(8 + (2 * r));
						const float ez = (float)(2 * ((10 * a) + 3 + (4 * r)));

						MODEL_CHECK((v->position[0] == ex) && (v->position[1] == ey) && (v->position[2] == ez),
						            "frame %d tri %d corner %d position %g %g %g, wanted %g %g %g", fi, tri, c, v->position[0], v->position[1],
						            v->position[2], ex, ey, ez);
					}
				}
			}
			for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
			{
				for (c = 0; c < 3; c++)
				{
					const EngineVertex *v = &d->vertices[((u32)tri * 3u) + (u32)c];
					const u32 col = ModelTest_Colour(recordOf[tri][c]);
					const int ou = (tri < 3) ? texelOf[tri][c][0] : 0;
					const int ov = (tri < 3) ? texelOf[tri][c][1] : 0;

					MODEL_CHECK((v->texcoord[0] == (float)ou) && (v->texcoord[1] == (float)ov), "tri %d corner %d texel %g %g, wanted %d %d", tri, c,
					            v->texcoord[0], v->texcoord[1], ou, ov);
					MODEL_CHECK((v->color[0] == (u8)col) && (v->color[1] == (u8)(col >> 8)) && (v->color[2] == (u8)(col >> 16)) && (v->color[3] == 255),
					            "tri %d corner %d colour %u %u %u %u", tri, c, v->color[0], v->color[1], v->color[2], v->color[3]);
				}
			}
			// THE UV BOX: the box of the three corners of a textured triangle on every
			// vertex of it and in every frame; the whole page for the untextured one.
			for (fi = 0; fi < 5; fi++)
			{
				for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
				{
					int box[4] = {0, 0, 255, 255};

					if (tri < 3)
					{
						box[0] = box[2] = texelOf[tri][0][0];
						box[1] = box[3] = texelOf[tri][0][1];
						for (c = 1; c < 3; c++)
						{
							box[0] = (texelOf[tri][c][0] < box[0]) ? texelOf[tri][c][0] : box[0];
							box[1] = (texelOf[tri][c][1] < box[1]) ? texelOf[tri][c][1] : box[1];
							box[2] = (texelOf[tri][c][0] > box[2]) ? texelOf[tri][c][0] : box[2];
							box[3] = (texelOf[tri][c][1] > box[3]) ? texelOf[tri][c][1] : box[3];
						}
					}
					for (c = 0; c < 3; c++)
					{
						const EngineVertex *v = &d->vertices[((u32)fi * 12u) + ((u32)tri * 3u) + (u32)c];
						int got[4];

						Engine_VertexBoxGet(v, got);
						MODEL_CHECK((got[0] == box[0]) && (got[1] == box[1]) && (got[2] == box[2]) && (got[3] == box[3]),
						            "frame %d tri %d corner %d uv box %d %d %d %d, wanted %d %d %d %d", fi, tri, c, got[0], got[1], got[2], got[3], box[0],
						            box[1], box[2], box[3]);
					}
				}
			}
			// The stored form: maxima as 255 - max, so a zeroed vertex (a writer that
			// set no box) is the whole page, and the whole page is stored as zeros.
			{
				EngineVertex z;
				int got[4];

				memset(&z, 0, sizeof(z));
				Engine_VertexBoxGet(&z, got);
				MODEL_CHECK((got[0] == 0) && (got[1] == 0) && (got[2] == 255) && (got[3] == 255), "a zeroed vertex's box is %d %d %d %d, not the whole page",
				            got[0], got[1], got[2], got[3]);
				Engine_VertexSetBox(&z, 1, 10, 20, 40, 60);
				MODEL_CHECK((z.uvBox[0] == 10) && (z.uvBox[1] == 20) && (z.uvBox[2] == 215) && (z.uvBox[3] == 195), "box 10 20 40 60 stored as %u %u %u %u",
				            z.uvBox[0], z.uvBox[1], z.uvBox[2], z.uvBox[3]);
				Engine_VertexBoxFull(&z, 1);
				MODEL_CHECK((z.uvBox[0] == 0) && (z.uvBox[1] == 0) && (z.uvBox[2] == 0) && (z.uvBox[3] == 0), "the whole page not stored as zeros");
			}
			{
				int identity = 1;
				u32 i;

				for (i = 0; i < d->indexCount; i++)
				{
					identity = identity && (d->indices[i] == i);
				}
				MODEL_CHECK(identity, "indices run 0..11");
			}
		}

		MODEL_CHECK((Engine_ModelFrame(&imp, 0, 0) == 0) && (Engine_ModelFrame(&imp, 0, 1) == 1) && (Engine_ModelFrame(&imp, 1, 0) == 2) &&
		                (Engine_ModelFrame(&imp, 1, 2) == 4),
		            "Engine_ModelFrame in range");
		MODEL_CHECK((Engine_ModelFrame(&imp, 0, 2) == -1) && (Engine_ModelFrame(&imp, 1, 3) == -1) && (Engine_ModelFrame(&imp, 2, 0) == -1) &&
		                (Engine_ModelFrame(&imp, -1, 0) == -1) && (Engine_ModelFrame(&imp, 0, -1) == -1) && (Engine_ModelFrame(NULL, 0, 0) == -1),
		            "Engine_ModelFrame out of range");
		memset(&none, 0, sizeof(none));
		none.mesh = -1;
		MODEL_CHECK(Engine_ModelFrame(&none, 0, 0) == -1, "Engine_ModelFrame of a failed import");

		// THE LIVE FRAME. An animated model has none; the same model made still
		// (pose 0 as its frame) has one that equals the import while nothing
		// changed, and follows a rewrite of one height byte, the colour indices
		// of the list and the colour table the way the start banner's thread
		// rewrites them (game/231/RB_Banner.c).
		{
			EngineVertex live[MODEL_TEST_RECORDS];
			EngineModelImport still;
			int r;

			MODEL_CHECK(Engine_ModelLiveFrame(&imp, &m->model, live, MODEL_TEST_RECORDS) == 0u, "live frame of an animated model refused");
			m->header.numAnimations = 0;
			m->header.ptrAnimations = NULL;
			m->header.ptrFrameData = (struct ModelFrame *)(m->animBytes[0] + sizeof(struct ModelAnim));
			memset(&still, 0, sizeof(still));
			if (!Engine_ModelImport(&m->model, "model-test still", &still) || (Engine_MeshDesc(still.mesh) == NULL))
			{
				MODEL_CHECK(0, "still import failed");
			}
			else
			{
				const EngineMeshDesc *sd = Engine_MeshDesc(still.mesh);
				u8 *bytes = (u8 *)m->header.ptrFrameData + m->header.ptrFrameData->vertexOffset;

				MODEL_CHECK((still.animCount == 0) && (sd->frames == 1u) && (sd->verticesPerFrame == 12u), "still import shape");
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 12u, "live frame of the still model");
				MODEL_CHECK(memcmp(live, sd->vertices, sizeof(live)) == 0, "live frame of unchanged data equals the import");
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS - 1) == 0u, "live frame larger than the room refused");

				bytes[(4 * 3) + 1] = 50; // record 4: height byte
				for (r = 0; r < MODEL_TEST_RECORDS; r++)
				{
					m->commands[1 + r] = (m->commands[1 + r] & 0xffff01ffu) | ((u32)(MODEL_TEST_RECORDS - 1 - r) << 9);
				}
				m->colours[0] = 0x00303132u;
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 12u, "live frame after the rewrite");
				for (tri = 0; tri < MODEL_TEST_TRIS; tri++)
				{
					for (c = 0; c < 3; c++)
					{
						const EngineVertex *v = &live[(tri * 3) + c];
						const EngineVertex *s = &sd->vertices[(tri * 3) + c];
						const int rec = recordOf[tri][c];
						const int ci = MODEL_TEST_RECORDS - 1 - rec;
						const u32 col = (ci == 0) ? 0x00303132u : ModelTest_Colour(ci);
						const float ez = (rec == 4) ? 100.0f : s->position[2];

						MODEL_CHECK((v->position[0] == s->position[0]) && (v->position[1] == s->position[1]) && (v->position[2] == ez) &&
						                (v->texcoord[0] == s->texcoord[0]) && (v->texcoord[1] == s->texcoord[1]),
						            "live tri %d corner %d position %g %g %g, wanted z %g", tri, c, v->position[0], v->position[1], v->position[2], ez);
						MODEL_CHECK((v->color[0] == (u8)col) && (v->color[1] == (u8)(col >> 8)) && (v->color[2] == (u8)(col >> 16)) && (v->color[3] == 255),
						            "live tri %d corner %d colour %u %u %u", tri, c, v->color[0], v->color[1], v->color[2]);
						MODEL_CHECK(memcmp(v->uvBox, s->uvBox, sizeof(v->uvBox)) == 0, "live tri %d corner %d uv box %u %u %u %u, the import's %u %u %u %u", tri,
						            c, v->uvBox[0], v->uvBox[1], v->uvBox[2], v->uvBox[3], s->uvBox[0], s->uvBox[1], s->uvBox[2], s->uvBox[3]);
					}
				}
				m->commands[1 + MODEL_TEST_RECORDS - 1] = 0xffffffffu; // one record fewer than imported
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 0u, "live frame of a list with other records refused");
				Engine_MeshDestroy(still.mesh);
				MODEL_CHECK(Engine_ModelLiveFrame(&still, &m->model, live, MODEL_TEST_RECORDS) == 0u, "live frame after destroy");
			}
		}

		Engine_MeshDestroy(imp.mesh);
		MODEL_CHECK(Engine_ModelFrame(&imp, 0, 0) == -1, "Engine_ModelFrame after destroy");
	}
	failures += ModelTest_AnimatedLayouts(checks);

	free(m->animBytes[0]);
	free(m->animBytes[1]);
	free(m);
	// Model runs last in Engine_SelfTest: nothing after it reads the table.
	Engine_MaterialsReset();
	return failures;
}

// THE TRIANGLE SIDES on the made-up model (called by Engine_SelfTestInstLight):
// the event walk on a list with a colour-only command and a longer strip, the
// bits of the four kept triangles (tri 1 has bit 29, tri 2 is two-sided),
// the same model again with bit 29 on tri 2, and no bits after the destroy.
int Engine_SelfTestModelTriangles(int *checks)
{
	// strip A B C (one event, drawn with A), a colour-only word, strip D E F G
	// (two events: D, then G's own command).
	static const u32 list[] = {4u, 0x80010001u, 0x00020002u, 0x00000123u, 0x20030003u, 0x90040004u, 0x00050005u, 0x00060006u, 0x20070007u, 0xffffffffu};
	static const u8 wanted[2][MODEL_TEST_TRIS] = {
	    {0u, ENGINE_MODEL_TRI_FLIP, ENGINE_MODEL_TRI_TWO_SIDED, 0u},
	    {0u, ENGINE_MODEL_TRI_FLIP, ENGINE_MODEL_TRI_TWO_SIDED | ENGINE_MODEL_TRI_FLIP, 0u},
	};
	int failures = 0;
	ModelTest *m = (ModelTest *)malloc(sizeof(ModelTest));
	struct ModelHeader header;
	u32 commands[4];
	u32 generation[2] = {0u, 0u};
	int pass;

	memset(&header, 0, sizeof(header));
	header.ptrCommandList = (u32)(uintptr_t)list;
	MODEL_CHECK(Model_EventCommands(&header, commands, 4u) == 3u, "event walk: %u events, wanted 3", (unsigned)Model_EventCommands(&header, commands, 4u));
	MODEL_CHECK((commands[0] == 0x80010001u) && (commands[1] == 0x90040004u) && (commands[2] == 0x20070007u),
	            "event walk commands 0x%08x 0x%08x 0x%08x", (unsigned)commands[0], (unsigned)commands[1], (unsigned)commands[2]);
	MODEL_CHECK(Model_EventCommands(&header, commands, 2u) == 3u, "event walk with too small room reports more");
	MODEL_CHECK(Engine_ModelTriangleBits(-1, NULL, NULL) == NULL, "bits of mesh -1");

	if ((m == NULL) || !ModelTest_Build(m))
	{
		MODEL_CHECK(0, "test model not built");
		if (m != NULL)
		{
			free(m->animBytes[0]);
			free(m->animBytes[1]);
			free(m);
		}
		return failures;
	}
	for (pass = 0; pass < 2; pass++)
	{
		EngineModelImport imp;
		const u8 *bits;
		u32 count = 0;
		int t;

		if (pass == 1)
		{
			m->commands[1 + 6] |= NATIVE_TWIN_CMD_FLIP; // tri 2's first command
		}
		memset(&imp, 0, sizeof(imp));
		if (!Engine_ModelImport(&m->model, "model-test sides", &imp))
		{
			MODEL_CHECK(0, "pass %d: import failed", pass);
			continue;
		}
		bits = Engine_ModelTriangleBits(imp.mesh, &count, &generation[pass]);
		MODEL_CHECK((bits != NULL) && (count == MODEL_TEST_TRIS), "pass %d: bits %p count %u", pass, (const void *)bits, (unsigned)count);
		if ((bits != NULL) && (count == MODEL_TEST_TRIS))
		{
			for (t = 0; t < MODEL_TEST_TRIS; t++)
			{
				MODEL_CHECK(bits[t] == wanted[pass][t], "pass %d: tri %d bits 0x%x, wanted 0x%x", pass, t, bits[t], wanted[pass][t]);
			}
		}
		Engine_MeshDestroy(imp.mesh);
		MODEL_CHECK(Engine_ModelTriangleBits(imp.mesh, NULL, NULL) == NULL, "pass %d: no bits after the destroy", pass);
	}
	MODEL_CHECK(generation[1] != generation[0], "a new import gives a new generation");
	free(m->animBytes[0]);
	free(m->animBytes[1]);
	free(m);
	Engine_MaterialsReset();
	return failures;
}
