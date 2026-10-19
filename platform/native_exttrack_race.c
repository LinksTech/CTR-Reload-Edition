#include <platform/native_exttrack_race.h>

// THE EXTERNAL TRACK'S RACE LOGIC - see include/platform/native_exttrack_race.h.
// SKELETON (round of 2026-10-19): stubs only.

global_variable int s_extRaceStubLogged;

internal void ExtRace_StubLog(void)
{
	if (!s_extRaceStubLogged)
	{
		s_extRaceStubLogged = 1;
		Platform_Log("[CTR ExtTrack] race: not implemented\n");
	}
}

void ExtRace_OnLevelLoaded(struct Level *lev)
{
	(void)lev;
	ExtRace_StubLog();
}

void ExtRace_OnLogicFrame(void)
{
	ExtRace_StubLog();
}

int ExtRace_Selftest(void)
{
	printf("ext selftest race: not implemented\n");
	printf("ext selftest race passed: 0 checks, 0 failures\n");
	return 0;
}
