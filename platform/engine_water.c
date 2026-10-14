#include <platform/engine.h>

// ENGINE WATER - see include/platform/engine.h, section WATER.
//
// Scaffold of the round of 2026-10-15: every function answers "nothing" (no
// mesh, no draw, the PS1 water buckets stay PS1).

void Engine_WaterLoad(void)
{
}

void Engine_WaterCollect(struct GameTracker *gGT)
{
	(void)gGT;
}

int Engine_WaterOwnsBucket(int bucketIndex)
{
	(void)bucketIndex;
	return 0;
}

int Engine_WaterProbe(const struct GameTracker *gGT, int view, EngineWaterProbe *out)
{
	(void)gGT;
	(void)view;
	if (out != NULL)
	{
		memset(out, 0, sizeof(*out));
	}
	return 0;
}

void Engine_WaterProbeLog(const struct GameTracker *gGT, int view, int vblank, const char *tag)
{
	(void)gGT;
	(void)view;
	(void)vblank;
	(void)tag;
}

int Engine_SelfTestWater(int *checks)
{
	(void)checks;
	return 0;
}
