#include <platform/engine.h>
#include <stdlib.h>
#include <string.h>

// ENGINE MESH - see include/platform/engine.h.
//
// The CPU registry of engine meshes. Every mesh owns ONE malloc'd block that
// holds copies of the vertices, indices, ranges and the name, so the caller's
// arrays can change or vanish right after Engine_MeshCreate. The stored
// EngineMeshDesc points into that block. Slots are never reordered: a mesh
// index stays valid until the mesh is destroyed.

#define MESH_UNUPLOADED 0xffffffffu
#define MESH_VERTICES_TOTAL_MAX (1u << 20)

typedef struct
{
	int used;
	EngineMeshDesc desc; // pointers into block
	void *block;
	u32 bytes;
	u32 gpuVertex;
	u32 gpuIndex;
} MeshSlot;

global_variable MeshSlot g_meshSlots[ENGINE_MESHES_MAX];

internal int Mesh_ValidIndex(int mesh)
{
	return mesh >= 0 && mesh < ENGINE_MESHES_MAX && g_meshSlots[mesh].used;
}

// Checks every rule of Engine_MeshCreate. Returns 1 when the description is usable.
internal int Mesh_Validate(const EngineMeshDesc *d)
{
	if (d == NULL || d->vertices == NULL || d->indices == NULL || d->ranges == NULL)
	{
		return 0;
	}
	if (d->verticesPerFrame < 1 || d->frames < 1 || d->rangeCount < 1)
	{
		return 0;
	}
	if (d->indexCount < 3 || d->indexCount % 3 != 0)
	{
		return 0;
	}
	// total vertex count must fit in u32 and in the 1<<20 budget
	if ((u64)d->frames * (u64)d->verticesPerFrame > (u64)MESH_VERTICES_TOTAL_MAX)
	{
		return 0;
	}
	for (u32 i = 0; i < d->indexCount; i++)
	{
		if (d->indices[i] >= d->verticesPerFrame)
		{
			return 0;
		}
	}
	for (u32 r = 0; r < d->rangeCount; r++)
	{
		u64 end = (u64)d->ranges[r].firstIndex + (u64)d->ranges[r].indexCount;
		if (d->ranges[r].firstIndex >= d->indexCount || end > (u64)d->indexCount)
		{
			return 0;
		}
	}
	return 1;
}

int Engine_MeshCreate(const EngineMeshDesc *desc)
{
	if (!Mesh_Validate(desc))
	{
		return -1;
	}
	int slot = -1;
	for (int i = 0; i < ENGINE_MESHES_MAX; i++)
	{
		if (!g_meshSlots[i].used)
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
	{
		return -1;
	}

	const char *name = desc->name != NULL ? desc->name : "";
	size_t nameBytes = strlen(name) + 1;
	size_t vertexBytes = (size_t)desc->frames * desc->verticesPerFrame * sizeof(EngineVertex);
	size_t indexBytes = (size_t)desc->indexCount * sizeof(u16);
	size_t rangeBytes = (size_t)desc->rangeCount * sizeof(EngineRange);
	// layout: vertices (float aligned), ranges (u32 aligned), indices, name
	size_t total = vertexBytes + rangeBytes + indexBytes + nameBytes;
	if (total > 0xffffffffu)
	{
		return -1;
	}
	u8 *block = (u8 *)malloc(total);
	if (block == NULL)
	{
		return -1;
	}
	EngineVertex *vertices = (EngineVertex *)block;
	EngineRange *ranges = (EngineRange *)(block + vertexBytes);
	u16 *indices = (u16 *)(block + vertexBytes + rangeBytes);
	char *nameCopy = (char *)(block + vertexBytes + rangeBytes + indexBytes);
	memcpy(vertices, desc->vertices, vertexBytes);
	memcpy(ranges, desc->ranges, rangeBytes);
	memcpy(indices, desc->indices, indexBytes);
	memcpy(nameCopy, name, nameBytes);

	MeshSlot *s = &g_meshSlots[slot];
	s->used = 1;
	s->block = block;
	s->bytes = (u32)total;
	s->gpuVertex = MESH_UNUPLOADED;
	s->gpuIndex = MESH_UNUPLOADED;
	s->desc.vertices = vertices;
	s->desc.verticesPerFrame = desc->verticesPerFrame;
	s->desc.frames = desc->frames;
	s->desc.indices = indices;
	s->desc.indexCount = desc->indexCount;
	s->desc.ranges = ranges;
	s->desc.rangeCount = desc->rangeCount;
	s->desc.name = nameCopy;
	return slot;
}

void Engine_MeshDestroy(int mesh)
{
	if (!Mesh_ValidIndex(mesh))
	{
		return;
	}
	free(g_meshSlots[mesh].block);
	memset(&g_meshSlots[mesh], 0, sizeof(g_meshSlots[mesh]));
}

void Engine_MeshesReset(void)
{
	for (int i = 0; i < ENGINE_MESHES_MAX; i++)
	{
		Engine_MeshDestroy(i);
	}
}

const EngineMeshDesc *Engine_MeshDesc(int mesh)
{
	return Mesh_ValidIndex(mesh) ? &g_meshSlots[mesh].desc : NULL;
}

u32 Engine_MeshBytes(void)
{
	u32 sum = 0;
	for (int i = 0; i < ENGINE_MESHES_MAX; i++)
	{
		if (g_meshSlots[i].used)
		{
			sum += g_meshSlots[i].bytes;
		}
	}
	return sum;
}

void Engine_MeshSetGpu(int mesh, u32 vertexBuffer, u32 indexBuffer)
{
	if (!Mesh_ValidIndex(mesh))
	{
		return;
	}
	g_meshSlots[mesh].gpuVertex = vertexBuffer;
	g_meshSlots[mesh].gpuIndex = indexBuffer;
}

void Engine_MeshGetGpu(int mesh, u32 *vertexBuffer, u32 *indexBuffer)
{
	u32 v = MESH_UNUPLOADED;
	u32 i = MESH_UNUPLOADED;
	if (Mesh_ValidIndex(mesh))
	{
		v = g_meshSlots[mesh].gpuVertex;
		i = g_meshSlots[mesh].gpuIndex;
	}
	if (vertexBuffer != NULL)
	{
		*vertexBuffer = v;
	}
	if (indexBuffer != NULL)
	{
		*indexBuffer = i;
	}
}
