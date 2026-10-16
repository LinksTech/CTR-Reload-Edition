#include <platform/engine.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE TRACK MOSAIC - the near textures of the engine track (--engine-track,
// round of 2026-10-15). See include/platform/engine.h, MOSAIC.
//
// WHAT. Most mid faces of a retail track carry, in level 3 of their IconGroup4,
// a pointer to a mosaic table instead of a layout (engine_track.c,
// EngTrack_FinestLayout). Near the camera the PS1 path cuts such a face into
// sub-quads and gives each its own 12-byte layout from that table
// (226_00_DrawLevelOvr1P.c, DrawLevelOvr1P_PrepareDeepestMosaicUv ~1193):
// tpage, clut and UVs per sub-quad. The engine does not cut the face. Every
// face keeps its two triangles; the fragment program "ne" picks the tile from
// the position inside the face and reads the tile's layout from a small data
// texture (slot 3), built here once per load.
//
// THE TABLE. An array of TextureLayouts, row-major, four tiles wide:
//   form 4x4 (leaf flag 4X4, DYNAMIC_SUBDIV or none): 16 tiles, 4 rows
//   form 4x2 (leaf flag 4X2):                          8 tiles, 2 rows
//   form 4x1 (leaf flag 4X1):                          4 tiles, 1 row
// followed by the same number of entries for a mirrored face (bit 23 of the
// face's scratch-table word): the grid reversed in each row, every tile with
// its corners swapped - stored that way, so it is copied as it is. The entry of
// a sub-quad is slot(frame 1) * 2 + slot(frame 2) of the PS1 recursion
// (~1228), the slot words of the 4x4 helpers being 0/0xc/0x30/0x3c (~1260):
// entry 4 * row + column. Tables lie back to back in the level, so a row reads
// exactly its own size. Checked offline on all 18 retail race tracks
// (tmp\engine\r15\p1\scan, 2026-10-14): sizes, arrangement and mirror for the
// three forms, against the level-2 picture of the same face.
//
// THE FACE'S OWN SPACE. The PS1 builds every child grid from the parent's
// corners in slot order (~10110): slot 0 at (0, 0), slot 1 at (1, 0), slot 2 at
// (0, 1), slot 3 at (1, 1). In that space tile (c, r) covers [c/4, (c+1)/4] x
// [r/4, (r+1)/4] and its uv0..uv3 sit on its own corners in the same order.
//
// THE DATA TEXTURE. RGBA8, nearest, EGM_ROW_TEXELS wide, one row per distinct
// (table, mirrored, form):
//   texel 0       header: r = cells per side (4, or 1 for a whole-face row),
//                 g = form, b = abr of the tiles, a = 0
//   texels 1..48  16 cells x 3 texels, the 12 bytes of the layout as they are:
//                 [u0 v0 clut.lo clut.hi] [u1 v1 tpage.lo tpage.hi] [u2 v2 u3 v3]
//   texels 49..57 the group's levels 2, 1 and 0 (3 texels each): the fallback
//                 when a tile texel is smaller than a pixel, then a level-2
//                 texel, and so on - picked by the screen derivative, like mip
//                 levels, not by the PS1's distance thresholds
// A 4x2 or 4x1 table is spread over the 16 cells at load (cell (c, r) = entry
// 4 * (r >> 1) + c, or entry c), so the program knows one grid only.
//
// THE VERTEX. A mosaic face keeps the vertex layout: texcoord = (s + 2 + 4a,
// t + 2 + 4b) with (s, t) the slot position of the vertex and row = a + 64 b
// (a, b 0..63). The values stay below 256, the range of the VRAM UVs before,
// so the interpolation is as precise as it was. EngMosaic_Decode is the C copy
// of the program's decode; the selftest holds the two together. The vertex
// alpha carries the face size (EngMosaic_FaceSizeCode) for the level choice.
//
// BLEND. The PS1 takes the semi-transparent packet code and the blend mode from
// the tile (~2504 reads the tpage the mosaic path has just loaded). A material
// has one blend mode, so a face is keyed with the abr of its row's tiles when
// they all agree; a row whose tiles disagree keeps level 2 (counted).
//
// ANIMATED TEXTURES (round of 2026-10-16): LAYOUT SOURCE = THE SLOT, READ AT
// DRAW TIME - the one rule for track faces and models (engine_model.c). A mid
// face whose texture pointer has bit 0 set names an AnimTex cell (ptr - 1 is
// the AnimTex; its first word holds the IconGroup4 shown now, which
// CTR_CycleTex_LEV moves every frame, game/CTR/CTR_CycleTex.c; the 226 walk
// reads it at the draw). Such a face, mosaic or not, reads an ANIMATED ROW
// keyed by (cell, mirror, form) that holds what the group shown now gives under
// the rule of a face of the load (EngMosaic_AnimFill): a group whose level 3 is
// a mosaic table reference gives the table's cells (side 4, the leaf's form,
// the face's mirror), any other group - or a table whose tiles disagree in abr -
// its finest layout as one cell (side 1); plus its levels 2, 1, 0. Once per
// frame, after the track is collected, Engine_TrackMosaicFrame reads every cell
// and rewrites the rows whose group changed (one row of the data texture each;
// the backend records the copy into the frame between passes,
// NativeVk_RecordUploadInFrame). The blend follows too: the face is keyed for
// every abr the cell's frames give (Engine_TrackMosaicAnimFace, *abrSet), and
// the track draws it each frame with the material of the abr its row holds now
// (Engine_TrackMosaicAnimAbr; engine_track.c, ANIMATED ROWS).
//
// DRAW TIME ONLY. Nothing here writes game state: the tables are read at load
// stage 8 and the AnimTex cells once per frame, from the same level memory the
// PS1 draw reads.

#define EGM_ROWS_MAX 4096
#define EGM_ROW_TEXELS 64
#define EGM_CELL_TEXEL 1
#define EGM_LEVEL_TEXEL 49 // level 2, then level 1, then level 0, 3 texels each
#define EGM_CELLS 16

// A layout is three texels; the three levels end inside the row.
_Static_assert(sizeof(struct TextureLayout) == 12, "TextureLayout must be 12 bytes (three RGBA8 texels)");
_Static_assert(EGM_LEVEL_TEXEL + 9 <= EGM_ROW_TEXELS, "the levels must fit into a table row");

// --------------------------------------------------------------------------
// Pure data side (no game state): the part the selftest checks.

typedef struct
{
	u32 group; // address of the IconGroup4 the row was made for (the key of a table row)
	u32 table; // address of its mosaic table (0 for a whole-face row)
	u32 animCell; // AnimTex cell of a whole-face row (its key), 0 for a table row
	u32 shown; // the IconGroup4 a whole-face row holds now
	u8 mirrored;
	u8 form;   // ENGINE_MOSAIC_FORM_*
	u8 side;   // cells per side: 4, or 1 for a whole-face row
	s8 abr;    // abr of every own tile, -1 when they disagree
	u8 faulted; // --engine-oracle-fault cell swapped two of its cells (Engine_TrackMosaicOracleFault)
	u8 abrLevels; // abr -1 because the tiles agree with each other but not with a usable level 0..2
	struct TextureLayout cell[EGM_CELLS];
	struct TextureLayout level[3]; // the group's far, middle and near layouts (LOD fallback)
} EngMosaicRow;

typedef struct
{
	EngMosaicRow *rows;
	int count;
	int cap;
} EngMosaicRowSet;

// The tiles a table holds for one orientation: 16, 8 or 4; 0 for an unknown form.
internal int EngMosaic_OwnEntries(int form)
{
	switch (form)
	{
	case ENGINE_MOSAIC_FORM_4X4:
		return 16;
	case ENGINE_MOSAIC_FORM_4X2:
		return 8;
	case ENGINE_MOSAIC_FORM_4X1:
		return 4;
	default:
		return 0;
	}
}

// The table entry of cell (c, r) of the 4x4 grid, -1 for an unknown form.
internal int EngMosaic_CellEntry(int form, int mirrored, int c, int r)
{
	const int own = EngMosaic_OwnEntries(form);
	const int base = mirrored ? own : 0;

	if ((own == 0) || (c < 0) || (c > 3) || (r < 0) || (r > 3))
	{
		return -1;
	}
	switch (form)
	{
	case ENGINE_MOSAIC_FORM_4X4:
		return base + 4 * r + c;
	case ENGINE_MOSAIC_FORM_4X2:
		return base + 4 * (r >> 1) + c;
	default:
		return base + c;
	}
}

internal int EngMosaic_Abr(const struct TextureLayout *lay)
{
	return (lay->tpage >> 5) & 3;
}

// A layout as a face of the given mirror sees it in its own space. Only the
// table is stored mirrored; any other layout is not: a mirrored face takes its
// UVs in the order {uv1, uv0, uv3, uv2} (engine_track.c, EngTrack_BuildBlock),
// which in the face's own space is the layout with its corners swapped so.
internal void EngMosaic_MirrorCopy(struct TextureLayout *dst, const struct TextureLayout *src, int mirrored)
{
	*dst = *src;
	if (mirrored)
	{
		dst->u0 = src->u1;
		dst->v0 = src->v1;
		dst->u1 = src->u0;
		dst->v1 = src->v0;
		dst->u2 = src->u3;
		dst->v2 = src->v3;
		dst->u3 = src->u2;
		dst->v3 = src->v2;
	}
}

// Room for one more row (the set grows). 0 when it is full or out of memory.
internal int EngMosaic_RowRoom(EngMosaicRowSet *set)
{
	if (set->count >= EGM_ROWS_MAX)
	{
		return 0;
	}
	if (set->count >= set->cap)
	{
		const int cap = (set->cap == 0) ? 64 : (set->cap * 2);
		EngMosaicRow *p = (EngMosaicRow *)realloc(set->rows, (size_t)cap * sizeof(EngMosaicRow));

		if (p == NULL)
		{
			return 0;
		}
		set->rows = p;
		set->cap = cap;
	}
	return 1;
}

// Fills a row from a table (both halves readable: 2 * own entries). Returns 0
// for an unknown form.
internal int EngMosaic_RowFill(EngMosaicRow *row, u32 groupAddress, u32 tableAddress, const struct TextureLayout *table, int mirrored, int form,
                               const struct TextureLayout *levels)
{
	int c;
	int r;
	int i;

	if (EngMosaic_OwnEntries(form) == 0)
	{
		return 0;
	}
	memset(row, 0, sizeof(*row));
	row->group = groupAddress;
	row->table = tableAddress;
	row->mirrored = (u8)(mirrored ? 1 : 0);
	row->form = (u8)form;
	row->side = 4;
	for (r = 0; r < 4; r++)
	{
		for (c = 0; c < 4; c++)
		{
			row->cell[4 * r + c] = table[EngMosaic_CellEntry(form, mirrored, c, r)];
		}
	}
	for (i = 0; i < 3; i++)
	{
		EngMosaic_MirrorCopy(&row->level[i], &levels[i], mirrored);
	}
	row->abr = (s8)EngMosaic_Abr(&row->cell[0]);
	for (i = 1; i < EGM_CELLS; i++)
	{
		if (EngMosaic_Abr(&row->cell[i]) != row->abr)
		{
			row->abr = -1;
			break;
		}
	}
	// The row's material blends its levels too (the shader's fallback for small texels): a
	// level 0..2 with another abr than the tiles makes the row mixed as well. A blank level
	// (not usable, never a texture) does not count.
	for (i = 0; (row->abr >= 0) && (i < 3); i++)
	{
		if (EngTrack_LayoutUsable(&levels[i]) && (EngMosaic_Abr(&levels[i]) != row->abr))
		{
			row->abr = -1;
			row->abrLevels = 1;
		}
	}
	return 1;
}

// The row of (group, mirrored, form): an existing one, or a new one filled by
// EngMosaic_RowFill. Keyed by the group, not the table: the row also holds the
// group's level 2 (one table per group on every retail track, but nothing in
// the format says so). Returns the index, -1 when the set is full or out of
// memory, -2 for an unknown form. *made = 1 when the row is new.
internal int EngMosaic_RowFind(EngMosaicRowSet *set, u32 groupAddress, u32 tableAddress, const struct TextureLayout *table, int mirrored, int form,
                               const struct TextureLayout *levels, int *made)
{
	int i;

	if (made != NULL)
	{
		*made = 0;
	}
	if (EngMosaic_OwnEntries(form) == 0)
	{
		return -2;
	}
	for (i = 0; i < set->count; i++)
	{
		const EngMosaicRow *row = &set->rows[i];

		if ((row->animCell == 0u) && (row->group == groupAddress) && (row->mirrored == (u8)(mirrored ? 1 : 0)) && (row->form == (u8)form))
		{
			return i;
		}
	}
	if (!EngMosaic_RowRoom(set))
	{
		return -1;
	}
	EngMosaic_RowFill(&set->rows[set->count], groupAddress, tableAddress, table, mirrored, form, levels);
	if (made != NULL)
	{
		*made = 1;
	}
	return set->count++;
}

// A whole-face row for the IconGroup4 group: its finest layout (finest, one of
// group's) in every cell, one cell per side, and its levels 2, 1, 0; all with
// the corners of a mirrored face swapped. The key fields stay as they are.
internal void EngMosaic_WholeFaceFill(EngMosaicRow *row, u32 groupAddress, const struct TextureLayout *group, const struct TextureLayout *finest)
{
	struct TextureLayout lay;
	int i;

	EngMosaic_MirrorCopy(&lay, finest, row->mirrored);
	for (i = 0; i < EGM_CELLS; i++)
	{
		row->cell[i] = lay;
	}
	for (i = 0; i < 3; i++)
	{
		EngMosaic_MirrorCopy(&row->level[i], &group[i], row->mirrored);
	}
	row->shown = groupAddress;
}

// An animated row (its key fields animCell, mirrored, form set) filled for the
// IconGroup4 group shown now, by the rule of a face of the load: when level 3 of
// group is a mosaic table reference (EngTrack_FinestLayout with isLevelPtr) whose
// both orientations spanOk vouches for and whose own tiles share one abr, the
// table's cells (side 4, as EngMosaic_RowFill); otherwise the finest layout as
// one cell (side 1, EngMosaic_WholeFaceFill: level 2 for a mosaic word, as a
// face the table refused). The key fields stay. 0 = no usable layout (row unchanged).
internal int EngMosaic_AnimFill(EngMosaicRow *row, u32 groupAddress, const struct TextureLayout *group, int (*spanOk)(u32 p, u32 size),
                                int (*isLevelPtr)(u32 word))
{
	const struct TextureLayout *finest;
	int mosaic = 0;
	u8 levelsDisagree = 0;

	finest = EngTrack_FinestLayout(group, isLevelPtr, &mosaic);
	if (mosaic)
	{
		const int own = EngMosaic_OwnEntries(row->form);
		u32 word = 0;

		memcpy(&word, &group[3], sizeof(word));
		if ((own > 0) && (word != 0u) && ((spanOk == NULL) || spanOk(word, (u32)(2 * own) * (u32)sizeof(struct TextureLayout))) &&
		    EngTrack_TableUsable((const struct TextureLayout *)(size_t)word, 2 * own))
		{
			EngMosaicRow table;

			EngMosaic_RowFill(&table, groupAddress, word, (const struct TextureLayout *)(size_t)word, row->mirrored, row->form, group);
			levelsDisagree = table.abrLevels;
			if (table.abr >= 0)
			{
				memcpy(row->cell, table.cell, sizeof(row->cell));
				memcpy(row->level, table.level, sizeof(row->level));
				row->abrLevels = 0;
				row->table = word;
				row->side = 4;
				row->abr = table.abr;
				row->shown = groupAddress;
				return 1;
			}
		}
	}
	if (!EngTrack_LayoutUsable(finest))
	{
		return 0;
	}
	row->abrLevels = (u8)(mosaic ? levelsDisagree : 0);
	row->table = 0;
	row->side = 1;
	row->abr = (s8)EngMosaic_Abr(finest);
	EngMosaic_WholeFaceFill(row, groupAddress, group, finest);
	return 1;
}

// The animated row of (AnimTex cell, mirrored, form): an existing one, or a new
// one filled for group (shown now, EngMosaic_AnimFill). Returns the index, -1
// when the set is full or out of memory, -2 for cell 0 or an unknown form, -3
// when group has no usable layout. *made = 1 when the row is new.
internal int EngMosaic_AnimRowFind(EngMosaicRowSet *set, u32 cell, int mirrored, int form, u32 groupAddress, const struct TextureLayout *group,
                                   int (*spanOk)(u32 p, u32 size), int (*isLevelPtr)(u32 word), int *made)
{
	EngMosaicRow *row;
	int i;

	if (made != NULL)
	{
		*made = 0;
	}
	// Cell 0 is the mark of a table row: never a key here.
	if ((cell == 0u) || (EngMosaic_OwnEntries(form) == 0))
	{
		return -2;
	}
	for (i = 0; i < set->count; i++)
	{
		if ((set->rows[i].animCell == cell) && (set->rows[i].mirrored == (u8)(mirrored ? 1 : 0)) && (set->rows[i].form == (u8)form))
		{
			return i;
		}
	}
	if (!EngMosaic_RowRoom(set))
	{
		return -1;
	}
	row = &set->rows[set->count];
	memset(row, 0, sizeof(*row));
	row->animCell = cell;
	row->mirrored = (u8)(mirrored ? 1 : 0);
	row->form = (u8)form;
	if (!EngMosaic_AnimFill(row, groupAddress, group, spanOk, isLevelPtr))
	{
		return -3;
	}
	if (made != NULL)
	{
		*made = 1;
	}
	return set->count++;
}

internal void EngMosaic_RowSetFree(EngMosaicRowSet *set)
{
	free(set->rows);
	memset(set, 0, sizeof(*set));
}

// The cell that holds own entry e (0 .. own - 1) of a row: every entry of the
// table's orientation appears in the 4x4 grid, a 4x2 entry twice, a 4x1 entry
// four times; this names its first cell.
internal int EngMosaic_OwnCell(int form, int e)
{
	switch (form)
	{
	case ENGINE_MOSAIC_FORM_4X2:
		return (e >> 2) * 8 + (e & 3);
	default:
		return e;
	}
}

// One layout as three RGBA8 texels: its 12 bytes in memory order.
internal void EngMosaic_PackLayout(u8 *out, const struct TextureLayout *lay)
{
	out[0] = lay->u0;
	out[1] = lay->v0;
	out[2] = (u8)(lay->clut & 0xffu);
	out[3] = (u8)(lay->clut >> 8);
	out[4] = lay->u1;
	out[5] = lay->v1;
	out[6] = (u8)(lay->tpage & 0xffu);
	out[7] = (u8)(lay->tpage >> 8);
	out[8] = lay->u2;
	out[9] = lay->v2;
	out[10] = lay->u3;
	out[11] = lay->v3;
}

// One row of the data texture (EGM_ROW_TEXELS * 4 bytes).
internal void EngMosaic_PackRow(u8 *out, const EngMosaicRow *row)
{
	int i;

	memset(out, 0, EGM_ROW_TEXELS * 4);
	out[0] = row->side;
	out[1] = row->form;
	out[2] = (u8)((row->abr < 0) ? 0 : row->abr);
	for (i = 0; i < EGM_CELLS; i++)
	{
		EngMosaic_PackLayout(out + 4 * (EGM_CELL_TEXEL + 3 * i), &row->cell[i]);
	}
	for (i = 0; i < 3; i++)
	{
		EngMosaic_PackLayout(out + 4 * (EGM_LEVEL_TEXEL + 3 * (2 - i)), &row->level[i]);
	}
}

// The slot position: slot 0 (0, 0), 1 (1, 0), 2 (0, 1), 3 (1, 1).
internal void EngMosaic_SlotPosition(int slot, float *s, float *t)
{
	*s = (float)(slot & 1);
	*t = (float)((slot >> 1) & 1);
}

internal void EngMosaic_Encode(int row, float s, float t, float *out)
{
	out[0] = s + 2.0f + 4.0f * (float)(row & 63);
	out[1] = t + 2.0f + 4.0f * (float)(row >> 6);
}

// The size of a face as its vertices' alpha byte: 16 * log2 of the geometric
// mean of its two side lengths (slot 0 to 1 and slot 0 to 2, world units),
// 1..255 (a face of 1 unit up to about 2^16). "ne" turns it back into a length
// and divides the world size of a pixel by it: how much of the face one pixel
// covers, from the world position, which is continuous across the faces (the
// face's own (s, t) jumps at every border, and its screen derivative there
// would send the border pixels to the coarsest level). 0 when a slot is missing.
internal u8 EngMosaic_FaceSizeCode(const EngineVertex *v, const u8 *slotOf, int vertCount)
{
	const EngineVertex *at[3] = {NULL, NULL, NULL};
	double side[2];
	double mean;
	double code;
	int i;
	int k;

	for (i = 0; i < vertCount; i++)
	{
		const int slot = slotOf[i] & 3;

		if ((slot < 3) && (at[slot] == NULL))
		{
			at[slot] = &v[i];
		}
	}
	if ((at[0] == NULL) || (at[1] == NULL) || (at[2] == NULL))
	{
		return 0;
	}
	for (k = 0; k < 2; k++)
	{
		const EngineVertex *to = at[k + 1];
		const double dx = (double)to->position[0] - (double)at[0]->position[0];
		const double dy = (double)to->position[1] - (double)at[0]->position[1];
		const double dz = (double)to->position[2] - (double)at[0]->position[2];

		side[k] = sqrt(dx * dx + dy * dy + dz * dz);
	}
	mean = sqrt(side[0] * side[1]);
	code = (mean > 1.0) ? floor(16.0 * log2(mean) + 0.5) : 1.0;
	return (u8)((code < 1.0) ? 1.0 : ((code > 255.0) ? 255.0 : code));
}

// A table face's vertices: texcoord = row and slot position (EngMosaic_Encode),
// alpha = the face size (EngMosaic_FaceSizeCode; "ne" draws table faces opaque
// in alpha, as every VRAM texel is, so the byte is free).
internal void EngMosaic_FaceWrite(int row, const u8 *slotOf, EngineVertex *v, int vertCount)
{
	const u8 size = EngMosaic_FaceSizeCode(v, slotOf, vertCount);
	int i;

	for (i = 0; i < vertCount; i++)
	{
		float s;
		float t;

		EngMosaic_SlotPosition(slotOf[i] & 3, &s, &t);
		EngMosaic_Encode(row, s, t, v[i].texcoord);
		v[i].color[3] = size;
	}
}

// The C copy of the decode in "ne" (native_shaders.inc, table mode).
internal void EngMosaic_Decode(float x, float y, int *row, float *s, float *t)
{
	const float a = floorf(x * 0.25f);
	const float b = floorf(y * 0.25f);

	*row = (int)a + 64 * (int)b;
	*s = x - 4.0f * a - 2.0f;
	*t = y - 4.0f * b - 2.0f;
}

// The C copy of the cell pick in "ne": side cells per side, (s, t) clamped to
// the grid, *fs / *ft the position inside the cell (0..1).
internal int EngMosaic_CellPick(int side, float s, float t, float *fs, float *ft)
{
	const float gs = s * (float)side;
	const float gt = t * (float)side;
	int c = (int)floorf(gs);
	int r = (int)floorf(gt);

	if (c < 0)
	{
		c = 0;
	}
	if (c > side - 1)
	{
		c = side - 1;
	}
	if (r < 0)
	{
		r = 0;
	}
	if (r > side - 1)
	{
		r = side - 1;
	}
	*fs = gs - (float)c;
	*ft = gt - (float)r;
	return 4 * r + c;
}

// The C copy of the UV inside a cell: bilinear over the four corners.
internal void EngMosaic_CellUv(const struct TextureLayout *lay, float fs, float ft, float *u, float *v)
{
	const float topU = (float)lay->u0 + ((float)lay->u1 - (float)lay->u0) * fs;
	const float topV = (float)lay->v0 + ((float)lay->v1 - (float)lay->v0) * fs;
	const float botU = (float)lay->u2 + ((float)lay->u3 - (float)lay->u2) * fs;
	const float botV = (float)lay->v2 + ((float)lay->v3 - (float)lay->v2) * fs;

	*u = topU + (botU - topU) * ft;
	*v = topV + (botV - topV) * ft;
}

// Texels a layout covers (its UV box), for the log.
internal int EngMosaic_LayoutTexels(const struct TextureLayout *lay)
{
	const int u[4] = {lay->u0, lay->u1, lay->u2, lay->u3};
	const int v[4] = {lay->v0, lay->v1, lay->v2, lay->v3};
	int uLo = u[0];
	int uHi = u[0];
	int vLo = v[0];
	int vHi = v[0];
	int i;

	for (i = 1; i < 4; i++)
	{
		uLo = (u[i] < uLo) ? u[i] : uLo;
		uHi = (u[i] > uHi) ? u[i] : uHi;
		vLo = (v[i] < vLo) ? v[i] : vLo;
		vHi = (v[i] > vHi) ? v[i] : vHi;
	}
	return (uHi - uLo + 1) * (vHi - vLo + 1);
}

// --------------------------------------------------------------------------
// Game side

typedef struct
{
	int faces;     // mosaic faces handed in
	int made;      // turned into table faces
	int byForm[3]; // made, per ENGINE_MOSAIC_FORM_*
	int rowsFull;
	int badTable;
	int unknownForm;
	int abrMixed;
	int animFaces; // AnimTex faces handed in
	int animMade;  // turned into faces of an animated row
	int animBad;   // their cell or group not readable at load
	int animTable; // of animMade: the row holds a mosaic table now (side 4)
	int animBlends; // of animMade: the cell's frames give the row more than one abr
} EngMosaicCounts;

global_variable EngMosaicRowSet s_egmRows;
global_variable EngMosaicCounts s_egmCount;
global_variable u32 s_egmTexture = NATIVE_GFX_INVALID;
global_variable int s_egmLevel = -1;
// Over the whole run (the exit line): rows rewritten by Engine_TrackMosaicFrame,
// frames it looked, rewrites whose group has another abr than the row's key,
// cells or groups it could not read.
global_variable u64 s_egmAnimRewrites;
global_variable u64 s_egmAnimFrames;
global_variable u64 s_egmAnimAbrChanges;
global_variable u64 s_egmAnimBadCells;
global_variable int s_egmReportRegistered;

internal void EngMosaic_Report(void)
{
	Platform_Log("[CTR Engine] track mosaic: animated rows rewritten %llu over %llu frames, abr changes %llu (followed), bad cells %llu\n",
	             (unsigned long long)s_egmAnimRewrites, (unsigned long long)s_egmAnimFrames, (unsigned long long)s_egmAnimAbrChanges,
	             (unsigned long long)s_egmAnimBadCells);
}

// The IconGroup4 a word names (a group pointer in level memory), or NULL. spanOk NULL: any.
internal const struct TextureLayout *EngMosaic_GroupAt(u32 word, int (*spanOk)(u32 p, u32 size))
{
	if ((word == 0u) || ((spanOk != NULL) && !spanOk(word, 4u * (u32)sizeof(struct TextureLayout))))
	{
		return NULL;
	}
	return (const struct TextureLayout *)(size_t)word;
}

// The IconGroup4 an AnimTex cell names now (its first word), or NULL when the
// cell or the group does not lie in level memory. *address is the group's address.
internal const struct TextureLayout *EngMosaic_AnimGroupWith(u32 cell, u32 *address, int (*spanOk)(u32 p, u32 size))
{
	const struct TextureLayout *group;
	u32 now = 0;

	*address = 0;
	if ((cell == 0u) || ((spanOk != NULL) && !spanOk(cell, (u32)sizeof(now))))
	{
		return NULL;
	}
	memcpy(&now, (const void *)(size_t)cell, sizeof(now));
	group = EngMosaic_GroupAt(now, spanOk);
	*address = (group != NULL) ? now : 0u;
	return group;
}

internal const struct TextureLayout *EngMosaic_AnimGroup(u32 cell, u32 *address)
{
	return EngMosaic_AnimGroupWith(cell, address, EngTrack_SpanOk);
}

// The layout the track builder would take for the group (engine_track.c: level
// 3, or 2 for a mosaic word or a blank level 3), NULL when it is not usable.
internal const struct TextureLayout *EngMosaic_AnimFinest(const struct TextureLayout *group)
{
	const struct TextureLayout *lay = EngTrack_FinestLayout(group, EngTrack_IsLevelPtr, NULL);

	return EngTrack_LayoutUsable(lay) ? lay : NULL;
}

// Bit a for every abr an animated row of (mirrored, form) holds for one of the
// groups the AnimTex at cell cycles through (its numFrames IconGroup4 pointers
// after the AnimTex, CTR_CycleTex_LEV), plus bit now. Frames whose group is not
// readable add nothing (the row keeps its group then, counted per frame).
internal u8 EngMosaic_AnimAbrSet(u32 cell, int mirrored, int form, int now, int (*spanOk)(u32 p, u32 size), int (*isLevelPtr)(u32 word))
{
	const struct AnimTex *at = (const struct AnimTex *)(size_t)cell;
	u8 set = (u8)((now >= 0) ? (1u << now) : 0u);
	int frames;
	int f;

	if ((cell == 0u) || ((spanOk != NULL) && !spanOk(cell, (u32)sizeof(struct AnimTex))))
	{
		return set;
	}
	frames = (int)at->numFrames;
	if ((frames <= 0) || (frames > 4096) ||
	    ((spanOk != NULL) && !spanOk(cell + (u32)sizeof(struct AnimTex), (u32)frames * (u32)sizeof(u32))))
	{
		return set;
	}
	for (f = 0; f < frames; f++)
	{
		u32 word = 0;
		const struct TextureLayout *group;
		EngMosaicRow row;

		memcpy(&word, (const u8 *)at + sizeof(struct AnimTex) + ((size_t)f * sizeof(u32)), sizeof(word));
		group = EngMosaic_GroupAt(word, spanOk);
		memset(&row, 0, sizeof(row));
		row.mirrored = (u8)(mirrored ? 1 : 0);
		row.form = (u8)form;
		if ((group != NULL) && EngMosaic_AnimFill(&row, word, group, spanOk, isLevelPtr))
		{
			set |= (u8)(1u << (row.abr & 3));
		}
	}
	return set;
}

internal int EngMosaic_CompareInt(const void *a, const void *b)
{
	const int x = *(const int *)a;
	const int y = *(const int *)b;

	return (x > y) - (x < y);
}

internal int EngMosaic_CompareLayout(const void *a, const void *b)
{
	return memcmp(a, b, sizeof(struct TextureLayout));
}

internal void EngMosaic_Release(void)
{
	if (s_egmTexture != NATIVE_GFX_INVALID)
	{
		// Retired by the backend, not destroyed under a recording frame
		// (NativeGfxVK_DestroyTexture).
		NativeGfx_DestroyTexture((TextureID)s_egmTexture);
		s_egmTexture = NATIVE_GFX_INVALID;
	}
	EngMosaic_RowSetFree(&s_egmRows);
	memset(&s_egmCount, 0, sizeof(s_egmCount));
}

void Engine_TrackMosaicBegin(int levelId)
{
	if (!g_cfg_engine || !g_cfg_engineTrack)
	{
		return;
	}
	if (!s_egmReportRegistered)
	{
		s_egmReportRegistered = 1;
		Platform_AtExitReport(EngMosaic_Report);
	}
	EngMosaic_Release();
	s_egmLevel = levelId;
}

int Engine_TrackMosaicFace(const struct TextureLayout *group, int mirrored, int form, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage)
{
	const struct TextureLayout *table;
	u32 word = 0;
	int own;
	int row;

	if (!g_cfg_engine || !g_cfg_engineTrack || (group == NULL) || (slotOf == NULL) || (v == NULL) || (keyTpage == NULL) || (vertCount < 1) ||
	    (vertCount > 4))
	{
		return 0;
	}
	s_egmCount.faces++;
	own = EngMosaic_OwnEntries(form);
	if (own == 0)
	{
		s_egmCount.unknownForm++;
		return 0;
	}
	// The first word of level 3 is the table (EngTrack_FinestLayout decided that
	// it is one); both orientations must lie in level memory and be mosaic tiles
	// (engine_track.c, THE TABLE TEST OF THE ENGINE).
	memcpy(&word, &group[3], sizeof(word));
	if ((word == 0u) || !DrawLevelOvr1P_IsNativeLevelSpan(word, (u32)(2 * own) * (u32)sizeof(struct TextureLayout)) ||
	    !EngTrack_TableUsable((const struct TextureLayout *)(size_t)word, 2 * own))
	{
		s_egmCount.badTable++;
		return 0;
	}
	table = (const struct TextureLayout *)(size_t)word;
	row = EngMosaic_RowFind(&s_egmRows, (u32)(size_t)group, word, table, mirrored, form, group, NULL);
	if (row < 0)
	{
		s_egmCount.rowsFull++;
		return 0;
	}
	if (s_egmRows.rows[row].abr < 0)
	{
		s_egmCount.abrMixed++;
		return 0;
	}
	EngMosaic_FaceWrite(row, slotOf, v, vertCount);
	*keyTpage = (u16)(0x8000u | ((u32)s_egmRows.rows[row].abr << 5));
	s_egmCount.made++;
	s_egmCount.byForm[form]++;
	return 1;
}

int Engine_TrackMosaicAnimFace(u32 cell, int mirrored, int form, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage, int *row, u8 *abrSet)
{
	const struct TextureLayout *group;
	u32 now = 0;
	int index;

	if (!g_cfg_engine || !g_cfg_engineTrack || (slotOf == NULL) || (v == NULL) || (keyTpage == NULL) || (row == NULL) || (abrSet == NULL) ||
	    (vertCount < 1) || (vertCount > 4))
	{
		return 0;
	}
	s_egmCount.animFaces++;
	group = EngMosaic_AnimGroup(cell, &now);
	index = (group != NULL) ? EngMosaic_AnimRowFind(&s_egmRows, cell, mirrored, form, now, group, EngTrack_SpanOk, EngTrack_IsLevelPtr, NULL) : -3;
	if ((index == -1) || (index == -2))
	{
		s_egmCount.rowsFull += (index == -1) ? 1 : 0;
		s_egmCount.unknownForm += (index == -2) ? 1 : 0;
		return 0;
	}
	if (index < 0)
	{
		s_egmCount.animBad++;
		return 0;
	}
	EngMosaic_FaceWrite(index, slotOf, v, vertCount);
	*keyTpage = (u16)(0x8000u | ((u32)(s_egmRows.rows[index].abr & 3) << 5));
	*row = index;
	*abrSet = EngMosaic_AnimAbrSet(cell, mirrored, form, s_egmRows.rows[index].abr, EngTrack_SpanOk, EngTrack_IsLevelPtr);
	s_egmCount.animMade++;
	s_egmCount.animTable += (s_egmRows.rows[index].side == 4) ? 1 : 0;
	s_egmCount.animBlends += ((*abrSet & (*abrSet - 1u)) != 0u) ? 1 : 0;
	return 1;
}

int Engine_TrackMosaicAnimAbr(int row)
{
	if ((row < 0) || (row >= s_egmRows.count) || (s_egmRows.rows[row].animCell == 0u) || (s_egmRows.rows[row].abr < 0))
	{
		return -1;
	}
	return (int)s_egmRows.rows[row].abr;
}

// Once per frame, after Engine_TrackCollect: the animated rows whose AnimTex
// cell names another group now are rewritten (EngMosaic_AnimFill: cells and
// abr of that group; the track picks the face's material by the abr when it
// submits, later in the same frame). The upload lands in the frame
// before the engine's draws are recorded (NativeGfx_UpdateTexture records the
// copy between passes, NativeVk_RecordUploadInFrame).
void Engine_TrackMosaicFrame(void)
{
	u8 px[EGM_ROW_TEXELS * 4];
	int i;

	if (!g_cfg_engine || !g_cfg_engineTrack || (s_egmTexture == NATIVE_GFX_INVALID) || (s_egmCount.animMade == 0) || !Engine_TrackDrawnThisFrame())
	{
		return;
	}
	s_egmAnimFrames++;
	for (i = 0; i < s_egmRows.count; i++)
	{
		EngMosaicRow *row = &s_egmRows.rows[i];
		const struct TextureLayout *group;
		const s8 abrBefore = row->abr;
		u32 now = 0;

		if (row->animCell == 0u)
		{
			continue;
		}
		group = EngMosaic_AnimGroup(row->animCell, &now);
		if ((group != NULL) && (now == row->shown))
		{
			continue;
		}
		// The group shown now, under the rule of the load (cells of its table, or its finest layout).
		if ((group == NULL) || !EngMosaic_AnimFill(row, now, group, EngTrack_SpanOk, EngTrack_IsLevelPtr))
		{
			s_egmAnimBadCells++;
			continue;
		}
		if (row->abr != abrBefore)
		{
			s_egmAnimAbrChanges++;
		}
		EngMosaic_PackRow(px, row);
		NativeGfx_UpdateTexture((TextureID)s_egmTexture, 0, i, EGM_ROW_TEXELS, 1, NATIVE_GFX_TEXFMT_RGBA8, px, 0);
		s_egmAnimRewrites++;
	}
}

// The log lines of section MOSAIC (engine.h), from the rows made.
internal void EngMosaic_Log(int width, int height, const char *state)
{
	const EngMosaicRowSet *set = &s_egmRows;
	struct TextureLayout *tiles = NULL;
	int *tileTexels = NULL;
	int *level2Texels = NULL;
	int tileCount = 0;
	int distinct = 0;
	int tables = 0;
	int mirroredRows = 0;
	int differ = 0;
	int abrDiffers = 0;
	int classDiffers = 0;
	int depth[3] = {0, 0, 0};
	int abrLevelRows = 0;
	int tableRows = 0;
	int animRows = 0;
	int n = 0;
	int i;

	for (i = 0; i < set->count; i++)
	{
		tileCount += (set->rows[i].animCell == 0u) ? EngMosaic_OwnEntries(set->rows[i].form) : 0;
	}
	if (set->count > 0)
	{
		tiles = (struct TextureLayout *)malloc((size_t)tileCount * sizeof(struct TextureLayout));
		tileTexels = (int *)malloc((size_t)tileCount * sizeof(int));
		level2Texels = (int *)malloc((size_t)set->count * sizeof(int));
	}
	for (i = 0; i < set->count; i++)
	{
		const EngMosaicRow *row = &set->rows[i];
		const int own = EngMosaic_OwnEntries(row->form);
		const int level2Abr = EngMosaic_Abr(&row->level[2]);
		int j;
		int e;

		abrLevelRows += row->abrLevels;
		// The animated rows (one per AnimTex cell, mirror and form) are not counted as table rows.
		if (row->animCell != 0u)
		{
			animRows++;
			continue;
		}
		mirroredRows += row->mirrored;
		// A table is counted at its first row.
		for (j = 0; j < i; j++)
		{
			if ((set->rows[j].animCell == 0u) && (set->rows[j].table == row->table))
			{
				break;
			}
		}
		tables += (j == i) ? 1 : 0;
		if (level2Texels != NULL)
		{
			level2Texels[tableRows] = EngMosaic_LayoutTexels(&row->level[2]);
		}
		tableRows++;
		for (e = 0; e < own; e++)
		{
			const struct TextureLayout *t = &row->cell[EngMosaic_OwnCell(row->form, e)];
			const int abr = EngMosaic_Abr(t);
			const int d = (t->tpage >> 7) & 3;

			if ((tiles != NULL) && (tileTexels != NULL))
			{
				tiles[n] = *t;
				tileTexels[n] = EngMosaic_LayoutTexels(t);
				n++;
			}
			differ += ((t->tpage != row->level[2].tpage) || (t->clut != row->level[2].clut)) ? 1 : 0;
			abrDiffers += (abr != level2Abr) ? 1 : 0;
			classDiffers += ((abr == 3) != (level2Abr == 3)) ? 1 : 0;
			depth[(d < 3) ? d : 2]++;
		}
	}
	if ((n > 0) && (level2Texels != NULL))
	{
		qsort(tiles, (size_t)n, sizeof(struct TextureLayout), EngMosaic_CompareLayout);
		distinct = 1;
		for (i = 1; i < n; i++)
		{
			distinct += (memcmp(&tiles[i], &tiles[i - 1], sizeof(struct TextureLayout)) != 0) ? 1 : 0;
		}
		qsort(tileTexels, (size_t)n, sizeof(int), EngMosaic_CompareInt);
		qsort(level2Texels, (size_t)tableRows, sizeof(int), EngMosaic_CompareInt);
	}

	Platform_Log("[CTR Engine] track mosaic: level %d: mosaic faces %d, made %d (4x4 %d, 4x2 %d, 4x1 %d), tables %d, rows %d (mirrored %d), "
	             "distinct tiles %d\n",
	             s_egmLevel, s_egmCount.faces, s_egmCount.made, s_egmCount.byForm[ENGINE_MOSAIC_FORM_4X4], s_egmCount.byForm[ENGINE_MOSAIC_FORM_4X2],
	             s_egmCount.byForm[ENGINE_MOSAIC_FORM_4X1], tables, tableRows, mirroredRows, distinct);
	Platform_Log("[CTR Engine] track mosaic: level %d: fallback to level 2: rows full %d, bad table %d, unknown form %d, abr mixed %d (rows whose tiles "
	             "agree but not with a level 0..2: %d)\n",
	             s_egmLevel, s_egmCount.rowsFull, s_egmCount.badTable, s_egmCount.unknownForm, s_egmCount.abrMixed, abrLevelRows);
	Platform_Log("[CTR Engine] track mosaic: level %d: tiles vs level 2: tpage/clut differ %d, abr differs %d (semi/opaque class differs %d); "
	             "tile depth 4/8/15 bit %d/%d/%d\n",
	             s_egmLevel, differ, abrDiffers, classDiffers, depth[0], depth[1], depth[2]);
	if ((n > 0) && (level2Texels != NULL))
	{
		Platform_Log("[CTR Engine] track mosaic: level %d: tile texels min %d median %d max %d, level 2 texels min %d median %d max %d\n", s_egmLevel,
		             tileTexels[0], tileTexels[n / 2], tileTexels[n - 1], level2Texels[0], level2Texels[tableRows / 2], level2Texels[tableRows - 1]);
	}
	else
	{
		Platform_Log("[CTR Engine] track mosaic: level %d: tile texels none\n", s_egmLevel);
	}
	Platform_Log("[CTR Engine] track mosaic: level %d: animated faces %d, made %d (holding a table now %d, more than one blend %d), animated rows %d, "
	             "bad cells %d\n",
	             s_egmLevel, s_egmCount.animFaces, s_egmCount.animMade, s_egmCount.animTable, s_egmCount.animBlends, animRows, s_egmCount.animBad);
	Platform_Log("[CTR Engine] track mosaic: level %d: data texture %dx%d %s\n", s_egmLevel, width, height, state);
	free(tiles);
	free(tileTexels);
	free(level2Texels);
}

int Engine_TrackMosaicEnd(void)
{
	NativeGfxTextureDesc desc;
	u8 *pixels;
	int i;

	if (!g_cfg_engine || !g_cfg_engineTrack)
	{
		return 0;
	}
	if (s_egmRows.count == 0)
	{
		EngMosaic_Log(0, 0, "not needed");
		return 0;
	}
	pixels = (u8 *)malloc((size_t)s_egmRows.count * EGM_ROW_TEXELS * 4u);
	if (pixels == NULL)
	{
		EngMosaic_Log(EGM_ROW_TEXELS, s_egmRows.count, "failed (out of memory)");
		return 0;
	}
	for (i = 0; i < s_egmRows.count; i++)
	{
		EngMosaic_PackRow(pixels + (size_t)i * EGM_ROW_TEXELS * 4u, &s_egmRows.rows[i]);
	}
	memset(&desc, 0, sizeof(desc));
	desc.width = EGM_ROW_TEXELS;
	desc.height = s_egmRows.count;
	desc.format = NATIVE_GFX_TEXFMT_RGBA8;
	desc.filter = NATIVE_GFX_FILTER_NEAREST;
	desc.wrap = NATIVE_GFX_WRAP_CLAMP;
	desc.pixels = pixels;
	s_egmTexture = (u32)NativeGfx_CreateTexture(&desc);
	free(pixels);
	EngMosaic_Log(EGM_ROW_TEXELS, s_egmRows.count, (s_egmTexture != NATIVE_GFX_INVALID) ? "ready" : "failed (device)");
	return (s_egmTexture != NATIVE_GFX_INVALID) ? 1 : 0;
}

void Engine_TrackMosaicUnload(void)
{
	if (!g_cfg_engine)
	{
		return;
	}
	EngMosaic_Release();
	s_egmLevel = -1;
}

u32 Engine_TrackMosaicTexture(void)
{
	return s_egmTexture;
}

// The row of a set a table face's texcoord names (EngMosaic_Decode), or NULL
// when the decoded row is not in the set.
internal const EngMosaicRow *EngMosaic_RowOfTexcoord(const EngMosaicRowSet *set, float u, float v)
{
	int row = -1;
	float s;
	float t;

	EngMosaic_Decode(u, v, &row, &s, &t);
	if ((set->rows == NULL) || (row < 0) || (row >= set->count))
	{
		return NULL;
	}
	return &set->rows[row];
}

int Engine_TrackMosaicOracleRow(float u, float v, int *side, int *form, int *finest, int *faulted, const struct TextureLayout **cells,
                                 const struct TextureLayout **levels)
{
	const EngMosaicRow *row;

	if (!g_cfg_engine || !g_cfg_engineTrack || (side == NULL) || (form == NULL) || (finest == NULL) || (faulted == NULL) || (cells == NULL) ||
	    (levels == NULL))
	{
		return 0;
	}
	row = EngMosaic_RowOfTexcoord(&s_egmRows, u, v);
	if (row == NULL)
	{
		return 0;
	}
	*side = (int)row->side;
	*form = (int)row->form;
	*faulted = (int)row->faulted;
	*cells = row->cell;
	*levels = row->level;
	// A whole-face row holds the finest layout of the group shown now: its level (3, or 2).
	*finest = 3;
	if ((row->side == 1) && (row->shown != 0u))
	{
		const struct TextureLayout *group = (const struct TextureLayout *)(size_t)row->shown;
		const struct TextureLayout *fin = EngMosaic_AnimFinest(group);

		*finest = (fin != NULL) ? (int)(fin - group) : -1;
	}
	return 1;
}

// Swaps cells a and b of a row (the fault of Engine_TrackMosaicOracleFault):
// the first pair in cell order whose layouts differ, both cells the row's own
// (4x4 any; 4x2 rows 0/2 - a spread pair moves with its cell; 4x1 row 0 - the
// same). Returns 1 with *a, *b set; 0 when all its own cells are equal.
internal int EngMosaic_FaultSwap(EngMosaicRow *row, int *a, int *b)
{
	const int own = EngMosaic_OwnEntries(row->form);
	const int ca = EngMosaic_OwnCell(row->form, 0);
	int i;

	for (i = 1; i < own; i++)
	{
		const int cb = EngMosaic_OwnCell(row->form, i);
		const struct TextureLayout first = row->cell[ca];
		const struct TextureLayout second = row->cell[cb];
		int j;

		if (memcmp(&first, &second, sizeof(first)) == 0)
		{
			continue;
		}
		// Every cell showing the one gets the other and the other way round (a 4x2
		// or 4x1 entry stands in more than one cell).
		for (j = 0; j < EGM_CELLS; j++)
		{
			if (memcmp(&row->cell[j], &first, sizeof(first)) == 0)
			{
				row->cell[j] = second;
			}
			else if (memcmp(&row->cell[j], &second, sizeof(second)) == 0)
			{
				row->cell[j] = first;
			}
		}
		*a = ca;
		*b = cb;
		row->faulted = 1;
		return 1;
	}
	return 0;
}

int Engine_TrackMosaicOracleFault(float u, float v, int block, int mid)
{
	EngMosaicRow *row;
	int a = 0;
	int b = 0;

	if (!g_cfg_engine || !g_cfg_engineTrack || (s_egmTexture != NATIVE_GFX_INVALID))
	{
		return 0;
	}
	row = (EngMosaicRow *)EngMosaic_RowOfTexcoord(&s_egmRows, u, v);
	if ((row == NULL) || (row->side != 4) || !EngMosaic_FaultSwap(row, &a, &b))
	{
		return 0;
	}
	Platform_Log("[CTR Engine] oracle fault: cell - quadblock %d face %d, its table row %d: cells %d,%d and %d,%d swapped (every face of that row)\n", block,
	             mid, (int)(row - s_egmRows.rows), a / 4, a % 4, b / 4, b % 4);
	return 1;
}

// --------------------------------------------------------------------------
// Selftest: rows, packing and the decode on synthetic tables.

#define EGM_CHECK(cond, ...)                            \
	do                                                  \
	{                                                   \
		(*checks)++;                                    \
		if (!(cond))                                    \
		{                                               \
			printf("engine selftest: mosaic " __VA_ARGS__); \
			printf("\n");                               \
			failures++;                                 \
		}                                               \
	} while (0)

// The table words the selftest's groups name (its isLevelPtr): a level pointer check without a level.
global_variable u32 s_egmTestTableWords[2];

internal int EngMosaic_TestIsTable(u32 word)
{
	return (word != 0u) && ((word == s_egmTestTableWords[0]) || (word == s_egmTestTableWords[1]));
}

// Entry i of a synthetic table: u0 = i, every other field derived from i, abr 3.
internal void EngMosaic_TestTable(struct TextureLayout *table, int entries)
{
	int i;

	memset(table, 0, (size_t)entries * sizeof(*table));
	for (i = 0; i < entries; i++)
	{
		table[i].u0 = (u8)i;
		table[i].v0 = (u8)(100 + i);
		table[i].clut = (u16)(0x7a00 + i);
		table[i].u1 = (u8)(i + 31);
		table[i].v1 = (u8)(100 + i);
		table[i].tpage = (u16)(0x0060 | (i & 15));
		table[i].u2 = (u8)i;
		table[i].v2 = (u8)(131 + i);
		table[i].u3 = (u8)(i + 31);
		table[i].v3 = (u8)(131 + i);
	}
}

int Engine_SelfTestTrackMosaic(int *checks)
{
	int failures = 0;
	struct TextureLayout table[32];
	struct TextureLayout levels[3];
	EngMosaicRowSet set;
	int made = 0;
	int a;
	int b;
	int i;

	memset(&set, 0, sizeof(set));
	EngMosaic_TestTable(table, 32);
	memset(levels, 0, sizeof(levels));
	levels[0].u0 = 200;
	levels[1].u0 = 201;
	levels[2].u0 = 1;
	levels[2].v0 = 2;
	levels[2].u1 = 3;
	levels[2].v1 = 4;
	levels[2].u2 = 5;
	levels[2].v2 = 6;
	levels[2].u3 = 7;
	levels[2].v3 = 8;
	levels[2].tpage = 0x0060;
	levels[0].tpage = 0x0060; // every level abr 3, as the tiles
	levels[1].tpage = 0x0060;

	// (1) row dedupe: the same (group, mirrored, form) is one row, any field of it a new one.
	a = EngMosaic_RowFind(&set, 0x1000u, 0x2000u, table, 0, ENGINE_MOSAIC_FORM_4X4, levels, &made);
	EGM_CHECK((a == 0) && (made == 1), "dedupe: first row %d made %d", a, made);
	b = EngMosaic_RowFind(&set, 0x1000u, 0x2000u, table, 0, ENGINE_MOSAIC_FORM_4X4, levels, &made);
	EGM_CHECK((b == 0) && (made == 0) && (set.count == 1), "dedupe: the same key gave %d (count %d)", b, set.count);
	EGM_CHECK((EngMosaic_RowFind(&set, 0x1000u, 0x2000u, table, 1, ENGINE_MOSAIC_FORM_4X4, levels, NULL) == 1) &&
	              (EngMosaic_RowFind(&set, 0x1000u, 0x2000u, table, 0, ENGINE_MOSAIC_FORM_4X2, levels, NULL) == 2) &&
	              (EngMosaic_RowFind(&set, 0x1010u, 0x2000u, table, 0, ENGINE_MOSAIC_FORM_4X4, levels, NULL) == 3) && (set.count == 4),
	          "dedupe: mirrored, form or group did not make a new row");
	EGM_CHECK(EngMosaic_RowFind(&set, 0x1000u, 0x2000u, table, 0, 7, levels, NULL) == -2, "unknown form not refused");

	// (2) 4x4: cell (c, r) = entry 4r + c; mirrored = entry 16 + 4r + c.
	{
		int bad = 0;

		for (i = 0; i < 16; i++)
		{
			bad += (set.rows[0].cell[i].u0 != (u8)i) + (set.rows[1].cell[i].u0 != (u8)(16 + i));
		}
		EGM_CHECK(bad == 0, "4x4 cells: %d wrong", bad);
	}

	// (3) 4x2 and 4x1 spread over the grid, with their mirror offsets.
	{
		static const int want42[16] = {0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7};
		EngMosaicRow row;
		int bad = 0;

		for (i = 0; i < 16; i++)
		{
			bad += (set.rows[2].cell[i].u0 != (u8)want42[i]);
		}
		EngMosaic_RowFill(&row, 1u, 2u, table, 1, ENGINE_MOSAIC_FORM_4X2, levels);
		for (i = 0; i < 16; i++)
		{
			bad += (row.cell[i].u0 != (u8)(8 + want42[i]));
		}
		EngMosaic_RowFill(&row, 1u, 2u, table, 0, ENGINE_MOSAIC_FORM_4X1, levels);
		for (i = 0; i < 16; i++)
		{
			bad += (row.cell[i].u0 != (u8)(i & 3));
		}
		EngMosaic_RowFill(&row, 1u, 2u, table, 1, ENGINE_MOSAIC_FORM_4X1, levels);
		for (i = 0; i < 16; i++)
		{
			bad += (row.cell[i].u0 != (u8)(4 + (i & 3)));
		}
		EGM_CHECK(bad == 0, "4x2 / 4x1 cells: %d wrong", bad);
		EGM_CHECK((EngMosaic_OwnCell(ENGINE_MOSAIC_FORM_4X2, 5) == 9) && (EngMosaic_OwnCell(ENGINE_MOSAIC_FORM_4X1, 3) == 3) &&
		              (EngMosaic_OwnCell(ENGINE_MOSAIC_FORM_4X4, 15) == 15),
		          "own cell of an entry");

		// The level 2 of a mirrored row has its corners swapped like the UVs of a mirrored face.
		EGM_CHECK((row.level[2].u0 == 3) && (row.level[2].v0 == 4) && (row.level[2].u1 == 1) && (row.level[2].u2 == 7) && (row.level[2].u3 == 5) &&
		              (set.rows[0].level[2].u0 == 1) && (row.level[0].u1 == 200) && (set.rows[0].level[1].u0 == 201),
		          "level 2 of a mirrored row not swapped");
	}

	// (4) abr: all tiles agree -> their abr; one differs -> -1.
	{
		struct TextureLayout mixed[32];
		EngMosaicRow row;

		EGM_CHECK(set.rows[0].abr == 3, "abr of agreeing tiles: %d", set.rows[0].abr);
		memcpy(mixed, table, sizeof(mixed));
		mixed[5].tpage = 0x0001; // abr 0
		EngMosaic_RowFill(&row, 1u, 2u, mixed, 0, ENGINE_MOSAIC_FORM_4X4, levels);
		EGM_CHECK(row.abr == -1, "mixed abr not seen: %d", row.abr);
		EngMosaic_RowFill(&row, 1u, 2u, mixed, 1, ENGINE_MOSAIC_FORM_4X4, levels);
		EGM_CHECK(row.abr == 3, "the other orientation must not see tile 5: %d", row.abr);
	}

	// (4b) abr of the tiles against the levels: tiles that agree with each other (abr 3) but not
	// with a usable level 0..2 (abr 0) make the row mixed (abrLevels); a blank level does not count.
	{
		struct TextureLayout lv[3];
		EngMosaicRow row;

		memcpy(lv, levels, sizeof(lv));
		lv[1].tpage = 0x0000; // abr 0
		EngMosaic_RowFill(&row, 1u, 2u, table, 0, ENGINE_MOSAIC_FORM_4X4, lv);
		EGM_CHECK((row.abr == -1) && (row.abrLevels == 1), "tiles against a level of another abr: abr %d levels %d", row.abr, row.abrLevels);
		memset(&lv[1], 0, sizeof(lv[1])); // blank: no texture
		EngMosaic_RowFill(&row, 1u, 2u, table, 0, ENGINE_MOSAIC_FORM_4X4, lv);
		EGM_CHECK((row.abr == 3) && (row.abrLevels == 0), "a blank level made the row mixed: abr %d", row.abr);
		EngMosaic_RowFill(&row, 1u, 2u, table, 0, ENGINE_MOSAIC_FORM_4X4, levels);
		EGM_CHECK((row.abr == 3) && (row.abrLevels == 0), "levels of the tiles' abr made the row mixed");
	}

	// (5) packing: header, a cell's 12 bytes in memory order, level 2.
	{
		u8 px[EGM_ROW_TEXELS * 4];
		const u8 *cell7 = px + 4 * (EGM_CELL_TEXEL + 3 * 7);
		const u8 *raw = (const u8 *)&set.rows[0].cell[7];

		EngMosaic_PackRow(px, &set.rows[0]);
		EGM_CHECK((px[0] == 4) && (px[1] == ENGINE_MOSAIC_FORM_4X4) && (px[2] == 3) && (px[3] == 0), "header %d %d %d %d", px[0], px[1], px[2], px[3]);
		EGM_CHECK(memcmp(cell7, raw, 12) == 0, "cell 7 bytes differ from the layout in memory");
		EGM_CHECK((px[4 * EGM_LEVEL_TEXEL] == 1) && (px[4 * EGM_LEVEL_TEXEL + 4] == 3) && (px[4 * EGM_LEVEL_TEXEL + 11] == 8) &&
		              (px[4 * (EGM_LEVEL_TEXEL + 3)] == 201) && (px[4 * (EGM_LEVEL_TEXEL + 6)] == 200),
		          "level texels");
	}

	// (6) encode / decode round trip (the program's decode), with overshoot.
	{
		static const int rows[5] = {0, 1, 63, 64, 4095};
		static const float st[6] = {0.0f, 0.25f, 0.999f, 1.0f, -0.01f, 1.01f};
		int bad = 0;
		int r;
		int k;

		for (r = 0; r < 5; r++)
		{
			for (k = 0; k < 6; k++)
			{
				float enc[2];
				int row = -1;
				float s = 0.0f;
				float t = 0.0f;

				EngMosaic_Encode(rows[r], st[k], st[5 - k], enc);
				EngMosaic_Decode(enc[0], enc[1], &row, &s, &t);
				bad += (row != rows[r]) || (fabsf(s - st[k]) > 1e-4f) || (fabsf(t - st[5 - k]) > 1e-4f) || (enc[0] > 256.0f) || (enc[1] > 256.0f);
			}
		}
		EGM_CHECK(bad == 0, "encode/decode: %d of 30 wrong", bad);
	}

	// (7) cell pick and the UV inside a cell.
	{
		float fs = 0.0f;
		float ft = 0.0f;
		float u = 0.0f;
		float v = 0.0f;

		EGM_CHECK((EngMosaic_CellPick(4, 0.1f, 0.1f, &fs, &ft) == 0) && (EngMosaic_CellPick(4, 0.9f, 0.1f, &fs, &ft) == 3) &&
		              (EngMosaic_CellPick(4, 0.1f, 0.9f, &fs, &ft) == 12) && (EngMosaic_CellPick(4, 1.0f, 1.0f, &fs, &ft) == 15) &&
		              (EngMosaic_CellPick(4, -0.01f, 0.3f, &fs, &ft) == 4) && (EngMosaic_CellPick(1, 0.7f, 0.2f, &fs, &ft) == 0),
		          "cell pick");
		EngMosaic_CellPick(4, 0.375f, 0.625f, &fs, &ft);
		EGM_CHECK((fabsf(fs - 0.5f) < 1e-5f) && (fabsf(ft - 0.5f) < 1e-5f), "position inside the cell %f %f", fs, ft);
		EngMosaic_CellUv(&table[2], 0.0f, 0.0f, &u, &v);
		EGM_CHECK((u == 2.0f) && (v == 102.0f), "cell UV at (0, 0): %f %f", u, v);
		EngMosaic_CellUv(&table[2], 1.0f, 1.0f, &u, &v);
		EGM_CHECK((u == 33.0f) && (v == 133.0f), "cell UV at (1, 1): %f %f", u, v);
		EngMosaic_CellUv(&table[2], 0.5f, 0.5f, &u, &v);
		EGM_CHECK((u == 17.5f) && (v == 117.5f), "cell UV at the centre: %f %f", u, v);
	}

	// (8) slot positions.
	{
		float s = -1.0f;
		float t = -1.0f;
		int bad = 0;

		for (i = 0; i < 4; i++)
		{
			EngMosaic_SlotPosition(i, &s, &t);
			bad += (s != (float)(i & 1)) || (t != (float)(i >> 1));
		}
		EGM_CHECK(bad == 0, "slot positions");
	}

	// (9) the row limit.
	{
		EngMosaicRowSet full;
		int r = 0;

		memset(&full, 0, sizeof(full));
		for (i = 0; (i < EGM_ROWS_MAX) && (r >= 0); i++)
		{
			r = EngMosaic_RowFind(&full, (u32)(i + 1), 2u, table, 0, ENGINE_MOSAIC_FORM_4X1, levels, NULL);
		}
		EGM_CHECK((r == EGM_ROWS_MAX - 1) && (EngMosaic_RowFind(&full, 0xffffu, 2u, table, 0, ENGINE_MOSAIC_FORM_4X1, levels, NULL) == -1),
		          "row limit: last %d", r);
		EngMosaic_RowSetFree(&full);
	}

	// (10) texel count of a layout.
	EGM_CHECK((EngMosaic_LayoutTexels(&table[0]) == 32 * 32) && (EngMosaic_LayoutTexels(&levels[2]) == 7 * 7), "layout texels %d %d",
	          EngMosaic_LayoutTexels(&table[0]), EngMosaic_LayoutTexels(&levels[2]));

	// (11) animated rows of a group without a mosaic word: keyed by (cell, mirrored, form), one cell
	// per side, every cell the finest layout (corners swapped when mirrored), apart from the table
	// rows; a rewrite for another group changes the cells and what the row shows.
	{
		u8 px[EGM_ROW_TEXELS * 4];
		int same = 0;

		made = 0;
		a = EngMosaic_AnimRowFind(&set, 0x5000u, 0, ENGINE_MOSAIC_FORM_4X4, 0x6000u, table, NULL, NULL, &made);
		EGM_CHECK((a == 4) && (made == 1) && (set.rows[4].side == 1) && (set.rows[4].shown == 0x6000u) && (set.rows[4].abr == 3) &&
		              (set.rows[4].level[2].u0 == 2),
		          "anim row: index %d made %d", a, made);
		for (i = 0; i < EGM_CELLS; i++)
		{
			same += (memcmp(&set.rows[4].cell[i], &table[3], sizeof(table[3])) == 0);
		}
		EGM_CHECK(same == EGM_CELLS, "anim row: %d of 16 cells hold the finest layout", same);
		b = EngMosaic_AnimRowFind(&set, 0x5000u, 0, ENGINE_MOSAIC_FORM_4X4, 0x6000u, table, NULL, NULL, &made);
		EGM_CHECK((b == 4) && (made == 0), "anim row: the same cell gave %d made %d", b, made);
		b = EngMosaic_AnimRowFind(&set, 0x5000u, 1, ENGINE_MOSAIC_FORM_4X4, 0x6000u, table, NULL, NULL, &made);
		EGM_CHECK((b == 5) && (made == 1) && (set.rows[5].cell[0].u0 == table[3].u1) && (set.rows[5].cell[0].u1 == table[3].u0),
		          "anim row: mirrored row %d not swapped", b);
		EGM_CHECK((EngMosaic_RowFind(&set, 0x1000u, 0x2000u, table, 0, ENGINE_MOSAIC_FORM_4X4, levels, NULL) == 0) &&
		              (EngMosaic_AnimRowFind(&set, 0u, 0, ENGINE_MOSAIC_FORM_4X4, 0x6000u, table, NULL, NULL, NULL) == -2) &&
		              (EngMosaic_AnimRowFind(&set, 0x5000u, 0, 7, 0x6000u, table, NULL, NULL, NULL) == -2),
		          "anim rows mixed up with table rows, or cell 0 or an unknown form taken");
		EngMosaic_WholeFaceFill(&set.rows[4], 0x7000u, &table[4], &table[7]);
		EGM_CHECK((set.rows[4].shown == 0x7000u) && (set.rows[4].cell[5].u0 == 7) && (set.rows[4].level[0].u0 == 4), "anim row rewrite");
		EngMosaic_PackRow(px, &set.rows[4]);
		EGM_CHECK((px[0] == 1) && (px[4 * (EGM_CELL_TEXEL + 3 * 15)] == 7), "anim row packing");
	}

	// (11b) ANIMATED ROWS follow the group shown now, by the rule of the load (K-ANIM-T, K-ANIM-T2):
	// a group with a mosaic table gives the table's cells (form, mirror) and their abr; a group
	// without one its finest layout and abr; a table whose tiles disagree its level 2. The abr set
	// of an AnimTex holds every abr its frames give.
	{
		struct TextureLayout mixed[32];
		struct TextureLayout groupM[4];
		struct TextureLayout groupW[4];
		struct TextureLayout groupX[4];
		struct TextureLayout blank[4];
		u32 animMem[16];
		struct AnimTex *at = (struct AnimTex *)animMem;
		u32 *frames = (u32 *)((u8 *)animMem + sizeof(struct AnimTex));
		const u32 tableWord = (u32)(size_t)table;
		const u32 mixedWord = (u32)(size_t)mixed;
		const u32 cell = (u32)(size_t)animMem;
		EngMosaicRow row;
		int bad = 0;
		int r;

		memcpy(mixed, table, sizeof(mixed));
		mixed[5].tpage = 0x0001;  // abr 0 on tile 5 of the own half
		mixed[21].tpage = 0x0001; // and of the mirrored half
		memset(groupM, 0, sizeof(groupM));
		memset(groupW, 0, sizeof(groupW));
		memset(groupX, 0, sizeof(groupX));
		memcpy(groupM, levels, sizeof(levels));
		memcpy(&groupM[3], &tableWord, sizeof(tableWord));
		memcpy(groupW, levels, sizeof(levels));
		groupW[3] = table[9];
		groupW[3].tpage = 0x0021; // abr 1
		memcpy(groupX, levels, sizeof(levels));
		groupX[2].tpage = 0x0041; // abr 2: the level 2 a disagreeing table falls back to
		memcpy(&groupX[3], &mixedWord, sizeof(mixedWord));
		memset(blank, 0, sizeof(blank));
		s_egmTestTableWords[0] = tableWord;
		s_egmTestTableWords[1] = mixedWord;

		r = EngMosaic_AnimRowFind(&set, 0x9000u, 0, ENGINE_MOSAIC_FORM_4X4, (u32)(size_t)groupM, groupM, NULL, EngMosaic_TestIsTable, &made);
		EGM_CHECK((r >= 0) && (made == 1) && (set.rows[r].side == 4) && (set.rows[r].abr == 3) && (set.rows[r].table == tableWord) &&
		              (set.rows[r].animCell == 0x9000u) && (set.rows[r].level[2].u0 == 1),
		          "animated row of a mosaic group: index %d side %d abr %d", r, (r >= 0) ? set.rows[r].side : -1, (r >= 0) ? set.rows[r].abr : -9);
		for (i = 0; (r >= 0) && (i < 16); i++)
		{
			bad += (set.rows[r].cell[i].u0 != (u8)i);
		}
		EGM_CHECK(bad == 0, "animated row of a mosaic group: %d cell(s) not the table's", bad);
		EGM_CHECK((EngMosaic_AnimRowFind(&set, 0x9000u, 0, ENGINE_MOSAIC_FORM_4X2, (u32)(size_t)groupM, groupM, NULL, EngMosaic_TestIsTable, &made) ==
		           r + 1) && (made == 1) && (set.rows[r + 1].form == ENGINE_MOSAIC_FORM_4X2) && (set.rows[r + 1].cell[8].u0 == 4),
		          "animated row: another form is another row with the form's spread");
		EGM_CHECK(EngMosaic_AnimRowFind(&set, 0x9100u, 0, ENGINE_MOSAIC_FORM_4X4, (u32)(size_t)blank, blank, NULL, EngMosaic_TestIsTable, NULL) == -3,
		          "animated row of a group without a usable layout made");

		// The rewrite for the next groups (Engine_TrackMosaicFrame's step).
		memset(&row, 0, sizeof(row));
		row.animCell = 0x9000u;
		row.form = ENGINE_MOSAIC_FORM_4X4;
		row.mirrored = 1;
		EGM_CHECK(EngMosaic_AnimFill(&row, (u32)(size_t)groupM, groupM, NULL, EngMosaic_TestIsTable) && (row.side == 4) && (row.cell[0].u0 == 16) &&
		              (row.abr == 3),
		          "animated row, mirrored mosaic group: side %d cell 0 u0 %d", row.side, row.cell[0].u0);
		EGM_CHECK(EngMosaic_AnimFill(&row, (u32)(size_t)groupW, groupW, NULL, EngMosaic_TestIsTable) && (row.side == 1) && (row.abr == 1) &&
		              (row.table == 0u) && (row.shown == (u32)(size_t)groupW) && (row.cell[0].u0 == groupW[3].u1) && (row.cell[15].u1 == groupW[3].u0) &&
		              (row.animCell == 0x9000u) && (row.mirrored == 1),
		          "animated row, group without a table: side %d abr %d", row.side, row.abr);
		EGM_CHECK(EngMosaic_AnimFill(&row, (u32)(size_t)groupX, groupX, NULL, EngMosaic_TestIsTable) && (row.side == 1) && (row.abr == 2) &&
		              (row.cell[0].tpage == 0x0041),
		          "animated row, table with disagreeing tiles: side %d abr %d (level 2 wanted)", row.side, row.abr);
		{
			// (4b) animated: tiles that agree (abr 3) with a level 2 of abr 1 give level 2 with its abr.
			struct TextureLayout groupL[4];

			memcpy(groupL, groupM, sizeof(groupL));
			groupL[2].tpage = 0x0021;
			EGM_CHECK(EngMosaic_AnimFill(&row, (u32)(size_t)groupL, groupL, NULL, EngMosaic_TestIsTable) && (row.side == 1) && (row.abr == 1) &&
			              (row.abrLevels == 1) && (row.cell[0].tpage == 0x0021),
			          "animated row, tiles against a level of another abr: side %d abr %d", row.side, row.abr);
			EGM_CHECK(EngMosaic_AnimFill(&row, (u32)(size_t)groupX, groupX, NULL, EngMosaic_TestIsTable) && (row.abrLevels == 0),
			          "animated row: the level reason stays on a row of disagreeing tiles");
		}
		EGM_CHECK(!EngMosaic_AnimFill(&row, (u32)(size_t)blank, blank, NULL, EngMosaic_TestIsTable) && (row.shown == (u32)(size_t)groupX),
		          "animated row: a group without a usable layout changed the row");
		{
			u8 px[EGM_ROW_TEXELS * 4];

			EngMosaic_AnimFill(&row, (u32)(size_t)groupM, groupM, NULL, EngMosaic_TestIsTable);
			EngMosaic_PackRow(px, &row);
			EGM_CHECK((px[0] == 4) && (px[1] == ENGINE_MOSAIC_FORM_4X4) && (px[2] == 3), "animated row packing of a table: %d %d %d", px[0], px[1], px[2]);
		}

		// The blends an AnimTex's frames give: M (3), W (1), X (2); an unreadable frame adds none.
		memset(animMem, 0, sizeof(animMem));
		at->ptrActiveTex = (int *)groupM;
		at->numFrames = 4;
		frames[0] = (u32)(size_t)groupM;
		frames[1] = (u32)(size_t)groupW;
		frames[2] = (u32)(size_t)groupX;
		frames[3] = (u32)(size_t)blank;
		EGM_CHECK(EngMosaic_AnimAbrSet(cell, 0, ENGINE_MOSAIC_FORM_4X4, -1, NULL, EngMosaic_TestIsTable) == 0x0eu, "abr set 0x%x, wanted 0x0e",
		          EngMosaic_AnimAbrSet(cell, 0, ENGINE_MOSAIC_FORM_4X4, -1, NULL, EngMosaic_TestIsTable));
		at->numFrames = 1;
		EGM_CHECK((EngMosaic_AnimAbrSet(cell, 0, ENGINE_MOSAIC_FORM_4X4, 0, NULL, EngMosaic_TestIsTable) == 0x09u) &&
		              (EngMosaic_AnimAbrSet(0u, 0, ENGINE_MOSAIC_FORM_4X4, 2, NULL, EngMosaic_TestIsTable) == 0x04u),
		          "abr set with the abr now, or of no cell");
		{
			u32 address = 0;

			EGM_CHECK((EngMosaic_AnimGroupWith(cell, &address, NULL) == groupM) && (address == (u32)(size_t)groupM), "the group an AnimTex cell names");
		}
		EGM_CHECK((Engine_TrackMosaicAnimAbr(-1) == -1) && (Engine_TrackMosaicAnimAbr(EGM_ROWS_MAX) == -1), "Engine_TrackMosaicAnimAbr out of range");
		s_egmTestTableWords[0] = 0u;
		s_egmTestTableWords[1] = 0u;
	}

	// (12) the face size in the vertex alpha: 16 * log2 of the mean side length, the same on every
	// vertex, back within 3 % as "ne" reads it; a face without slots 0..2 gets 0.
	{
		static const float corner[4][3] = {{0, 5, 0}, {384, 5, 0}, {0, 5, 384}, {384, 5, 384}};
		const u8 slots[4] = {0, 1, 2, 3};
		const u8 noSlots[4] = {3, 3, 3, 3};
		EngineVertex quad[4];
		int bad = 0;
		int r = -1;
		float s = 0.0f;
		float t = 0.0f;

		memset(quad, 0, sizeof(quad));
		for (i = 0; i < 4; i++)
		{
			memcpy(quad[i].position, corner[i], sizeof(corner[i]));
		}
		EngMosaic_FaceWrite(70, slots, quad, 4);
		for (i = 0; i < 4; i++)
		{
			EngMosaic_Decode(quad[i].texcoord[0], quad[i].texcoord[1], &r, &s, &t);
			bad += (quad[i].color[3] != 137) || (r != 70) || (s != (float)(i & 1)) || (t != (float)(i >> 1));
		}
		EGM_CHECK(bad == 0, "face write: %d vertices wrong (alpha %d)", bad, quad[0].color[3]);
		EGM_CHECK(fabs(pow(2.0, (double)quad[0].color[3] / 16.0) / 384.0 - 1.0) < 0.03, "face size code does not read back");
		quad[1].position[0] = 100.0f;
		quad[2].position[2] = 400.0f;
		EGM_CHECK((EngMosaic_FaceSizeCode(quad, slots, 4) == 122) && (EngMosaic_FaceSizeCode(quad, noSlots, 4) == 0), "face size code %d",
		          EngMosaic_FaceSizeCode(quad, slots, 4));
	}

	// (oracle) the row a table face's texcoord names (Engine_TrackMosaicOracleRow): every vertex of a
	// face written for row 2 finds row 2 (the 4x2 spread above); a row past the set none.
	{
		float tc[2];
		int bad = 0;
		int slot;

		for (slot = 0; slot < 4; slot++)
		{
			float s;
			float t;

			EngMosaic_SlotPosition(slot, &s, &t);
			EngMosaic_Encode(2, s, t, tc);
			bad += (EngMosaic_RowOfTexcoord(&set, tc[0], tc[1]) != &set.rows[2]);
		}
		EngMosaic_Encode(set.count, 0.0f, 0.0f, tc);
		EGM_CHECK((bad == 0) && (EngMosaic_RowOfTexcoord(&set, tc[0], tc[1]) == NULL), "oracle row: %d slot(s) wrong, or a row past the set found", bad);
	}

	EngMosaic_RowSetFree(&set);
	return failures;
}
