#include <platform/engine.h>

// ENGINE SCENE - see include/platform/engine.h. Skeleton: every function is a stub.

void Engine_SceneBeginFrame(void)
{
}

int Engine_SceneSubmit(int view, const EngineDraw *draw)
{
	(void)view;
	(void)draw;
	return 0;
}

int Engine_SceneSorted(int view, const EngineDraw **out)
{
	(void)view;
	(void)out;
	return 0;
}

u32 Engine_SceneOverflows(void)
{
	return 0;
}

int Engine_SelfTestMeshScene(int *checks)
{
	(void)checks;
	return 0;
}
