#include <platform/native_extcoll.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// THE EXTERNAL TRACK'S COLLISION - see include/platform/native_extcoll.h.
//
// WHAT. The game's collision (game/COLL.c) searches quadblocks in a BSP. An
// external track has no LEV: its collision comes as collision.glb (one node per
// body, node extras {collision, terrain, wall_scrub, double_sided}, the
// attribute _CHECKPOINT per triangle, positions already in world units as
// integers). At the first level load of the track this file turns every
// triangle into ONE quadblock record and puts the records into a search tree in
// the BSP format - in its own heap, never in the MEMPACK, never in the level's
// mesh_info (the renderer, the vis lists and LevInstDef keep reading the
// donor's). Every collision reader asks COLL_WorldMesh(level), which hands out
// this mesh_info while the external track holds the level's slot and the
// level's own pointer otherwise - without --ext-track nothing is built and the
// answer is always level->ptr_mesh_info, the same pointer as before.
//
// NO GAME LOGIC CHANGES. The triangle tests, normals, sweep, scrub, mask grab
// and terrain code of COLL.c run unchanged on these records. What this file
// must get right is only the data they read:
//
//   index[9]    the corners (a, c, b) of the file's triangle (a, b, c) -
//               REVERSED: the game's normal is (v3 - v1) x (v2 - v1)
//               (COLL_FIXED_TRIANGL_GetNormVec), so a triangle that is
//               counter-clockwise from above in the file (glTF front, normal
//               up) is stored clockwise. index[3] = index[2]: no second
//               triangle (COLL.c tests `index[2] != index[3]`). index[4..6] =
//               the midpoints of the record's edges (0,1) (0,2) (1,2): the four
//               high-LOD triangles (0,4,5) (4,6,5) (6,4,1) (5,6,2) tile the
//               triangle exactly, and the low-LOD one is (0,1,2).
//               index[7..8] = index[2] (never read without a second triangle).
//   quadFlags   ground 0x1800 (GROUND | CAMERA_SEARCH; the camera searches only
//               CAMERA_SEARCH), wall 0x2001 (COLLISION_SURFACE; bit 0 picks the
//               sweep's scrub "no scrub") or 0x2000 ("solid"), kill 0xE210 (as
//               the retail kill planes: passed through, catches).
//   draw_order_low bit 31 = double-sided (walls, kills): the sweep keeps a
//               back face only then.
//   terrain_type, checkpointIndex (255 = none), blockID = record index,
//   pvs = NULL: the vis code skips a NULL pvs before it would take the
//               record's index in the level's array (MainFrame.c,
//               MainFrame_VisMemAddDriverPVS / MainFrame_VisMemFullFrame).
//   triNormalVecDividend / BitShift in the game's 32-bit arithmetic: every
//               product below 2^30, fine length 4096, coarse 1024 (as the
//               generator tools/collision_gen/lev_normals.py).
//   bbox        over the six corners.
// The vertices get colour 0x80: light, the kart is not darkened by the ground.
//
// THE TREE. Median split on the longest axis of the centroids, leaves of at
// most EXT_COLL_LEAF_QUADS records and never empty (a leaf tests one record
// even when it has none), the root always a branch (it is read as one), the
// depth far below the ~76 entries of the search stack in the scratchpad, at
// most 0x3FFF nodes. The records of a leaf are contiguous. No instance hitboxes
// (bspHitboxArray = NULL). A leaf holding a WATER or FASTWATER record carries
// the water flag, as the retail water leaves.
//
// LIMITS (refused with one log line, nothing is cut silently): positions s16
// integers, at most 65535 vertices with the midpoints (VERTEX PAGES: a track
// whose collision section asks for vertex_pages gets as many pages of at most
// 65535 vertices as it needs; a record's indices count from its page's first
// vertex, the base of each record in recBase, read by every collision reader
// through COLL_RecordVertexArray; without the key no page, recBase NULL and
// the indices as before), at most 0x3FFF nodes,
// edges at most EXT_COLL_MAX_EDGE (the converter splits longer ones), node
// transforms none, a dividend that fits for every triangle.
//
// BUILT ONCE PER RUN and kept: the drivers hold record pointers (underDriver,
// lastValid, currBlockTouching) across restarts.

enum
{
	EXT_COLL_LEAF_QUADS = 16,
	EXT_COLL_MAX_EDGE = 1024,
	EXT_COLL_FLAGS_GROUND = 0x1800,
	EXT_COLL_FLAGS_WALL_NOSCRUB = 0x2001,
	EXT_COLL_FLAGS_WALL_SOLID = 0x2000,
	EXT_COLL_FLAGS_KILL = 0xE210,
	// a turbo pad as retail has it (184 of 184 pad quads in 12 LEVs, the retail
	// one included): TRIGGER | COLLISION_SURFACE | SKIP_WATER_LIST, not GROUND,
	// not double-sided, no checkpoint, 32 units over the road; touching it ORs
	// terrain_type (1 turbo pad, 2 super pad) into the step flags (COLL.c:2092-2098)
	EXT_COLL_FLAGS_TRIGGER = 0xA040,
	EXT_COLL_PADS_MAX = 64,
	EXT_COLL_CP_NONE = 0xFF,
	EXT_COLL_PRODUCT_MAX = 1 << 30,
	EXT_COLL_DIVIDEND_MAX = 0x7FFF,
	EXT_COLL_VERTEX_MAX = 65535,
	EXT_COLL_NODE_MAX = 0x3FFF,
	EXT_COLL_RECORD_MAX = 32767,     // QuadBlock.blockID is s16
	EXT_COLL_SEARCH_STACK = 76,      // COLL_SearchBSP_CallbackPARAM: s16 entries from scratchpad 0x70 to 0x108
	EXT_COLL_TERRAINS = 21,
	EXT_COLL_TEST_TRIS = 64,
};

#define EXT_COLL_EDGE_SLACK 2.5
// a miss this close to an edge of the four high-LOD triangles is a seam of the
// game's fixed point (3.0: a large sloped record of the second test track had one at 2.63)
#define EXT_COLL_SEAM_MISS_SLACK 3.0
#define EXT_COLL_SEAM_MAX_FRACTION 0.0005
#define EXT_COLL_ZONE_RAY_MIN_NY 0.5

typedef enum
{
	EXT_COLL_CLASS_NONE,
	EXT_COLL_CLASS_GROUND,
	EXT_COLL_CLASS_WALL,
	EXT_COLL_CLASS_KILL,
	EXT_COLL_CLASS_TRIGGER, // a turbo pad: terrain = its trigger bits (COLL_STEP_TRIGGER_*)
	EXT_COLL_CLASS_COUNT
} ExtCollClass;

// One input triangle in file order (glTF front = counter-clockwise from above).
typedef struct
{
	s16 p[3][3];
	u8 cls;
	u8 terrain;
	u8 checkpoint;
	u8 doubleSided;
	u8 wallNoScrub;
	u8 pad; // trigger: the pad's number (0..EXT_COLL_PADS_MAX-1)
	u8 zone; // the anti-gravity zone of a ground triangle (physics.zones), 0 none
} ExtCollTri;

// The collision section's vertex_pages (set by ExtColl_OnLevelLoaded and the
// selftest right before ExtColl_Build): 0 = one array of at most 65535
// vertices, as always.
global_variable int s_extCollPagesAsked;

// The turbo pads of the track: the box of each pad's records (for the log
// and for telling which pad gave a boost).
typedef struct
{
	struct BoundingBox box;
	int records;
	int bits;
} ExtCollPad;

global_variable struct
{
	struct mesh_info mesh;
	struct QuadBlock *quads;
	struct LevVertex *verts;
	struct BSP *bsp;
	int numQuads;
	int numVerts;
	int numBsp;
	int ready;
	int tried;
	int treeDepth;
	int waterLeaves;
	int countClass[EXT_COLL_CLASS_COUNT];
	int countTerrain[EXT_COLL_TERRAINS];
	int killTopY;    // the highest corner of a kill record (for the frame log)
	// the limits line: what the build used of each fixed size
	double maxEdge;  // the longest corner edge, units
	int shiftMin;    // the smallest / largest dividend shift of a record
	int shiftMax;
	int midOff2;     // twice the largest offset (per axis) of a midpoint from its corners' exact middle
	size_t heapKept; // bytes kept after the build (records, vertex array, node array)
	size_t heapPeak; // bytes held during the build
	u8 *recZone; // per record (tree order): its zone, 0 none
	u32 *recBase; // per record (tree order): its vertex page's first vertex; NULL without vertex pages
	int numPages; // vertex pages used (0 without vertex pages)
	int zoneRecords;
	ExtCollPad pads[EXT_COLL_PADS_MAX];
	int numPads; // the highest pad number + 1
} s_extColl;

// THE FRAME LOG of seat 0 on the external track (read only, for the gate of
// Phase 3: ground contact per terrain, wall contacts, the lowest height).
global_variable struct
{
	int frames;
	int groundFrames;
	int terrainFrames[EXT_COLL_TERRAINS];
	int wallFrames;
	int minY;
	int minAt[3];
	int armed;
} s_extCollRun;

struct mesh_info *COLL_WorldMesh(struct Level *level)
{
	// Without --ext-track nothing was built: the first compare already answers
	// with the level's own pointer, as before.
	if (s_extColl.ready && (level != NULL) && (sdata->gGT != NULL) && (level == sdata->gGT->level1) && ExtTrack_ActiveForLevel(sdata->gGT->levelID))
	{
		return &s_extColl.mesh;
	}
	return (level != NULL) ? level->ptr_mesh_info : NULL;
}

// a record's vertices: its page's first vertex (always the array itself
// without vertex pages)
#define EXT_COLL_RV(q) (s_extColl.verts + ((s_extColl.recBase != NULL) ? s_extColl.recBase[(q) - s_extColl.quads] : 0u))

struct LevVertex *COLL_RecordVertexArray(struct mesh_info *mesh, const struct QuadBlock *quad)
{
	// Without vertex pages recBase is NULL and the answer is the mesh's own
	// array, the same pointer as before.
	if ((s_extColl.recBase != NULL) && (mesh == &s_extColl.mesh) && (quad >= s_extColl.quads) && (quad < s_extColl.quads + s_extColl.numQuads))
	{
		return s_extColl.verts + s_extColl.recBase[quad - s_extColl.quads];
	}
	return mesh->ptrVertexArray;
}

internal void ExtColl_Free(void)
{
	free(s_extColl.quads);
	free(s_extColl.verts);
	free(s_extColl.bsp);
	free(s_extColl.recZone);
	free(s_extColl.recBase);
	s_extColl.recBase = NULL;
	s_extColl.numPages = 0;
	s_extColl.quads = NULL;
	s_extColl.verts = NULL;
	s_extColl.bsp = NULL;
	s_extColl.recZone = NULL;
	s_extColl.zoneRecords = 0;
	s_extColl.ready = 0;
	memset(&s_extColl.mesh, 0, sizeof(s_extColl.mesh));
	s_extColl.numQuads = 0;
	s_extColl.numVerts = 0;
	s_extColl.numBsp = 0;
	s_extColl.treeDepth = 0;
	s_extColl.waterLeaves = 0;
	s_extColl.maxEdge = 0.0;
	s_extColl.shiftMin = 0;
	s_extColl.shiftMax = 0;
	s_extColl.midOff2 = 0;
	s_extColl.heapKept = 0;
	s_extColl.heapPeak = 0;
	memset(s_extColl.pads, 0, sizeof(s_extColl.pads));
	s_extColl.numPads = 0;
	memset(s_extColl.countClass, 0, sizeof(s_extColl.countClass));
	memset(s_extColl.countTerrain, 0, sizeof(s_extColl.countTerrain));
}

// ---------------------------------------------------------------------------
// vertices: welded by position; a midpoint of the same edge is the same
// position from both of its triangles, so midpoints are shared too

typedef struct
{
	s16 x, y, z;
	int index;
	int used;
} ExtCollVSlot;

typedef struct
{
	ExtCollVSlot *slots;
	int cap;
	struct LevVertex *verts;
	int num;
	int max;
} ExtCollVerts;

internal int ExtColl_Vertex(ExtCollVerts *vs, s16 x, s16 y, s16 z)
{
	u32 h = (((u32)(u16)x * 73856093u) ^ ((u32)(u16)y * 19349663u) ^ ((u32)(u16)z * 83492791u)) & (u32)(vs->cap - 1);

	for (;;)
	{
		ExtCollVSlot *s = &vs->slots[h];

		if (!s->used)
		{
			struct LevVertex *v;

			if (vs->num >= vs->max)
			{
				return -1;
			}
			s->used = 1;
			s->x = x;
			s->y = y;
			s->z = z;
			s->index = vs->num;
			v = &vs->verts[vs->num++];
			memset(v, 0, sizeof(*v));
			v->pos.x = x;
			v->pos.y = y;
			v->pos.z = z;
			v->color_hi[0] = v->color_hi[1] = v->color_hi[2] = 0x80;
			v->color_lo[0] = v->color_lo[1] = v->color_lo[2] = 0x80;
			return s->index;
		}
		if ((s->x == x) && (s->y == y) && (s->z == z))
		{
			return s->index;
		}
		h = (h + 1) & (u32)(vs->cap - 1);
	}
}

internal int ExtColl_Mid(ExtCollVerts *vs, int a, int b)
{
	const SVec3 pa = vs->verts[a].pos;
	const SVec3 pb = vs->verts[b].pos;

	const s32 sum[3] = {(s32)pa.x + pb.x, (s32)pa.y + pb.y, (s32)pa.z + pb.z};

	// how far the rounded-down midpoint lies from the exact middle, for the
	// limits line (twice the offset: 0 or 1 per axis)
	for (int k = 0; k < 3; k++)
	{
		const s32 off2 = sum[k] - 2 * (sum[k] >> 1);

		if (off2 > s_extColl.midOff2)
		{
			s_extColl.midOff2 = off2;
		}
	}
	// (pa + pb) >> 1: symmetric in a and b, the same from both triangles.
	return ExtColl_Vertex(vs, (s16)(sum[0] >> 1), (s16)(sum[1] >> 1), (s16)(sum[2] >> 1));
}

// ---------------------------------------------------------------------------
// normals in the game's arithmetic

internal void ExtColl_GameCross(const SVec3 *p1, const SVec3 *p2, const SVec3 *p3, s32 out[3])
{
	const s32 ax = p3->x - p1->x, ay = p3->y - p1->y, az = p3->z - p1->z;
	const s32 bx = p2->x - p1->x, by = p2->y - p1->y, bz = p2->z - p1->z;

	out[0] = ay * bz - az * by;
	out[1] = az * bx - ax * bz;
	out[2] = ax * by - ay * bx;
}

// The used triangles of a one-triangle record: high LOD 0..3 (shift 0) and the
// low LOD one (shift 2), with their dividend slots.
global_variable const u8 s_extCollTri[5][3] = {{0, 4, 5}, {4, 6, 5}, {6, 4, 1}, {5, 6, 2}, {0, 1, 2}};
global_variable const u8 s_extCollTriLod[5] = {0, 0, 0, 0, 2};
global_variable const u8 s_extCollTriDiv[5] = {QUADBLOCK_TRI_NORMAL_DIVIDEND_HI_0, QUADBLOCK_TRI_NORMAL_DIVIDEND_HI_1, QUADBLOCK_TRI_NORMAL_DIVIDEND_HI_2,
                                                 QUADBLOCK_TRI_NORMAL_DIVIDEND_HI_3, QUADBLOCK_TRI_NORMAL_DIVIDEND_LO_0};

// bitShift and dividends; 0 when no shift keeps every product below 2^30 or a
// triangle is degenerate.
internal int ExtColl_Dividends(struct QuadBlock *q, const struct LevVertex *verts)
{
	s32 cross[5][3];
	double len[5];

	for (int t = 0; t < 5; t++)
	{
		ExtColl_GameCross(&verts[q->index[s_extCollTri[t][0]]].pos, &verts[q->index[s_extCollTri[t][1]]].pos, &verts[q->index[s_extCollTri[t][2]]].pos,
		                  cross[t]);
		len[t] = sqrt((double)cross[t][0] * cross[t][0] + (double)cross[t][1] * cross[t][1] + (double)cross[t][2] * cross[t][2]);
		if (len[t] <= 0.0)
		{
			return 0;
		}
	}
	for (int shift = 24; shift >= 0; shift--)
	{
		s32 div[5];
		int ok = 1;

		for (int t = 0; (t < 5) && ok; t++)
		{
			const double d = floor(4096.0 * (double)(1u << shift) / len[t] + 0.5);

			if (d > (double)EXT_COLL_DIVIDEND_MAX)
			{
				ok = 0;
				break;
			}
			div[t] = (s32)d;
			for (int k = 0; k < 3; k++)
			{
				const s32 c = cross[t][k] >> s_extCollTriLod[t];
				const s64 product = (s64)((c < 0) ? -c : c) * div[t];

				if (product >= EXT_COLL_PRODUCT_MAX)
				{
					ok = 0;
					break;
				}
			}
		}
		if (ok)
		{
			memset(q->triNormalVecDividend, 0, sizeof(q->triNormalVecDividend));
			for (int t = 0; t < 5; t++)
			{
				q->triNormalVecDividend[s_extCollTriDiv[t]] = (s16)div[t];
			}
			q->triNormalVecBitShift = (char)shift;
			return 1;
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
// the tree

typedef struct
{
	int quad;
	s32 c[3]; // the centroid times 3
} ExtCollKey;

global_variable int s_extCollSortAxis;

internal int ExtColl_CompareKey(const void *pa, const void *pb)
{
	const ExtCollKey *a = (const ExtCollKey *)pa;
	const ExtCollKey *b = (const ExtCollKey *)pb;
	const s32 d = a->c[s_extCollSortAxis] - b->c[s_extCollSortAxis];

	if (d != 0)
	{
		return (d < 0) ? -1 : 1;
	}
	return a->quad - b->quad; // deterministic
}

internal void ExtColl_GrowBox(struct BoundingBox *box, const struct BoundingBox *add)
{
	if (add->min.x < box->min.x)
	{
		box->min.x = add->min.x;
	}
	if (add->min.y < box->min.y)
	{
		box->min.y = add->min.y;
	}
	if (add->min.z < box->min.z)
	{
		box->min.z = add->min.z;
	}
	if (add->max.x > box->max.x)
	{
		box->max.x = add->max.x;
	}
	if (add->max.y > box->max.y)
	{
		box->max.y = add->max.y;
	}
	if (add->max.z > box->max.z)
	{
		box->max.z = add->max.z;
	}
}

typedef struct
{
	ExtCollKey *keys;
	const struct QuadBlock *src; // input order
	const u8 *srcZone;           // input order: the zone of each record
	u8 *dstZone;                 // tree order
	const u32 *srcBase;          // input order: the vertex page of each record (NULL without pages)
	u32 *dstBase;                // tree order
	struct QuadBlock *dst;       // tree order
	struct BSP *nodes;
	int numNodes;
	int numDst;
	int maxDepth;
	int waterLeaves;
} ExtCollTree;

// The subtree over keys[first .. first + count): returns its child id (with the
// leaf flag for a leaf), or -1 when out of nodes.
internal int ExtColl_BuildNode(ExtCollTree *tr, int first, int count, int depth, int forceBranch)
{
	int self;
	struct BSP *node;

	if (tr->numNodes >= EXT_COLL_NODE_MAX)
	{
		return -1;
	}
	if (depth > tr->maxDepth)
	{
		tr->maxDepth = depth;
	}
	self = tr->numNodes++;
	node = &tr->nodes[self];
	memset(node, 0, sizeof(*node));
	node->id = (s16)self;

	if ((count <= EXT_COLL_LEAF_QUADS) && !forceBranch)
	{
		int water = 0;

		node->flag = BSP_NODE_FLAG_LEAF;
		node->data.leaf.numQuads = count;
		node->data.leaf.bspHitboxArray = NULL;
		node->data.leaf.ptrQuadBlockArray = &tr->dst[tr->numDst];
		for (int i = 0; i < count; i++)
		{
			const struct QuadBlock *q = &tr->src[tr->keys[first + i].quad];

			tr->dst[tr->numDst] = *q;
			tr->dstZone[tr->numDst] = tr->srcZone[tr->keys[first + i].quad];
			if (tr->dstBase != NULL)
			{
				tr->dstBase[tr->numDst] = tr->srcBase[tr->keys[first + i].quad];
			}
			tr->dst[tr->numDst].blockID = (s16)tr->numDst;
			if (i == 0)
			{
				node->box = q->bbox;
			}
			else
			{
				ExtColl_GrowBox(&node->box, &q->bbox);
			}
			if ((q->terrain_type == TERRAIN_WATER) || (q->terrain_type == TERRAIN_FASTWATER))
			{
				water = 1;
			}
			tr->numDst++;
		}
		if (water)
		{
			node->flag |= BSP_LEAF_FLAG_WATER;
			tr->waterLeaves++;
		}
		return BSP_CHILD_ID_LEAF_FLAG | self;
	}

	{
		s32 lo[3], hi[3];
		int axis = 0;
		int half, c0, c1;

		for (int k = 0; k < 3; k++)
		{
			lo[k] = hi[k] = tr->keys[first].c[k];
		}
		for (int i = 1; i < count; i++)
		{
			for (int k = 0; k < 3; k++)
			{
				const s32 v = tr->keys[first + i].c[k];

				if (v < lo[k])
				{
					lo[k] = v;
				}
				if (v > hi[k])
				{
					hi[k] = v;
				}
			}
		}
		for (int k = 1; k < 3; k++)
		{
			if ((hi[k] - lo[k]) > (hi[axis] - lo[axis]))
			{
				axis = k;
			}
		}
		s_extCollSortAxis = axis;
		qsort(&tr->keys[first], (size_t)count, sizeof(ExtCollKey), ExtColl_CompareKey);

		// a forced branch over a small set: one leaf child, no second
		half = (count > EXT_COLL_LEAF_QUADS) ? (count / 2) : count;
		c0 = ExtColl_BuildNode(tr, first, half, depth + 1, 0);
		c1 = (count - half > 0) ? ExtColl_BuildNode(tr, first + half, count - half, depth + 1, 0) : (int)(u16)BSP_CHILD_ID_NONE;
		if ((c0 < 0) || (c1 < 0))
		{
			return -1;
		}
		node = &tr->nodes[self];
		node->flag = 0;
		node->data.branch.childID[0] = (BspChildId)c0;
		node->data.branch.childID[1] = (BspChildId)c1;
		node->data.branch.childID[2] = (BspChildId)BSP_CHILD_ID_NONE;
		node->data.branch.childID[3] = (BspChildId)BSP_CHILD_ID_NONE;
		node->box = tr->nodes[c0 & BSP_CHILD_ID_INDEX_MASK].box;
		if (c1 != (int)(u16)BSP_CHILD_ID_NONE)
		{
			ExtColl_GrowBox(&node->box, &tr->nodes[c1 & BSP_CHILD_ID_INDEX_MASK].box);
		}
		return self;
	}
}

// ---------------------------------------------------------------------------
// build from triangles

internal int ExtColl_EdgeTooLong(const s16 *a, const s16 *b)
{
	const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];

	return (dx * dx + dy * dy + dz * dz) > ((double)EXT_COLL_MAX_EDGE * EXT_COLL_MAX_EDGE + 0.5);
}

internal int ExtColl_Build(const ExtCollTri *tris, int numTris, char *why, int whySize)
{
	ExtCollVerts vs;
	ExtCollTree tr;
	struct QuadBlock *src = NULL;
	ExtCollKey *keys = NULL;
	u8 *srcZone = NULL;
	u32 *srcBase = NULL;
	const int pages = s_extCollPagesAsked;
	int pageBase = 0;
	int numPages = 1;
	int numSolid = 0;
	int nq = 0;

	ExtColl_Free();
	memset(&vs, 0, sizeof(vs));
	memset(&tr, 0, sizeof(tr));

	for (int i = 0; i < numTris; i++)
	{
		if (tris[i].cls != EXT_COLL_CLASS_NONE)
		{
			numSolid++;
		}
	}
	if (numSolid == 0)
	{
		snprintf(why, (size_t)whySize, "no collision triangles");
		return 0;
	}
	if (numSolid > EXT_COLL_RECORD_MAX)
	{
		snprintf(why, (size_t)whySize, "%d collision triangles, more than %d records (the record id is s16)", numSolid, EXT_COLL_RECORD_MAX);
		return 0;
	}

	vs.cap = 1;
	while (vs.cap < 4 * EXT_COLL_VERTEX_MAX)
	{
		vs.cap <<= 1;
	}
	// with vertex pages: room for six new vertices per record, a page of at
	// most EXT_COLL_VERTEX_MAX of them (the hash holds one page)
	vs.max = pages ? (6 * numSolid) : EXT_COLL_VERTEX_MAX;
	vs.slots = (ExtCollVSlot *)calloc((size_t)vs.cap, sizeof(ExtCollVSlot));
	vs.verts = (struct LevVertex *)malloc((size_t)vs.max * sizeof(struct LevVertex));
	if (pages)
	{
		srcBase = (u32 *)calloc((size_t)numSolid, sizeof(u32));
		tr.dstBase = (u32 *)calloc((size_t)numSolid, sizeof(u32));
		if (!srcBase || !tr.dstBase)
		{
			snprintf(why, (size_t)whySize, "out of memory");
			goto Fail;
		}
	}
	src = (struct QuadBlock *)calloc((size_t)numSolid, sizeof(struct QuadBlock));
	keys = (ExtCollKey *)malloc((size_t)numSolid * sizeof(ExtCollKey));
	tr.dst = (struct QuadBlock *)calloc((size_t)numSolid, sizeof(struct QuadBlock));
	tr.nodes = (struct BSP *)calloc((size_t)EXT_COLL_NODE_MAX, sizeof(struct BSP));
	srcZone = (u8 *)calloc((size_t)numSolid, 1);
	tr.dstZone = (u8 *)calloc((size_t)numSolid, 1);
	if (!vs.slots || !vs.verts || !src || !keys || !tr.dst || !tr.nodes || !srcZone || !tr.dstZone)
	{
		snprintf(why, (size_t)whySize, "out of memory");
		goto Fail;
	}

	for (int i = 0; i < numTris; i++)
	{
		const ExtCollTri *t = &tris[i];
		struct QuadBlock *q;
		int va, vb, vc, m01, m02, m12;

		if (t->cls == EXT_COLL_CLASS_NONE)
		{
			continue;
		}
		if (ExtColl_EdgeTooLong(t->p[0], t->p[1]) || ExtColl_EdgeTooLong(t->p[1], t->p[2]) || ExtColl_EdgeTooLong(t->p[2], t->p[0]))
		{
			snprintf(why, (size_t)whySize, "triangle %d has an edge longer than %d units (the converter splits them)", i, EXT_COLL_MAX_EDGE);
			goto Fail;
		}
		for (int e = 0; e < 3; e++)
		{
			const s16 *a = t->p[e];
			const s16 *b = t->p[(e + 1) % 3];
			const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
			const double edge = sqrt(dx * dx + dy * dy + dz * dz);

			if (edge > s_extColl.maxEdge)
			{
				s_extColl.maxEdge = edge;
			}
		}
		// a new page when this record's six vertices might not fit the
		// current one: the hash forgets the old page, so every index found
		// below lies in the new one
		if (pages && ((vs.num - pageBase) > (EXT_COLL_VERTEX_MAX - 6)))
		{
			pageBase = vs.num;
			numPages++;
			memset(vs.slots, 0, (size_t)vs.cap * sizeof(ExtCollVSlot));
		}
		// REVERSED corners (a, c, b): see the head of the file
		va = ExtColl_Vertex(&vs, t->p[0][0], t->p[0][1], t->p[0][2]);
		vc = ExtColl_Vertex(&vs, t->p[2][0], t->p[2][1], t->p[2][2]);
		vb = ExtColl_Vertex(&vs, t->p[1][0], t->p[1][1], t->p[1][2]);
		m01 = ((va >= 0) && (vc >= 0)) ? ExtColl_Mid(&vs, va, vc) : -1;
		m02 = ((va >= 0) && (vb >= 0)) ? ExtColl_Mid(&vs, va, vb) : -1;
		m12 = ((vc >= 0) && (vb >= 0)) ? ExtColl_Mid(&vs, vc, vb) : -1;
		if ((va < 0) || (vb < 0) || (vc < 0) || (m01 < 0) || (m02 < 0) || (m12 < 0))
		{
			snprintf(why, (size_t)whySize, "more than %d vertices with the midpoints", EXT_COLL_VERTEX_MAX);
			goto Fail;
		}
		q = &src[nq];
		// pageBase is 0 without vertex pages: the indices as before
		va -= pageBase;
		vb -= pageBase;
		vc -= pageBase;
		m01 -= pageBase;
		m02 -= pageBase;
		m12 -= pageBase;
		if (srcBase != NULL)
		{
			srcBase[nq] = (u32)pageBase;
		}
		q->index[0] = (u16)va;
		q->index[1] = (u16)vc;
		q->index[2] = (u16)vb;
		q->index[3] = (u16)vb;
		q->index[4] = (u16)m01;
		q->index[5] = (u16)m02;
		q->index[6] = (u16)m12;
		q->index[7] = (u16)vb;
		q->index[8] = (u16)vb;
		if (t->cls == EXT_COLL_CLASS_GROUND)
		{
			q->quadFlags = EXT_COLL_FLAGS_GROUND;
		}
		else if (t->cls == EXT_COLL_CLASS_WALL)
		{
			q->quadFlags = t->wallNoScrub ? EXT_COLL_FLAGS_WALL_NOSCRUB : EXT_COLL_FLAGS_WALL_SOLID;
		}
		else if (t->cls == EXT_COLL_CLASS_TRIGGER)
		{
			q->quadFlags = EXT_COLL_FLAGS_TRIGGER;
		}
		else
		{
			q->quadFlags = EXT_COLL_FLAGS_KILL;
		}
		q->draw_order_low = (t->doubleSided && (t->cls != EXT_COLL_CLASS_TRIGGER)) ? QUADBLOCK_DRAW_ORDER_LOW_DOUBLE_SIDED : 0;
		q->terrain_type = t->terrain;
		q->checkpointIndex = (t->cls == EXT_COLL_CLASS_GROUND) ? t->checkpoint : EXT_COLL_CP_NONE;
		q->pvs = NULL;
		for (int k = 0; k < 7; k++)
		{
			const SVec3 p = vs.verts[pageBase + q->index[k]].pos;

			if (k == 0)
			{
				q->bbox.min = p;
				q->bbox.max = p;
				continue;
			}
			if (p.x < q->bbox.min.x)
			{
				q->bbox.min.x = p.x;
			}
			if (p.y < q->bbox.min.y)
			{
				q->bbox.min.y = p.y;
			}
			if (p.z < q->bbox.min.z)
			{
				q->bbox.min.z = p.z;
			}
			if (p.x > q->bbox.max.x)
			{
				q->bbox.max.x = p.x;
			}
			if (p.y > q->bbox.max.y)
			{
				q->bbox.max.y = p.y;
			}
			if (p.z > q->bbox.max.z)
			{
				q->bbox.max.z = p.z;
			}
		}
		if (!ExtColl_Dividends(q, vs.verts + pageBase))
		{
			snprintf(why, (size_t)whySize, "triangle %d: degenerate, or no collision normal fits 32 bits", i);
			goto Fail;
		}
		if ((nq == 0) || (q->triNormalVecBitShift < s_extColl.shiftMin))
		{
			s_extColl.shiftMin = q->triNormalVecBitShift;
		}
		if ((nq == 0) || (q->triNormalVecBitShift > s_extColl.shiftMax))
		{
			s_extColl.shiftMax = q->triNormalVecBitShift;
		}
		keys[nq].quad = nq;
		srcZone[nq] = (t->cls == EXT_COLL_CLASS_GROUND) ? t->zone : 0;
		s_extColl.zoneRecords += (srcZone[nq] != 0) ? 1 : 0;
		for (int k = 0; k < 3; k++)
		{
			keys[nq].c[k] = (s32)t->p[0][k] + t->p[1][k] + t->p[2][k];
		}
		s_extColl.countClass[t->cls]++;
		if ((t->cls == EXT_COLL_CLASS_GROUND) && (t->terrain < EXT_COLL_TERRAINS))
		{
			s_extColl.countTerrain[t->terrain]++;
		}
		if ((t->cls == EXT_COLL_CLASS_TRIGGER) && (t->pad < EXT_COLL_PADS_MAX))
		{
			ExtCollPad *pad = &s_extColl.pads[t->pad];

			if (pad->records == 0)
			{
				pad->box = q->bbox;
			}
			else
			{
				ExtColl_GrowBox(&pad->box, &q->bbox);
			}
			pad->records++;
			pad->bits |= t->terrain;
			if (t->pad + 1 > s_extColl.numPads)
			{
				s_extColl.numPads = t->pad + 1;
			}
		}
		nq++;
	}

	tr.keys = keys;
	tr.src = src;
	tr.srcZone = srcZone;
	tr.srcBase = srcBase;
	// the root is always a branch
	if (ExtColl_BuildNode(&tr, 0, nq, 0, 1) < 0)
	{
		snprintf(why, (size_t)whySize, "more than %d tree nodes", EXT_COLL_NODE_MAX);
		goto Fail;
	}

	s_extColl.quads = tr.dst;
	s_extColl.recZone = tr.dstZone;
	s_extColl.recBase = tr.dstBase;
	s_extColl.numPages = pages ? numPages : 0;
	s_extColl.verts = vs.verts;
	s_extColl.bsp = tr.nodes;
	s_extColl.numQuads = nq;
	s_extColl.numVerts = vs.num;
	s_extColl.numBsp = tr.numNodes;
	s_extColl.treeDepth = tr.maxDepth;
	s_extColl.waterLeaves = tr.waterLeaves;
	s_extColl.killTopY = -32768;
	for (int i = 0; i < nq; i++)
	{
		if ((tr.dst[i].quadFlags == EXT_COLL_FLAGS_KILL) && (tr.dst[i].bbox.max.y > s_extColl.killTopY))
		{
			s_extColl.killTopY = tr.dst[i].bbox.max.y;
		}
	}
	memset(&s_extColl.mesh, 0, sizeof(s_extColl.mesh));
	s_extColl.mesh.numQuadBlock = nq;
	s_extColl.mesh.numVertex = vs.num;
	s_extColl.mesh.ptrQuadBlockArray = tr.dst;
	s_extColl.mesh.ptrVertexArray = vs.verts;
	s_extColl.mesh.bspRoot = tr.nodes;
	s_extColl.mesh.numBspNodes = tr.numNodes;
	s_extColl.heapKept = (size_t)numSolid * sizeof(struct QuadBlock) + (size_t)vs.max * sizeof(struct LevVertex) + (size_t)EXT_COLL_NODE_MAX * sizeof(struct BSP);
	s_extColl.heapPeak = s_extColl.heapKept + (size_t)vs.cap * sizeof(ExtCollVSlot) + (size_t)numSolid * (sizeof(struct QuadBlock) + sizeof(ExtCollKey));
	s_extColl.heapKept += (size_t)numSolid;
	s_extColl.heapPeak += 2 * (size_t)numSolid;
	if (pages)
	{
		s_extColl.heapKept += (size_t)numSolid * sizeof(u32);
		s_extColl.heapPeak += 2 * (size_t)numSolid * sizeof(u32);
	}
	s_extColl.ready = 1;
	free(vs.slots);
	free(src);
	free(keys);
	free(srcZone);
	free(srcBase);
	return 1;

Fail:
	free(vs.slots);
	free(vs.verts);
	free(src);
	free(keys);
	free(srcZone);
	free(srcBase);
	free(tr.dst);
	free(tr.dstZone);
	free(tr.dstBase);
	free(tr.nodes);
	ExtColl_Free();
	return 0;
}

// ---------------------------------------------------------------------------
// collision.glb

internal const char *ExtColl_NodeName(const cgltf_node *node)
{
	return (node->name != NULL) ? node->name : "?";
}

// The node's extras through the JSON reader of the external track.
internal int ExtColl_ReadExtras(const cgltf_node *node, ExtCollTri *proto, char *why, int whySize)
{
	ExtJsonDoc doc;
	char jwhy[128];
	const char *cls;
	double terrain;

	memset(&doc, 0, sizeof(doc));
	if ((node->extras.data == NULL) || !ExtJson_Parse(&doc, node->extras.data, strlen(node->extras.data), jwhy, (int)sizeof(jwhy)))
	{
		snprintf(why, (size_t)whySize, "node %s: no readable extras", ExtColl_NodeName(node));
		return 0;
	}
	cls = ExtJson_String(&doc, ExtJson_Get(&doc, 0, "collision"), "");
	terrain = ExtJson_Number(&doc, ExtJson_Get(&doc, 0, "terrain"), -1.0);
	proto->cls = (strcmp(cls, "ground") == 0) ? EXT_COLL_CLASS_GROUND
	           : (strcmp(cls, "wall") == 0)   ? EXT_COLL_CLASS_WALL
	           : (strcmp(cls, "kill") == 0)   ? EXT_COLL_CLASS_KILL
	           : (strcmp(cls, "none") == 0)   ? EXT_COLL_CLASS_NONE
	           : (strcmp(cls, "trigger") == 0) ? EXT_COLL_CLASS_TRIGGER
	                                          : 0xFF;
	proto->doubleSided = (u8)ExtJson_Bool(&doc, ExtJson_Get(&doc, 0, "double_sided"), 0);
	proto->wallNoScrub = (u8)(strcmp(ExtJson_String(&doc, ExtJson_Get(&doc, 0, "wall_scrub"), "none"), "solid") != 0);
	if (proto->cls == EXT_COLL_CLASS_TRIGGER)
	{
		// a turbo pad: its trigger bits (1 turbo pad, 2 super pad - nothing else,
		// the other bits steer hubs and weapons) and its number
		const double bits = ExtJson_Number(&doc, ExtJson_Get(&doc, 0, "trigger_bits"), -1.0);
		const double pad = ExtJson_Number(&doc, ExtJson_Get(&doc, 0, "pad"), 0.0);

		if (((bits != 1.0) && (bits != 2.0)) || (pad < 0.0) || (pad >= (double)EXT_COLL_PADS_MAX) || (pad != floor(pad)))
		{
			snprintf(why, (size_t)whySize, "node %s: trigger bits %g / pad %g not accepted (bits 1 or 2, pad 0..%d)", ExtColl_NodeName(node), bits, pad,
			         EXT_COLL_PADS_MAX - 1);
			ExtJson_Free(&doc);
			return 0;
		}
		ExtJson_Free(&doc);
		proto->terrain = (u8)bits;
		proto->pad = (u8)pad;
		return 1;
	}
	if ((proto->cls == 0xFF) || (terrain < 0.0) || (terrain > 20.0) || (terrain != floor(terrain)) || ((int)terrain == TERRAIN_TRACK) ||
	    ((int)terrain == TERRAIN_MUD))
	{
		snprintf(why, (size_t)whySize, "node %s: class '%s' / terrain %g not accepted", ExtColl_NodeName(node), cls, terrain);
		ExtJson_Free(&doc);
		return 0;
	}
	{
		// the anti-gravity zone of a ground node (physics.zones ids 1..255)
		const double zone = ExtJson_Number(&doc, ExtJson_Get(&doc, 0, "zone"), 0.0);

		if ((zone < 0.0) || (zone > 255.0) || (zone != floor(zone)) || ((zone != 0.0) && (proto->cls != EXT_COLL_CLASS_GROUND)))
		{
			snprintf(why, (size_t)whySize, "node %s: zone %g not accepted (0..255, ground only)", ExtColl_NodeName(node), zone);
			ExtJson_Free(&doc);
			return 0;
		}
		proto->zone = (u8)zone;
	}
	ExtJson_Free(&doc);
	proto->terrain = (u8)terrain;
	return 1;
}

// All triangles of the file; NULL with why when refused. *outCount = triangles.
internal ExtCollTri *ExtColl_ReadGlb(const char *path, int *outCount, char *why, int whySize)
{
	cgltf_options opt;
	cgltf_data *gltf = NULL;
	ExtCollTri *tris = NULL;
	int capacity = 0;
	int nt = 0;
	int ok = 1;

	memset(&opt, 0, sizeof(opt));
	if ((cgltf_parse_file(&opt, path, &gltf) != cgltf_result_success) || (cgltf_load_buffers(&opt, gltf, path) != cgltf_result_success))
	{
		snprintf(why, (size_t)whySize, "cannot be read as a glTF binary");
		if (gltf != NULL)
		{
			cgltf_free(gltf);
		}
		return NULL;
	}
	for (cgltf_size n = 0; n < gltf->nodes_count; n++)
	{
		const cgltf_node *node = &gltf->nodes[n];

		if ((node->mesh != NULL) && (node->mesh->primitives_count >= 1) && (node->mesh->primitives[0].indices != NULL))
		{
			capacity += (int)(node->mesh->primitives[0].indices->count / 3);
		}
	}
	tris = (ExtCollTri *)calloc((size_t)((capacity > 0) ? capacity : 1), sizeof(ExtCollTri));
	if (tris == NULL)
	{
		snprintf(why, (size_t)whySize, "out of memory");
		ok = 0;
	}
	for (cgltf_size n = 0; ok && (n < gltf->nodes_count); n++)
	{
		const cgltf_node *node = &gltf->nodes[n];
		const cgltf_primitive *prim;
		const cgltf_accessor *apos = NULL;
		const cgltf_accessor *acp = NULL;
		ExtCollTri proto;

		if (node->mesh == NULL)
		{
			continue;
		}
		if (node->has_matrix || node->has_translation || node->has_rotation || node->has_scale || (node->parent != NULL))
		{
			snprintf(why, (size_t)whySize, "node %s has a transform (the positions must be world units)", ExtColl_NodeName(node));
			ok = 0;
			break;
		}
		memset(&proto, 0, sizeof(proto));
		if (!ExtColl_ReadExtras(node, &proto, why, whySize))
		{
			ok = 0;
			break;
		}
		if (node->mesh->primitives_count != 1)
		{
			snprintf(why, (size_t)whySize, "node %s: %d primitives, one expected", ExtColl_NodeName(node), (int)node->mesh->primitives_count);
			ok = 0;
			break;
		}
		prim = &node->mesh->primitives[0];
		for (cgltf_size a = 0; a < prim->attributes_count; a++)
		{
			if (prim->attributes[a].type == cgltf_attribute_type_position)
			{
				apos = prim->attributes[a].data;
			}
			else if ((prim->attributes[a].name != NULL) && (strcmp(prim->attributes[a].name, "_CHECKPOINT") == 0))
			{
				acp = prim->attributes[a].data;
			}
		}
		if ((apos == NULL) || (prim->indices == NULL) || (prim->type != cgltf_primitive_type_triangles))
		{
			snprintf(why, (size_t)whySize, "node %s: no indexed TRIANGLES with POSITION", ExtColl_NodeName(node));
			ok = 0;
			break;
		}
		for (cgltf_size k = 0; ok && ((k + 2) < prim->indices->count); k += 3)
		{
			ExtCollTri *t = &tris[nt];
			float cp[3] = {255.0f, 255.0f, 255.0f};

			*t = proto;
			for (int j = 0; (j < 3) && ok; j++)
			{
				const cgltf_size vi = cgltf_accessor_read_index(prim->indices, k + (cgltf_size)j);
				float p[3] = {0.0f, 0.0f, 0.0f};

				if ((vi >= apos->count) || !cgltf_accessor_read_float(apos, vi, p, 3))
				{
					snprintf(why, (size_t)whySize, "node %s: an index outside the positions", ExtColl_NodeName(node));
					ok = 0;
					break;
				}
				for (int a = 0; a < 3; a++)
				{
					if (!((p[a] >= -32767.0f) && (p[a] <= 32767.0f)) || (p[a] != floorf(p[a])))
					{
						snprintf(why, (size_t)whySize, "node %s: a position is not an s16 integer", ExtColl_NodeName(node));
						ok = 0;
						break;
					}
					t->p[j][a] = (s16)p[a];
				}
				if (ok && (acp != NULL))
				{
					cgltf_accessor_read_float(acp, vi, &cp[j], 1);
				}
			}
			if (ok && ((cp[0] != cp[1]) || (cp[0] != cp[2]) || (cp[0] < 0.0f) || (cp[0] > 255.0f) || (cp[0] != floorf(cp[0]))))
			{
				snprintf(why, (size_t)whySize, "node %s: _CHECKPOINT is not one value 0..255 on a triangle", ExtColl_NodeName(node));
				ok = 0;
			}
			t->checkpoint = (u8)cp[0];
			nt++;
		}
	}
	cgltf_free(gltf);
	if (!ok)
	{
		free(tris);
		return NULL;
	}
	*outCount = nt;
	return tris;
}

// ---------------------------------------------------------------------------
// probes with the game's own functions (selftest and the check after a load)

global_variable u32 s_extCollScratch[CTR_SCRATCHPAD_SIZE / 4];

// The game's downward ray at (x, z) from y + 0x80 to y - 0x100, set up as
// BOTS.c does for a nav point but with the player's flags (GROUND |
// COLLISION_SURFACE, NO_COLLISION_RESPONSE ignored). 1 and the hit when hit.
// The scratchpad base must point at s_extCollScratch (the callers set it).
internal int ExtColl_Probe(s16 x, s16 y, s16 z, s16 *hitY, SVec3 *normal)
{
	struct ScratchpadStruct *sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);
	SVec3 lower = {.x = x, .y = (s16)(y - 0x100), .z = z};
	SVec3 upper = {.x = x, .y = (s16)(y + 0x80), .z = z};

	sps->ptr_mesh_info = &s_extColl.mesh;
	sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE;
	sps->Union.QuadBlockColl.quadFlagsIgnored = QUADBLOCK_FLAG_NO_COLLISION_RESPONSE;
	sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
	COLL_SearchBSP_CallbackQUADBLK(&lower, &upper, sps, 0);
	if (sps->boolDidTouchQuadblock == 0)
	{
		return 0;
	}
	*hitY = sps->Union.QuadBlockColl.hitPos.y;
	*normal = sps->hit.plane.normal;
	return 1;
}

typedef struct
{
	int records;
	int normalsBad;  // a game normal more than 1 degree off the triangle's
	int lengthBad;   // fine length outside 4096 +-2 %, coarse outside 1024 +-2 %
	int overflow;    // a product of the game's normal over 32 bits
	int notReached;  // a record the tree search does not offer for its own box
	int points;      // ray: lattice points tested
	int misses;      // ray: no hit
	int below;       // ray: the hit lies more than 1 unit under the record's own surface
	int above;       // ray: a higher face within the window wins (layers) - counted only
	int skippedEdge; // ray: lattice points closer than 1 unit to the record's edges
	int offExact;    // ray: on the game's plane, but more than 1 unit from the exact surface (counted only)
	int edgeMisses;  // ray: no hit, but closer than EXT_COLL_EDGE_SLACK to an edge of the four high-LOD triangles:
	                 // the game's barycentric test in fixed point (COLL_FIXED_TRIANGL_TestPoint, >> 6) leaves
	                 // such seams on small triangles - counted and reported, not a failure
	int zoneSteep;   // ray: zone records steeper than EXT_COLL_ZONE_RAY_MIN_NY, not tested by the vertical ray (counted)
	int edgeBelow;   // ray: more than 1 under the record's plane, but within EXT_COLL_EDGE_SLACK of an edge (the same seam,
	                 // a neighbour's plane wins); with edgeMisses at most EXT_COLL_SEAM_MAX_FRACTION of the points
	double planeOffMax; // largest height difference of the game's stored plane from the exact surface
	double planeOffNormalMax; // the same along the record's normal (height difference times |n.y|)
	double normalWorst;  // largest angle of a game normal from its triangle (degrees)
	int normalsBadEdge[4]; // the records with a normal off, by their shortest edge: < 16, < 64, < 256, >= 256 units
	double sampleEdge[8];  // per sample: the distance to the nearest edge of the four high-LOD triangles
	int sampleCount; // the first EXT_COLL_SAMPLES misses / below hits, for the report
	struct
	{
		int x, z, record;
		double y;
		int hit;
		int hitY;
	} sample[8];
} ExtCollCheck;

global_variable const struct QuadBlock *s_extCollTarget;
global_variable int s_extCollTargetFound;

internal void ExtColl_FindLeaf(struct BSP *leaf, struct ScratchpadStruct *sps)
{
	(void)sps;
	if ((s_extCollTarget >= leaf->data.leaf.ptrQuadBlockArray) && (s_extCollTarget < (leaf->data.leaf.ptrQuadBlockArray + leaf->data.leaf.numQuads)))
	{
		s_extCollTargetFound = 1;
	}
}

internal double ExtColl_Angle(const double a[3], const double b[3])
{
	const double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	const double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
	double c;

	if ((la <= 0.0) || (lb <= 0.0))
	{
		return 180.0;
	}
	c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
	c = (c > 1.0) ? 1.0 : ((c < -1.0) ? -1.0 : c);
	return acos(c) * 180.0 / 3.14159265358979323846;
}

internal double ExtColl_SubEdgeDistance(const struct QuadBlock *q, int x, int z)
{
	double best = 1e30;

	for (int t = 0; t < 4; t++)
	{
		for (int k = 0; k < 3; k++)
		{
			const SVec3 a = EXT_COLL_RV(q)[q->index[s_extCollTri[t][k]]].pos;
			const SVec3 b = EXT_COLL_RV(q)[q->index[s_extCollTri[t][(k + 1) % 3]]].pos;
			const double dx = (double)b.x - a.x, dz = (double)b.z - a.z, ll = dx * dx + dz * dz;
			double tt = (ll > 0.0) ? ((((double)x - a.x) * dx + ((double)z - a.z) * dz) / ll) : 0.0;
			double dist;

			tt = (tt < 0.0) ? 0.0 : ((tt > 1.0) ? 1.0 : tt);
			dist = hypot((double)x - (a.x + tt * dx), (double)z - (a.z + tt * dz));
			if (dist < best)
			{
				best = dist;
			}
		}
	}
	return best;
}

internal void ExtColl_Sample(ExtCollCheck *ck, int x, int z, double y, int record, int hit, int hitY)
{
	if (ck->sampleCount < (int)(sizeof(ck->sample) / sizeof(ck->sample[0])))
	{
		ck->sampleEdge[ck->sampleCount] = ExtColl_SubEdgeDistance(&s_extColl.quads[record], x, z);
		ck->sample[ck->sampleCount].x = x;
		ck->sample[ck->sampleCount].z = z;
		ck->sample[ck->sampleCount].y = y;
		ck->sample[ck->sampleCount].record = record;
		ck->sample[ck->sampleCount].hit = hit;
		ck->sample[ck->sampleCount].hitY = hitY;
		ck->sampleCount++;
	}
}

internal void ExtColl_PrintSamples(const ExtCollCheck *ck, int toLog)
{
	for (int i = 0; i < ck->sampleCount; i++)
	{
		const struct QuadBlock *q = &s_extColl.quads[ck->sample[i].record];
		char line[256];

		snprintf(line, sizeof(line), "ray at x %d z %d over record %d (flags 0x%04x, y %.1f, %.2f from a triangle edge): %s %d\n", ck->sample[i].x,
		         ck->sample[i].z, ck->sample[i].record, (unsigned)q->quadFlags, ck->sample[i].y, ck->sampleEdge[i], ck->sample[i].hit ? "hit at y" : "no hit",
		         ck->sample[i].hitY);
		if (toLog)
		{
			Platform_Log("[CTR ExtColl] %s", line);
		}
		else
		{
			printf("ext selftest coll: %s", line);
		}
	}
}

// The ray over one ground record on a lattice of step units.
// The plane the game itself stores for a high-LOD triangle: n . p = 8192 * halfDistance
// (COLL_FIXED_TRIANGL_GetNormVec keeps (n . v1 >> 12) >> 1, the ray compares
// n . p with halfDistance << 13). Its height at (x, z); 0 when n.y is 0.
internal int ExtColl_GamePlaneY(const struct CollPlane *pl, int x, int z, double *y)
{
	if (pl->normal.y == 0)
	{
		return 0;
	}
	*y = (8192.0 * pl->halfDistance - (double)pl->normal.x * x - (double)pl->normal.z * z) / (double)pl->normal.y;
	return 1;
}

// Which of the four high-LOD triangles of a record holds (x, z): 0..3, -1 none.
internal int ExtColl_SubTriangle(const struct QuadBlock *q, int x, int z)
{
	for (int t = 0; t < 4; t++)
	{
		const SVec3 a = EXT_COLL_RV(q)[q->index[s_extCollTri[t][0]]].pos;
		const SVec3 b = EXT_COLL_RV(q)[q->index[s_extCollTri[t][1]]].pos;
		const SVec3 c = EXT_COLL_RV(q)[q->index[s_extCollTri[t][2]]].pos;
		const double d = (double)(b.z - c.z) * (a.x - c.x) + (double)(c.x - b.x) * (a.z - c.z);
		double l0, l1;

		if (d == 0.0)
		{
			continue;
		}
		l0 = ((double)(b.z - c.z) * (x - c.x) + (double)(c.x - b.x) * (z - c.z)) / d;
		l1 = ((double)(c.z - a.z) * (x - c.x) + (double)(a.x - c.x) * (z - c.z)) / d;
		if ((l0 >= -1e-9) && (l1 >= -1e-9) && ((1.0 - l0 - l1) >= -1e-9))
		{
			return t;
		}
	}
	return -1;
}

internal void ExtColl_CheckRay(ExtCollCheck *ck, const struct QuadBlock *q, const struct CollPlane planes[4], int step)
{
	const SVec3 p0 = EXT_COLL_RV(q)[q->index[0]].pos;
	const SVec3 p1 = EXT_COLL_RV(q)[q->index[1]].pos;
	const SVec3 p2 = EXT_COLL_RV(q)[q->index[2]].pos;
	const SVec3 e[3][2] = {{p0, p1}, {p1, p2}, {p2, p0}};
	const double d = (double)(p1.z - p2.z) * (p0.x - p2.x) + (double)(p2.x - p1.x) * (p0.z - p2.z);
	// |n.y| of the exact unit normal: a height difference times it is the
	// distance along the normal
	const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
	const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
	const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
	const double clen = sqrt(cx * cx + cy * cy + cz * cz);
	const double nyAbs = (clen > 0.0) ? fabs(cy) / clen : 0.0;
	int x0 = q->bbox.min.x;
	int z0 = q->bbox.min.z;

	if (d == 0.0)
	{
		return;
	}
	// the first lattice line at or after the box (a multiple of step)
	x0 = (x0 >= 0) ? (((x0 + step - 1) / step) * step) : (-((-x0) / step) * step);
	z0 = (z0 >= 0) ? (((z0 + step - 1) / step) * step) : (-((-z0) / step) * step);
	for (int x = x0; x <= q->bbox.max.x; x += step)
	{
		for (int z = z0; z <= q->bbox.max.z; z += step)
		{
			const double l0 = ((double)(p1.z - p2.z) * (x - p2.x) + (double)(p2.x - p1.x) * (z - p2.z)) / d;
			const double l1 = ((double)(p2.z - p0.z) * (x - p2.x) + (double)(p0.x - p2.x) * (z - p2.z)) / d;
			const double l2 = 1.0 - l0 - l1;
			double y;
			double yGame = 0.0;
			int sub;
			int nearEdge = 0;
			s16 hy = 0;
			SVec3 hn;

			if ((l0 < 0.0) || (l1 < 0.0) || (l2 < 0.0))
			{
				continue;
			}
			for (int k = 0; k < 3; k++)
			{
				const double ax = e[k][0].x, az = e[k][0].z, dx = e[k][1].x - ax, dz = e[k][1].z - az;
				const double ll = dx * dx + dz * dz;
				double tt = (ll > 0.0) ? ((((double)x - ax) * dx + ((double)z - az) * dz) / ll) : 0.0;

				tt = (tt < 0.0) ? 0.0 : ((tt > 1.0) ? 1.0 : tt);
				if (hypot((double)x - (ax + tt * dx), (double)z - (az + tt * dz)) < 1.0)
				{
					nearEdge = 1;
				}
			}
			if (nearEdge)
			{
				ck->skippedEdge++;
				continue;
			}
			y = l0 * p0.y + l1 * p1.y + l2 * p2.y;
			sub = ExtColl_SubTriangle(q, x, z);
			ck->points++;
			if ((sub < 0) || !ExtColl_GamePlaneY(&planes[sub], x, z, &yGame))
			{
				ck->misses++;
				ExtColl_Sample(ck, x, z, y, (int)(q - s_extColl.quads), 0, 0);
				continue;
			}
			// how far the plane the game stores lies from the record's exact surface
			if (fabs(y - yGame) > ck->planeOffMax)
			{
				ck->planeOffMax = fabs(y - yGame);
			}
			if (fabs(y - yGame) * nyAbs > ck->planeOffNormalMax)
			{
				ck->planeOffNormalMax = fabs(y - yGame) * nyAbs;
			}
			if (!ExtColl_Probe((s16)x, (s16)floor(y + 0.5), (s16)z, &hy, &hn))
			{
				if (ExtColl_SubEdgeDistance(q, x, z) < EXT_COLL_SEAM_MISS_SLACK)
				{
					ck->edgeMisses++;
				}
				else
				{
					ck->misses++;
				}
				ExtColl_Sample(ck, x, z, y, (int)(q - s_extColl.quads), 0, 0);
				continue;
			}
			// |dh| <= 1 against the game's own plane of the record; higher = another face on top
			if (hy < yGame - 1.0)
			{
				// within the slack of an edge: the seam case of the game's
				// fixed point (a neighbour's plane), counted apart
				if (ExtColl_SubEdgeDistance(q, x, z) < EXT_COLL_EDGE_SLACK)
				{
					ck->edgeBelow++;
				}
				else
				{
					ck->below++;
				}
				ExtColl_Sample(ck, x, z, yGame, (int)(q - s_extColl.quads), 1, hy);
			}
			else if (hy > yGame + 1.0)
			{
				ck->above++;
			}
			else if (fabs(hy - y) > 1.0)
			{
				ck->offExact++;
			}
		}
	}
}

// Every record: the game's normals (COLL_FIXED_QUADBLK_GetNormVecs_*) against the
// float normal of its triangle, the tree search for its own box, and for a
// ground record the game's ray on a lattice inside it. Own scratchpad, restored.
internal void ExtColl_CheckAll(ExtCollCheck *ck, int step)
{
	u8 *const savedScratch = gCTRNativeScratchpadBase;
	struct ScratchpadStruct *sps;
	const int hiV1[4] = {0, 4, 6, 5};

	memset(ck, 0, sizeof(*ck));
	memset(s_extCollScratch, 0, sizeof(s_extCollScratch));
	gCTRNativeScratchpadBase = (u8 *)s_extCollScratch;
	sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);

	for (int i = 0; i < s_extColl.numQuads; i++)
	{
		struct QuadBlock *q = &s_extColl.quads[i];
		const SVec3 p0 = EXT_COLL_RV(q)[q->index[0]].pos;
		const SVec3 p1 = EXT_COLL_RV(q)[q->index[1]].pos;
		const SVec3 p2 = EXT_COLL_RV(q)[q->index[2]].pos;
		// the record's own normal in the game's sense: (p2 - p0) x (p1 - p0)
		const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
		const double wx = p2.x - p0.x, wy = p2.y - p0.y, wz = p2.z - p0.z;
		const double ref[3] = {wy * uz - wz * uy, wz * ux - wx * uz, wx * uy - wy * ux};
		struct CollPlane planes[4];

		ck->records++;

		// 1. the game's normals: high LOD (the first vertex of each triangle), low LOD
		sps->ptr_mesh_info = &s_extColl.mesh;
		COLL_FIXED_QUADBLK_GetNormVecs_HiLOD(sps, q);
		for (int t = 0; t < 4; t++)
		{
			planes[t] = sps->bspSearchVert[hiV1[t]].plane;
		}
		for (int t = 0; t < 4; t++)
		{
			const SVec3 n = sps->bspSearchVert[hiV1[t]].plane.normal;
			const double nv[3] = {n.x, n.y, n.z};
			const double len = sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
			const double angle = ExtColl_Angle(nv, ref);

			if (angle > ck->normalWorst)
			{
				ck->normalWorst = angle;
			}
			if (angle > 1.0)
			{
				const double e0 = sqrt(ux * ux + uy * uy + uz * uz);
				const double e1 = sqrt(wx * wx + wy * wy + wz * wz);
				const double e2 = sqrt((wx - ux) * (wx - ux) + (wy - uy) * (wy - uy) + (wz - uz) * (wz - uz));
				const double shortest = (e0 < e1) ? ((e0 < e2) ? e0 : e2) : ((e1 < e2) ? e1 : e2);

				ck->normalsBad++;
				ck->normalsBadEdge[(shortest < 16.0) ? 0 : ((shortest < 64.0) ? 1 : ((shortest < 256.0) ? 2 : 3))]++;
			}
			if ((len < 4096.0 * 0.98) || (len > 4096.0 * 1.02))
			{
				ck->lengthBad++;
			}
		}
		COLL_FIXED_QUADBLK_GetNormVecs_LoLOD(sps, q);
		{
			const SVec3 n = sps->bspSearchVert[0].plane.normal;
			const double nv[3] = {n.x, n.y, n.z};
			const double len = sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);

			if (ExtColl_Angle(nv, ref) > 1.0)
			{
				ck->normalsBad++;
			}
			if ((len < 1024.0 * 0.98) || (len > 1024.0 * 1.02))
			{
				ck->lengthBad++;
			}
		}
		for (int t = 0; t < 5; t++)
		{
			s32 c[3];

			ExtColl_GameCross(&EXT_COLL_RV(q)[q->index[s_extCollTri[t][0]]].pos, &EXT_COLL_RV(q)[q->index[s_extCollTri[t][1]]].pos,
			                  &EXT_COLL_RV(q)[q->index[s_extCollTri[t][2]]].pos, c);
			for (int k = 0; k < 3; k++)
			{
				const s64 product = (s64)(c[k] >> s_extCollTriLod[t]) * q->triNormalVecDividend[s_extCollTriDiv[t]];

				if ((product > 0x7FFFFFFFLL) || (product < -0x7FFFFFFFLL - 1))
				{
					ck->overflow++;
				}
			}
		}

		// 2. the tree offers the record for its own box
		{
			struct BoundingBox box = q->bbox;

			s_extCollTarget = q;
			s_extCollTargetFound = 0;
			COLL_SearchBSP_CallbackPARAM(s_extColl.mesh.bspRoot, &box, ExtColl_FindLeaf, sps);
			if (!s_extCollTargetFound)
			{
				ck->notReached++;
			}
		}

		// 3. the ray over a ground record whose surface faces up; a steep zone
		// record (n.y < 0.5) is left to the game's ray along the zone's normal
		// and only counted (as the converter's check does)
		if (((q->quadFlags & QUADBLOCK_FLAG_GROUND) != 0) && (ref[1] > 0.0) && (step > 0))
		{
			const double refLen = sqrt(ref[0] * ref[0] + ref[1] * ref[1] + ref[2] * ref[2]);

			if ((s_extColl.recZone != NULL) && (s_extColl.recZone[i] != 0) && (refLen > 0.0) && (ref[1] < EXT_COLL_ZONE_RAY_MIN_NY * refLen))
			{
				ck->zoneSteep++;
			}
			else
			{
				ExtColl_CheckRay(ck, q, planes, step);
			}
		}
	}
	gCTRNativeScratchpadBase = savedScratch;
}

internal int ExtColl_CheckPassed(const ExtCollCheck *ck)
{
	// seams (misses and below hits within the slack of an edge) at most 0.05 %
	// of the ray points together; everything else 0
	return (ck->normalsBad == 0) && (ck->lengthBad == 0) && (ck->overflow == 0) && (ck->notReached == 0) && (ck->misses == 0) && (ck->below == 0) &&
	       (ck->points > 0) && ((double)(ck->edgeMisses + ck->edgeBelow) <= EXT_COLL_SEAM_MAX_FRACTION * (double)ck->points);
}

// ---------------------------------------------------------------------------
// the turbo pads (records of class trigger), for the physics part's log

int ExtColl_PadCount(void)
{
	return s_extColl.ready ? s_extColl.numPads : 0;
}

int ExtColl_PadBox(int pad, struct BoundingBox *box, int *records, int *bits)
{
	if (!s_extColl.ready || (pad < 0) || (pad >= s_extColl.numPads) || (s_extColl.pads[pad].records == 0))
	{
		return 0;
	}
	*box = s_extColl.pads[pad].box;
	*records = s_extColl.pads[pad].records;
	*bits = s_extColl.pads[pad].bits;
	return 1;
}

int ExtColl_PadAt(int x, int y, int z, int margin)
{
	for (int p = 0; s_extColl.ready && (p < s_extColl.numPads); p++)
	{
		const struct BoundingBox *b = &s_extColl.pads[p].box;

		if ((s_extColl.pads[p].records > 0) && (x >= b->min.x - margin) && (x <= b->max.x + margin) && (y >= b->min.y - margin) &&
		    (y <= b->max.y + margin) && (z >= b->min.z - margin) && (z <= b->max.z + margin))
		{
			return p;
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------
// the anti-gravity zones (records of ground nodes with "zone": n) and a ray in
// any direction with the game's own ray test

int ExtColl_RecordZone(const struct QuadBlock *q)
{
	if (!s_extColl.ready || (s_extColl.recZone == NULL) || (q == NULL) || (q < s_extColl.quads) || (q >= s_extColl.quads + s_extColl.numQuads))
	{
		return 0;
	}
	return s_extColl.recZone[q - s_extColl.quads];
}

int ExtColl_ZoneRecords(void)
{
	return s_extColl.ready ? s_extColl.zoneRecords : 0;
}

int ExtColl_ProbeSurface(const SVec3 *from, const SVec3 *dir, int len, ExtCollHit *out)
{
	u8 *const savedScratch = gCTRNativeScratchpadBase;
	struct ScratchpadStruct *sps;
	SVec3 end;
	int hit = 0;

	if (!s_extColl.ready || (from == NULL) || (dir == NULL) || (out == NULL) || (len <= 0))
	{
		return 0;
	}
	end.x = (s16)(from->x + ((dir->x * len) >> 12));
	end.y = (s16)(from->y + ((dir->y * len) >> 12));
	end.z = (s16)(from->z + ((dir->z * len) >> 12));
	// an own scratchpad: the game's (and its search stack) stay untouched
	memset(s_extCollScratch, 0, sizeof(s_extCollScratch));
	gCTRNativeScratchpadBase = (u8 *)s_extCollScratch;
	sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);
	sps->ptr_mesh_info = &s_extColl.mesh;
	sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_COLLISION_SURFACE;
	sps->Union.QuadBlockColl.quadFlagsIgnored = QUADBLOCK_FLAG_NO_COLLISION_RESPONSE;
	sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
	// the game's ray runs from its second point to its first (COLL.c, ExtColl_Probe)
	COLL_SearchBSP_CallbackQUADBLK(&end, from, sps, 0);
	if ((sps->boolDidTouchQuadblock != 0) && (sps->hit.ptrQuadblock != NULL))
	{
		out->pos = sps->Union.QuadBlockColl.hitPos;
		out->normal = sps->hit.plane.normal;
		out->record = sps->hit.ptrQuadblock;
		out->zone = ExtColl_RecordZone(sps->hit.ptrQuadblock);
		out->terrain = sps->hit.ptrQuadblock->terrain_type;
		hit = 1;
	}
	gCTRNativeScratchpadBase = savedScratch;
	return hit;
}

// ---------------------------------------------------------------------------
// the line query (see the header): exact, both faces, over the leaves whose box
// the line's piece meets (the leaves are walked as an array: no search stack,
// no scratchpad)

internal int ExtColl_RecordLineClass(const struct QuadBlock *q)
{
	switch (q->quadFlags)
	{
	case EXT_COLL_FLAGS_GROUND:
		return EXT_COLL_LINE_GROUND;
	case EXT_COLL_FLAGS_WALL_NOSCRUB:
	case EXT_COLL_FLAGS_WALL_SOLID:
		return EXT_COLL_LINE_WALL;
	case EXT_COLL_FLAGS_KILL:
		return EXT_COLL_LINE_KILL;
	case EXT_COLL_FLAGS_TRIGGER:
		return EXT_COLL_LINE_TRIGGER;
	default:
		return 0;
	}
}

// The segment p + t d (t in [t0, t1]) against a box grown by 1: 1 = it may meet it.
internal int ExtColl_SegmentBox(const double p[3], const double d[3], double t0, double t1, const struct BoundingBox *b)
{
	const double lo[3] = {(double)b->min.x - 1.0, (double)b->min.y - 1.0, (double)b->min.z - 1.0};
	const double hi[3] = {(double)b->max.x + 1.0, (double)b->max.y + 1.0, (double)b->max.z + 1.0};

	for (int a = 0; a < 3; a++)
	{
		if (fabs(d[a]) < 1e-12)
		{
			if ((p[a] < lo[a]) || (p[a] > hi[a]))
			{
				return 0;
			}
			continue;
		}
		{
			double ta = (lo[a] - p[a]) / d[a];
			double tb = (hi[a] - p[a]) / d[a];

			if (ta > tb)
			{
				const double s = ta;
				ta = tb;
				tb = s;
			}
			t0 = (ta > t0) ? ta : t0;
			t1 = (tb < t1) ? tb : t1;
			if (t0 > t1)
			{
				return 0;
			}
		}
	}
	return 1;
}

int ExtColl_LineQuery(const double from[3], const double dir[3], double tMin, double tMax, int classes, int nearestAbs, ExtCollLineHit *out)
{
	double d[3];
	double len;
	int found = 0;
	double bestKey = 0.0;

	if (!s_extColl.ready || (from == NULL) || (dir == NULL) || (out == NULL) || (tMin > tMax))
	{
		return 0;
	}
	len = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
	if (len < 1e-9)
	{
		return 0;
	}
	d[0] = dir[0] / len;
	d[1] = dir[1] / len;
	d[2] = dir[2] / len;
	for (int b = 0; b < s_extColl.numBsp; b++)
	{
		const struct BSP *node = &s_extColl.bsp[b];

		if (((node->flag & BSP_NODE_FLAG_LEAF) == 0) || !ExtColl_SegmentBox(from, d, tMin, tMax, &node->box))
		{
			continue;
		}
		for (int r = 0; r < node->data.leaf.numQuads; r++)
		{
			const struct QuadBlock *q = &node->data.leaf.ptrQuadBlockArray[r];
			const int cls = ExtColl_RecordLineClass(q);
			// the record keeps the file's triangle (a, b, c) as corners (a, c, b):
			// v0 v1 v2 below are the file's a b c again, and the front
			// (counter-clockwise in the file) is (v1 - v0) x (v2 - v0)
			const SVec3 v0 = EXT_COLL_RV(q)[q->index[0]].pos;
			const SVec3 v1 = EXT_COLL_RV(q)[q->index[2]].pos;
			const SVec3 v2 = EXT_COLL_RV(q)[q->index[1]].pos;
			double e1[3], e2[3], h[3], s[3], qv[3], a, f, u, v, t, n[3], nl, key;

			if (((cls & classes) == 0) || !ExtColl_SegmentBox(from, d, tMin, tMax, &q->bbox))
			{
				continue;
			}
			e1[0] = (double)v1.x - v0.x;
			e1[1] = (double)v1.y - v0.y;
			e1[2] = (double)v1.z - v0.z;
			e2[0] = (double)v2.x - v0.x;
			e2[1] = (double)v2.y - v0.y;
			e2[2] = (double)v2.z - v0.z;
			// Moeller-Trumbore, both faces
			h[0] = d[1] * e2[2] - d[2] * e2[1];
			h[1] = d[2] * e2[0] - d[0] * e2[2];
			h[2] = d[0] * e2[1] - d[1] * e2[0];
			a = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
			if (fabs(a) < 1e-9)
			{
				continue;
			}
			f = 1.0 / a;
			s[0] = from[0] - v0.x;
			s[1] = from[1] - v0.y;
			s[2] = from[2] - v0.z;
			u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
			if ((u < -1e-9) || (u > 1.0 + 1e-9))
			{
				continue;
			}
			qv[0] = s[1] * e1[2] - s[2] * e1[1];
			qv[1] = s[2] * e1[0] - s[0] * e1[2];
			qv[2] = s[0] * e1[1] - s[1] * e1[0];
			v = f * (d[0] * qv[0] + d[1] * qv[1] + d[2] * qv[2]);
			if ((v < -1e-9) || (u + v > 1.0 + 1e-9))
			{
				continue;
			}
			t = f * (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]);
			if ((t < tMin) || (t > tMax))
			{
				continue;
			}
			key = nearestAbs ? fabs(t) : t;
			if (found && (key >= bestKey))
			{
				continue;
			}
			n[0] = e1[1] * e2[2] - e1[2] * e2[1];
			n[1] = e1[2] * e2[0] - e1[0] * e2[2];
			n[2] = e1[0] * e2[1] - e1[1] * e2[0];
			nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
			if (nl < 1e-9)
			{
				continue;
			}
			found = 1;
			bestKey = key;
			out->t = t;
			out->pos[0] = from[0] + t * d[0];
			out->pos[1] = from[1] + t * d[1];
			out->pos[2] = from[2] + t * d[2];
			out->normal[0] = n[0] / nl;
			out->normal[1] = n[1] / nl;
			out->normal[2] = n[2] / nl;
			out->front = (d[0] * n[0] + d[1] * n[1] + d[2] * n[2]) < 0.0;
			out->cls = cls;
			out->zone = ExtColl_RecordZone(q);
			out->terrain = q->terrain_type;
			out->recordIndex = (int)(q - s_extColl.quads);
			out->record = q;
		}
	}
	return found;
}

// ---------------------------------------------------------------------------
// the limits line: each fixed size against what the build used of it

internal void ExtColl_FormatLimits(char *out, size_t size)
{
	int cpMax = -1;

	for (int i = 0; i < s_extColl.numQuads; i++)
	{
		const int cp = s_extColl.quads[i].checkpointIndex;

		if ((cp != EXT_COLL_CP_NONE) && (cp > cpMax))
		{
			cpMax = cp;
		}
	}
	snprintf(out, size,
	         "limits: records %d/%d, vertices %d/%d, tree nodes %d/%d, depth %d (search stack %d), checkpoint index %d/%d, longest edge %.1f/%d, "
	         "dividend shift %d..%d, midpoint off its corners' middle up to %.1f per axis; heap %u bytes kept, %u at the build's peak",
	         s_extColl.numQuads, EXT_COLL_RECORD_MAX, s_extColl.numVerts, EXT_COLL_VERTEX_MAX, s_extColl.numBsp, EXT_COLL_NODE_MAX, s_extColl.treeDepth,
	         EXT_COLL_SEARCH_STACK, cpMax, EXT_COLL_CP_NONE - 1, s_extColl.maxEdge, EXT_COLL_MAX_EDGE, s_extColl.shiftMin, s_extColl.shiftMax,
	         0.5 * s_extColl.midOff2, (unsigned)s_extColl.heapKept, (unsigned)s_extColl.heapPeak);
	if (s_extColl.numPages > 0)
	{
		const size_t used = strlen(out);

		snprintf(out + used, (used < size) ? (size - used) : 0, "; vertex pages %d of at most %d vertices", s_extColl.numPages, EXT_COLL_VERTEX_MAX);
	}
}

internal void ExtColl_LogLimits(void)
{
	char line[512];

	ExtColl_FormatLimits(line, sizeof(line));
	Platform_Log("[CTR ExtColl] %s\n", line);
}

// ---------------------------------------------------------------------------
// the entry point of the dispatcher

void ExtColl_OnLevelLoaded(struct Level *lev)
{
	char path[ENGINE_EXTTRACK_PATH_MAX + 64];
	char why[256];
	const ExtJsonDoc *json = ExtTrack_Json();
	const int section = ExtTrack_Section("collision");
	const char *glb = (json != NULL) ? ExtJson_String(json, ExtJson_Get(json, section, "glb"), "collision.glb") : "collision.glb";
	const int wantTriangles = (json != NULL) ? (int)ExtJson_Number(json, ExtJson_Get(json, ExtJson_Get(json, section, "counts"), "triangles"), -1.0) : -1;
	ExtCollTri *tris;
	int numTris = 0;
	ExtCollCheck ck;

	(void)lev;
	if (s_extColl.tried)
	{
		if (s_extColl.ready)
		{
			Platform_Log("[CTR ExtColl] level load: the collision built at the first load is kept (%d records)\n", s_extColl.numQuads);
		}
		return;
	}
	s_extColl.tried = 1;
	why[0] = '\0';
	if (!ExtTrack_FilePath(glb, path, (int)sizeof(path)))
	{
		Platform_Log("[CTR ExtColl] refused: the path of '%s' is too long - the donor's collision stays\n", glb);
		return;
	}
	tris = ExtColl_ReadGlb(path, &numTris, why, (int)sizeof(why));
	s_extCollPagesAsked = (json != NULL) ? ExtJson_Bool(json, ExtJson_Get(json, section, "vertex_pages"), 0) : 0;
	if ((tris == NULL) || !ExtColl_Build(tris, numTris, why, (int)sizeof(why)))
	{
		s_extCollPagesAsked = 0;
		Platform_Log("[CTR ExtColl] refused '%s': %s - the donor's collision stays\n", path, why);
		free(tris);
		return;
	}
	s_extCollPagesAsked = 0;
	free(tris);
	Platform_Log("[CTR ExtColl] built from '%s': %d triangle(s) (json %d, %s) -> %d record(s): ground %d (in zones %d), wall %d, kill %d, trigger %d "
	             "(%d pad(s)); %d vertex(es), %d tree node(s), depth %d, %d water leaf(s)\n",
	             path, numTris, wantTriangles, (wantTriangles == numTris) ? "same" : "DIFFERENT", s_extColl.numQuads, s_extColl.countClass[EXT_COLL_CLASS_GROUND],
	             s_extColl.zoneRecords, s_extColl.countClass[EXT_COLL_CLASS_WALL], s_extColl.countClass[EXT_COLL_CLASS_KILL], s_extColl.countClass[EXT_COLL_CLASS_TRIGGER],
	             s_extColl.numPads, s_extColl.numVerts, s_extColl.numBsp, s_extColl.treeDepth, s_extColl.waterLeaves);
	ExtColl_LogLimits();
	{
		char line[512];
		size_t used = 0;

		line[0] = '\0';
		for (int t = 0; t < EXT_COLL_TERRAINS; t++)
		{
			if (s_extColl.countTerrain[t] > 0)
			{
				const int w = snprintf(&line[used], sizeof(line) - used, "%s%d:%d", (used > 0) ? " " : "", t, s_extColl.countTerrain[t]);

				if ((w < 0) || ((size_t)w >= sizeof(line) - used))
				{
					break;
				}
				used += (size_t)w;
			}
		}
		Platform_Log("[CTR ExtColl] ground terrains (id:records): %s\n", line);
	}
	// The selftest's checks on the real data with the game's own functions (own
	// scratchpad and own mesh: no game state is touched).
	ExtColl_CheckAll(&ck, 64);
	Platform_Log("[CTR ExtColl] check %s: %d record(s), normals off %d, length off %d, overflow %d, not in tree %d; ray %d point(s), %d miss(es), "
	             "%d seam miss(es) within %.1f of a triangle edge, %d seam below hit(s), %d below the game's plane, %d under a higher face, %d near a "
	             "record edge skipped, %d steep zone record(s) left to the zone's ray; the "
	             "game's planes lie up to %.2f from the exact surface in height, %.2f along the normal (%d hit(s) more than 1 off it)\n",
	             ExtColl_CheckPassed(&ck) ? "passed" : "FAILED", ck.records, ck.normalsBad, ck.lengthBad, ck.overflow, ck.notReached, ck.points, ck.misses,
	             ck.edgeMisses, EXT_COLL_SEAM_MISS_SLACK, ck.edgeBelow, ck.below, ck.above, ck.skippedEdge, ck.zoneSteep, ck.planeOffMax, ck.planeOffNormalMax,
	             ck.offExact);
	ExtColl_PrintSamples(&ck, 1);
}

// ---------------------------------------------------------------------------
// the frame log (from the dispatcher after the game logic of a frame)

internal void ExtColl_RunReport(void)
{
	char line[512];
	size_t used = 0;

	line[0] = '\0';
	for (int t = 0; t < EXT_COLL_TERRAINS; t++)
	{
		if (s_extCollRun.terrainFrames[t] > 0)
		{
			const int w = snprintf(&line[used], sizeof(line) - used, "%s%d:%d", (used > 0) ? " " : "", t, s_extCollRun.terrainFrames[t]);

			if ((w < 0) || ((size_t)w >= sizeof(line) - used))
			{
				break;
			}
			used += (size_t)w;
		}
	}
	Platform_Log("[CTR ExtColl] seat 0 after %d frame(s): on the ground %d, terrains (id:frames) %s; wall contact %d frame(s); lowest y %d at %d %d %d "
	             "(kill faces reach up to y %d)\n",
	             s_extCollRun.frames, s_extCollRun.groundFrames, (used > 0) ? line : "-", s_extCollRun.wallFrames, s_extCollRun.minY, s_extCollRun.minAt[0],
	             s_extCollRun.minAt[1], s_extCollRun.minAt[2], s_extColl.killTopY);
}

void ExtColl_OnLogicFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	int y;

	if (!s_extColl.ready || (gGT == NULL) || !ExtTrack_ActiveForLevel(gGT->levelID) || ((gGT->gameMode1 & MAIN_MENU) != 0))
	{
		return;
	}
	d = gGT->drivers[0];
	if (d == NULL)
	{
		return;
	}
	if (!s_extCollRun.armed)
	{
		s_extCollRun.armed = 1;
		s_extCollRun.minY = 0x7fffffff;
		Platform_AtExitReport(ExtColl_RunReport);
	}
	s_extCollRun.frames++;
	y = d->posCurr.y >> 8;
	if (y < s_extCollRun.minY)
	{
		s_extCollRun.minY = y;
		s_extCollRun.minAt[0] = d->posCurr.x >> 8;
		s_extCollRun.minAt[1] = y;
		s_extCollRun.minAt[2] = d->posCurr.z >> 8;
	}
	if ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0)
	{
		s_extCollRun.groundFrames++;
		if (d->currentTerrain < EXT_COLL_TERRAINS)
		{
			s_extCollRun.terrainFrames[d->currentTerrain]++;
		}
	}
	// a sweep hit this frame whose normal is steeper than 60 degrees: a wall
	if (((d->collisionFlags & DRIVER_COLL_FLAG_SURFACE_PUSHBACK) != 0) && (d->spsNormalVec.y < 2048) && (d->spsNormalVec.y > -2048))
	{
		s_extCollRun.wallFrames++;
		if (s_extCollRun.wallFrames == 1)
		{
			Platform_Log("[CTR ExtColl] seat 0 first wall contact at frame %d pos %d %d %d normal %d %d %d\n", s_extCollRun.frames, d->posCurr.x >> 8, y,
			             d->posCurr.z >> 8, d->spsNormalVec.x, d->spsNormalVec.y, d->spsNormalVec.z);
		}
	}
	if ((s_extCollRun.frames % 300) == 0)
	{
		ExtColl_RunReport();
	}
}

// ---------------------------------------------------------------------------
// --ext-selftest coll: a made-up track in code, no file, no game data

internal void ExtColl_TestCheck(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest coll: FAILED %s\n", what);
	}
}

internal void ExtColl_TestSquare(ExtCollTri *tris, int *n, s16 x0, s16 z0, s16 size, s16 y0, s16 y1, u8 terrain, u8 checkpoint)
{
	// (x0,z0) -> (x0,z1) -> (x1,z0) is counter-clockwise seen from +y; y0 at x0, y1 at x1
	const s16 x1 = (s16)(x0 + size);
	const s16 z1 = (s16)(z0 + size);
	const ExtCollTri a = {{{x0, y0, z0}, {x0, y0, z1}, {x1, y1, z0}}, EXT_COLL_CLASS_GROUND, terrain, checkpoint, 0, 0};
	const ExtCollTri b = {{{x1, y1, z0}, {x0, y0, z1}, {x1, y1, z1}}, EXT_COLL_CLASS_GROUND, terrain, checkpoint, 0, 0};

	tris[(*n)++] = a;
	tris[(*n)++] = b;
}

// The made-up track: a 2048 x 2048 floor at y = 0 in 512-squares, a ramp east
// of it rising 128 per 512, a bridge deck 200 over the floor's middle, a wall on
// the west edge, a kill plane under the floor and one 'none' triangle - every
// ground triangle counter-clockwise from above, as the converter writes them.
internal int ExtColl_TestTrack(ExtCollTri *tris)
{
	int n = 0;

	memset(tris, 0, EXT_COLL_TEST_TRIS * sizeof(ExtCollTri));
	for (int i = 0; i < 4; i++)
	{
		for (int k = 0; k < 4; k++)
		{
			ExtColl_TestSquare(tris, &n, (s16)(-1024 + 512 * i), (s16)(-1024 + 512 * k), 512, 0, 0, TERRAIN_ASPHALT, 0);
		}
	}
	for (int i = 0; i < 2; i++)
	{
		for (int k = 0; k < 2; k++)
		{
			ExtColl_TestSquare(tris, &n, (s16)(1024 + 512 * i), (s16)(-512 + 512 * k), 512, (s16)(128 * i), (s16)(128 * (i + 1)), TERRAIN_DIRT, 1);
		}
	}
	{
		const ExtCollTri d1 = {{{-256, 200, -128}, {-256, 200, 128}, {256, 200, -128}}, EXT_COLL_CLASS_GROUND, TERRAIN_WOOD, 2, 0, 0};
		const ExtCollTri d2 = {{{256, 200, -128}, {-256, 200, 128}, {256, 200, 128}}, EXT_COLL_CLASS_GROUND, TERRAIN_WOOD, 2, 0, 0};
		const ExtCollTri w1 = {{{-1024, 0, -1024}, {-1024, 300, -1024}, {-1024, 0, -300}}, EXT_COLL_CLASS_WALL, TERRAIN_ASPHALT, EXT_COLL_CP_NONE, 1, 1};
		const ExtCollTri k1 = {{{-1024, -400, -1024}, {-1024, -400, -512}, {-512, -400, -1024}}, EXT_COLL_CLASS_KILL, TERRAIN_ASPHALT, EXT_COLL_CP_NONE, 1, 0};
		const ExtCollTri none = {{{0, 500, 0}, {0, 500, 100}, {100, 500, 0}}, EXT_COLL_CLASS_NONE, TERRAIN_ASPHALT, EXT_COLL_CP_NONE, 0, 0};

		tris[n++] = d1;
		tris[n++] = d2;
		tris[n++] = w1;
		tris[n++] = k1;
		tris[n++] = none;
	}
	return n;
}

int ExtColl_Selftest(void)
{
	ExtCollTri tris[EXT_COLL_TEST_TRIS];
	char why[256];
	char what[256];
	int checks = 0;
	int failures = 0;
	const int n = ExtColl_TestTrack(tris);
	ExtCollCheck ck;

	why[0] = '\0';
	ExtColl_TestCheck(ExtColl_Build(tris, n, why, (int)sizeof(why)), "the made-up track builds", &checks, &failures);
	if (!s_extColl.ready)
	{
		printf("ext selftest coll: build refused: %s\n", why);
		printf("ext selftest coll FAILED: %d checks, %d failures\n", checks, failures);
		return 1;
	}

	// structure
	ExtColl_TestCheck(s_extColl.numQuads == n - 1, "one record per solid triangle (the 'none' one left out)", &checks, &failures);
	ExtColl_TestCheck((s_extColl.countClass[EXT_COLL_CLASS_GROUND] == 42) && (s_extColl.countClass[EXT_COLL_CLASS_WALL] == 1) &&
	                      (s_extColl.countClass[EXT_COLL_CLASS_KILL] == 1),
	                  "class counts ground 42, wall 1, kill 1", &checks, &failures);
	ExtColl_TestCheck((s_extColl.bsp[0].flag & BSP_NODE_FLAG_LEAF) == 0, "the root is a branch", &checks, &failures);
	ExtColl_TestCheck((s_extColl.mesh.bspRoot == s_extColl.bsp) && (s_extColl.mesh.ptrQuadBlockArray == s_extColl.quads) &&
	                      (s_extColl.mesh.ptrVertexArray == s_extColl.verts) && (s_extColl.mesh.numQuadBlock == s_extColl.numQuads),
	                  "the mesh_info points at the records", &checks, &failures);
	{
		int empty = 0;
		int flagsOk = 1;
		int pvsNull = 1;
		int oneTri = 1;

		for (int i = 0; i < s_extColl.numBsp; i++)
		{
			const struct BSP *b = &s_extColl.bsp[i];

			if (((b->flag & BSP_NODE_FLAG_LEAF) != 0) && (b->data.leaf.numQuads <= 0))
			{
				empty++;
			}
		}
		for (int i = 0; i < s_extColl.numQuads; i++)
		{
			const struct QuadBlock *q = &s_extColl.quads[i];
			const u16 f = q->quadFlags;

			if ((f != EXT_COLL_FLAGS_GROUND) && (f != EXT_COLL_FLAGS_WALL_NOSCRUB) && (f != EXT_COLL_FLAGS_WALL_SOLID) && (f != EXT_COLL_FLAGS_KILL))
			{
				flagsOk = 0;
			}
			if (q->pvs != NULL)
			{
				pvsNull = 0;
			}
			if ((q->index[2] != q->index[3]) || (q->blockID != i))
			{
				oneTri = 0;
			}
		}
		ExtColl_TestCheck(empty == 0, "no empty leaf", &checks, &failures);
		ExtColl_TestCheck(flagsOk, "every record has the flags of its class", &checks, &failures);
		ExtColl_TestCheck(pvsNull, "every record has pvs NULL", &checks, &failures);
		ExtColl_TestCheck(oneTri, "index[2] == index[3] (one triangle) and blockID = record index", &checks, &failures);
	}

	// the limits line's values (A1): the longest edge of the solid triangles
	// (the wall's slant, 783.7), a midpoint lies at most half a unit off its
	// corners' middle, the tree fits the game's search stack, the heap numbers
	// are filled
	{
		char line[512];
		double longest = 0.0;

		for (int i = 0; i < n; i++)
		{
			for (int e = 0; (e < 3) && (tris[i].cls != EXT_COLL_CLASS_NONE); e++)
			{
				const s16 *a = tris[i].p[e];
				const s16 *b = tris[i].p[(e + 1) % 3];
				const double len = sqrt((double)(a[0] - b[0]) * (a[0] - b[0]) + (double)(a[1] - b[1]) * (a[1] - b[1]) + (double)(a[2] - b[2]) * (a[2] - b[2]));

				longest = (len > longest) ? len : longest;
			}
		}
		snprintf(what, sizeof(what), "longest edge %.1f = the longest solid edge of the made-up track %.1f", s_extColl.maxEdge, longest);
		ExtColl_TestCheck((fabs(s_extColl.maxEdge - longest) < 0.01) && (longest > 783.0) && (longest < 784.0), what, &checks, &failures);
		ExtColl_TestCheck(s_extColl.midOff2 <= 1, "a midpoint lies at most 0.5 per axis off its corners' middle", &checks, &failures);
		ExtColl_TestCheck((s_extColl.shiftMin >= 0) && (s_extColl.shiftMin <= s_extColl.shiftMax) && (s_extColl.shiftMax <= 24), "dividend shifts 0..24",
		                  &checks, &failures);
		ExtColl_TestCheck(s_extColl.treeDepth + 1 <= EXT_COLL_SEARCH_STACK, "the tree is shallower than the game's search stack", &checks, &failures);
		ExtColl_TestCheck((s_extColl.heapKept > 0) && (s_extColl.heapPeak > s_extColl.heapKept), "heap kept and peak are counted", &checks, &failures);
		ExtColl_FormatLimits(line, sizeof(line));
		printf("ext selftest coll: made-up track %s\n", line);
	}

	// the thresholds fixed before (P1 order): normals within 1 degree, 0
	// overflows, the tree reaches every record, the ray hits 100 %, |dh| <= 1
	ExtColl_CheckAll(&ck, 32);
	snprintf(what, sizeof(what), "game normals within 1 degree and of the right length (%d / %d off)", ck.normalsBad, ck.lengthBad);
	ExtColl_TestCheck((ck.normalsBad == 0) && (ck.lengthBad == 0), what, &checks, &failures);
	snprintf(what, sizeof(what), "no normal product over 32 bits (%d)", ck.overflow);
	ExtColl_TestCheck(ck.overflow == 0, what, &checks, &failures);
	snprintf(what, sizeof(what), "the tree offers every record for its own box (%d not)", ck.notReached);
	ExtColl_TestCheck(ck.notReached == 0, what, &checks, &failures);
	snprintf(what, sizeof(what), "the game's ray hits at every lattice point (%d + %d seam of %d missed)", ck.misses, ck.edgeMisses, ck.points);
	ExtColl_TestCheck((ck.misses == 0) && (ck.edgeMisses == 0) && (ck.points > 500), what, &checks, &failures);
	snprintf(what, sizeof(what), "no hit more than 1 under the game's own plane of the record (%d)", ck.below);
	ExtColl_TestCheck(ck.below == 0, what, &checks, &failures);
	ExtColl_PrintSamples(&ck, 0);
	snprintf(what, sizeof(what), "no hit higher than the own surface on the made-up track (%d)", ck.above);
	ExtColl_TestCheck(ck.above == 0, what, &checks, &failures);

	// exact heights of the game's ray at chosen points
	{
		u8 *const savedScratch = gCTRNativeScratchpadBase;
		s16 hy = 0;
		SVec3 hn = {0, 0, 0};
		int hit;

		memset(s_extCollScratch, 0, sizeof(s_extCollScratch));
		gCTRNativeScratchpadBase = (u8 *)s_extCollScratch;
		hit = ExtColl_Probe(-700, 0, 700, &hy, &hn);
		ExtColl_TestCheck(hit && (hy == 0) && (hn.y > 4000), "floor: hit at y 0 with the normal up", &checks, &failures);
		hit = ExtColl_Probe(1280, 64, 0, &hy, &hn);
		ExtColl_TestCheck(hit && (hy >= 60) && (hy <= 65) && (hn.y > 3900) && (hn.x < 0),
		                  "ramp: hit at y 64 a quarter up (the game's stored plane may lie up to 4 lower), the normal leans back", &checks, &failures);
		hit = ExtColl_Probe(0, 200, 0, &hy, &hn);
		ExtColl_TestCheck(hit && (hy == 200), "bridge from above: the deck (the topmost face in the window)", &checks, &failures);
		hit = ExtColl_Probe(-100, 0, 0, &hy, &hn);
		ExtColl_TestCheck(hit && (hy == 0), "under the bridge: the floor (the deck is 200 above, outside the window)", &checks, &failures);
		hit = ExtColl_Probe(-900, -390, -900, &hy, &hn);
		ExtColl_TestCheck(!hit, "the kill plane is no floor for the ray (NO_COLLISION_RESPONSE is ignored)", &checks, &failures);
		hit = ExtColl_Probe(5000, 0, 5000, &hy, &hn);
		ExtColl_TestCheck(!hit, "no hit outside the track", &checks, &failures);
		gCTRNativeScratchpadBase = savedScratch;
	}

	// refusals: an edge over the limit, a degenerate triangle
	{
		ExtCollTri bad = tris[0];

		bad.p[2][0] = (s16)(bad.p[0][0] + EXT_COLL_MAX_EDGE + 10);
		ExtColl_TestCheck(!ExtColl_Build(&bad, 1, why, (int)sizeof(why)) && !s_extColl.ready, "an edge longer than the limit is refused", &checks, &failures);
		bad = tris[0];
		bad.p[1][0] = bad.p[0][0];
		bad.p[1][1] = bad.p[0][1];
		bad.p[1][2] = bad.p[0][2];
		ExtColl_TestCheck(!ExtColl_Build(&bad, 1, why, (int)sizeof(why)) && !s_extColl.ready, "a degenerate triangle is refused", &checks, &failures);
	}
	// more records than the s16 record id holds: refused before anything is built
	{
		const int many = EXT_COLL_RECORD_MAX + 1;
		ExtCollTri *lots = (ExtCollTri *)malloc((size_t)many * sizeof(ExtCollTri));

		if (lots != NULL)
		{
			for (int i = 0; i < many; i++)
			{
				lots[i] = tris[0];
			}
			why[0] = '\0';
			ExtColl_TestCheck(!ExtColl_Build(lots, many, why, (int)sizeof(why)) && !s_extColl.ready && (strstr(why, "record id is s16") != NULL),
			                  "more than 32767 records are refused with the reason", &checks, &failures);
			free(lots);
		}
		else
		{
			ExtColl_TestCheck(0, "memory for the record-limit test", &checks, &failures);
		}
	}

	// VERTEX PAGES: 12000 separate small floor triangles need 72000 vertices
	// with the midpoints - refused without vertex_pages, built in two pages with
	// it; every record reads its own corners through its page, also in the
	// game's own vertex loader (COLL.c through COLL_RecordVertexArray, the ray)
	{
		char lim[640];
		const int many = 12000;
		const int cols = 110;
		const s16 side = 32;
		ExtCollTri *grid = (ExtCollTri *)calloc((size_t)many, sizeof(ExtCollTri));

		if (grid != NULL)
		{
			int cornersOk = 1;
			int rangeOk = 1;
			int accessOk = 1;
			int lastPage = 0;
			struct mesh_info other;

			for (int i = 0; i < many; i++)
			{
				const s16 x = (s16)(-3520 + 64 * (i % cols));
				const s16 z = (s16)(-3520 + 64 * (i / cols));
				const ExtCollTri t = {{{x, 0, z}, {x, 0, (s16)(z + side)}, {(s16)(x + side), 0, z}}, EXT_COLL_CLASS_GROUND, TERRAIN_ASPHALT, 0, 0, 0};

				grid[i] = t;
			}
			s_extCollPagesAsked = 0;
			why[0] = '\0';
			ExtColl_TestCheck(!ExtColl_Build(grid, many, why, (int)sizeof(why)) && !s_extColl.ready && (strstr(why, "vertices with the midpoints") != NULL),
			                  "72000 vertices without vertex_pages: refused as before", &checks, &failures);
			s_extCollPagesAsked = 1;
			why[0] = '\0';
			ExtColl_TestCheck(ExtColl_Build(grid, many, why, (int)sizeof(why)) && s_extColl.ready, "72000 vertices with vertex_pages: built", &checks, &failures);
			s_extCollPagesAsked = 0;
			ExtColl_TestCheck(s_extColl.ready && (s_extColl.numVerts == 6 * many) && (s_extColl.numPages == 2) && (s_extColl.recBase != NULL),
			                  "6 vertices per record, two pages", &checks, &failures);
			for (int r = 0; s_extColl.ready && (r < s_extColl.numQuads); r++)
			{
				const struct QuadBlock *q = &s_extColl.quads[r];
				const u32 base = s_extColl.recBase[r];
				const struct LevVertex *v = EXT_COLL_RV(q);
				const SVec3 a = v[q->index[0]].pos, c = v[q->index[1]].pos, b = v[q->index[2]].pos;

				for (int k = 0; k < 9; k++)
				{
					if ((base + q->index[k]) >= (u32)s_extColl.numVerts)
					{
						rangeOk = 0;
					}
				}
				if ((base > 0) && (base != (u32)lastPage))
				{
					lastPage = (int)base;
				}
				// (a, c, b) = the file's (p0, p2, p1): a right triangle of side 32 at y 0
				if ((a.y != 0) || (b.y != 0) || (c.y != 0) || (b.x != a.x) || (b.z != a.z + side) || (c.x != a.x + side) || (c.z != a.z) ||
				    (v[q->index[4]].pos.x != a.x + side / 2) || (v[q->index[5]].pos.z != a.z + side / 2))
				{
					cornersOk = 0;
				}
				if (COLL_RecordVertexArray(&s_extColl.mesh, q) != v)
				{
					accessOk = 0;
				}
			}
			ExtColl_TestCheck(rangeOk && (lastPage > 0), "every record's indices lie inside the array from its page (a second page used)", &checks, &failures);
			ExtColl_TestCheck(cornersOk, "every record's corners and midpoints read through its page are its own triangle's", &checks, &failures);
			ExtColl_TestCheck(accessOk, "COLL_RecordVertexArray gives each record's page", &checks, &failures);
			memset(&other, 0, sizeof(other));
			other.ptrVertexArray = s_extColl.verts + 7;
			ExtColl_TestCheck(s_extColl.ready && (COLL_RecordVertexArray(&other, &s_extColl.quads[0]) == other.ptrVertexArray),
			                  "another mesh: COLL_RecordVertexArray = its own ptrVertexArray", &checks, &failures);
			{
				u8 *const savedScratch = gCTRNativeScratchpadBase;
				s16 hy = 1;
				SVec3 hn = {0, 0, 0};
				const s16 lx = (s16)(-3520 + 64 * ((many - 1) % cols) + 8);
				const s16 lz = (s16)(-3520 + 64 * ((many - 1) / cols) + 8);
				int hitFirst, hitLast;

				memset(s_extCollScratch, 0, sizeof(s_extCollScratch));
				gCTRNativeScratchpadBase = (u8 *)s_extCollScratch;
				hitFirst = ExtColl_Probe(-3512, 0, -3512, &hy, &hn) && (hy == 0) && (hn.y > 4000);
				hy = 1;
				hitLast = ExtColl_Probe(lx, 0, lz, &hy, &hn) && (hy == 0) && (hn.y > 4000);
				gCTRNativeScratchpadBase = savedScratch;
				ExtColl_TestCheck(hitFirst && hitLast, "the game's ray hits the first and the last triangle at y 0 (both pages, COLL.c's loader)", &checks, &failures);
			}
			ExtColl_FormatLimits(lim, sizeof(lim));
			ExtColl_TestCheck(strstr(lim, "vertex pages 2") != NULL, "the limits line names the pages", &checks, &failures);
			ExtColl_Free();
			ExtColl_TestCheck((s_extColl.recBase == NULL) && (s_extColl.numPages == 0), "freed: no pages left", &checks, &failures);
			free(grid);
		}
		else
		{
			ExtColl_TestCheck(0, "memory for the vertex-page test", &checks, &failures);
		}
		ExtColl_Build(tris, n, why, (int)sizeof(why));
		ExtColl_FormatLimits(lim, sizeof(lim));
		ExtColl_TestCheck(s_extColl.ready && (s_extColl.recBase == NULL) && (strstr(lim, "vertex pages") == NULL),
		                  "the made-up track again: no pages, the limits line as before", &checks, &failures);
	}

	// COLL_WorldMesh answers with the level's own pointer unless the external
	// track holds the slot (never in a selftest: no --ext-track, no game tracker)
	{
		struct Level lev;
		struct mesh_info own;

		memset(&lev, 0, sizeof(lev));
		memset(&own, 0, sizeof(own));
		lev.ptr_mesh_info = &own;
		ExtColl_Free();
		ExtColl_TestCheck(COLL_WorldMesh(&lev) == &own, "nothing built: COLL_WorldMesh(level) == level->ptr_mesh_info", &checks, &failures);
		ExtColl_TestCheck(COLL_WorldMesh(NULL) == NULL, "COLL_WorldMesh(NULL) == NULL", &checks, &failures);
		ExtColl_Build(tris, n, why, (int)sizeof(why));
		ExtColl_TestCheck(s_extColl.ready && (COLL_WorldMesh(&lev) == &own), "built, but not the track's level: still the level's own pointer", &checks,
		                  &failures);
	}

	// a turbo pad (class trigger, pad 3): a 256-square 32 over the floor, as
	// retail lays its pads; built from a copy of the track plus the pad
	{
		ExtCollTri padTris[EXT_COLL_TEST_TRIS];
		ExtCollTri p1 = {{{-768, 32, -768}, {-768, 32, -512}, {-512, 32, -768}}, EXT_COLL_CLASS_TRIGGER, 1, EXT_COLL_CP_NONE, 1, 0, 3};
		ExtCollTri p2 = {{{-512, 32, -768}, {-768, 32, -512}, {-512, 32, -512}}, EXT_COLL_CLASS_TRIGGER, 1, EXT_COLL_CP_NONE, 1, 0, 3};
		int np = ExtColl_TestTrack(padTris);
		int padRecords = 0;
		int padOk = 1;
		struct BoundingBox pb;
		int pr = 0, bits = 0;

		padTris[np++] = p1;
		padTris[np++] = p2;
		ExtColl_TestCheck(ExtColl_Build(padTris, np, why, (int)sizeof(why)), "the made-up track with a pad builds", &checks, &failures);
		for (int i = 0; s_extColl.ready && (i < s_extColl.numQuads); i++)
		{
			const struct QuadBlock *q = &s_extColl.quads[i];

			if ((q->quadFlags & QUADBLOCK_FLAG_TRIGGER) != 0)
			{
				padRecords++;
				padOk = padOk && (q->quadFlags == EXT_COLL_FLAGS_TRIGGER) && (q->terrain_type == 1) && (q->draw_order_low == 0) &&
				        (q->checkpointIndex == EXT_COLL_CP_NONE);
			}
		}
		ExtColl_TestCheck((padRecords == 2) && padOk && (s_extColl.countClass[EXT_COLL_CLASS_TRIGGER] == 2),
		                  "two pad records with the retail pad flags 0xA040, terrain 1 (turbo pad), one-sided, no checkpoint", &checks, &failures);
		ExtColl_TestCheck((ExtColl_PadCount() == 4) && ExtColl_PadBox(3, &pb, &pr, &bits) && (pr == 2) && (bits == 1) && (pb.min.x == -768) &&
		                      (pb.max.z == -512) && (pb.min.y == 32) && !ExtColl_PadBox(0, &pb, &pr, &bits),
		                  "pad 3: its box and records; pads 0..2 have none", &checks, &failures);
		ExtColl_TestCheck((ExtColl_PadAt(-640, 25, -640, 64) == 3) && (ExtColl_PadAt(0, 0, 0, 64) == -1) && (ExtColl_PadAt(-640, 200, -640, 64) == -1),
		                  "the pad at a point: in its box (grown by 64) yes, elsewhere none", &checks, &failures);
		{
			u8 *const savedScratch = gCTRNativeScratchpadBase;
			s16 hy = 0;
			SVec3 hn = {0, 0, 0};
			int hit;

			memset(s_extCollScratch, 0, sizeof(s_extCollScratch));
			gCTRNativeScratchpadBase = (u8 *)s_extCollScratch;
			hit = ExtColl_Probe(-640, 0, -640, &hy, &hn);
			gCTRNativeScratchpadBase = savedScratch;
			ExtColl_TestCheck(hit && (hy == 0), "the ground ray under a pad still lands on the floor (a pad is no hit)", &checks, &failures);
		}
	}

	// anti-gravity zones (B2): the bridge deck as zone 7; the ray in any
	// direction (ExtColl_ProbeSurface)
	{
		ExtCollTri zoneTris[EXT_COLL_TEST_TRIS];
		const int nz = ExtColl_TestTrack(zoneTris);
		int deckZone = 1;
		int others = 0;
		ExtCollHit h;
		const SVec3 down = {0, -4096, 0};
		const SVec3 up = {0, 4096, 0};
		const SVec3 slant = {0, -2896, 2896}; // 45 degrees down towards +z
		SVec3 from;

		for (int i = 0; i < nz; i++)
		{
			if (zoneTris[i].terrain == TERRAIN_WOOD)
			{
				zoneTris[i].zone = 7;
			}
		}
		ExtColl_TestCheck(ExtColl_Build(zoneTris, nz, why, (int)sizeof(why)) && (ExtColl_ZoneRecords() == 2), "the deck's two records are in zone 7", &checks,
		                  &failures);
		for (int i = 0; s_extColl.ready && (i < s_extColl.numQuads); i++)
		{
			const struct QuadBlock *q = &s_extColl.quads[i];

			if (q->terrain_type == TERRAIN_WOOD)
			{
				deckZone = deckZone && (ExtColl_RecordZone(q) == 7);
			}
			else if (ExtColl_RecordZone(q) != 0)
			{
				others++;
			}
		}
		{
			struct QuadBlock foreign;

			memset(&foreign, 0, sizeof(foreign));
			ExtColl_TestCheck(deckZone && (others == 0) && (ExtColl_RecordZone(NULL) == 0) && (ExtColl_RecordZone(&foreign) == 0),
			                  "RecordZone: 7 on the deck, 0 elsewhere and for a record not of the external mesh", &checks, &failures);
		}
		from = (SVec3){0, 300, 0};
		ExtColl_TestCheck(ExtColl_ProbeSurface(&from, &down, 400, &h) && (h.pos.y == 200) && (h.zone == 7) && (h.terrain == TERRAIN_WOOD) && (h.normal.y > 4000),
		                  "probe down onto the deck: y 200, zone 7, normal up", &checks, &failures);
		from = (SVec3){0, 100, 0};
		ExtColl_TestCheck(!ExtColl_ProbeSurface(&from, &up, 300, &h), "probe up from under the deck: no hit (the deck faces up, the ray takes front faces)",
		                  &checks, &failures);
		from = (SVec3){-700, 200, -900};
		ExtColl_TestCheck(ExtColl_ProbeSurface(&from, &slant, 400, &h) && (h.zone == 0) && (h.pos.y >= -1) && (h.pos.y <= 1) && (h.pos.z >= -702) &&
		                      (h.pos.z <= -698),
		                  "probe at 45 degrees onto the floor: 200 down is 200 on in z, the hit at z -700, y 0", &checks, &failures);

		// the line query (exact, both faces, by class)
		{
			ExtCollLineHit lh;
			const double vUp[3] = {0.0, 1.0, 0.0};
			const double vDown[3] = {0.0, -3.0, 0.0}; // any length
			const double vWest[3] = {-1.0, 0.0, 0.0};

			{
				const double p[3] = {0.0, 120.0, 0.0};

				ExtColl_TestCheck(ExtColl_LineQuery(p, vUp, -300.0, 300.0, EXT_COLL_LINE_GROUND, 1, &lh) && (fabs(lh.t - 80.0) < 1e-6) &&
				                      (fabs(lh.pos[1] - 200.0) < 1e-6) && (lh.normal[1] > 0.999) && !lh.front && (lh.zone == 7) && (lh.cls == EXT_COLL_LINE_GROUND) &&
				                      (lh.terrain == TERRAIN_WOOD) && (lh.record == &s_extColl.quads[lh.recordIndex]),
				                  "line +-300 up from 120 under the deck: the nearest is the deck (+80), its back face, front normal up, zone 7", &checks, &failures);
				ExtColl_TestCheck(ExtColl_LineQuery(p, vUp, -300.0, 300.0, EXT_COLL_LINE_GROUND, 0, &lh) && (fabs(lh.t + 120.0) < 1e-6) && (lh.zone == 0) &&
				                      (lh.normal[1] > 0.999) && !lh.front,
				                  "the same line by smallest t: the floor (-120); +dir points up, so the line meets its back face", &checks, &failures);
				ExtColl_TestCheck(!ExtColl_LineQuery(p, vUp, -100.0, 60.0, EXT_COLL_LINE_GROUND, 1, &lh), "the same line within -100..60: nothing", &checks, &failures);
			}
			{
				const double p[3] = {-900.0, 50.0, -900.0};

				ExtColl_TestCheck(ExtColl_LineQuery(p, vDown, 0.0, 500.0, EXT_COLL_LINE_KILL, 1, &lh) && (fabs(lh.t - 450.0) < 1e-6) && (lh.cls == EXT_COLL_LINE_KILL),
				                  "down onto kills only: the kill plane at -400", &checks, &failures);
				ExtColl_TestCheck(ExtColl_LineQuery(p, vDown, 0.0, 500.0, EXT_COLL_LINE_GROUND | EXT_COLL_LINE_KILL, 1, &lh) && (fabs(lh.t - 50.0) < 1e-6) &&
				                      (lh.cls == EXT_COLL_LINE_GROUND),
				                  "down onto ground or kill: the floor comes first", &checks, &failures);
				ExtColl_TestCheck(ExtColl_LineQuery(p, vWest, 0.0, 500.0, EXT_COLL_LINE_WALL, 1, &lh) && (fabs(lh.t - 124.0) < 1e-6) && (lh.cls == EXT_COLL_LINE_WALL),
				                  "west onto walls: the wall at x -1024", &checks, &failures);
				ExtColl_TestCheck(!ExtColl_LineQuery(p, vWest, 0.0, 500.0, EXT_COLL_LINE_GROUND, 1, &lh), "west onto ground: none", &checks, &failures);
			}
			{
				const double p[3] = {1280.0, 200.0, 0.0};

				ExtColl_TestCheck(ExtColl_LineQuery(p, vDown, 0.0, 400.0, EXT_COLL_LINE_GROUND, 1, &lh) && (fabs(lh.pos[1] - 64.0) < 1e-6) && (lh.normal[0] < -0.2) &&
				                      (lh.normal[1] > 0.95) && lh.front && (lh.terrain == TERRAIN_DIRT),
				                  "down onto the ramp: exactly y 64, the front normal leans back", &checks, &failures);
			}
			{
				const double p[3] = {5000.0, 0.0, 5000.0};
				const double zero[3] = {0.0, 0.0, 0.0};

				ExtColl_TestCheck(!ExtColl_LineQuery(p, vDown, -1000.0, 1000.0, EXT_COLL_LINE_ALL, 1, &lh) &&
				                      !ExtColl_LineQuery(p, zero, -1000.0, 1000.0, EXT_COLL_LINE_ALL, 1, &lh),
				                  "no hit outside the track, none for a zero direction", &checks, &failures);
			}
		}
	}

	printf("ext selftest coll: made-up track %d record(s), %d vertex(es), %d node(s), depth %d, ray %d point(s), %d edge point(s) skipped; the game's "
	       "planes lie up to %.2f from the exact surface in height, %.2f along the normal, %d hit(s) more than 1 off it\n",
	       s_extColl.numQuads, s_extColl.numVerts, s_extColl.numBsp, s_extColl.treeDepth, ck.points, ck.skippedEdge, ck.planeOffMax, ck.planeOffNormalMax,
	       ck.offExact);
	ExtColl_Free();

	// THE REAL FILE, only when asked: the environment variable
	// CTR_EXT_COLL_SELFTEST_DIR names a folder with collision.glb (the selftest
	// runs before the switches are read, so --ext-track is not known here). The
	// same build and the same checks as at a level load of the track.
	{
		const char *dir = getenv("CTR_EXT_COLL_SELFTEST_DIR");

		if ((dir != NULL) && (dir[0] != '\0'))
		{
			char path[ENGINE_EXTTRACK_PATH_MAX + 64];
			ExtCollTri *real = NULL;
			int numReal = 0;

			snprintf(path, sizeof(path), "%s/collision.glb", dir);
			real = ExtColl_ReadGlb(path, &numReal, why, (int)sizeof(why));
			snprintf(what, sizeof(what), "the real file %s reads and builds (%s)", path, (real == NULL) ? why : "read");
			ExtColl_TestCheck((real != NULL) && ExtColl_Build(real, numReal, why, (int)sizeof(why)), what, &checks, &failures);
			free(real);
			if (s_extColl.ready)
			{
				ExtColl_CheckAll(&ck, 64);
				printf("ext selftest coll: real file %d triangle(s) -> %d record(s) (ground %d, wall %d, kill %d), %d vertex(es), %d node(s), depth %d, "
				       "%d water leaf(s)\n",
				       numReal, s_extColl.numQuads, s_extColl.countClass[EXT_COLL_CLASS_GROUND], s_extColl.countClass[EXT_COLL_CLASS_WALL],
				       s_extColl.countClass[EXT_COLL_CLASS_KILL], s_extColl.numVerts, s_extColl.numBsp, s_extColl.treeDepth, s_extColl.waterLeaves);
				printf("ext selftest coll: real file ground records in zones %d, trigger records %d in %d pad(s)\n", s_extColl.zoneRecords,
				       s_extColl.countClass[EXT_COLL_CLASS_TRIGGER], s_extColl.numPads);
				printf("ext selftest coll: real file ground terrains:");
				for (int t = 0; t < EXT_COLL_TERRAINS; t++)
				{
					if (s_extColl.countTerrain[t] > 0)
					{
						printf(" %d:%d", t, s_extColl.countTerrain[t]);
					}
				}
				printf("\n");
				printf("ext selftest coll: real file normals off %d, length off %d, overflow %d, not in tree %d; ray %d point(s), %d miss(es), %d seam miss(es) "
				       "within %.1f of a triangle edge, %d seam below hit(s), %d below the game's plane, %d under a higher face, %d near a record edge "
				       "skipped, %d steep zone record(s) left to the zone's ray; planes up to %.2f "
				       "from the exact surface in height, %.2f along the normal, %d hit(s) more than 1 off it\n",
				       ck.normalsBad, ck.lengthBad, ck.overflow, ck.notReached, ck.points, ck.misses, ck.edgeMisses, EXT_COLL_SEAM_MISS_SLACK, ck.edgeBelow, ck.below,
				       ck.above, ck.skippedEdge, ck.zoneSteep, ck.planeOffMax, ck.planeOffNormalMax, ck.offExact);
				printf("ext selftest coll: real file worst normal %.2f deg; records with a normal off by shortest edge <16 %d, <64 %d, <256 %d, >=256 %d\n",
				       ck.normalWorst, ck.normalsBadEdge[0], ck.normalsBadEdge[1], ck.normalsBadEdge[2], ck.normalsBadEdge[3]);
				ExtColl_PrintSamples(&ck, 0);
				{
					char line[512];

					ExtColl_FormatLimits(line, sizeof(line));
					printf("ext selftest coll: real file %s\n", line);
				}
				snprintf(what, sizeof(what), "the real file passes the checks");
				ExtColl_TestCheck(ExtColl_CheckPassed(&ck), what, &checks, &failures);
				ExtColl_Free();
			}
		}
		else
		{
			printf("ext selftest coll: real file skipped (CTR_EXT_COLL_SELFTEST_DIR not set)\n");
		}
	}
	if (failures)
	{
		printf("ext selftest coll FAILED: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	printf("ext selftest coll passed: %d checks, 0 failures\n", checks);
	return 0;
}
