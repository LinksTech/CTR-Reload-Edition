#include <platform/engine.h>

// ENGINE FX COMMON - which drivers' effects the engine owns, and the effect
// counters (step 4). See include/platform/engine.h.

global_variable u32 s_engFxDrawn[ENGINE_FX_KINDS];
global_variable u32 s_engFxRetail[ENGINE_FX_KINDS];
global_variable int s_engFxReportOn = 0;

// The set of a seat, read from the driver-set table of platform/native_char_gpu.c
// (s_ncgSeatSet, earlier in the translation unit). Only read.
int Engine_DriverSetOfSeat(int seat)
{
	if ((seat < 0) || (seat >= NATIVE_CHAR_GPU_SETS))
	{
		return -1;
	}
	return s_ncgSeatSet[seat];
}

// The seat of a driver is its driverID (0..7); the engine owns it when the
// seat is bound to a custom driver whose set the engine draws.
int Engine_FxOwnsDriver(const struct Driver *d)
{
	int set;

	if (!g_cfg_engine || (d == NULL))
	{
		return 0;
	}
	set = Engine_DriverSetOfSeat((int)d->driverID);
	return (set >= 0) && (Engine_DriverBodyMesh(set) >= 0);
}

void Engine_FxNoteDrawn(EngineFxKind kind)
{
	if ((unsigned)kind < ENGINE_FX_KINDS)
	{
		s_engFxDrawn[kind]++;
	}
	if (!s_engFxReportOn)
	{
		s_engFxReportOn = 1;
		Platform_AtExitReport(Engine_FxReport);
	}
}

void Engine_FxNoteRetail(EngineFxKind kind)
{
	if ((unsigned)kind < ENGINE_FX_KINDS)
	{
		s_engFxRetail[kind]++;
	}
	if (!s_engFxReportOn)
	{
		s_engFxReportOn = 1;
		Platform_AtExitReport(Engine_FxReport);
	}
}

void Engine_FxReport(void)
{
	Platform_Log("[CTR Engine] effects drawn by the engine: shadow %u, exhaust %u, turbo %u, skids %u; retail effect primitives of owned "
	             "drivers: shadow %u, exhaust %u, turbo %u, skids %u\n",
	             s_engFxDrawn[0], s_engFxDrawn[1], s_engFxDrawn[2], s_engFxDrawn[3], s_engFxRetail[0], s_engFxRetail[1], s_engFxRetail[2],
	             s_engFxRetail[3]);
}
