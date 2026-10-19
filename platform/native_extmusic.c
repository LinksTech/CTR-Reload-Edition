// THE EXTERNAL TRACK'S MUSIC (--ext-track, see platform/engine_exttrack.c).
// SKELETON (round of 2026-10-19): every function is an empty stub that says so
// in the log once; without --ext-track none of them is reached. Declared in
// include/platform/engine.h (EXTERNAL TRACK, the parts' entry points).

global_variable int s_extMusicStubLogged;

internal void ExtMusic_StubLog(void)
{
	if (!s_extMusicStubLogged)
	{
		s_extMusicStubLogged = 1;
		Platform_Log("[CTR ExtTrack] music: not implemented\n");
	}
}

// From the dispatcher, once per level load of the active external track.
void ExtMusic_OnLevelLoaded(struct Level *lev)
{
	(void)lev;
	ExtMusic_StubLog();
}

// From the dispatcher once per logic frame.
void ExtMusic_OnLogicFrame(void)
{
	ExtMusic_StubLog();
}

// --ext-selftest music: prints "ext selftest music passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtMusic_Selftest(void)
{
	printf("ext selftest music: not implemented\n");
	printf("ext selftest music passed: 0 checks, 0 failures\n");
	return 0;
}
