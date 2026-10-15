#include <platform/engine.h>
#include <math.h>
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
// stays. A pointer with bit 0 set is an AnimTex indirection (*(ptr - 1)) which
// is resolved ONCE at load: animated textures freeze (documented limit).
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

			// A mosaic face the table takes: keyed by the table's tpage word, clut 0. So is an
			// animated face (no mosaic): a whole-face row read from its AnimTex cell (pointer - 1) every frame.
			if ((mos[f->mid] && Engine_TrackMosaicFace(groups[f->mid], f->mirrored, form[i], f->slotOf, f->v, f->vertCount, &keyTpage)) ||
			    (!mos[f->mid] && anim[f->mid] &&
			     Engine_TrackMosaicAnimFace((u32)(size_t)qb->ptr_texture_mid[f->mid] - 1u, f->mirrored, f->slotOf, f->v, f->vertCount, &keyTpage)))
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
	s_egtVertices = mesh->ptrVertexArray;
	s_egtLevel = level;
	s_egtMesh = mesh;
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
		return 0;
	}
	s_egtPeakFaces = (faces > s_egtPeakFaces) ? faces : s_egtPeakFaces;
	s_egtPeakVertices = (vertices > s_egtPeakVertices) ? vertices : s_egtPeakVertices;
	s_egtPeakExpanded = (expanded > s_egtPeakExpanded) ? expanded : s_egtPeakExpanded;
	s_egtPeakSpecial = (specials > s_egtPeakSpecial) ? specials : s_egtPeakSpecial;

	for (i = 0; i < s_egtGroupCount; i++)
	{
		const EngTrackDrawGroup *g = &s_egtGroups[i];
		const int cell = (g->semi || (g->lift != 0)) ? EGT_CELL_SEMI : EGT_CELL_OPAQUE;
		int next = 0;

		// Usually one draw per group; more only when its shown faces pass the u16 index range of a draw.
		while (next < g->count)
		{
			EngineDraw draw;
			u32 indexCount = 0;
			const int from = next;
			const u32 vertexCount = EngTrack_FillFacesFrom(&s_egtFaces[g->first], &s_egtShown[g->first], from, g->count, s_egtVertices, s_egtScratchV,
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
// model has an entry in the model table; containerLevel: the level came from a
// track container (level id NATIVE_TRACK_LEVELID_FIRST and up). Runtime
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
	if (Engine_InstanceSubmit(inst, idpp, pb, &d) > 0)
	{
		em->drawn++;
		s_egtInstDrawn++;
		EngTrack_PositionCheck(em, inst, idpp, pb, d.mesh, d.frame, runtime);
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
	switch (EngTrack_InstanceRule(inst, em != NULL, s_egtLevelID >= NATIVE_TRACK_LEVELID_FIRST))
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

	return failures;
}
