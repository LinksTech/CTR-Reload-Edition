#include <platform/engine.h>

// ENGINE DRIVER CMDL - custom drivers whose file has only a CMDL, drawn by the
// engine (round of 2026-10-13). See include/platform/engine.h, CUSTOM DRIVERS WITH
// ONLY A CMDL. Scaffold: package B1c fills it.

void Engine_DriverCmdlLoad(void)
{
}

int Engine_DriverCmdlOwns(const struct Driver *d)
{
	(void)d;
	return 0;
}

int Engine_DriverCmdlRoute(struct Instance *inst, struct InstDrawPerPlayer *idpp, struct PushBuffer *pb)
{
	(void)inst;
	(void)idpp;
	(void)pb;
	return 0;
}

void Engine_DriverNoteRetailBody(const struct Instance *inst)
{
	(void)inst;
}
