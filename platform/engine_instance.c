#include <platform/engine.h>

// ENGINE INSTANCE - an instance of the game drawn by the engine in world space
// (round of 2026-10-13). See include/platform/engine.h, INSTANCES IN WORLD SPACE.
// Scaffold: package B1a fills it.

int Engine_InstanceWorldMatrix(const struct Instance *inst, EngineMat4 *worldFromModel)
{
	(void)inst;
	(void)worldFromModel;
	return 0;
}

int Engine_InstanceSubmit(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb, const EngineInstanceDraw *d)
{
	(void)inst;
	(void)idpp;
	(void)pb;
	(void)d;
	return 0;
}

int Engine_InstanceViewOf(const struct PushBuffer *pb)
{
	const struct GameTracker *gGT = sdata->gGT;
	int v;

	if ((gGT == NULL) || (pb == NULL))
	{
		return -1;
	}
	for (v = 0; v < ENGINE_VIEWS; v++)
	{
		if (pb == &gGT->pushBuffer[v])
		{
			return v;
		}
	}
	return -1;
}

// Scaffold: no checks yet.
int Engine_SelfTestInstance(int *checks)
{
	(void)checks;
	return 0;
}
