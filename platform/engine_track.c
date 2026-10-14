#include <platform/engine.h>
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
// KNOWN LIMITS: one detail level (the finest), no visibility culling (everything
// not water, in a leaf), hidden faces of track mods (platform frames) are drawn,
// PS1 objects have no depth test and draw on top of the engine track.

#define EGT_MESH_VERTICES_MAX 65536u // u16 indices
#define EGT_MESHES_MAX 48
#define EGT_KEYS_MAX 4096
#define EGT_MODELS_MAX 128
#define EGT_CELL_OPAQUE 0x3fe // above every cell a track face uses (<= 1020) and below the clear/glow/sky cell 0x3ff
#define EGT_CELL_SEMI 0x3fd   // after the opaque meshes: a lower cell is drawn later
#define EGT_BUCKET_WATER_RENDERED 8
#define EGT_BUCKET_WATER_LIST 9
#define EGT_BUCKET_COUNT 11
// Live faces (scenery vertex animation) per frame, as expanded vertices (3 per
// triangle): a third of the renderer's dynamic store (ENGINE_DYN_EXPANDED,
// engine_render.c), the rest stays for the effects. Above it a face stays static.
#define EGT_LIVE_EXPANDED_MAX 32768u

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
} EngTrackKey;

typedef struct
{
	EngTrackKey key[EGT_KEYS_MAX];
	int count;
} EngTrackKeySet;

typedef struct
{
	EngineVertex *vertices;
	u32 vertexCount;
	u16 *indices;
	u32 indexCount;
	EngineRange *ranges;
	u32 rangeCount;
	u32 vertexCap;
	u32 indexCap;
	u32 rangeCap;
} EngTrackMeshBuild;

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

internal int EngTrack_KeysFind(EngTrackKeySet *set, u16 tpage, u16 clut, int doubleSided, int bias)
{
	int i;

	for (i = 0; i < set->count; i++)
	{
		const EngTrackKey *k = &set->key[i];

		if ((k->tpage == tpage) && (k->clut == clut) && (k->doubleSided == (u8)(doubleSided ? 1 : 0)) && (k->bias == (s8)bias))
		{
			return i;
		}
	}
	if (set->count >= EGT_KEYS_MAX)
	{
		return -1;
	}
	set->key[set->count].tpage = tpage;
	set->key[set->count].clut = clut;
	set->key[set->count].doubleSided = (u8)(doubleSided ? 1 : 0);
	set->key[set->count].bias = (s8)bias;
	return set->count++;
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

internal int EngTrack_BuildEnsure(EngTrackMeshBuild *b, u32 vertices, u32 indices, u32 ranges)
{
	if (b->vertexCount + vertices > b->vertexCap)
	{
		u32 cap = (b->vertexCap == 0u) ? 1024u : b->vertexCap;
		EngineVertex *p;

		while (cap < b->vertexCount + vertices)
		{
			cap *= 2u;
		}
		if (cap > EGT_MESH_VERTICES_MAX)
		{
			cap = EGT_MESH_VERTICES_MAX;
		}
		p = (EngineVertex *)realloc(b->vertices, (size_t)cap * sizeof(EngineVertex));
		if (p == NULL)
		{
			return 0;
		}
		b->vertices = p;
		b->vertexCap = cap;
	}
	if (b->indexCount + indices > b->indexCap)
	{
		u32 cap = (b->indexCap == 0u) ? 3072u : b->indexCap;
		u16 *p;

		while (cap < b->indexCount + indices)
		{
			cap *= 2u;
		}
		p = (u16 *)realloc(b->indices, (size_t)cap * sizeof(u16));
		if (p == NULL)
		{
			return 0;
		}
		b->indices = p;
		b->indexCap = cap;
	}
	if (b->rangeCount + ranges > b->rangeCap)
	{
		const u32 cap = (b->rangeCap == 0u) ? 16u : (b->rangeCap * 2u);
		EngineRange *p = (EngineRange *)realloc(b->ranges, (size_t)cap * sizeof(EngineRange));

		if (p == NULL)
		{
			return 0;
		}
		b->ranges = p;
		b->rangeCap = cap;
	}
	return 1;
}

internal void EngTrack_BuildFree(EngTrackMeshBuild *b)
{
	free(b->vertices);
	free(b->indices);
	free(b->ranges);
	memset(b, 0, sizeof(*b));
}

// The faces whose key is semi (semi = 1) or opaque (semi = 0) into meshes of at
// most maxVertices vertices, grouped by key (one range per key per mesh, the key's
// material from matOfKey; a key with a negative material is skipped). Writes up
// to maxBuilds builds into builds[] (zeroed by the caller) and returns their
// count. *dropped counts faces left out because maxBuilds was reached or memory ran out.
internal int EngTrack_Assemble(const EngTrackFace *faces, int faceCount, const EngTrackKeySet *keys, const int *matOfKey, int semi, u32 maxVertices,
                               EngTrackMeshBuild *builds, int maxBuilds, int *dropped)
{
	int *order = NULL;
	int *start = NULL;
	int *fill = NULL;
	int buildCount = 0;
	int k;
	int i;

	if (maxVertices > EGT_MESH_VERTICES_MAX)
	{
		maxVertices = EGT_MESH_VERTICES_MAX;
	}
	order = (int *)malloc(((size_t)faceCount + 1u) * sizeof(int));
	start = (int *)calloc((size_t)keys->count + 2u, sizeof(int));
	fill = (int *)calloc((size_t)keys->count + 2u, sizeof(int));
	if ((order == NULL) || (start == NULL) || (fill == NULL))
	{
		free(order);
		free(start);
		free(fill);
		if (dropped != NULL)
		{
			*dropped += faceCount;
		}
		return 0;
	}
	// Counting sort of the faces by key (stable).
	for (i = 0; i < faceCount; i++)
	{
		start[faces[i].key + 1]++;
	}
	for (k = 0; k < keys->count; k++)
	{
		start[k + 1] += start[k];
	}
	memcpy(fill, start, ((size_t)keys->count + 1u) * sizeof(int));
	for (i = 0; i < faceCount; i++)
	{
		order[fill[faces[i].key]++] = i;
	}
	free(fill);

	for (k = 0; k < keys->count; k++)
	{
		int n;

		if ((matOfKey[k] < 0) || (EngTrack_IsSemi(keys->key[k].tpage) != semi))
		{
			continue;
		}
		for (n = start[k]; n < start[k + 1]; n++)
		{
			const EngTrackFace *f = &faces[order[n]];
			EngTrackMeshBuild *b = (buildCount > 0) ? &builds[buildCount - 1] : NULL;
			u32 base;
			int t;
			int c;

			if ((b == NULL) || (b->vertexCount + f->vertCount > maxVertices))
			{
				if (buildCount >= maxBuilds)
				{
					if (dropped != NULL)
					{
						(*dropped)++;
					}
					continue;
				}
				b = &builds[buildCount++];
			}
			if (!EngTrack_BuildEnsure(b, f->vertCount, (u32)f->triCount * 3u, 1u))
			{
				if (dropped != NULL)
				{
					(*dropped)++;
				}
				continue;
			}
			// A new range at the start of the mesh and whenever the material changes.
			if ((b->rangeCount == 0u) || (b->ranges[b->rangeCount - 1u].material != (u16)matOfKey[k]))
			{
				EngineRange *r = &b->ranges[b->rangeCount++];

				r->firstIndex = b->indexCount;
				r->indexCount = 0;
				r->material = (u16)matOfKey[k];
			}
			base = b->vertexCount;
			for (c = 0; c < f->vertCount; c++)
			{
				b->vertices[b->vertexCount++] = f->v[c];
			}
			for (t = 0; t < f->triCount; t++)
			{
				b->indices[b->indexCount++] = (u16)(base + f->tri[t][0]);
				b->indices[b->indexCount++] = (u16)(base + f->tri[t][1]);
				b->indices[b->indexCount++] = (u16)(base + f->tri[t][2]);
			}
			b->ranges[b->rangeCount - 1u].indexCount += (u32)f->triCount * 3u;
		}
	}
	free(order);
	free(start);
	return buildCount;
}

// LIVE FACES. Where the game moves level vertices itself every frame - the
// scenery vertex animation of a level with configFlags & 4 (AnimateQuad,
// game/RenderLevel/AnimateWater.c, writes position and colour of the SCVert
// vertices in place; MainFrame_RenderFrame.c 1P path) - a face touching such a
// vertex is not baked into the static meshes: it is kept with the level vertex of
// each of its slot vertices and drawn every frame as dynamic geometry from what
// the level vertex holds now. Read only: the game writes, the engine reads.
typedef struct
{
	EngTrackFace face; // texture coordinates, triangles, key as built
	u16 vertex[4];     // the level vertex (mesh->ptrVertexArray index) of each slot vertex
	int material;
	int semi;
} EngTrackLiveFace;

// Live faces of one material, drawn as one dynamic draw.
typedef struct
{
	int material;
	int semi;
	int first;
	int count;
} EngTrackLiveGroup;

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

// The vertices and indices of count live faces with the level vertices as they
// are now: position and colour from verts, texture coordinates as built.
// Returns the vertex count; *indexCount gets the index count. outV needs 4 and
// outI 6 entries per face.
internal u32 EngTrack_LiveFill(const EngTrackLiveFace *faces, int count, const struct LevVertex *verts, EngineVertex *outV, u16 *outI, u32 *indexCount)
{
	u32 vertexCount = 0;
	u32 indices = 0;
	int i;

	for (i = 0; i < count; i++)
	{
		const EngTrackFace *f = &faces[i].face;
		int c;
		int t;

		for (c = 0; c < f->vertCount; c++)
		{
			const struct LevVertex *lv = &verts[faces[i].vertex[c]];
			EngineVertex *ev = &outV[vertexCount + (u32)c];

			*ev = f->v[c];
			ev->position[0] = (float)lv->pos.x;
			ev->position[1] = (float)lv->pos.y;
			ev->position[2] = (float)lv->pos.z;
			ev->color[0] = lv->color_hi[0];
			ev->color[1] = lv->color_hi[1];
			ev->color[2] = lv->color_hi[2];
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
	return vertexCount;
}

// --------------------------------------------------------------------------
// Game side

typedef struct
{
	int mesh;
	int semi;
	u32 vertices; // verticesPerFrame at load, to recognise the mesh at draw time
} EngTrackMesh;

typedef struct
{
	const struct Model *model;
	char name[17];
	s16 id;
	EngineModelImport imp;
	int imported;
	int transparent;
	u32 drawn;
	u32 declined;
	char why[96]; // Engine_ModelImportWhy of a failed import
} EngTrackModel;

global_variable EngTrackKeySet s_egtKeys;
global_variable EngTrackMesh s_egtMeshes[EGT_MESHES_MAX];
global_variable int s_egtMeshCount;
global_variable const struct Level *s_egtLevel;
global_variable EngTrackModel s_egtModels[EGT_MODELS_MAX];
global_variable int s_egtModelCount;
global_variable int s_egtSubmitted; // this frame the engine track was submitted
global_variable int s_egtReportRegistered;
global_variable u64 s_egtFramesDrawn;
global_variable u64 s_egtBucketsSkipped;
global_variable u64 s_egtWaterVisits;
global_variable u64 s_egtInstDrawn;
global_variable u64 s_egtInstDeclined;
// Live faces of the loaded level (made at load, drawn every frame), grouped by material.
global_variable EngTrackLiveFace *s_egtLive;
global_variable int s_egtLiveCount;
global_variable int s_egtLiveCap;
global_variable EngTrackLiveGroup *s_egtLiveGroups;
global_variable int s_egtLiveGroupCount;
global_variable const struct LevVertex *s_egtLiveVertices; // the level's vertex array the live faces read
global_variable EngineVertex *s_egtLiveScratchV;           // one group's vertices, rebuilt per draw
global_variable u16 *s_egtLiveScratchI;
global_variable u64 s_egtLiveDrawn;

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
// Race frames of the loaded level that Engine_TrackCollect saw (drawn or not).
global_variable u64 s_egtCollectFrames;

// What the last Engine_TrackLoad built, for the tour's per-track line.
global_variable struct
{
	int levelID;
	int faces;
	int meshes;
	int materials;
	int dropped;
	int liveFaces;
	int frozenFaces;
	int modelsImported;
	int modelsFailed;
} s_egtLoad = {-1, 0, 0, 0, 0, 0, 0, 0, 0};

// The counters at the first race frame of a tour entry (EngTrack_TourReport phase 0).
global_variable struct
{
	u64 framesDrawn;
	u64 collectFrames;
	u64 bucketsSkipped;
	u64 instDrawn;
	u64 instDeclined;
	u64 instDeclinedBy[EGT_DECLINE_COUNT];
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

		Platform_Log("[CTR Engine] tour %d level %d track: loaded for level %d, meshes %d, materials %d, faces %d, dropped %d, live faces %d (frozen %d); "
		             "models %d imported, %d failed; frames drawn %llu of %llu, PS1 level buckets skipped %llu\n",
		             entry, levelID, s_egtLoad.levelID, s_egtLoad.meshes, s_egtLoad.materials, s_egtLoad.faces, s_egtLoad.dropped, s_egtLoad.liveFaces,
		             s_egtLoad.frozenFaces, s_egtLoad.modelsImported, s_egtLoad.modelsFailed,
		             (unsigned long long)(s_egtFramesDrawn - s_egtTourBase.framesDrawn), (unsigned long long)(s_egtCollectFrames - s_egtTourBase.collectFrames),
		             (unsigned long long)(s_egtBucketsSkipped - s_egtTourBase.bucketsSkipped));
		Platform_Log("[CTR Engine] tour %d level %d instances: drawn %llu, declined %llu (model not imported %llu, flags %llu, frame %llu, submit %llu)\n",
		             entry, levelID, (unsigned long long)(s_egtInstDrawn - s_egtTourBase.instDrawn),
		             (unsigned long long)(s_egtInstDeclined - s_egtTourBase.instDeclined),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_MODEL] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_MODEL]),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_FLAGS] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_FLAGS]),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_FRAME] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_FRAME]),
		             (unsigned long long)(s_egtInstDeclinedBy[EGT_DECLINE_SUBMIT] - s_egtTourBase.instDeclinedBy[EGT_DECLINE_SUBMIT]));
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
	}
}

// Whether the track meshes made at load are still the live meshes (the mesh
// tables are reset at level unload and slots are reused).
internal int EngTrack_MeshesLive(void)
{
	int i;

	if (s_egtMeshCount <= 0)
	{
		return 0;
	}
	for (i = 0; i < s_egtMeshCount; i++)
	{
		const EngineMeshDesc *d = Engine_MeshDesc(s_egtMeshes[i].mesh);
		u32 vb = 0xffffffffu;
		u32 ib = 0xffffffffu;

		if ((d == NULL) || (d->name == NULL) || (strncmp(d->name, "track ", 6) != 0) || (d->verticesPerFrame != s_egtMeshes[i].vertices))
		{
			return 0;
		}
		Engine_MeshGetGpu(s_egtMeshes[i].mesh, &vb, &ib);
		if ((vb == 0xffffffffu) || (ib == 0xffffffffu))
		{
			return 0;
		}
	}
	return 1;
}

internal int EngTrack_Active(const struct GameTracker *gGT)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->numPlyrCurrGame != 1))
	{
		return 0;
	}
	if (((gGT->gameMode1 & MAIN_MENU) != 0) || (gGT->level1 == NULL) || (gGT->level1 != s_egtLevel))
	{
		return 0;
	}
	return EngTrack_MeshesLive();
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
// blocks that sit in a leaf), and the mosaic form of every block in a leaf.
// Returns the number of leaves seen.
internal int EngTrack_MarkLeaves(const struct mesh_info *mesh, u8 *water, u8 *inLeaf, u8 *form)
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
		for (i = 0; i < node->data.leaf.numQuads; i++)
		{
			inLeaf[first + i] = 1;
			form[first + i] = EngTrack_MosaicFormOfLeaf(node->flag);
			if ((node->flag & BSP_LEAF_FLAG_WATER) != 0)
			{
				water[first + i] = 1;
			}
		}
	}
	return leaves;
}

// The retail models of the level's instances, imported like the turbo flames
// (platform/engine_fx_turbo.c EngineFx_TurboLoad): the meshes exist before
// Engine_RenderUploadMeshes runs.
internal void EngTrack_ImportModels(const struct Level *level)
{
	u32 i;
	int imported = 0;
	int failed = 0;

	s_egtModelCount = 0;
	if ((level->ptrInstDefs == NULL) || (level->numInstances == 0u))
	{
		Platform_Log("[CTR Engine] track: instance models: 0 imported, 0 failed (no instances)\n");
		s_egtLoad.modelsImported = 0;
		s_egtLoad.modelsFailed = 0;
		return;
	}
	for (i = 0; i < level->numInstances; i++)
	{
		const struct Model *model = level->ptrInstDefs[i].model;
		EngTrackModel *m;
		int k;

		if (model == NULL)
		{
			continue;
		}
		if (EngTrack_ModelEntry(model, 0) != NULL)
		{
			continue;
		}
		m = EngTrack_ModelEntry(model, 1);
		if (m == NULL)
		{
			failed++;
			continue;
		}
		if (!Engine_ModelImport(model, m->name, &m->imp) || (m->imp.mesh < 0))
		{
			m->imported = 0;
			snprintf(m->why, sizeof(m->why), "%s", (Engine_ModelImportWhy()[0] != '\0') ? Engine_ModelImportWhy() : "no mesh");
			Platform_Log("[CTR Engine] track: instance model '%s' id %d not imported: %s\n", m->name, (int)m->id, m->why);
			failed++;
			continue;
		}
		m->imported = 1;
		imported++;
		for (k = 0; k < m->imp.materialCount; k++)
		{
			const EngineMaterial *mat = Engine_Material(m->imp.firstMaterial + k);

			if ((mat != NULL) && (mat->blend != ENGINE_BLEND_OPAQUE) && (mat->blend != ENGINE_BLEND_MASK))
			{
				m->transparent = 1;
			}
		}
	}
	Platform_Log("[CTR Engine] track: instance models: %d imported, %d failed (of %d distinct)\n", imported, failed, s_egtModelCount);
	s_egtLoad.modelsImported = imported;
	s_egtLoad.modelsFailed = failed;
}

internal void EngTrack_LiveFree(void)
{
	free(s_egtLive);
	free(s_egtLiveGroups);
	free(s_egtLiveScratchV);
	free(s_egtLiveScratchI);
	s_egtLive = NULL;
	s_egtLiveGroups = NULL;
	s_egtLiveScratchV = NULL;
	s_egtLiveScratchI = NULL;
	s_egtLiveCount = 0;
	s_egtLiveCap = 0;
	s_egtLiveGroupCount = 0;
	s_egtLiveVertices = NULL;
}

// A built face (key set) as a live face with the level vertices of its block. 0 = out of memory.
internal int EngTrack_LiveAppend(const EngTrackFace *f, const u16 *blockIndex)
{
	EngTrackLiveFace *lf;
	int v;

	if (s_egtLiveCount >= s_egtLiveCap)
	{
		const int cap = (s_egtLiveCap == 0) ? 1024 : (s_egtLiveCap * 2);
		EngTrackLiveFace *p = (EngTrackLiveFace *)realloc(s_egtLive, (size_t)cap * sizeof(EngTrackLiveFace));

		if (p == NULL)
		{
			return 0;
		}
		s_egtLive = p;
		s_egtLiveCap = cap;
	}
	lf = &s_egtLive[s_egtLiveCount++];
	memset(lf, 0, sizeof(*lf));
	lf->face = *f;
	for (v = 0; v < 4; v++)
	{
		lf->vertex[v] = blockIndex[f->grid[v]];
	}
	lf->material = -1;
	return 1;
}

internal int EngTrack_LiveCompare(const void *a, const void *b)
{
	const EngTrackLiveFace *x = (const EngTrackLiveFace *)a;
	const EngTrackLiveFace *y = (const EngTrackLiveFace *)b;

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

// The live faces get their materials (a face whose key has none is dropped and
// counted), sorted by (semi, material) and grouped; the scratch for the largest
// group is made. 0 = out of memory (no live face is drawn then).
internal int EngTrack_LiveGroup(const int *matOfKey, int *dropped)
{
	int kept = 0;
	int largest = 0;
	int i;

	for (i = 0; i < s_egtLiveCount; i++)
	{
		EngTrackLiveFace *lf = &s_egtLive[i];

		lf->material = matOfKey[lf->face.key];
		lf->semi = EngTrack_IsSemi(s_egtKeys.key[lf->face.key].tpage);
		if (lf->material < 0)
		{
			(*dropped)++;
			continue;
		}
		s_egtLive[kept++] = *lf;
	}
	s_egtLiveCount = kept;
	if (s_egtLiveCount == 0)
	{
		return 1;
	}
	qsort(s_egtLive, (size_t)s_egtLiveCount, sizeof(EngTrackLiveFace), EngTrack_LiveCompare);
	s_egtLiveGroups = (EngTrackLiveGroup *)calloc((size_t)s_egtLiveCount, sizeof(EngTrackLiveGroup));
	if (s_egtLiveGroups == NULL)
	{
		return 0;
	}
	for (i = 0; i < s_egtLiveCount; i++)
	{
		EngTrackLiveGroup *g = (s_egtLiveGroupCount > 0) ? &s_egtLiveGroups[s_egtLiveGroupCount - 1] : NULL;

		if ((g == NULL) || (g->material != s_egtLive[i].material) || (g->semi != s_egtLive[i].semi))
		{
			g = &s_egtLiveGroups[s_egtLiveGroupCount++];
			g->material = s_egtLive[i].material;
			g->semi = s_egtLive[i].semi;
			g->first = i;
			g->count = 0;
		}
		g->count++;
		if (g->count > largest)
		{
			largest = g->count;
		}
	}
	s_egtLiveScratchV = (EngineVertex *)malloc((size_t)largest * 4u * sizeof(EngineVertex));
	s_egtLiveScratchI = (u16 *)malloc((size_t)largest * 6u * sizeof(u16));
	return ((s_egtLiveScratchV != NULL) && (s_egtLiveScratchI != NULL)) ? 1 : 0;
}

// At load stage 8, after level1 is set and the models are in gGT->modelPtr, before
// Engine_RenderUploadMeshes. Builds the track meshes and imports the instance models.
void Engine_TrackLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Level *level;
	const struct mesh_info *mesh;
	u8 *water = NULL;
	u8 *inLeaf = NULL;
	u8 *form = NULL;
	EngTrackFace *faces = NULL;
	int faceCount = 0;
	int faceCap = 0;
	int *matOfKey = NULL;
	EngTrackMeshBuild *builds = NULL;
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
	int leaves;
	int meshCount = 0;
	int semiPass;
	int i;
	u8 *live = NULL; // level vertices the game animates (SCVert), NULL when none
	u32 liveExpanded = 0;
	int frozenFaces = 0;

	EngTrack_LiveFree();
	s_egtMeshCount = 0;
	s_egtModelCount = 0;
	s_egtLevel = NULL;
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
	memset(hist, 0, sizeof(hist));
	water = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	inLeaf = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	form = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	builds = (EngTrackMeshBuild *)calloc(EGT_MESHES_MAX, sizeof(EngTrackMeshBuild));
	if ((water == NULL) || (inLeaf == NULL) || (form == NULL) || (builds == NULL))
	{
		Platform_Log("[CTR Engine] track: out of memory\n");
		goto done;
	}
	leaves = EngTrack_MarkLeaves(mesh, water, inLeaf, form);
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
			// A face on an animated vertex: live, while the budget lasts; else static (frozen).
			if ((live != NULL) && EngTrack_FaceTouches(f, qb->index, live, mesh->numVertex))
			{
				const u32 expanded = (u32)f->triCount * 3u;

				if ((liveExpanded + expanded <= EGT_LIVE_EXPANDED_MAX) && EngTrack_LiveAppend(f, qb->index))
				{
					liveExpanded += expanded;
					triangles += f->triCount;
					hist[(u8)f->bias]++;
					continue;
				}
				frozenFaces++;
			}
			if (faceCount >= faceCap)
			{
				const int cap = (faceCap == 0) ? 4096 : (faceCap * 2);
				EngTrackFace *p = (EngTrackFace *)realloc(faces, (size_t)cap * sizeof(EngTrackFace));

				if (p == NULL)
				{
					droppedFaces++;
					continue;
				}
				faces = p;
				faceCap = cap;
			}
			faces[faceCount++] = *f;
			triangles += f->triCount;
			hist[(u8)f->bias]++;
		}
	}

	// The mosaic data texture, after the last face.
	Engine_TrackMosaicEnd();

	// Materials: one per key, registered once (Engine_MaterialRegister does not dedupe).
	matOfKey = (int *)malloc(((size_t)s_egtKeys.count + 1u) * sizeof(int));
	if (matOfKey == NULL)
	{
		Platform_Log("[CTR Engine] track: out of memory\n");
		goto done;
	}
	for (i = 0; i < s_egtKeys.count; i++)
	{
		const EngTrackKey *key = &s_egtKeys.key[i];
		EngineMaterial m;
		const int semi = EngTrack_IsSemi(key->tpage);

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
		matOfKey[i] = Engine_MaterialRegister(&m);
		if (matOfKey[i] >= 0)
		{
			materials++;
		}
	}
	for (i = 0; i < faceCount; i++)
	{
		if (matOfKey[faces[i].key] < 0)
		{
			droppedFaces++;
		}
	}
	if ((s_egtLiveCount > 0) && !EngTrack_LiveGroup(matOfKey, &droppedFaces))
	{
		droppedFaces += s_egtLiveCount;
		EngTrack_LiveFree();
	}
	s_egtLiveVertices = mesh->ptrVertexArray;

	// Meshes: opaque first, then semi-transparent (the draw order of the two cells).
	for (semiPass = 0; semiPass < 2; semiPass++)
	{
		const int first = meshCount;
		const int made = EngTrack_Assemble(faces, faceCount, &s_egtKeys, matOfKey, semiPass, EGT_MESH_VERTICES_MAX, builds + first, EGT_MESHES_MAX - first,
		                                   &droppedFaces);
		int b;

		for (b = 0; b < made; b++)
		{
			EngTrackMeshBuild *mb = &builds[first + b];
			EngineMeshDesc desc;
			char name[32];
			int id;

			memset(&desc, 0, sizeof(desc));
			snprintf(name, sizeof(name), "track %s %d", semiPass ? "semi" : "opaque", b);
			desc.vertices = mb->vertices;
			desc.verticesPerFrame = mb->vertexCount;
			desc.frames = 1;
			desc.indices = mb->indices;
			desc.indexCount = mb->indexCount;
			desc.ranges = mb->ranges;
			desc.rangeCount = mb->rangeCount;
			desc.name = name;
			id = Engine_MeshCreate(&desc);
			if (id < 0)
			{
				droppedFaces++; // a whole mesh is lost: counted once, the log line shows the mesh count
				continue;
			}
			s_egtMeshes[s_egtMeshCount].mesh = id;
			s_egtMeshes[s_egtMeshCount].semi = semiPass;
			s_egtMeshes[s_egtMeshCount].vertices = mb->vertexCount;
			s_egtMeshCount++;
		}
		meshCount += made;
	}
	s_egtLevel = level;
	s_egtLoad.levelID = (int)gGT->levelID;
	s_egtLoad.faces = faceCount + s_egtLiveCount;
	s_egtLoad.meshes = s_egtMeshCount;
	s_egtLoad.materials = materials;
	s_egtLoad.dropped = droppedFaces;
	s_egtLoad.liveFaces = s_egtLiveCount;
	s_egtLoad.frozenFaces = frozenFaces;

	Platform_Log("[CTR Engine] track: %d blocks, %d water blocks excluded, %d invisible skipped, %d faces, %d triangles, %d mesh(es), %d materials, "
	             "mosaic faces %d, animtex faces %d\n",
	             blocks, waterBlocks, invisible, faceCount + s_egtLiveCount, triangles, s_egtMeshCount, materials, mosaicFaces, animFaces);
	if ((live != NULL) || (s_egtLiveCount > 0))
	{
		Platform_Log("[CTR Engine] track: %d live face(s) in %d draw(s) (scenery vertex animation, %d animated vertices), %d left static over the budget\n",
		             s_egtLiveCount, s_egtLiveGroupCount, level->numSCVert, frozenFaces);
	}
	Platform_Log("[CTR Engine] track: also %d block(s) in no leaf skipped, %d with a bad vertex index, %d face(s) without a usable layout, "
	             "%d out of the scratch table, %d dropped (keys, materials, meshes, memory)\n",
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
	EngTrack_ImportModels(level);

done:
	for (i = 0; (builds != NULL) && (i < EGT_MESHES_MAX); i++)
	{
		EngTrack_BuildFree(&builds[i]);
	}
	free(builds);
	free(faces);
	free(matOfKey);
	free(water);
	free(inLeaf);
	free(form);
	free(live);
}

// Once per frame from Engine_FrameCollect: one draw per track mesh in view 0.
void Engine_TrackCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	EngineMat4 screenFromWorld;
	int i;
	int submitted = 0;

	s_egtSubmitted = 0;
	if (g_cfg_engine && g_cfg_engineTrack && (gGT != NULL) && ((gGT->gameMode1 & MAIN_MENU) == 0) && (gGT->level1 != NULL) && (gGT->level1 == s_egtLevel))
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
	Engine_CameraScreenFromWorld(cam, &screenFromWorld);
	for (i = 0; i < s_egtMeshCount; i++)
	{
		EngineDraw draw;
		const int cell = s_egtMeshes[i].semi ? EGT_CELL_SEMI : EGT_CELL_OPAQUE;

		memset(&draw, 0, sizeof(draw));
		draw.mesh = s_egtMeshes[i].mesh;
		draw.frame = 0;
		draw.clipFromModel = screenFromWorld; // the track is in world units: the raw s16 positions
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.materialOverride = -1;
		draw.sortDepth = 1.0e9f; // farther than anything: first of the transparent list
		draw.otCell = (u32)cell;
		draw.flags = s_egtMeshes[i].semi ? ENGINE_DRAW_TRANSPARENT : 0;
		draw.dynMaterial = -1;
		if (Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
		{
			submitted++;
		}
	}
	// The live faces, one dynamic draw per material, from the level vertices as the game left them.
	for (i = 0; i < s_egtLiveGroupCount; i++)
	{
		const EngTrackLiveGroup *g = &s_egtLiveGroups[i];
		const int cell = g->semi ? EGT_CELL_SEMI : EGT_CELL_OPAQUE;
		EngineDraw draw;
		u32 indexCount = 0;
		const u32 vertexCount = EngTrack_LiveFill(&s_egtLive[g->first], g->count, s_egtLiveVertices, s_egtLiveScratchV, s_egtLiveScratchI, &indexCount);

		memset(&draw, 0, sizeof(draw));
		draw.mesh = -1;
		draw.clipFromModel = screenFromWorld;
		draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
		draw.materialOverride = -1;
		draw.sortDepth = 1.0e9f;
		draw.otCell = (u32)cell;
		draw.flags = g->semi ? ENGINE_DRAW_TRANSPARENT : 0;
		draw.dynVertices = s_egtLiveScratchV;
		draw.dynVertexCount = vertexCount;
		draw.dynIndices = s_egtLiveScratchI;
		draw.dynIndexCount = indexCount;
		draw.dynMaterial = g->material;
		if ((vertexCount > 0u) && Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, cell))
		{
			submitted++;
			s_egtLiveDrawn++;
		}
	}
	// The PS1 level is skipped only when every mesh and every live draw of the track was submitted this frame.
	if (submitted == (s_egtMeshCount + s_egtLiveGroupCount))
	{
		s_egtSubmitted = 1;
		s_egtFramesDrawn++;
	}
}

int Engine_TrackDrawnThisFrame(void)
{
	return s_egtSubmitted;
}

// 1 = the PS1 walk of DrawLevelOvr1P treats this bucket as empty this frame.
// Buckets 0-7 and 10 are track faces (4x4, dynamic, 4x2, 4x1, full dynamic; rendered
// and list variants); 8 and 9 are water, answered by Engine_WaterOwnsBucket. The
// counts are bucket visits of the walk, not drawn buckets.
int Engine_TrackOwnsBucket(int bucketIndex)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || !s_egtSubmitted || (bucketIndex < 0) || (bucketIndex >= EGT_BUCKET_COUNT))
	{
		return 0;
	}
	if ((bucketIndex == EGT_BUCKET_WATER_RENDERED) || (bucketIndex == EGT_BUCKET_WATER_LIST))
	{
		s_egtWaterVisits++;
		return 0;
	}
	s_egtBucketsSkipped++;
	return 1;
}

// A level instance (inst->instDef != NULL: INSTANCE.c:395 links it to its InstDef
// in the LEV, only level instances have one) whose model was imported at load,
// drawn by Engine_InstanceSubmit. Declined (0): not a level instance, the track
// not engine-drawn this frame, screen-space or billboard or hidden instances,
// a model without import, a frame out of range, or the submit refusing.
int Engine_TrackInstanceRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	struct GameTracker *gGT = sdata->gGT;
	EngTrackModel *em;
	EngineInstanceDraw d;
	const struct ModelHeader *mh0;
	int anim;
	int frame;
	int meshFrame;

	if (!g_cfg_engine || !g_cfg_engineTrack || !s_egtSubmitted || (inst == NULL) || (idpp == NULL) || (pb == NULL) || (gGT == NULL) ||
	    (inst->instDef == NULL) || (inst->model == NULL))
	{
		return 0;
	}
	em = EngTrack_ModelEntry(inst->model, 1);
	if (em == NULL)
	{
		s_egtInstDeclined++;
		s_egtInstDeclinedBy[EGT_DECLINE_MODEL]++;
		return 0;
	}
	if ((em->drawn == 0u) && (em->declined == 0u))
	{
		Platform_Log("[CTR Engine] track: instance model '%s' id %d first seen (%s)\n", em->name, (int)em->id, em->imported ? "imported" : "not imported");
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

	// The animation frame as the turbo flames take it (platform/engine_fx_turbo.c).
	anim = (int)inst->animIndex;
	frame = (int)inst->animFrame;
	{
		const struct ModelAnim *ma = NativeRenderLayer_AnimOf(mh0, (u32)anim);

		if (ma != NULL)
		{
			frame = NativeRenderLayer_AnimFrameNumber(ma, (u16)inst->animFrame);
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
	if (Engine_InstanceSubmit(inst, idpp, pb, &d) > 0)
	{
		em->drawn++;
		s_egtInstDrawn++;
		return 1;
	}
	em->declined++;
	s_egtInstDeclined++;
	s_egtInstDeclinedBy[EGT_DECLINE_SUBMIT]++;
	return 0;
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

	// (9) mesh split at 65536 vertices: 16384 four-vertex faces fill one mesh, the next face starts a second.
	{
		static EngTrackKeySet set;
		EngTrackMeshBuild builds[4];
		EngTrackFace *faces;
		const int matOfKey[1] = {7};
		int dropped = 0;
		int made;
		int fi;

		memset(builds, 0, sizeof(builds));
		set.count = 0;
		EngTrack_KeysFind(&set, 0x0160, 0, 0, 0);
		faces = (EngTrackFace *)calloc(16385u, sizeof(EngTrackFace));
		EGT_CHECK(faces != NULL, "split test: out of memory");
		if (faces != NULL)
		{
			int bad = 0;

			for (fi = 0; fi < 16385; fi++)
			{
				faces[fi].vertCount = 4;
				faces[fi].triCount = 2;
				faces[fi].tri[0][0] = 0;
				faces[fi].tri[0][1] = 1;
				faces[fi].tri[0][2] = 2;
				faces[fi].tri[1][0] = 1;
				faces[fi].tri[1][1] = 3;
				faces[fi].tri[1][2] = 2;
				faces[fi].key = 0;
			}
			made = EngTrack_Assemble(faces, 16384, &set, matOfKey, 0, EGT_MESH_VERTICES_MAX, builds, 4, &dropped);
			EGT_CHECK((made == 1) && (builds[0].vertexCount == 65536u) && (builds[0].indexCount == 16384u * 6u) && (dropped == 0),
			          "split test: 16384 faces gave %d mesh(es), %u vertices", made, builds[0].vertexCount);
			for (fi = 0; fi < 4; fi++)
			{
				EngTrack_BuildFree(&builds[fi]);
			}
			made = EngTrack_Assemble(faces, 16385, &set, matOfKey, 0, EGT_MESH_VERTICES_MAX, builds, 4, &dropped);
			EGT_CHECK((made == 2) && (builds[0].vertexCount == 65536u) && (builds[1].vertexCount == 4u) && (builds[1].indexCount == 6u),
			          "split test: 16385 faces gave %d mesh(es), %u + %u vertices", made, builds[0].vertexCount, builds[1].vertexCount);
			for (fi = 0; (fi < 2) && (fi < made); fi++)
			{
				u32 q;

				for (q = 0; q < builds[fi].indexCount; q++)
				{
					bad += (builds[fi].indices[q] >= builds[fi].vertexCount);
				}
				bad += (builds[fi].rangeCount != 1u) || (builds[fi].ranges[0].material != 7u) || (builds[fi].ranges[0].indexCount != builds[fi].indexCount);
			}
			EGT_CHECK(bad == 0, "split test: %d bad index or range entries", bad);
			for (fi = 0; fi < 4; fi++)
			{
				EngTrack_BuildFree(&builds[fi]);
			}
			// The semi pass of an opaque-only set is empty.
			made = EngTrack_Assemble(faces, 16385, &set, matOfKey, 1, EGT_MESH_VERTICES_MAX, builds, 4, &dropped);
			EGT_CHECK(made == 0, "split test: opaque faces appeared in the semi pass");
			free(faces);
		}
		for (fi = 0; fi < 4; fi++)
		{
			EngTrack_BuildFree(&builds[fi]);
		}
	}

	// (10) live faces: the grid of the slot vertices, the touch test, the fill from the level vertices.
	{
		const u16 blockIndex[9] = {10, 11, 12, 13, 14, 15, 16, 17, 18};
		struct LevVertex verts[20];
		u8 live[20];
		EngTrackLiveFace lf[2];
		EngineVertex outV[8];
		u16 outI[12];
		u32 indexCount = 0;
		u32 vertexCount;

		EngTrack_TestBlock(&in, &lay);
		n = EngTrack_BuildBlock(&in, &identity, 1, out, NULL);
		EGT_CHECK((n == 4) && (out[1].grid[0] == 4) && (out[1].grid[1] == 1) && (out[1].grid[2] == 6) && (out[1].grid[3] == 7),
		          "live: face 1 grid %d %d %d %d (expected 4 1 6 7)", out[1].grid[0], out[1].grid[1], out[1].grid[2], out[1].grid[3]);
		memset(live, 0, sizeof(live));
		live[16] = 1; // grid corner 6 of this block: the middle vertex, shared by all four faces
		EGT_CHECK(EngTrack_FaceTouches(&out[0], blockIndex, live, 20) && EngTrack_FaceTouches(&out[3], blockIndex, live, 20), "live: a face on the middle vertex not touched");
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
		memset(lf, 0, sizeof(lf));
		for (k = 0; k < 2; k++)
		{
			lf[k].face = out[k];
			for (v = 0; v < 4; v++)
			{
				lf[k].vertex[v] = blockIndex[out[k].grid[v]];
			}
		}
		vertexCount = EngTrack_LiveFill(lf, 2, verts, outV, outI, &indexCount);
		EGT_CHECK((vertexCount == 8u) && (indexCount == 12u), "live fill: %u vertices, %u indices (expected 8, 12)", vertexCount, indexCount);
		EGT_CHECK((outV[4].position[0] == 1014.0f) && (outV[4].position[1] == 2014.0f) && (outV[4].position[2] == 3014.0f) && (outV[4].color[0] == 114) &&
		              (outV[4].color[2] == 154) && (outV[5].position[0] == 1011.0f),
		          "live fill: positions/colours not read from the level vertices");
		EGT_CHECK((outV[4].texcoord[0] == out[1].v[0].texcoord[0]) && (outV[4].texcoord[1] == out[1].v[0].texcoord[1]) && (outV[4].color[3] == 255),
		          "live fill: texture coordinates or alpha changed");
		EGT_CHECK((outI[6] == (u16)(4 + out[1].tri[0][0])) && (outI[11] == (u16)(4 + out[1].tri[1][2])), "live fill: indices not offset to the second face");
	}

	return failures;
}
