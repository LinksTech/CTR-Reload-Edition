#include <platform/engine.h>

// ENGINE TRACK - the track prototype (--engine-track, round of 2026-10-13). See
// include/platform/engine.h, TRACK PROTOTYPE. Scaffold: package B4 fills it.

void Engine_TrackLoad(void)
{
}

void Engine_TrackCollect(struct GameTracker *gGT)
{
	(void)gGT;
}

int Engine_TrackOwnsBucket(int bucketIndex)
{
	(void)bucketIndex;
	return 0;
}

int Engine_TrackInstanceRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	(void)inst;
	(void)idpp;
	(void)pb;
	return 0;
}

// Scaffold: no checks yet.
int Engine_SelfTestTrack(int *checks)
{
	(void)checks;
	return 0;
}
