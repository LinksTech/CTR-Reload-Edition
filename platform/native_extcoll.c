#include <platform/native_extcoll.h>

// THE EXTERNAL TRACK'S COLLISION - see include/platform/native_extcoll.h.
// SKELETON (round of 2026-10-19): stubs only.

global_variable int s_extCollStubLogged;

void ExtColl_OnLevelLoaded(struct Level *lev)
{
	(void)lev;
	if (!s_extCollStubLogged)
	{
		s_extCollStubLogged = 1;
		Platform_Log("[CTR ExtTrack] collision: not implemented\n");
	}
}

struct mesh_info *COLL_WorldMesh(struct Level *level)
{
	return (level != NULL) ? level->ptr_mesh_info : NULL;
}

int ExtColl_Selftest(void)
{
	printf("ext selftest coll: not implemented\n");
	printf("ext selftest coll passed: 0 checks, 0 failures\n");
	return 0;
}
