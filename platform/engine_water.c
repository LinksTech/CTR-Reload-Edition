#include <platform/engine.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE WATER - the water of a level through the engine (--engine-track, round
// of 2026-10-15). See include/platform/engine.h, WATER.
//
// WHAT. The quadblocks of the level's water leaves (BSP leaves with
// BSP_LEAF_FLAG_WATER; RenderLists puts such a leaf in the water slot only,
// game/RenderLevel/RenderLists.c Select1P2PSlot) become one static engine mesh,
// drawn every race frame by the program "nw" (platform/engine_water_shader.inc):
// a rippled reflection of the level's env map (on the disc a 64 x 64 sky
// panorama), weighted by fresnel and added to what lies below. While it is
// drawn, the PS1 walk of the level skips its two water buckets (8 "water
// rendered", 9 "water list" of DrawLevelOvr1P) through Engine_WaterOwnsBucket.
//
// NOT THE PS1 TRICK. The PS1 path steps every water vertex through 28 stored
// env-map coordinates (game/RenderLevel/AnimateWater.c), fades the colour with
// the GTE by the distance to the camera (Ovr226_800a2234), subdivides near the
// camera and sorts the pieces into the ordering table. None of that is rebuilt:
// the mesh is built once, the motion comes from the time (params3.y) in the
// shader, the depth test against the engine track does the occlusion.
//
// GAME STATE STAYS. AnimateWater keeps running every frame as before: it writes
// LevVertex.color_hi of the water vertices, which the collision reads for the
// driver's light (game/COLL.c, COLL_FIXED_PlayerSearch_UpdateLighting). This
// file reads the level (positions, BSP, the env map layout) and changes nothing.
//
// ORDER AND DEPTH. One draw in OT cell 0x3fc: after the track's opaque (0x3fe)
// and semi-transparent (0x3fd) cells, before everything nearer. It tests the
// depth the track wrote and writes none (additive, the floor below shows
// through). Engine drivers drawn later overwrite their part below the surface,
// which engine_instance.c already tints as under water. A PS1 primitive (no
// depth) below the surface is drawn over the water - a phase-1 limit. The water
// is drawn only in a frame in which the engine track was drawn: without the
// track's depth it would show through every hill, so then the PS1 water stays.
//
// THE MESH. Per block the four grid faces of the PS1 draw ({0,4,5,6} {4,1,6,7}
// {5,6,2,8} {6,7,8,3}) as the triangles (g0,g1,g2) and (g1,g3,g2), double
// sided (the PS1 water face gate has no NCLIP test). Vertices are shared
// through the LevVertex index. a_texcoord carries the x and z of the
// area-weighted vertex normal (oriented up): the shader bends it with its
// ripples and carries the ripples downhill on slopes (rivers).

#define EGW_CELL 0x3fc
#define EGW_BUCKET_WATER_RENDERED 8
#define EGW_BUCKET_WATER_LIST 9
#define EGW_VERTICES_MAX 65535u // u16 indices
#define EGW_FRAMES_PER_SECOND 30.0f

internal const int s_egwGrid[4][4] = {{0, 4, 5, 6}, {4, 1, 6, 7}, {5, 6, 2, 8}, {6, 7, 8, 3}};

// --------------------------------------------------------------------------
// Pure data side (no game state): the part the selftest checks.

typedef struct
{
	EngineVertex *vertices;
	u32 vertexCount;
	u16 *indices;
	u32 indexCount;
	int blocks;      // water blocks used
	int hidden;      // water blocks the zero rule hides
	int badBlocks;   // a vertex index out of range
	int degenerate;  // triangles dropped (repeated corner or no area)
} EngWaterBuild;

// Marks the quadblocks of water leaves in water[] (one byte per quadblock).
// Returns the number of water leaves; *badLeaves counts leaves whose block
// range lies outside the quadblock array (skipped).
internal int EngWater_MarkBlocks(const struct mesh_info *mesh, u8 *water, int *badLeaves)
{
	int leaves = 0;
	int n;

	if (badLeaves != NULL)
	{
		*badLeaves = 0;
	}
	if ((mesh == NULL) || (mesh->bspRoot == NULL) || (mesh->ptrQuadBlockArray == NULL))
	{
		return 0;
	}
	for (n = 0; n < mesh->numBspNodes; n++)
	{
		const struct BSP *node = &mesh->bspRoot[n];
		ptrdiff_t first;
		int i;

		if (((node->flag & BSP_NODE_FLAG_LEAF) == 0) || ((node->flag & BSP_LEAF_FLAG_WATER) == 0))
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
			if (badLeaves != NULL)
			{
				(*badLeaves)++;
			}
			continue;
		}
		leaves++;
		for (i = 0; i < node->data.leaf.numQuads; i++)
		{
			water[first + i] = 1;
		}
	}
	return leaves;
}

internal void EngWater_BuildFree(EngWaterBuild *b)
{
	free(b->vertices);
	free(b->indices);
	b->vertices = NULL;
	b->indices = NULL;
	b->vertexCount = 0;
	b->indexCount = 0;
}

// The mesh of the marked blocks. isHidden (may be NULL) is the zero rule of the
// PS1 water list. Returns 1 on success (also with 0 triangles), 0 when memory
// or the u16 vertex limit runs out (out is then empty).
internal int EngWater_Build(const struct LevVertex *lv, int numVertex, const struct QuadBlock *blocks, int numBlocks, const u8 *water,
                            int (*isHidden)(const struct QuadBlock *), EngWaterBuild *out)
{
	int *map = NULL;
	float *acc = NULL;
	u8 *use = NULL;
	u32 vertexCount = 0;
	u32 triangles = 0;
	int ok = 0;
	int i;

	memset(out, 0, sizeof(*out));
	if ((lv == NULL) || (blocks == NULL) || (water == NULL) || (numVertex <= 0) || (numBlocks <= 0))
	{
		return 1;
	}
	map = (int *)malloc((size_t)numVertex * sizeof(int));
	use = (u8 *)calloc((size_t)numBlocks, 1u);
	if ((map == NULL) || (use == NULL))
	{
		goto done;
	}
	for (i = 0; i < numVertex; i++)
	{
		map[i] = -1;
	}

	// Pass 1: which blocks, which vertices.
	for (i = 0; i < numBlocks; i++)
	{
		const struct QuadBlock *qb = &blocks[i];
		int k;
		int bad = 0;

		if (!water[i])
		{
			continue;
		}
		if ((isHidden != NULL) && isHidden(qb))
		{
			out->hidden++;
			continue;
		}
		for (k = 0; k < 9; k++)
		{
			bad |= ((int)qb->index[k] >= numVertex);
		}
		if (bad)
		{
			out->badBlocks++;
			continue;
		}
		use[i] = 1;
		out->blocks++;
		for (k = 0; k < 9; k++)
		{
			if (map[qb->index[k]] < 0)
			{
				map[qb->index[k]] = (int)vertexCount++;
			}
		}
		triangles += 8u;
	}
	if (vertexCount > EGW_VERTICES_MAX)
	{
		goto done;
	}
	if (vertexCount == 0u)
	{
		ok = 1;
		goto done;
	}
	out->vertices = (EngineVertex *)calloc(vertexCount, sizeof(EngineVertex));
	out->indices = (u16 *)malloc((size_t)triangles * 3u * sizeof(u16));
	acc = (float *)calloc((size_t)vertexCount * 3u, sizeof(float));
	if ((out->vertices == NULL) || (out->indices == NULL) || (acc == NULL))
	{
		goto done;
	}
	for (i = 0; i < numVertex; i++)
	{
		const int m = map[i];

		if (m >= 0)
		{
			EngineVertex *v = &out->vertices[m];

			v->position[0] = (float)lv[i].pos.x;
			v->position[1] = (float)lv[i].pos.y;
			v->position[2] = (float)lv[i].pos.z;
			v->color[0] = v->color[1] = v->color[2] = v->color[3] = 255;
		}
	}

	// Pass 2: the triangles and the area-weighted normals.
	for (i = 0; i < numBlocks; i++)
	{
		const struct QuadBlock *qb = &blocks[i];
		int f;

		if (!use[i])
		{
			continue;
		}
		for (f = 0; f < 4; f++)
		{
			const int *g = s_egwGrid[f];
			const int tri[2][3] = {{g[0], g[1], g[2]}, {g[1], g[3], g[2]}};
			int t;

			for (t = 0; t < 2; t++)
			{
				const int a = qb->index[tri[t][0]];
				const int b = qb->index[tri[t][1]];
				const int c = qb->index[tri[t][2]];
				const float *pa = out->vertices[map[a]].position;
				const float *pb = out->vertices[map[b]].position;
				const float *pc = out->vertices[map[c]].position;
				float e1[3];
				float e2[3];
				float n[3];
				int j;

				if ((a == b) || (b == c) || (a == c))
				{
					out->degenerate++;
					continue;
				}
				for (j = 0; j < 3; j++)
				{
					e1[j] = pb[j] - pa[j];
					e2[j] = pc[j] - pa[j];
				}
				n[0] = e1[1] * e2[2] - e1[2] * e2[1];
				n[1] = e1[2] * e2[0] - e1[0] * e2[2];
				n[2] = e1[0] * e2[1] - e1[1] * e2[0];
				if ((n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) < 1.0e-6f)
				{
					out->degenerate++;
					continue;
				}
				// Double sided: the normal that points up.
				if (n[1] < 0.0f)
				{
					n[0] = -n[0];
					n[1] = -n[1];
					n[2] = -n[2];
				}
				for (j = 0; j < 3; j++)
				{
					acc[map[a] * 3 + j] += n[j];
					acc[map[b] * 3 + j] += n[j];
					acc[map[c] * 3 + j] += n[j];
				}
				out->indices[out->indexCount++] = (u16)map[a];
				out->indices[out->indexCount++] = (u16)map[b];
				out->indices[out->indexCount++] = (u16)map[c];
			}
		}
	}
	for (i = 0; i < (int)vertexCount; i++)
	{
		const float *n = &acc[i * 3];
		const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

		// A vertex of degenerate triangles only keeps the flat normal (0, 1, 0).
		out->vertices[i].texcoord[0] = (len > 0.0f) ? (n[0] / len) : 0.0f;
		out->vertices[i].texcoord[1] = (len > 0.0f) ? (n[2] / len) : 0.0f;
	}
	out->vertexCount = vertexCount;
	ok = 1;

done:
	if (!ok)
	{
		EngWater_BuildFree(out);
	}
	free(map);
	free(acc);
	free(use);
	return ok;
}

// The CPU twin of the lookup of "nw" (engine_water_shader.inc), from the same
// macros: the env texel coordinate (u wraps at 64, v 0 = zenith .. 32 =
// horizon), the brightness factor (K_BASE + K_FRESNEL * fresnel) and the far
// fade. eye and world in world units, (nx, nz) the mesh normal, depth the view
// depth, time in seconds.
typedef struct
{
	float u;
	float v;
	float intensity;
	float fade;
} EngWaterLook;

internal double EngWater_Clamp(double x, double lo, double hi)
{
	return (x < lo) ? lo : ((x > hi) ? hi : x);
}

internal void EngWater_WaveSlope(double dx, double dz, double len, double speed, double slope, double px, double pz, double time, double *sx,
                                 double *sz)
{
	const double phase = (2.0 * 3.14159265 / len) * ((dx * px + dz * pz) - speed * time);
	const double c = cos(phase);

	*sx += slope * dx * c;
	*sz += slope * dz * c;
}

internal EngWaterLook EngWater_Lookup(const float eye[3], const float world[3], float nx, float nz, float depth, float time)
{
	EngWaterLook out;
	const double env = (double)EGW_ENV_TEXELS;
	const double ny = sqrt(EngWater_Clamp(1.0 - (double)nx * nx - (double)nz * nz, 0.0, 1.0));
	const double px = (double)world[0] - (double)nx * (EGW_FLOW * (double)time);
	const double pz = (double)world[2] - (double)nz * (EGW_FLOW * (double)time);
	const double ripple = EngWater_Clamp((EGW_RIPPLE_ZERO - (double)depth) / (EGW_RIPPLE_ZERO - EGW_RIPPLE_FULL), 0.0, 1.0);
	double sx = 0.0;
	double sz = 0.0;
	double n[3];
	double view[3];
	double r[3];
	double len;
	double d;
	double elevation;
	double azimuth;
	double cosine;
	double fresnel;
	int i;

	EngWater_WaveSlope(EGW_W0_DX, EGW_W0_DZ, EGW_W0_LEN, EGW_W0_SPEED, EGW_W0_SLOPE, px, pz, time, &sx, &sz);
	EngWater_WaveSlope(EGW_W1_DX, EGW_W1_DZ, EGW_W1_LEN, EGW_W1_SPEED, EGW_W1_SLOPE, px, pz, time, &sx, &sz);
	EngWater_WaveSlope(EGW_W2_DX, EGW_W2_DZ, EGW_W2_LEN, EGW_W2_SPEED, EGW_W2_SLOPE, px, pz, time, &sx, &sz);
	n[0] = (double)nx - sx * ripple;
	n[1] = ny;
	n[2] = (double)nz - sz * ripple;
	len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	for (i = 0; i < 3; i++)
	{
		n[i] /= len;
		view[i] = (double)world[i] - (double)eye[i];
	}
	len = sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
	for (i = 0; i < 3; i++)
	{
		view[i] = (len > 0.0) ? (view[i] / len) : 0.0;
	}
	d = view[0] * n[0] + view[1] * n[1] + view[2] * n[2];
	for (i = 0; i < 3; i++)
	{
		r[i] = view[i] - 2.0 * d * n[i];
	}
	elevation = asin(EngWater_Clamp(r[1], 0.0, 1.0));
	azimuth = ((fabs(r[0]) + fabs(r[2])) > 1.0e-6) ? atan2(r[2], r[0]) : 0.0;
	azimuth = azimuth / (2.0 * 3.14159265);
	out.u = (float)((azimuth - floor(azimuth)) * env);
	out.v = (float)((env * 0.5) * (1.0 - elevation / (0.5 * 3.14159265)));
	cosine = EngWater_Clamp(-d, 0.0, 1.0);
	fresnel = EGW_F0 + (1.0 - EGW_F0) * pow(1.0 - cosine, 5.0);
	out.intensity = (float)(EGW_K_BASE + EGW_K_FRESNEL * fresnel);
	out.fade = (float)EngWater_Clamp((EGW_FADE_END - (double)depth) / (EGW_FADE_END - EGW_FADE_START), 0.0, 1.0);
	return out;
}

// The camera position in world units: view = A * world + t, so eye = -A^-1 * t.
// 0 when the matrix cannot be inverted.
internal int EngWater_Eye(const EngineMat4 *viewFromWorld, float eye[3])
{
	const float *m = viewFromWorld->m;
	// a[r][c] = m[c * 4 + r] (column-major)
	const double a00 = m[0], a01 = m[4], a02 = m[8];
	const double a10 = m[1], a11 = m[5], a12 = m[9];
	const double a20 = m[2], a21 = m[6], a22 = m[10];
	const double t0 = m[12], t1 = m[13], t2 = m[14];
	const double c00 = a11 * a22 - a12 * a21;
	const double c01 = a02 * a21 - a01 * a22;
	const double c02 = a01 * a12 - a02 * a11;
	const double c10 = a12 * a20 - a10 * a22;
	const double c11 = a00 * a22 - a02 * a20;
	const double c12 = a02 * a10 - a00 * a12;
	const double c20 = a10 * a21 - a11 * a20;
	const double c21 = a01 * a20 - a00 * a21;
	const double c22 = a00 * a11 - a01 * a10;
	const double det = a00 * c00 + a01 * c10 + a02 * c20;

	if (fabs(det) < 1.0e-12)
	{
		return 0;
	}
	eye[0] = (float)(-(c00 * t0 + c01 * t1 + c02 * t2) / det);
	eye[1] = (float)(-(c10 * t0 + c11 * t1 + c12 * t2) / det);
	eye[2] = (float)(-(c20 * t0 + c21 * t1 + c22 * t2) / det);
	return 1;
}

// Sutherland-Hodgman: keeps the part of polygon in[0..count) where
// a * x + b * y + c * z + d >= 0 (points as x, y, z triples). Returns the new count.
internal int EngWater_ClipPlane(const float (*in)[3], int count, float a, float b, float c, float d, float (*out)[3])
{
	int n = 0;
	int i;

	for (i = 0; i < count; i++)
	{
		const float *p = in[i];
		const float *q = in[(i + 1) % count];
		const float dp = a * p[0] + b * p[1] + c * p[2] + d;
		const float dq = a * q[0] + b * q[1] + c * q[2] + d;

		if (dp >= 0.0f)
		{
			out[n][0] = p[0];
			out[n][1] = p[1];
			out[n][2] = p[2];
			n++;
		}
		if ((dp >= 0.0f) != (dq >= 0.0f))
		{
			const float s = dp / (dp - dq);

			out[n][0] = p[0] + (q[0] - p[0]) * s;
			out[n][1] = p[1] + (q[1] - p[1]) * s;
			out[n][2] = p[2] + (q[2] - p[2]) * s;
			n++;
		}
	}
	return n;
}

// One water quad (corners in polygon order) in a view: the part in front of the
// near plane, projected, clipped to the view rectangle. *area in PSX pixels,
// box = x0, y0, x1, y1 in PSX pixels of the view, *nearest the smallest view
// depth of the part in front. Returns 1 when something of it is on screen.
internal int EngWater_ClipQuad(const EngineCamera *cam, const EngineVec3 corner[4], float *area, float box[4], float *nearest)
{
	float a[16][3];
	float b[16][3];
	int n;
	int i;
	double sum = 0.0;

	*area = 0.0f;
	for (i = 0; i < 4; i++)
	{
		const EngineVec3 v = Engine_Mat4MulPoint(&cam->viewFromWorld, corner[i]);

		a[i][0] = v.x;
		a[i][1] = v.y;
		a[i][2] = v.z;
	}
	n = EngWater_ClipPlane((const float (*)[3])a, 4, 0.0f, 0.0f, 1.0f, -cam->zNear, b);
	if (n < 3)
	{
		return 0;
	}
	*nearest = b[0][2];
	for (i = 0; i < n; i++)
	{
		if (b[i][2] < *nearest)
		{
			*nearest = b[i][2];
		}
		// To the screen of the view (z kept, not used below).
		b[i][0] = cam->offsetX + cam->H * b[i][0] / b[i][2];
		b[i][1] = cam->offsetY + cam->H * b[i][1] / b[i][2];
	}
	n = EngWater_ClipPlane((const float (*)[3])b, n, 1.0f, 0.0f, 0.0f, 0.0f, a);                   // x >= 0
	n = (n >= 3) ? EngWater_ClipPlane((const float (*)[3])a, n, -1.0f, 0.0f, 0.0f, (float)cam->rectW, b) : 0; // x <= w
	n = (n >= 3) ? EngWater_ClipPlane((const float (*)[3])b, n, 0.0f, 1.0f, 0.0f, 0.0f, a) : 0;               // y >= 0
	n = (n >= 3) ? EngWater_ClipPlane((const float (*)[3])a, n, 0.0f, -1.0f, 0.0f, (float)cam->rectH, b) : 0; // y <= h
	if (n < 3)
	{
		return 0;
	}
	box[0] = box[2] = b[0][0];
	box[1] = box[3] = b[0][1];
	for (i = 0; i < n; i++)
	{
		const float *p = b[i];
		const float *q = b[(i + 1) % n];

		sum += (double)p[0] * q[1] - (double)q[0] * p[1];
		box[0] = (p[0] < box[0]) ? p[0] : box[0];
		box[1] = (p[1] < box[1]) ? p[1] : box[1];
		box[2] = (p[0] > box[2]) ? p[0] : box[2];
		box[3] = (p[1] > box[3]) ? p[1] : box[3];
	}
	*area = (float)(fabs(sum) * 0.5);
	return (*area > 0.0f) ? 1 : 0;
}

// The answer of Engine_WaterOwnsBucket as a table: the PS1 water buckets are
// skipped only when the engine owns the water this frame.
internal int EngWater_Owns(int engineOn, int trackOn, int owned, int bucketIndex)
{
	if (!engineOn || !trackOn || !owned)
	{
		return 0;
	}
	return ((bucketIndex == EGW_BUCKET_WATER_RENDERED) || (bucketIndex == EGW_BUCKET_WATER_LIST)) ? 1 : 0;
}

// --------------------------------------------------------------------------
// Game side

typedef struct
{
	int valid; // this level has engine water
	int levelID;
	u32 trackFrames;   // frames the engine track was drawn
	u32 drawn;         // frames the water draw was submitted
	u32 hidden;        // frames --engine-hide water owned it without drawing
	u32 noProgram;     // frames "nw" was missing (the PS1 water stayed)
	u32 refusals;      // submits refused (no camera, eye, scene or cell)
	u32 bucketsSkipped;
	u32 ps1Left;       // water bucket visits left to the PS1 path while the track drew
	float timeFirst;
	float timeLast;
	int rising;
} EngWaterStat;

global_variable int s_egwMesh = -1;
global_variable int s_egwMaterial = -1;
global_variable u32 s_egwVertices;
global_variable const struct Level *s_egwLevel;
global_variable int s_egwOwned; // this frame the water buckets belong to the engine
global_variable int s_egwReportRegistered;
global_variable EngWaterStat s_egwStat;

internal int EngWater_IsHidden(const struct QuadBlock *qb)
{
	return DrawLevelOvr1P_QuadBlockHasNoTexture(qb);
}

internal void EngWater_LevelSummary(void)
{
	const EngWaterStat *s = &s_egwStat;

	if (!s->valid)
	{
		return;
	}
	Platform_Log("[CTR Engine] water: level %d: drawn %u of %u engine-track frame(s), hidden %u, no program %u, time %.3f..%.3f s, rising %s, "
	             "PS1 water buckets skipped %u, left to the PS1 path while the track drew %u, submit refusals %u\n",
	             s->levelID, s->drawn, s->trackFrames, s->hidden, s->noProgram, s->timeFirst, s->timeLast, s->rising ? "yes" : "no", s->bucketsSkipped,
	             s->ps1Left, s->refusals);
}

internal void EngWater_Report(void)
{
	EngWater_LevelSummary();
	s_egwStat.valid = 0;
}

// Whether the mesh made at load is still the live one (mesh slots are reused
// after a level unload).
internal int EngWater_MeshLive(void)
{
	const EngineMeshDesc *d;
	u32 vb = 0xffffffffu;
	u32 ib = 0xffffffffu;

	if (s_egwMesh < 0)
	{
		return 0;
	}
	d = Engine_MeshDesc(s_egwMesh);
	if ((d == NULL) || (d->name == NULL) || (strcmp(d->name, "water") != 0) || (d->verticesPerFrame != s_egwVertices))
	{
		return 0;
	}
	Engine_MeshGetGpu(s_egwMesh, &vb, &ib);
	return ((vb != 0xffffffffu) && (ib != 0xffffffffu)) ? 1 : 0;
}

internal int EngWater_Active(const struct GameTracker *gGT)
{
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->numPlyrCurrGame != 1))
	{
		return 0;
	}
	if (((gGT->gameMode1 & MAIN_MENU) != 0) || (gGT->level1 == NULL) || (gGT->level1 != s_egwLevel))
	{
		return 0;
	}
	return EngWater_MeshLive();
}

// The env map region: the smallest u and v of the layout's corners. 0 when the
// layout cannot carry a 64 x 64 region inside its page.
internal int EngWater_EnvRegion(const struct TextureLayout *lay, int *u0, int *v0)
{
	const int us[4] = {lay->u0, lay->u1, lay->u2, lay->u3};
	const int vs[4] = {lay->v0, lay->v1, lay->v2, lay->v3};
	int i;

	*u0 = us[0];
	*v0 = vs[0];
	for (i = 1; i < 4; i++)
	{
		*u0 = (us[i] < *u0) ? us[i] : *u0;
		*v0 = (vs[i] < *v0) ? vs[i] : *v0;
	}
	return (((lay->tpage & 0xfe00u) == 0u) && ((*u0 + EGW_ENV_TEXELS) <= 256) && ((*v0 + EGW_ENV_TEXELS) <= 256)) ? 1 : 0;
}

// At load stage 8, after Engine_TrackLoad, before Engine_RenderUploadMeshes.
void Engine_WaterLoad(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Level *level;
	const struct mesh_info *mesh;
	const struct TextureLayout *env;
	EngWaterBuild build;
	EngineMaterial m;
	EngineMeshDesc desc;
	EngineRange range;
	u8 *water = NULL;
	const char *reason = NULL;
	int leaves;
	int badLeaves = 0;
	int marked = 0;
	int u0 = 0;
	int v0 = 0;
	int i;

	EngWater_LevelSummary();
	memset(&s_egwStat, 0, sizeof(s_egwStat));
	memset(&build, 0, sizeof(build));
	s_egwMesh = -1;
	s_egwMaterial = -1;
	s_egwVertices = 0;
	s_egwLevel = NULL;
	s_egwOwned = 0;
	if (!g_cfg_engine || !g_cfg_engineTrack || (gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}
	if (!s_egwReportRegistered)
	{
		s_egwReportRegistered = 1;
		Platform_AtExitReport(EngWater_Report);
	}
	level = gGT->level1;
	mesh = level->ptr_mesh_info;
	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL) || (mesh->ptrVertexArray == NULL) || (mesh->numQuadBlock <= 0) || (mesh->numVertex <= 0))
	{
		return;
	}
	water = (u8 *)calloc((size_t)mesh->numQuadBlock, 1u);
	if (water == NULL)
	{
		Platform_Log("[CTR Engine] water: level %d: out of memory\n", gGT->levelID);
		return;
	}
	leaves = EngWater_MarkBlocks(mesh, water, &badLeaves);
	for (i = 0; i < mesh->numQuadBlock; i++)
	{
		marked += water[i];
	}
	if (marked == 0)
	{
		Platform_Log("[CTR Engine] water: level %d: no water\n", gGT->levelID);
		goto done;
	}

	env = level->ptr_tex_waterEnvMap;
	if ((env == NULL) || !DrawLevelOvr1P_IsNativeLevelSpan((u32)(size_t)env, (u32)sizeof(*env)))
	{
		reason = "no env map";
	}
	else if (!EngWater_EnvRegion(env, &u0, &v0))
	{
		reason = "env region outside its page";
	}
	else if (!EngWater_Build(mesh->ptrVertexArray, mesh->numVertex, mesh->ptrQuadBlockArray, mesh->numQuadBlock, water, EngWater_IsHidden, &build))
	{
		reason = "mesh build failed (memory or more than 65535 vertices)";
	}
	else if (build.indexCount == 0u)
	{
		reason = "no triangles";
	}
	if (reason == NULL)
	{
		memset(&m, 0, sizeof(m));
		m.blend = ENGINE_BLEND_PSX_ADD;
		m.texSource = ENGINE_TEX_VRAM;
		m.flags = (u16)(ENGINE_MATERIAL_DOUBLE_SIDED | ENGINE_MATERIAL_NO_DEPTH_WRITE);
		m.tpage = env->tpage;
		m.clut = env->clut;
		m.tint[0] = m.tint[1] = m.tint[2] = m.tint[3] = 1.0f;
		m.program = ENGINE_PROGRAM_NW;
		m.programParam[0] = (float)u0;
		m.programParam[1] = (float)v0;
		s_egwMaterial = Engine_MaterialRegister(&m);
		if (s_egwMaterial < 0)
		{
			reason = "material refused";
		}
	}
	if (reason == NULL)
	{
		memset(&range, 0, sizeof(range));
		range.firstIndex = 0;
		range.indexCount = build.indexCount;
		range.material = (u16)s_egwMaterial;
		memset(&desc, 0, sizeof(desc));
		desc.vertices = build.vertices;
		desc.verticesPerFrame = build.vertexCount;
		desc.frames = 1;
		desc.indices = build.indices;
		desc.indexCount = build.indexCount;
		desc.ranges = &range;
		desc.rangeCount = 1;
		desc.name = "water";
		s_egwMesh = Engine_MeshCreate(&desc);
		if (s_egwMesh < 0)
		{
			reason = "mesh refused";
		}
	}
	if (reason != NULL)
	{
		Platform_Log("[CTR Engine] water: level %d: %d block(s) left to the PS1 path (%s)\n", gGT->levelID, marked, reason);
		s_egwMesh = -1;
		goto done;
	}
	s_egwVertices = build.vertexCount;
	s_egwLevel = level;
	s_egwStat.valid = 1;
	s_egwStat.levelID = gGT->levelID;
	s_egwStat.rising = 1;
	Platform_Log("[CTR Engine] water: level %d: %d block(s) in %d water leaf(s), %d hidden, %d triangle(s), %u vertices, env tpage 0x%04x clut 0x%04x "
	             "origin %d,%d, mesh %d, material %d\n",
	             gGT->levelID, build.blocks, leaves, build.hidden, (int)(build.indexCount / 3u), build.vertexCount, env->tpage, env->clut, u0, v0, s_egwMesh,
	             s_egwMaterial);
	if ((badLeaves != 0) || (build.badBlocks != 0) || (build.degenerate != 0))
	{
		Platform_Log("[CTR Engine] water: level %d: also %d leaf(s) with a bad block range, %d block(s) with a bad vertex index, %d degenerate "
		             "triangle(s) dropped\n",
		             gGT->levelID, badLeaves, build.badBlocks, build.degenerate);
	}

done:
	EngWater_BuildFree(&build);
	free(water);
}

// Once per frame from Engine_FrameCollect, after Engine_TrackCollect.
void Engine_WaterCollect(struct GameTracker *gGT)
{
	const EngineCamera *cam;
	EngineDraw draw;
	float eye[3];
	float time;

	s_egwOwned = 0;
	if (!EngWater_Active(gGT) || !Engine_TrackDrawnThisFrame())
	{
		return;
	}
	s_egwStat.trackFrames++;
	// --engine-hide water: the engine owns the water and draws nothing (the minus run).
	if ((g_cfg_engineHide & ENGINE_HIDE_WATER) != 0)
	{
		s_egwOwned = 1;
		s_egwStat.hidden++;
		return;
	}
	if (Engine_ProgramOf(ENGINE_PROGRAM_NW) == 0xffffffffu)
	{
		s_egwStat.noProgram++;
		return;
	}
	cam = Engine_FrameCamera(0);
	if ((cam == NULL) || !EngWater_Eye(&cam->viewFromWorld, eye))
	{
		s_egwStat.refusals++;
		return;
	}
	memset(&draw, 0, sizeof(draw));
	draw.mesh = s_egwMesh;
	draw.frame = 0;
	Engine_CameraScreenFromWorld(cam, &draw.clipFromModel); // the mesh is in world units
	draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
	draw.materialOverride = -1;
	draw.sortDepth = 1.0e9f;
	draw.otCell = EGW_CELL;
	draw.flags = ENGINE_DRAW_TRANSPARENT;
	draw.dynMaterial = -1;
	draw.user[0] = eye[0];
	draw.user[1] = eye[1];
	draw.user[2] = eye[2];
	if (!Engine_RenderSubmitLinked(0, &draw, gGT->pushBuffer[0].ptrOT, EGW_CELL))
	{
		s_egwStat.refusals++;
		return;
	}
	s_egwOwned = 1;
	time = (float)gGT->timer / EGW_FRAMES_PER_SECOND;
	if (s_egwStat.drawn == 0u)
	{
		s_egwStat.timeFirst = time;
	}
	else if (!(time > s_egwStat.timeLast))
	{
		s_egwStat.rising = 0;
	}
	s_egwStat.timeLast = time;
	s_egwStat.drawn++;
}

// 1 = the PS1 walk of DrawLevelOvr1P treats this bucket as empty this frame:
// 8 and 9 while the engine owns the water.
int Engine_WaterOwnsBucket(int bucketIndex)
{
	const int owns = EngWater_Owns(g_cfg_engine, g_cfg_engineTrack, s_egwOwned, bucketIndex);
	const struct GameTracker *gGT = sdata->gGT;

	if (owns)
	{
		s_egwStat.bucketsSkipped++;
	}
	else if (g_cfg_engine && g_cfg_engineTrack && ((bucketIndex == EGW_BUCKET_WATER_RENDERED) || (bucketIndex == EGW_BUCKET_WATER_LIST)) &&
	         (s_egwLevel != NULL) && (gGT != NULL) && (gGT->level1 == s_egwLevel) && Engine_TrackDrawnThisFrame())
	{
		s_egwStat.ps1Left++;
	}
	return owns;
}

int Engine_WaterProbe(const struct GameTracker *gGT, int view, EngineWaterProbe *out)
{
	const struct mesh_info *mesh;
	EngineCamera cam;
	double areaSum = 0.0;
	int n;

	if (out == NULL)
	{
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if ((gGT == NULL) || (view < 0) || (view >= ENGINE_VIEWS) || (gGT->level1 == NULL))
	{
		return 0;
	}
	mesh = gGT->level1->ptr_mesh_info;
	if ((mesh == NULL) || (mesh->bspRoot == NULL) || (mesh->ptrQuadBlockArray == NULL) || (mesh->ptrVertexArray == NULL) ||
	    !Engine_CameraFromPushBuffer(&gGT->pushBuffer[view], view, &cam))
	{
		return 0;
	}
	for (n = 0; n < mesh->numBspNodes; n++)
	{
		const struct BSP *node = &mesh->bspRoot[n];
		ptrdiff_t first;
		int i;

		if (((node->flag & BSP_NODE_FLAG_LEAF) == 0) || ((node->flag & BSP_LEAF_FLAG_WATER) == 0) || (node->data.leaf.ptrQuadBlockArray == NULL))
		{
			continue;
		}
		first = node->data.leaf.ptrQuadBlockArray - mesh->ptrQuadBlockArray;
		if ((first < 0) || (first + node->data.leaf.numQuads > mesh->numQuadBlock))
		{
			continue;
		}
		for (i = 0; i < node->data.leaf.numQuads; i++)
		{
			const struct QuadBlock *qb = &node->data.leaf.ptrQuadBlockArray[i];
			static const int order[4] = {0, 1, 3, 2}; // the corners as a polygon
			EngineVec3 corner[4];
			float area;
			float box[4];
			float nearest = 0.0f;
			int k;
			int bad = 0;

			for (k = 0; k < 4; k++)
			{
				bad |= ((int)qb->index[order[k]] >= mesh->numVertex);
			}
			if (bad || DrawLevelOvr1P_QuadBlockHasNoTexture(qb))
			{
				continue;
			}
			out->blocks++;
			for (k = 0; k < 4; k++)
			{
				const struct LevVertex *lv = &mesh->ptrVertexArray[qb->index[order[k]]];

				corner[k].x = (float)lv->pos.x;
				corner[k].y = (float)lv->pos.y;
				corner[k].z = (float)lv->pos.z;
			}
			if (!EngWater_ClipQuad(&cam, corner, &area, box, &nearest))
			{
				continue;
			}
			if (out->onScreen == 0)
			{
				memcpy(out->box, box, sizeof(box));
				out->nearestDepth = nearest;
			}
			out->box[0] = (box[0] < out->box[0]) ? box[0] : out->box[0];
			out->box[1] = (box[1] < out->box[1]) ? box[1] : out->box[1];
			out->box[2] = (box[2] > out->box[2]) ? box[2] : out->box[2];
			out->box[3] = (box[3] > out->box[3]) ? box[3] : out->box[3];
			out->nearestDepth = (nearest < out->nearestDepth) ? nearest : out->nearestDepth;
			out->onScreen++;
			areaSum += area;
		}
	}
	out->share = (float)EngWater_Clamp(areaSum / ((double)cam.rectW * (double)cam.rectH), 0.0, 1.0);
	if (out->onScreen > 0)
	{
		out->box[0] /= (float)cam.rectW;
		out->box[1] /= (float)cam.rectH;
		out->box[2] /= (float)cam.rectW;
		out->box[3] /= (float)cam.rectH;
	}
	return 1;
}

// The probe as a log line, for the tour (every 30 race frames; frame is the
// tour's race frame, the number --level-tour-shots uses). Quiet on a level
// without water blocks.
void Engine_WaterProbeLog(const struct GameTracker *gGT, int view, int frame, const char *tag)
{
	EngineWaterProbe p;

	if (!Engine_WaterProbe(gGT, view, &p) || (p.blocks == 0))
	{
		return;
	}
	Platform_Log("[CTR Engine] water probe %s: level %d frame %d view %d: %d of %d block(s) on screen, share %.3f, box %.3f,%.3f-%.3f,%.3f, nearest %.0f\n",
	             (tag != NULL) ? tag : "-", gGT->levelID, frame, view, p.onScreen, p.blocks, p.share, p.box[0], p.box[1], p.box[2], p.box[3], p.nearestDepth);
}

// --------------------------------------------------------------------------
// Selftest

#define EGW_CHECK(cond, ...)                                \
	do                                                      \
	{                                                       \
		(*checks)++;                                        \
		if (!(cond))                                        \
		{                                                   \
			printf("engine selftest: water " __VA_ARGS__);  \
			printf("\n");                                   \
			failures++;                                     \
		}                                                   \
	} while (0)

// A 5 x 3 vertex grid, 100 units apart in x and z; y = slope * x.
internal void EngWater_TestGrid(struct LevVertex *v, float slope)
{
	int ix;
	int iz;

	memset(v, 0, 15u * sizeof(*v));
	for (iz = 0; iz < 3; iz++)
	{
		for (ix = 0; ix < 5; ix++)
		{
			struct LevVertex *p = &v[iz * 5 + ix];

			p->pos.x = (s16)(ix * 100);
			p->pos.y = (s16)(slope * (float)(ix * 100));
			p->pos.z = (s16)(iz * 100);
		}
	}
}

// A block of the grid with its left edge at column ix0 (0 or 2).
internal void EngWater_TestBlock(struct QuadBlock *qb, int ix0)
{
	static const int cell[9][2] = {{0, 0}, {2, 0}, {0, 2}, {2, 2}, {1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}};
	int k;

	memset(qb, 0, sizeof(*qb));
	for (k = 0; k < 9; k++)
	{
		qb->index[k] = (u16)(cell[k][1] * 5 + ix0 + cell[k][0]);
	}
}

internal int EngWater_TestHidden(const struct QuadBlock *qb)
{
	return (qb->blockID == 99) ? 1 : 0;
}

internal float EngWater_TestUDist(float a, float b)
{
	const float d = fabsf(a - b);

	return (d > 32.0f) ? (64.0f - d) : d;
}

int Engine_SelfTestWater(int *checks)
{
	int failures = 0;
	struct LevVertex verts[15];
	struct QuadBlock blocks[3];
	EngWaterBuild b;
	u8 water[3];

	// (1) leaf marking: a water leaf, a dry leaf, a branch, a water leaf with a range past the array.
	{
		struct BSP nodes[4];
		struct mesh_info mesh;
		int bad = -1;
		int leaves;

		memset(nodes, 0, sizeof(nodes));
		memset(&mesh, 0, sizeof(mesh));
		memset(water, 0, sizeof(water));
		nodes[0].flag = BSP_NODE_FLAG_LEAF | BSP_LEAF_FLAG_WATER;
		nodes[0].data.leaf.ptrQuadBlockArray = &blocks[0];
		nodes[0].data.leaf.numQuads = 2;
		nodes[1].flag = BSP_NODE_FLAG_LEAF;
		nodes[1].data.leaf.ptrQuadBlockArray = &blocks[2];
		nodes[1].data.leaf.numQuads = 1;
		nodes[2].flag = BSP_LEAF_FLAG_WATER; // a branch
		nodes[3].flag = BSP_NODE_FLAG_LEAF | BSP_LEAF_FLAG_WATER;
		nodes[3].data.leaf.ptrQuadBlockArray = &blocks[2];
		nodes[3].data.leaf.numQuads = 2; // one past the end
		mesh.numQuadBlock = 3;
		mesh.ptrQuadBlockArray = blocks;
		mesh.bspRoot = nodes;
		mesh.numBspNodes = 4;
		leaves = EngWater_MarkBlocks(&mesh, water, &bad);
		EGW_CHECK((leaves == 1) && (bad == 1) && water[0] && water[1] && !water[2], "leaf marking: %d leaves, %d bad, marks %d %d %d", leaves, bad, water[0],
		          water[1], water[2]);
	}

	// (2) two flat blocks sharing an edge: 15 vertices, 16 triangles, normals up, indices in range.
	EngWater_TestGrid(verts, 0.0f);
	EngWater_TestBlock(&blocks[0], 0);
	EngWater_TestBlock(&blocks[1], 2);
	EngWater_TestBlock(&blocks[2], 0);
	water[0] = water[1] = 1;
	water[2] = 0;
	EGW_CHECK(EngWater_Build(verts, 15, blocks, 3, water, NULL, &b), "build failed");
	{
		int bad = 0;
		u32 i;

		EGW_CHECK((b.vertexCount == 15u) && (b.indexCount == 48u) && (b.blocks == 2) && (b.degenerate == 0), "shared edge: %u vertices, %u indices, %d blocks",
		          b.vertexCount, b.indexCount, b.blocks);
		for (i = 0; i < b.indexCount; i++)
		{
			bad += (b.indices[i] >= b.vertexCount);
		}
		for (i = 0; i < b.vertexCount; i++)
		{
			bad += (fabsf(b.vertices[i].texcoord[0]) > 1.0e-6f) || (fabsf(b.vertices[i].texcoord[1]) > 1.0e-6f) || (b.vertices[i].color[3] != 255);
		}
		EGW_CHECK(bad == 0, "flat blocks: %d bad index/normal/colour entries", bad);
	}
	EngWater_BuildFree(&b);

	// (3) the zero rule and a bad vertex index skip their blocks.
	blocks[1].blockID = 99;
	blocks[0].index[8] = 15;
	EGW_CHECK(EngWater_Build(verts, 15, blocks, 3, water, EngWater_TestHidden, &b) && (b.hidden == 1) && (b.badBlocks == 1) && (b.indexCount == 0u),
	          "hidden %d, bad %d, indices %u", b.hidden, b.badBlocks, b.indexCount);
	EngWater_BuildFree(&b);
	blocks[1].blockID = 0;
	EngWater_TestBlock(&blocks[0], 0);

	// (4) a repeated corner (a triangle block): its degenerate triangles are dropped.
	blocks[0].index[3] = blocks[0].index[2];
	blocks[0].index[7] = blocks[0].index[8];
	water[1] = 0;
	EGW_CHECK(EngWater_Build(verts, 15, blocks, 3, water, NULL, &b) && (b.degenerate > 0) && (b.indexCount == (8u - (u32)b.degenerate) * 3u),
	          "repeated corner: %d degenerate, %u indices", b.degenerate, b.indexCount);
	EngWater_BuildFree(&b);
	EngWater_TestBlock(&blocks[0], 0);

	// (5) a slope rising in x: the normal leans to -x (downhill), z about 0.
	EngWater_TestGrid(verts, 0.2f);
	water[0] = 1;
	water[1] = 0;
	EGW_CHECK(EngWater_Build(verts, 15, blocks, 3, water, NULL, &b) && (b.vertexCount == 9u), "slope build: %u vertices", b.vertexCount);
	{
		const float expect = -0.2f / sqrtf(1.04f);
		int bad = 0;
		u32 i;

		for (i = 0; i < b.vertexCount; i++)
		{
			bad += (fabsf(b.vertices[i].texcoord[0] - expect) > 0.01f) || (fabsf(b.vertices[i].texcoord[1]) > 0.01f);
		}
		EGW_CHECK(bad == 0, "slope: %d vertices without the downhill normal", bad);
	}
	EngWater_BuildFree(&b);

	// (6) the lookup. Ripples off (depth past EGW_RIPPLE_ZERO, fade still 1).
	{
		const float point[3] = {0.0f, 0.0f, 0.0f};
		const float above[3] = {0.0f, 1000.0f, 0.0f};
		const float grazing[3] = {0.0f, 10.0f, -5000.0f};
		const float fromLeftA[3] = {-1000.0f, 500.0f, -1.0f};
		const float fromLeftB[3] = {-1000.0f, 500.0f, 1.0f};
		const float still = (float)EGW_RIPPLE_ZERO + 1000.0f;
		EngWaterLook down = EngWater_Lookup(above, point, 0.0f, 0.0f, still, 0.0f);
		EngWaterLook flat = EngWater_Lookup(grazing, point, 0.0f, 0.0f, still, 0.0f);
		EngWaterLook a = EngWater_Lookup(fromLeftA, point, 0.0f, 0.0f, still, 0.0f);
		EngWaterLook c = EngWater_Lookup(fromLeftB, point, 0.0f, 0.0f, still, 0.0f);
		EngWaterLook later = EngWater_Lookup(grazing, point, 0.0f, 0.0f, still, 10.0f);

		EGW_CHECK(down.v < 0.5f, "straight down: v %.3f, expected the zenith row 0", down.v);
		EGW_CHECK((flat.v > 31.0f) && (flat.v <= 32.0f), "grazing: v %.3f, expected about the horizon row 32", flat.v);
		EGW_CHECK((EngWater_TestUDist(a.u, c.u) < 0.1f) && (fabsf(a.u - c.u) > 60.0f), "azimuth wrap: u %.3f and %.3f", a.u, c.u);
		EGW_CHECK((fabsf(later.u - flat.u) < 1.0e-4f) && (fabsf(later.v - flat.v) < 1.0e-4f), "ripples past EGW_RIPPLE_ZERO still move: %.5f %.5f",
		          later.u - flat.u, later.v - flat.v);
		EGW_CHECK(fabsf(down.intensity - (float)(EGW_K_BASE + EGW_K_FRESNEL * EGW_F0)) < 1.0e-3f, "straight down: intensity %.4f", down.intensity);
		EGW_CHECK(flat.intensity > down.intensity + 0.3f, "fresnel: grazing %.3f not brighter than straight down %.3f", flat.intensity, down.intensity);
		EGW_CHECK((EngWater_Lookup(above, point, 0.0f, 0.0f, 1000.0f, 0.0f).fade == 1.0f) &&
		              (EngWater_Lookup(above, point, 0.0f, 0.0f, (float)EGW_FADE_END, 0.0f).fade == 0.0f) &&
		              (fabsf(EngWater_Lookup(above, point, 0.0f, 0.0f, (float)(0.5 * (EGW_FADE_START + EGW_FADE_END)), 0.0f).fade - 0.5f) < 1.0e-4f),
		          "fade: not 1 near, 0.5 half way, 0 at EGW_FADE_END");
	}

	// (7) animated, slowly: per game frame the lookup moves 0.01 .. 0.5 texel on average and never 0.5 or more.
	{
		const float eye[3] = {0.0f, 300.0f, -1500.0f};
		double sum = 0.0;
		float most = 0.0f;
		int samples = 0;
		int ix;
		int iz;
		int t;

		for (t = 0; t < 4; t++)
		{
			for (iz = 0; iz < 5; iz++)
			{
				for (ix = 0; ix < 5; ix++)
				{
					const float p[3] = {(float)(ix * 173 - 400), 0.0f, (float)(iz * 211)};
					const float time = 3.0f + 7.3f * (float)t;
					const EngWaterLook l0 = EngWater_Lookup(eye, p, 0.0f, 0.0f, 1200.0f, time);
					const EngWaterLook l1 = EngWater_Lookup(eye, p, 0.0f, 0.0f, 1200.0f, time + 1.0f / EGW_FRAMES_PER_SECOND);
					const float du = EngWater_TestUDist(l0.u, l1.u);
					const float dv = l1.v - l0.v;
					const float d = sqrtf(du * du + dv * dv);

					sum += d;
					most = (d > most) ? d : most;
					samples++;
				}
			}
		}
		EGW_CHECK((sum / samples >= 0.01) && (sum / samples <= 0.5) && (most < 0.5f), "motion per frame: mean %.4f, max %.4f texel", sum / samples, most);
	}

	// (8) the eye from a view matrix: it maps to the view origin.
	{
		static const s16 r[3][3] = {{3547, 0, -2048}, {0, 4096, 0}, {2048, 0, 3547}}; // 30 degrees about y
		static const s32 t[3] = {120, -340, 2500};
		EngineMat4 m;
		float eye[3];
		EngineVec3 e;
		EngineVec3 v;

		Engine_Mat4FromGte(r, t, &m);
		EGW_CHECK(EngWater_Eye(&m, eye), "eye: matrix not invertible");
		e.x = eye[0];
		e.y = eye[1];
		e.z = eye[2];
		v = Engine_Mat4MulPoint(&m, e);
		EGW_CHECK((fabsf(v.x) < 0.05f) && (fabsf(v.y) < 0.05f) && (fabsf(v.z) < 0.05f), "eye: maps to %.3f %.3f %.3f", v.x, v.y, v.z);
	}

	// (9) the probe's quad clip: in front, behind, across the near plane, partly off screen.
	{
		EngineCamera cam;
		EngineVec3 q[4];
		float area = -1.0f;
		float box[4] = {0, 0, 0, 0};
		float nearest = 0.0f;
		int on;

		memset(&cam, 0, sizeof(cam));
		Engine_Mat4Identity(&cam.viewFromWorld);
		cam.valid = 1;
		cam.H = 256.0f;
		cam.offsetX = 256.0f;
		cam.offsetY = 120.0f;
		cam.rectW = 512;
		cam.rectH = 240;
		cam.zNear = 32.0f;
		q[0].x = -100.0f; q[0].y = -100.0f; q[0].z = 1000.0f;
		q[1].x = 100.0f;  q[1].y = -100.0f; q[1].z = 1000.0f;
		q[2].x = 100.0f;  q[2].y = 100.0f;  q[2].z = 1000.0f;
		q[3].x = -100.0f; q[3].y = 100.0f;  q[3].z = 1000.0f;
		on = EngWater_ClipQuad(&cam, q, &area, box, &nearest);
		EGW_CHECK(on && (fabsf(area - 51.2f * 51.2f) < 1.0f) && (fabsf(box[0] - 230.4f) < 0.01f) && (fabsf(box[3] - 145.6f) < 0.01f) &&
		              (fabsf(nearest - 1000.0f) < 0.01f),
		          "probe in front: on %d, area %.2f, box %.2f..%.2f", on, area, box[0], box[3]);
		q[0].z = q[1].z = q[2].z = q[3].z = -1000.0f;
		EGW_CHECK(!EngWater_ClipQuad(&cam, q, &area, box, &nearest), "probe behind the camera is on screen");
		// A floor quad below the camera reaching from behind it to far ahead.
		q[0].x = -100.0f; q[0].y = 50.0f; q[0].z = -500.0f;
		q[1].x = 100.0f;  q[1].y = 50.0f; q[1].z = -500.0f;
		q[2].x = 100.0f;  q[2].y = 50.0f; q[2].z = 1000.0f;
		q[3].x = -100.0f; q[3].y = 50.0f; q[3].z = 1000.0f;
		on = EngWater_ClipQuad(&cam, q, &area, box, &nearest);
		EGW_CHECK(on && (area > 0.0f) && (box[0] >= 0.0f) && (box[2] <= 512.0f) && (box[1] >= 0.0f) && (box[3] <= 240.0f) && (fabsf(nearest - 32.0f) < 0.01f),
		          "probe across the near plane: on %d, area %.2f, box %.1f,%.1f-%.1f,%.1f, nearest %.2f", on, area, box[0], box[1], box[2], box[3], nearest);
		// Wider than the view: clipped to the rectangle.
		q[0].x = -5000.0f; q[0].y = -10.0f; q[0].z = 1000.0f;
		q[1].x = 5000.0f;  q[1].y = -10.0f; q[1].z = 1000.0f;
		q[2].x = 5000.0f;  q[2].y = 10.0f;  q[2].z = 1000.0f;
		q[3].x = -5000.0f; q[3].y = 10.0f;  q[3].z = 1000.0f;
		on = EngWater_ClipQuad(&cam, q, &area, box, &nearest);
		EGW_CHECK(on && (fabsf(box[0]) < 0.01f) && (fabsf(box[2] - 512.0f) < 0.01f) && (fabsf(area - 512.0f * 5.12f) < 1.0f), "probe wide: area %.2f, box %.2f..%.2f",
		          area, box[0], box[2]);
	}

	// (10) the bucket answer.
	{
		int bad = 0;
		int bucket;

		for (bucket = 0; bucket < 11; bucket++)
		{
			const int water89 = (bucket == EGW_BUCKET_WATER_RENDERED) || (bucket == EGW_BUCKET_WATER_LIST);

			bad += EngWater_Owns(0, 1, 1, bucket) || EngWater_Owns(1, 0, 1, bucket) || EngWater_Owns(1, 1, 0, bucket);
			bad += (EngWater_Owns(1, 1, 1, bucket) != water89);
		}
		EGW_CHECK(bad == 0, "bucket answer: %d wrong", bad);
	}

	return failures;
}
