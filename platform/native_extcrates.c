// THE EXTERNAL TRACK'S CRATES (--ext-track, see platform/engine_exttrack.c):
// the item crates at the positions of the data. SKELETON (round of
// 2026-10-19): every function is an empty stub that says so in the log once;
// without --ext-track none of them is reached. Declared in
// include/platform/engine.h (EXTERNAL TRACK, the parts' entry points).

global_variable int s_extCratesStubLogged;

internal void ExtCrates_StubLog(void)
{
	if (!s_extCratesStubLogged)
	{
		s_extCratesStubLogged = 1;
		Platform_Log("[CTR ExtTrack] crates: not implemented\n");
	}
}

// From the dispatcher at the start of a race (fresh load and restart, game/MAIN/MainMain.c).
void ExtCrates_OnRaceStart(void)
{
	ExtCrates_StubLog();
}

// From the dispatcher once per logic frame.
void ExtCrates_OnLogicFrame(void)
{
	ExtCrates_StubLog();
}

// From the dispatcher when the race lets go of its state (restart, quit; the
// memory of the race's instances is given back right after).
void ExtCrates_OnRestart(void)
{
	ExtCrates_StubLog();
}

// --ext-selftest crates: prints "ext selftest crates passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtCrates_Selftest(void)
{
	printf("ext selftest crates: not implemented\n");
	printf("ext selftest crates passed: 0 checks, 0 failures\n");
	return 0;
}
