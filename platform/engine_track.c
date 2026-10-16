#include <platform/engine.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE TRACK - the track prototype (--engine-track, round of 2026-10-13). See
// include/platform/engine.h, TRACK PROTOTYPE.
//
// WHAT. The level's quadblocks become engine meshes with VRAM materials (the
// retail texture pages, sampled live from the VRAM texture by the "ne"
// program), drawn once per race frame with the camera of view 0; the PS1 walk
// of the level (DrawLevelOvr1P) takes the retail empty-bucket path for the
// buckets the engine draws. Level instances (crates, fruit, ...) go through the
// model importer and Engine_InstanceSubmit.
//
// DRAW TIME ONLY. Nothing the game writes is changed: BuildBlock reads the
// same quadblock, vertex and texture data the PS1 draw reads, at load stage 8.
//
// QUADBLOCK TO TRIANGLES. The PS1 draw (game/226/226_00_DrawLevelOvr1P.c) cuts
// a block into four middle faces on the 3x3 vertex grid (sDrawLevelOvr1PGridFaceIndices,
// ~148): face k = grid corners {0,4,5,6} {4,1,6,7} {5,6,2,8} {6,7,8,3}, texture
// ptr_texture_mid[k]. The 5-bit field (draw_order_low >> (8 + 5k)) & 0x1f picks a word
// of R226.scratchInitTable (Select4x1ProjectedTableWord ~326). Of that word:
//   - slot v (0..3) takes the shift s = (word >> ((3 - v) * 8)) & 0x1f; the
//     selector constants of sDrawLevelOvr1P4x1FaceSelectors map s = 24, 16, 8, 0
//     to grid corner 0, 1, 2, 3 of the face, so corner = (24 - s) / 8 (checked
//     for all four faces against the four selector constants);
//   - bit 31 flips the winding rule (SelectDirectBit ~4409);
//   - bit 23 mirrors the UV order to {uv1, uv0, uv3, uv2} (WriteProjectedUv ~1036);
//   - bit 7 suppresses the UV write of slot 3 (the vertex keeps what an earlier
//     slot wrote for the same corner - a triangle-mode entry has slot 3 on the
//     corner of slot 2, so its second triangle is degenerate and dropped).
// The quad is the two triangles T1 = slots (0,1,2) and T2 = slots (1,3,2), both
// kept in PS1 when NCLIP > 0 (draw_order_low bit 31 = double sided: both ways).
// A flip bit set inverts that. The engine's cull keeps the opposite screen
// winding (platform/native_twin.c ~999, engine_model.c: a one-sided triangle
// retail keeps with NCLIP > 0 is written REVERSED, one it keeps with NCLIP < 0
// as listed), and the track's clip matrix has the same determinant sign as
// every model's, so the same rule holds here: flip clear -> reversed, flip set
// -> as listed, double sided -> as listed in a DOUBLE_SIDED material.
//
// TEXTURES. An IconGroup4 holds four layouts (far, middle, near, mosaic). The
// finest REAL level is 3, or 2 when the first word of level 3 is a mosaic table
// reference (negative, or a pointer into the level: DrawLevelOvr1P_TextureLodLevel
// ~913). A mosaic face is offered to platform/engine_track_mosaic.c
// (Engine_TrackMosaicFace, round of 2026-10-15): when it takes the face, the
// shader picks the tile and the face is keyed with the table's tpage word
// (bit 15 set, clut 0) and drawn as ENGINE_TEX_VRAM_TABLE; otherwise level 2
// stays. A pointer with bit 0 set is an AnimTex indirection (ptr - 1 is the
// AnimTex, its first word the IconGroup4 shown now): LAYOUT SOURCE = THE SLOT,
// READ AT DRAW TIME (round of 2026-10-16, the rule of the models too) - the face,
// mosaic or not, reads the animated row of its cell, which follows the cell every
// frame in cells and blend (engine_track_mosaic.c, ANIMATED TEXTURES; here
// ANIMATED ROWS). Only a face whose cell names no usable group at load keeps the
// layout it resolved then (frozen, counted).
//
// SCENERY VERTEX ANIMATION (round of 2026-10-15): faces on vertices the game
// animates in place (SCVert, configFlags & 4) are LIVE FACES, drawn every frame
// from the level vertices as the game left them (see LIVE FACES below).
//
// VISIBILITY (round of 2026-10-15): every frame the engine draws the faces of
// the blocks the game's own visibility lists show (see VISIBILITY below), as
// dynamic geometry; static meshes are no longer made.
//
// KNOWN LIMITS: one detail level (the finest); what stays on the PS1 path (the
// particles, for example) has no depth test and draws on top of the engine track.

#define EGT_KEYS_MAX 4096
#define EGT_MODELS_MAX 128
#define EGT_CELL_OPAQUE 0x3fe // above every cell a track face uses (<= 1020) and below the clear/glow/sky cell 0x3ff
#define EGT_CELL_SEMI 0x3fd   // after the opaque meshes: a lower cell is drawn later; also the decals (see DECALS)
#define EGT_BUCKET_WATER_RENDERED 8
#define EGT_BUCKET_WATER_LIST 9
#define EGT_BUCKET_COUNT 11
// The track's share of the frame's dynamic geometry. The limits are the per-bank
// rings: the scene's ENGINE_DYN_VERTICES_PER_FRAME vertices and 3/2 of that in
// indices (engine_scene.c, SceneBank), and the renderer's expanded store of the
// same 3/2 (ENGINE_DYN_EXPANDED, engine_render.c: one vertex per index). The track
// takes all of it but a fixed reserve for sky, water and effects (24576 vertices,
// 40960 expanded; with 262144 the track's share is 237568 / 352256). A frame
// whose shown faces do not fit is left to the PS1 walk (counted).
#define EGT_DYN_RESERVE_VERTICES 24576u
#define EGT_DYN_RESERVE_EXPANDED 40960u
#define EGT_DYN_VERTICES_MAX ((u32)ENGINE_DYN_VERTICES_PER_FRAME - EGT_DYN_RESERVE_VERTICES)
#define EGT_DYN_EXPANDED_MAX ((u32)ENGINE_DYN_VERTICES_PER_FRAME * 3u / 2u - EGT_DYN_RESERVE_EXPANDED)
// One dynamic draw indexes its own vertices with u16: a group with more shown
// vertices than this goes in several draws (EngTrack_FillFacesFrom).
#define EGT_DRAW_VERTICES_MAX 65532u
// A leaf flag no render-list rule names (RenderLists.c:128-160 read 0x02, 0x08,
// 0x10, 0x20, 0x80): its leaves hold the large 'blanket' blocks over finer ones
// (Cortex Castle, N. Gin Labs). Only counted (the tour line), drawn like any leaf.
#define EGT_LEAF_FLAG_SPECIAL 0x0040u

// --------------------------------------------------------------------------
// Pure data side (no game state): the part the selftest checks.

// One quadblock as plain data. layout[k] is the finest real layout of mid face
// k, already resolved, or NULL (face skipped).
typedef struct
{
	s16 pos[9][3];
	u8 color[9][3];
	u32 drawOrderLow;
	u32 drawOrderHigh;
	const struct TextureLayout *layout[4];
} EngTrackBlockIn;

// One face as built: up to four slot vertices, up to two triangles (indices
// into v, in the order the engine draws them).
typedef struct
{
	EngineVertex v[4];
	u8 vertCount;
	u8 triCount;
	u8 tri[2][3];
	u16 tpage;
	u16 clut;
	u8 doubleSided;
	s8 bias;
	int key; // set by the loader (EngTrack_KeysFind)
	// For Engine_TrackMosaicFace: the slot (0..3) whose UV vertex v took before
	// the mirror swap, bit 23 of the scratch word, and the mid face index k.
	u8 slotOf[4];
	u8 mirrored;
	u8 mid;
	// The grid corner (0..8) of each slot vertex: its level vertex is the block's index[grid[v]]
	// (the live faces read the vertex again every frame).
	u8 grid[4];
} EngTrackFace;

typedef struct
{
	u16 tpage;
	u16 clut;
	u8 doubleSided;
	s8 bias;
	// A decal key (see DECALS): lift > 0 in world units, and the plane normal its
	// faces share (normalQ: x and z in 1/1024, the key; normal: of its first face).
	u8 lift;
	s16 normalQ[2];
	float normal[3];
} EngTrackKey;

typedef struct
{
	EngTrackKey key[EGT_KEYS_MAX];
	int count;
} EngTrackKeySet;

internal const int s_egtGrid[4][4] = {{0, 4, 5, 6}, {4, 1, 6, 7}, {5, 6, 2, 8}, {6, 7, 8, 3}};

// Semi-transparent when the abr bits of the tpage are not 3 (the retail packet
// code 0x36/0x3e against 0x34/0x3c, DrawLevelOvr1P_SelectRawPrimitiveCode ~2504).
internal int EngTrack_IsSemi(u16 tpage)
{
	return (((tpage >> 5) & 3) != 3) ? 1 : 0;
}

// A layout the engine can use: the tpage sieve of the PS1 path (no bit from 9
// up) and not all zero (the blank layout of an invisible face).
internal int EngTrack_LayoutUsable(const struct TextureLayout *lay)
{
	const u8 *at = (const u8 *)lay;
	size_t i;

	if ((lay == NULL) || ((lay->tpage & 0xfe00) != 0))
	{
		return 0;
	}
	for (i = 0; i < sizeof(*lay); i++)
	{
		if (at[i] != 0)
		{
			return 1;
		}
	}
	return 0;
}

// The IconGroup4 a texture pointer names. Bit 0 set = AnimTex indirection: the
// real pointer is stored at (ptr - 1) and is resolved now. spanOk (may be NULL)
// vouches that [p, p + size) is readable level memory. *anim is set to 1 when
// the indirection was followed.
internal const struct TextureLayout *EngTrack_ResolveGroup(u32 ptr, int (*spanOk)(u32 p, u32 size), int *anim)
{
	if (anim != NULL)
	{
		*anim = 0;
	}
	if (ptr == 0u)
	{
		return NULL;
	}
	if ((ptr & 1u) != 0u)
	{
		u32 real = 0;

		if ((spanOk != NULL) && !spanOk(ptr - 1u, 4u))
		{
			return NULL;
		}
		memcpy(&real, (const void *)(size_t)(ptr - 1u), sizeof(real));
		if (anim != NULL)
		{
			*anim = 1;
		}
		ptr = real;
		if (ptr == 0u)
		{
			return NULL;
		}
	}
	if ((spanOk != NULL) && !spanOk(ptr, (u32)(4u * sizeof(struct TextureLayout))))
	{
		return NULL;
	}
	return (const struct TextureLayout *)(size_t)ptr;
}

// The finest real level of a group: level 3, or 2 when the first word of level 3
// is a mosaic table reference - negative, or a pointer isLevelPtr recognizes
// (DrawLevelOvr1P_TreatAsRetailNegativeTextureWord ~589). A level 3 that is
// blank while level 2 is not falls back to level 2. *mosaic = 1 when level 2 was
// taken because of the mosaic word.
internal const struct TextureLayout *EngTrack_FinestLayout(const struct TextureLayout *group, int (*isLevelPtr)(u32), int *mosaic)
{
	u32 word = 0;
	int isMosaic;

	memcpy(&word, &group[3], sizeof(word));
	isMosaic = (((s32)word < 0) || ((isLevelPtr != NULL) && isLevelPtr(word))) ? 1 : 0;
	if (mosaic != NULL)
	{
		*mosaic = isMosaic;
	}
	if (isMosaic)
	{
		return &group[2];
	}
	if (!EngTrack_LayoutUsable(&group[3]) && EngTrack_LayoutUsable(&group[2]))
	{
		return &group[2];
	}
	return &group[3];
}

// A key with a lift (1..255) also needs the normal: faces of one decal key share the
// plane normal within 1/1024 per component, so the vertex stage can measure the
// camera's distance to their plane with the key's normal. lift 0 ignores normal.
internal int EngTrack_KeysFindLifted(EngTrackKeySet *set, u16 tpage, u16 clut, int doubleSided, int bias, int lift, const float *normal)
{
	s16 q0 = 0;
	s16 q1 = 0;
	EngTrackKey *nk;
	int i;

	if ((lift <= 0) || (lift > 255) || (normal == NULL))
	{
		lift = 0;
	}
	else
	{
		q0 = (s16)floorf(normal[0] * 1024.0f + 0.5f);
		q1 = (s16)floorf(normal[2] * 1024.0f + 0.5f);
	}
	for (i = 0; i < set->count; i++)
	{
		const EngTrackKey *k = &set->key[i];

		if ((k->tpage == tpage) && (k->clut == clut) && (k->doubleSided == (u8)(doubleSided ? 1 : 0)) && (k->bias == (s8)bias) && (k->lift == (u8)lift) &&
		    (k->normalQ[0] == q0) && (k->normalQ[1] == q1))
		{
			return i;
		}
	}
	if (set->count >= EGT_KEYS_MAX)
	{
		return -1;
	}
	nk = &set->key[set->count];
	nk->tpage = tpage;
	nk->clut = clut;
	nk->doubleSided = (u8)(doubleSided ? 1 : 0);
	nk->bias = (s8)bias;
	nk->lift = (u8)lift;
	nk->normalQ[0] = q0;
	nk->normalQ[1] = q1;
	nk->normal[0] = (lift > 0) ? normal[0] : 0.0f;
	nk->normal[1] = (lift > 0) ? normal[1] : 1.0f;
	nk->normal[2] = (lift > 0) ? normal[2] : 0.0f;
	return set->count++;
}

internal int EngTrack_KeysFind(EngTrackKeySet *set, u16 tpage, u16 clut, int doubleSided, int bias)
{
	return EngTrack_KeysFindLifted(set, tpage, clut, doubleSided, bias, 0, NULL);
}

// Zero-area test on the three world points (the PS1 draws nothing for NCLIP = 0).
internal int EngTrack_ZeroArea(const s16 *a, const s16 *b, const s16 *c)
{
	const double ux = (double)b[0] - (double)a[0];
	const double uy = (double)b[1] - (double)a[1];
	const double uz = (double)b[2] - (double)a[2];
	const double vx = (double)c[0] - (double)a[0];
	const double vy = (double)c[1] - (double)a[1];
	const double vz = (double)c[2] - (double)a[2];
	const double cx = uy * vz - uz * vy;
	const double cy = uz * vx - ux * vz;
	const double cz = ux * vy - uy * vx;

	return ((cx == 0.0) && (cy == 0.0) && (cz == 0.0)) ? 1 : 0;
}

// The faces of one block, at most four, into out[]. table = R226.scratchInitTable
// (tableCount words). Returns the number of faces written; *fieldsOut (may be
// NULL) counts faces whose 5-bit field pointed past the table (they use entry 0).
internal int EngTrack_BuildBlock(const EngTrackBlockIn *in, const u32 *table, int tableCount, EngTrackFace *out, int *fieldsOut)
{
	static const int slotsOfTri[2][3] = {{0, 1, 2}, {1, 3, 2}};
	int faces = 0;
	int k;

	for (k = 0; k < 4; k++)
	{
		const struct TextureLayout *lay = in->layout[k];
		EngTrackFace *f;
		u32 field;
		u32 tw;
		int corner[4] = {0, 0, 0, 0};
		int grid[4] = {0, 0, 0, 0};
		int uvSlot[4] = {0, 1, 2, 3};
		int slotOf[4] = {0, 1, 2, 3};
		int uvOk[4] = {1, 1, 1, 1};
		int valid[2] = {0, 0};
		int bad = 0;
		int mirrored;
		int v;
		int t;
		float uvU[4];
		float uvV[4];

		if (lay == NULL)
		{
			continue;
		}
		field = (in->drawOrderLow >> (8 + (5 * k))) & 0x1fu;
		if ((int)field >= tableCount)
		{
			field = 0;
			if (fieldsOut != NULL)
			{
				(*fieldsOut)++;
			}
		}
		tw = table[field];
		mirrored = ((tw >> 23) & 1u) ? 1 : 0;
		for (v = 0; v < 4; v++)
		{
			const u32 s = (tw >> ((3 - v) * 8)) & 0x1fu;

			if ((s > 24u) || ((s & 7u) != 0u))
			{
				bad = 1;
				break;
			}
			corner[v] = (int)((24u - s) / 8u);
			grid[v] = s_egtGrid[k][corner[v]];
			uvSlot[v] = mirrored ? (v ^ 1) : v;
		}
		if (bad)
		{
			continue;
		}
		uvU[0] = (float)lay->u0;
		uvV[0] = (float)lay->v0;
		uvU[1] = (float)lay->u1;
		uvV[1] = (float)lay->v1;
		uvU[2] = (float)lay->u2;
		uvV[2] = (float)lay->v2;
		uvU[3] = (float)lay->u3;
		uvV[3] = (float)lay->v3;

		f = &out[faces];
		memset(f, 0, sizeof(*f));
		for (v = 0; v < 4; v++)
		{
			EngineVertex *ev = &f->v[v];
			int src = uvSlot[v];

			if ((v == 3) && (((tw >> 7) & 1u) != 0u))
			{
				// Slot 3's UV write is suppressed: the vertex keeps the UV an
				// earlier slot wrote for the same grid corner.
				int j;

				uvOk[3] = 0;
				for (j = 2; j >= 0; j--)
				{
					if (corner[j] == corner[3])
					{
						src = uvSlot[j];
						slotOf[3] = j;
						uvOk[3] = 1;
						break;
					}
				}
			}
			ev->position[0] = (float)in->pos[grid[v]][0];
			ev->position[1] = (float)in->pos[grid[v]][1];
			ev->position[2] = (float)in->pos[grid[v]][2];
			ev->texcoord[0] = uvU[src];
			ev->texcoord[1] = uvV[src];
			ev->color[0] = in->color[grid[v]][0];
			ev->color[1] = in->color[grid[v]][1];
			ev->color[2] = in->color[grid[v]][2];
			ev->color[3] = 255;
		}

		f->doubleSided = ((in->drawOrderLow & 0x80000000u) != 0u) ? 1 : 0;
		for (t = 0; t < 2; t++)
		{
			const int a = slotsOfTri[t][0];
			const int b = slotsOfTri[t][1];
			const int c = slotsOfTri[t][2];

			valid[t] = ((corner[a] != corner[b]) && (corner[b] != corner[c]) && (corner[a] != corner[c]) && uvOk[a] && uvOk[b] && uvOk[c] &&
			            !EngTrack_ZeroArea(in->pos[grid[a]], in->pos[grid[b]], in->pos[grid[c]]))
			               ? 1
			               : 0;
			if (valid[t])
			{
				// As listed when the face is two-sided or the flip bit is set, else reversed (see the header).
				const int listed = f->doubleSided || (((tw >> 31) & 1u) != 0u);

				f->tri[f->triCount][0] = (u8)(listed ? a : c);
				f->tri[f->triCount][1] = (u8)b;
				f->tri[f->triCount][2] = (u8)(listed ? c : a);
				f->triCount++;
			}
		}
		if (f->triCount == 0)
		{
			continue;
		}
		f->vertCount = (valid[1] || !valid[0]) ? 4 : 3;
		// The UV box of the face (engine.h, THE UV BOX): the box of the corners its
		// triangles use, on all four slot vertices. A face drawn through the mosaic
		// table (texSource 3) carries it unread.
		{
			u8 box[4] = {255, 255, 0, 0};

			for (t = 0; t < f->triCount; t++)
			{
				int c;

				for (c = 0; c < 3; c++)
				{
					const EngineVertex *ev = &f->v[f->tri[t][c]];
					const u8 u = Engine_UvBoxTexel(ev->texcoord[0]);
					const u8 tv = Engine_UvBoxTexel(ev->texcoord[1]);

					box[0] = (u < box[0]) ? u : box[0];
					box[1] = (tv < box[1]) ? tv : box[1];
					box[2] = (u > box[2]) ? u : box[2];
					box[3] = (tv > box[3]) ? tv : box[3];
				}
			}
			Engine_VertexSetBox(f->v, 4, box[0], box[1], box[2], box[3]);
		}
		f->tpage = lay->tpage;
		f->clut = lay->clut;
		f->bias = (s8)((in->drawOrderHigh >> (8 * k)) & 0xffu);
		f->key = -1;
		for (v = 0; v < 4; v++)
		{
			f->slotOf[v] = (u8)slotOf[v];
		}
		for (v = 0; v < 4; v++)
		{
			f->grid[v] = (u8)grid[v];
		}
		f->mirrored = (u8)mirrored;
		f->mid = (u8)k;
		faces++;
	}
	return faces;
}

// DRAW FACES (round of 2026-10-15). Every track face is kept at load with what
// a frame needs to draw it: its built vertices (texture coordinates of the
// mosaic and AnimTex rows, key, material), the level vertex of each slot vertex,
// and where the PS1 walk finds its visibility (below). Every frame the faces the
// game's visibility shows are written as dynamic geometry, one draw per material.
//
// LIVE FACES. Where the game moves level vertices itself every frame - the
// scenery vertex animation of a level with configFlags & 4 (AnimateQuad,
// game/RenderLevel/AnimateWater.c, writes position and colour of the SCVert
// vertices in place; MainFrame_RenderFrame.c 1P path) - a face touching such a
// vertex reads position and colour from its level vertices every frame. Read
// only: the game writes, the engine reads.
//
// VISIBILITY. The PS1 draws a block only when
//   1. the walk of the BSP reaches its leaf: RenderLists_Walk1P2P
//      (game/RenderLevel/RenderLists.c:261-331) follows a child only when its bit
//      in the frame's leaf list is set - RenderLists_IsVisible (:28-37), word
//      (node >> 5) & 0x1ff, bit 31 - (node & 31) of gGT->visMem1->visLeafList[0]
//      (game/MAIN/MainFrame_RenderFrame.c:1004) - and its box overlaps the view's
//      box (RenderLists_BoxOverlapsPushBuffer, :40-44, pushBuffer[0].bbox); the
//      frustum test it adds for leaves (:301) is left to the GPU; and
//   2. its bit in the frame's face list is set: the walk seeds the bit pointer
//      with the leaf's FIRST block (word blockID >> 5, index blockID & 31,
//      game/226/226_00_DrawLevelOvr1P.c:9497-9505) and consumes one bit per
//      block of the leaf in array order - bit 31 - index, the index stepping
//      down and moving to the next word at index 31 below 0 (226_00:9507-9525,
//      used at :9558, :9663, :9921) - in visMem1->visFaceList[0]
//      (MainFrame_RenderFrame.c:1007).
// Both lists are refilled for the frame by MainFrame_VisMemFullFrame
// (MainFrame_RenderFrame.c:52, game/MAIN/MainFrame.c:771-817). For 1 the engine
// reads the leaves the PS1 walk itself linked into view 0's render lists
// (gGT->LevRenderLists[0]) at the first bucket of DrawLevelOvr1P, after the walk
// of the same frame (EngTrack_SubmitShown); its own mirror of the walk
// (EngTrack_WalkLeaves) only checks itself against them (counted). For 2 it
// consumes the face bits as above. Everything is read only; frustum culling
// is the GPU's. Where a level hides geometry from a place through its PVS
// (the walls of Roo's Tubes, Hot Air Skyway, Mystery Caves), the engine hides it too.
typedef struct
{
	EngTrackFace face; // texture coordinates, triangles, key as built
	u16 vertex[4];     // the level vertex (mesh->ptrVertexArray index) of each slot vertex
	int material;
	u16 faceWord; // the face-list bit of its block
	u16 leafNode; // its leaf (BSP node index)
	u8 faceBit;
	u8 semi;
	u8 live;    // 1 = position and colour read from the level vertices every frame
	u8 special; // its leaf has flag 0x40 (counted per frame for the tour line)
	// For the oracle (RETAIL ORACLE): its quadblock (mesh->ptrQuadBlockArray index);
	// the group level a VRAM face took (3, or 2); anim = its texture pointer names an
	// AnimTex cell; animRow = it reads the whole-face row that follows the cell every
	// frame (anim without animRow: the layout of the load, frozen); tableRefused = a
	// mosaic face the table refused (drawn with level 2).
	int block;
	u8 stage;
	u8 anim;
	u8 animRow;
	u8 tableRefused;
	// ANIMATED ROWS: 1 + its entry in s_egtAnim when its cell's frames give more
	// than one blend (its key and material follow the row every frame), else 0.
	u16 animEntry;
} EngTrackDrawFace;

// The faces of one material (index range of the sorted faces), drawn as one dynamic draw.
typedef struct
{
	int material;
	int semi;
	int first;
	int count;
	int lift;         // its key's lift (0 = no decal)
	float normal[3];  // its key's plane normal (decals)
} EngTrackDrawGroup;

// 1 when a slot vertex of the face is a marked level vertex (live[] has
// numVertex entries; an index outside it counts as not marked).
internal int EngTrack_FaceTouches(const EngTrackFace *f, const u16 *blockIndex, const u8 *live, int numVertex)
{
	int v;

	for (v = 0; v < f->vertCount; v++)
	{
		const int index = (int)blockIndex[f->grid[v]];

		if ((index < numVertex) && live[index])
		{
			return 1;
		}
	}
	return 0;
}

// DECALS (round of 2026-10-15 night). The PS1 layers faces with the s8 bias of
// draw_order_high: it is added to the OT index (226_00_DrawLevelOvr1P.c ~2352), so
// of two faces at about the same depth the one with the LOWER bias is painted
// later. Level data relies on it for faces that do not lie on their base: the
// boost pads of Turbo Track lie flat at y 96 on a road (bias 16) that climbs 1 in
// 12 under them, so half a pad is up to 22.7 units UNDER the road. A depth test
// needs its own rule; the engine does not copy the OT order:
//   - A DECAL is a floor face (normal within 60 degrees of up) every point of
//     which lies over or under a floor face with a HIGHER bias, nearly parallel
//     (normals within 25 degrees), at most EGT_DECAL_GAP units away along y.
//     Found once at load (EngTrack_FindDecals); live faces take no part.
//   - Its LIFT is the most it lies under such a base, plus EGT_DECAL_MARGIN,
//     rounded up to whole units (so at least the margin).
//   - It is drawn after the other opaque track faces (cell EGT_CELL_DECAL, the
//     higher bias first), tests depth but does not write it, and its depth is
//     that of its plane moved the lift toward the camera along its normal (the
//     vertex stage of "ne": params3.z = lift, params4 = normal and normal . camera;
//     the screen position does not change). Karts and objects come later and
//     test against the base, so a lifted pad never covers a kart on the road.
// Every other face keeps the fractional depth offset of its bias.
#define EGT_DECAL_GAP 32.0f      // the largest distance along y between a decal and its base
#define EGT_DECAL_MARGIN 2.0f    // a decal ends at least this far in front of its base
#define EGT_DECAL_FLOOR 0.5f     // normal y of a floor face (60 degrees)
#define EGT_DECAL_PARALLEL 0.9f  // cosine between the normals of a decal and its base
#define EGT_DECAL_SHRINK 0.96f   // samples pulled toward the triangle centre (shared edges)

// The unit plane normal of a built face (area-weighted over its triangles), turned
// to point up (y >= 0). 0 = no area.
internal int EngTrack_FaceNormal(const EngTrackFace *f, float n[3])
{
	double s[3] = {0.0, 0.0, 0.0};
	double len;
	int t;

	for (t = 0; t < f->triCount; t++)
	{
		const float *a = f->v[f->tri[t][0]].position;
		const float *b = f->v[f->tri[t][1]].position;
		const float *c = f->v[f->tri[t][2]].position;
		const double ux = (double)b[0] - a[0], uy = (double)b[1] - a[1], uz = (double)b[2] - a[2];
		const double vx = (double)c[0] - a[0], vy = (double)c[1] - a[1], vz = (double)c[2] - a[2];

		// Every triangle of a face has the same winding, so the sum keeps the plane.
		s[0] += uy * vz - uz * vy;
		s[1] += uz * vx - ux * vz;
		s[2] += ux * vy - uy * vx;
	}
	len = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (len < 1e-6)
	{
		return 0;
	}
	if (s[1] < 0.0)
	{
		len = -len;
	}
	n[0] = (float)(s[0] / len);
	n[1] = (float)(s[1] / len);
	n[2] = (float)(s[2] / len);
	return 1;
}

// The height (y) of face f over the point (x, z), from the triangle that holds it
// in x/z. 0 = no triangle holds it.
internal int EngTrack_FaceHeightAt(const EngTrackFace *f, float x, float z, float *y)
{
	int t;

	for (t = 0; t < f->triCount; t++)
	{
		const float *a = f->v[f->tri[t][0]].position;
		const float *b = f->v[f->tri[t][1]].position;
		const float *c = f->v[f->tri[t][2]].position;
		const float det = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
		float l1;
		float l2;
		float l3;

		if (fabsf(det) < 1e-3f)
		{
			continue;
		}
		l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / det;
		l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / det;
		l3 = 1.0f - l1 - l2;
		if ((l1 >= -1e-4f) && (l2 >= -1e-4f) && (l3 >= -1e-4f))
		{
			*y = l1 * a[1] + l2 * b[1] + l3 * c[1];
			return 1;
		}
	}
	return 0;
}

// The lift of face a over its candidate bases, or 0 when a is no decal. Samples
// per triangle: the corners, three points on every edge and the centre, pulled
// EGT_DECAL_SHRINK toward the centre; every sample needs a base within the gap.
internal int EngTrack_DecalLift(const EngTrackFace *a, const EngTrackFace *const *bases, int baseCount)
{
	float need = -EGT_DECAL_GAP;
	int t;

	for (t = 0; t < a->triCount; t++)
	{
		const float *p[3];
		float c[3];
		int s;
		int k;

		for (k = 0; k < 3; k++)
		{
			p[k] = a->v[a->tri[t][k]].position;
		}
		for (k = 0; k < 3; k++)
		{
			c[k] = (p[0][k] + p[1][k] + p[2][k]) / 3.0f;
		}
		for (s = 0; s < 13; s++)
		{
			float q[3];
			float best = 0.0f;
			int found = 0;
			int b;

			for (k = 0; k < 3; k++)
			{
				if (s < 3)
				{
					q[k] = p[s][k];
				}
				else if (s < 12)
				{
					const int e = (s - 3) / 3;
					const float w = (float)((s - 3) % 3 + 1) / 4.0f;

					q[k] = p[e][k] + w * (p[(e + 1) % 3][k] - p[e][k]);
				}
				else
				{
					q[k] = c[k];
				}
				q[k] = c[k] + EGT_DECAL_SHRINK * (q[k] - c[k]);
			}
			for (b = 0; b < baseCount; b++)
			{
				float y;

				if (EngTrack_FaceHeightAt(bases[b], q[0], q[2], &y) && (fabsf(y - q[1]) <= EGT_DECAL_GAP) && (!found || ((y - q[1]) > best)))
				{
					best = y - q[1];
					found = 1;
				}
			}
			if (!found)
			{
				return 0;
			}
			need = (best > need) ? best : need;
		}
	}
	if (a->triCount == 0)
	{
		return 0;
	}
	// Rounded up to whole units; a coplanar base (need 0 within float noise) gives the margin.
	return (int)ceilf(((need > 0.0f) ? need : 0.0f) + EGT_DECAL_MARGIN - 1e-3f);
}

typedef struct
{
	float n[3];
	float x0, x1, z0, z1;
	int floor;
} EngTrackDecalInfo;

typedef struct
{
	int bias;
	int index;
} EngTrackDecalOrder;

internal int EngTrack_DecalOrderCompare(const void *a, const void *b)
{
	const EngTrackDecalOrder *x = (const EngTrackDecalOrder *)a;
	const EngTrackDecalOrder *y = (const EngTrackDecalOrder *)b;

	if (x->bias != y->bias)
	{
		return (x->bias > y->bias) ? -1 : 1;
	}
	return (x->index < y->index) ? -1 : ((x->index > y->index) ? 1 : 0);
}

// The decals among faces[0..count) (see DECALS): liftOut[i] = lift or 0, and
// normalOut[3 i..3 i + 2] = the face's up normal (every face). Returns the number
// of decals, -1 when out of memory (then no face is a decal).
internal int EngTrack_FindDecals(const EngTrackDrawFace *faces, int count, u8 *liftOut, float *normalOut)
{
	EngTrackDecalInfo *info;
	EngTrackDecalOrder *order;
	const EngTrackFace **bases;
	int floors = 0;
	int decals = 0;
	int i;

	memset(liftOut, 0, (size_t)count);
	if (count <= 0)
	{
		return 0;
	}
	info = (EngTrackDecalInfo *)malloc((size_t)count * sizeof(EngTrackDecalInfo));
	order = (EngTrackDecalOrder *)malloc((size_t)count * sizeof(EngTrackDecalOrder));
	bases = (const EngTrackFace **)malloc((size_t)count * sizeof(const EngTrackFace *));
	if ((info == NULL) || (order == NULL) || (bases == NULL))
	{
		free(info);
		free(order);
		free(bases);
		return -1;
	}
	for (i = 0; i < count; i++)
	{
		const EngTrackFace *f = &faces[i].face;
		EngTrackDecalInfo *d = &info[i];
		int v;

		d->floor = (!faces[i].live && EngTrack_FaceNormal(f, d->n) && (d->n[1] >= EGT_DECAL_FLOOR)) ? 1 : 0;
		if (!d->floor)
		{
			d->n[0] = 0.0f;
			d->n[1] = 1.0f;
			d->n[2] = 0.0f;
		}
		normalOut[3 * i + 0] = d->n[0];
		normalOut[3 * i + 1] = d->n[1];
		normalOut[3 * i + 2] = d->n[2];
		d->x0 = d->x1 = f->v[0].position[0];
		d->z0 = d->z1 = f->v[0].position[2];
		for (v = 1; v < f->vertCount; v++)
		{
			const float *p = f->v[v].position;

			d->x0 = (p[0] < d->x0) ? p[0] : d->x0;
			d->x1 = (p[0] > d->x1) ? p[0] : d->x1;
			d->z0 = (p[2] < d->z0) ? p[2] : d->z0;
			d->z1 = (p[2] > d->z1) ? p[2] : d->z1;
		}
		if (d->floor)
		{
			order[floors].bias = (int)f->bias;
			order[floors].index = i;
			floors++;
		}
	}
	qsort(order, (size_t)floors, sizeof(EngTrackDecalOrder), EngTrack_DecalOrderCompare);
	for (i = 0; i < count; i++)
	{
		const EngTrackDecalInfo *a = &info[i];
		int baseCount = 0;
		int j;
		int lift;

		if (!a->floor)
		{
			continue;
		}
		// The floors sorted by bias, the highest first: the bases are a prefix.
		for (j = 0; (j < floors) && (order[j].bias > (int)faces[i].face.bias); j++)
		{
			const EngTrackDecalInfo *b = &info[order[j].index];

			if ((b->x1 < a->x0) || (a->x1 < b->x0) || (b->z1 < a->z0) || (a->z1 < b->z0))
			{
				continue;
			}
			if ((a->n[0] * b->n[0] + a->n[1] * b->n[1] + a->n[2] * b->n[2]) < EGT_DECAL_PARALLEL)
			{
				continue;
			}
			bases[baseCount++] = &faces[order[j].index].face;
		}
		if (baseCount == 0)
		{
			continue;
		}
		lift = EngTrack_DecalLift(&faces[i].face, bases, baseCount);
		if (lift > 0)
		{
			liftOut[i] = (u8)((lift > 255) ? 255 : lift);
			decals++;
		}
	}
	free(info);
	free(order);
	free(bases);
	return decals;
}

// The world position of the camera of a view matrix (view = M * world + t, column
// major as EngineMat4 keeps it): -M^-1 * t. 0 when M is singular.
internal int EngTrack_CameraWorld(const EngineMat4 *viewFromWorld, float out[3])
{
	const float *m = viewFromWorld->m;
	// a[r][c] = m[c * 4 + r]
	const double a00 = m[0], a01 = m[4], a02 = m[8];
	const double a10 = m[1], a11 = m[5], a12 = m[9];
	const double a20 = m[2], a21 = m[6], a22 = m[10];
	const double t0 = -(double)m[12], t1 = -(double)m[13], t2 = -(double)m[14];
	const double det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);

	if ((det > -1e-12) && (det < 1e-12))
	{
		return 0;
	}
	// Cramer's rule for M * c = -t.
	out[0] = (float)((t0 * (a11 * a22 - a12 * a21) - a01 * (t1 * a22 - a12 * t2) + a02 * (t1 * a21 - a11 * t2)) / det);
	out[1] = (float)((a00 * (t1 * a22 - a12 * t2) - t0 * (a10 * a22 - a12 * a20) + a02 * (a10 * t2 - t1 * a20)) / det);
	out[2] = (float)((a00 * (a11 * t2 - t1 * a21) - a01 * (a10 * t2 - t1 * a20) + t0 * (a10 * a21 - a11 * a20)) / det);
	return 1;
}

// The face-list bits of the count blocks of a leaf whose first block has
// firstBlockID, as the PS1 walk consumes them (see VISIBILITY).
internal void EngTrack_LeafFaceBits(u16 firstBlockID, int count, u16 *wordOut, u8 *bitOut)
{
	int word = (int)(firstBlockID >> 5);
	int index = (int)(firstBlockID & 0x1fu);
	int i;

	for (i = 0; i < count; i++)
	{
		if (index < 0)
		{
			index = 31;
			word++;
		}
		wordOut[i] = (u16)word;
		bitOut[i] = (u8)(31 - index);
		index--;
	}
}

// The leaf-list bit of BSP node `node`, as RenderLists_IsVisible tests it.
internal void EngTrack_LeafBit(int node, u16 *wordOut, u8 *bitOut)
{
	*wordOut = (u16)((node >> 5) & 0x1ff);
	*bitOut = (u8)(31 - (node & 31));
}

// Bit `bit` of word `word` of a list of wordCount words. A word past the list
// counts as set (the face is drawn; the loader counts such blocks).
internal int EngTrack_ListBit(const u32 *list, int wordCount, u16 word, u8 bit)
{
	if ((int)word >= wordCount)
	{
		return 1;
	}
	return ((list[word] >> bit) & 1u) ? 1 : 0;
}

// One child of the walk: followed when its leaf-list bit is set and its box
// overlaps the view's box. 0 = the stack is full.
internal int EngTrack_WalkPush(const struct BSP *root, int nodeCount, const u32 *leafList, int leafWords, const struct BoundingBox *viewBox, BspChildId child,
                               u16 *walkStack, int *sp, int stackMax)
{
	const int node = (int)((u16)child & BSP_CHILD_ID_INDEX_MASK);
	const struct BoundingBox *box;
	u16 word = 0;
	u8 bit = 0;

	if ((child < 0) || (node >= nodeCount))
	{
		return 1;
	}
	EngTrack_LeafBit(node, &word, &bit);
	if ((leafList != NULL) && !EngTrack_ListBit(leafList, leafWords, word, bit))
	{
		return 1;
	}
	box = &root[node].box;
	if ((viewBox != NULL) && !((box->min.x <= viewBox->max.x) && (viewBox->min.x <= box->max.x) && (box->min.y <= viewBox->max.y) &&
	                           (viewBox->min.y <= box->max.y) && (box->min.z <= viewBox->max.z) && (viewBox->min.z <= box->max.z)))
	{
		return 1;
	}
	if (*sp >= stackMax)
	{
		return 0;
	}
	walkStack[(*sp)++] = (u16)child;
	return 1;
}

// The leaves the PS1 walk reaches this frame (VISIBILITY 1): shown[] (nodeCount
// entries) is cleared and set for them. Returns the leaves reached, -1 when the
// walkStack (stackMax entries) ran full (the caller leaves the frame to the PS1 walk).
internal int EngTrack_WalkLeaves(const struct BSP *root, int nodeCount, const u32 *leafList, int leafWords, const struct BoundingBox *viewBox, u8 *shown,
                                 u16 *walkStack, int stackMax)
{
	int sp = 0;
	int reached = 0;
	int ok;

	memset(shown, 0, (size_t)nodeCount);
	if ((root == NULL) || (nodeCount <= 0))
	{
		return 0;
	}
	if ((root->flag & BSP_NODE_FLAG_LEAF) != 0)
	{
		shown[0] = 1;
		return 1;
	}
	ok = EngTrack_WalkPush(root, nodeCount, leafList, leafWords, viewBox, root->data.branch.childID[0], walkStack, &sp, stackMax) &&
	     EngTrack_WalkPush(root, nodeCount, leafList, leafWords, viewBox, root->data.branch.childID[1], walkStack, &sp, stackMax);
	while (ok && (sp > 0))
	{
		const u16 child = walkStack[--sp];
		const int node = (int)(child & BSP_CHILD_ID_INDEX_MASK);

		if ((child & BSP_CHILD_ID_LEAF_FLAG) != 0)
		{
			if (!shown[node])
			{
				shown[node] = 1;
				reached++;
			}
			continue;
		}
		ok = EngTrack_WalkPush(root, nodeCount, leafList, leafWords, viewBox, root[node].data.branch.childID[0], walkStack, &sp, stackMax) &&
		     EngTrack_WalkPush(root, nodeCount, leafList, leafWords, viewBox, root[node].data.branch.childID[1], walkStack, &sp, stackMax);
	}
	return ok ? reached : -1;
}

// The leaves the PS1 walk linked into a frame's render lists (headCount list heads
// of struct VisMemBspListNode, each node naming a BSP leaf): shown[] (nodeCount
// entries) is cleared and set for them. Returns the leaves, -1 for a node that is
// no leaf of root or a list longer than nodeCount (a broken list: not followed).
internal int EngTrack_LeavesOfLists(const struct BSP *root, int nodeCount, struct VisMemBspListNode *const *heads, int headCount, u8 *shown)
{
	int count = 0;
	int h;

	memset(shown, 0, (size_t)nodeCount);
	for (h = 0; h < headCount; h++)
	{
		const struct VisMemBspListNode *node;
		int steps = 0;

		for (node = heads[h]; node != NULL; node = node->next)
		{
			const ptrdiff_t at = node->bsp - root;

			if ((node->bsp == NULL) || (at < 0) || (at >= nodeCount) || ((root[at].flag & BSP_NODE_FLAG_LEAF) == 0) || (++steps > nodeCount))
			{
				return -1;
			}
			if (!shown[at])
			{
				shown[at] = 1;
				count++;
			}
		}
	}
	return count;
}

// 1 = the frame shows the face: the walk reached its leaf (leafShown, NULL = every
// leaf) and the face list has its block's bit (NULL = every block).
internal int EngTrack_FaceShown(const EngTrackDrawFace *f, const u32 *faceList, int faceWords, const u8 *leafShown)
{
	if ((leafShown != NULL) && !leafShown[f->leafNode])
	{
		return 0;
	}
	if ((faceList != NULL) && !EngTrack_ListBit(faceList, faceWords, f->faceWord, f->faceBit))
	{
		return 0;
	}
	return 1;
}

// The vertices and indices of the faces of faces[first..count) with shown[i] set
// (shown NULL: all), up to maxVertices vertices: a live face with its level
// vertices as they are now (position and colour from verts), every other face as
// built. Returns the vertex count; *indexCount gets the index count and *next the
// first face not written (count when all were). outV needs 4 and outI 6 entries
// per face written; the indices count from 0 for every call (one draw each).
internal u32 EngTrack_FillFacesFrom(const EngTrackDrawFace *faces, const u8 *shown, int first, int count, const struct LevVertex *verts, EngineVertex *outV,
                                    u16 *outI, u32 maxVertices, u32 *indexCount, int *next)
{
	u32 vertexCount = 0;
	u32 indices = 0;
	int i;

	for (i = first; i < count; i++)
	{
		const EngTrackFace *f = &faces[i].face;
		int c;
		int t;

		if ((shown != NULL) && !shown[i])
		{
			continue;
		}
		if ((vertexCount + f->vertCount) > maxVertices)
		{
			break;
		}
		for (c = 0; c < f->vertCount; c++)
		{
			EngineVertex *ev = &outV[vertexCount + (u32)c];

			*ev = f->v[c];
			if (faces[i].live)
			{
				const struct LevVertex *lv = &verts[faces[i].vertex[c]];

				ev->position[0] = (float)lv->pos.x;
				ev->position[1] = (float)lv->pos.y;
				ev->position[2] = (float)lv->pos.z;
				ev->color[0] = lv->color_hi[0];
				ev->color[1] = lv->color_hi[1];
				ev->color[2] = lv->color_hi[2];
			}
		}
		for (t = 0; t < f->triCount; t++)
		{
			outI[indices++] = (u16)(vertexCount + f->tri[t][0]);
			outI[indices++] = (u16)(vertexCount + f->tri[t][1]);
			outI[indices++] = (u16)(vertexCount + f->tri[t][2]);
		}
		vertexCount += f->vertCount;
	}
	*indexCount = indices;
	*next = i;
	return vertexCount;
}

internal u32 EngTrack_FillFaces(const EngTrackDrawFace *faces, const u8 *shown, int count, const struct LevVertex *verts, EngineVertex *outV, u16 *outI,
                                u32 *indexCount)
{
	int next;

	return EngTrack_FillFacesFrom(faces, shown, 0, count, verts, outV, outI, EGT_DRAW_VERTICES_MAX, indexCount, &next);
}

// The mesh frame of an animation word of a retail model: the importer makes one
// mesh frame per LOGICAL frame of an animation - a half-frame animation
// (numFrames with the top bit) included, its odd frames as the in-between poses
// retail interpolates (platform/native_twin.c:296-334, RB:2236-2239) - so the
// word is the frame, clamped to the last logical one. Halving it the way the
// retail queue finds its STORED frame (NativeRenderLayer_AnimFrameNumber) would
// show half the time on these animations. (The start banner has no animation:
// the game moves it in place, see LIVE FRAME at Engine_TrackInstanceRoute.)
internal int EngTrack_LogicalFrame(u16 numFrames, u16 animFrame)
{
	const int logical = (int)(numFrames & 0x7fffu);

	if (logical <= 0)
	{
		return 0;
	}
	return ((int)animFrame < logical) ? (int)animFrame : (logical - 1);
}

// --------------------------------------------------------------------------
// Game side

typedef struct
{
	const struct Model *model;
	char name[17];
	s16 id;
	EngineModelImport imp;
	int imported;
	int transparent;
	int live;          // drawn from its live frame (Engine_ModelLiveFrame)
	int levelListOnly; // in the level's model list, used by no InstDef (RUNTIME INSTANCES)
	int liveRefusedLogged;
	u32 drawn;
	u32 declined;
	// POSITION CHECK: drawn instances whose screen box was made through both
	// matrices, and the largest centre difference (fractions of the view).
	u32 posChecked;
	float posMaxDx;
	float posMaxDy;
	char why[96]; // Engine_ModelImportWhy of a failed import
} EngTrackModel;

global_variable EngTrackKeySet s_egtKeys;
global_variable const struct Level *s_egtLevel;
global_variable const struct mesh_info *s_egtMesh; // with s_egtLevel and s_egtLevelID: the level the faces were built for
global_variable int s_egtLevelID = -1;
global_variable EngTrackModel s_egtModels[EGT_MODELS_MAX];
global_variable int s_egtModelCount;
global_variable int s_egtSubmitted; // this frame the engine track was submitted
global_variable int s_egtReportRegistered;
global_variable u64 s_egtFramesDrawn;
global_variable u64 s_egtBucketsSkipped;
global_variable u64 s_egtWaterVisits;
global_variable u64 s_egtInstDrawn;
global_variable u64 s_egtInstDeclined;
global_variable u64 s_egtRuntimeDrawn;    // of s_egtInstDrawn: runtime instances (RUNTIME INSTANCES)
global_variable u64 s_egtRuntimeDeclined; // of s_egtInstDeclined: runtime instances
// POSITION CHECK lines of runtime instances written since the load.
#define EGT_POSITION_LINES_MAX 6000u
global_variable u32 s_egtPositionLines;
// The live frame of a level instance (Engine_TrackInstanceRoute, LIVE FRAME),
// rebuilt per draw; the submit copies it.
#define EGT_LIVE_VERTICES_MAX 4096u
global_variable EngineVertex s_egtLiveVertices[EGT_LIVE_VERTICES_MAX];
// The faces of the loaded level (made at load, drawn every frame as far as the
// game's visibility shows them), sorted by (semi, material) into groups.
global_variable EngTrackDrawFace *s_egtFaces;
global_variable int s_egtFaceCount;
global_variable int s_egtFaceCap;
global_variable EngTrackDrawGroup *s_egtGroups;
global_variable int s_egtGroupCount;
global_variable u8 *s_egtShown;                         // per face, this frame
global_variable const struct LevVertex *s_egtVertices; // the level's vertex array the live faces read
global_variable EngineVertex *s_egtScratchV;           // one group's vertices, rebuilt per draw
global_variable u16 *s_egtScratchI;
global_variable int s_egtFaceWords; // words of the face list ((numQuadBlock + 31) >> 5)
global_variable int s_egtLeafWords; // words of the leaf list ((numBspNodes + 31) >> 5)
global_variable int s_egtNodeCount;   // BSP nodes of the level
global_variable u8 *s_egtBlockForm; // per quadblock: ENGINE_MOSAIC_FORM_* of its leaf (the oracle's mosaic tables)
#define EGT_BLOCK_WATER 1u   // s_egtBlockMark: the block lies in a water leaf
#define EGT_BLOCK_IN_LEAF 2u // s_egtBlockMark: the block lies in a leaf
global_variable u8 *s_egtBlockMark;    // per quadblock: EGT_BLOCK_WATER / EGT_BLOCK_IN_LEAF (the oracle)
global_variable int *s_egtFaceOfBlock; // per quadblock and mid face (4 * block + k): its draw face, -1 none (the oracle)
// Per quadblock and mid face (4 * block + k): what the load made of it (the oracle's markers).
enum
{
	EGT_FACE_SKIPPED = 0, // the block was not built: water, no leaf, invisible, a bad vertex index
	EGT_FACE_NO_LAYOUT,   // no usable layout
	EGT_FACE_DEGENERATE,  // EngTrack_BuildBlock made no triangle of it
	EGT_FACE_DROPPED,     // built, but no key or no room
	EGT_FACE_BUILT
};
global_variable u8 *s_egtFaceWhy;
global_variable u8 *s_egtLeafShown;   // per node, this frame: the PS1 walk linked the leaf
global_variable u8 *s_egtLeafWalk;    // per node, this frame: the engine's own walk reached the leaf (the check)
global_variable int s_egtWalkValid;
global_variable int s_egtPending; // collected, faces not yet submitted (EngTrack_SubmitShown)
global_variable const u32 *s_egtFaceListNow;
global_variable EngineMat4 s_egtScreenFromWorld;
global_variable float s_egtCameraWorld[3]; // the camera of view 0 in world units (decal lift)
global_variable int s_egtCameraWorldOk;
global_variable u64 s_egtNotWalkedFrames; // collected, but DrawLevelOvr1P did not come
global_variable u64 s_egtBadListFrames;   // a render list that is not a list of leaves
global_variable u64 s_egtWalkMissed;      // leaves the PS1 lists had and the engine walk not
global_variable u64 s_egtWalkExtra;       // leaves the engine walk had and the PS1 lists not (its frustum test)
global_variable u64 s_egtWalkDiffFrames;
global_variable u64 s_egtAnimSwitches; // ANIMATED ROWS: faces that took the material of another blend
global_variable int s_egtWalkMissedLogged; // per tour entry
global_variable u16 *s_egtWalkStack;  // the walk's stack (numBspNodes + 2 entries)
// Frames of the loaded level the engine could not draw: no visibility list, the
// dynamic store would not hold the shown faces (the PS1 walk draws those frames),
// or a submit refused after the first one went in (counted, the frame stays partial).
global_variable u64 s_egtNoVisFrames;
global_variable u64 s_egtOverBudgetFrames;
global_variable u64 s_egtRefusedFrames;
// Per frame peaks since the last tour entry began (reset by EngTrack_TourReport phase 0).
global_variable u32 s_egtPeakFaces;
global_variable u32 s_egtPeakVertices;
global_variable u32 s_egtPeakExpanded;
global_variable u32 s_egtPeakSpecial;

// Why a level instance was left to retail (the tour's per-track line).
enum
{
	EGT_DECLINE_MODEL = 0, // no model entry, model not imported, no header
	EGT_DECLINE_FLAGS,     // screen-space, billboard or hidden instance
	EGT_DECLINE_FRAME,     // animation frame outside the imported frames
	EGT_DECLINE_SUBMIT,    // Engine_InstanceSubmit refused
	EGT_DECLINE_COUNT
};
global_variable u64 s_egtInstDeclinedBy[EGT_DECLINE_COUNT];
// Race frames of the loaded level in which the game draws the level (the engine drew it or not).
global_variable u64 s_egtCollectFrames;

// What the last Engine_TrackLoad built, for the tour's per-track line.
global_variable struct
{
	int levelID;
	int faces;
	int liveFaces;
	int specialFaces;
	int groups;
	int materials;
	int dropped;
	int modelsImported;
	int modelsFailed;
	int listModelsImported; // models of the level list no InstDef uses (RUNTIME INSTANCES)
	int listModelsFailed;
} s_egtLoad = {-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// The counters at the first race frame of a tour entry (EngTrack_TourReport phase 0).
global_variable struct
{
	u64 framesDrawn;
	u64 collectFrames;
	u64 bucketsSkipped;
	u64 instDrawn;
	u64 instDeclined;
	u64 instDeclinedBy[EGT_DECLINE_COUNT];
	u64 runtimeDrawn;
	u64 runtimeDeclined;
	u64 noVisFrames;
	u64 overBudgetFrames;
	u64 refusedFrames;
	u64 notWalkedFrames;
	u64 badListFrames;
	u64 walkMissed;
	u64 walkExtra;
	u64 walkDiffFrames;
	EngineRenderCounters render;
	u32 fxDrawn[ENGINE_FX_KINDS];
	u32 fxRetail[ENGINE_FX_KINDS];
} s_egtTourBase;

internal int EngTrack_SpanOk(u32 p, u32 size)
{
	return DrawLevelOvr1P_IsNativeLevelSpan(p, size);
}

internal int EngTrack_IsLevelPtr(u32 word)
{
	return DrawLevelOvr1P_IsNativeLevelTexturePointer(word);
}

internal void EngTrack_Report(void)
{
	int i;

	Platform_Log("[CTR Engine] track: frames drawn %llu, PS1 level buckets skipped %llu, water buckets drawn by PS1 %llu\n",
	             (unsigned long long)s_egtFramesDrawn, (unsigned long long)s_egtBucketsSkipped, (unsigned long long)s_egtWaterVisits);
	Platform_Log("[CTR Engine] track: level instances drawn by the engine %llu, declined (retail draws them) %llu\n",
	             (unsigned long long)s_egtInstDrawn, (unsigned long long)s_egtInstDeclined);
	Platform_Log("[CTR Engine] track: of these runtime instances (no InstDef, no thread): drawn %llu, declined %llu\n", (unsigned long long)s_egtRuntimeDrawn,
	             (unsigned long long)s_egtRuntimeDeclined);
	Platform_Log("[CTR Engine] track: frames left to the PS1 walk: no visibility list %llu, over the dynamic budget %llu; frames with a submit refused %llu\n",
	             (unsigned long long)s_egtNoVisFrames, (unsigned long long)s_egtOverBudgetFrames, (unsigned long long)s_egtRefusedFrames);
	Platform_Log("[CTR Engine] track: walk check: frames without the level walk %llu, broken lists %llu; engine walk against the PS1 lists: leaves missed %llu, "
	             "extra %llu, in %llu frame(s)\n",
	             (unsigned long long)s_egtNotWalkedFrames, (unsigned long long)s_egtBadListFrames, (unsigned long long)s_egtWalkMissed,
	             (unsigned long long)s_egtWalkExtra, (unsigned long long)s_egtWalkDiffFrames);
	Platform_Log("[CTR Engine] track: animated faces that took the material of another blend %llu\n", (unsigned long long)s_egtAnimSwitches);
	for (i = 0; i < s_egtModelCount; i++)
	{
		const EngTrackModel *m = &s_egtModels[i];

		Platform_Log("[CTR Engine] track:   model '%s' id %d (%s): drawn %u, declined %u\n", m->name, (int)m->id, m->imported ? "imported" : "not imported",
		             m->drawn, m->declined);
	}
}

// The per-track lines of a --level-tour entry (Engine_TourRegister, include/platform/engine.h
// TRACK TOUR): phase 0 (first race frame) keeps the counters, phase 1 (after the
// last) prints what the entry added. The model table is the level's own (reset
// at every load), so its counts are printed as they stand.
internal void EngTrack_TourReport(int entry, int levelID, int phase)
{
	EngineRenderCounters rc;
	u32 fxDrawn[ENGINE_FX_KINDS];
	u32 fxRetail[ENGINE_FX_KINDS];
	int i;

	Engine_RenderCountersGet(&rc);
	Engine_FxCountersGet(fxDrawn, fxRetail);
	if (phase == 0)
	{
		s_egtTourBase.framesDrawn = s_egtFramesDrawn;
		s_egtTourBase.collectFrames = s_egtCollectFrames;
		s_egtTourBase.bucketsSkipped = s_egtBucketsSkipped;
		s_egtTourBase.instDrawn = s_egtInstDrawn;
		s_egtTourBase.instDeclined = s_egtInstDeclined;
		memcpy(s_egtTourBase.instDeclinedBy, s_egtInstDeclinedBy, sizeof(s_egtTourBase.instDeclinedBy));
		s_egtTourBase.runtimeDrawn = s_egtRuntimeDrawn;
		s_egtTourBase.runtimeDeclined = s_egtRuntimeDeclined;
		s_egtTourBase.noVisFrames = s_egtNoVisFrames;
		s_egtTourBase.overBudgetFrames = s_egtOverBudgetFrames;
		s_egtTourBase.refusedFrames = s_egtRefusedFrames;
		s_egtTourBase.notWalkedFrames = s_egtNotWalkedFrames;
		s_egtTourBase.badListFrames = s_egtBadListFrames;
		s_egtTourBase.walkMissed = s_egtWalkMissed;
		s_egtTourBase.walkExtra = s_egtWalkExtra;
		s_egtTourBase.walkDiffFrames = s_egtWalkDiffFrames;
		s_egtWalkMissedLogged = 0;
		s_egtPeakFaces = 0;
		s_egtPeakVertices = 0;
		s_egtPeakExpanded = 0;
		s_egtPeakSpecial = 0;
		s_egtTourBase.render = rc;
		memcpy(s_egtTourBase.fxDrawn, fxDrawn, sizeof(fxDrawn));
		memcpy(s_egtTourBase.fxRetail, fxRetail, sizeof(fxRetail));
		return;
	}
	{
		const EngineRenderCounters *b = &s_egtTourBase.render;
		const u32 refusedScene = rc.refusedScene - b->refusedScene;
		const u32 refusedCell = rc.refusedCell - b->refusedCell;
		const u32 refusedArena = rc.refusedArena - b->refusedArena;
		const u32 refusedDyn = rc.refusedDyn - b->refusedDyn;
		const u32 refusedNotReady = rc.refusedNotReady - b->refusedNotReady;

		Platform_Log("[CTR Engine] tour %d level %d track: loaded for level %d, faces %d (live %d, in leaves 0x40 %d), groups %d, materials %d, dropped %d; "
		             "models %d imported, %d failed; frames drawn %llu of %llu, PS1 level buckets skipped %llu\n",
		             entry, levelID, s_egtLoad.levelID, s_egtLoad.faces, s_egtLoad.liveFaces, s_egtLoad.specialFaces, s_egtLoad.groups, s_egtLoad.materials,
		             s_egtLoad.dropped, s_egtLoad.modelsImported, s_egtLoad.modelsFailed,
		             (unsigned long long)(s_egtFramesDrawn - s_egtTourBase.framesDrawn), (unsigned long long)(s_egtCollectFrames - s_egtTourBase.collectFrames),
		             (unsigned long long)(s_egtBucketsSkipped - s_egtTourBase.bucketsSkipped));
		Platform_Log("[CTR Engine] tour %d level %d visibility: frames left to the PS1 walk %llu (no list %llu, over budget %llu), submit refused %llu; "
		             "peak per frame: faces %u, vertices %u, expanded %u (budget %u), in leaves 0x40 %u\n",
		             entry, levelID,
		             (unsigned long long)((s_egtNoVisFrames - s_egtTourBase.noVisFrames) + (s_egtOverBudgetFrames - s_egtTourBase.overBudgetFrames)),
		             (unsigned long long)(s_egtNoVisFrames - s_egtTourBase.noVisFrames), (unsigned long long)(s_egtOverBudgetFrames - s_egtTourBase.overBudgetFrames),
		             (unsigned long long)(s_egtRefusedFrames - s_egtTourBase.refusedFrames), s_egtPeakFaces, s_egtPeakVertices, s_egtPeakExpanded,
		             (unsigned)EGT_DYN_EXPANDED_MAX, s_egtPeakSpecial);
		Platform_Log("[CTR Engine] tour %d level %d walk check: frames without the level walk %llu, broken lists %llu; engine walk against the PS1 lists: "
		             "leaves missed %llu, extra %llu, in %llu frame(s)\n",
		             entry, levelID, (unsigned long long)(s_egtNotWalkedFrames - s_egtTourBase.notWalkedFrames),
		             (unsigned long long)(s_egtBadListFrames - s_egtTourBase.badListFrames), (unsigned long long)(s_egtWalkMissed - s_egtTourBase.walkMissed),
		             (unsigned long long)(s_egtWalkExtra - s_egtTourBase.walkExtra), (unsigned long long)(s_egtWalkDiffFrames - s_egtTourBase.walkDiffFrames));
		Platform_Log("[CTR Engine] tour %d level %d instances: drawn %llu, declined %llu (model not imported %llu, flags %llu, frame %llu, submit %llu)\n",
		             entry, levelID, (unsigned long long)(s_egtInstDrawn - s_egtTourBase.instDrawn),
		             (unsigned long long)(s_egtInstDeclined - s_egtTourBase.instDeclined),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_MODEL] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_MODEL]),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_FLAGS] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_FLAGS]),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_FRAME] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_FRAME]),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_SUBMIT] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_SUBMIT]));
		Platform_Log("[CTR Engine] tour %d level %d runtime instances: drawn %llu, declined %llu; level list models without an InstDef %d imported, "
		             "%d failed\n",
		             entry, levelID, (unsigned long long)(s_egtRuntimeDrawn - s_egtTourBase.runtimeDrawn),
		             (unsigned long long)(s_egtRuntimeDeclined - s_egtTourBase.runtimeDeclined), s_egtLoad.listModelsImported, s_egtLoad.listModelsFailed);
		Platform_Log("[CTR Engine] tour %d level %d render: draws %u, ranges %u, refused at submit %u (scene %u, cell/args %u, arena %u, dynamic %u, "
		             "not ready %u), refused at draw %u, upload failed %u\n",
		             entry, levelID, rc.drawn - b->drawn, rc.ranges - b->ranges, refusedScene + refusedCell + refusedArena + refusedDyn + refusedNotReady,
		             refusedScene, refusedCell, refusedArena, refusedDyn, refusedNotReady, rc.refusedAtDraw - b->refusedAtDraw,
		             rc.uploadFailed - b->uploadFailed);
		Platform_Log("[CTR Engine] tour %d level %d effects retail of owned drivers: shadow %u, exhaust %u, turbo %u, skids %u, body %u, tires %u\n", entry,
		             levelID, fxRetail[ENGINE_FX_SHADOW] - s_egtTourBase.fxRetail[ENGINE_FX_SHADOW],
		             fxRetail[ENGINE_FX_EXHAUST] - s_egtTourBase.fxRetail[ENGINE_FX_EXHAUST], fxRetail[ENGINE_FX_TURBO] - s_egtTourBase.fxRetail[ENGINE_FX_TURBO],
		             fxRetail[ENGINE_FX_SKIDS] - s_egtTourBase.fxRetail[ENGINE_FX_SKIDS], fxRetail[ENGINE_FX_BODY] - s_egtTourBase.fxRetail[ENGINE_FX_BODY],
		             fxRetail[ENGINE_FX_TIRES] - s_egtTourBase.fxRetail[ENGINE_FX_TIRES]);
	}
	for (i = 0; i < s_egtModelCount; i++)
	{
		const EngTrackModel *m = &s_egtModels[i];

		Platform_Log("[CTR Engine] tour %d level %d model '%s' id %d: drawn %u, declined %u%s%s\n", entry, levelID, m->name, (int)m->id, m->drawn, m->declined,
		             m->imported ? "" : ", not imported: ", m->imported ? "" : ((m->why[0] != '\0') ? m->why : "no entry"));
		if (m->posChecked > 0u)
		{
			Platform_Log("[CTR Engine] tour %d level %d model '%s' position: %u draw(s) checked, centre engine vs retail at most dx %.4f dy %.4f of the view\n",
			             entry, levelID, m->name, m->posChecked, (double)m->posMaxDx, (double)m->posMaxDy);
		}
	}
}

// The engine draws the track only in frames the game draws the level: the PS1
// path walks the level under RENDER_FLAG_DRAW_LEVEL (game/MAIN/MainFrame_RenderFrame.c:173,
// the same rule the sky follows, engine_sky.c EngSky_Eligible); the water follows
// the track (Engine_TrackDrawnThisFrame). The level memory is reused from load to
// load, so the address alone does not name the level the faces were built for.
internal int EngTrack_Active(const struct GameTracker *gGT)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->numPlyrCurrGame != 1))
	{
		return 0;
	}
	if (((gGT->gameMode1 & MAIN_MENU) != 0) || ((gGT->renderFlags & RENDER_FLAG_DRAW_LEVEL) == 0) || (gGT->level1 == NULL) ||
	    (gGT->level1 != s_egtLevel))
	{
		return 0;
	}
	return (((int)gGT->levelID == s_egtLevelID) && (gGT->level1->ptr_mesh_info == s_egtMesh) && (s_egtGroupCount > 0) &&
	        (s_egtLeafShown != NULL)) ? 1 : 0;
}

internal EngTrackModel *EngTrack_ModelEntry(const struct Model *model, int create)
{
	int i;

	for (i = 0; i < s_egtModelCount; i++)
	{
		if (s_egtModels[i].model == model)
		{
			return &s_egtModels[i];
		}
	}
	if (!create || (s_egtModelCount >= EGT_MODELS_MAX))
	{
		return NULL;
	}
	{
		EngTrackModel *m = &s_egtModels[s_egtModelCount++];

		memset(m, 0, sizeof(*m));
		m->model = model;
		memcpy(m->name, model->name, 16);
		m->name[16] = 0;
		m->id = model->id;
		m->imp.mesh = -1;
		return m;
	}
}

// The mosaic form a leaf's flags give its blocks, in the order the render list
// picks the slot (game/RenderLevel/RenderLists.c, RenderLists_Select1P2PSlot and
// RenderLists_Select1P2PSlotByFlags): DYNAMIC_SUBDIV, 4X4 and no flag use the
// 4x4 table, then 4X1, then 4X2.
internal u8 EngTrack_MosaicFormOfLeaf(u16 flag)
{
	if ((flag & (BSP_RENDER_LEAF_FLAG_DYNAMIC_SUBDIV | BSP_RENDER_LEAF_FLAG_4X4)) != 0)
	{
		return ENGINE_MOSAIC_FORM_4X4;
	}
	if ((flag & BSP_RENDER_LEAF_FLAG_4X1) != 0)
	{
		return ENGINE_MOSAIC_FORM_4X1;
	}
	if ((flag & BSP_RENDER_LEAF_FLAG_4X2) != 0)
	{
		return ENGINE_MOSAIC_FORM_4X2;
	}
	return ENGINE_MOSAIC_FORM_4X4;
}

// Marks the quadblocks of water leaves and of any leaf (the PS1 walk draws only
// blocks that sit in a leaf), the mosaic form of every block in a leaf, and its
// visibility (VISIBILITY above): the face-list word and bit of the block, its
// leaf (node index), special = its leaf has EGT_LEAF_FLAG_SPECIAL.
// Returns the number of leaves seen.
internal int EngTrack_MarkLeaves(const struct mesh_info *mesh, u8 *water, u8 *inLeaf, u8 *form, u16 *faceWord, u8 *faceBit, u16 *leafNode, u8 *special)
{
	int leaves = 0;
	int n;

	if (mesh->bspRoot == NULL)
	{
		return 0;
	}
	for (n = 0; n < mesh->numBspNodes; n++)
	{
		const struct BSP *node = &mesh->bspRoot[n];
		ptrdiff_t first;
		int i;

		if ((node->flag & BSP_NODE_FLAG_LEAF) == 0)
		{
			continue;
		}
		if ((node->data.leaf.ptrQuadBlockArray == NULL) || (node->data.leaf.numQuads <= 0))
		{
			continue;
		}
		first = node->data.leaf.ptrQuadBlockArray - mesh->ptrQuadBlockArray;
		if ((first < 0) || (first + node->data.leaf.numQuads > mesh->numQuadBlock))
		{
			continue;
		}
		leaves++;
		EngTrack_LeafFaceBits((u16)node->data.leaf.ptrQuadBlockArray[0].blockID, node->data.leaf.numQuads, &faceWord[first], &faceBit[first]);
		for (i = 0; i < node->data.leaf.numQuads; i++)
		{
			inLeaf[first + i] = 1;
			form[first + i] = EngTrack_MosaicFormOfLeaf(node->flag);
			leafNode[first + i] = (u16)n;
			special[first + i] = (u8)(((node->flag & EGT_LEAF_FLAG_SPECIAL) != 0u) ? 1 : 0);
			if ((node->flag & BSP_LEAF_FLAG_WATER) != 0)
			{
				water[first + i] = 1;
			}
		}
	}
	return leaves;
}

// One model into the table and through the importer: 1 imported, 0 failed,
// -1 already in the table.
internal int EngTrack_ImportOne(const struct Model *model, int levelListOnly)
{
	EngTrackModel *m;
	int k;

	if (EngTrack_ModelEntry(model, 0) != NULL)
	{
		return -1;
	}
	m = EngTrack_ModelEntry(model, 1);
	if (m == NULL)
	{
		return 0;
	}
	m->levelListOnly = levelListOnly;
	if (!Engine_ModelImport(model, m->name, &m->imp) || (m->imp.mesh < 0))
	{
		m->imported = 0;
		snprintf(m->why, sizeof(m->why), "%s", (Engine_ModelImportWhy()[0] != '\0') ? Engine_ModelImportWhy() : "no mesh");
		Platform_Log("[CTR Engine] track: %s model '%s' id %d not imported: %s\n", levelListOnly ? "level list" : "instance", m->name, (int)m->id, m->why);
		return 0;
	}
	m->imported = 1;
	m->live = (model->id == STATIC_STARTBANNERWAVE) ? 1 : 0;
	Engine_ModelLogTiles(model, m->name);
	for (k = 0; k < m->imp.materialCount; k++)
	{
		const EngineMaterial *mat = Engine_Material(m->imp.firstMaterial + k);

		if ((mat != NULL) && (mat->blend != ENGINE_BLEND_OPAQUE) && (mat->blend != ENGINE_BLEND_MASK))
		{
			m->transparent = 1;
		}
	}
	return 1;
}

// The retail models of the level's instances, imported like the turbo flames
// (platform/engine_fx_turbo.c EngineFx_TurboLoad): the meshes exist before
// Engine_RenderUploadMeshes runs (and before Engine_MaterialsWarmPipelines,
// LOAD_TenStages.c). Then the models of the level's model list
// (level->ptrModelsPtrArray, the list LOAD_TenStages.c stores into
// gGT->modelPtr) that no InstDef uses: only runtime-born instances can show
// them (RUNTIME INSTANCES, Engine_TrackInstanceRoute). They are counted apart,
// so the line of the InstDef models stays what it was.
internal void EngTrack_ImportModels(const struct Level *level)
{
	u32 i;
	int imported = 0;
	int failed = 0;
	int listImported = 0;
	int listFailed = 0;

	s_egtModelCount = 0;
	s_egtPositionLines = 0;
	if ((level->ptrInstDefs == NULL) || (level->numInstances == 0u))
	{
		Platform_Log("[CTR Engine] track: instance models: 0 imported, 0 failed (no instances)\n");
	}
	else
	{
		for (i = 0; i < level->numInstances; i++)
		{
			const struct Model *model = level->ptrInstDefs[i].model;
			int r;

			if (model == NULL)
			{
				continue;
			}
			r = EngTrack_ImportOne(model, 0);
			imported += (r == 1) ? 1 : 0;
			failed += (r == 0) ? 1 : 0;
		}
		Platform_Log("[CTR Engine] track: instance models: %d imported, %d failed (of %d distinct)\n", imported, failed, s_egtModelCount);
	}
	s_egtLoad.modelsImported = imported;
	s_egtLoad.modelsFailed = failed;

	if (level->ptrModelsPtrArray != NULL)
	{
		for (i = 0; i < level->numModels; i++)
		{
			const struct Model *model = level->ptrModelsPtrArray[i];
			int r;

			if (model == NULL)
			{
				continue;
			}
			r = EngTrack_ImportOne(model, 1);
			listImported += (r == 1) ? 1 : 0;
			listFailed += (r == 0) ? 1 : 0;
		}
	}
	Platform_Log("[CTR Engine] track: level list models without an InstDef (runtime instances): %d imported, %d failed (list of %u)\n", listImported, listFailed, (level->ptrModelsPtrArray != NULL) ? (unsigned)level->numModels : 0u);
	s_egtLoad.listModelsImported = listImported;
	s_egtLoad.listModelsFailed = listFailed;
}

internal void EngTrack_AnimFree(void); // ANIMATED ROWS

internal void EngTrack_DrawFree(void)
{
	free(s_egtFaces);
	free(s_egtGroups);
	free(s_egtShown);
	free(s_egtScratchV);
	free(s_egtScratchI);
	free(s_egtLeafShown);
	free(s_egtLeafWalk);
	free(s_egtWalkStack);
	s_egtLeafShown = NULL;
	s_egtLeafWalk = NULL;
	s_egtPending = 0;
	s_egtWalkStack = NULL;
	s_egtNodeCount = 0;
	s_egtFaces = NULL;
	s_egtGroups = NULL;
	s_egtShown = NULL;
	s_egtScratchV = NULL;
	s_egtScratchI = NULL;
	s_egtFaceCount = 0;
	s_egtFaceCap = 0;
	s_egtGroupCount = 0;
	s_egtVertices = NULL;
	free(s_egtBlockForm);
	free(s_egtBlockMark);
	free(s_egtFaceOfBlock);
	free(s_egtFaceWhy);
	s_egtFaceWhy = NULL;
	s_egtBlockMark = NULL;
	s_egtFaceOfBlock = NULL;
	s_egtBlockForm = NULL;
	EngTrack_AnimFree();
}

// A built face (key set) as a draw face with the level vertices of its block, the
// face-list bit of its block and its leaf. 0 = out of memory.
internal int EngTrack_DrawAppend(const EngTrackFace *f, const u16 *blockIndex, int live, u16 faceWord, u8 faceBit, u16 leafNode, int special)
{
	EngTrackDrawFace *df;
	int v;

	if (s_egtFaceCount >= s_egtFaceCap)
	{
		const int cap = (s_egtFaceCap == 0) ? 4096 : (s_egtFaceCap * 2);
		EngTrackDrawFace *p = (EngTrackDrawFace *)realloc(s_egtFaces, (size_t)cap * sizeof(EngTrackDrawFace));

		if (p == NULL)
		{
			return 0;
		}
		s_egtFaces = p;
		s_egtFaceCap = cap;
	}
	df = &s_egtFaces[s_egtFaceCount++];
	memset(df, 0, sizeof(*df));
	df->face = *f;
	for (v = 0; v < 4; v++)
	{
		df->vertex[v] = blockIndex[f->grid[v]];
	}
	df->material = -1;
	df->faceWord = faceWord;
	df->faceBit = faceBit;
	df->leafNode = leafNode;
	df->live = (u8)(live ? 1 : 0);
	df->special = (u8)(special ? 1 : 0);
	return 1;
}

internal int EngTrack_DrawCompare(const void *a, const void *b)
{
	const EngTrackDrawFace *x = (const EngTrackDrawFace *)a;
	const EngTrackDrawFace *y = (const EngTrackDrawFace *)b;
	const EngTrackKey *kx = &s_egtKeys.key[x->face.key];
	const EngTrackKey *ky = &s_egtKeys.key[y->face.key];

	// Decals last, among them the lower bias first: a later marker in the same OT
	// cell is painted EARLIER (it becomes the cell's head), so the decal with the
	// lowest bias paints last (see DECALS).
	if ((kx->lift != 0) != (ky->lift != 0))
	{
		return (kx->lift != 0) ? 1 : -1;
	}
	if ((kx->lift != 0) && (kx->bias != ky->bias))
	{
		return (kx->bias < ky->bias) ? -1 : 1;
	}
	if (x->semi != y->semi)
	{
		return (x->semi < y->semi) ? -1 : 1;
	}
	if (x->material != y->material)
	{
		return (x->material < y->material) ? -1 : 1;
	}
	return 0;
}

// The faces get their materials (a face whose key has none is dropped and
// counted), are sorted by (semi, material) and grouped; the per-face shown flags
// and the scratch for the largest group are made. 0 = out of memory (nothing is
// drawn then).
internal int EngTrack_DrawGroup(const int *matOfKey, int *dropped)
{
	int kept = 0;
	int largest = 0;
	int i;

	for (i = 0; i < s_egtFaceCount; i++)
	{
		EngTrackDrawFace *df = &s_egtFaces[i];

		df->material = matOfKey[df->face.key];
		df->semi = (u8)EngTrack_IsSemi(s_egtKeys.key[df->face.key].tpage);
		if (df->material < 0)
		{
			(*dropped)++;
			continue;
		}
		s_egtFaces[kept++] = *df;
	}
	s_egtFaceCount = kept;
	if (s_egtFaceCount == 0)
	{
		return 1;
	}
	qsort(s_egtFaces, (size_t)s_egtFaceCount, sizeof(EngTrackDrawFace), EngTrack_DrawCompare);
	s_egtGroups = (EngTrackDrawGroup *)calloc((size_t)s_egtFaceCount, sizeof(EngTrackDrawGroup));
	s_egtShown = (u8 *)calloc((size_t)s_egtFaceCount, 1u);
	if ((s_egtGroups == NULL) || (s_egtShown == NULL))
	{
		return 0;
	}
	for (i = 0; i < s_egtFaceCount; i++)
	{
		EngTrackDrawGroup *g = (s_egtGroupCount > 0) ? &s_egtGroups[s_egtGroupCount - 1] : NULL;

		if ((g == NULL) || (g->material != s_egtFaces[i].material) || (g->semi != (int)s_egtFaces[i].semi))
		{
			g = &s_egtGroups[s_egtGroupCount++];
			g->material = s_egtFaces[i].material;
			g->semi = (int)s_egtFaces[i].semi;
			g->first = i;
			g->count = 0;
			// One key per material: the group's decal data is its first face's key.
			{
				const EngTrackKey *key = &s_egtKeys.key[s_egtFaces[i].face.key];

				g->lift = (int)key->lift;
				g->normal[0] = key->normal[0];
				g->normal[1] = key->normal[1];
				g->normal[2] = key->normal[2];
			}
		}
		g->count++;
		if (g->count > largest)
		{
			largest = g->count;
		}
	}
	s_egtScratchV = (EngineVertex *)malloc((size_t)largest * 4u * sizeof(EngineVertex));
	s_egtScratchI = (u16 *)malloc((size_t)largest * 6u * sizeof(u16));
	return ((s_egtScratchV != NULL) && (s_egtScratchI != NULL)) ? 1 : 0;
}

// --------------------------------------------------------------------------
// ANIMATED ROWS (round of 2026-10-16; engine_track_mosaic.c, ANIMATED TEXTURES).
// A face of an AnimTex cell reads the animated row of its cell, which follows the
// cell every frame: its cells (Engine_TrackMosaicFrame) and its blend. A material
// carries the blend, so a face whose cell's frames give more than one abr is
// keyed once per abr at load (EngTrack_AnimKeys: the face's own key - sidedness,
// bias, decal lift and plane - with the table tpage word of that abr) and, every
// frame before the faces are chosen, takes the key and material of the abr its
// row holds now (EngTrack_AnimFollow); the oracle and the draw read the same.
// Such faces are left out of the draw groups of their load material and drawn
// in a pass of their own after the groups, one dynamic draw per material
// (EngTrack_AnimSubmit). A face whose cell gives one abr stays in its group.

typedef struct
{
	int row;     // its animated row (Engine_TrackMosaicAnimAbr)
	u8 abrSet;   // bit a: the cell's frames give abr a
	int key[4];  // per abr its key, -1 none
	int material[4];
} EngTrackAnimFace;

global_variable EngTrackAnimFace *s_egtAnim;
global_variable int s_egtAnimCount;
global_variable int s_egtAnimCap;
global_variable int *s_egtAnimFaces; // the faces (s_egtFaces index, after the sort) with an animEntry
global_variable int s_egtAnimFaceCount;
global_variable u8 *s_egtDrawShown; // per face this frame: shown and drawn by its group
global_variable u8 *s_egtAnimMark;  // per face: in the pass's draw being filled
global_variable EngineVertex *s_egtAnimScratchV;
global_variable u16 *s_egtAnimScratchI;

internal void EngTrack_AnimFree(void)
{
	free(s_egtAnim);
	free(s_egtAnimFaces);
	free(s_egtDrawShown);
	free(s_egtAnimMark);
	free(s_egtAnimScratchV);
	free(s_egtAnimScratchI);
	s_egtAnim = NULL;
	s_egtAnimCount = 0;
	s_egtAnimCap = 0;
	s_egtAnimFaces = NULL;
	s_egtAnimFaceCount = 0;
	s_egtDrawShown = NULL;
	s_egtAnimMark = NULL;
	s_egtAnimScratchV = NULL;
	s_egtAnimScratchI = NULL;
}

// A face of row whose cell's frames give the blends abrSet: its entry (1 + index), 0 when it
// needs none (one blend) or there is no room (the face keeps the blend of the load).
internal u16 EngTrack_AnimAppend(int row, u8 abrSet)
{
	EngTrackAnimFace *e;
	int a;

	if ((abrSet & (abrSet - 1u)) == 0u)
	{
		return 0;
	}
	if (s_egtAnimCount >= 65535)
	{
		return 0;
	}
	if (s_egtAnimCount >= s_egtAnimCap)
	{
		const int cap = (s_egtAnimCap == 0) ? 64 : (s_egtAnimCap * 2);
		EngTrackAnimFace *p = (EngTrackAnimFace *)realloc(s_egtAnim, (size_t)cap * sizeof(EngTrackAnimFace));

		if (p == NULL)
		{
			return 0;
		}
		s_egtAnim = p;
		s_egtAnimCap = cap;
	}
	e = &s_egtAnim[s_egtAnimCount++];
	e->row = row;
	e->abrSet = abrSet;
	for (a = 0; a < 4; a++)
	{
		e->key[a] = -1;
		e->material[a] = -1;
	}
	return (u16)s_egtAnimCount;
}

// After the decals: per face with an entry, a key for every abr its cell gives. Returns the keys refused (table full).
internal int EngTrack_AnimKeys(void)
{
	int refused = 0;
	int i;

	for (i = 0; i < s_egtFaceCount; i++)
	{
		const EngTrackDrawFace *df = &s_egtFaces[i];
		EngTrackAnimFace *e;
		EngTrackKey base;
		int a;

		if (df->animEntry == 0u)
		{
			continue;
		}
		e = &s_egtAnim[df->animEntry - 1u];
		base = s_egtKeys.key[df->face.key];
		for (a = 0; a < 4; a++)
		{
			if ((e->abrSet & (1u << a)) == 0u)
			{
				continue;
			}
			e->key[a] = EngTrack_KeysFindLifted(&s_egtKeys, (u16)(0x8000u | ((u32)a << 5)), 0, base.doubleSided, base.bias, base.lift, base.normal);
			refused += (e->key[a] < 0) ? 1 : 0;
		}
	}
	return refused;
}

// Before the materials are made: the keys of every abr count as used (matOfKey -1, not -2).
internal void EngTrack_AnimKeysUsed(int *matOfKey)
{
	int j;
	int a;

	for (j = 0; j < s_egtAnimCount; j++)
	{
		for (a = 0; a < 4; a++)
		{
			const int key = s_egtAnim[j].key[a];

			if ((key >= 0) && (key < s_egtKeys.count) && (matOfKey[key] == -2))
			{
				matOfKey[key] = -1;
			}
		}
	}
}

// After the draw groups: every abr's material, the faces with an entry and the pass's
// arrays. Out of memory: the faces keep their load material in their groups (logged).
internal void EngTrack_AnimLoad(const int *matOfKey)
{
	int n = 0;
	int i;
	int j;
	int a;

	for (j = 0; j < s_egtAnimCount; j++)
	{
		for (a = 0; a < 4; a++)
		{
			const int key = s_egtAnim[j].key[a];

			s_egtAnim[j].material[a] = ((key >= 0) && (key < s_egtKeys.count)) ? matOfKey[key] : -1;
		}
	}
	for (i = 0; i < s_egtFaceCount; i++)
	{
		n += (s_egtFaces[i].animEntry != 0u) ? 1 : 0;
	}
	if (n == 0)
	{
		return;
	}
	s_egtAnimFaces = (int *)malloc((size_t)n * sizeof(int));
	s_egtDrawShown = (u8 *)calloc((size_t)s_egtFaceCount, 1u);
	s_egtAnimMark = (u8 *)calloc((size_t)s_egtFaceCount, 1u);
	s_egtAnimScratchV = (EngineVertex *)malloc((size_t)n * 4u * sizeof(EngineVertex));
	s_egtAnimScratchI = (u16 *)malloc((size_t)n * 6u * sizeof(u16));
	if ((s_egtAnimFaces == NULL) || (s_egtDrawShown == NULL) || (s_egtAnimMark == NULL) || (s_egtAnimScratchV == NULL) || (s_egtAnimScratchI == NULL))
	{
		Platform_Log("[CTR Engine] track: out of memory for %d animated face(s) with more than one blend: they keep the blend of the load\n", n);
		free(s_egtAnimFaces);
		free(s_egtDrawShown);
		free(s_egtAnimMark);
		free(s_egtAnimScratchV);
		free(s_egtAnimScratchI);
		s_egtAnimFaces = NULL;
		s_egtDrawShown = NULL;
		s_egtAnimMark = NULL;
		s_egtAnimScratchV = NULL;
		s_egtAnimScratchI = NULL;
		return;
	}
	for (i = 0; i < s_egtFaceCount; i++)
	{
		if (s_egtFaces[i].animEntry != 0u)
		{
			s_egtAnimFaces[s_egtAnimFaceCount++] = i;
		}
	}
}

// The key of abr a for a face entry: a's key and material, or -1 when there is none
// (the face keeps what it has).
internal int EngTrack_AnimKeyOf(const EngTrackAnimFace *e, int abr)
{
	if ((e == NULL) || (abr < 0) || (abr > 3) || (e->key[abr] < 0) || (e->material[abr] < 0))
	{
		return -1;
	}
	return e->key[abr];
}

// Every frame, before the faces are chosen: each face with an entry takes the key and
// material of the abr its row holds now (read only; the row follows the cell).
internal void EngTrack_AnimFollow(void)
{
	int j;

	for (j = 0; j < s_egtAnimFaceCount; j++)
	{
		EngTrackDrawFace *df = &s_egtFaces[s_egtAnimFaces[j]];
		const EngTrackAnimFace *e = &s_egtAnim[df->animEntry - 1u];
		const int abr = Engine_TrackMosaicAnimAbr(e->row);
		const int key = EngTrack_AnimKeyOf(e, abr);

		if ((key < 0) || (key == df->face.key))
		{
			continue;
		}
		df->face.key = key;
		df->face.tpage = s_egtKeys.key[key].tpage;
		df->material = e->material[abr];
		df->semi = (u8)EngTrack_IsSemi(s_egtKeys.key[key].tpage);
		s_egtAnimSwitches++;
	}
}

// After the groups: the shown faces with an entry, one dynamic draw per material
// (as a group: cell, transparency, decal plane of its key). *submittedAny / *refused as the groups.
internal void EngTrack_AnimSubmit(struct GameTracker *gGT, int *submittedAny, int *refused)
{
	int j;

	for (j = 0; j < s_egtAnimFaceCount; j++)
	{
		const int lead = s_egtAnimFaces[j];
		const EngTrackDrawFace *first = &s_egtFaces[lead];
		const EngTrackKey *key = &s_egtKeys.key[first->face.key];
		const int cell = (first->semi || (key->lift != 0)) ? EGT_CELL_SEMI : EGT_CELL_OPAQUE;
		int lo = lead;
		int hi = lead;
		int next;
		int k;

		if (!s_egtShown[lead])
		{
			continue;
		}
		// A material is drawn at its first shown face.
		for (k = 0; k < j; k++)
		{
			if (s_egtShown[s_egtAnimFaces[k]] && (s_egtFaces[s_egtAnimFaces[k]].material == first->material))
			{
				break;
			}
		}
		if (k < j)
		{
			continue;
		}
		for (k = j; k < s_egtAnimFaceCount; k++)
		{
			const int i = s_egtAnimFaces[k];

			if (s_egtShown[i] && (s_egtFaces[i].material == first->material))
			{
				s_egtAnimMark[i] = 1;
				lo = (i < lo) ? i : lo;
				hi = (i > hi) ? i : hi;
			}
		}
		next = lo;
		while (next <= hi)
		{
			EngineDraw draw;
			u32 indexCount = 0;
			const int from = next;
			const u32 vertexCount = EngTrack_FillFacesFrom(s_egtFaces, s_egtAnimMark, from, hi + 1, s_egtVertices, s_egtAnimScratchV, s_egtAnimScratchI,
			                                               EGT_DRAW_VERTICES_MAX, &indexCount, &next);

			if ((vertexCount == 0u) || (next <= from))
			{
				break;
			}
			memset(&draw, 0, sizeof(draw));
			draw.mesh = -1;
			draw.clipFromModel = s_egtScreenFromWorld;
			draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
			draw.materialOverride = -1;
			draw.sortDepth = 1.0e9f;
			draw.otCell = (u32)cell;
			draw.flags = first->semi ? ENGINE_DRAW_TRANSPARENT : 0;
			draw.dynVertices = s_egtAnimScratchV;
			draw.dynVertexCount = vertexCount;
			draw.dynIndices = s_egtAnimScratchI;
			draw.dynIndexCount = indexCount;
			draw.dynMaterial = first->material;
			if ((key->lift != 0) && s_egtCameraWorldOk)
			{
				draw.user[0] = key->normal[0];
				draw.user[1] = key->normal[1];
				draw.user[2] = key->normal[2];
				draw.user[3] = key->normal[0] * s_egtCameraWorld[0] + key->normal[1] * s_egtCameraWorld[1] + key->normal[2] * s_egtCameraWorld[2];
			}
			if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
			{
				*submittedAny = 1;
			}
			else
			{
				*refused = 1;
			}
		}
		for (k = j; k < s_egtAnimFaceCount; k++)
		{
			s_egtAnimMark[s_egtAnimFaces[k]] = 0;
		}
	}
}

internal void EngTrack_OracleFault(void);   // RETAIL ORACLE
internal void EngTrack_OracleLoad(int levelID); // RETAIL ORACLE

// At load stage 8, after level1 is set and the models are in gGT->modelPtr, before
// Engine_RenderUploadMeshes. Builds the draw faces and imports the instance models.
void Engine_TrackLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Level *level;
	const struct mesh_info *mesh;
	u8 *water = NULL;
	u8 *inLeaf = NULL;
	u8 *form = NULL;
	u16 *faceWord = NULL;
	u8 *faceBit = NULL;
	u16 *leafNode = NULL;
	u8 *special = NULL;
	int *matOfKey = NULL;
	int hist[256];
	int blocks = 0;
	int waterBlocks = 0;
	int invisible = 0;
	int unreferenced = 0;
	int badBlocks = 0;
	int noLayout = 0;
	int badFields = 0;
	int triangles = 0;
	int mosaicFaces = 0;
	int animFaces = 0;
	int droppedFaces = 0;
	int materials = 0;
	int liveFaces = 0;
	int specialFaces = 0;
	int bitsPastList = 0;
	int decals = 0;
	int decalsLifted = 0;
	int decalsUnkeyed = 0;
	int decalLiftMax = 0;
	int unusedKeys = 0;
	int animKeysRefused = 0;
	int leaves;
	int i;
	u8 *live = NULL; // level vertices the game animates (SCVert), NULL when none
	const char *failed = NULL; // the reason for Engine_Fallback; NULL = the level is drawable

	EngTrack_DrawFree();
	s_egtModelCount = 0;
	s_egtLevel = NULL;
	s_egtMesh = NULL;
	s_egtLevelID = -1;
	s_egtSubmitted = 0;
	s_egtKeys.count = 0;
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}
	if (!s_egtReportRegistered)
	{
		s_egtReportRegistered = 1;
		Platform_AtExitReport(EngTrack_Report);
		Engine_TourRegister(EngTrack_TourReport);
	}
	level = gGT->level1;
	mesh = level->ptr_mesh_info;
	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL) || (mesh->ptrVertexArray == NULL) || (mesh->numQuadBlock <= 0) || (mesh->numVertex <= 0))
	{
		Platform_Log("[CTR Engine] track: no mesh_info in this level\n");
		return;
	}
	// --engine-fail track: the first level the engine would draw fails here, through the same exit as a real failure.
	if (g_cfg_engineFail == ENGINE_FAIL_TRACK)
	{
		failed = "forced by --engine-fail track";
		goto done;
	}
	memset(hist, 0, sizeof(hist));
	water = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	inLeaf = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	form = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	faceWord = (u16 *)calloc((size_t)mesh->numQuadBlock, sizeof(u16));
	faceBit = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	leafNode = (u16 *)calloc((size_t)mesh->numQuadBlock, sizeof(u16));
	special = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	if ((water == NULL) || (inLeaf == NULL) || (form == NULL) || (faceWord == NULL) || (faceBit == NULL) || (leafNode == NULL) || (special == NULL))
	{
		Platform_Log("[CTR Engine] track: out of memory\n");
		failed = "out of memory";
		goto done;
	}
	leaves = EngTrack_MarkLeaves(mesh, water, inLeaf, form, faceWord, faceBit, leafNode, special);
	s_egtNodeCount = mesh->numBspNodes;
	s_egtLeafShown = (u8 *)calloc((size_t)((mesh->numBspNodes > 0) ? mesh->numBspNodes : 1), 1u);
	s_egtLeafWalk = (u8 *)calloc((size_t)((mesh->numBspNodes > 0) ? mesh->numBspNodes : 1), 1u);
	s_egtWalkStack = (u16 *)calloc((size_t)((mesh->numBspNodes > 0) ? mesh->numBspNodes : 1) + 2u, sizeof(u16));
	if ((s_egtLeafShown == NULL) || (s_egtLeafWalk == NULL) || (s_egtWalkStack == NULL))
	{
		Platform_Log("[CTR Engine] track: out of memory\n");
		failed = "out of memory";
		goto done;
	}
	s_egtFaceWords = (mesh->numQuadBlock + 31) >> 5;
	s_egtLeafWords = (mesh->numBspNodes + 31) >> 5;
	// The scenery vertices the game animates in place (configFlags & 4: SCVert, not water).
	if (((level->configFlags & 4u) != 0u) && (level->numSCVert > 0) && (level->ptrSCVert != NULL))
	{
		live = (u8 *)calloc((size_t)mesh->numVertex, 1u);
		if (live != NULL)
		{
			for (i = 0; i < level->numSCVert; i++)
			{
				const ptrdiff_t at = level->ptrSCVert[i].v - mesh->ptrVertexArray;

				if ((at >= 0) && (at < mesh->numVertex))
				{
					live[at] = 1;
				}
			}
		}
	}
	Engine_TrackMosaicBegin((int)gGT->levelID);
	blocks = mesh->numQuadBlock;
	s_egtFaceWhy = (u8 *)calloc((size_t)mesh->numQuadBlock * 4u, 1u); // EGT_FACE_SKIPPED; NULL: the oracle marks nothing

	for (i = 0; i < mesh->numQuadBlock; i++)
	{
		const struct QuadBlock *qb = &mesh->ptrQuadBlockArray[i];
		EngTrackBlockIn in;
		EngTrackFace built[4];
		const struct TextureLayout *groups[4] = {NULL, NULL, NULL, NULL};
		int anim[4] = {0, 0, 0, 0};
		int mos[4] = {0, 0, 0, 0};
		int n;
		int k;
		int ok = 1;

		if (water[i])
		{
			waterBlocks++;
			continue;
		}
		if ((leaves > 0) && !inLeaf[i])
		{
			unreferenced++;
			continue;
		}
		if (DrawLevelOvr1P_QuadBlockHasNoTexture(qb))
		{
			invisible++;
			continue;
		}
		for (k = 0; k < 9; k++)
		{
			const struct LevVertex *lv;

			if ((int)qb->index[k] >= mesh->numVertex)
			{
				ok = 0;
				break;
			}
			lv = &mesh->ptrVertexArray[qb->index[k]];
			in.pos[k][0] = lv->pos.x;
			in.pos[k][1] = lv->pos.y;
			in.pos[k][2] = lv->pos.z;
			in.color[k][0] = lv->color_hi[0];
			in.color[k][1] = lv->color_hi[1];
			in.color[k][2] = lv->color_hi[2];
		}
		if (!ok)
		{
			badBlocks++;
			continue;
		}
		if ((int)faceWord[i] >= s_egtFaceWords)
		{
			bitsPastList++;
		}
		in.drawOrderLow = qb->draw_order_low;
		in.drawOrderHigh = qb->draw_order_high;
		for (k = 0; k < 4; k++)
		{
			const struct TextureLayout *group = EngTrack_ResolveGroup((u32)(size_t)qb->ptr_texture_mid[k], EngTrack_SpanOk, &anim[k]);

			in.layout[k] = NULL;
			groups[k] = group;
			if (group != NULL)
			{
				const struct TextureLayout *lay = EngTrack_FinestLayout(group, EngTrack_IsLevelPtr, &mos[k]);

				in.layout[k] = EngTrack_LayoutUsable(lay) ? lay : NULL;
			}
			if (in.layout[k] == NULL)
			{
				noLayout++;
			}
		}
		n = EngTrack_BuildBlock(&in, R226.scratchInitTable, 24, built, &badFields);
		if (s_egtFaceWhy != NULL)
		{
			for (k = 0; k < 4; k++)
			{
				s_egtFaceWhy[4 * i + k] = (u8)((in.layout[k] == NULL) ? EGT_FACE_NO_LAYOUT : EGT_FACE_DEGENERATE);
			}
			for (k = 0; k < n; k++)
			{
				s_egtFaceWhy[4 * i + built[k].mid] = EGT_FACE_DROPPED;
			}
		}
		// The flags are per usable layout, not per built face: a face that lost both
		// triangles to degeneracy is still counted. Close enough for a log line.
		for (k = 0; k < 4; k++)
		{
			if (in.layout[k] != NULL)
			{
				mosaicFaces += mos[k];
				animFaces += anim[k];
			}
		}
		for (k = 0; k < n; k++)
		{
			EngTrackFace *f = &built[k];
			u16 keyTpage = 0;
			int isLive;
			int animRowIndex = -1;
			u8 animAbrSet = 0;
			// An animated face, mosaic or not: the animated row of its AnimTex cell (pointer - 1),
			// which follows the cell every frame (ANIMATED ROWS). Any other mosaic face (or an
			// animated one without its row, frozen): its table.
			const int animRowFace = anim[f->mid] && Engine_TrackMosaicAnimFace((u32)(size_t)qb->ptr_texture_mid[f->mid] - 1u, f->mirrored, form[i], f->slotOf,
			                                                                   f->v, f->vertCount, &keyTpage, &animRowIndex, &animAbrSet);
			const int tableFace =
			    !animRowFace && mos[f->mid] && Engine_TrackMosaicFace(groups[f->mid], f->mirrored, form[i], f->slotOf, f->v, f->vertCount, &keyTpage);

			// A mosaic face the table takes: keyed by the table's tpage word, clut 0. So is an
			// animated face: keyed by the abr its row holds at the load.
			if (tableFace || animRowFace)
			{
				f->tpage = keyTpage;
				f->clut = 0;
			}
			f->key = EngTrack_KeysFind(&s_egtKeys, f->tpage, f->clut, f->doubleSided, f->bias);
			if (f->key < 0)
			{
				droppedFaces++;
				continue;
			}
			isLive = (live != NULL) && EngTrack_FaceTouches(f, qb->index, live, mesh->numVertex);
			if (!EngTrack_DrawAppend(f, qb->index, isLive, faceWord[i], faceBit[i], leafNode[i], special[i]))
			{
				droppedFaces++;
				continue;
			}
			if (s_egtFaceWhy != NULL)
			{
				s_egtFaceWhy[4 * i + f->mid] = EGT_FACE_BUILT;
			}
			{
				EngTrackDrawFace *df = &s_egtFaces[s_egtFaceCount - 1];

				df->block = i;
				df->stage = (u8)((groups[f->mid] != NULL) && (in.layout[f->mid] == &groups[f->mid][3]) ? 3 : 2);
				df->anim = (u8)anim[f->mid];
				df->animRow = (u8)animRowFace;
				df->tableRefused = (u8)(mos[f->mid] && !animRowFace && !tableFace);
				// ANIMATED ROWS: a face whose cell gives more than one blend follows it.
				df->animEntry = animRowFace ? EngTrack_AnimAppend(animRowIndex, animAbrSet) : 0u;
			}
			liveFaces += isLive;
			specialFaces += special[i];
			triangles += f->triCount;
			hist[(u8)f->bias]++;
		}
	}

	// Decals (see DECALS): their own keys, with lift and plane normal.
	if (s_egtFaceCount > 0)
	{
		u8 *lift = (u8 *)malloc((size_t)s_egtFaceCount);
		float *normal = (float *)malloc((size_t)s_egtFaceCount * 3u * sizeof(float));

		decals = ((lift != NULL) && (normal != NULL)) ? EngTrack_FindDecals(s_egtFaces, s_egtFaceCount, lift, normal) : -1;
		for (i = 0; i < s_egtFaceCount; i++)
		{
			EngTrackFace *f = &s_egtFaces[i].face;
			const EngTrackKey old = s_egtKeys.key[f->key];
			int key;

			if ((decals <= 0) || (lift[i] == 0))
			{
				continue;
			}
			key = EngTrack_KeysFindLifted(&s_egtKeys, old.tpage, old.clut, old.doubleSided, old.bias, lift[i], &normal[3 * i]);
			if (key < 0)
			{
				decalsUnkeyed++;
				continue;
			}
			f->key = key;
			decalLiftMax = ((int)lift[i] > decalLiftMax) ? (int)lift[i] : decalLiftMax;
			decalsLifted += ((int)lift[i] > (int)EGT_DECAL_MARGIN) ? 1 : 0;
		}
		free(lift);
		free(normal);
	}
	// ANIMATED ROWS: a key per blend of a face whose cell gives more than one (after the decals: their lift goes along).
	animKeysRefused = EngTrack_AnimKeys();

	// --engine-oracle-fault cell (RETAIL ORACLE): a row of the table is changed before it is uploaded.
	EngTrack_OracleFault();

	// The mosaic data texture, after the last face. Without it every face keyed
	// to a table (tpage bit 15) is refused at its draw (engine_render.c, "mosaic
	// table range without its data texture"): the track would have holes.
	if (!Engine_TrackMosaicEnd())
	{
		for (i = 0; i < s_egtKeys.count; i++)
		{
			if ((s_egtKeys.key[i].tpage & 0x8000u) != 0u)
			{
				failed = "the track mosaic data texture was not made";
			}
		}
		if (failed != NULL)
		{
			goto done;
		}
	}

	// Materials: one per key, registered once (Engine_MaterialRegister does not dedupe).
	matOfKey = (int *)malloc(((size_t)s_egtKeys.count + 1u) * sizeof(int));
	if (matOfKey == NULL)
	{
		Platform_Log("[CTR Engine] track: out of memory\n");
		EngTrack_DrawFree();
		failed = "out of memory";
		goto done;
	}
	// A key whose faces all became decals has no face left: no material for it.
	for (i = 0; i < s_egtKeys.count; i++)
	{
		matOfKey[i] = -2;
	}
	for (i = 0; i < s_egtFaceCount; i++)
	{
		matOfKey[s_egtFaces[i].face.key] = -1;
	}
	EngTrack_AnimKeysUsed(matOfKey);
	for (i = 0; i < s_egtKeys.count; i++)
	{
		const EngTrackKey *key = &s_egtKeys.key[i];
		EngineMaterial m;
		const int semi = EngTrack_IsSemi(key->tpage);

		if (matOfKey[i] == -2)
		{
			matOfKey[i] = -1;
			unusedKeys++;
			continue;
		}
		memset(&m, 0, sizeof(m));
		// Bit 15 never sits in a real tpage (EngTrack_LayoutUsable): the key of a mosaic table.
		m.texSource = ((key->tpage & 0x8000u) != 0u) ? ENGINE_TEX_VRAM_TABLE : ENGINE_TEX_VRAM;
		m.tpage = key->tpage;
		m.clut = key->clut;
		m.tint[0] = m.tint[1] = m.tint[2] = m.tint[3] = 1.0f;
		m.flags = (u16)(key->doubleSided ? ENGINE_MATERIAL_DOUBLE_SIDED : 0u);
		if (semi)
		{
			m.blend = (u8)Engine_BlendFromAbr((key->tpage >> 5) & 3);
			m.flags |= (u16)(ENGINE_MATERIAL_SEMI_STP | ENGINE_MATERIAL_NO_DEPTH_WRITE);
		}
		else
		{
			m.blend = ENGINE_BLEND_OPAQUE;
		}
		// The PS1 layers coplanar faces with the s8 bias added to the OT index (higher = farther).
		m.depthBias = -(float)key->bias / 16384.0f;
		if (key->lift != 0)
		{
			// A decal tests depth but does not write it; "ne" lifts it (params3.z, see DECALS).
			m.flags |= (u16)ENGINE_MATERIAL_NO_DEPTH_WRITE;
			m.programParam[0] = (float)key->lift;
		}
		matOfKey[i] = Engine_MaterialRegister(&m);
		if (matOfKey[i] >= 0)
		{
			materials++;
		}
	}
	if (!EngTrack_DrawGroup(matOfKey, &droppedFaces))
	{
		Platform_Log("[CTR Engine] track: out of memory\n");
		droppedFaces += s_egtFaceCount;
		EngTrack_DrawFree();
		failed = "out of memory (draw groups)";
	}
	else
	{
		EngTrack_AnimLoad(matOfKey); // ANIMATED ROWS: the faces and materials per blend
	}
	s_egtVertices = mesh->ptrVertexArray;
	s_egtLevel = level;
	s_egtMesh = mesh;
	s_egtBlockForm = form; // kept for the oracle (freed with the draw faces)
	form = NULL;
	// The oracle's view of the blocks: water / in a leaf, and the draw face of each mid face.
	s_egtBlockMark = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	s_egtFaceOfBlock = (int *)malloc((size_t)mesh->numQuadBlock * 4u * sizeof(int));
	if ((s_egtBlockMark != NULL) && (s_egtFaceOfBlock != NULL))
	{
		for (i = 0; i < mesh->numQuadBlock; i++)
		{
			s_egtBlockMark[i] = (u8)((water[i] ? EGT_BLOCK_WATER : 0u) | (inLeaf[i] ? EGT_BLOCK_IN_LEAF : 0u));
		}
		for (i = 0; i < 4 * mesh->numQuadBlock; i++)
		{
			s_egtFaceOfBlock[i] = -1;
		}
		for (i = 0; i < s_egtFaceCount; i++)
		{
			s_egtFaceOfBlock[4 * s_egtFaces[i].block + s_egtFaces[i].face.mid] = i;
		}
	}
	s_egtLevelID = (int)gGT->levelID;
	s_egtLoad.levelID = (int)gGT->levelID;
	s_egtLoad.faces = s_egtFaceCount;
	s_egtLoad.liveFaces = liveFaces;
	s_egtLoad.specialFaces = specialFaces;
	s_egtLoad.groups = s_egtGroupCount;
	s_egtLoad.materials = materials;
	s_egtLoad.dropped = droppedFaces;

	Platform_Log("[CTR Engine] track: %d blocks, %d water blocks excluded, %d invisible skipped, %d faces, %d triangles, %d draw group(s), %d materials, "
	             "mosaic faces %d, animtex faces %d\n",
	             blocks, waterBlocks, invisible, s_egtFaceCount, triangles, s_egtGroupCount, materials, mosaicFaces, animFaces);
	Platform_Log("[CTR Engine] track: visibility from the frame's leaf and face lists: %d leaves, %d face(s) in leaves with flag 0x40, %d block(s) with a face "
	             "bit past the list (always drawn)\n",
	             leaves, specialFaces, bitsPastList);
	if (live != NULL)
	{
		Platform_Log("[CTR Engine] track: %d live face(s) (scenery vertex animation, %d animated vertices)\n", liveFaces, level->numSCVert);
	}
	Platform_Log("[CTR Engine] track: also %d block(s) in no leaf skipped, %d with a bad vertex index, %d face(s) without a usable layout, "
	             "%d out of the scratch table, %d dropped (keys, materials, memory)\n",
	             unreferenced, badBlocks, noLayout, badFields, droppedFaces);
	{
		char text[1024];
		size_t used = 0;
		int v;

		used += (size_t)snprintf(text + used, sizeof(text) - used, "[CTR Engine] track: depth bias histogram (value:faces)");
		for (v = -128; v < 128; v++)
		{
			if ((hist[(u8)v] != 0) && (used + 24 < sizeof(text)))
			{
				used += (size_t)snprintf(text + used, sizeof(text) - used, " %d:%d", v, hist[(u8)v]);
			}
		}
		Platform_Log("%s\n", text);
	}
	Platform_Log("[CTR Engine] track: decals %d (%d lifted past the margin, largest lift %d), %d without a key, %d unused key(s)%s\n",
	             (decals > 0) ? decals : 0, decalsLifted, decalLiftMax, decalsUnkeyed, unusedKeys, (decals < 0) ? ", out of memory: none lifted" : "");
	Platform_Log("[CTR Engine] track: animated faces whose blend follows their cell: %d (drawn by their own pass: %d), %d blend key(s) refused\n",
	             s_egtAnimCount, s_egtAnimFaceCount, animKeysRefused);
	// --engine-oracle: the static block of the level (RETAIL ORACLE).
	EngTrack_OracleLoad((int)gGT->levelID);
	EngTrack_ImportModels(level);

done:
	free(matOfKey);
	free(water);
	free(inLeaf);
	free(form);
	free(faceWord);
	free(faceBit);
	free(leafNode);
	free(special);
	free(live);
	// A level the engine cannot draw goes to the PS1 path, with the reason (FALLBACK, engine.h).
	if (failed != NULL)
	{
		Engine_Fallback("track load", failed);
	}
}

// --------------------------------------------------------------------------
// RETAIL ORACLE (--engine-oracle, round of 2026-10-16: the material comparison
// W1 of the round notes, done points D-W1.1/D-W1.3). Per quadblock face both paths say which
// texture they use; the line format is P1's (engine_oracle.c, Engine_OracleWrite),
// reached through the ORACLE SHIM below.
//
// R LINES (the PS1 path, game/226/226_00_DrawLevelOvr1P.c):
//   - Dynamic, per logged frame: DrawLevelOvr1P runs its whole walk once more
//     before the frame's own walk (DrawLevelOvr1P_OraclePass). Its packets are
//     never linked into the ordering table; afterwards primMem, scratchpad, GTE
//     and the render lists are put back. Each block's packets come here
//     (Engine_TrackOracleBlock; the near-plane clip records' packets after the
//     buckets, Engine_TrackOracleClip, matched to the block that wrote the
//     record) with the level position of their corners (Engine_TrackOracleNotePrim,
//     the walk's own projected records). The packet is the truth - whatever
//     helper chose the layout, it carries tpage, clut, uv, abr and colours.
//     Its place names the mid face (k) and, for a mosaic piece, the cell
//     (row, col) = the quarter of the face it covers, found from its position
//     in the face (EngTrack_OracleFaceParam), not from the table index. Its uv
//     matched against the block's layouts names the whole-face stage (0..3,
//     low) when it is one.
//   - Static, once per load (F tag L<id>-load, extra set=load) and per logged
//     frame for the faces of AnimTex cells (set=anim): the layouts 226 resolves
//     for every face (DrawLevelOvr1P_OracleLayouts, AnimTex and sieve included),
//     the table entries with entry=<e> (own half first, then the mirrored half).
// E LINES (the engine):
//   - per face the layouts its shader reads: a VRAM face its key's tpage/clut
//     and texcoords (stage = the level taken); a table face every cell of its
//     row the form holds (16/8/4, as built, mirror included; cell column) and
//     the row's levels 2, 1, 0 (stage); a whole-face (AnimTex) row its layout
//     (stage = its level) and levels. box= the texels the shader can reach.
//     Dynamic: the faces drawn this frame; set=load: all faces; set=anim: all
//     faces of AnimTex cells.
// Raw words on both sides: tp=0x.... cl=0x.... (bits a field drops stay visible).

#define EGT_ORACLE_ID_MAX 48
// The marks of a block visit (Engine_TrackOracleBlock): the sieve dropped a
// layout (sieve=1); the walk's distance fade moved the block's midpoints, their
// colour toward color_lo and their place toward the edge middle (226,
// Ovr226_800a1408_AdjustFullDynamicMidVertex: morph=1 - the engine draws the
// finest geometry and colour at every distance, decided 2026-10-16).
#define EGT_ORACLE_MARK_SIEVE 1
#define EGT_ORACLE_MARK_MORPH 2
#define EGT_ORACLE_MARK_ZEROAREA 4 // the packet covers no area (its corners coincide): drawn by retail, seen by nobody
#define EGT_ORACLE_MARK_WIDE 8 // the block went through a subdivision frame that takes the centre from the edge midpoints (226 Ovr226_800a90c0, Ovr226_800a9d70, DrawLevelOvr1P_BuildGridSubdivisionFrame4x4)
#define EGT_ORACLE_EXTRA_MAX 256
#define EGT_ORACLE_BLEND_OPAQUE (-1)
#define EGT_ORACLE_SPANS_MAX 4096
#define EGT_ORACLE_GEO_MAX 32768
// Candidate layouts of one block: the low layout and per mid face its four
// levels and both orientations of a 4x4 table (32 entries).
#define EGT_ORACLE_CANDS_MAX (1 + 4 * (4 + 32))
enum
{
	EGT_ORACLE_LEVEL_TILE = 4, // a mosaic table entry
	EGT_ORACLE_LEVEL_LOW = 5   // the block's ptr_texture_low
};

// One R or E line: the fields of EngineOracleFace (engine.h, ENGINE ORACLE (core)).
typedef struct
{
	char src;
	char srcId[EGT_ORACLE_ID_MAX];
	int sub;
	int textured;
	int tpX, tpY, depth;
	int clutX, clutY;
	int uvCount;
	int uv[8];
	int blend;
	int cellRow, cellCol;
	char extra[EGT_ORACLE_EXTRA_MAX];
} EngTrackOracleFace;

// ORACLE SHIM - the only calls into the oracle core (platform/engine_oracle.c).
// The fault of the cell kind: --engine-oracle-fault cell:<n> (g_cfg_engineOracleFaultCell, -1 = off).
// The extra text of a track line carries its corner colours and raw words itself
// (col=, tp=, cl=), so the core's own colour and raw fields stay off.
internal int EngTrack_OracleOn(void)
{
	return Engine_OracleActive();
}

internal int EngTrack_OracleSwitch(void)
{
	return g_cfg_engine && (g_cfg_engineOracle[0] != '\0');
}

internal void EngTrack_OracleArmLoad(const char *tag)
{
	Engine_OracleArmFrame(tag, -1);
}

internal int EngTrack_OracleFaultCell(void)
{
	return g_cfg_engineOracleFaultCell;
}

internal void EngTrack_OracleEmit(char kind, const EngTrackOracleFace *f)
{
	EngineOracleFace o;

	memset(&o, 0, sizeof(o));
	o.src = f->src;
	snprintf(o.srcId, sizeof(o.srcId), "%s", f->srcId);
	o.sub = f->sub;
	o.textured = f->textured;
	o.tpX = f->tpX;
	o.tpY = f->tpY;
	o.depth = f->depth;
	o.clutX = f->clutX;
	o.clutY = f->clutY;
	o.uvCount = f->uvCount;
	memcpy(o.uv, f->uv, sizeof(o.uv));
	o.blend = f->blend;
	o.cellRow = f->cellRow;
	o.cellCol = f->cellCol;
	snprintf(o.extra, sizeof(o.extra), "%s", f->extra);
	Engine_OracleWrite(kind, &o);
}

// A candidate layout of a block: a copy (read from level memory), its mid face
// (-1 for low), its level (0..3, EGT_ORACLE_LEVEL_*), 1 = from an AnimTex cell.
typedef struct
{
	struct TextureLayout lay;
	s8 k;
	s8 level;
	u8 anim;
	s8 entry; // a table entry's index in its table (both halves), -1 for a level
} EngTrackOracleCand;

// Per logged frame: the retail side of the oracle pass.
global_variable struct
{
	int active; // the oracle pass of this frame runs (Engine_TrackOracleWanted .. Done)
	const char *set; // "load" / "anim" while a static set is written, else NULL
	u32 seq;    // running number of the R lines of the frame (their sub)
	u32 blocks;
	u32 prims;
	u32 clipPrims;
	u32 notTextured; // packets that are no GT3/GT4 (none expected)
	u32 noBlock;     // a block pointer outside the level's quadblocks
	u32 noOwner;     // clip records no block span holds
	u32 spansLost;   // block spans past EGT_ORACLE_SPANS_MAX
	u32 noGeometry;  // packets without a position note (geometry lost or past the array)
	u32 outsideFace; // packets whose position lies in no face of their block
	u32 gridFallback; // block visits whose walk grid failed EngTrack_OracleGridValid (level positions used)
	u32 zeroArea;     // packets without area (zeroarea=1)
	u32 staticLines;
	u32 eFaces;      // engine faces written (E side)
	u32 eLines;
	u32 eNoRow;      // table faces whose row was not found
	int spanCount;
	int spanAt;
	int geoCount;
	int geoAt;
	struct
	{
		const u8 *from;
		const u8 *to;
		int block;
		int water;
		int sieve;
		int hasGrid;
		s16 grid[27]; // the block's walk grid at its visit (EngTrack_OracleBlockFaces)
	} span[EGT_ORACLE_SPANS_MAX];
	struct
	{
		const void *prim;
		int corners;
		int sourceEntry;
		float corner[4][3];
	} geo[EGT_ORACLE_GEO_MAX];
} s_ego;

// The texture page and CLUT words of the PS1 GPU as the fields of a line.
internal void EngTrack_OracleTexture(EngTrackOracleFace *f, u16 tpage, u16 clut)
{
	f->textured = 1;
	f->tpX = (int)(tpage & 0xfu) * 64;
	f->tpY = (int)((tpage >> 4) & 1u) * 256;
	f->depth = (int)((tpage >> 7) & 3u);
	if (f->depth > 2)
	{
		f->depth = 2;
	}
	f->clutX = (int)(clut & 0x3fu) * 16;
	f->clutY = (int)((clut >> 6) & 0x1ffu);
}

// srcId of a track line: "<quadblock>.<mid face>" (the face is the unit both sides
// compare), "<quadblock>" for the block's low layout (k < 0).
internal void EngTrack_OracleSourceId(EngTrackOracleFace *f, int block, int k)
{
	if (k >= 0)
	{
		snprintf(f->srcId, sizeof(f->srcId), "%d.%d", block, k);
	}
	else
	{
		snprintf(f->srcId, sizeof(f->srcId), "%d", block);
	}
}

// Appends "key=value" to a line's extra (pairs separated by one space, P1's
// format; values hold no space); a pair that would not fit is left out.
internal void EngTrack_OracleExtra(EngTrackOracleFace *f, const char *fmt, ...)
{
	char part[96];
	size_t used = strlen(f->extra);
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(part, sizeof(part), fmt, args);
	va_end(args);
	if ((n <= 0) || ((size_t)n >= sizeof(part)) || (used + (size_t)n + 2u > sizeof(f->extra)))
	{
		return;
	}
	if (used > 0)
	{
		f->extra[used++] = ' ';
	}
	memcpy(f->extra + used, part, (size_t)n + 1u);
}

// tp= and cl=: the raw words.
internal void EngTrack_OracleRawWords(EngTrackOracleFace *f, u16 tpage, u16 clut)
{
	EngTrack_OracleExtra(f, "tp=0x%04x", (unsigned)tpage);
	EngTrack_OracleExtra(f, "cl=0x%04x", (unsigned)clut);
}

// col=: the corner colours (three or four "r,g,b" separated by ';').
internal void EngTrack_OracleColoursNamed(EngTrackOracleFace *f, const char *name, const u8 (*rgb)[3], int count)
{
	char text[64];
	size_t used = 0;
	int i;

	text[0] = '\0';
	for (i = 0; (i < count) && (used + 13u < sizeof(text)); i++)
	{
		used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%d,%d,%d", (i > 0) ? ";" : "", rgb[i][0], rgb[i][1], rgb[i][2]);
	}
	EngTrack_OracleExtra(f, "%s=%s", name, text);
}

internal void EngTrack_OracleColours(EngTrackOracleFace *f, const u8 (*rgb)[3], int count)
{
	EngTrack_OracleColoursNamed(f, "col", rgb, count);
}

// One packet of the level walk at words (words[0] the tag, word count << 24).
// The walk writes POLY_GT3 (9 words) and POLY_GT4 (12) only, codes 0x34..0x37 and
// 0x3c..0x3f (DrawLevelOvr1P_SelectRawPrimitiveCode). Fills tpage, clut, the uv
// points, the blend (abr when the code is semi-transparent) and the corner
// colours of a textured one (*isFace = 1). Returns the packet's words with the
// tag; 0 = no packet (count 0 or past wordsLeft): stop.
internal u32 EngTrack_OracleDecodePrim(const u32 *words, u32 wordsLeft, EngTrackOracleFace *f, u16 *tpageOut, u16 *clutOut, u8 (*rgb)[3], int *isFace)
{
	static const int uvWord[4] = {3, 6, 9, 12};
	static const int rgbWord[4] = {1, 4, 7, 10};
	const u32 count = (wordsLeft > 0u) ? (words[0] >> 24) : 0u;
	u32 code;
	u16 tpage;
	u16 clut;
	int corners;
	int i;

	*isFace = 0;
	if ((count == 0u) || (count + 1u > wordsLeft))
	{
		return 0;
	}
	code = (words[1] >> 24) & 0xffu;
	if ((count == 9u) && ((code & 0xfcu) == 0x34u))
	{
		corners = 3;
	}
	else if ((count == 12u) && ((code & 0xfcu) == 0x3cu))
	{
		corners = 4;
	}
	else
	{
		return count + 1u;
	}
	clut = (u16)(words[3] >> 16);
	tpage = (u16)(words[6] >> 16);
	EngTrack_OracleTexture(f, tpage, clut);
	f->uvCount = corners;
	for (i = 0; i < corners; i++)
	{
		f->uv[2 * i] = (int)(words[uvWord[i]] & 0xffu);
		f->uv[2 * i + 1] = (int)((words[uvWord[i]] >> 8) & 0xffu);
		rgb[i][0] = (u8)(words[rgbWord[i]] & 0xffu);
		rgb[i][1] = (u8)((words[rgbWord[i]] >> 8) & 0xffu);
		rgb[i][2] = (u8)((words[rgbWord[i]] >> 16) & 0xffu);
	}
	f->blend = ((code & 2u) != 0u) ? (int)((tpage >> 5) & 3u) : EGT_ORACLE_BLEND_OPAQUE;
	*tpageOut = tpage;
	*clutOut = clut;
	*isFace = 1;
	return count + 1u;
}

// 1 when the primitive lies on the layout: the same tpage (page, abr, depth: the
// low 9 bits; the PS1 sieve keeps the rest 0) and clut, every uv point inside
// the box of the layout's four corners. *exact = every point is a corner.
internal int EngTrack_OracleOnLayout(u16 tpage, u16 clut, const int *uv, int uvCount, const struct TextureLayout *lay, int *exact)
{
	const int lu[4] = {lay->u0, lay->u1, lay->u2, lay->u3};
	const int lv[4] = {lay->v0, lay->v1, lay->v2, lay->v3};
	int umin = 255;
	int umax = 0;
	int vmin = 255;
	int vmax = 0;
	int i;
	int c;

	*exact = 0;
	if (((tpage & 0x1ffu) != (lay->tpage & 0x1ffu)) || (clut != lay->clut))
	{
		return 0;
	}
	for (c = 0; c < 4; c++)
	{
		umin = (lu[c] < umin) ? lu[c] : umin;
		umax = (lu[c] > umax) ? lu[c] : umax;
		vmin = (lv[c] < vmin) ? lv[c] : vmin;
		vmax = (lv[c] > vmax) ? lv[c] : vmax;
	}
	*exact = 1;
	for (i = 0; i < uvCount; i++)
	{
		const int u = uv[2 * i];
		const int v = uv[2 * i + 1];
		int corner = 0;

		if ((u < umin) || (u > umax) || (v < vmin) || (v > vmax))
		{
			*exact = 0;
			return 0;
		}
		for (c = 0; c < 4; c++)
		{
			corner |= ((lu[c] == u) && (lv[c] == v)) ? 1 : 0;
		}
		*exact &= corner;
	}
	return 1;
}

// 1 = a layout of mid face k among the candidates holds the packet (corners or box).
internal int EngTrack_OracleFaceHolds(u16 tpage, u16 clut, const int *uv, int uvCount, const EngTrackOracleCand *c, int n, int k)
{
	int i;

	for (i = 0; i < n; i++)
	{
		int ex = 0;

		if (((int)c[i].k == k) && EngTrack_OracleOnLayout(tpage, clut, uv, uvCount, &c[i].lay, &ex))
		{
			return 1;
		}
	}
	return 0;
}

// The candidate the primitive is: the first that holds its points as corners,
// else the first whose box holds them (the list is in the order tiles, levels
// 3..0 per face, low). -1 = none. Only a label: the line keeps the packet's values.
// builtMask (bit k = the engine built mid face k; -1 = all): a layout several
// faces share names a built face first, so that a packet of a degenerate face's
// twin is not put on the degenerate one.
internal int EngTrack_OracleClassify(u16 tpage, u16 clut, const int *uv, int uvCount, const EngTrackOracleCand *c, int n, int *exact, int builtMask)
{
	int pass;

	*exact = 0;
	for (pass = 0; pass < 4; pass++)
	{
		const int wantExact = (pass < 2) ? 1 : 0;
		const int wantBuilt = ((pass & 1) == 0) ? 1 : 0;
		int i;

		for (i = 0; i < n; i++)
		{
			int ex = 0;

			if (wantBuilt && (c[i].k >= 0) && ((builtMask & (1 << c[i].k)) == 0))
			{
				continue;
			}
			if (!EngTrack_OracleOnLayout(tpage, clut, uv, uvCount, &c[i].lay, &ex) || (ex != wantExact))
			{
				continue;
			}
			*exact = ex;
			return i;
		}
	}
	return -1;
}

// Entries of a table for the leaf form: own half + mirrored half.
internal int EngTrack_OracleTableEntries(int form)
{
	switch (form)
	{
	case ENGINE_MOSAIC_FORM_4X2:
		return 16;
	case ENGINE_MOSAIC_FORM_4X1:
		return 8;
	default:
		return 32;
	}
}

// The layouts a block's packets can carry this frame, for the stage label: the
// mosaic table entries of each mid face (both orientations), the mid groups'
// levels 3..0 (level 3 not when it is the mosaic word), the low layout. form =
// ENGINE_MOSAIC_FORM_* of its leaf. Reads level memory only. Returns the count.
internal int EngTrack_OracleCandidates(const struct QuadBlock *qb, int form, EngTrackOracleCand *out)
{
	const struct TextureLayout *group[4] = {NULL, NULL, NULL, NULL};
	int anim[4] = {0, 0, 0, 0};
	int mos[4] = {0, 0, 0, 0};
	int n = 0;
	int k;
	int level;

	for (k = 0; k < 4; k++)
	{
		group[k] = EngTrack_ResolveGroup((u32)(size_t)qb->ptr_texture_mid[k], EngTrack_SpanOk, &anim[k]);
		if (group[k] != NULL)
		{
			(void)EngTrack_FinestLayout(group[k], EngTrack_IsLevelPtr, &mos[k]);
		}
	}
	for (k = 0; k < 4; k++)
	{
		const int entries = EngTrack_OracleTableEntries(form);
		u32 word = 0;
		int e;

		if ((group[k] == NULL) || !mos[k])
		{
			continue;
		}
		memcpy(&word, &group[k][3], sizeof(word));
		if ((word == 0u) || !EngTrack_SpanOk(word, (u32)entries * (u32)sizeof(struct TextureLayout)))
		{
			continue;
		}
		for (e = 0; e < entries; e++)
		{
			memcpy(&out[n].lay, (const struct TextureLayout *)(size_t)word + e, sizeof(struct TextureLayout));
			out[n].k = (s8)k;
			out[n].entry = (s8)e;
			out[n].level = EGT_ORACLE_LEVEL_TILE;
			out[n].anim = (u8)anim[k];
			n++;
		}
	}
	for (level = 3; level >= 0; level--)
	{
		for (k = 0; k < 4; k++)
		{
			if ((group[k] == NULL) || ((level == 3) && mos[k]) || !EngTrack_LayoutUsable(&group[k][level]))
			{
				continue;
			}
			out[n].lay = group[k][level];
			out[n].k = (s8)k;
			out[n].entry = -1;
			out[n].level = (s8)level;
			out[n].anim = (u8)anim[k];
			n++;
		}
	}
	if ((qb->ptr_texture_low != NULL) && EngTrack_SpanOk((u32)(size_t)qb->ptr_texture_low, (u32)sizeof(struct TextureLayout)))
	{
		memcpy(&out[n].lay, qb->ptr_texture_low, sizeof(struct TextureLayout));
		out[n].k = -1;
		out[n].entry = -1;
		out[n].level = EGT_ORACLE_LEVEL_LOW;
		out[n].anim = 0;
		n++;
	}
	return n;
}

// The face's own (s, t) of point p: corner[0..3] are the face's slot corners,
// slot 0 at (0, 0), 1 at (1, 0), 2 at (0, 1), 3 at (1, 1) (the space of the
// mosaic cells, engine_track_mosaic.c). Bilinear inverse by Newton steps in the
// plane of the face's two largest normal axes. Returns the distance of p from
// the face's plane over the face size (0 on the face), or a negative value when
// the face is degenerate.
internal float EngTrack_OracleFaceParam(const float corner[4][3], const float p[3], float *s, float *t)
{
	float e1[3];
	float e2[3];
	float n[3];
	float c[4][2];
	float q[2];
	float size;
	float off;
	int ax = 0;
	int ay = 1;
	int drop;
	int i;
	int step;

	for (i = 0; i < 3; i++)
	{
		e1[i] = corner[1][i] - corner[0][i];
		e2[i] = corner[2][i] - corner[0][i];
	}
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	size = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (size < 1e-6f)
	{
		return -1.0f;
	}
	drop = (fabsf(n[0]) >= fabsf(n[1])) ? ((fabsf(n[0]) >= fabsf(n[2])) ? 0 : 2) : ((fabsf(n[1]) >= fabsf(n[2])) ? 1 : 2);
	ax = (drop == 0) ? 1 : 0;
	ay = (drop == 2) ? 1 : 2;
	for (i = 0; i < 4; i++)
	{
		c[i][0] = corner[i][ax];
		c[i][1] = corner[i][ay];
	}
	q[0] = p[ax];
	q[1] = p[ay];
	*s = 0.5f;
	*t = 0.5f;
	for (step = 0; step < 12; step++)
	{
		float f[2];
		float ds[2];
		float dt[2];
		float det;
		int a;

		for (a = 0; a < 2; a++)
		{
			const float x = (1.0f - *s) * (1.0f - *t) * c[0][a] + *s * (1.0f - *t) * c[1][a] + (1.0f - *s) * *t * c[2][a] + *s * *t * c[3][a];

			f[a] = x - q[a];
			ds[a] = (1.0f - *t) * (c[1][a] - c[0][a]) + *t * (c[3][a] - c[2][a]);
			dt[a] = (1.0f - *s) * (c[2][a] - c[0][a]) + *s * (c[3][a] - c[1][a]);
		}
		det = ds[0] * dt[1] - ds[1] * dt[0];
		if (fabsf(det) < 1e-9f)
		{
			break;
		}
		*s -= (f[0] * dt[1] - f[1] * dt[0]) / det;
		*t -= (ds[0] * f[1] - ds[1] * f[0]) / det;
	}
	off = ((p[0] - corner[0][0]) * n[0] + (p[1] - corner[0][1]) * n[1] + (p[2] - corner[0][2]) * n[2]) / size;
	return fabsf(off) / sqrtf(size);
}

// The cell of (s, t) in a grid of side cells per side (clamped to the grid).
internal void EngTrack_OracleCellOf(float s, float t, int side, int *row, int *col)
{
	int c = (int)floorf(s * (float)side);
	int r = (int)floorf(t * (float)side);

	*col = (c < 0) ? 0 : ((c > side - 1) ? side - 1 : c);
	*row = (r < 0) ? 0 : ((r > side - 1) ? side - 1 : r);
}


// The cell of (s, t) as a table of the leaf form shows it, named by its first
// cell like the engine rows and the static lines: 4x4 any cell; 4x2 the tile
// spans two rows (rows 0 or 2); 4x1 the whole column (row 0).
internal void EngTrack_OracleCellOfForm(float s, float t, int form, int *row, int *col)
{
	EngTrack_OracleCellOf(s, t, 4, row, col);
	if (form == ENGINE_MOSAIC_FORM_4X2)
	{
		*row &= ~1;
	}
	else if (form == ENGINE_MOSAIC_FORM_4X1)
	{
		*row = 0;
	}
}
// (s, t) of p on a face with one triangle (EngTrack_BuildBlock kept one of the
// two: the other is degenerate, so its fourth slot corner lies on another and
// the bilinear inverse has no answer): barycentric in the triangle's plane,
// carried to the slot positions of its three vertices (vertex v sits at slot v,
// (v & 1, v >> 1)). Returns the plane distance over the face size like
// EngTrack_OracleFaceParam, -1 for a degenerate triangle or p more than margin
// (in barycentric units) outside it.
internal float EngTrack_OracleTriParam(const EngTrackFace *face, const float p[3], float margin, float *s, float *t)
{
	const u8 *tri = face->tri[0];
	float e1[3];
	float e2[3];
	float q[3];
	float n[3];
	float d00 = 0.0f;
	float d01 = 0.0f;
	float d11 = 0.0f;
	float d20 = 0.0f;
	float d21 = 0.0f;
	float den;
	float size;
	float off = 0.0f;
	float w[3];
	int i;
	int a;

	for (a = 0; a < 3; a++)
	{
		e1[a] = face->v[tri[1]].position[a] - face->v[tri[0]].position[a];
		e2[a] = face->v[tri[2]].position[a] - face->v[tri[0]].position[a];
		q[a] = p[a] - face->v[tri[0]].position[a];
		d00 += e1[a] * e1[a];
		d01 += e1[a] * e2[a];
		d11 += e2[a] * e2[a];
		d20 += q[a] * e1[a];
		d21 += q[a] * e2[a];
	}
	n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	size = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	den = d00 * d11 - d01 * d01;
	if ((size < 1e-6f) || (fabsf(den) < 1e-9f))
	{
		return -1.0f;
	}
	w[1] = (d11 * d20 - d01 * d21) / den;
	w[2] = (d00 * d21 - d01 * d20) / den;
	w[0] = 1.0f - w[1] - w[2];
	if ((w[0] < -margin) || (w[1] < -margin) || (w[2] < -margin))
	{
		return -1.0f;
	}
	*s = 0.0f;
	*t = 0.0f;
	for (i = 0; i < 3; i++)
	{
		*s += w[i] * (float)(tri[i] & 1);
		*t += w[i] * (float)((tri[i] >> 1) & 1);
	}
	for (a = 0; a < 3; a++)
	{
		off += q[a] * n[a];
	}
	return fabsf(off / size) / sqrtf(size);
}

// (s, t) of p on a built face: the bilinear inverse over its four slot corners,
// or EngTrack_OracleTriParam when it kept one triangle. -1 = no answer.
internal float EngTrack_OracleFacePoint(const EngTrackFace *face, const float p[3], float margin, float *s, float *t)
{
	float corner[4][3];
	int v;
	int a;

	if (face->triCount < 2)
	{
		return EngTrack_OracleTriParam(face, p, margin, s, t);
	}
	for (v = 0; v < 4; v++)
	{
		for (a = 0; a < 3; a++)
		{
			corner[v][a] = face->v[v].position[a];
		}
	}
	return EngTrack_OracleFaceParam((const float(*)[3])corner, p, s, t);
}

// The mid face of the block that holds p and p's (s, t) in it: faces[k] (NULL =
// no engine face k). The face whose (s, t) lies inside [-0.02, 1.02] (inside its
// triangle for a one-triangle face) and nearest its plane; -1 when none does.
internal int EngTrack_OracleFaceAt(const EngTrackFace *const *faces, const float p[3], float *sOut, float *tOut)
{
	float bestOff = 1e30f;
	int best = -1;
	int k;

	for (k = 0; k < 4; k++)
	{
		float s;
		float t;
		float off;

		if (faces[k] == NULL)
		{
			continue;
		}
		off = EngTrack_OracleFacePoint(faces[k], p, 0.02f, &s, &t);
		if ((off < 0.0f) || (s < -0.02f) || (s > 1.02f) || (t < -0.02f) || (t > 1.02f) || (off >= bestOff))
		{
			continue;
		}
		bestOff = off;
		best = k;
		*sOut = s;
		*tOut = t;
	}
	return best;
}

// The face slot of every packet corner (FM A9: corner k <-> slot k): the
// corners' (s, t) in the face (st[v]) against the four corners of the unit they
// belong to - a cell (s0, t0) - (s1, t1), or the whole face (0, 0) - (1, 1) -
// in slot order (s0, t0), (s1, t0), (s0, t1), (s1, t1). Every corner must lie
// within a quarter of the unit's smaller side of one slot, each slot at most
// once. 1 = slotOf[v] set.
internal int EngTrack_OracleSlotOrder(const float (*st)[2], int count, float s0, float t0, float s1, float t1, int *slotOf)
{
	const float ss[4] = {s0, s1, s0, s1};
	const float ts[4] = {t0, t0, t1, t1};
	const float w = s1 - s0;
	const float h = t1 - t0;
	const float tol = 0.25f * ((w < h) ? w : h);
	int used = 0;
	int v;

	if ((count < 3) || (count > 4) || (tol <= 0.0f))
	{
		return 0;
	}
	for (v = 0; v < count; v++)
	{
		int best = -1;
		float bestD = tol * tol;
		int j;

		for (j = 0; j < 4; j++)
		{
			const float ds = st[v][0] - ss[j];
			const float dt = st[v][1] - ts[j];
			const float d = ds * ds + dt * dt;

			if (d <= bestD)
			{
				bestD = d;
				best = j;
			}
		}
		if ((best < 0) || ((used & (1 << best)) != 0))
		{
			return 0;
		}
		used |= 1 << best;
		slotOf[v] = best;
	}
	return 1;
}

// A line's corners (uv and colours) put in slot order: slotOf[v] is the slot of
// packet corner v; the corners come out ascending by slot and slots= names the
// slot of each written corner ("0,1,2,3", or three of them for a GT3; the form
// the comparison tool reads). A line whose corners could not be placed carries
// no slots= (the tool then compares its corners as a set).
internal void EngTrack_OracleApplySlots(EngTrackOracleFace *f, u8 (*rgb)[3], const int *slotOf)
{
	int uv[8];
	u8 col[4][3];
	char text[8];
	int n = 0;
	int j;
	int v;

	for (j = 0; j < 4; j++)
	{
		for (v = 0; v < f->uvCount; v++)
		{
			if (slotOf[v] != j)
			{
				continue;
			}
			uv[2 * n] = f->uv[2 * v];
			uv[2 * n + 1] = f->uv[2 * v + 1];
			memcpy(col[n], rgb[v], 3);
			text[2 * n] = (char)('0' + j);
			text[2 * n + 1] = ',';
			n++;
		}
	}
	text[(n > 0) ? (2 * n - 1) : 0] = '\0';
	memcpy(f->uv, uv, (size_t)n * 2u * sizeof(int));
	memcpy(rgb, col, (size_t)n * 3u);
	EngTrack_OracleExtra(f, "slots=%s", text);
}

// One dynamic R line from a decoded packet. k and the cell come from where the
// packet lies (faceAt >= 0: its face, (s, t) its centre in it); the stage from
// the layout its uv is (exact corners: stage=<0..3|low>, a table entry: the
// cell; else part=1 with the layout that holds it, none = no layout of the
// block). anim=1 from an AnimTex cell, clip=1 from a clip record; sieve (the block's marks,
// EGT_ORACLE_MARK_*): sieve=1, morph=1 (its midpoints faded by distance); prim=normal (the track
// walk's one writer), tp=/cl=, col=.
internal void EngTrack_OracleRetailLine(EngTrackOracleFace *f, int block, int water, int clip, int sieve, int form, u16 tpage, u16 clut, const u8 (*rgbIn)[3],
                                        const EngTrackOracleCand *c, int n, int faceAt, float s, float t, const float (*cornerST)[2], int builtMask)
{
	static const char *const levelName[6] = {"0", "1", "2", "3", "tile", "low"};
	u8 rgb[4][3];
	int slotOf[4] = {0, 1, 2, 3};
	int ordered = 0;
	int exact = 0;
	const int at = EngTrack_OracleClassify(tpage, clut, f->uv, f->uvCount, c, n, &exact, builtMask);
	// A stage packet whose layout belongs to another mid face of the block and to
	// none of the face it lies on (FaceAt places by the centre; folded or steep
	// blocks put a neighbour's packet there): it is that face's (foreign=<place>).
	const int foreign = (faceAt >= 0) && (at >= 0) && (c[at].level != EGT_ORACLE_LEVEL_TILE) && (c[at].k >= 0) && ((int)c[at].k != faceAt) &&
	                    !EngTrack_OracleFaceHolds(tpage, clut, f->uv, f->uvCount, c, n, faceAt);
	const int k = foreign ? (int)c[at].k : ((faceAt >= 0) ? faceAt : ((at >= 0) ? (int)c[at].k : -1));

	f->src = water ? 'W' : 'T';
	EngTrack_OracleSourceId(f, block, k);
	f->cellRow = -1;
	f->cellCol = -1;
	f->extra[0] = '\0';
	memcpy(rgb, rgbIn, (size_t)f->uvCount * 3u);
	if (k >= 0)
	{
		EngTrack_OracleExtra(f, "k=%d", k);
	}
	if (at < 0)
	{
		EngTrack_OracleExtra(f, "stage=none");
	}
	else if (c[at].level == EGT_ORACLE_LEVEL_TILE)
	{
		if (faceAt >= 0)
		{
			EngTrack_OracleCellOfForm(s, t, form, &f->cellRow, &f->cellCol);
			if (cornerST != NULL)
			{
				const float rows = (form == ENGINE_MOSAIC_FORM_4X1) ? 4.0f : ((form == ENGINE_MOSAIC_FORM_4X2) ? 2.0f : 1.0f);

				ordered = EngTrack_OracleSlotOrder(cornerST, f->uvCount, (float)f->cellCol * 0.25f, (float)f->cellRow * 0.25f,
				                                   (float)(f->cellCol + 1) * 0.25f, ((float)f->cellRow + rows) * 0.25f, slotOf);
			}
		}
		else
		{
			EngTrack_OracleExtra(f, "nogeo=1");
		}
	}
	else
	{
		EngTrack_OracleExtra(f, "stage=%s", levelName[(int)c[at].level]);
		if ((faceAt >= 0) && (cornerST != NULL) && !foreign)
		{
			ordered = EngTrack_OracleSlotOrder(cornerST, f->uvCount, 0.0f, 0.0f, 1.0f, 1.0f, slotOf);
		}
	}
	// part=1: the packet is a piece of the layout - its uv not the layout's corners,
	// or (a stage line placed on its face) its corners not on the face's corners,
	// whatever its uv (single-texel layouts put a piece's uv on the corners by chance).
	if ((at >= 0) && (!exact || ((c[at].level != EGT_ORACLE_LEVEL_TILE) && (faceAt >= 0) && (cornerST != NULL) && !ordered && !foreign &&
	                             ((sieve & EGT_ORACLE_MARK_ZEROAREA) == 0))))
	{
		EngTrack_OracleExtra(f, "part=1");
	}
	if (foreign)
	{
		EngTrack_OracleExtra(f, "foreign=%d", faceAt);
	}
	if ((at >= 0) && (c[at].level == EGT_ORACLE_LEVEL_TILE) && ((sieve & EGT_ORACLE_MARK_WIDE) != 0))
	{
		// The tile colours come from the wide frame: compare col against the E line's colw= (when it has one).
		EngTrack_OracleExtra(f, "wide=1");
	}
	if ((at >= 0) && c[at].anim)
	{
		EngTrack_OracleExtra(f, "anim=1");
	}
	if (clip)
	{
		EngTrack_OracleExtra(f, "clip=1");
	}
	if ((sieve & EGT_ORACLE_MARK_SIEVE) != 0)
	{
		EngTrack_OracleExtra(f, "sieve=1");
	}
	if ((sieve & EGT_ORACLE_MARK_MORPH) != 0)
	{
		EngTrack_OracleExtra(f, "morph=1");
	}
	if ((sieve & EGT_ORACLE_MARK_ZEROAREA) != 0)
	{
		EngTrack_OracleExtra(f, "zeroarea=1");
	}
	EngTrack_OracleExtra(f, "prim=normal");
	EngTrack_OracleRawWords(f, tpage, clut);
	if (ordered)
	{
		EngTrack_OracleApplySlots(f, rgb, slotOf);
	}
	EngTrack_OracleColours(f, (const u8(*)[3])rgb, f->uvCount);
}

// The engine faces of a block per mid face (NULL = none), from s_egtFaceOfBlock.
// walkGrid (may be NULL): the block's nine grid points as the walk holds them
// (27 values, grid index order, its projection records) - the faces are then
// copied into copies[] with their corners moved to those points, so that a
// packet's place and the face lie in the same space: the walk moves the
// midpoints of far blocks (226, DrawLevelOvr1P full-dynamic fade, ~2715).
internal void EngTrack_OracleBlockFaces(int block, const s16 *walkGrid, EngTrackFace *copies, const EngTrackFace **faces)
{
	int k;

	for (k = 0; k < 4; k++)
	{
		const int at = (s_egtFaceOfBlock != NULL) ? s_egtFaceOfBlock[4 * block + k] : -1;
		int v;
		int a;

		faces[k] = (at >= 0) ? &s_egtFaces[at].face : NULL;
		if ((faces[k] == NULL) || (walkGrid == NULL))
		{
			continue;
		}
		copies[k] = *faces[k];
		for (v = 0; v < 4; v++)
		{
			for (a = 0; a < 3; a++)
			{
				copies[k].v[v].position[a] = (float)walkGrid[3 * copies[k].grid[v] + a];
			}
		}
		faces[k] = &copies[k];
	}
}

// 1 = walkGrid holds the block's grid: its four corners (never moved by the walk)
// are the level positions of the block's corner vertices.
internal int EngTrack_OracleGridValid(const struct QuadBlock *qb, const struct LevVertex *verts, int numVertex, const s16 *walkGrid)
{
	int c;

	if ((qb == NULL) || (verts == NULL) || (walkGrid == NULL))
	{
		return 0;
	}
	for (c = 0; c < 4; c++)
	{
		const int at = (int)qb->index[c];

		if ((at >= numVertex) || (walkGrid[3 * c] != verts[at].pos.x) || (walkGrid[3 * c + 1] != verts[at].pos.y) || (walkGrid[3 * c + 2] != verts[at].pos.z))
		{
			return 0;
		}
	}
	return 1;
}

// The position note of the packet at prim (the notes come in packet order):
// its centre and its corners (*corners of them, the packet's vertex order).
internal int EngTrack_OracleGeometry(const void *prim, float centre[3], float (*corner)[3], int *corners, int *sourceEntry)
{
	int i;

	for (i = s_ego.geoAt; i < s_ego.geoCount; i++)
	{
		if (s_ego.geo[i].prim == prim)
		{
			int v;
			int a;

			s_ego.geoAt = i + 1;
			*corners = s_ego.geo[i].corners;
			*sourceEntry = s_ego.geo[i].sourceEntry;
			for (a = 0; a < 3; a++)
			{
				centre[a] = 0.0f;
			}
			for (v = 0; v < *corners; v++)
			{
				for (a = 0; a < 3; a++)
				{
					corner[v][a] = s_ego.geo[i].corner[v][a];
					centre[a] += corner[v][a] / (float)*corners;
				}
			}
			return 1;
		}
	}
	return 0;
}

// The R lines of the packets in [from, to) of one block.
// A GT4 whose corners v and u (u < v) lie on one place (within half a unit):
// corner v is dropped from the line's uv and colours and from the corner list,
// leaving a GT3. Only one such pair is taken; returns 1 when one was dropped.
internal int EngTrack_OracleDropTwin(EngTrackOracleFace *f, u8 (*rgb)[3], float (*corner)[3], int *corners)
{
	int v;
	int u;

	for (v = 1; v < *corners; v++)
	{
		for (u = 0; u < v; u++)
		{
			const float dx = corner[v][0] - corner[u][0];
			const float dy = corner[v][1] - corner[u][1];
			const float dz = corner[v][2] - corner[u][2];
			int i;

			if ((dx * dx + dy * dy + dz * dz) >= 0.25f)
			{
				continue;
			}
			for (i = v; i + 1 < *corners; i++)
			{
				corner[i][0] = corner[i + 1][0];
				corner[i][1] = corner[i + 1][1];
				corner[i][2] = corner[i + 1][2];
				f->uv[2 * i] = f->uv[2 * i + 2];
				f->uv[2 * i + 1] = f->uv[2 * i + 3];
				memcpy(rgb[i], rgb[i + 1], 3);
			}
			(*corners)--;
			f->uvCount--;
			return 1;
		}
	}
	return 0;
}

// 1 = the packet covers no area: every triangle of its corners (0, 1, 2) and,
// for four, (1, 3, 2) has a cross product of (almost) zero.
internal int EngTrack_OracleZeroArea(const float (*corner)[3], int corners)
{
	static const int tris[2][3] = {{0, 1, 2}, {1, 3, 2}};
	int t;

	for (t = 0; t < ((corners == 4) ? 2 : 1); t++)
	{
		float e1[3];
		float e2[3];
		float n[3];
		int a;

		for (a = 0; a < 3; a++)
		{
			e1[a] = corner[tris[t][1]][a] - corner[tris[t][0]][a];
			e2[a] = corner[tris[t][2]][a] - corner[tris[t][0]][a];
		}
		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		if ((n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) > 1e-6f)
		{
			return 0;
		}
	}
	return 1;
}

// src=<e> on a tile packet: the table entry the walk's mosaic reload read for it
// (226 DrawLevelOvr1P_PrepareDeepestMosaicUv and its two twins, sourceOffset /
// 12), written only when that entry of the block's tables is the layout the
// packet carries (its corners exactly): the walk's own choice next to the
// cell its place names.
internal void EngTrack_OracleSourceMark(EngTrackOracleFace *f, u16 tpage, u16 clut, const EngTrackOracleCand *c, int n, int sourceEntry)
{
	int i;

	if ((sourceEntry < 0) || (f->cellRow < 0))
	{
		return;
	}
	for (i = 0; i < n; i++)
	{
		int exact = 0;

		if ((c[i].level == EGT_ORACLE_LEVEL_TILE) && (c[i].entry == sourceEntry) && EngTrack_OracleOnLayout(tpage, clut, f->uv, f->uvCount, &c[i].lay, &exact) &&
		    exact)
		{
			EngTrack_OracleExtra(f, "src=%d", sourceEntry);
			return;
		}
	}
}

internal void EngTrack_OracleRetailRange(int block, int water, int clip, int sieve, const s16 *walkGrid, const void *from, const void *to, const float *place)
{
	static EngTrackOracleCand cand[EGT_ORACLE_CANDS_MAX];
	EngTrackFace copies[4];
	const EngTrackFace *faces[4];
	const u32 *w = (const u32 *)from;
	const u32 *end = (const u32 *)to;
	const int form = (s_egtBlockForm != NULL) ? (int)s_egtBlockForm[block] : ENGINE_MOSAIC_FORM_4X4;
	int builtMask;
	int n;
	int k;

	if ((from == NULL) || (to == NULL) || (end <= w))
	{
		return;
	}
	n = EngTrack_OracleCandidates(&s_egtMesh->ptrQuadBlockArray[block], form, cand);
	EngTrack_OracleBlockFaces(block, walkGrid, copies, faces);
	builtMask = 0;
	for (k = 0; k < 4; k++)
	{
		builtMask |= (faces[k] != NULL) ? (1 << k) : 0;
	}
	while (w < end)
	{
		EngTrackOracleFace f;
		u16 tpage = 0;
		u16 clut = 0;
		u8 rgb[4][3];
		float centre[3];
		float corner[4][3];
		float cornerST[4][2];
		int corners = 0;
		int haveST = 0;
		float s = 0.0f;
		float t = 0.0f;
		int faceAt = -1;
		int isFace = 0;
		int lineMarks = sieve;
		int sourceEntry = -1;
		const void *prim = w;
		const u32 used = EngTrack_OracleDecodePrim(w, (u32)(end - w), &f, &tpage, &clut, rgb, &isFace);

		if (used == 0u)
		{
			break;
		}
		w += used;
		if (!isFace)
		{
			s_ego.notTextured++;
			continue;
		}
		if (place != NULL)
		{
			centre[0] = place[0];
			centre[1] = place[1];
			centre[2] = place[2];
		}
		if ((place == NULL) && !EngTrack_OracleGeometry(prim, centre, corner, &corners, &sourceEntry))
		{
			s_ego.noGeometry++;
		}
		else
		{
			// A GT4 with two corners on one place is a triangle (its second half has no
			// area): the repeated corner goes, the rest is a GT3. A packet with no area
			// at all is marked and placed nowhere.
			if ((corners == 4) && (f.uvCount == 4))
			{
				EngTrack_OracleDropTwin(&f, rgb, corner, &corners);
			}
			if ((corners >= 3) && (corners == f.uvCount) && EngTrack_OracleZeroArea(corner, corners))
			{
				lineMarks |= EGT_ORACLE_MARK_ZEROAREA;
				s_ego.zeroArea++;
			}
			else
			{
				faceAt = EngTrack_OracleFaceAt(faces, centre, &s, &t);
				s_ego.outsideFace += (faceAt < 0) ? 1u : 0u;
			}
		}
		// The corners' own (s, t) in that face: their slots (not for clip pieces, whose
		// corners are cut points; corners stays 0 for them).
		if ((faceAt >= 0) && (corners == f.uvCount))
		{
			int v;

			haveST = 1;
			for (v = 0; v < corners; v++)
			{
				haveST &= (EngTrack_OracleFacePoint(faces[faceAt], corner[v], 0.1f, &cornerST[v][0], &cornerST[v][1]) >= 0.0f) ? 1 : 0;
			}
		}
		f.sub = (int)s_ego.seq++;
		EngTrack_OracleRetailLine(&f, block, water, clip, lineMarks, form, tpage, clut, (const u8(*)[3])rgb, cand, n, faceAt, s, t,
		                          haveST ? (const float(*)[2])cornerST : NULL, builtMask);
		EngTrack_OracleSourceMark(&f, tpage, clut, cand, n, sourceEntry);
		EngTrack_OracleEmit('R', &f);
		if (clip)
		{
			s_ego.clipPrims++;
		}
		else
		{
			s_ego.prims++;
		}
	}
}

// The packets in [from, to) (the whole pass, for the check of the Done line).
internal u32 EngTrack_OracleCountPackets(const void *from, const void *to)
{
	const u32 *w = (const u32 *)from;
	const u32 *end = (const u32 *)to;
	u32 packets = 0;

	while ((w != NULL) && (w < end) && ((w[0] >> 24) != 0u) && ((w[0] >> 24) + 1u <= (u32)(end - w)))
	{
		w += (w[0] >> 24) + 1u;
		packets++;
	}
	return packets;
}

// box= of a layout the shader samples clamped (the table path: cell and
// fraction clamped, "never a texel beyond the tile"): the box of its corners.
internal void EngTrack_OracleLayoutLine(EngTrackOracleFace *f, const struct TextureLayout *lay, int withBox)
{
	const int lu[4] = {lay->u0, lay->u1, lay->u2, lay->u3};
	const int lv[4] = {lay->v0, lay->v1, lay->v2, lay->v3};
	int umin = 255;
	int umax = 0;
	int vmin = 255;
	int vmax = 0;
	int c;

	EngTrack_OracleTexture(f, lay->tpage, lay->clut);
	f->uvCount = 4;
	for (c = 0; c < 4; c++)
	{
		f->uv[2 * c] = lu[c];
		f->uv[2 * c + 1] = lv[c];
		umin = (lu[c] < umin) ? lu[c] : umin;
		umax = (lu[c] > umax) ? lu[c] : umax;
		vmin = (lv[c] < vmin) ? lv[c] : vmin;
		vmax = (lv[c] > vmax) ? lv[c] : vmax;
	}
	EngTrack_OracleRawWords(f, lay->tpage, lay->clut);
	if (withBox)
	{
		EngTrack_OracleExtra(f, "box=%d,%d,%d,%d", umin, vmin, umax, vmax);
	}
	EngTrack_OracleExtra(f, "slots=0,1,2,3"); // corners in the face's slot order (FM A9)
}

// The corners of a 4-corner line in the order of a mirrored face: u0<->u1, u2<->u3.
internal void EngTrack_OracleMirrorCorners(EngTrackOracleFace *f)
{
	int a;

	for (a = 0; a < 2; a++)
	{
		const int top = f->uv[a];
		const int bottom = f->uv[4 + a];

		f->uv[a] = f->uv[2 + a];
		f->uv[2 + a] = top;
		f->uv[4 + a] = f->uv[6 + a];
		f->uv[6 + a] = bottom;
	}
}

// A static R line (set=load / set=anim): one layout 226 resolved for a face -
// level 0..3 (stage=; lodonly=1 below the level the walk draws without distance
// LOD, `finest`: native_view.c s_lodNone, 226 DrawLevelOvr1P_TextureLodLevel),
// the low layout (level -1, k -1: stage=low lodonly=1) - or a table entry
// (entry >= 0, its cell in the cell column). mirrored: the walk draws the face
// mirrored - a level's corners are written in the face's own order as the engine
// lines are (u0<->u1, u2<->u3: the uvSlot swap of EngTrack_BuildBlock, the
// EngMosaic_MirrorCopy of the table rows); a table entry is stored that way.
internal void EngTrack_OracleStaticLine(EngTrackOracleFace *f, int block, int water, int k, int level, int finest, int entry, int cellRow, int cellCol,
                                        const struct TextureLayout *lay, int mirrored, int anim, int sieve, const char *set)
{
	memset(f, 0, sizeof(*f));
	f->src = water ? 'W' : 'T';
	EngTrack_OracleSourceId(f, block, k);
	f->sub = k;
	f->blend = EGT_ORACLE_BLEND_OPAQUE;
	f->cellRow = -1;
	f->cellCol = -1;
	if (k >= 0)
	{
		EngTrack_OracleExtra(f, "k=%d", k);
	}
	if (lay != NULL)
	{
		EngTrack_OracleLayoutLine(f, lay, 0);
		f->blend = EngTrack_IsSemi(lay->tpage) ? (int)((lay->tpage >> 5) & 3u) : EGT_ORACLE_BLEND_OPAQUE;
		if (mirrored && (entry < 0))
		{
			EngTrack_OracleMirrorCorners(f);
		}
		{
			const u8 *bytes = (const u8 *)lay;
			size_t i;
			int blank = 1;

			for (i = 0; i < sizeof(*lay); i++)
			{
				blank &= (bytes[i] == 0) ? 1 : 0;
			}
			if (blank)
			{
				// An all-zero layout (an empty table entry): nothing the walk can show (information).
				EngTrack_OracleExtra(f, "blank=1");
			}
		}
	}
	if (mirrored)
	{
		EngTrack_OracleExtra(f, "mir=1");
	}
	if (entry >= 0)
	{
		f->cellRow = cellRow;
		f->cellCol = cellCol;
		EngTrack_OracleExtra(f, "entry=%d", entry);
	}
	else if (level >= 0)
	{
		EngTrack_OracleExtra(f, "stage=%d", level);
		if (level < finest)
		{
			EngTrack_OracleExtra(f, "lodonly=1");
		}
	}
	else
	{
		EngTrack_OracleExtra(f, "stage=low lodonly=1");
	}
	if (anim)
	{
		EngTrack_OracleExtra(f, "anim=1");
	}
	if (sieve)
	{
		EngTrack_OracleExtra(f, "sieve=1");
	}
	EngTrack_OracleExtra(f, "set=%s", (set != NULL) ? set : "-");
}

// The cell a table entry stands in (the first one; a 4x2 entry also covers the
// row below, a 4x1 entry the whole column), for the face's orientation:
// e = entry - (mirrored ? own : 0), cell (e / 4, e % 4) for 4x4, (2 (e / 4),
// e % 4) for 4x2, (0, e) for 4x1 - the rule the engine's rows are built by
// (engine_track_mosaic.c, EngMosaic_CellEntry); the dynamic lines, whose cell
// comes from where the packet lies, check it.
internal void EngTrack_OracleEntryCell(int form, int e, int *row, int *col)
{
	switch (form)
	{
	case ENGINE_MOSAIC_FORM_4X2:
		*row = 2 * (e >> 2);
		*col = e & 3;
		break;
	case ENGINE_MOSAIC_FORM_4X1:
		*row = 0;
		*col = e;
		break;
	default:
		*row = e >> 2;
		*col = e & 3;
		break;
	}
}

// 1 = a block index of the loaded level; *water its water mark.
internal int EngTrack_OracleBlockIndex(const struct QuadBlock *block, int *index, int *water)
{
	ptrdiff_t at;

	if ((block == NULL) || (s_egtMesh == NULL))
	{
		return 0;
	}
	at = block - s_egtMesh->ptrQuadBlockArray;
	if ((at < 0) || (at >= s_egtMesh->numQuadBlock))
	{
		return 0;
	}
	*index = (int)at;
	*water = ((s_egtBlockMark != NULL) && ((s_egtBlockMark[at] & EGT_BLOCK_WATER) != 0u)) ? 1 : 0;
	return 1;
}

// Why the engine has no face for a static line (information for the tool, not a
// deviation by itself): noleaf=1 the block lies in no leaf (the walk never
// reaches it); for mid face k: nolayout=1 no usable layout (EngTrack_LayoutUsable),
// degenerate=1 EngTrack_BuildBlock made no triangle of it (equal corners, zero
// area, a bad scratch-table slot - the walk draws none either), dropped=1 built
// but no key or material. Not in the per-frame AnimTex set (set=anim): there a
// missing engine face stays a deviation (FM, class K-ANIM-T2).
internal void EngTrack_OracleFaceMarks(EngTrackOracleFace *f, int index, int k)
{
	if ((s_egtBlockMark != NULL) && ((s_egtBlockMark[index] & EGT_BLOCK_IN_LEAF) == 0u))
	{
		EngTrack_OracleExtra(f, "noleaf=1");
	}
	if ((s_ego.set != NULL) && (strcmp(s_ego.set, "anim") == 0))
	{
		return;
	}
	if ((k < 0) || (k > 3) || (s_egtFaceOfBlock == NULL) || (s_egtFaceOfBlock[4 * index + k] >= 0))
	{
		return;
	}
	switch ((s_egtFaceWhy != NULL) ? s_egtFaceWhy[4 * index + k] : EGT_FACE_BUILT)
	{
	case EGT_FACE_NO_LAYOUT:
		EngTrack_OracleExtra(f, "nolayout=1");
		break;
	case EGT_FACE_DEGENERATE:
		EngTrack_OracleExtra(f, "degenerate=1");
		break;
	case EGT_FACE_SKIPPED:
		break; // water, no leaf, invisible: marked by src W / noleaf / not drawn by the walk
	default:
		EngTrack_OracleExtra(f, "dropped=1");
		break;
	}
}

// From DrawLevelOvr1P_OracleLayouts (226): one resolved layout of a face.
void Engine_TrackOracleLayout(const struct QuadBlock *block, int face, int level, int finest, const struct TextureLayout *layout, int mirrored, int anim,
                              int sieve)
{
	EngTrackOracleFace f;
	int index = 0;
	int water = 0;

	if ((s_ego.set == NULL) || !EngTrack_OracleBlockIndex(block, &index, &water))
	{
		return;
	}
	EngTrack_OracleStaticLine(&f, index, water, face, level, finest, -1, -1, -1, layout, mirrored, anim, sieve, s_ego.set);
	EngTrack_OracleFaceMarks(&f, index, face);
	EngTrack_OracleEmit('R', &f);
	s_ego.staticLines++;
}

// From DrawLevelOvr1P_OracleLayouts (226): the table a face's level 3 names and
// whether the walk draws the face mirrored. The entries of the half the face
// shows (the form of the block's leaf), each with its cell.
void Engine_TrackOracleTable(const struct QuadBlock *block, int face, const struct TextureLayout *table, int anim, int mirrored)
{
	int index = 0;
	int water = 0;
	int form;
	int entries;
	int own;
	int e;

	if ((s_ego.set == NULL) || (table == NULL) || !EngTrack_OracleBlockIndex(block, &index, &water))
	{
		return;
	}
	form = (s_egtBlockForm != NULL) ? (int)s_egtBlockForm[index] : ENGINE_MOSAIC_FORM_4X4;
	entries = EngTrack_OracleTableEntries(form);
	own = entries / 2;
	if (!EngTrack_SpanOk((u32)(size_t)table, (u32)entries * (u32)sizeof(struct TextureLayout)))
	{
		return;
	}
	for (e = 0; e < own; e++)
	{
		const int entry = (mirrored ? own : 0) + e;
		EngTrackOracleFace f;
		int row = 0;
		int col = 0;

		EngTrack_OracleEntryCell(form, e, &row, &col);
		EngTrack_OracleStaticLine(&f, index, water, face, -1, 0, entry, row, col, &table[entry], mirrored, anim, 0, s_ego.set);
		EngTrack_OracleExtra(&f, "form=%s", (form == ENGINE_MOSAIC_FORM_4X2) ? "4x2" : ((form == ENGINE_MOSAIC_FORM_4X1) ? "4x1" : "4x4"));
		EngTrack_OracleFaceMarks(&f, index, face);
		if ((face >= 0) && (face <= 3) && (s_egtFaceOfBlock != NULL) && (s_egtFaceOfBlock[4 * index + face] >= 0) &&
		    s_egtFaces[s_egtFaceOfBlock[4 * index + face]].tableRefused)
		{
			// The walk takes the level-3 word for a table, the engine refused it
			// (Engine_TrackMosaicFace) and draws level 2: information, not a missing cell.
			EngTrack_OracleExtra(&f, "tablefail=1");
		}
		EngTrack_OracleEmit('R', &f);
		s_ego.staticLines++;
	}
}

// From the 226 packet writers during the oracle pass: a packet's corner
// positions (level units, the packet's vertex order) and their count.
void Engine_TrackOracleNotePrim(const void *prim, const s16 *cornerPositions, int corners, int sourceEntry)
{
	int v;
	int a;

	if (!s_ego.active || (prim == NULL) || (cornerPositions == NULL) || (corners <= 0) || (corners > 4))
	{
		return;
	}
	if (s_ego.geoCount >= EGT_ORACLE_GEO_MAX)
	{
		s_ego.noGeometry++;
		return;
	}
	s_ego.geo[s_ego.geoCount].prim = prim;
	s_ego.geo[s_ego.geoCount].corners = corners;
	s_ego.geo[s_ego.geoCount].sourceEntry = sourceEntry;
	for (v = 0; v < corners; v++)
	{
		for (a = 0; a < 3; a++)
		{
			s_ego.geo[s_ego.geoCount].corner[v][a] = (float)cornerPositions[3 * v + a];
		}
	}
	s_ego.geoCount++;
}

// Called by DrawLevelOvr1P before its walk: 1 = run the oracle pass now (a
// logged frame whose track the engine draws, faces not yet submitted). Writes
// the retail AnimTex set of the frame first (DrawLevelOvr1P_OracleLayouts).
int Engine_TrackOracleWanted(void)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || !s_egtSubmitted || !s_egtPending || (s_egtMesh == NULL) || !EngTrack_OracleOn())
	{
		return 0;
	}
	memset(&s_ego, 0, sizeof(s_ego));
	s_ego.set = "anim";
	DrawLevelOvr1P_OracleLayouts(s_egtMesh, 1);
	s_ego.set = NULL;
	s_ego.active = 1;
	return 1;
}

void Engine_TrackOracleBlock(const struct QuadBlock *block, int water, int marks, const s16 *walkGrid, const void *primFrom, const void *primTo,
                             const void *clipFrom, const void *clipTo)
{
	const int sieve = marks & (EGT_ORACLE_MARK_SIEVE | EGT_ORACLE_MARK_MORPH | EGT_ORACLE_MARK_WIDE); // handed on as the line marks
	int index = 0;
	int markedWater = 0;

	if (!s_ego.active)
	{
		return;
	}
	if (!EngTrack_OracleBlockIndex(block, &index, &markedWater))
	{
		s_ego.noBlock++;
		return;
	}
	s_ego.blocks++;
	if ((walkGrid != NULL) && !EngTrack_OracleGridValid(block, s_egtVertices, s_egtMesh->numVertex, walkGrid))
	{
		s_ego.gridFallback++;
		walkGrid = NULL;
	}
	if ((clipFrom != NULL) && (clipTo != NULL) && ((const u8 *)clipTo > (const u8 *)clipFrom))
	{
		if (s_ego.spanCount < EGT_ORACLE_SPANS_MAX)
		{
			s_ego.span[s_ego.spanCount].from = (const u8 *)clipFrom;
			s_ego.span[s_ego.spanCount].to = (const u8 *)clipTo;
			s_ego.span[s_ego.spanCount].block = index;
			s_ego.span[s_ego.spanCount].water = water;
			s_ego.span[s_ego.spanCount].sieve = sieve;
			s_ego.span[s_ego.spanCount].hasGrid = (walkGrid != NULL) ? 1 : 0;
			if (walkGrid != NULL)
			{
				memcpy(s_ego.span[s_ego.spanCount].grid, walkGrid, sizeof(s_ego.span[s_ego.spanCount].grid));
			}
			s_ego.spanCount++;
		}
		else
		{
			s_ego.spansLost++;
		}
	}
	EngTrack_OracleRetailRange(index, water, 0, sieve, walkGrid, primFrom, primTo, NULL);
}

// The consumer takes the records in the order they were written: the spans are
// searched from the last one found.
void Engine_TrackOracleClip(const void *record, const s32 *positionSum, int corners, const void *primFrom, const void *primTo)
{
	const u8 *at = (const u8 *)record;
	float place[3] = {0.0f, 0.0f, 0.0f};

	if (!s_ego.active)
	{
		return;
	}
	if ((s_ego.spanAt < s_ego.spanCount) && (at < s_ego.span[s_ego.spanAt].from))
	{
		s_ego.spanAt = 0;
	}
	while ((s_ego.spanAt < s_ego.spanCount) && (at >= s_ego.span[s_ego.spanAt].to))
	{
		s_ego.spanAt++;
	}
	if ((s_ego.spanAt >= s_ego.spanCount) || (at < s_ego.span[s_ego.spanAt].from))
	{
		s_ego.noOwner++;
		return;
	}
	if ((positionSum != NULL) && (corners > 0))
	{
		place[0] = (float)positionSum[0] / (float)corners;
		place[1] = (float)positionSum[1] / (float)corners;
		place[2] = (float)positionSum[2] / (float)corners;
	}
	EngTrack_OracleRetailRange(s_ego.span[s_ego.spanAt].block, s_ego.span[s_ego.spanAt].water, 1, s_ego.span[s_ego.spanAt].sieve,
	                           s_ego.span[s_ego.spanAt].hasGrid ? s_ego.span[s_ego.spanAt].grid : NULL, primFrom, primTo,
	                           ((positionSum != NULL) && (corners > 0)) ? place : NULL);
}

void Engine_TrackOracleDone(int completed, const void *primFrom, const void *primTo)
{
	u32 packets;

	if (!s_ego.active)
	{
		return;
	}
	s_ego.active = 0;
	packets = EngTrack_OracleCountPackets(primFrom, primTo);
	Platform_Log("[CTR Engine] oracle track: retail walk %s, %u block visit(s), %u R line(s) (%u from clip records), %u AnimTex set line(s), %u "
	             "packet(s) in the pass, %u not a textured GT3/GT4, %u packet(s) outside any block, %u without position, %u in no face of their "
	             "block, %u block visit(s) placed on level positions (walk grid not valid), %u block pointer(s) outside the level, %u clip "
	             "record(s) without block, %u block span(s) lost\n",
	             completed ? "complete" : "STOPPED (no primitive space)", s_ego.blocks, s_ego.prims + s_ego.clipPrims, s_ego.clipPrims, s_ego.staticLines,
	             packets, s_ego.notTextured,
	             (packets > s_ego.prims + s_ego.clipPrims + s_ego.notTextured) ? (packets - s_ego.prims - s_ego.clipPrims - s_ego.notTextured) : 0u,
	             s_ego.noGeometry, s_ego.outsideFace, s_ego.gridFallback, s_ego.noBlock, s_ego.noOwner, s_ego.spansLost);
}

// The layouts a table row offers a face, each cell the form holds once (4x4:
// all 16; 4x2: rows 0 and 2, each standing for itself and the row below; 4x1:
// row 0; a whole-face row, side 1: cell 0), then levels 2, 1, 0 (the order of
// the data texture). which[i] = cell index 0..15, or 16 + level. Returns the
// count (<= 19).
internal int EngTrack_OracleRowLayouts(int side, int form, int *which)
{
	int n = 0;
	int i;

	for (i = 0; i < ((side == 1) ? 1 : 16); i++)
	{
		const int r = i >> 2;

		if ((side != 1) && (((form == ENGINE_MOSAIC_FORM_4X2) && ((r & 1) != 0)) || ((form == ENGINE_MOSAIC_FORM_4X1) && (r != 0))))
		{
			continue;
		}
		which[n++] = i;
	}
	for (i = 2; i >= 0; i--)
	{
		which[n++] = 16 + i;
	}
	return n;
}

// The colour of one corner pair's midpoint as the walk makes it: (a + b) >> 1 per
// channel (226, DrawLevelOvr1P_BuildMidpointValue and the subdivision frames).
internal void EngTrack_OracleMidColour(u8 *out, const u8 *a, const u8 *b)
{
	int ch;

	for (ch = 0; ch < 3; ch++)
	{
		out[ch] = (u8)(((u32)a[ch] + (u32)b[ch]) >> 1);
	}
}

// One subdivision step of the quad (r0, c0) - (r1, c1) of the 5 x 5 grid: the
// edge midpoints, and with both = 1 also the centre, which the walk takes from
// the diagonal of slots 1 and 2 (226, the list, rendered, full-dynamic and grid
// subdivision frames: sub[6] from indices 1 and 2); both = 2 takes the centre
// from the top and bottom edge midpoints (the wide-dynamic frame, Ovr226_800a90c0:
// sub[6] from sub[4] and sub[8]). both = 0 splits along s only (top and bottom edge).
internal void EngTrack_OracleSplitColours(u8 (*g)[5][3], int r0, int c0, int r1, int c1, int both)
{
	const int rm = (r0 + r1) / 2;
	const int cm = (c0 + c1) / 2;

	EngTrack_OracleMidColour(g[r0][cm], g[r0][c0], g[r0][c1]);
	EngTrack_OracleMidColour(g[r1][cm], g[r1][c1], g[r1][c0]);
	if (!both)
	{
		return;
	}
	EngTrack_OracleMidColour(g[rm][c0], g[r1][c0], g[r0][c0]);
	EngTrack_OracleMidColour(g[rm][c1], g[r0][c1], g[r1][c1]);
	if (both == 2)
	{
		EngTrack_OracleMidColour(g[rm][cm], g[r0][cm], g[r1][cm]);
	}
	else
	{
		EngTrack_OracleMidColour(g[rm][cm], g[r0][c1], g[r1][c0]);
	}
}

// The colours at the corners of the mosaic cells (a 5 x 5 grid in the face's own
// space, rows t, columns s) from the face's four slot colours, made the way the
// walk makes the colours of its mosaic pieces: midpoints of midpoints, each
// (a + b) >> 1. 4x4: both directions twice; 4x2: both once, then s only; 4x1: s
// only, twice (rows a form does not split keep no colour). The engine itself
// interpolates its two triangles linearly; these are the values the retail
// pieces carry at the same points, so that col compares per cell. wide = 1: the
// centres as the wide-dynamic frame makes them (see EngTrack_OracleSplitColours).
internal void EngTrack_OracleCellColours(const u8 (*corner)[3], int form, int wide, u8 (*g)[5][3])
{
	const int both = wide ? 2 : 1;
	int ch;
	int q;

	memset(g, 0, 5 * 5 * 3);
	for (ch = 0; ch < 3; ch++)
	{
		g[0][0][ch] = corner[0][ch];
		g[0][4][ch] = corner[1][ch];
		g[4][0][ch] = corner[2][ch];
		g[4][4][ch] = corner[3][ch];
	}
	if (form == ENGINE_MOSAIC_FORM_4X1)
	{
		EngTrack_OracleSplitColours(g, 0, 0, 4, 4, 0);
		EngTrack_OracleSplitColours(g, 0, 0, 4, 2, 0);
		EngTrack_OracleSplitColours(g, 0, 2, 4, 4, 0);
		return;
	}
	EngTrack_OracleSplitColours(g, 0, 0, 4, 4, both);
	for (q = 0; q < 4; q++)
	{
		const int r0 = (q >> 1) * 2;
		const int c0 = (q & 1) * 2;

		EngTrack_OracleSplitColours(g, r0, c0, r0 + 2, c0 + 2, (form == ENGINE_MOSAIC_FORM_4X2) ? 0 : both);
	}
}

// The E lines of one table face: out[] gets up to 19 lines. base = the face's
// line with src, srcId, sub, blend and the shared extra filled (no col=);
// corner = the face's slot colours: a cell line carries the colours of its own
// corners (EngTrack_OracleCellColours: rows r and r + 1, r + 2 for 4x2, 0 and 4
// for 4x1), a stage line those of the face. finest = the level a whole-face row
// holds (side 1).
internal int EngTrack_OracleTableLines(const EngTrackOracleFace *base, int side, int form, int finest, int faulted, const struct TextureLayout *cells,
                                       const struct TextureLayout *levels, const u8 (*corner)[3], EngTrackOracleFace *out)
{
	int which[19];
	const int n = EngTrack_OracleRowLayouts(side, form, which);
	u8 grid[5][5][3];
	u8 gridWide[5][5][3];
	int i;

	EngTrack_OracleCellColours(corner, form, 0, grid);
	EngTrack_OracleCellColours(corner, form, 1, gridWide);

	for (i = 0; i < n; i++)
	{
		EngTrackOracleFace *f = &out[i];

		*f = *base;
		f->cellRow = -1;
		f->cellCol = -1;
		if (which[i] < 16)
		{
			EngTrack_OracleLayoutLine(f, &cells[which[i]], 1);
			if (side == 1)
			{
				EngTrack_OracleExtra(f, "stage=%d", finest);
				EngTrack_OracleColours(f, corner, 4);
			}
			else
			{
				const int r = which[i] / 4;
				const int c = which[i] % 4;
				const int r0 = (form == ENGINE_MOSAIC_FORM_4X1) ? 0 : r;
				const int r1 = (form == ENGINE_MOSAIC_FORM_4X1) ? 4 : ((form == ENGINE_MOSAIC_FORM_4X2) ? r + 2 : r + 1);
				u8 cellCorner[4][3];
				u8 wideCorner[4][3];

				memcpy(cellCorner[0], grid[r0][c], 3);
				memcpy(cellCorner[1], grid[r0][c + 1], 3);
				memcpy(cellCorner[2], grid[r1][c], 3);
				memcpy(cellCorner[3], grid[r1][c + 1], 3);
				memcpy(wideCorner[0], gridWide[r0][c], 3);
				memcpy(wideCorner[1], gridWide[r0][c + 1], 3);
				memcpy(wideCorner[2], gridWide[r1][c], 3);
				memcpy(wideCorner[3], gridWide[r1][c + 1], 3);
				f->cellRow = r;
				f->cellCol = c;
				if (faulted)
				{
					EngTrack_OracleExtra(f, "fault=cell");
				}
				EngTrack_OracleColours(f, (const u8(*)[3])cellCorner, 4);
				if (memcmp(cellCorner, wideCorner, sizeof(cellCorner)) != 0)
				{
					// The same cell when the walk takes the wide frame (R lines with wide=1).
					EngTrack_OracleColoursNamed(f, "colw", (const u8(*)[3])wideCorner, 4);
				}
			}
		}
		else
		{
			EngTrack_OracleLayoutLine(f, &levels[which[i] - 16], 1);
			EngTrack_OracleExtra(f, "stage=%d", which[i] - 16);
			EngTrack_OracleColours(f, corner, 4);
		}
	}
	return n;
}

// The E line of a VRAM face: the key's tpage and clut, the face's texcoords;
// box= the face's UV box ("ne" clamps floor(v_uv) to EngineVertex.uvBox, the
// same on all slot vertices of the face, EngTrack_BuildBlock).
internal void EngTrack_OracleEngineVram(EngTrackOracleFace *f, u16 tpage, u16 clut, const EngTrackFace *face)
{
	int box[4];
	int v;

	EngTrack_OracleTexture(f, tpage, clut);
	f->uvCount = face->vertCount;
	for (v = 0; v < face->vertCount; v++)
	{
		const int u = (int)floorf(face->v[v].texcoord[0] + 0.5f);
		const int t = (int)floorf(face->v[v].texcoord[1] + 0.5f);

		f->uv[2 * v] = u;
		f->uv[2 * v + 1] = t;
	}
	EngTrack_OracleRawWords(f, tpage, clut);
	Engine_VertexBoxGet(&face->v[0], box);
	EngTrack_OracleExtra(f, "box=%d,%d,%d,%d", box[0], box[1], box[2], box[3]);
	// Vertex v of a built face is slot v (EngTrack_BuildBlock); a 3-vertex face is slots 0, 1, 2.
	EngTrack_OracleExtra(f, "slots=%s", (face->vertCount == 4) ? "0,1,2,3" : "0,1,2");
}

// The shared part of a face's E lines: src, srcId, sub (its mid face), blend of
// its material, extra k=, anim=1 (and frozen=1 when it does not follow the cell),
// decal=, live=1, tablefail=1, set= for the static sets. rgb[] gets the colours
// the face is drawn with now (its four slot vertices; live faces from the level
// vertices); each line adds its col= itself.
internal void EngTrack_OracleEngineBase(EngTrackOracleFace *f, const EngTrackDrawFace *df, const EngTrackKey *key, const struct LevVertex *verts,
                                        const char *set, u8 (*rgb)[3])
{
	int v;

	memset(f, 0, sizeof(*f));
	f->src = 'T';
	EngTrack_OracleSourceId(f, df->block, (int)df->face.mid);
	f->sub = (int)df->face.mid;
	f->blend = EngTrack_IsSemi(key->tpage) ? (int)((key->tpage >> 5) & 3u) : EGT_ORACLE_BLEND_OPAQUE;
	f->cellRow = -1;
	f->cellCol = -1;
	EngTrack_OracleExtra(f, "k=%d", (int)df->face.mid);
	if (df->anim)
	{
		EngTrack_OracleExtra(f, "anim=1");
		if (!df->animRow)
		{
			EngTrack_OracleExtra(f, "frozen=1");
		}
	}
	if (key->lift != 0)
	{
		EngTrack_OracleExtra(f, "decal=%d", (int)key->lift);
	}
	if (df->live)
	{
		EngTrack_OracleExtra(f, "live=1");
	}
	if (df->tableRefused)
	{
		EngTrack_OracleExtra(f, "tablefail=1");
	}
	if (set != NULL)
	{
		EngTrack_OracleExtra(f, "set=%s", set);
	}
	for (v = 0; v < 4; v++)
	{
		const u8 *c = df->face.v[v].color;

		if (df->live && (verts != NULL))
		{
			c = verts[df->vertex[v]].color_hi;
		}
		rgb[v][0] = c[0];
		rgb[v][1] = c[1];
		rgb[v][2] = c[2];
	}
}

// The E lines of one engine face into lines[] (<= 19). 0 = a table face whose row is missing.
internal int EngTrack_OracleEngineFaceLines(const EngTrackDrawFace *df, const char *set, EngTrackOracleFace *lines)
{
	const EngTrackKey *key = &s_egtKeys.key[df->face.key];
	EngTrackOracleFace base;
	u8 rgb[4][3];

	EngTrack_OracleEngineBase(&base, df, key, s_egtVertices, set, rgb);
	if ((key->tpage & 0x8000u) != 0u)
	{
		const struct TextureLayout *cells = NULL;
		const struct TextureLayout *levels = NULL;
		int side = 0;
		int form = 0;
		int finest = 0;
		int faulted = 0;

		if (!Engine_TrackMosaicOracleRow(df->face.v[0].texcoord[0], df->face.v[0].texcoord[1], &side, &form, &finest, &faulted, &cells, &levels))
		{
			return 0;
		}
		return EngTrack_OracleTableLines(&base, side, form, finest, faulted, cells, levels, (const u8(*)[3])rgb, lines);
	}
	lines[0] = base;
	EngTrack_OracleEngineVram(&lines[0], key->tpage, key->clut, &df->face);
	EngTrack_OracleExtra(&lines[0], "stage=%d", (int)df->stage);
	EngTrack_OracleColours(&lines[0], (const u8(*)[3])rgb, df->face.vertCount);
	return 1;
}

// The E lines of the engine's faces: shownOnly = the faces drawn this frame
// (s_egtShown), else all; animOnly = only faces of AnimTex cells.
internal void EngTrack_OracleEngineFaces(int shownOnly, int animOnly, const char *set)
{
	static EngTrackOracleFace lines[19];
	u32 faces = 0;
	u32 written = 0;
	u32 noRow = 0;
	int i;

	for (i = 0; i < s_egtFaceCount; i++)
	{
		const EngTrackDrawFace *df = &s_egtFaces[i];
		int n;
		int j;

		if ((shownOnly && ((s_egtShown == NULL) || !s_egtShown[i])) || (animOnly && !df->anim))
		{
			continue;
		}
		n = EngTrack_OracleEngineFaceLines(df, set, lines);
		if (n == 0)
		{
			noRow++;
			continue;
		}
		for (j = 0; j < n; j++)
		{
			EngTrack_OracleEmit('E', &lines[j]);
		}
		faces++;
		written += (u32)n;
	}
	Platform_Log("[CTR Engine] oracle track: engine %s %u face(s), %u E line(s), %u table face(s) without their row\n",
	             (set != NULL) ? set : "frame", faces, written, noRow);
}

// --engine-oracle-fault cell:<n> (done point D-W3.1): in the row of the n-th
// table face of the level (block order), two cells of different layouts swap
// places (Engine_TrackMosaicOracleFault). Measurement only; logged; E lines of
// faces reading that row carry fault=cell. Before the data texture is made.
internal void EngTrack_OracleFault(void)
{
	const int n = EngTrack_OracleFaultCell();
	int seen = 0;
	int i;

	if (n < 0)
	{
		return;
	}
	for (i = 0; i < s_egtFaceCount; i++)
	{
		const EngTrackDrawFace *df = &s_egtFaces[i];

		if (((df->face.tpage & 0x8000u) == 0u) || df->animRow)
		{
			continue;
		}
		if (seen++ < n)
		{
			continue;
		}
		if (Engine_TrackMosaicOracleFault(df->face.v[0].texcoord[0], df->face.v[0].texcoord[1], df->block, (int)df->face.mid))
		{
			return;
		}
	}
	Platform_Log("[CTR Engine] oracle fault: cell %d - no such table face (or no two different cells in its row)\n", n);
}

// The tag of a static block, one per load: t<NN>-L<level>-load while the tour
// drives an entry (the pattern of its oracle frames, EngineTour_OracleTag; entry
// and level read from game/DebugMenu.c, s_tourPos / s_tourLevel / s_tourPhase,
// read only - containers sharing a donor slot get their own entry number), else
// L<level>-load<n> with n counting the loads of the run. 0 = no room.
global_variable int s_egoLoads;

internal int EngTrack_OracleLoadTag(char *out, size_t size, int levelID, int tourDriving, int tourEntry, int tourLevel, int loadNumber)
{
	int n;

	if (tourDriving)
	{
		return EngineTour_OracleTag(out, size, tourEntry, tourLevel, "load");
	}
	n = snprintf(out, size, "L%d-load%d", levelID, loadNumber);
	return ((n > 0) && ((size_t)n < size)) ? 1 : 0;
}

// End of Engine_TrackLoad: the static block of the level (F tag
// EngTrack_OracleLoadTag): R = the layouts 226 resolves for every face, E = every
// face of the engine.
internal void EngTrack_OracleLoad(int levelID)
{
	char tag[48];

	if (!EngTrack_OracleSwitch() || (s_egtMesh == NULL))
	{
		return;
	}
	s_egoLoads++;
	if (!EngTrack_OracleLoadTag(tag, sizeof(tag), levelID, s_tourPhase == DBG_TOUR_PHASE_DRIVING, s_tourPos + 1, s_tourLevel, s_egoLoads))
	{
		return;
	}
	EngTrack_OracleArmLoad(tag);
	if (!EngTrack_OracleOn())
	{
		return;
	}
	memset(&s_ego, 0, sizeof(s_ego));
	s_ego.set = "load";
	DrawLevelOvr1P_OracleLayouts(s_egtMesh, 0);
	s_ego.set = NULL;
	Platform_Log("[CTR Engine] oracle track: load block of level %d, %u retail layout line(s)\n", levelID, s_ego.staticLines);
	EngTrack_OracleEngineFaces(0, 0, "load");
}

// Once per frame from Engine_FrameCollect (MainFrame_RenderFrame.c:134): decides
// whether the engine draws the track this frame and keeps what the drawing needs.
// The faces themselves are chosen and submitted later in the same frame, at the
// first bucket of DrawLevelOvr1P (EngTrack_SubmitShown from Engine_TrackOwnsBucket):
// only then has the PS1 walk of this frame (RenderLists_Init1P2P,
// MainFrame_RenderFrame.c:1004) linked its leaves, and the engine draws exactly
// those. Its own walk (EngTrack_WalkLeaves) is still run here, as a check that is
// counted against the PS1's lists. Without lists nothing is kept and the PS1 walk
// draws the level (s_egtSubmitted 0) - never a frame without track.
void Engine_TrackCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	const u32 *faceList;
	const u32 *leafList;

	s_egtSubmitted = 0;
	if (s_egtPending)
	{
		// The last frame's DrawLevelOvr1P never came (no level walk): nothing was drawn.
		s_egtPending = 0;
		s_egtNotWalkedFrames++;
	}
	if (g_cfg_engine && g_cfg_engineTrack && (gGT != NULL) && ((gGT->gameMode1 & MAIN_MENU) == 0) && ((gGT->renderFlags & RENDER_FLAG_DRAW_LEVEL) != 0) &&
	    (gGT->level1 != NULL) && (gGT->level1 == s_egtLevel))
	{
		s_egtCollectFrames++;
	}
	if (!EngTrack_Active(gGT))
	{
		return;
	}
	cam = Engine_FrameCamera(0);
	if (cam == NULL)
	{
		return;
	}
	faceList = (gGT->visMem1 != NULL) ? (const u32 *)gGT->visMem1->visFaceList[0] : NULL;
	leafList = (gGT->visMem1 != NULL) ? (const u32 *)gGT->visMem1->visLeafList[0] : NULL;
	if ((faceList == NULL) || (leafList == NULL))
	{
		s_egtNoVisFrames++;
		return;
	}
	s_egtWalkValid = (EngTrack_WalkLeaves(s_egtMesh->bspRoot, s_egtNodeCount, leafList, s_egtLeafWords, &gGT->pushBuffer[0].bbox, s_egtLeafWalk,
	                                      s_egtWalkStack, s_egtNodeCount + 2) >= 0)
	                     ? 1
	                     : 0;
	Engine_CameraScreenFromWorld(cam, &s_egtScreenFromWorld);
	// Without a camera point the decals are drawn without their lift this frame (params4 = 0).
	s_egtCameraWorldOk = EngTrack_CameraWorld(&cam->viewFromWorld, s_egtCameraWorld);
	s_egtFaceListNow = faceList;
	s_egtPending = 1;
	// The engine means to draw the track this frame (the water and the instances ask now);
	// EngTrack_SubmitShown hands it back to the PS1 walk when the faces do not fit.
	s_egtSubmitted = 1;
}

// The first bucket of this frame's DrawLevelOvr1P: the leaves the PS1 walk linked
// in view 0's render lists (gGT->LevRenderLists[0], the lists RenderLists_Init1P2P
// filled at MainFrame_RenderFrame.c:1004 and DrawLevelOvr1P reads at :1007), the
// face bit of every block in them, the faces into the dynamic ring. 1 = the engine
// draws the track this frame; 0 = the PS1 buckets draw it (counted).
internal int EngTrack_SubmitShown(struct GameTracker *gGT)
{
	struct VisMemBspListNode *heads[DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT + 1];
	u32 faces = 0;
	u32 vertices = 0;
	u32 expanded = 0;
	u32 specials = 0;
	int refused = 0;
	int submittedAny = 0;
	int differs = 0;
	int i;

	for (i = 0; i < DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT; i++)
	{
		heads[i] = gGT->LevRenderLists[0].list[i].bspListStart;
	}
	heads[DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT] = gGT->LevRenderLists[0].bspListStart_FullDynamic;
	if (EngTrack_LeavesOfLists(s_egtMesh->bspRoot, s_egtNodeCount, heads, DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT + 1, s_egtLeafShown) < 0)
	{
		s_egtBadListFrames++;
		if (EngTrack_OracleOn())
		{
			Platform_Log("[CTR Engine] oracle track: this frame the PS1 walk draws the track (bad render list): no E lines\n");
		}
		return 0;
	}

	// The check: the engine's own walk against the PS1's lists (leaves the frustum
	// test of the PS1 walk dropped show up as engine-only; they cost nothing).
	if (s_egtWalkValid)
	{
		for (i = 0; i < s_egtNodeCount; i++)
		{
			if (s_egtLeafShown[i] == s_egtLeafWalk[i])
			{
				continue;
			}
			differs = 1;
			if (s_egtLeafShown[i])
			{
				s_egtWalkMissed++;
				if (s_egtWalkMissedLogged < 6)
				{
					s_egtWalkMissedLogged++;
					Platform_Log("[CTR Engine] track: the engine walk missed leaf %d (flag 0x%04x) the PS1 walk linked\n", i, (unsigned)s_egtMesh->bspRoot[i].flag);
				}
			}
			else
			{
				s_egtWalkExtra++;
			}
		}
		s_egtWalkDiffFrames += (u64)differs;
	}

	// ANIMATED ROWS: the faces whose cell gives more than one blend take the material of the blend
	// their row holds now (before the oracle and the draws read the faces).
	EngTrack_AnimFollow();

	// What the lists show, and whether it fits (else the PS1 buckets draw this frame).
	for (i = 0; i < s_egtFaceCount; i++)
	{
		const EngTrackDrawFace *df = &s_egtFaces[i];

		s_egtShown[i] = (u8)EngTrack_FaceShown(df, s_egtFaceListNow, s_egtFaceWords, s_egtLeafShown);
		if (s_egtShown[i])
		{
			faces++;
			vertices += df->face.vertCount;
			expanded += (u32)df->face.triCount * 3u;
			specials += df->special;
		}
	}
	if ((vertices > EGT_DYN_VERTICES_MAX) || (expanded > EGT_DYN_EXPANDED_MAX))
	{
		s_egtOverBudgetFrames++;
		if (EngTrack_OracleOn())
		{
			Platform_Log("[CTR Engine] oracle track: this frame the PS1 walk draws the track (over budget): no E lines\n");
		}
		return 0;
	}
	// --engine-oracle: what the shader reads for each face drawn now (RETAIL ORACLE).
	if (EngTrack_OracleOn())
	{
		EngTrack_OracleEngineFaces(1, 0, NULL); // the faces drawn now
		// and every face of an AnimTex cell, seen or not (done point D-W1.3)
		EngTrack_OracleEngineFaces(0, 1, "anim");
	}
	s_egtPeakFaces = (faces > s_egtPeakFaces) ? faces : s_egtPeakFaces;
	s_egtPeakVertices = (vertices > s_egtPeakVertices) ? vertices : s_egtPeakVertices;
	s_egtPeakExpanded = (expanded > s_egtPeakExpanded) ? expanded : s_egtPeakExpanded;
	s_egtPeakSpecial = (specials > s_egtPeakSpecial) ? specials : s_egtPeakSpecial;

	// ANIMATED ROWS: the faces drawn by the pass after the groups are left out of their groups.
	if (s_egtAnimFaceCount > 0)
	{
		memcpy(s_egtDrawShown, s_egtShown, (size_t)s_egtFaceCount);
		for (i = 0; i < s_egtAnimFaceCount; i++)
		{
			s_egtDrawShown[s_egtAnimFaces[i]] = 0;
		}
	}
	for (i = 0; i < s_egtGroupCount; i++)
	{
		const EngTrackDrawGroup *g = &s_egtGroups[i];
		const int cell = (g->semi || (g->lift != 0)) ? EGT_CELL_SEMI : EGT_CELL_OPAQUE;
		const u8 *shown = (s_egtAnimFaceCount > 0) ? s_egtDrawShown : s_egtShown;
		int next = 0;

		// Usually one draw per group; more only when its shown faces pass the u16 index range of a draw.
		while (next < g->count)
		{
			EngineDraw draw;
			u32 indexCount = 0;
			const int from = next;
			const u32 vertexCount = EngTrack_FillFacesFrom(&s_egtFaces[g->first], &shown[g->first], from, g->count, s_egtVertices, s_egtScratchV,
			                                               s_egtScratchI, EGT_DRAW_VERTICES_MAX, &indexCount, &next);

			if ((vertexCount == 0u) || (next <= from))
			{
				break;
			}
			memset(&draw, 0, sizeof(draw));
			draw.mesh = -1;
			draw.clipFromModel = s_egtScreenFromWorld; // the track is in world units: the raw s16 positions
			draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
			draw.materialOverride = -1;
			draw.sortDepth = 1.0e9f; // farther than anything: first of the transparent list
			draw.otCell = (u32)cell;
			draw.flags = g->semi ? ENGINE_DRAW_TRANSPARENT : 0;
			draw.dynVertices = s_egtScratchV;
			draw.dynVertexCount = vertexCount;
			draw.dynIndices = s_egtScratchI;
			draw.dynIndexCount = indexCount;
			draw.dynMaterial = g->material;
			if ((g->lift != 0) && s_egtCameraWorldOk)
			{
				// The decal's plane for the vertex stage of "ne": h = params4.w - normal . position is the
				// camera's distance to the plane through the vertex (see DECALS).
				draw.user[0] = g->normal[0];
				draw.user[1] = g->normal[1];
				draw.user[2] = g->normal[2];
				draw.user[3] = g->normal[0] * s_egtCameraWorld[0] + g->normal[1] * s_egtCameraWorld[1] + g->normal[2] * s_egtCameraWorld[2];
			}
			if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
			{
				submittedAny = 1;
			}
			else
			{
				refused = 1;
			}
		}
	}
	EngTrack_AnimSubmit(gGT, &submittedAny, &refused);
	if (refused)
	{
		s_egtRefusedFrames++;
		// Nothing went in: the PS1 buckets draw the level this frame. Once a group went
		// in, they must not draw it a second time: the frame stays partial (counted).
		if (!submittedAny)
		{
			return 0;
		}
	}
	s_egtFramesDrawn++;
	return 1;
}

int Engine_TrackDrawnThisFrame(void)
{
	return s_egtSubmitted;
}

// 1 = the PS1 walk of DrawLevelOvr1P treats this bucket as empty this frame.
// Buckets 0-7 and 10 are track faces (4x4, dynamic, 4x2, 4x1, full dynamic; rendered
// and list variants); 8 and 9 are water, answered by Engine_WaterOwnsBucket. The
// counts are bucket visits of the walk, not drawn buckets. The first call of a
// frame (bucket 10, game/226/226_00_DrawLevelOvr1P.c:11255-11262) submits the
// track's faces (EngTrack_SubmitShown).
int Engine_TrackOwnsBucket(int bucketIndex)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || !s_egtSubmitted || (bucketIndex < 0) || (bucketIndex >= EGT_BUCKET_COUNT))
	{
		return 0;
	}
	if (s_egtPending)
	{
		s_egtPending = 0;
		if (!EngTrack_SubmitShown(sdata->gGT))
		{
			s_egtSubmitted = 0;
			return 0;
		}
	}
	if ((bucketIndex == EGT_BUCKET_WATER_RENDERED) || (bucketIndex == EGT_BUCKET_WATER_LIST))
	{
		s_egtWaterVisits++;
		return 0;
	}
	s_egtBucketsSkipped++;
	return 1;
}

enum
{
	EGT_RULE_RETAIL = 0, // left to the retail handler, not counted
	EGT_RULE_LEVEL,      // a level instance (it has an InstDef)
	EGT_RULE_RUNTIME     // a runtime instance (see Engine_TrackInstanceRoute)
};

// Which rule of Engine_TrackInstanceRoute takes an instance; modelInTable: its
// model has an entry in the model table; containerLevel: the level is a loaded
// track container's (NativeTrack_ActiveForLevel - a container plays in its
// donor's level slot, so its gGT->levelID is a retail one). Runtime
// instances are taken on container levels only: retail births some without a
// thread too (the 'wake' model, id 67, on Dingo Canyon and Tiny Arena; E7 tour
// of 2026-10-15), and those stay with their retail handler.
internal int EngTrack_InstanceRule(const struct Instance *inst, int modelInTable, int containerLevel)
{
	if ((inst == NULL) || (inst->model == NULL))
	{
		return EGT_RULE_RETAIL;
	}
	if (inst->instDef != NULL)
	{
		return EGT_RULE_LEVEL;
	}
	return ((inst->thread == NULL) && modelInTable && containerLevel) ? EGT_RULE_RUNTIME : EGT_RULE_RETAIL;
}

// POSITION CHECK (round of 2026-10-15, S1). After every engine draw of a level
// instance: the centre of its screen box through the engine's matrix against
// the one through the retail queue's matrix (Engine_InstanceScreenBoxes); the
// largest difference per model goes into the tour's model line. A runtime
// instance also writes one line per draw (at most EGT_POSITION_LINES_MAX per
// load) with the game's frame counter (sdata->frameCounter, the frameCounter
// column of the --perf-dir frame_times.csv), so a burst shot can be matched to
// the boxes. Reads only; draws nothing.
internal void EngTrack_PositionCheck(EngTrackModel *em, const struct Instance *inst, const struct InstDrawPerPlayer *idpp, const struct PushBuffer *pb, int mesh,
                                     u32 frame, int runtime)
{
	float e[4];
	float r[4];
	float dx;
	float dy;

	if (!Engine_InstanceScreenBoxes(inst, idpp, pb, mesh, frame, e, r))
	{
		return;
	}
	dx = fabsf(((e[0] + e[2]) - (r[0] + r[2])) * 0.5f);
	dy = fabsf(((e[1] + e[3]) - (r[1] + r[3])) * 0.5f);
	em->posChecked++;
	em->posMaxDx = (dx > em->posMaxDx) ? dx : em->posMaxDx;
	em->posMaxDy = (dy > em->posMaxDy) ? dy : em->posMaxDy;
	if (runtime && (s_egtPositionLines < EGT_POSITION_LINES_MAX))
	{
		s_egtPositionLines++;
		Platform_Log("[CTR Engine] track: position fc %d '%s' '%s' engine %.4f %.4f %.4f %.4f retail %.4f %.4f %.4f %.4f\n", (int)sdata->frameCounter, em->name,
		             inst->name, e[0], e[1], e[2], e[3], r[0], r[1], r[2], r[3]);
	}
}

// One instance of a model of the table (em, NULL = no entry) drawn by
// Engine_InstanceSubmit: 1 drawn, 0 declined (counted). runtime: the instance
// came through the RUNTIME INSTANCES rule (only the first-seen line says so).
internal int EngTrack_RouteInstance(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, EngTrackModel *em, int runtime)
{
	EngineInstanceDraw d;
	const struct ModelHeader *mh0;
	int anim;
	int frame;
	int meshFrame;

	if (em == NULL)
	{
		s_egtInstDeclined++;
		s_egtInstDeclinedBy[EGT_DECLINE_MODEL]++;
		return 0;
	}
	if ((em->drawn == 0u) && (em->declined == 0u))
	{
		Platform_Log("[CTR Engine] track: instance model '%s' id %d first seen (%s)%s\n", em->name, (int)em->id,
		             em->imported ? (em->live ? "imported, live frame" : "imported") : "not imported",
		             runtime ? (em->levelListOnly ? ", runtime instance of a level list model" : ", runtime instance") : "");
		// Where retail (idpp->mvp, the matrix its queue built) and the engine (the view's camera on
		// inst->matrix) put the model's origin in view space, and the scales: the evidence for a
		// transform difference between the two paths.
		{
			const int view = Engine_InstanceViewOf(pb);
			const EngineCamera *cam = (view >= 0) ? Engine_FrameCamera(view) : NULL;
			const struct ModelHeader *h0 = ((inst->model->numHeaders >= 1) && (inst->model->headers != NULL)) ? inst->model->headers : NULL;
			EngineVec3 at;

			at.x = (float)inst->matrix.t[0];
			at.y = (float)inst->matrix.t[1];
			at.z = (float)inst->matrix.t[2];
			if (cam != NULL)
			{
				at = Engine_Mat4MulPoint(&cam->viewFromWorld, at);
			}
			Platform_Log("[CTR Engine] track:   '%s' view position retail %d %d %d, engine %.0f %.0f %.0f; inst scale %d %d %d, header 0 scale %d %d %d of %d header(s), "
			             "flags 0x%08x\n",
			             em->name, (int)idpp->mvp.t[0], (int)idpp->mvp.t[1], (int)idpp->mvp.t[2], at.x, at.y, at.z, (int)inst->scale.x, (int)inst->scale.y,
			             (int)inst->scale.z, (h0 != NULL) ? (int)h0->scale.x : 0, (h0 != NULL) ? (int)h0->scale.y : 0, (h0 != NULL) ? (int)h0->scale.z : 0,
			             (int)inst->model->numHeaders, (unsigned)inst->flags);
		}
	}
	if (!em->imported || (inst->model->numHeaders < 1) || (inst->model->headers == NULL))
	{
		em->declined++;
		s_egtInstDeclined++;
		s_egtInstDeclinedBy[EGT_DECLINE_MODEL]++;
		return 0;
	}
	if ((inst->flags & (SCREENSPACE_INSTANCE | DRAW_BILLBOARD | HIDE_MODEL)) != 0u)
	{
		em->declined++;
		s_egtInstDeclined++;
		s_egtInstDeclinedBy[EGT_DECLINE_FLAGS]++;
		return 0;
	}
	mh0 = inst->model->headers;

	// The animation frame: the logical frame of the word (EngTrack_LogicalFrame).
	anim = (int)inst->animIndex;
	frame = (int)inst->animFrame;
	{
		const struct ModelAnim *ma = NativeRenderLayer_AnimOf(mh0, (u32)anim);

		if (ma != NULL)
		{
			frame = EngTrack_LogicalFrame(ma->numFrames, (u16)inst->animFrame);
		}
		else
		{
			anim = 0;
			frame = 0;
		}
	}
	meshFrame = Engine_ModelFrame(&em->imp, anim, frame);
	if (meshFrame < 0)
	{
		em->declined++;
		s_egtInstDeclined++;
		s_egtInstDeclinedBy[EGT_DECLINE_FRAME]++;
		return 0;
	}

	memset(&d, 0, sizeof(d));
	d.mesh = em->imp.mesh;
	d.frame = (u32)meshFrame;
	d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
	d.belowTint[0] = d.belowTint[1] = d.belowTint[2] = d.belowTint[3] = 1.0f;
	d.mirrorTint[0] = d.mirrorTint[1] = d.mirrorTint[2] = d.mirrorTint[3] = 1.0f;
	d.drawBelow = 1;
	d.flags = em->transparent ? ENGINE_DRAW_TRANSPARENT : 0;
	// LIVE FRAME. The start banner (STATIC_STARTBANNERWAVE) is the model the
	// game rewrites in place: at birth its colour indices and colour table
	// (RB_Banner_LInB, game/231/RB_Banner.c:210-266, after this load stage
	// imported the disc state), every tick its vertex heights and the turn of
	// its colour table (RB_Banner_ThTick -> RB_Banner_Animate_Play, :161-206).
	// It is drawn from the frame rebuilt from that state, read when retail would
	// draw it; if that fails, from the imported frame (logged once).
	if (em->live)
	{
		if (Engine_ModelLiveFrame(&em->imp, inst->model, s_egtLiveVertices, EGT_LIVE_VERTICES_MAX) > 0u)
		{
			d.liveVertices = s_egtLiveVertices;
		}
		else if (!em->liveRefusedLogged)
		{
			em->liveRefusedLogged = 1;
			Platform_Log("[CTR Engine] track: instance model '%s' id %d: live frame refused, the imported frame is drawn\n", em->name, (int)em->id);
		}
	}
	// THE RETAIL WRITER (round 18, platform/engine_inst_light.c): colour and
	// blend as the primitive writer and the water/mirror side selector retail
	// chose for this instance from its flags (funcPtr[1], funcPtr[2]) - the
	// side tints of the split and mirror draws, and for the key and lit writers
	// the lit triangles of the mesh's variant. A writer or a side selection the
	// engine does not show is left to retail (declined, counted under flags).
	if (!Engine_InstLightWriterFollowed(inst) || !Engine_InstLightSplit(inst, idpp, &d) || !Engine_InstLightApply(inst, idpp, em->imp.mesh, (u32)meshFrame, &d))
	{
		em->declined++;
		s_egtInstDeclined++;
		s_egtInstDeclinedBy[EGT_DECLINE_FLAGS]++;
		return 0;
	}
	if (Engine_InstanceSubmit(inst, idpp, pb, &d) > 0)
	{
		em->drawn++;
		s_egtInstDrawn++;
		// The position of the imported mesh frame (a lit variant has the same corners).
		EngTrack_PositionCheck(em, inst, idpp, pb, em->imp.mesh, (u32)meshFrame, runtime);
		return 1;
	}
	em->declined++;
	s_egtInstDeclined++;
	s_egtInstDeclinedBy[EGT_DECLINE_SUBMIT]++;
	return 0;
}

// 1 = the engine draws this instance of the level (Engine_InstanceSubmit), 0 =
// the retail handler draws it. Called for every instance retail would draw now
// (RenderBucket_DispatchDrawFunc), only while the engine draws the track.
//
// LEVEL INSTANCES: inst->instDef != NULL (INSTANCE.c:395 links it to its InstDef
// in the LEV, only level instances have one); the model was imported at load
// or gets an entry here (counted as declined when it has no import).
//
// RUNTIME INSTANCES (round of 2026-10-15): instDef NULL (INSTANCE_Birth sets 0,
// game/INSTANCE.c:68) AND no thread AND a model of the table (an InstDef model
// or one of the level's model list, EngTrack_ImportModels). That is an instance
// native code births from a level model and places itself, writing
// inst->matrix every tick (a track mod's runtime instances): the engine reads
// that state at draw time like for any instance. Retail births its runtime
// instances with a thread (explosions, shadows, warp pad prizes, ...); they and
// every instance of a model outside the table stay with retail, uncounted, as
// before.
//
// Declined (0, counted): the model has no import, a screen-space or billboard
// or hidden instance, a frame out of range, or the submit refusing.
int Engine_TrackInstanceRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	EngTrackModel *em;

	if (!g_cfg_engine || !g_cfg_engineTrack || !s_egtSubmitted || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (sdata->gGT == NULL) ||
	    (inst->model == NULL))
	{
		return 0;
	}
	em = EngTrack_ModelEntry(inst->model, 0);
	switch (EngTrack_InstanceRule(inst, em != NULL, NativeTrack_ActiveForLevel(s_egtLevelID)))
	{
	case EGT_RULE_LEVEL:
		return EngTrack_RouteInstance(inst, idpp, pb, (em != NULL) ? em : EngTrack_ModelEntry(inst->model, 1), 0);
	case EGT_RULE_RUNTIME:
		if (EngTrack_RouteInstance(inst, idpp, pb, em, 1))
		{
			s_egtRuntimeDrawn++;
			return 1;
		}
		s_egtRuntimeDeclined++;
		return 0;
	default:
		return 0;
	}
}

// --------------------------------------------------------------------------
// Selftest: the quadblock builder on synthetic data.

#define EGT_CHECK(cond, ...)                           \
	do                                                 \
	{                                                  \
		(*checks)++;                                   \
		if (!(cond))                                   \
		{                                              \
			printf("engine selftest: track " __VA_ARGS__); \
			printf("\n");                              \
			failures++;                                \
		}                                              \
	} while (0)

internal void EngTrack_TestBlock(EngTrackBlockIn *in, const struct TextureLayout *layout)
{
	static const s16 xz[9][2] = {{0, 0}, {200, 0}, {0, 200}, {200, 200}, {100, 0}, {0, 100}, {100, 100}, {200, 100}, {100, 200}};
	int i;
	int k;

	memset(in, 0, sizeof(*in));
	for (i = 0; i < 9; i++)
	{
		in->pos[i][0] = xz[i][0];
		in->pos[i][1] = (s16)(i * 3); // not flat; any non-collinear triple in x/z has area
		in->pos[i][2] = xz[i][1];
		in->color[i][0] = (u8)(10 + i);
		in->color[i][1] = (u8)(20 + i);
		in->color[i][2] = (u8)(30 + i);
	}
	for (k = 0; k < 4; k++)
	{
		in->layout[k] = layout;
	}
}

internal int EngTrack_TestIsPtr(u32 word)
{
	return (word == 0x1234u) ? 1 : 0;
}

// A test face for the decal pass: the quad x0..x1 by z 0..200 at height y0 + slope * x
// (slots v0 (x0,0), v1 (x1,0), v2 (x0,200), v3 (x1,200), triangles (0,1,2) and (1,3,2)).
internal void EngTrack_TestDecalFace(EngTrackDrawFace *df, float x0, float x1, float y0, float slope, int bias)
{
	int v;

	memset(df, 0, sizeof(*df));
	for (v = 0; v < 4; v++)
	{
		const float x = (v & 1) ? x1 : x0;

		df->face.v[v].position[0] = x;
		df->face.v[v].position[1] = y0 + slope * x;
		df->face.v[v].position[2] = (v & 2) ? 200.0f : 0.0f;
	}
	df->face.vertCount = 4;
	df->face.triCount = 2;
	df->face.tri[0][0] = 0;
	df->face.tri[0][1] = 1;
	df->face.tri[0][2] = 2;
	df->face.tri[1][0] = 1;
	df->face.tri[1][1] = 3;
	df->face.tri[1][2] = 2;
	df->face.bias = (s8)bias;
}

int Engine_SelfTestTrack(int *checks)
{
	int failures = 0;
	struct TextureLayout lay;
	EngTrackBlockIn in;
	EngTrackFace out[4];
	int n;
	int k;
	int v;
	static const u32 identity = 0x18100800u;

	memset(&lay, 0, sizeof(lay));
	lay.u0 = 1;
	lay.v0 = 2;
	lay.clut = 0x7a40;
	lay.u1 = 31;
	lay.v1 = 4;
	lay.tpage = 0x011f; // abr 0 (semi), 8 bit
	lay.u2 = 6;
	lay.v2 = 33;
	lay.u3 = 36;
	lay.v3 = 38;

	// (1) the identity entry: slot v on corner v, UV v, reversed (flip clear, one-sided), T1 (0,1,2) T2 (1,3,2).
	EngTrack_TestBlock(&in, &lay);
	n = EngTrack_BuildBlock(&in, &identity, 1, out, NULL);
	EGT_CHECK(n == 4, "identity: %d faces, expected 4", n);
	for (k = 0; (k < n) && (k < 4); k++)
	{
		const float uu[4] = {1, 31, 6, 36};
		const float vv[4] = {2, 4, 33, 38};
		int bad = 0;

		for (v = 0; v < 4; v++)
		{
			const int g = s_egtGrid[k][v];

			bad += (out[k].v[v].position[0] != (float)in.pos[g][0]) || (out[k].v[v].position[1] != (float)in.pos[g][1]) ||
			       (out[k].v[v].position[2] != (float)in.pos[g][2]) || (out[k].v[v].texcoord[0] != uu[v]) || (out[k].v[v].texcoord[1] != vv[v]) ||
			       (out[k].v[v].color[0] != in.color[g][0]) || (out[k].v[v].color[3] != 255);
		}
		EGT_CHECK(bad == 0, "identity: face %d vertices/UVs differ in %d slot(s)", k, bad);
		for (v = 0, bad = 0; v < 4; v++)
		{
			int box[4];

			Engine_VertexBoxGet(&out[k].v[v], box);
			bad += (box[0] != 1) || (box[1] != 2) || (box[2] != 36) || (box[3] != 38);
		}
		EGT_CHECK(bad == 0, "identity: face %d UV box is not 1,2,36,38 on %d slot(s)", k, bad);
		EGT_CHECK((out[k].triCount == 2) && (out[k].vertCount == 4) && (out[k].tri[0][0] == 2) && (out[k].tri[0][1] == 1) && (out[k].tri[0][2] == 0) &&
		              (out[k].tri[1][0] == 2) && (out[k].tri[1][1] == 3) && (out[k].tri[1][2] == 1),
		          "identity: face %d triangles are not the reversed (0,1,2) and (1,3,2)", k);
		EGT_CHECK((out[k].tpage == 0x011f) && (out[k].clut == 0x7a40) && (out[k].doubleSided == 0), "identity: face %d key fields", k);
	}

	// (2) mirrored UV: bit 23 -> {uv1, uv0, uv3, uv2}.
	{
		const u32 mirrored = 0x18900800u;
		const float uu[4] = {31, 1, 36, 6};
		const float vv[4] = {4, 2, 38, 33};
		int bad = 0;

		n = EngTrack_BuildBlock(&in, &mirrored, 1, out, NULL);
		for (v = 0; v < 4; v++)
		{
			bad += (out[0].v[v].texcoord[0] != uu[v]) || (out[0].v[v].texcoord[1] != vv[v]);
		}
		EGT_CHECK((n == 4) && (bad == 0), "mirrored UV order wrong (%d slots differ)", bad);
	}

	// (3) triangle mode: bit 7 of the last byte, slot 3 on the corner of slot 2 -> T2 degenerate and
	// dropped, slot 3 keeps the UV of slot 2. This word puts the slots on corners 1, 2, 0, 0.
	{
		const u32 tri = 0x10081898u;

		n = EngTrack_BuildBlock(&in, &tri, 1, out, NULL);
		EGT_CHECK((n == 4) && (out[0].triCount == 1) && (out[0].vertCount == 3), "triangle mode: %d faces, %d triangles, %d vertices (expected 4, 1, 3)", n,
		          out[0].triCount, out[0].vertCount);
		EGT_CHECK((out[0].v[0].position[0] == (float)in.pos[s_egtGrid[0][1]][0]) && (out[0].v[0].position[2] == (float)in.pos[s_egtGrid[0][1]][2]) &&
		              (out[0].v[1].position[0] == (float)in.pos[s_egtGrid[0][2]][0]) && (out[0].v[1].position[2] == (float)in.pos[s_egtGrid[0][2]][2]) &&
		              (out[0].v[2].position[0] == (float)in.pos[s_egtGrid[0][0]][0]) && (out[0].v[2].position[2] == (float)in.pos[s_egtGrid[0][0]][2]),
		          "triangle mode: slots are not on corners 1, 2, 0");
		EGT_CHECK((out[0].v[3].texcoord[0] == out[0].v[2].texcoord[0]) && (out[0].v[3].texcoord[1] == out[0].v[2].texcoord[1]),
		          "triangle mode: slot 3 did not keep the UV of slot 2");
		{
			// The UV box of the one triangle: its three corners (slots 0, 1, 2), not slot 3's.
			const u8 u0 = Engine_UvBoxTexel(out[0].v[0].texcoord[0]);
			const u8 u1 = Engine_UvBoxTexel(out[0].v[1].texcoord[0]);
			const u8 u2 = Engine_UvBoxTexel(out[0].v[2].texcoord[0]);
			const u8 t0 = Engine_UvBoxTexel(out[0].v[0].texcoord[1]);
			const u8 t1 = Engine_UvBoxTexel(out[0].v[1].texcoord[1]);
			const u8 t2 = Engine_UvBoxTexel(out[0].v[2].texcoord[1]);
			const u8 umin = (u0 < u1) ? ((u0 < u2) ? u0 : u2) : ((u1 < u2) ? u1 : u2);
			const u8 umax = (u0 > u1) ? ((u0 > u2) ? u0 : u2) : ((u1 > u2) ? u1 : u2);
			const u8 tmin = (t0 < t1) ? ((t0 < t2) ? t0 : t2) : ((t1 < t2) ? t1 : t2);
			const u8 tmax = (t0 > t1) ? ((t0 > t2) ? t0 : t2) : ((t1 > t2) ? t1 : t2);
			int box[4];

			Engine_VertexBoxGet(&out[0].v[0], box);
			EGT_CHECK((box[0] == umin) && (box[1] == tmin) && (box[2] == umax) && (box[3] == tmax) && (memcmp(out[0].v[3].uvBox, out[0].v[0].uvBox, 4) == 0),
			          "triangle mode: UV box %d %d %d %d, expected %u %u %u %u on all slots", box[0], box[1], box[2], box[3], umin, tmin, umax, tmax);
		}
	}

	// (4) the winding: the flip bit and double sided both draw as listed.
	{
		const u32 flip = 0x98100800u;
		EngTrackBlockIn dbl = in;

		n = EngTrack_BuildBlock(&in, &flip, 1, out, NULL);
		EGT_CHECK((n == 4) && (out[0].tri[0][0] == 0) && (out[0].tri[0][1] == 1) && (out[0].tri[0][2] == 2) && (out[0].tri[1][0] == 1) &&
		              (out[0].tri[1][1] == 3) && (out[0].tri[1][2] == 2),
		          "flip bit: triangles are not as listed");
		dbl.drawOrderLow = 0x80000000u;
		n = EngTrack_BuildBlock(&dbl, &identity, 1, out, NULL);
		EGT_CHECK((n == 4) && (out[0].doubleSided == 1) && (out[0].tri[0][0] == 0) && (out[0].tri[0][2] == 2), "double sided: not as listed or not flagged");
	}

	// (5) a blank (NULL) layout skips its face; a field past the table uses entry 0 and is counted.
	{
		EngTrackBlockIn blank = in;
		int fields = 0;
		struct TextureLayout zero;

		blank.layout[1] = NULL;
		blank.layout[2] = NULL;
		n = EngTrack_BuildBlock(&blank, &identity, 1, out, NULL);
		EGT_CHECK(n == 2, "blank layouts: %d faces, expected 2", n);
		blank = in;
		blank.drawOrderLow = 5u << 8; // field 5 of a one-entry table
		n = EngTrack_BuildBlock(&blank, &identity, 1, out, &fields);
		EGT_CHECK((n == 4) && (fields == 1), "field past the table: %d faces, %d counted", n, fields);

		memset(&zero, 0, sizeof(zero));
		EGT_CHECK(!EngTrack_LayoutUsable(&zero) && !EngTrack_LayoutUsable(NULL) && EngTrack_LayoutUsable(&lay), "layout rule: blank, NULL, real");
		zero = lay;
		zero.tpage = 0x0a1f; // bits 9 and 11 set: the sieve rejects
		EGT_CHECK(!EngTrack_LayoutUsable(&zero), "layout rule: tpage bits from 9 up");
	}

	// (6) the AnimTex indirection is resolved: ptr | 1 names a cell that holds the real group.
	{
		struct TextureLayout group[4];
		u32 cell;
		int anim = -1;
		const struct TextureLayout *r;

		memset(group, 0, sizeof(group));
		cell = (u32)(size_t)group;
		r = EngTrack_ResolveGroup(((u32)(size_t)&cell) | 1u, NULL, &anim);
		EGT_CHECK((r == group) && (anim == 1), "AnimTex indirection not resolved");
		r = EngTrack_ResolveGroup((u32)(size_t)group, NULL, &anim);
		EGT_CHECK((r == group) && (anim == 0), "plain texture pointer changed");
		EGT_CHECK(EngTrack_ResolveGroup(0u, NULL, &anim) == NULL, "NULL texture pointer is not NULL");
	}

	// (7) mosaic detection: a negative first word of level 3, or a pointer the level recognizes, picks level 2.
	{
		struct TextureLayout group[4];
		int mosaic = -1;

		memset(group, 0, sizeof(group));
		group[2] = lay;
		group[3] = lay;
		EGT_CHECK((EngTrack_FinestLayout(group, EngTrack_TestIsPtr, &mosaic) == &group[3]) && (mosaic == 0), "real level 3 not picked");
		group[3].clut = 0x8000; // the word is negative
		EGT_CHECK((EngTrack_FinestLayout(group, EngTrack_TestIsPtr, &mosaic) == &group[2]) && (mosaic == 1), "negative mosaic word did not pick level 2");
		group[3].u0 = 0x34;
		group[3].v0 = 0x12;
		group[3].clut = 0; // the word is 0x1234
		EGT_CHECK((EngTrack_FinestLayout(group, EngTrack_TestIsPtr, &mosaic) == &group[2]) && (mosaic == 1), "level pointer did not pick level 2");
		memset(&group[3], 0, sizeof(group[3]));
		EGT_CHECK((EngTrack_FinestLayout(group, NULL, &mosaic) == &group[2]) && (mosaic == 0), "blank level 3 did not fall back to level 2");
	}

	// (8) material dedupe: the same key gives the same index, any field of it a new one.
	{
		static EngTrackKeySet set;
		int a;
		int b;

		set.count = 0;
		a = EngTrack_KeysFind(&set, 0x011f, 0x7a40, 0, 0);
		b = EngTrack_KeysFind(&set, 0x011f, 0x7a40, 0, 0);
		EGT_CHECK((a == 0) && (b == 0) && (set.count == 1), "key dedupe: the same key twice gave %d, %d (count %d)", a, b, set.count);
		EGT_CHECK((EngTrack_KeysFind(&set, 0x011f, 0x7a40, 0, -3) == 1) && (EngTrack_KeysFind(&set, 0x011f, 0x7a40, 1, 0) == 2) &&
		              (EngTrack_KeysFind(&set, 0x011f, 0x7a41, 0, 0) == 3) && (EngTrack_KeysFind(&set, 0x0100, 0x7a40, 0, 0) == 4) && (set.count == 5),
		          "key dedupe: bias, double sided, clut or tpage did not make a new key");
		EGT_CHECK((EngTrack_KeysFind(&set, 0x011f, 0x7a40, 0, -3) == 1) && (set.count == 5), "key dedupe: a known key made a new one");
		EGT_CHECK(EngTrack_IsSemi(0x0100) && !EngTrack_IsSemi(0x0160), "semi rule: abr 0 is semi, abr 3 is opaque");
	}

	// (9) draw faces: the grid of the slot vertices, the touch test, the fill with and without live vertices.
	{
		const u16 blockIndex[9] = {10, 11, 12, 13, 14, 15, 16, 17, 18};
		struct LevVertex verts[20];
		u8 live[20];
		EngTrackDrawFace df[2];
		EngineVertex outV[8];
		u16 outI[12];
		u32 indexCount = 0;
		u32 vertexCount;
		const u8 shownSecond[2] = {0, 1};

		EngTrack_TestBlock(&in, &lay);
		n = EngTrack_BuildBlock(&in, &identity, 1, out, NULL);
		EGT_CHECK((n == 4) && (out[1].grid[0] == 4) && (out[1].grid[1] == 1) && (out[1].grid[2] == 6) && (out[1].grid[3] == 7),
		          "draw faces: face 1 grid %d %d %d %d (expected 4 1 6 7)", out[1].grid[0], out[1].grid[1], out[1].grid[2], out[1].grid[3]);
		memset(live, 0, sizeof(live));
		live[16] = 1; // grid corner 6 of this block: the middle vertex, shared by all four faces
		EGT_CHECK(EngTrack_FaceTouches(&out[0], blockIndex, live, 20) && EngTrack_FaceTouches(&out[3], blockIndex, live, 20),
		          "live: a face on the middle vertex not touched");
		live[16] = 0;
		live[11] = 1; // grid corner 1: only face 1
		EGT_CHECK(!EngTrack_FaceTouches(&out[0], blockIndex, live, 20) && EngTrack_FaceTouches(&out[1], blockIndex, live, 20), "live: the corner test is wrong");
		EGT_CHECK(!EngTrack_FaceTouches(&out[1], blockIndex, live, 11), "live: a vertex past the level is touched");

		memset(verts, 0, sizeof(verts));
		for (k = 0; k < 20; k++)
		{
			verts[k].pos.x = (s16)(1000 + k);
			verts[k].pos.y = (s16)(2000 + k);
			verts[k].pos.z = (s16)(3000 + k);
			verts[k].color_hi[0] = (u8)(100 + k);
			verts[k].color_hi[1] = (u8)(120 + k);
			verts[k].color_hi[2] = (u8)(140 + k);
		}
		memset(df, 0, sizeof(df));
		for (k = 0; k < 2; k++)
		{
			df[k].face = out[k];
			df[k].live = 1;
			for (v = 0; v < 4; v++)
			{
				df[k].vertex[v] = blockIndex[out[k].grid[v]];
			}
		}
		vertexCount = EngTrack_FillFaces(df, NULL, 2, verts, outV, outI, &indexCount);
		EGT_CHECK((vertexCount == 8u) && (indexCount == 12u), "fill: %u vertices, %u indices (expected 8, 12)", vertexCount, indexCount);
		EGT_CHECK((outV[4].position[0] == 1014.0f) && (outV[4].position[1] == 2014.0f) && (outV[4].position[2] == 3014.0f) && (outV[4].color[0] == 114) &&
		              (outV[4].color[2] == 154) && (outV[5].position[0] == 1011.0f),
		          "fill: live positions/colours not read from the level vertices");
		EGT_CHECK((outV[4].texcoord[0] == out[1].v[0].texcoord[0]) && (outV[4].texcoord[1] == out[1].v[0].texcoord[1]) && (outV[4].color[3] == 255),
		          "fill: texture coordinates or alpha changed");
		EGT_CHECK((outI[6] == (u16)(4 + out[1].tri[0][0])) && (outI[11] == (u16)(4 + out[1].tri[1][2])), "fill: indices not offset to the second face");
		df[1].live = 0;
		vertexCount = EngTrack_FillFaces(df, shownSecond, 2, verts, outV, outI, &indexCount);
		EGT_CHECK((vertexCount == 4u) && (indexCount == 6u) && (outV[0].position[0] == out[1].v[0].position[0]) && (outV[0].color[0] == out[1].v[0].color[0]) &&
		              (outI[0] == out[1].tri[0][0]),
		          "fill: a hidden face written, or a static face not as built (%u vertices)", vertexCount);
		// Chunks: room for one face of 4 stops before the second; the next call starts there and counts from 0.
		{
			int next = -1;

			vertexCount = EngTrack_FillFacesFrom(df, NULL, 0, 2, verts, outV, outI, 7u, &indexCount, &next);
			EGT_CHECK((vertexCount == 4u) && (indexCount == 6u) && (next == 1), "chunk: first %u vertices, %u indices, next %d (expected 4, 6, 1)", vertexCount,
			          indexCount, next);
			vertexCount = EngTrack_FillFacesFrom(df, NULL, next, 2, verts, outV, outI, 7u, &indexCount, &next);
			EGT_CHECK((vertexCount == 4u) && (indexCount == 6u) && (next == 2) && (outI[0] == (u16)out[1].tri[0][0]) &&
			              (outV[0].position[0] == out[1].v[0].position[0]),
			          "chunk: second %u vertices, next %d, indices not from 0", vertexCount, next);
			vertexCount = EngTrack_FillFacesFrom(df, NULL, 0, 2, verts, outV, outI, 3u, &indexCount, &next);
			EGT_CHECK((vertexCount == 0u) && (next == 0), "chunk: a face larger than the room was written or skipped (next %d)", next);
		}
	}

	// (11) the pose of an animation word: logical frames, half-frame animations not halved, clamped.
	EGT_CHECK((EngTrack_LogicalFrame(5, 3) == 3) && (EngTrack_LogicalFrame(5, 9) == 4) && (EngTrack_LogicalFrame(0x8005, 3) == 3) &&
	              (EngTrack_LogicalFrame(0x8005, 4) == 4) && (EngTrack_LogicalFrame(0x8005, 200) == 4) && (EngTrack_LogicalFrame(0, 7) == 0) &&
	              (EngTrack_LogicalFrame(0x8000, 3) == 0),
	          "pose: the logical frame of an animation word is wrong");

	// (10) visibility: the face-list bits of a leaf as the PS1 walk consumes them, the leaf bit, the tests.
	{
		u16 words[4];
		u8 bits[4];
		u16 lw = 0;
		u8 lb = 0;
		u32 faceList[3];
		u32 leafList[2];
		const u8 reachedLeaves[3] = {0, 1, 0};
		EngTrackDrawFace f;

		EngTrack_LeafFaceBits(5, 3, words, bits);
		EGT_CHECK((words[0] == 0) && (bits[0] == 26) && (words[1] == 0) && (bits[1] == 27) && (words[2] == 0) && (bits[2] == 28),
		          "visibility: blocks 5.. got word/bit %d/%d %d/%d %d/%d (expected 0/26 0/27 0/28)", words[0], bits[0], words[1], bits[1], words[2], bits[2]);
		EngTrack_LeafFaceBits(32, 2, words, bits);
		EGT_CHECK((words[0] == 1) && (bits[0] == 31) && (words[1] == 2) && (bits[1] == 0), "visibility: first block 32 got %d/%d then %d/%d (expected 1/31 2/0)",
		          words[0], bits[0], words[1], bits[1]);
		EngTrack_LeafFaceBits(31, 2, words, bits);
		EGT_CHECK((words[0] == 0) && (bits[0] == 0) && (words[1] == 0) && (bits[1] == 1), "visibility: first block 31 got %d/%d then %d/%d (expected 0/0 0/1)",
		          words[0], bits[0], words[1], bits[1]);
		EngTrack_LeafBit(37, &lw, &lb);
		EGT_CHECK((lw == 1) && (lb == 26), "visibility: leaf 37 got word %d bit %d (expected 1, 26)", lw, lb);
		EngTrack_LeafBit(0x3fff, &lw, &lb);
		EGT_CHECK((lw == 0x1ff) && (lb == 0), "visibility: leaf 0x3fff got word %d bit %d (expected 0x1ff, 0)", lw, lb);

		faceList[0] = 0x04000000u; // bit 26
		faceList[1] = 0u;
		faceList[2] = 0u;
		leafList[0] = 0u;
		leafList[1] = 0x04000000u; // leaf 37
		EGT_CHECK(EngTrack_ListBit(faceList, 3, 0, 26) && !EngTrack_ListBit(faceList, 3, 0, 27) && EngTrack_ListBit(faceList, 3, 5, 0),
		          "visibility: list bit test (set, clear, past the list)");
		memset(&f, 0, sizeof(f));
		f.faceWord = 0;
		f.faceBit = 26;
		f.leafNode = 1;
		EGT_CHECK(EngTrack_FaceShown(&f, faceList, 3, reachedLeaves), "visibility: a face in a reached leaf with its bit set is hidden");
		f.faceBit = 27;
		EGT_CHECK(!EngTrack_FaceShown(&f, faceList, 3, reachedLeaves), "visibility: a face with its bit clear is shown");
		f.faceBit = 26;
		f.leafNode = 2;
		EGT_CHECK(!EngTrack_FaceShown(&f, faceList, 3, reachedLeaves), "visibility: a face in a leaf not reached is shown");
		EGT_CHECK(EngTrack_FaceShown(&f, NULL, 3, NULL), "visibility: without lists a face is hidden");

		// The walk: root 0 -> leaf 1, branch 2 -> leaves 3, 4. Leaf list: 1, 2, 3 set; 4 clear.
		{
			struct BSP tree[5];
			struct BoundingBox view;
			u8 shownNodes[5];
			u16 walkStack[8];
			u32 leaves[1];
			int reached;

			memset(tree, 0, sizeof(tree));
			for (k = 0; k < 5; k++)
			{
				tree[k].box.min.x = tree[k].box.min.y = tree[k].box.min.z = -10;
				tree[k].box.max.x = tree[k].box.max.y = tree[k].box.max.z = 10;
			}
			tree[0].data.branch.childID[0] = (BspChildId)(BSP_CHILD_ID_LEAF_FLAG | 1);
			tree[0].data.branch.childID[1] = 2;
			tree[1].flag = BSP_NODE_FLAG_LEAF;
			tree[2].data.branch.childID[0] = (BspChildId)(BSP_CHILD_ID_LEAF_FLAG | 3);
			tree[2].data.branch.childID[1] = (BspChildId)(BSP_CHILD_ID_LEAF_FLAG | 4);
			tree[3].flag = BSP_NODE_FLAG_LEAF;
			tree[4].flag = BSP_NODE_FLAG_LEAF;
			leaves[0] = (1u << 30) | (1u << 29) | (1u << 28); // nodes 1, 2, 3
			view.min.x = view.min.y = view.min.z = -100;
			view.max.x = view.max.y = view.max.z = 100;
			reached = EngTrack_WalkLeaves(tree, 5, leaves, 1, &view, shownNodes, walkStack, 8);
			EGT_CHECK((reached == 2) && shownNodes[1] && shownNodes[3] && !shownNodes[4] && !shownNodes[2], "walk: reached %d, leaves 1 %d 3 %d 4 %d", reached,
			          shownNodes[1], shownNodes[3], shownNodes[4]);
			leaves[0] = (1u << 30) | (1u << 28); // branch 2 cleared: its leaves are not reached
			reached = EngTrack_WalkLeaves(tree, 5, leaves, 1, &view, shownNodes, walkStack, 8);
			EGT_CHECK((reached == 1) && shownNodes[1] && !shownNodes[3], "walk: a leaf under a hidden branch was reached");
			leaves[0] = (1u << 30) | (1u << 29) | (1u << 28);
			tree[1].box.min.x = 200;
			tree[1].box.max.x = 300; // outside the view's box
			reached = EngTrack_WalkLeaves(tree, 5, leaves, 1, &view, shownNodes, walkStack, 8);
			EGT_CHECK((reached == 1) && !shownNodes[1] && shownNodes[3], "walk: a leaf outside the view's box was reached");
			tree[1].box.min.x = -10;
			tree[1].box.max.x = 10;
			reached = EngTrack_WalkLeaves(tree, 5, leaves, 1, &view, shownNodes, walkStack, 1);
			EGT_CHECK(reached == -1, "walk: a full stack not reported");

			// The PS1 walk's render lists: two heads, leaves 3 and 1, the same leaf twice.
			{
				struct VisMemBspListNode nodes[3];
				struct VisMemBspListNode *heads[2];

				nodes[0].bsp = &tree[3];
				nodes[0].next = &nodes[1];
				nodes[1].bsp = &tree[1];
				nodes[1].next = NULL;
				nodes[2].bsp = &tree[3];
				nodes[2].next = NULL;
				heads[0] = &nodes[0];
				heads[1] = &nodes[2];
				reached = EngTrack_LeavesOfLists(tree, 5, heads, 2, shownNodes);
				EGT_CHECK((reached == 2) && shownNodes[1] && shownNodes[3] && !shownNodes[4], "lists: %d leaves (expected 1 and 3)", reached);
				nodes[2].bsp = &tree[2]; // a branch in a list of leaves
				EGT_CHECK(EngTrack_LeavesOfLists(tree, 5, heads, 2, shownNodes) == -1, "lists: a branch node not refused");
				nodes[2].bsp = &tree[3];
				nodes[1].next = &nodes[0]; // a cycle
				EGT_CHECK(EngTrack_LeavesOfLists(tree, 5, heads, 2, shownNodes) == -1, "lists: a cycle not refused");
			}
		}
	}

	// (12) decals: the Turbo Track pad on its climbing road, and what is no decal.
	{
		EngTrackDrawFace df[8];
		u8 lift[8];
		float normal[24];
		int decals;

		EngTrack_TestDecalFace(&df[0], 0.0f, 800.0f, 64.0f, 1.0f / 12.0f, 16); // the road, y = 64 + x / 12
		EngTrack_TestDecalFace(&df[1], 384.0f, 656.0f, 96.0f, 0.0f, 0);        // pad half under the road (to 22.7)
		EngTrack_TestDecalFace(&df[2], 100.0f, 200.0f, 96.0f, 0.0f, 0);        // pad over the road
		EngTrack_TestDecalFace(&df[3], 700.0f, 900.0f, 130.0f, 0.0f, 0);       // half off the road
		EngTrack_TestDecalFace(&df[4], 100.0f, 200.0f, 96.0f, 0.0f, 16);       // the road's own bias
		EngTrack_TestDecalFace(&df[5], 100.0f, 200.0f, 200.0f, 0.0f, 0);       // far over the road
		EngTrack_TestDecalFace(&df[6], 100.0f, 200.0f, 96.0f, 0.0f, 0);        // a live face
		df[6].live = 1;
		EngTrack_TestDecalFace(&df[7], 100.0f, 100.0f, 80.0f, 0.0f, 0);        // a wall: x fixed, z and y vary
		df[7].face.v[1].position[0] = 100.0f;
		df[7].face.v[1].position[1] = 120.0f;
		df[7].face.v[1].position[2] = 0.0f;
		df[7].face.v[3].position[1] = 120.0f;
		decals = EngTrack_FindDecals(df, 8, lift, normal);
		EGT_CHECK((lift[1] >= 24) && (lift[1] <= 26), "decals: pad under the road lifted %d (expected 24..26)", lift[1]);
		EGT_CHECK(lift[2] == 2, "decals: pad over the road lifted %d (expected the margin 2)", lift[2]);
		EGT_CHECK((lift[0] == 0) && (lift[3] == 0) && (lift[4] == 0) && (lift[5] == 0) && (lift[6] == 0) && (lift[7] == 0),
		          "decals: no-decal faces lifted (road %d, half off %d, same bias %d, far %d, live %d, wall %d)", lift[0], lift[3], lift[4], lift[5],
		          lift[6], lift[7]);
		EGT_CHECK(decals == 2, "decals: %d found (expected 2)", decals);
		EGT_CHECK((fabsf(normal[0] + 0.0830f) < 1e-3f) && (fabsf(normal[1] - 0.9965f) < 1e-3f) && (fabsf(normal[2]) < 1e-5f) && (fabsf(normal[3]) < 1e-5f) &&
		              (fabsf(normal[4] - 1.0f) < 1e-5f),
		          "decals: normals (road %.4f %.4f %.4f, expected -0.0830 0.9965 0; pad y %.4f, expected 1)", normal[0], normal[1], normal[2], normal[4]);
	}

	// (13) decal keys and the camera point.
	{
		static EngTrackKeySet set;
		const float up[3] = {0.0f, 1.0f, 0.0f};
		const float tilt[3] = {-0.0830f, 0.9965f, 0.0f};
		const float c[3] = {100.0f, 50.0f, -30.0f};
		EngineMat4 view;
		float got[3];
		int a;

		set.count = 0;
		a = EngTrack_KeysFind(&set, 0x011f, 0x7a40, 0, 0);
		EGT_CHECK((EngTrack_KeysFindLifted(&set, 0x011f, 0x7a40, 0, 0, 0, tilt) == a) && (set.count == 1), "decal keys: lift 0 is the plain key");
		EGT_CHECK((EngTrack_KeysFindLifted(&set, 0x011f, 0x7a40, 0, 0, 25, up) == 1) && (EngTrack_KeysFindLifted(&set, 0x011f, 0x7a40, 0, 0, 25, up) == 1) &&
		              (EngTrack_KeysFindLifted(&set, 0x011f, 0x7a40, 0, 0, 2, up) == 2) && (EngTrack_KeysFindLifted(&set, 0x011f, 0x7a40, 0, 0, 25, tilt) == 3) &&
		              (set.count == 4),
		          "decal keys: lift and normal did not key (count %d)", set.count);
		EGT_CHECK((set.key[3].lift == 25) && (set.key[3].normal[0] == tilt[0]) && (set.key[0].lift == 0) && (set.key[0].normal[1] == 1.0f),
		          "decal keys: stored lift/normal wrong");

		// view = M * world + t, M a quarter turn about y with a scale of 2 on x: the camera point is c.
		memset(&view, 0, sizeof(view));
		view.m[0 * 4 + 2] = -1.0f; // a[2][0]
		view.m[1 * 4 + 1] = 1.0f;  // a[1][1]
		view.m[2 * 4 + 0] = 2.0f;  // a[0][2]
		view.m[15] = 1.0f;
		view.m[12] = -(2.0f * c[2]);
		view.m[13] = -(c[1]);
		view.m[14] = -(-c[0]);
		EGT_CHECK(EngTrack_CameraWorld(&view, got) && (fabsf(got[0] - c[0]) < 1e-3f) && (fabsf(got[1] - c[1]) < 1e-3f) && (fabsf(got[2] - c[2]) < 1e-3f),
		          "camera point: got %.3f %.3f %.3f (expected 100 50 -30)", got[0], got[1], got[2]);
		memset(&view, 0, sizeof(view));
		EGT_CHECK(!EngTrack_CameraWorld(&view, got), "camera point: a singular matrix gave a point");
	}

	// The instance rules of Engine_TrackInstanceRoute (RUNTIME INSTANCES).
	{
		static struct Instance inst;
		static struct Model model;
		static struct InstDef def;
		static struct Thread thread;

		memset(&inst, 0, sizeof(inst));
		EGT_CHECK(EngTrack_InstanceRule(NULL, 1, 1) == EGT_RULE_RETAIL, "rules: no instance not retail");
		EGT_CHECK(EngTrack_InstanceRule(&inst, 1, 1) == EGT_RULE_RETAIL, "rules: an instance without a model not retail");
		inst.model = &model;
		EGT_CHECK(EngTrack_InstanceRule(&inst, 1, 1) == EGT_RULE_RUNTIME, "rules: no InstDef, no thread, model in the table not runtime");
		EGT_CHECK(EngTrack_InstanceRule(&inst, 0, 1) == EGT_RULE_RETAIL, "rules: a model outside the table not retail");
		EGT_CHECK(EngTrack_InstanceRule(&inst, 1, 0) == EGT_RULE_RETAIL, "rules: a runtime instance on a retail level not retail");
		inst.thread = &thread;
		EGT_CHECK(EngTrack_InstanceRule(&inst, 1, 1) == EGT_RULE_RETAIL, "rules: a runtime instance with a thread not retail");
		inst.instDef = &def;
		EGT_CHECK(EngTrack_InstanceRule(&inst, 1, 1) == EGT_RULE_LEVEL, "rules: an InstDef instance with a thread not a level instance");
		inst.thread = NULL;
		EGT_CHECK(EngTrack_InstanceRule(&inst, 0, 1) == EGT_RULE_LEVEL, "rules: an InstDef instance outside the table not a level instance");
	}

	// (14) ANIMATED ROWS: an entry only for a face whose cell gives more than one blend; the key of
	// the abr a row holds, none for an abr without a key or material (the face keeps its own).
	{
		const int countBefore = s_egtAnimCount;
		u16 entry;

		EGT_CHECK((EngTrack_AnimAppend(3, 0x08u) == 0u) && (EngTrack_AnimAppend(3, 0x00u) == 0u) && (s_egtAnimCount == countBefore),
		          "animated rows: an entry for a face with one blend");
		entry = EngTrack_AnimAppend(3, 0x0au);
		EGT_CHECK((entry == (u16)(countBefore + 1)) && (s_egtAnim[entry - 1u].row == 3) && (s_egtAnim[entry - 1u].abrSet == 0x0au) &&
		              (s_egtAnim[entry - 1u].key[1] == -1) && (s_egtAnim[entry - 1u].material[3] == -1),
		          "animated rows: entry %u", (unsigned)entry);
		if (entry != 0u)
		{
			EngTrackAnimFace *e = &s_egtAnim[entry - 1u];

			e->key[1] = 5;
			e->material[1] = 7;
			e->key[3] = 6;
			EGT_CHECK((EngTrack_AnimKeyOf(e, 1) == 5) && (EngTrack_AnimKeyOf(e, 3) == -1) && (EngTrack_AnimKeyOf(e, 2) == -1) &&
			              (EngTrack_AnimKeyOf(e, -1) == -1) && (EngTrack_AnimKeyOf(e, 4) == -1) && (EngTrack_AnimKeyOf(NULL, 1) == -1),
			          "animated rows: the key of an abr");
		}
		// The selftest runs without a level: the table goes again.
		if (countBefore == 0)
		{
			EngTrack_AnimFree();
		}
	}

	// (oracle) RETAIL ORACLE on synthetic packets, layouts and faces.
	{
		static const u16 tpage = 0x00b5; // page (5, 1) = (320, 256), abr 1, depth 1 (8 bit)
		static const u16 clut = 0x7802;  // (32, 480)
		u32 gt4[13];
		u32 gt3[10];
		EngTrackOracleFace f;
		EngTrackOracleCand cand[3];
		u8 rgb[4][3];
		u16 tp = 0;
		u16 cl = 0;
		int isFace = 0;
		int exact = 0;
		u32 used;
		int uv[8];

		// Decode: a semi-transparent GT4 (code 0x3e) and an opaque GT3 (0x34).
		memset(gt4, 0, sizeof(gt4));
		gt4[0] = 12u << 24;
		gt4[1] = (0x3eu << 24) | (30u << 16) | (20u << 8) | 10u;
		gt4[3] = ((u32)clut << 16) | (2u << 8) | 1u;
		gt4[4] = (60u << 16) | (50u << 8) | 40u;
		gt4[6] = ((u32)tpage << 16) | (4u << 8) | 3u;
		gt4[7] = 70u;
		gt4[9] = (6u << 8) | 5u;
		gt4[10] = 80u;
		gt4[12] = (8u << 8) | 7u;
		memset(&f, 0, sizeof(f));
		used = EngTrack_OracleDecodePrim(gt4, 13u, &f, &tp, &cl, rgb, &isFace);
		EGT_CHECK((used == 13u) && isFace && (tp == tpage) && (cl == clut) && (f.tpX == 320) && (f.tpY == 256) && (f.depth == 1) && (f.clutX == 32) &&
		              (f.clutY == 480) && (f.blend == 1) && (f.uvCount == 4),
		          "oracle decode GT4: used %u face %d tp %d,%d,%d clut %d,%d blend %d", used, isFace, f.tpX, f.tpY, f.depth, f.clutX, f.clutY, f.blend);
		EGT_CHECK((f.uv[0] == 1) && (f.uv[1] == 2) && (f.uv[2] == 3) && (f.uv[3] == 4) && (f.uv[4] == 5) && (f.uv[5] == 6) && (f.uv[6] == 7) &&
		              (f.uv[7] == 8) && (rgb[0][0] == 10) && (rgb[0][2] == 30) && (rgb[1][1] == 50) && (rgb[2][0] == 70) && (rgb[3][0] == 80),
		          "oracle decode GT4: uv or colours wrong");
		memcpy(gt3, gt4, sizeof(gt3));
		gt3[0] = 9u << 24;
		gt3[1] = (0x34u << 24) | 0x010203u;
		used = EngTrack_OracleDecodePrim(gt3, 10u, &f, &tp, &cl, rgb, &isFace);
		EGT_CHECK((used == 10u) && isFace && (f.uvCount == 3) && (f.blend == EGT_ORACLE_BLEND_OPAQUE), "oracle decode GT3: used %u blend %d", used,
		          f.blend);
		gt3[1] = 0x24u << 24;
		used = EngTrack_OracleDecodePrim(gt3, 10u, &f, &tp, &cl, rgb, &isFace);
		EGT_CHECK((used == 10u) && !isFace, "oracle decode: a packet that is no GT3/GT4 not skipped (used %u)", used);
		EGT_CHECK((EngTrack_OracleDecodePrim(gt4, 12u, &f, &tp, &cl, rgb, &isFace) == 0u) && (EngTrack_OracleDecodePrim(gt4, 0u, &f, &tp, &cl, rgb, &isFace) == 0u),
		          "oracle decode: a packet past the range not stopped");

		// Classify: a tile (0..15), level 3 of the same page (0..63), the low layout (another clut).
		memset(cand, 0, sizeof(cand));
		cand[0].lay.tpage = tpage;
		cand[0].lay.clut = clut;
		cand[0].lay.u1 = 15;
		cand[0].lay.v2 = 15;
		cand[0].lay.u3 = 15;
		cand[0].lay.v3 = 15;
		cand[0].k = 2;
		cand[0].level = EGT_ORACLE_LEVEL_TILE;
		cand[0].anim = 1;
		cand[1] = cand[0];
		cand[1].lay.u1 = 63;
		cand[1].lay.v2 = 63;
		cand[1].lay.u3 = 63;
		cand[1].lay.v3 = 63;
		cand[1].level = 3;
		cand[1].anim = 0;
		cand[2] = cand[1];
		cand[2].lay.clut = 0x7803;
		cand[2].k = -1;
		cand[2].level = EGT_ORACLE_LEVEL_LOW;
		uv[0] = 0;
		uv[1] = 0;
		uv[2] = 15;
		uv[3] = 0;
		uv[4] = 0;
		uv[5] = 15;
		EGT_CHECK((EngTrack_OracleClassify(tpage, clut, uv, 3, cand, 3, &exact, -1) == 0) && exact, "oracle classify: tile corners not the tile");
		uv[2] = 8;
		uv[5] = 8;
		EGT_CHECK((EngTrack_OracleClassify(tpage, clut, uv, 3, cand, 3, &exact, -1) == 0) && !exact, "oracle classify: a piece of the tile not part of it");
		uv[2] = 63;
		uv[5] = 63;
		EGT_CHECK((EngTrack_OracleClassify(tpage, clut, uv, 3, cand, 3, &exact, -1) == 1) && exact, "oracle classify: level 3 corners not level 3");
		EGT_CHECK((EngTrack_OracleClassify(tpage, 0x7803, uv, 3, cand, 3, &exact, -1) == 2) && exact, "oracle classify: the low layout not found");
		EGT_CHECK(EngTrack_OracleClassify(tpage, 0x7804, uv, 3, cand, 3, &exact, -1) == -1, "oracle classify: a foreign clut matched");
		EGT_CHECK(EngTrack_OracleClassify((u16)(tpage ^ 0x0020u), clut, uv, 3, cand, 3, &exact, -1) == -1, "oracle classify: another abr matched");
		uv[2] = 64;
		EGT_CHECK(EngTrack_OracleClassify(tpage, clut, uv, 3, cand, 3, &exact, -1) == -1, "oracle classify: a point past the box matched");

		// Where a packet lies: (s, t) in a square and a trapezoid face, the plane distance, the cell.
		{
			static const float square[4][3] = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}, {100, 0, 100}};
			static const float trapezoid[4][3] = {{0, 0, 0}, {100, 0, 0}, {20, 0, 100}, {80, 0, 100}};
			static const float flat[4][3] = {{0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {300, 0, 0}};
			const float p1[3] = {30.0f, 0.0f, 80.0f};
			const float p2[3] = {30.0f, 0.0f, 50.0f};
			const float p3[3] = {30.0f, 10.0f, 50.0f};
			float s = 0.0f;
			float t = 0.0f;
			float off;
			int row = -1;
			int col = -1;

			off = EngTrack_OracleFaceParam(square, p1, &s, &t);
			EGT_CHECK((fabsf(s - 0.3f) < 1e-4f) && (fabsf(t - 0.8f) < 1e-4f) && (off < 1e-4f), "oracle face param (square): s %.4f t %.4f off %.4f", s, t, off);
			off = EngTrack_OracleFaceParam(trapezoid, p2, &s, &t);
			EGT_CHECK((fabsf(s - 0.25f) < 1e-3f) && (fabsf(t - 0.5f) < 1e-3f) && (off < 1e-4f), "oracle face param (trapezoid): s %.4f t %.4f", s, t);
			off = EngTrack_OracleFaceParam(square, p3, &s, &t);
			EGT_CHECK(fabsf(off - 0.1f) < 1e-4f, "oracle face param: plane distance %.4f (expected 0.1)", off);
			EGT_CHECK(EngTrack_OracleFaceParam(flat, p1, &s, &t) < 0.0f, "oracle face param: a degenerate face not refused");
			EngTrack_OracleCellOf(0.3f, 0.8f, 4, &row, &col);
			EGT_CHECK((row == 3) && (col == 1), "oracle cell of (0.3, 0.8): %d,%d", row, col);
			EngTrack_OracleCellOf(1.0f, -0.01f, 4, &row, &col);
			EGT_CHECK((row == 0) && (col == 3), "oracle cell at the border: %d,%d", row, col);
			EngTrack_OracleCellOfForm(0.3f, 0.4f, ENGINE_MOSAIC_FORM_4X2, &row, &col);
			EGT_CHECK((row == 0) && (col == 1), "oracle cell 4x2 upper half: %d,%d", row, col);
			EngTrack_OracleCellOfForm(0.3f, 0.6f, ENGINE_MOSAIC_FORM_4X2, &row, &col);
			EGT_CHECK((row == 2) && (col == 1), "oracle cell 4x2 lower half: %d,%d", row, col);
			EngTrack_OracleCellOfForm(0.9f, 0.9f, ENGINE_MOSAIC_FORM_4X1, &row, &col);
			EGT_CHECK((row == 0) && (col == 3), "oracle cell 4x1: %d,%d", row, col);
		}
		{
			static EngTrackFace a;
			static EngTrackFace b;
			const EngTrackFace *faces[4] = {&a, &b, NULL, NULL};
			const float inB[3] = {150.0f, 0.0f, 50.0f};
			const float away[3] = {500.0f, 0.0f, 0.0f};
			float s = 0.0f;
			float t = 0.0f;
			int corner;

			memset(&a, 0, sizeof(a));
			memset(&b, 0, sizeof(b));
			a.triCount = 2;
			b.triCount = 2;
			for (corner = 0; corner < 4; corner++)
			{
				a.v[corner].position[0] = (corner & 1) ? 100.0f : 0.0f;
				a.v[corner].position[2] = (corner & 2) ? 100.0f : 0.0f;
				b.v[corner].position[0] = (corner & 1) ? 200.0f : 100.0f;
				b.v[corner].position[2] = (corner & 2) ? 100.0f : 0.0f;
			}
			EGT_CHECK((EngTrack_OracleFaceAt(faces, inB, &s, &t) == 1) && (fabsf(s - 0.5f) < 1e-4f) && (fabsf(t - 0.5f) < 1e-4f),
			          "oracle face at: not face 1 (s %.3f t %.3f)", s, t);
			EGT_CHECK(EngTrack_OracleFaceAt(faces, away, &s, &t) == -1, "oracle face at: a point off the block found a face");
		}

		// A face with one triangle (slots 0, 1, 2; slot 3 lies on slot 2): placed by the
		// triangle; twins of a GT4 dropped; packets without area found; a layout shared
		// by a degenerate and a built face names the built one.
		{
			static EngTrackFace tri;
			const EngTrackFace *faces[4] = {NULL, NULL, &tri, NULL};
			const float inTri[3] = {25.0f, 0.0f, 50.0f};
			const float outTri[3] = {90.0f, 0.0f, 90.0f};
			float cornerPos[4][3] = {{0, 0, 0}, {10, 0, 0}, {0, 0, 10}, {10, 0, 0}};
			const float flat[3][3] = {{0, 0, 0}, {5, 0, 0}, {10, 0, 0}};
			u8 twinRgb[4][3] = {{1, 1, 1}, {2, 2, 2}, {3, 3, 3}, {4, 4, 4}};
			EngTrackOracleCand shared[2];
			int corners = 4;
			float s = 0.0f;
			float t = 0.0f;
			int tv;

			memset(&tri, 0, sizeof(tri));
			for (tv = 0; tv < 4; tv++)
			{
				tri.v[tv].position[0] = (tv & 1) ? 100.0f : 0.0f;
				tri.v[tv].position[2] = (tv >= 2) ? 100.0f : 0.0f;
			}
			tri.v[3].position[0] = 0.0f; // slot 3 on slot 2
			tri.triCount = 1;
			tri.tri[0][0] = 0;
			tri.tri[0][1] = 1;
			tri.tri[0][2] = 2;
			EGT_CHECK((EngTrack_OracleFaceAt(faces, inTri, &s, &t) == 2) && (fabsf(s - 0.25f) < 1e-4f) && (fabsf(t - 0.5f) < 1e-4f),
			          "oracle one-triangle face: not found (s %.3f t %.3f)", s, t);
			EGT_CHECK(EngTrack_OracleFaceAt(faces, outTri, &s, &t) == -1, "oracle one-triangle face: a point past its triangle found it");

			memset(&f, 0, sizeof(f));
			f.uvCount = 4;
			for (tv = 0; tv < 8; tv++)
			{
				f.uv[tv] = 10 * tv;
			}
			EGT_CHECK(EngTrack_OracleDropTwin(&f, twinRgb, cornerPos, &corners) && (corners == 3) && (f.uvCount == 3) && (f.uv[4] == 40) && (f.uv[5] == 50) &&
			              (twinRgb[2][0] == 3) && (cornerPos[2][2] == 10.0f),
			          "oracle twin corner: not dropped (corners %d, uv %d,%d)", corners, f.uv[4], f.uv[5]);
			EGT_CHECK(!EngTrack_OracleDropTwin(&f, twinRgb, cornerPos, &corners) && (corners == 3), "oracle twin corner: dropped without a twin");
			EGT_CHECK(EngTrack_OracleZeroArea(flat, 3) && !EngTrack_OracleZeroArea((const float(*)[3])cornerPos, 3), "oracle zero area: wrong");

			memset(shared, 0, sizeof(shared));
			shared[0].lay.tpage = 0x006d;
			shared[0].lay.u1 = 15;
			shared[0].lay.v2 = 15;
			shared[0].lay.u3 = 15;
			shared[0].lay.v3 = 15;
			shared[0].k = 3;
			shared[0].level = 3;
			shared[1] = shared[0];
			shared[1].k = 2;
			uv[0] = 0;
			uv[1] = 0;
			uv[2] = 15;
			uv[3] = 0;
			uv[4] = 0;
			uv[5] = 15;
			EGT_CHECK((EngTrack_OracleClassify(0x006d, 0, uv, 3, shared, 2, &exact, 1 << 2) == 1) && (EngTrack_OracleClassify(0x006d, 0, uv, 3, shared, 2, &exact, -1) == 0),
			          "oracle classify: a shared layout not put on the built face");
			shared[0].level = EGT_ORACLE_LEVEL_TILE;
			shared[0].entry = 7;
			memset(&f, 0, sizeof(f));
			f.uvCount = 3;
			memcpy(f.uv, uv, 6 * sizeof(int));
			f.cellRow = 1;
			EngTrack_OracleSourceMark(&f, 0x006d, 0, shared, 2, 6);
			EGT_CHECK(f.extra[0] == '\0', "oracle src: written for another entry: '%s'", f.extra);
			EngTrack_OracleSourceMark(&f, 0x006d, 0, shared, 2, 7);
			EGT_CHECK(strcmp(f.extra, "src=7") == 0, "oracle src: '%s'", f.extra);
		}

		// The R line: face and cell from the place, stage from the layout, markers; pairs separated by a space.
		f.uvCount = 3;
		f.uv[0] = 0;
		f.uv[1] = 0;
		f.uv[2] = 15;
		f.uv[3] = 0;
		f.uv[4] = 0;
		f.uv[5] = 15;
		rgb[0][0] = 10;
		rgb[0][1] = 20;
		rgb[0][2] = 30;
		EngTrack_OracleRetailLine(&f, 77, 0, 1, 1, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, 3, 0.3f, 0.8f, NULL, -1);
		EGT_CHECK((f.src == 'T') && (strcmp(f.srcId, "77.3") == 0) && (f.cellRow == 3) && (f.cellCol == 1) &&
		              (strncmp(f.extra, "k=3 anim=1 clip=1 sieve=1 prim=normal tp=0x00b5 cl=0x7802 col=10,20,30;", 71) == 0),
		          "oracle R line (tile): src %c id %s cell %d,%d extra '%s'", f.src, f.srcId, f.cellRow, f.cellCol, f.extra);
		EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, -1, 0.0f, 0.0f, NULL, -1);
		EGT_CHECK((f.cellRow == -1) && (strcmp(f.srcId, "77.2") == 0) && (strncmp(f.extra, "k=2 nogeo=1 anim=1 prim=normal", 30) == 0), "oracle R line (tile, no place): '%s'", f.extra);
		f.uv[2] = 63;
		f.uv[5] = 63;
		EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, 2, 0.5f, 0.5f, NULL, -1);
		EGT_CHECK((f.cellRow == -1) && (strncmp(f.extra, "k=2 stage=3 prim=normal", 23) == 0), "oracle R line (level 3): '%s'", f.extra);
		f.uv[2] = 31;
		EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, 2, 0.5f, 0.5f, NULL, -1);
		EGT_CHECK(strncmp(f.extra, "k=2 stage=3 part=1 prim=normal", 30) == 0, "oracle R line (a piece of level 3): '%s'", f.extra);
		EngTrack_OracleRetailLine(&f, 77, 0, 0, EGT_ORACLE_MARK_MORPH, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, 2, 0.5f, 0.5f, NULL, -1);
		EGT_CHECK(strncmp(f.extra, "k=2 stage=3 part=1 morph=1 prim=normal", 38) == 0, "oracle R line (faded midpoints): '%s'", f.extra);
		// A packet of face 2's layout lying on face 1 (no layout of face 1 holds it): face 2's, foreign=1.
		f.uv[2] = 63;
		EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, 1, 0.5f, 0.5f, NULL, -1);
		EGT_CHECK((strcmp(f.srcId, "77.2") == 0) && (strncmp(f.extra, "k=2 stage=3 foreign=1 prim=normal", 33) == 0), "oracle R line (another face's layout): %s '%s'",
		          f.srcId, f.extra);
		// A tile packet of a block the wide frame drew: wide=1.
		f.uv[2] = 15;
		f.uv[5] = 15;
		EngTrack_OracleRetailLine(&f, 77, 0, 0, EGT_ORACLE_MARK_WIDE, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])rgb, cand, 3, 2, 0.3f, 0.8f, NULL, -1);
		EGT_CHECK(strstr(f.extra, "k=2 wide=1 ") != NULL, "oracle R line (wide frame tile): '%s'", f.extra);
		EngTrack_OracleRetailLine(&f, 78, 1, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, 0x7804, (const u8(*)[3])rgb, cand, 3, -1, 0.0f, 0.0f, NULL, -1);
		EGT_CHECK((f.src == 'W') && (strcmp(f.srcId, "78") == 0) && (strncmp(f.extra, "stage=none prim=normal", 22) == 0), "oracle R line (water, no layout): extra '%s'", f.extra);

		// Corners in slot order (FM A9): a GT4 tile packet whose vertices come as slots
		// 3, 0, 2, 1 of cell (3, 1); a GT3 holding slots 2, 1, 3 of the whole face; a
		// corner off every slot leaves the order as it came (slots=-).
		{
			static const float tileST[4][2] = {{0.5f, 1.0f}, {0.25f, 0.75f}, {0.25f, 1.0f}, {0.5f, 0.75f}};
			static const float faceST[3][2] = {{0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
			static const float offST[3][2] = {{0.0f, 1.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}};
			u8 tileRgb[4][3] = {{3, 3, 3}, {0, 0, 0}, {2, 2, 2}, {1, 1, 1}};

			f.uvCount = 4;
			f.uv[0] = 15;
			f.uv[1] = 15;
			f.uv[2] = 0;
			f.uv[3] = 0;
			f.uv[4] = 0;
			f.uv[5] = 15;
			f.uv[6] = 15;
			f.uv[7] = 0;
			EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])tileRgb, cand, 3, 3, 0.375f, 0.875f, tileST, -1);
			EGT_CHECK((f.cellRow == 3) && (f.cellCol == 1) && (f.uv[0] == 0) && (f.uv[1] == 0) && (f.uv[2] == 15) && (f.uv[3] == 0) && (f.uv[4] == 0) &&
			              (f.uv[5] == 15) && (f.uv[6] == 15) && (f.uv[7] == 15) && (strstr(f.extra, "slots=0,1,2,3 col=0,0,0;1,1,1;2,2,2;3,3,3") != NULL),
			          "oracle R line in slot order: uv %d,%d %d,%d %d,%d %d,%d '%s'", f.uv[0], f.uv[1], f.uv[2], f.uv[3], f.uv[4], f.uv[5], f.uv[6], f.uv[7],
			          f.extra);
			f.uvCount = 3;
			f.uv[0] = 0;
			f.uv[1] = 63;
			f.uv[2] = 63;
			f.uv[3] = 0;
			f.uv[4] = 63;
			f.uv[5] = 63;
			EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])tileRgb, cand, 3, 2, 0.5f, 0.5f, faceST, -1);
			EGT_CHECK((f.uv[0] == 63) && (f.uv[1] == 0) && (f.uv[2] == 0) && (f.uv[3] == 63) && (strstr(f.extra, "stage=3 prim=normal") != NULL) &&
			              (strstr(f.extra, "slots=1,2,3 col=0,0,0;3,3,3;2,2,2") != NULL),
			          "oracle GT3 in slot order: '%s'", f.extra);
			f.uv[0] = 0;
			f.uv[1] = 63;
			f.uv[2] = 63;
			f.uv[3] = 0;
			EngTrack_OracleRetailLine(&f, 77, 0, 0, 0, ENGINE_MOSAIC_FORM_4X4, tpage, clut, (const u8(*)[3])tileRgb, cand, 3, 2, 0.5f, 0.5f, offST, -1);
			EGT_CHECK((f.uv[0] == 0) && (f.uv[1] == 63) && (strstr(f.extra, "slots=") == NULL) && (strstr(f.extra, "stage=3 part=1 prim=normal") != NULL),
			          "oracle corner off the slots (a piece whatever its uv): '%s'", f.extra);
		}

		// Static lines (226-resolved layouts): a table entry (stored as the face shows
		// it), the low layout, a face the sieve dropped, a level of a mirrored face
		// (corners in the face's order, as the engine lines: u0<->u1, u2<->u3).
		EngTrack_OracleStaticLine(&f, 5, 1, 2, -1, 0, 7, 1, 3, &cand[0].lay, 1, 1, 0, "load");
		EGT_CHECK((f.src == 'W') && (strcmp(f.srcId, "5.2") == 0) && (f.cellRow == 1) && (f.cellCol == 3) && (f.uvCount == 4) && (f.uv[2] == 15) &&
		              (strcmp(f.extra, "k=2 tp=0x00b5 cl=0x7802 slots=0,1,2,3 mir=1 entry=7 anim=1 set=load") == 0),
		          "oracle static entry line: '%s'", f.extra);
		EngTrack_OracleStaticLine(&f, 5, 0, -1, -1, -1, -1, -1, -1, &cand[2].lay, 0, 0, 0, "load");
		EGT_CHECK((f.src == 'T') && (strcmp(f.srcId, "5") == 0) && (strcmp(f.extra, "tp=0x00b5 cl=0x7803 slots=0,1,2,3 stage=low lodonly=1 set=load") == 0),
		          "oracle static low line: '%s'", f.extra);
		EngTrack_OracleStaticLine(&f, 5, 0, 1, 0, 3, -1, -1, -1, NULL, 0, 1, 1, "anim");
		EGT_CHECK(!f.textured && (strcmp(f.extra, "k=1 stage=0 lodonly=1 anim=1 sieve=1 set=anim") == 0), "oracle static sieve line: '%s'", f.extra);
		{
			struct TextureLayout mirLay;

			memset(&mirLay, 0, sizeof(mirLay));
			mirLay.tpage = 0x006b;
			mirLay.u0 = 80;
			mirLay.v0 = 240;
			mirLay.u1 = 95;
			mirLay.v1 = 240;
			mirLay.u2 = 80;
			mirLay.v2 = 251;
			mirLay.u3 = 95;
			mirLay.v3 = 251;
			EngTrack_OracleStaticLine(&f, 0, 0, 0, 0, 3, -1, -1, -1, &mirLay, 1, 0, 0, "load");
			EGT_CHECK((f.uv[0] == 95) && (f.uv[1] == 240) && (f.uv[2] == 80) && (f.uv[3] == 240) && (f.uv[4] == 95) && (f.uv[5] == 251) && (f.uv[6] == 80) &&
			              (f.uv[7] == 251) && (strstr(f.extra, " slots=0,1,2,3 mir=1 stage=0 lodonly=1") != NULL),
			          "oracle static mirrored level: uv %d,%d,%d,%d,%d,%d,%d,%d '%s'", f.uv[0], f.uv[1], f.uv[2], f.uv[3], f.uv[4], f.uv[5], f.uv[6], f.uv[7],
			          f.extra);
			EngTrack_OracleStaticLine(&f, 0, 0, 0, 0, 3, -1, -1, -1, &mirLay, 0, 0, 0, "load");
			EGT_CHECK(strstr(f.extra, "blank=1") == NULL, "oracle static line: a layout marked blank");
			EGT_CHECK((f.uv[0] == 80) && (f.uv[2] == 95) && (strstr(f.extra, "mir") == NULL), "oracle static level not mirrored: '%s'", f.extra);
			memset(&mirLay, 0, sizeof(mirLay));
			EngTrack_OracleStaticLine(&f, 0, 0, 0, 0, 3, -1, -1, -1, &mirLay, 0, 0, 0, "load");
			EGT_CHECK(strstr(f.extra, "blank=1") != NULL, "oracle static line: an all-zero layout not marked blank: '%s'", f.extra);
		}

		// The tag of a load block: the tour entry while it drives, else the load number.
		{
			char tag[48];

			EGT_CHECK(EngTrack_OracleLoadTag(tag, sizeof(tag), 0, 1, 23, 69, 5) && (strcmp(tag, "t23-L69-load") == 0), "oracle load tag (tour): '%s'", tag);
			EGT_CHECK(EngTrack_OracleLoadTag(tag, sizeof(tag), 3, 0, 23, 69, 5) && (strcmp(tag, "L3-load5") == 0), "oracle load tag (no tour): '%s'", tag);
			EGT_CHECK(!EngTrack_OracleLoadTag(tag, 4, 3, 0, 0, 0, 5), "oracle load tag: a short buffer not refused");
		}

		// The walk's grid: valid when its four corners are the level positions; the faces then sit on it.
		{
			static struct QuadBlock qb;
			static struct LevVertex lvs[9];
			EngTrackFace copies[4];
			const EngTrackFace *faces[4];
			s16 grid[27];
			int gv;

			memset(&qb, 0, sizeof(qb));
			memset(lvs, 0, sizeof(lvs));
			for (gv = 0; gv < 9; gv++)
			{
				qb.index[gv] = (u16)gv;
				lvs[gv].pos.x = (s16)(10 * gv);
				lvs[gv].pos.y = (s16)(gv + 1);
				lvs[gv].pos.z = (s16)(-5 * gv);
				grid[3 * gv] = lvs[gv].pos.x;
				grid[3 * gv + 1] = lvs[gv].pos.y;
				grid[3 * gv + 2] = lvs[gv].pos.z;
			}
			grid[3 * 6] = 123; // a midpoint moved by the walk: still valid
			EGT_CHECK(EngTrack_OracleGridValid(&qb, lvs, 9, grid), "oracle walk grid: a moved midpoint made it invalid");
			grid[3 * 2] = 7; // a corner that is not the level's: not the block's grid
			EGT_CHECK(!EngTrack_OracleGridValid(&qb, lvs, 9, grid) && !EngTrack_OracleGridValid(&qb, lvs, 2, grid), "oracle walk grid: a wrong corner accepted");
			grid[3 * 2] = lvs[2].pos.x;
			{
				static EngTrackDrawFace drawFaces[1];
				static int faceOfBlock[4] = {0, -1, -1, -1};
				EngTrackDrawFace *const keepFaces = s_egtFaces;
				int *const keepIndex = s_egtFaceOfBlock;

				memset(drawFaces, 0, sizeof(drawFaces));
				drawFaces[0].face.grid[0] = 0;
				drawFaces[0].face.grid[1] = 4;
				drawFaces[0].face.grid[2] = 5;
				drawFaces[0].face.grid[3] = 6;
				s_egtFaces = drawFaces;
				s_egtFaceOfBlock = faceOfBlock;
				EngTrack_OracleBlockFaces(0, grid, copies, faces);
				EGT_CHECK((faces[0] == &copies[0]) && (faces[1] == NULL) && (copies[0].v[3].position[0] == 123.0f) && (copies[0].v[1].position[2] == -20.0f) &&
				              (drawFaces[0].face.v[3].position[0] == 0.0f),
				          "oracle block faces: corners not moved onto the walk grid (or the face itself changed)");
				EngTrack_OracleBlockFaces(0, NULL, copies, faces);
				EGT_CHECK(faces[0] == &drawFaces[0].face, "oracle block faces: without a grid not the face itself");
				s_egtFaces = keepFaces;
				s_egtFaceOfBlock = keepIndex;
			}
		}

		// Why a static line has no engine face: the marks of the load.
		{
			static u8 marks[2] = {EGT_BLOCK_IN_LEAF, 0};
			static int faceOf[8] = {-1, -1, -1, 0, -1, -1, -1, -1};
			static u8 why[8] = {EGT_FACE_NO_LAYOUT, EGT_FACE_DEGENERATE, EGT_FACE_DROPPED, EGT_FACE_BUILT, EGT_FACE_SKIPPED, 0, 0, 0};
			u8 *const keepMarks = s_egtBlockMark;
			int *const keepFaceOf = s_egtFaceOfBlock;
			u8 *const keepWhy = s_egtFaceWhy;
			const char *want[4] = {"nolayout=1", "degenerate=1", "dropped=1", ""};
			int mk;

			s_egtBlockMark = marks;
			s_egtFaceOfBlock = faceOf;
			s_egtFaceWhy = why;
			for (mk = 0; mk < 4; mk++)
			{
				memset(&f, 0, sizeof(f));
				EngTrack_OracleFaceMarks(&f, 0, mk);
				EGT_CHECK(strcmp(f.extra, want[mk]) == 0, "oracle face marks k=%d: '%s'", mk, f.extra);
			}
			memset(&f, 0, sizeof(f));
			EngTrack_OracleFaceMarks(&f, 1, 0);
			EGT_CHECK(strcmp(f.extra, "noleaf=1") == 0, "oracle face marks (no leaf): '%s'", f.extra);
			s_ego.set = "anim";
			memset(&f, 0, sizeof(f));
			EngTrack_OracleFaceMarks(&f, 0, 1);
			EGT_CHECK(f.extra[0] == '\0', "oracle face marks in the AnimTex set: '%s'", f.extra);
			s_ego.set = NULL;
			s_egtBlockMark = keepMarks;
			s_egtFaceOfBlock = keepFaceOf;
			s_egtFaceWhy = keepWhy;
		}

		// Engine cell colours as the walk averages them: midpoints (a + b) >> 1, the centre from slots 1 and 2.
		{
			static const u8 corner[4][3] = {{0, 0, 0}, {200, 100, 0}, {100, 50, 10}, {255, 255, 255}};
			u8 g[5][5][3];

			EngTrack_OracleCellColours(corner, ENGINE_MOSAIC_FORM_4X4, 0, g);
			EGT_CHECK((g[0][2][0] == 100) && (g[2][0][0] == 50) && (g[2][2][0] == 150) && (g[2][2][1] == 75) && (g[0][1][0] == 50) && (g[1][1][0] == 75) &&
			              (g[4][4][2] == 255),
			          "oracle cell colours 4x4: %d %d %d %d %d %d", g[0][2][0], g[2][0][0], g[2][2][0], g[2][2][1], g[0][1][0], g[1][1][0]);
			EngTrack_OracleCellColours(corner, ENGINE_MOSAIC_FORM_4X1, 0, g);
			EGT_CHECK((g[0][2][0] == 100) && (g[4][2][0] == 177) && (g[0][1][0] == 50) && (g[4][3][0] == 216) && (g[2][2][0] == 0),
			          "oracle cell colours 4x1: %d %d %d %d", g[0][2][0], g[4][2][0], g[0][1][0], g[4][3][0]);
		}
		{
			// The wide frame (centre from the edge midpoints), against retail tiles of t02-L1
			// block 258 face 2 (n1): corners and inner grid points as the walk drew them.
			static const u8 corner[4][3] = {{203, 236, 255}, {115, 140, 173}, {188, 220, 255}, {156, 189, 222}};
			static const int at[4][2] = {{1, 1}, {2, 1}, {2, 2}, {3, 3}};
			static const u8 want[4][3] = {{180, 212, 237}, {180, 212, 240}, {165, 196, 226}, {157, 188, 220}};
			u8 g[5][5][3];
			int i;
			int bad = 0;

			EngTrack_OracleCellColours(corner, ENGINE_MOSAIC_FORM_4X4, 1, g);
			for (i = 0; i < 4; i++)
			{
				bad += memcmp(g[at[i][0]][at[i][1]], want[i], 3) != 0;
			}
			EGT_CHECK(bad == 0, "oracle cell colours, wide frame: %d of 4 retail grid points differ (1,1 = %d,%d,%d)", bad, g[1][1][0], g[1][1][1], g[1][1][2]);
		}

		{
			int row = -1;
			int col = -1;
			int bad = 0;

			EngTrack_OracleEntryCell(ENGINE_MOSAIC_FORM_4X2, 5, &row, &col);
			bad += (row != 2) || (col != 1);
			EngTrack_OracleEntryCell(ENGINE_MOSAIC_FORM_4X1, 3, &row, &col);
			bad += (row != 0) || (col != 3);
			EngTrack_OracleEntryCell(ENGINE_MOSAIC_FORM_4X4, 6, &row, &col);
			bad += (row != 1) || (col != 2);
			EGT_CHECK(bad == 0, "oracle entry cells: %d wrong", bad);
		}

		// The extra never overflows: a pair that does not fit is left out.
		memset(&f, 0, sizeof(f));
		for (isFace = 0; isFace < 40; isFace++)
		{
			EngTrack_OracleExtra(&f, "key%d=0123456789", isFace);
		}
		EGT_CHECK(strlen(f.extra) < sizeof(f.extra), "oracle extra: overflow");
	}

	// (oracle) E lines: a VRAM face (unclamped box), the shared part, a table row's layouts.
	{
		static EngTrackDrawFace df;
		static EngTrackOracleFace lines[19];
		static struct LevVertex verts[4];
		struct TextureLayout cells[16];
		struct TextureLayout lv[3];
		EngTrackKey key;
		EngTrackOracleFace base;
		u8 rgb[4][3];
		int which[19];
		int count;
		int i;

		memset(&df, 0, sizeof(df));
		memset(&key, 0, sizeof(key));
		df.block = 12;
		df.face.mid = 3;
		df.face.vertCount = 3;
		df.face.v[0].texcoord[0] = 10.0f;
		df.face.v[0].texcoord[1] = 20.0f;
		df.face.v[1].texcoord[0] = 30.0f;
		df.face.v[1].texcoord[1] = 20.0f;
		df.face.v[2].texcoord[0] = 10.0f;
		df.face.v[2].texcoord[1] = 40.0f;
		Engine_VertexSetBox(df.face.v, 4, 10, 20, 30, 40); // the face's UV box, as EngTrack_BuildBlock writes it
		df.face.v[0].color[0] = 1;
		df.face.v[1].color[0] = 2;
		df.face.v[2].color[0] = 3;
		df.anim = 1;
		df.stage = 3;
		key.tpage = 0x0015; // abr 0: semi
		key.clut = 0x7802;
		EngTrack_OracleEngineBase(&base, &df, &key, NULL, NULL, rgb);
		EGT_CHECK((base.src == 'T') && (strcmp(base.srcId, "12.3") == 0) && (base.sub == 3) && (base.blend == 0) &&
		              (strcmp(base.extra, "k=3 anim=1 frozen=1") == 0) && (rgb[0][0] == 1) && (rgb[2][0] == 3),
		          "oracle E base: '%s' blend %d", base.extra, base.blend);
		df.live = 1;
		df.vertex[0] = 2;
		verts[2].color_hi[0] = 99;
		key.tpage = 0x0075; // abr 3: opaque
		key.lift = 5;
		EngTrack_OracleEngineBase(&base, &df, &key, verts, "load", rgb);
		EGT_CHECK((base.blend == EGT_ORACLE_BLEND_OPAQUE) && (strstr(base.extra, "decal=5 live=1 set=load") != NULL) && (rgb[0][0] == 99),
		          "oracle E base (live, decal, set): '%s'", base.extra);
		lines[0] = base;
		EngTrack_OracleEngineVram(&lines[0], key.tpage, key.clut, &df.face);
		EGT_CHECK((lines[0].uvCount == 3) && (lines[0].uv[2] == 30) && (lines[0].uv[5] == 40) &&
		              (strstr(lines[0].extra, " tp=0x0075 cl=0x7802 box=10,20,30,40 slots=0,1,2") != NULL),
		          "oracle E VRAM: '%s'", lines[0].extra);

		// The cells a row's form holds, then levels 2, 1, 0.
		count = EngTrack_OracleRowLayouts(4, ENGINE_MOSAIC_FORM_4X2, which);
		EGT_CHECK((count == 11) && (which[3] == 3) && (which[4] == 8) && (which[7] == 11) && (which[8] == 18) && (which[10] == 16),
		          "oracle row layouts 4x2: %d (which[4] %d)", count, which[4]);
		EGT_CHECK((EngTrack_OracleRowLayouts(4, ENGINE_MOSAIC_FORM_4X4, which) == 19) && (EngTrack_OracleRowLayouts(4, ENGINE_MOSAIC_FORM_4X1, which) == 7) &&
		              (EngTrack_OracleRowLayouts(1, ENGINE_MOSAIC_FORM_4X4, which) == 4),
		          "oracle row layouts: 4x4 / 4x1 / whole-face counts wrong");
		memset(cells, 0, sizeof(cells));
		memset(lv, 0, sizeof(lv));
		for (i = 0; i < 16; i++)
		{
			const int e = 4 * ((i >> 2) >> 1) + (i & 3);

			cells[i].tpage = 0x0075;
			cells[i].u0 = (u8)(16 * e);
			cells[i].u1 = (u8)(16 * e + 15);
			cells[i].u2 = (u8)(16 * e);
			cells[i].v2 = 15;
			cells[i].u3 = (u8)(16 * e + 15);
			cells[i].v3 = 15;
		}
		lv[1].u0 = 100;
		lv[1].u3 = 120;
		lv[1].v3 = 7;
		memset(rgb, 0, sizeof(rgb));
		rgb[1][0] = 200;
		rgb[3][0] = 100;
		count = EngTrack_OracleTableLines(&base, 4, ENGINE_MOSAIC_FORM_4X2, 3, 1, cells, lv, (const u8(*)[3])rgb, lines);
		EGT_CHECK((count == 11) && (lines[1].cellRow == 0) && (lines[1].cellCol == 1) &&
		              (strstr(lines[1].extra, " box=16,0,31,15 slots=0,1,2,3 fault=cell col=50,0,0;100,0,0;50,0,0;100,0,0") != NULL),
		          "oracle table lines: n %d cell %d,%d '%s'", count, lines[1].cellRow, lines[1].cellCol, lines[1].extra);
		EGT_CHECK((lines[4].cellRow == 2) && (lines[4].cellCol == 0) && (lines[9].cellRow == -1) &&
		              (strstr(lines[9].extra, " box=0,0,120,7 slots=0,1,2,3 stage=1 col=0,0,0;200,0,0;0,0,0;100,0,0") != NULL),
		          "oracle table lines: second row half at %d,%d, level line '%s'", lines[4].cellRow, lines[4].cellCol, lines[9].extra);
		count = EngTrack_OracleTableLines(&base, 1, ENGINE_MOSAIC_FORM_4X4, 2, 0, cells, lv, (const u8(*)[3])rgb, lines);
		EGT_CHECK((count == 4) && (lines[0].cellRow == -1) && (strstr(lines[0].extra, " stage=2 col=0,0,0;200,0,0;0,0,0;100,0,0") != NULL) &&
		              (strstr(lines[0].extra, "fault") == NULL),
		          "oracle whole-face lines: '%s'", lines[0].extra);
	}

	// (oracle) Without a logged frame every entry point is a no-op (done point D-W1.10).
	{
		const s32 sum[3] = {1, 2, 3};
		const s16 cornerPositions[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
		struct TextureLayout spare[2];

		memset(spare, 0, sizeof(spare));
		memset(&s_ego, 0, sizeof(s_ego));
		Engine_TrackOracleNotePrim(&spare[0], cornerPositions, 3, 5);
		Engine_TrackOracleBlock(NULL, 0, 0, NULL, &spare[0], &spare[1], NULL, NULL);
		Engine_TrackOracleClip(&spare[0], sum, 3, &spare[0], &spare[1]);
		Engine_TrackOracleDone(1, &spare[0], &spare[1]);
		Engine_TrackOracleLayout(NULL, 0, 0, 3, &spare[0], 1, 0, 0);
		Engine_TrackOracleTable(NULL, 0, &spare[0], 0, 1);
		EGT_CHECK(!Engine_TrackOracleWanted() && !s_ego.active && (s_ego.geoCount == 0) && (s_ego.blocks == 0) && (s_ego.staticLines == 0) &&
		              (s_ego.noBlock == 0),
		          "oracle: an entry point acted outside a logged frame");
	}

	return failures;
}
