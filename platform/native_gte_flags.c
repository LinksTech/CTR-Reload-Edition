// THE COUNTING OF THE SX/SY SATURATION.
//
// The question it is built for: `Lm_G1`/`Lm_G2` in
// platform/native_gte_core.c clamp the projected screen coordinate hard
// to +-1024. At 4:3 probably everything lies below that; at 43:18 the
// field of view is widened horizontally, and edge faces could run into the limit.
// A clamped corner next to otherwise correct neighbours would give exactly the
// picture that was reported - torn textures at the screen edge, only while driving.
//
// WHERE IT SITS AND WHY THERE. In doCOP2, right after the operator
// has returned. The FLAG register is still intact there - GTE_operator sets it
// to zero at the start and ORs into it during the computation - so the
// question "did this operation clamp" can be answered after the call without changing
// even one character in the core. Both GTE paths go through the same
// place; the counter applies to whichever one is computing.
//
// WHAT IS COUNTED, EXACTLY. Bit 14 is the X clamp (Lm_G1), bit 13 the
// Y clamp (Lm_G2). What is counted is OPERATIONS in which at least one
// clamp occurred - not vertices. For RTPS that is the same, for RTPT not:
// an RTPT projects three points and sets a single flag bit, behind which
// one, two or three clamped corners can stand. That is why the number
// of RTPT operations stands separately next to it - whoever needs the upper bound computes
// it, instead of getting it estimated here.
//
// THE SECOND NUMBER: HOW FAR OUT. The largest magnitude SX and SY
// actually took. It is capped at 1024 by the clamp itself
// - and that is the point: if it reads 1024, the clamp was active;
// if it reads 700, there is room. For RTPS the new point is SXY2; for RTPT all
// three slots of the stack are new, so all three are looked at.
//
// SWITCH: --gte-report [every N frames], default off. If it is off, the
// counting costs one branch on a global zero per operation and nothing
// else.
//
// The report additionally hangs on atexit. Without that it would be lost: Ctrl+Q,
// the window close button and SDL_EVENT_QUIT all go via exit(0), and exactly
// that killed a whole evening of measurements on 2026-08-28.
//
// ONE LIMITATION, MEASURED AND NOT ASSUMED: on the --dump-exit path
// the sum is only on the console, not in the log file. This path calls
// Platform_Shutdown explicitly and after that exit(0)
// (platform/native_platform.c), so the log is already closed when atexit gets its
// turn - in the run of 2026-09-02 "---- LOG CLOSED ----" stood two lines before
// the sum. Nothing is lost, Platform_Log always writes to
// stdout as well; whoever needs the sum from a dump run captures the console. The
// window lines are in both.

#include <stdio.h>
#include <stdlib.h>

#include <macros.h>
#include <psx/gtereg.h>
#include <platform.h>

#define NATIVE_GTE_FLAG_SX (1u << 14)
#define NATIVE_GTE_FLAG_SY (1u << 13)

// From how many frames one report line is made. 0 means: only the
// sum at the end.
int g_cfg_gteReportEvery = 0;

// Whether counting happens at all. Separate from the window above, so that --gte-report 0
// (sum only) is not read as "off".
int g_cfg_gteReport = 0;

struct NativeGteFlagCensus
{
	long long ops;
	long long projections;
	long long rtptOps;
	long long sxSaturated;
	long long sySaturated;
	int maxAbsX;
	int maxAbsY;
};

global_variable struct NativeGteFlagCensus s_gteWindow;
global_variable struct NativeGteFlagCensus s_gteTotal;
global_variable int s_gteWindowFrames;
global_variable int s_gteTotalFrames;
global_variable int s_gteReportArmed;

void NativeGteFlags_Report(void);

internal int NativeGteFlags_Abs(int value)
{
	return (value < 0) ? -value : value;
}

internal void NativeGteFlags_NotePoint(struct NativeGteFlagCensus *census, int x, int y)
{
	const int absX = NativeGteFlags_Abs(x);
	const int absY = NativeGteFlags_Abs(y);

	if (absX > census->maxAbsX)
	{
		census->maxAbsX = absX;
	}
	if (absY > census->maxAbsY)
	{
		census->maxAbsY = absY;
	}
}

// An operation is done. flag is C2_FLAG, funct the function number.
void NativeGteFlags_Note(u32 flag, int funct)
{
	const int isRtps = (funct == 0x00) || (funct == 0x01);
	const int isRtpt = (funct == 0x30);

	s_gteWindow.ops++;
	s_gteTotal.ops++;

	if (!isRtps && !isRtpt)
	{
		// Only RTPS and RTPT write SX/SY. Every other operation cannot set the
		// two bits at all, and counting them would dilute the rate
		// below with denominators that can never become numerators.
		return;
	}

	s_gteWindow.projections++;
	s_gteTotal.projections++;

	if (isRtpt)
	{
		s_gteWindow.rtptOps++;
		s_gteTotal.rtptOps++;

		NativeGteFlags_NotePoint(&s_gteWindow, C2_SX0, C2_SY0);
		NativeGteFlags_NotePoint(&s_gteTotal, C2_SX0, C2_SY0);
		NativeGteFlags_NotePoint(&s_gteWindow, C2_SX1, C2_SY1);
		NativeGteFlags_NotePoint(&s_gteTotal, C2_SX1, C2_SY1);
	}

	NativeGteFlags_NotePoint(&s_gteWindow, C2_SX2, C2_SY2);
	NativeGteFlags_NotePoint(&s_gteTotal, C2_SX2, C2_SY2);

	if ((flag & NATIVE_GTE_FLAG_SX) != 0)
	{
		s_gteWindow.sxSaturated++;
		s_gteTotal.sxSaturated++;
	}

	if ((flag & NATIVE_GTE_FLAG_SY) != 0)
	{
		s_gteWindow.sySaturated++;
		s_gteTotal.sySaturated++;
	}
}

internal void NativeGteFlags_Line(const char *what, const struct NativeGteFlagCensus *census, int frames)
{
	int aspectW = 4;
	int aspectH = 3;

	CTR_View_GetWorldAspect(&aspectW, &aspectH);

	Platform_Log("[CTR GTE] %s %d frames, aspect %d:%d - %lld projections (%lld RTPT), "
	             "SX clamped %lld, SY clamped %lld, largest |SX| %d |SY| %d\n",
	             what, frames, aspectW, aspectH, census->projections, census->rtptOps, census->sxSaturated, census->sySaturated, census->maxAbsX,
	             census->maxAbsY);
}

internal void NativeGteFlags_Clear(struct NativeGteFlagCensus *census)
{
	census->ops = 0;
	census->projections = 0;
	census->rtptOps = 0;
	census->sxSaturated = 0;
	census->sySaturated = 0;
	census->maxAbsX = 0;
	census->maxAbsY = 0;
}

// Once per frame, from Platform_EndFrame.
void NativeGteFlags_Frame(void)
{
	if (!g_cfg_gteReport)
	{
		return;
	}

	// Registered via Platform_AtExitReport, which runs in Platform_Shutdown before
	// the log is closed. Before, this hung on atexit and had to be HERE
	// instead of where the switch is read, because atexit calls in reverse
	// order and Platform_Shutdown would otherwise have had its turn earlier - which is why the
	// first measurement was only on the console. There is now one door,
	// and the order does not matter to it; registering once stays correct.
	if (!s_gteReportArmed)
	{
		s_gteReportArmed = 1;
		Platform_AtExitReport(NativeGteFlags_Report);
	}

	s_gteWindowFrames++;
	s_gteTotalFrames++;

	if ((g_cfg_gteReportEvery <= 0) || (s_gteWindowFrames < g_cfg_gteReportEvery))
	{
		return;
	}

	NativeGteFlags_Line("over", &s_gteWindow, s_gteWindowFrames);
	NativeGteFlags_Clear(&s_gteWindow);
	s_gteWindowFrames = 0;
}

void NativeGteFlags_Report(void)
{
	if (!g_cfg_gteReport)
	{
		return;
	}

	// The open window belongs to it. A run that ends between two
	// report lines would otherwise have concealed its last frames -
	// and for a run that ends exactly when it gets interesting, that would
	// be the wrong part to conceal.
	if (s_gteWindowFrames > 0)
	{
		NativeGteFlags_Line("closing", &s_gteWindow, s_gteWindowFrames);
	}

	NativeGteFlags_Line("TOTAL", &s_gteTotal, s_gteTotalFrames);
	Platform_Log("[CTR GTE] a clamp count is per OPERATION, not per vertex - one RTPT can hide up to three\n");
	Platform_LogFlush();
}

// From main, when the switch was read.
void NativeGteFlags_Arm(int everyFrames)
{
	g_cfg_gteReport = 1;
	g_cfg_gteReportEvery = everyFrames;

	Platform_Log("[CTR GTE] flag census on, reporting every %d frames\n", everyFrames);
}
