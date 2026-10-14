#include <platform/engine.h>

// ENGINE SKY - see include/platform/engine.h, section SKY.
//
// Scaffold of the round of 2026-10-15: every function answers "nothing" (no
// mesh, no draw, the PS1 sky and stars stay PS1).

void Engine_SkyLoad(void)
{
}

void Engine_SkyCollect(struct GameTracker *gGT)
{
	(void)gGT;
}

int Engine_SkyOwnsView(const struct PushBuffer *pb)
{
	(void)pb;
	return 0;
}

int Engine_SkyOwnsStars(const struct PushBuffer *pb)
{
	(void)pb;
	return 0;
}

int Engine_SelfTestSky(int *checks)
{
	(void)checks;
	return 0;
}
