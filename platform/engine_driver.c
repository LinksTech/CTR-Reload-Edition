#include <platform/engine.h>

// ENGINE DRIVER - custom drivers through the engine (step 3). See
// include/platform/engine.h. Skeleton: every function is a stub.

void Engine_DriverLoad(void)
{
}

void Engine_DriverUnload(void)
{
}

int Engine_DriverBodyMesh(int set)
{
	(void)set;
	return -1;
}

int Engine_DriverSubmitItem(const struct NrDrawItem *it, u32 *ot, int cell)
{
	(void)it;
	(void)ot;
	(void)cell;
	return 0;
}
