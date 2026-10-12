#include <platform/engine.h>

// ENGINE MESH - see include/platform/engine.h. Skeleton: every function is a stub.

int Engine_MeshCreate(const EngineMeshDesc *desc)
{
	(void)desc;
	return 0;
}

void Engine_MeshDestroy(int mesh)
{
	(void)mesh;
}

void Engine_MeshesReset(void)
{
}

const EngineMeshDesc *Engine_MeshDesc(int mesh)
{
	(void)mesh;
	return NULL;
}

u32 Engine_MeshBytes(void)
{
	return 0;
}

void Engine_MeshSetGpu(int mesh, u32 vertexBuffer, u32 indexBuffer)
{
	(void)mesh;
	(void)vertexBuffer;
	(void)indexBuffer;
}

void Engine_MeshGetGpu(int mesh, u32 *vertexBuffer, u32 *indexBuffer)
{
	(void)mesh;
	(void)vertexBuffer;
	(void)indexBuffer;
}
