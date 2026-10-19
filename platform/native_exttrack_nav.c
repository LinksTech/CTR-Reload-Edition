#include <platform/native_exttrack_nav.h>

// THE EXTERNAL TRACK'S CPU PATHS - see include/platform/native_exttrack_nav.h.
// SKELETON (round of 2026-10-19): stubs only.

global_variable int s_extNavStubLogged;

void ExtNav_OnLevelLoaded(struct Level *lev)
{
	(void)lev;
	if (!s_extNavStubLogged)
	{
		s_extNavStubLogged = 1;
		Platform_Log("[CTR ExtTrack] nav: not implemented\n");
	}
}

int ExtNav_Selftest(void)
{
	printf("ext selftest nav: not implemented\n");
	printf("ext selftest nav passed: 0 checks, 0 failures\n");
	return 0;
}
