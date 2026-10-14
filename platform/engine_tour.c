#include <platform/engine.h>

// ENGINE TOUR - see include/platform/engine.h, section TRACK TOUR.
//
// Scaffold of the round of 2026-10-15: nothing can register, nothing is called.

int Engine_TourRegister(EngineTourReportFn fn)
{
	(void)fn;
	return 0;
}

void Engine_TourNotify(int entry, int levelID, int phase)
{
	(void)entry;
	(void)levelID;
	(void)phase;
}

int Engine_SelfTestTour(int *checks)
{
	(void)checks;
	return 0;
}
