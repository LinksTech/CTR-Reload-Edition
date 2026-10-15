#include <platform/engine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE SCENE - see include/platform/engine.h.
//
// The per-view draw lists of the frames in flight. The lists are static
// storage, the dynamic rings heap storage made by Engine_SceneRingsAlloc when
// the engine starts (Engine_RenderInit; without --engine they never exist),
// once per bank (one bank per double buffer of the game):
//   - draws[view]   : the submitted draws in submission order
//   - sorted[view]  : the same draws as copies in drawing order, rebuilt lazily
//                     by Engine_SceneSortedBank when the list changed
//   - dynVertices / dynIndices : one ring shared by the views of the bank that
//                     holds the dynamic geometry of the frame
// Engine_SceneSorted hands out a pointer to sorted[view] of a bank; it stays
// valid until the next Engine_SceneSubmit or Engine_SceneBeginFrame on THAT
// bank (the ring copies the sorted draws point to live until BeginFrame of
// that bank). The other bank is never touched by them.

#define SCENE_DYN_INDICES_PER_FRAME (ENGINE_DYN_VERTICES_PER_FRAME * 3 / 2)

typedef struct
{
	EngineDraw draws[ENGINE_VIEWS][ENGINE_DRAWS_PER_VIEW];
	EngineDraw sorted[ENGINE_VIEWS][ENGINE_DRAWS_PER_VIEW];
	u16 order[ENGINE_VIEWS][ENGINE_DRAWS_PER_VIEW];
	int count[ENGINE_VIEWS];
	int dirty[ENGINE_VIEWS];
	EngineVertex *dynVertices; // ENGINE_DYN_VERTICES_PER_FRAME, NULL until Engine_SceneRingsAlloc
	u16 *dynIndices;           // SCENE_DYN_INDICES_PER_FRAME
	u32 dynVertexUsed;
	u32 dynIndexUsed;
} SceneBank;

global_variable SceneBank g_sceneBanks[ENGINE_SCENE_BANKS];
global_variable int g_sceneBank;
global_variable u32 g_sceneOverflows;

int Engine_SceneRingsAlloc(void)
{
	int bank;

	for (bank = 0; bank < ENGINE_SCENE_BANKS; bank++)
	{
		SceneBank *b = &g_sceneBanks[bank];

		if (b->dynVertices == NULL)
		{
			b->dynVertices = (EngineVertex *)malloc((size_t)ENGINE_DYN_VERTICES_PER_FRAME * sizeof(EngineVertex));
		}
		if (b->dynIndices == NULL)
		{
			b->dynIndices = (u16 *)malloc((size_t)SCENE_DYN_INDICES_PER_FRAME * sizeof(u16));
		}
		if ((b->dynVertices == NULL) || (b->dynIndices == NULL))
		{
			Engine_SceneRingsFree();
			return 0;
		}
		b->dynVertexUsed = 0;
		b->dynIndexUsed = 0;
	}
	return 1;
}

void Engine_SceneRingsFree(void)
{
	int bank;

	for (bank = 0; bank < ENGINE_SCENE_BANKS; bank++)
	{
		SceneBank *b = &g_sceneBanks[bank];

		free(b->dynVertices);
		free(b->dynIndices);
		b->dynVertices = NULL;
		b->dynIndices = NULL;
		b->dynVertexUsed = 0;
		b->dynIndexUsed = 0;
	}
}

u32 Engine_SceneRingsBytes(void)
{
	return (u32)ENGINE_SCENE_BANKS * (((u32)ENGINE_DYN_VERTICES_PER_FRAME * (u32)sizeof(EngineVertex)) + ((u32)SCENE_DYN_INDICES_PER_FRAME * (u32)sizeof(u16)));
}

void Engine_SceneSelectBank(int bank)
{
	if (bank >= 0 && bank < ENGINE_SCENE_BANKS)
	{
		g_sceneBank = bank;
	}
}

int Engine_SceneBank(void)
{
	return g_sceneBank;
}

void Engine_SceneBeginFrame(void)
{
	SceneBank *b = &g_sceneBanks[g_sceneBank];
	for (int v = 0; v < ENGINE_VIEWS; v++)
	{
		b->count[v] = 0;
		b->dirty[v] = 1;
	}
	b->dynVertexUsed = 0;
	b->dynIndexUsed = 0;
}

int Engine_SceneSubmit(int view, const EngineDraw *draw)
{
	if (view < 0 || view >= ENGINE_VIEWS || draw == NULL)
	{
		return -1;
	}
	if (draw->dynVertexCount > 0)
	{
		if (draw->dynVertices == NULL || draw->dynIndices == NULL || draw->dynIndexCount == 0)
		{
			return -1;
		}
	}
	SceneBank *b = &g_sceneBanks[g_sceneBank];
	if (b->count[view] >= ENGINE_DRAWS_PER_VIEW)
	{
		g_sceneOverflows++;
		return -1;
	}
	if (draw->dynVertexCount > 0)
	{
		// No rings (not allocated, or the allocation failed): refused like a full ring.
		if (b->dynVertices == NULL || b->dynIndices == NULL || draw->dynVertexCount > ENGINE_DYN_VERTICES_PER_FRAME - b->dynVertexUsed ||
		    draw->dynIndexCount > SCENE_DYN_INDICES_PER_FRAME - b->dynIndexUsed)
		{
			g_sceneOverflows++;
			return -1;
		}
	}
	int index = b->count[view];
	EngineDraw *stored = &b->draws[view][index];
	*stored = *draw;
	if (draw->dynVertexCount > 0)
	{
		EngineVertex *rv = &b->dynVertices[b->dynVertexUsed];
		u16 *ri = &b->dynIndices[b->dynIndexUsed];
		memcpy(rv, draw->dynVertices, (size_t)draw->dynVertexCount * sizeof(EngineVertex));
		memcpy(ri, draw->dynIndices, (size_t)draw->dynIndexCount * sizeof(u16));
		stored->dynVertices = rv;
		stored->dynIndices = ri;
		b->dynVertexUsed += draw->dynVertexCount;
		b->dynIndexUsed += draw->dynIndexCount;
	}
	b->count[view] = index + 1;
	b->dirty[view] = 1;
	return index;
}

internal int Scene_EffectiveMaterial(const EngineDraw *d)
{
	if (d->materialOverride >= 0)
	{
		return d->materialOverride;
	}
	return d->mesh < 0 ? d->dynMaterial : -1;
}

// Strict "a must be drawn before b" for the opaque group.
internal int Scene_OpaqueBefore(const EngineDraw *a, const EngineDraw *b)
{
	if (a->mesh != b->mesh)
	{
		return a->mesh < b->mesh;
	}
	int ma = Scene_EffectiveMaterial(a);
	int mb = Scene_EffectiveMaterial(b);
	if (ma != mb)
	{
		return ma < mb;
	}
	return a->frame < b->frame;
}

// Stable insertion sort of order[0..n) with a strict "before" predicate.
// n is at most ENGINE_DRAWS_PER_VIEW, so O(n^2) is fine and stays stable.
internal void Scene_SortRange(u16 *order, int n, const EngineDraw *draws, int transparent)
{
	for (int i = 1; i < n; i++)
	{
		u16 key = order[i];
		int j = i - 1;
		while (j >= 0)
		{
			const EngineDraw *prev = &draws[order[j]];
			const EngineDraw *cur = &draws[key];
			int curFirst = transparent ? (cur->sortDepth > prev->sortDepth) : Scene_OpaqueBefore(cur, prev);
			if (!curFirst)
			{
				break;
			}
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = key;
	}
}

int Engine_SceneSortedBank(int bank, int view, const EngineDraw **out)
{
	if (out != NULL)
	{
		*out = NULL;
	}
	if (bank < 0 || bank >= ENGINE_SCENE_BANKS || view < 0 || view >= ENGINE_VIEWS)
	{
		return 0;
	}
	SceneBank *b = &g_sceneBanks[bank];
	int n = b->count[view];
	if (b->dirty[view])
	{
		const EngineDraw *draws = b->draws[view];
		u16 *order = b->order[view];
		int opaque = 0;
		int transparent = 0;
		// opaque ones fill from the front, transparent ones follow behind them
		for (int i = 0; i < n; i++)
		{
			if (!(draws[i].flags & ENGINE_DRAW_TRANSPARENT))
			{
				order[opaque++] = (u16)i;
			}
		}
		for (int i = 0; i < n; i++)
		{
			if (draws[i].flags & ENGINE_DRAW_TRANSPARENT)
			{
				order[opaque + transparent++] = (u16)i;
			}
		}
		Scene_SortRange(order, opaque, draws, 0);
		Scene_SortRange(order + opaque, transparent, draws, 1);
		for (int i = 0; i < n; i++)
		{
			b->sorted[view][i] = draws[order[i]];
		}
		b->dirty[view] = 0;
	}
	if (out != NULL && n > 0)
	{
		*out = b->sorted[view];
	}
	return n;
}

int Engine_SceneSorted(int view, const EngineDraw **out)
{
	return Engine_SceneSortedBank(g_sceneBank, view, out);
}

u32 Engine_SceneOverflows(void)
{
	return g_sceneOverflows;
}

// --------------------------------------------------------------------------
// SELFTEST (meshes and scene)

#define ST_CHECK(cond, text)                                   \
	do                                                         \
	{                                                          \
		(*checks)++;                                           \
		if (!(cond))                                           \
		{                                                      \
			printf("engine selftest: mesh/scene: %s\n", text); \
			failures++;                                        \
		}                                                      \
	} while (0)

// A valid 2-frame, 4-vertex, 2-triangle mesh in the caller's arrays.
typedef struct
{
	EngineVertex v[8];
	u16 i[6];
	EngineRange r[2];
	EngineMeshDesc d;
} StMesh;

internal void St_FillMesh(StMesh *m)
{
	memset(m, 0, sizeof(*m));
	for (int k = 0; k < 8; k++)
	{
		m->v[k].position[0] = (float)k;
		m->v[k].position[1] = (float)(k * 2);
		m->v[k].color[0] = (u8)(k + 1);
	}
	u16 idx[6] = {0, 1, 2, 2, 1, 3};
	memcpy(m->i, idx, sizeof(idx));
	m->r[0].firstIndex = 0;
	m->r[0].indexCount = 3;
	m->r[0].material = 5;
	m->r[1].firstIndex = 3;
	m->r[1].indexCount = 3;
	m->r[1].material = 6;
	m->d.vertices = m->v;
	m->d.verticesPerFrame = 4;
	m->d.frames = 2;
	m->d.indices = m->i;
	m->d.indexCount = 6;
	m->d.ranges = m->r;
	m->d.rangeCount = 2;
	m->d.name = "test";
}

internal EngineDraw St_Draw(int mesh, int override, u32 frame, u16 flags, float depth, u32 tag)
{
	EngineDraw d;
	memset(&d, 0, sizeof(d));
	d.mesh = mesh;
	d.materialOverride = override;
	d.frame = frame;
	d.flags = flags;
	d.sortDepth = depth;
	d.otCell = tag; // identifies the draw in the order checks
	return d;
}

int Engine_SelfTestMeshScene(int *checks)
{
	int failures = 0;
	StMesh m;
	EngineMeshDesc bad;

	Engine_MeshesReset();
	ST_CHECK(Engine_MeshBytes() == 0, "bytes not 0 after reset");

	// --- the rings: none before the allocation (a dynamic draw is refused), then there
	{
		static const EngineVertex v3[3] = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0, 0, 0, 0}}};
		static const u16 i3[3] = {0, 1, 2};
		EngineDraw d;
		u32 ov;
		int bank;

		Engine_SceneRingsFree();
		memset(&d, 0, sizeof(d));
		d.mesh = -1;
		d.dynVertices = v3;
		d.dynVertexCount = 3;
		d.dynIndices = i3;
		d.dynIndexCount = 3;
		Engine_SceneBeginFrame();
		ov = Engine_SceneOverflows();
		ST_CHECK(Engine_SceneSubmit(0, &d) == -1 && Engine_SceneOverflows() == ov + 1, "dynamic draw without rings not refused/counted");
		ST_CHECK(Engine_SceneRingsAlloc() == 1, "rings not allocated");
		for (bank = 0; bank < ENGINE_SCENE_BANKS; bank++)
		{
			ST_CHECK(g_sceneBanks[bank].dynVertices != NULL && g_sceneBanks[bank].dynIndices != NULL, "a bank without rings");
		}
		ST_CHECK(Engine_SceneRingsAlloc() == 1 && Engine_SceneRingsBytes() > 0u, "second allocation refused");
		Engine_SceneBeginFrame();
	}

	// --- validation
	ST_CHECK(Engine_MeshCreate(NULL) == -1, "NULL desc accepted");
	St_FillMesh(&m);
	bad = m.d;
	bad.verticesPerFrame = 0;
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "verticesPerFrame 0 accepted");
	bad = m.d;
	bad.frames = 0;
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "frames 0 accepted");
	bad = m.d;
	bad.indexCount = 0;
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "indexCount 0 accepted");
	bad = m.d;
	bad.indexCount = 4;
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "indexCount not multiple of 3 accepted");
	bad = m.d;
	bad.rangeCount = 0;
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "rangeCount 0 accepted");
	bad = m.d;
	bad.vertices = NULL;
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "NULL vertices accepted");
	m.i[4] = 4; // == verticesPerFrame
	ST_CHECK(Engine_MeshCreate(&m.d) == -1, "index out of vertex range accepted");
	St_FillMesh(&m);
	m.r[1].indexCount = 4; // 3 + 4 > 6
	ST_CHECK(Engine_MeshCreate(&m.d) == -1, "range beyond indices accepted");
	St_FillMesh(&m);
	m.r[0].firstIndex = 6;
	m.r[0].indexCount = 0;
	ST_CHECK(Engine_MeshCreate(&m.d) == -1, "range start outside indices accepted");
	St_FillMesh(&m);
	m.r[0].firstIndex = 0xfffffffeu;
	m.r[0].indexCount = 4; // wraps in u32
	ST_CHECK(Engine_MeshCreate(&m.d) == -1, "overflowing range accepted");
	St_FillMesh(&m);
	bad = m.d;
	bad.verticesPerFrame = 0x10001u;
	bad.frames = 16; // 1 048 592 > 1<<20
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "vertex budget exceeded accepted");
	bad = m.d;
	bad.verticesPerFrame = 0xffffffffu;
	bad.frames = 2; // u32 overflow
	ST_CHECK(Engine_MeshCreate(&bad) == -1, "u32 vertex overflow accepted");
	ST_CHECK(Engine_MeshBytes() == 0, "refused meshes allocated memory");

	// --- a valid mesh, copies independent of the source
	St_FillMesh(&m);
	int a = Engine_MeshCreate(&m.d);
	ST_CHECK(a == 0, "valid mesh not index 0");
	if (a >= 0)
	{
		const EngineMeshDesc *d = Engine_MeshDesc(a);
		ST_CHECK(d != NULL && d != &m.d, "desc missing or not a copy");
		if (d != NULL)
		{
			u32 expect = 8 * sizeof(EngineVertex) + 6 * sizeof(u16) + 2 * sizeof(EngineRange) + 5;
			ST_CHECK(d->vertices != m.v && d->indices != m.i && d->ranges != m.r, "arrays not copied");
			ST_CHECK(d->frames == 2 && d->verticesPerFrame == 4 && d->indexCount == 6 && d->rangeCount == 2, "desc fields wrong");
			ST_CHECK(Engine_MeshBytes() == expect, "bytes accounting wrong");
			memset(m.v, 0, sizeof(m.v));
			memset(m.i, 0xff, sizeof(m.i));
			memset(m.r, 0, sizeof(m.r));
			m.d.name = NULL;
			ST_CHECK(d->vertices[7].color[0] == 8 && d->vertices[5].position[1] == 10.0f, "vertex copy changed with source");
			ST_CHECK(d->indices[5] == 3 && d->ranges[1].material == 6 && d->ranges[1].firstIndex == 3, "index/range copy changed with source");
			ST_CHECK(strcmp(d->name, "test") == 0, "name copy wrong");
		}
	}
	St_FillMesh(&m);
	m.d.name = NULL;
	int b = Engine_MeshCreate(&m.d);
	ST_CHECK(b == 1, "second mesh not index 1");
	ST_CHECK(Engine_MeshDesc(b) != NULL && Engine_MeshDesc(b)->name[0] == 0, "NULL name not empty string");

	// --- GPU handles
	{
		u32 vb = 1, ib = 1;
		Engine_MeshGetGpu(a, &vb, &ib);
		ST_CHECK(vb == 0xffffffffu && ib == 0xffffffffu, "GPU default not 0xffffffff");
		Engine_MeshSetGpu(a, 11, 22);
		Engine_MeshGetGpu(a, &vb, &ib);
		ST_CHECK(vb == 11 && ib == 22, "GPU set/get wrong");
		Engine_MeshGetGpu(b, &vb, &ib);
		ST_CHECK(vb == 0xffffffffu && ib == 0xffffffffu, "GPU handle leaked to other mesh");
		Engine_MeshGetGpu(-1, &vb, &ib);
		ST_CHECK(vb == 0xffffffffu && ib == 0xffffffffu, "GPU get invalid (-1) not 0xffffffff");
		vb = ib = 0;
		Engine_MeshGetGpu(ENGINE_MESHES_MAX, &vb, &ib);
		ST_CHECK(vb == 0xffffffffu && ib == 0xffffffffu, "GPU get invalid (max) not 0xffffffff");
		Engine_MeshSetGpu(-5, 1, 2); // must not crash
	}

	// --- destroy, slot reuse, invalid index
	Engine_MeshDestroy(a);
	ST_CHECK(Engine_MeshDesc(a) == NULL, "destroyed mesh still has desc");
	Engine_MeshDestroy(a);
	Engine_MeshDestroy(-1);
	Engine_MeshDestroy(ENGINE_MESHES_MAX + 3);
	ST_CHECK(Engine_MeshDesc(-1) == NULL && Engine_MeshDesc(ENGINE_MESHES_MAX) == NULL, "invalid index has desc");
	St_FillMesh(&m);
	ST_CHECK(Engine_MeshCreate(&m.d) == a, "freed slot not reused");
	{
		u32 vb;
		Engine_MeshGetGpu(a, &vb, NULL);
		ST_CHECK(vb == 0xffffffffu, "reused slot kept GPU handle");
	}

	// --- reset and full table
	Engine_MeshesReset();
	ST_CHECK(Engine_MeshBytes() == 0 && Engine_MeshDesc(b) == NULL, "reset left meshes");
	{
		int allOk = 1;
		for (int k = 0; k < ENGINE_MESHES_MAX; k++)
		{
			if (Engine_MeshCreate(&m.d) != k)
			{
				allOk = 0;
			}
		}
		ST_CHECK(allOk, "filling the table failed");
		ST_CHECK(Engine_MeshCreate(&m.d) == -1, "full table accepted a mesh");
	}
	Engine_MeshesReset();
	ST_CHECK(Engine_MeshBytes() == 0, "bytes not 0 after final reset");

	// ----------------------------------------------------------------------
	// scene (start from a clean bank 0 and bank 1)
	Engine_SceneSelectBank(1);
	Engine_SceneBeginFrame();
	Engine_SceneSelectBank(0);
	Engine_SceneBeginFrame();
	ST_CHECK(Engine_SceneBank() == 0, "bank 0 not selected");
	{
		const EngineDraw *out = (const EngineDraw *)1;
		ST_CHECK(Engine_SceneSorted(0, &out) == 0 && out == NULL, "empty view not 0/NULL");
		ST_CHECK(Engine_SceneSorted(-1, &out) == 0 && out == NULL, "view -1 sorted not 0/NULL");
		ST_CHECK(Engine_SceneSorted(ENGINE_VIEWS, &out) == 0 && out == NULL, "view 4 sorted not 0/NULL");
	}
	{
		u32 ovBefore = Engine_SceneOverflows();
		EngineDraw d = St_Draw(0, -1, 0, 0, 0, 0);
		ST_CHECK(Engine_SceneSubmit(-1, &d) == -1 && Engine_SceneSubmit(ENGINE_VIEWS, &d) == -1, "invalid view accepted");
		ST_CHECK(Engine_SceneSubmit(0, NULL) == -1, "NULL draw accepted");
		d.dynVertexCount = 3; // no dynVertices
		ST_CHECK(Engine_SceneSubmit(0, &d) == -1, "dynamic draw without arrays accepted");
		ST_CHECK(Engine_SceneOverflows() == ovBefore, "invalid arguments counted as overflow");
	}

	// order: otCell = submission number
	{
		EngineDraw d[10];
		d[0] = St_Draw(2, -1, 0, 0, 0, 0);                            // opaque mesh 2
		d[1] = St_Draw(1, 7, 1, 0, 0, 1);                             // mesh 1 mat 7 frame 1
		d[2] = St_Draw(1, 7, 0, 0, 0, 2);                             // mesh 1 mat 7 frame 0
		d[3] = St_Draw(1, -1, 5, 0, 0, 3);                            // mesh 1 mat -1 frame 5
		d[4] = St_Draw(-1, -1, 0, 0, 0, 4);                           // dynamic, dynMaterial 9
		d[4].dynMaterial = 9;
		d[5] = St_Draw(1, 7, 0, 0, 0, 5);                             // ties with 2 -> after it
		d[6] = St_Draw(1, 7, 0, ENGINE_DRAW_TRANSPARENT, 10.0f, 6);   // transparent, mid
		d[7] = St_Draw(0, -1, 0, ENGINE_DRAW_TRANSPARENT, 30.0f, 7);  // transparent, far
		d[8] = St_Draw(0, -1, 0, ENGINE_DRAW_TRANSPARENT, 10.0f, 8);  // ties with 6 -> after it
		d[9] = St_Draw(3, -1, 0, ENGINE_DRAW_TRANSPARENT, 1.0f, 9);   // transparent, near
		int idxOk = 1;
		for (int k = 0; k < 10; k++)
		{
			if (Engine_SceneSubmit(1, &d[k]) != k)
			{
				idxOk = 0;
			}
		}
		ST_CHECK(idxOk, "submit did not return sequential indices");
		const EngineDraw *out = NULL;
		int n = Engine_SceneSorted(1, &out);
		ST_CHECK(n == 10 && out != NULL, "sorted count wrong");
		// opaque: mesh -1 (tag 4), mesh 1: mat -1 (3), mat 7 frame 0 (2,5) frame 1 (1), mesh 2 (0)
		// transparent: depth 30 (7), 10 (6,8), 1 (9)
		static const u32 expect[10] = {4, 3, 2, 5, 1, 0, 7, 6, 8, 9};
		int orderOk = (n == 10 && out != NULL);
		for (int k = 0; orderOk && k < 10; k++)
		{
			if (out[k].otCell != expect[k])
			{
				orderOk = 0;
			}
		}
		ST_CHECK(orderOk, "drawing order wrong");
		ST_CHECK(Engine_SceneSorted(0, &out) == 0, "other view not empty");
		const EngineDraw *out2 = NULL;
		ST_CHECK(Engine_SceneSorted(1, &out2) == 10 && out2 != NULL && out2[0].otCell == 4 && out2[9].otCell == 9, "repeated sort differs");
		EngineDraw late = St_Draw(-1, -1, 0, 0, 0, 20);
		ST_CHECK(Engine_SceneSubmit(1, &late) == 10, "late submit index wrong");
		n = Engine_SceneSorted(1, &out);
		ST_CHECK(n == 11 && out != NULL && out[0].otCell == 20 && out[1].otCell == 4, "late submit not resorted (dynMaterial 0 before 9)");
	}

	// dynamic geometry
	{
		Engine_SceneBeginFrame();
		EngineVertex dv[3];
		u16 di[3] = {0, 1, 2};
		memset(dv, 0, sizeof(dv));
		dv[2].position[0] = 42.0f;
		EngineDraw d = St_Draw(-1, -1, 0, 0, 0, 0);
		d.dynVertices = dv;
		d.dynVertexCount = 3;
		d.dynIndices = di;
		d.dynIndexCount = 3;
		d.dynMaterial = 3;
		ST_CHECK(Engine_SceneSubmit(2, &d) == 0, "dynamic submit failed");
		ST_CHECK(Engine_SceneSubmit(3, &d) == 0, "dynamic submit to second view failed");
		dv[2].position[0] = 0.0f;
		di[2] = 0;
		const EngineDraw *out = NULL;
		int n = Engine_SceneSorted(2, &out);
		ST_CHECK(n == 1 && out != NULL, "dynamic sorted count wrong");
		if (n == 1 && out != NULL)
		{
			const SceneBank *b0 = &g_sceneBanks[0];
			ST_CHECK(out[0].dynVertices != dv && out[0].dynIndices != di, "dynamic pointers not redirected");
			ST_CHECK(out[0].dynVertices >= b0->dynVertices && out[0].dynVertices < b0->dynVertices + ENGINE_DYN_VERTICES_PER_FRAME &&
			             out[0].dynIndices >= b0->dynIndices && out[0].dynIndices < b0->dynIndices + SCENE_DYN_INDICES_PER_FRAME,
			         "dynamic pointers not in the ring");
			ST_CHECK(out[0].dynVertices[2].position[0] == 42.0f && out[0].dynIndices[2] == 2, "dynamic copy changed with source");
			const EngineDraw *out3 = NULL;
			Engine_SceneSorted(3, &out3);
			ST_CHECK(out3 != NULL && out3[0].dynVertices == out[0].dynVertices + 3 && out3[0].dynIndices == out[0].dynIndices + 3,
			         "ring allocations overlap or not consecutive");
		}
	}

	// banks: bank 0 keeps its sorted lists and dynamic geometry while bank 1 is collected
	{
		const EngineDraw *before = NULL;
		int nBefore = Engine_SceneSortedBank(0, 2, &before);
		ST_CHECK(nBefore == 1 && before != NULL, "bank 0 list missing before bank switch");
		const EngineVertex *ringBefore = before != NULL ? before[0].dynVertices : NULL;
		Engine_SceneSelectBank(2);
		Engine_SceneSelectBank(-1);
		ST_CHECK(Engine_SceneBank() == 0, "invalid bank selection not ignored");
		ST_CHECK(Engine_SceneSortedBank(2, 2, &before) == 0 && before == NULL, "invalid bank sorted not 0/NULL");
		ST_CHECK(Engine_SceneSortedBank(-1, 2, &before) == 0 && before == NULL, "bank -1 sorted not 0/NULL");
		ST_CHECK(Engine_SceneSortedBank(0, 4, &before) == 0 && before == NULL, "bank sorted invalid view not 0/NULL");

		Engine_SceneSelectBank(1);
		ST_CHECK(Engine_SceneBank() == 1, "bank 1 not selected");
		Engine_SceneBeginFrame();
		const EngineDraw *other = NULL;
		ST_CHECK(Engine_SceneSorted(2, &other) == 0 && other == NULL, "bank 1 not empty after BeginFrame");
		EngineVertex dv[3];
		u16 di[3] = {0, 1, 2};
		memset(dv, 0, sizeof(dv));
		dv[2].position[0] = 7.0f;
		EngineDraw d = St_Draw(-1, -1, 0, 0, 0, 55);
		d.dynVertices = dv;
		d.dynVertexCount = 3;
		d.dynIndices = di;
		d.dynIndexCount = 3;
		ST_CHECK(Engine_SceneSubmit(2, &d) == 0 && Engine_SceneSubmit(2, &d) == 1, "bank 1 submit failed");
		ST_CHECK(Engine_SceneSorted(2, &other) == 2 && other != NULL && other[0].otCell == 55, "bank 1 sorted wrong");
		ST_CHECK(other != NULL && other[0].dynVertices == g_sceneBanks[1].dynVertices, "bank 1 ring not its own");

		const EngineDraw *after = NULL;
		int nAfter = Engine_SceneSortedBank(0, 2, &after);
		ST_CHECK(nAfter == 1 && after != NULL && after[0].otCell == 0 && after[0].dynVertices == ringBefore &&
		             after[0].dynVertices[2].position[0] == 42.0f,
		         "bank 0 changed by bank 1 frame");
		ST_CHECK(Engine_SceneSortedBank(0, 1, &after) == 0, "bank 0 view 1 should be empty after its BeginFrame");
		Engine_SceneBeginFrame(); // empties bank 1 only
		ST_CHECK(Engine_SceneSortedBank(1, 2, &after) == 0 && Engine_SceneSortedBank(0, 2, &after) == 1, "BeginFrame touched the wrong bank");
		Engine_SceneSelectBank(0);
	}

	// ring full (bank 0)
	{
		Engine_SceneBeginFrame();
		// Heap, not static: a ring-sized array would be static memory of every run.
		EngineVertex *big = (EngineVertex *)calloc((size_t)ENGINE_DYN_VERTICES_PER_FRAME, sizeof(EngineVertex));
		u16 *manyI = (u16 *)calloc((size_t)SCENE_DYN_INDICES_PER_FRAME + 3u, sizeof(u16));
		static u16 bigI[3] = {0, 1, 2};
		EngineDraw d = St_Draw(-1, -1, 0, 0, 0, 0);
		ST_CHECK(big != NULL && manyI != NULL, "out of memory for the ring-full test");
		if (big == NULL || manyI == NULL)
		{
			free(big);
			free(manyI);
			return failures;
		}
		d.dynVertices = big;
		d.dynVertexCount = ENGINE_DYN_VERTICES_PER_FRAME - 2;
		d.dynIndices = bigI;
		d.dynIndexCount = 3;
		u32 ov = Engine_SceneOverflows();
		ST_CHECK(Engine_SceneSubmit(0, &d) == 0, "nearly ring-sized draw refused");
		d.dynVertexCount = 3;
		ST_CHECK(Engine_SceneSubmit(0, &d) == -1, "vertex ring overflow accepted");
		ST_CHECK(Engine_SceneOverflows() == ov + 1, "vertex ring overflow not counted");
		d.dynVertexCount = 2;
		ST_CHECK(Engine_SceneSubmit(0, &d) == 1, "draw filling the ring exactly refused");
		{
			Engine_SceneBeginFrame();
			d.dynVertexCount = 3;
			d.dynIndices = manyI;
			d.dynIndexCount = SCENE_DYN_INDICES_PER_FRAME + 3;
			ov = Engine_SceneOverflows();
			ST_CHECK(Engine_SceneSubmit(0, &d) == -1 && Engine_SceneOverflows() == ov + 1, "index ring overflow not refused/counted");
		}
		free(big);
		free(manyI);
	}

	// list full, BeginFrame
	{
		Engine_SceneBeginFrame();
		EngineDraw d = St_Draw(0, -1, 0, 0, 0, 0);
		int allOk = 1;
		for (int k = 0; k < ENGINE_DRAWS_PER_VIEW; k++)
		{
			if (Engine_SceneSubmit(0, &d) != k)
			{
				allOk = 0;
			}
		}
		ST_CHECK(allOk, "filling the draw list failed");
		u32 ov = Engine_SceneOverflows();
		ST_CHECK(Engine_SceneSubmit(0, &d) == -1, "full list accepted a draw");
		ST_CHECK(Engine_SceneOverflows() == ov + 1, "full list overflow not counted");
		ST_CHECK(Engine_SceneSubmit(1, &d) == 0, "full view blocked another view");
		const EngineDraw *out = NULL;
		ST_CHECK(Engine_SceneSorted(0, &out) == ENGINE_DRAWS_PER_VIEW, "full list sorted count wrong");
		Engine_SceneBeginFrame();
		ST_CHECK(Engine_SceneSorted(0, &out) == 0 && out == NULL && Engine_SceneSorted(1, &out) == 0, "BeginFrame did not empty lists");
		ST_CHECK(Engine_SceneSubmit(0, &d) == 0, "submit after BeginFrame not index 0");
		ST_CHECK(Engine_SceneOverflows() == ov + 1, "BeginFrame reset the overflow counter");
	}
	Engine_SceneBeginFrame();
	return failures;
}
