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
// of the program's decode; the selftest holds the two together.
//
// BLEND. The PS1 takes the semi-transparent packet code and the blend mode from
// the tile (~2504 reads the tpage the mosaic path has just loaded). A material
// has one blend mode, so a face is keyed with the abr of its row's tiles when
// they all agree; a row whose tiles disagree keeps level 2 (counted).
//
// DRAW TIME ONLY. Nothing here writes game state: the tables are read at load
// stage 8, from the same level memory the PS1 draw reads.

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
	u32 group; // address of the IconGroup4 the row was made for (the key)
	u32 table; // address of its mosaic table (0 for a whole-face row)
	u8 mirrored;
	u8 form;   // ENGINE_MOSAIC_FORM_*
	u8 side;   // cells per side: 4, or 1 for a whole-face row
	s8 abr;    // abr of every own tile, -1 when they disagree
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
	// Levels 0..2 are not stored mirrored: a mirrored face takes its UVs in the
	// order {uv1, uv0, uv3, uv2} (engine_track.c, EngTrack_BuildBlock), which in
	// the face's own space is the layout with its corners swapped the same way.
	for (i = 0; i < 3; i++)
	{
		const struct TextureLayout *src = &levels[i];
		struct TextureLayout *dst = &row->level[i];

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
	row->abr = (s8)EngMosaic_Abr(&row->cell[0]);
	for (i = 1; i < EGM_CELLS; i++)
	{
		if (EngMosaic_Abr(&row->cell[i]) != row->abr)
		{
			row->abr = -1;
			break;
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

		if ((row->group == groupAddress) && (row->mirrored == (u8)(mirrored ? 1 : 0)) && (row->form == (u8)form))
		{
			return i;
		}
	}
	if (set->count >= EGM_ROWS_MAX)
	{
		return -1;
	}
	if (set->count >= set->cap)
	{
		const int cap = (set->cap == 0) ? 64 : (set->cap * 2);
		EngMosaicRow *p = (EngMosaicRow *)realloc(set->rows, (size_t)cap * sizeof(EngMosaicRow));

		if (p == NULL)
		{
			return -1;
		}
		set->rows = p;
		set->cap = cap;
	}
	EngMosaic_RowFill(&set->rows[set->count], groupAddress, tableAddress, table, mirrored, form, levels);
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
} EngMosaicCounts;

global_variable EngMosaicRowSet s_egmRows;
global_variable EngMosaicCounts s_egmCount;
global_variable u32 s_egmTexture = NATIVE_GFX_INVALID;
global_variable int s_egmLevel = -1;

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
	EngMosaic_Release();
	s_egmLevel = levelId;
}

int Engine_TrackMosaicFace(const struct TextureLayout *group, int mirrored, int form, const u8 *slotOf, EngineVertex *v, int vertCount, u16 *keyTpage)
{
	const struct TextureLayout *table;
	u32 word = 0;
	int own;
	int row;
	int i;

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
	// it is one); both orientations must lie in level memory.
	memcpy(&word, &group[3], sizeof(word));
	if ((word == 0u) || !DrawLevelOvr1P_IsNativeLevelSpan(word, (u32)(2 * own) * (u32)sizeof(struct TextureLayout)))
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
	for (i = 0; i < vertCount; i++)
	{
		float s;
		float t;

		EngMosaic_SlotPosition(slotOf[i] & 3, &s, &t);
		EngMosaic_Encode(row, s, t, v[i].texcoord);
	}
	*keyTpage = (u16)(0x8000u | ((u32)s_egmRows.rows[row].abr << 5));
	s_egmCount.made++;
	s_egmCount.byForm[form]++;
	return 1;
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
	int n = 0;
	int i;

	for (i = 0; i < set->count; i++)
	{
		tileCount += EngMosaic_OwnEntries(set->rows[i].form);
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

		mirroredRows += row->mirrored;
		// A table is counted at its first row.
		for (j = 0; j < i; j++)
		{
			if (set->rows[j].table == row->table)
			{
				break;
			}
		}
		tables += (j == i) ? 1 : 0;
		if (level2Texels != NULL)
		{
			level2Texels[i] = EngMosaic_LayoutTexels(&row->level[2]);
		}
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
		qsort(level2Texels, (size_t)set->count, sizeof(int), EngMosaic_CompareInt);
	}

	Platform_Log("[CTR Engine] track mosaic: level %d: mosaic faces %d, made %d (4x4 %d, 4x2 %d, 4x1 %d), tables %d, rows %d (mirrored %d), "
	             "distinct tiles %d\n",
	             s_egmLevel, s_egmCount.faces, s_egmCount.made, s_egmCount.byForm[ENGINE_MOSAIC_FORM_4X4], s_egmCount.byForm[ENGINE_MOSAIC_FORM_4X2],
	             s_egmCount.byForm[ENGINE_MOSAIC_FORM_4X1], tables, set->count, mirroredRows, distinct);
	Platform_Log("[CTR Engine] track mosaic: level %d: fallback to level 2: rows full %d, bad table %d, unknown form %d, abr mixed %d\n", s_egmLevel,
	             s_egmCount.rowsFull, s_egmCount.badTable, s_egmCount.unknownForm, s_egmCount.abrMixed);
	Platform_Log("[CTR Engine] track mosaic: level %d: tiles vs level 2: tpage/clut differ %d, abr differs %d (semi/opaque class differs %d); "
	             "tile depth 4/8/15 bit %d/%d/%d\n",
	             s_egmLevel, differ, abrDiffers, classDiffers, depth[0], depth[1], depth[2]);
	if ((n > 0) && (level2Texels != NULL))
	{
		Platform_Log("[CTR Engine] track mosaic: level %d: tile texels min %d median %d max %d, level 2 texels min %d median %d max %d\n", s_egmLevel,
		             tileTexels[0], tileTexels[n / 2], tileTexels[n - 1], level2Texels[0], level2Texels[set->count / 2], level2Texels[set->count - 1]);
	}
	else
	{
		Platform_Log("[CTR Engine] track mosaic: level %d: tile texels none\n", s_egmLevel);
	}
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

	EngMosaic_RowSetFree(&set);
	return failures;
}
