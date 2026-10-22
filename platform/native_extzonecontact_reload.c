// THE ZONE PORT'S CONTACTS, Reload's side (game build only; included by main.c
// after native_extcoll.c and native_extzoneport.c): the triangle provider over
// the external track's collision (see include/platform/native_extzonecontact.h).
//
// Every record of the external collision is one triangle (corners index[0],
// index[2], index[1] = the file's a, b, c; front = (b - a) x (c - a), as
// ExtColl_LineQuery reads them). The provider walks the leaves of the mesh's
// search tree whose box meets the sphere's box, converts the corners of every
// ground and wall record whose own box meets it from Reload units to the source's
// metres (the zone port's frame) and hands them out. Kill and trigger records
// never touch.

#include <platform/native_extzonecontact.h>
#include <platform/native_extzoneport.h>
#include <platform/native_extcoll.h>

#define ZCR_FLAGS_GROUND 0x1800u
#define ZCR_FLAGS_WALL_NOSCRUB 0x2001u
#define ZCR_FLAGS_WALL_SOLID 0x2000u

static int zcr_box_meets(const struct BoundingBox *b, const double lo[3], const double hi[3])
{
	return !(((double)b->max.x < lo[0]) || ((double)b->min.x > hi[0]) || ((double)b->max.y < lo[1]) || ((double)b->min.y > hi[1]) ||
	         ((double)b->max.z < lo[2]) || ((double)b->min.z > hi[2]));
}

// measuring only: calls, triangles handed out and host time, one log line per 3000 calls
static uint32_t s_zcrCalls, s_zcrTris, s_zcrLeaves;
static uint64_t s_zcrTicks;

static int zcr_triangles(const double centre[3], double reach, ExtZcTri *out, int max);

int ExtZc_ReloadTriangles(void *user, const double centre[3], double reach, ExtZcTri *out, int max)
{
	const uint64_t t0 = SDL_GetPerformanceCounter();
	const int n = zcr_triangles(centre, reach, out, max);
	(void)user;
	s_zcrTicks += SDL_GetPerformanceCounter() - t0;
	s_zcrCalls++;
	s_zcrTris += (uint32_t)n;
	if ((s_zcrCalls % 3000u) == 0u)
	{
		const double ms = 1000.0 * (double)s_zcrTicks / (double)SDL_GetPerformanceFrequency();
		uint32_t heapUsed, heapRefused;
		ExtZc_HeapStats(&heapUsed, &heapRefused);
		Platform_Log("[CTR ExtZc] triangle provider: %u calls, %.2f triangles and %.1f leaves per call, %.4f ms per call (%.1f ms in all); "
		             "heap %u of %u bytes, %u refused\n",
		             s_zcrCalls, (double)s_zcrTris / (double)s_zcrCalls, (double)s_zcrLeaves / (double)s_zcrCalls, ms / (double)s_zcrCalls, ms,
		             (unsigned)heapUsed, (unsigned)EXT_ZC_ARENA_BYTES, (unsigned)heapRefused);
	}
	return n;
}

static int zcr_triangles(const double centre[3], double reach, ExtZcTri *out, int max)
{
	struct mesh_info *mesh;
	double rc[3], rx[3], one[3], scale, r, lo[3], hi[3];
	int n = 0;
	if ((sdata->gGT == NULL) || (sdata->gGT->level1 == NULL))
		return 0;
	mesh = COLL_WorldMesh(sdata->gGT->level1);
	if ((mesh == NULL) || (mesh->bspRoot == NULL) || (mesh->ptrQuadBlockArray == NULL))
		return 0;
	ExtZp_PointFromSource(centre, rc);
	one[0] = centre[0] + 1.0;
	one[1] = centre[1];
	one[2] = centre[2];
	ExtZp_PointFromSource(one, rx);
	scale = sqrt((rx[0] - rc[0]) * (rx[0] - rc[0]) + (rx[1] - rc[1]) * (rx[1] - rc[1]) + (rx[2] - rc[2]) * (rx[2] - rc[2]));
	r = reach * scale + 2.0; // two units: the records' corners are whole units
	for (int k = 0; k < 3; k++)
	{
		lo[k] = rc[k] - r;
		hi[k] = rc[k] + r;
	}
	for (int b = 0; (b < mesh->numBspNodes) && (n < max); b++)
	{
		const struct BSP *node = &mesh->bspRoot[b];
		if (((node->flag & BSP_NODE_FLAG_LEAF) == 0) || !zcr_box_meets(&node->box, lo, hi))
			continue;
		s_zcrLeaves++;
		for (int q = 0; (q < node->data.leaf.numQuads) && (n < max); q++)
		{
			const struct QuadBlock *rec = &node->data.leaf.ptrQuadBlockArray[q];
			const unsigned flags = (unsigned)rec->quadFlags;
			const int wall = (flags == ZCR_FLAGS_WALL_SOLID) || (flags == ZCR_FLAGS_WALL_NOSCRUB);
			const struct LevVertex *v;
			double p[3][3];
			if (((flags != ZCR_FLAGS_GROUND) && !wall) || !zcr_box_meets(&rec->bbox, lo, hi))
				continue;
			v = COLL_RecordVertexArray(mesh, rec);
			{
				const SVec3 a = v[rec->index[0]].pos, bb = v[rec->index[2]].pos, cc = v[rec->index[1]].pos;
				const double ra[3] = {a.x, a.y, a.z}, rb[3] = {bb.x, bb.y, bb.z}, rcc[3] = {cc.x, cc.y, cc.z};
				ExtZp_PointToSource(ra, p[0]);
				ExtZp_PointToSource(rb, p[1]);
				ExtZp_PointToSource(rcc, p[2]);
			}
			memcpy(out[n].a, p[0], sizeof(p[0]));
			memcpy(out[n].b, p[1], sizeof(p[1]));
			memcpy(out[n].c, p[2], sizeof(p[2]));
			out[n].key = (int)(rec - mesh->ptrQuadBlockArray);
			out[n].solid = 1;
			out[n].surface = (ExtColl_RecordZone(rec) != 0) ? 24 : (wall ? 27 : 1);
			n++;
		}
	}
	return n;
}
