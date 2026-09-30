#include <common.h>

#if defined(CTR_NATIVE)
#include <stdlib.h>
#endif

// ===========================================================================
// THE MEMORY MEASUREMENT. MEASURING ONLY.
//
// This file builds nothing and repairs nothing. Per frame it reads how full
// the eight JitPools and the particle counter are, and on request it writes
// a snapshot to the log - before and after every restart, before
// and after every track switch. At the end the peaks stand against the
// capacities.
//
// WHY HERE AND NOT IN native_trackmod.c. The question is not "what does
// the track code cost" but "what does the whole thing cost, and how much of it is the
// track". An answer to that must also run on a disc track, where there is
// no track code. Hence a module of its own with an entry point
// in the frame loop that always runs.
//
// HOW FULL A POOL IS STANDS IN THE FREE LIST, NOT IN THE TAKEN ONE.
// Measured on 2026-09-20: only INSTANCE_Birth puts its instance on
// pool.taken (INSTANCE.c:76). The LEV instances (INSTANCE.c:258) and the
// particles (Particle.c:1483) take their item from the free list with
// LIST_RemoveFront and never hook it in anywhere - their taken.count stays
// zero although the pool is full. Hence this measurement counts
// maxItems - free.count. The taken.count is also in the
// snapshot, because the difference is a statement itself: it is the
// number of items that no owner finds through the list any more.
//
// THE EIGHT POOLS LIE ONE AFTER ANOTHER. gGT->JitPools is an unnamed
// aggregate of eight struct JitPool of 0x28 bytes each (namespace_Main.h:413).
// Hence a pointer to the first one walks through all eight - that is no
// assumption about the layout but the firm guarantee of the C standard for
// aggregates of the same type, and the static assertion below pins it down.
//
// THE TRIGGER FOR RESTART AND SWITCH. Neither is reachable from
// outside: the restart hangs on the pause menu, the switch as well. So that
// both can be measured, this module does on request exactly what
// MainFreeze.c does at these two places - lines 947..961 for "RESTART",
// lines 1015..1103 for the load request. No shortcut, no path of its own:
// the same two fields, the same call.
//
// Controlled through the environment, not through a program option: main.c was
// carrying another, unfinished change at the time, and a measurement is no reason
// to write into it.
//
//   CTR_BUDGET_MARK_EVERY=N     a snapshot every N frames
//   CTR_BUDGET_RETRY_AT=a,b,c   trigger a restart at these frames
//   CTR_BUDGET_LEVEL_AT=f:id    load track id at frame f
// ===========================================================================

void Platform_Log(const char *format, ...);
void Platform_AtExitReport(void (*report)(void));

// From game/native_trackmod.c, which comes before this file in game_unity.h.
void NativeTrackMod_BudgetLine(const char *what);

CTR_STATIC_ASSERT(sizeof(struct JitPool) == 0x28);
CTR_STATIC_ASSERT(offsetof(struct GameTracker, JitPools.instance) - offsetof(struct GameTracker, JitPools.thread) == sizeof(struct JitPool));
CTR_STATIC_ASSERT(offsetof(struct GameTracker, JitPools.rain) - offsetof(struct GameTracker, JitPools.thread) == 7 * sizeof(struct JitPool));

enum
{
	BUDGET_POOL_COUNT = 8,
	BUDGET_TRIGGER_MAX = 8,
	BUDGET_SCOPE_COUNT = 4,
};

global_variable const char *const s_budgetPoolName[BUDGET_POOL_COUNT] = {
    "thread", "instance", "smallStack", "mediumStack", "largeStack", "particle", "oscillator", "rain",
};

global_variable int s_budgetPoolPeak[BUDGET_POOL_COUNT];
global_variable int s_budgetPoolMax[BUDGET_POOL_COUNT];
global_variable u32 s_budgetPoolItem[BUDGET_POOL_COUNT];
global_variable int s_budgetPoolBytes[BUDGET_POOL_COUNT];
global_variable int s_budgetParticlePeak;
global_variable int s_budgetMempackFreeMin = -1;
global_variable int s_budgetMempackFreeFirst = -1;
global_variable s64 s_budgetSamples;
global_variable s64 s_budgetMarks;
global_variable int s_budgetReportArmed;
global_variable int s_budgetConfigRead;
global_variable int s_budgetMarkEvery;
global_variable int s_budgetRetryAt[BUDGET_TRIGGER_MAX];
global_variable int s_budgetRetryCount;
global_variable int s_budgetRetryFired;
global_variable int s_budgetLevelAtFrame[BUDGET_TRIGGER_MAX];
global_variable int s_budgetLevelAtID[BUDGET_TRIGGER_MAX];
global_variable int s_budgetLevelCount;
global_variable int s_budgetLevelFired;
global_variable s64 s_budgetFrame;

internal void NativeBudget_Report(void);

internal void NativeBudget_Arm(void)
{
	if (!s_budgetReportArmed)
	{
		s_budgetReportArmed = 1;
		Platform_AtExitReport(NativeBudget_Report);
	}
}

#if defined(CTR_NATIVE)
internal void NativeBudget_ReadConfig(void)
{
	const char *every = getenv("CTR_BUDGET_MARK_EVERY");
	const char *retry = getenv("CTR_BUDGET_RETRY_AT");
	const char *level = getenv("CTR_BUDGET_LEVEL_AT");

	s_budgetConfigRead = 1;

	if (every != NULL)
	{
		s_budgetMarkEvery = atoi(every);
	}

	if (retry != NULL)
	{
		const char *at = retry;

		while ((*at != '\0') && (s_budgetRetryCount < BUDGET_TRIGGER_MAX))
		{
			s_budgetRetryAt[s_budgetRetryCount++] = atoi(at);

			while ((*at != '\0') && (*at != ','))
			{
				at++;
			}
			if (*at == ',')
			{
				at++;
			}
		}
	}

	if (level != NULL)
	{
		const char *at = level;

		while ((*at != '\0') && (s_budgetLevelCount < BUDGET_TRIGGER_MAX))
		{
			s_budgetLevelAtFrame[s_budgetLevelCount] = atoi(at);
			s_budgetLevelAtID[s_budgetLevelCount] = -1;

			while ((*at != '\0') && (*at != ',') && (*at != ':'))
			{
				at++;
			}
			if (*at == ':')
			{
				s_budgetLevelAtID[s_budgetLevelCount] = atoi(at + 1);
				while ((*at != '\0') && (*at != ','))
				{
					at++;
				}
			}
			s_budgetLevelCount++;

			if (*at == ',')
			{
				at++;
			}
		}
	}

	if ((s_budgetMarkEvery > 0) || (s_budgetRetryCount > 0) || (s_budgetLevelCount > 0))
	{
		Platform_Log("[CTR Budget] driver: mark every %d frame(s), %d retry trigger(s), %d level switch(es)\n", s_budgetMarkEvery,
		             s_budgetRetryCount, s_budgetLevelCount);
	}
}
#endif

// ---------------------------------------------------------------------------
// THE RUNTIME MEASUREMENT. FOUR CLOCKS, NOTHING ELSE.
//
// SDL comes before game_unity.h in main.c, so SDL_GetPerformanceCounter
// is already available here - no time source of its own, no windows.h in game code. Every
// clock only sums up; dividing happens only in the report.
//
// The crate loop gets a clock OF ITS OWN, because it is the only one that runs
// over everything every frame: eight driver slots times 42 instances. The other three
// clocks are the pre-logic, the post-logic without the crates and the pass
// through the visibility field.
// ---------------------------------------------------------------------------

global_variable const char *const s_budgetScopeName[BUDGET_SCOPE_COUNT] = {
    "pre-logic", "post-logic (crates in)", "crate loop", "vis pass",
};

global_variable u64 s_budgetScopeTotal[BUDGET_SCOPE_COUNT];
global_variable s64 s_budgetScopeCalls[BUDGET_SCOPE_COUNT];
global_variable u64 s_budgetScopeStart[BUDGET_SCOPE_COUNT];

void NativeBudget_ScopeBegin(int scope)
{
	if ((scope >= 0) && (scope < BUDGET_SCOPE_COUNT))
	{
		s_budgetScopeStart[scope] = SDL_GetPerformanceCounter();
	}
}

void NativeBudget_ScopeEnd(int scope)
{
	if ((scope >= 0) && (scope < BUDGET_SCOPE_COUNT))
	{
		s_budgetScopeTotal[scope] += SDL_GetPerformanceCounter() - s_budgetScopeStart[scope];
		s_budgetScopeCalls[scope]++;
	}
}

// THE SNAPSHOT. One line with everything that would show a leak.
void NativeBudget_Mark(const char *what)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct JitPool *pools;
	int i;
	int free;

	NativeBudget_Arm();

	if (gGT == NULL)
	{
		return;
	}

	s_budgetMarks++;
	pools = &gGT->JitPools.thread;
	free = MEMPACK_GetFreeBytes();

	Platform_Log("[CTR Budget] %-22s frame %lld, level %d: thread %d/%d, instance %d/%d (%d on taken), particle %d/%d, oscillator %d/%d, "
	             "rain %d/%d; numParticles %d; mempack free %d\n",
	             what, s_budgetFrame, gGT->levelID, pools[0].maxItems - pools[0].free.count, pools[0].maxItems,
	             pools[1].maxItems - pools[1].free.count, pools[1].maxItems, pools[1].taken.count,
	             pools[5].maxItems - pools[5].free.count, pools[5].maxItems, pools[6].maxItems - pools[6].free.count, pools[6].maxItems,
	             pools[7].maxItems - pools[7].free.count, pools[7].maxItems, gGT->numParticles, free);

	NativeTrackMod_BudgetLine(what);

	for (i = 0; i < BUDGET_POOL_COUNT; i++)
	{
		s_budgetPoolMax[i] = pools[i].maxItems;
		s_budgetPoolItem[i] = pools[i].itemSize;
		s_budgetPoolBytes[i] = pools[i].poolSize;
	}
}

// THE PER-FRAME TAP. It sits in MainFrame_RenderFrame and runs on every
// track, with and without track code.
void NativeBudget_Sample(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct JitPool *pools;
	int i;
	int free;

	NativeBudget_Arm();

#if defined(CTR_NATIVE)
	if (!s_budgetConfigRead)
	{
		NativeBudget_ReadConfig();
	}
#endif

	if (gGT == NULL)
	{
		return;
	}

	s_budgetFrame++;
	s_budgetSamples++;
	pools = &gGT->JitPools.thread;

	for (i = 0; i < BUDGET_POOL_COUNT; i++)
	{
		int used = pools[i].maxItems - pools[i].free.count;

		if (used > s_budgetPoolPeak[i])
		{
			s_budgetPoolPeak[i] = used;
		}

		s_budgetPoolMax[i] = pools[i].maxItems;
		s_budgetPoolItem[i] = pools[i].itemSize;
		s_budgetPoolBytes[i] = pools[i].poolSize;
	}

	if (gGT->numParticles > s_budgetParticlePeak)
	{
		s_budgetParticlePeak = gGT->numParticles;
	}

	free = MEMPACK_GetFreeBytes();
	if (s_budgetMempackFreeFirst < 0)
	{
		s_budgetMempackFreeFirst = free;
	}
	if ((s_budgetMempackFreeMin < 0) || (free < s_budgetMempackFreeMin))
	{
		s_budgetMempackFreeMin = free;
	}

	if ((s_budgetMarkEvery > 0) && ((s_budgetFrame % s_budgetMarkEvery) == 0))
	{
		NativeBudget_Mark("periodic");
	}

	// THE TRIGGERS. Exactly what MainFreeze.c does at its two places.
	if (s_budgetRetryFired < s_budgetRetryCount)
	{
		if (s_budgetFrame >= s_budgetRetryAt[s_budgetRetryFired])
		{
			s_budgetRetryFired++;

			NativeBudget_Mark("before retry");

			gGT->gameMode1 &= ~PAUSE_1;
			if (RaceFlag_IsFullyOffScreen())
			{
				RaceFlag_BeginTransition(1);
			}
			sdata->Loading.stage = LOAD_RESTART;

			Platform_Log("[CTR Budget] retry %d of %d requested at frame %lld\n", s_budgetRetryFired, s_budgetRetryCount, s_budgetFrame);
		}
	}

	if (s_budgetLevelFired < s_budgetLevelCount)
	{
		if (s_budgetFrame >= s_budgetLevelAtFrame[s_budgetLevelFired])
		{
			int want = s_budgetLevelAtID[s_budgetLevelFired];

			s_budgetLevelFired++;

			NativeBudget_Mark("before level switch");

			gGT->gameMode1 &= ~PAUSE_1;
			MainRaceTrack_RequestLoad((s16)want);

			Platform_Log("[CTR Budget] level switch %d of %d to level %d requested at frame %lld\n", s_budgetLevelFired, s_budgetLevelCount, want,
			             s_budgetFrame);
		}
	}
}

internal void NativeBudget_Report(void)
{
	int i;

	Platform_Log("[CTR Budget] at exit: %lld sample(s), %lld mark(s); particle counter peak %d\n", s_budgetSamples, s_budgetMarks,
	             s_budgetParticlePeak);

	for (i = 0; i < BUDGET_POOL_COUNT; i++)
	{
		int percent = (s_budgetPoolMax[i] > 0) ? ((s_budgetPoolPeak[i] * 100) / s_budgetPoolMax[i]) : 0;

		Platform_Log("[CTR Budget] at exit:   %-12s peak %4d of %4d (%3d%%), %u byte(s) each, %d byte(s) of pool\n", s_budgetPoolName[i],
		             s_budgetPoolPeak[i], s_budgetPoolMax[i], percent, s_budgetPoolItem[i], s_budgetPoolBytes[i]);
	}

	Platform_Log("[CTR Budget] at exit: mempack free %d byte(s) at the first sample, %d at the tightest\n", s_budgetMempackFreeFirst,
	             s_budgetMempackFreeMin);

	{
		u64 freq = SDL_GetPerformanceFrequency();

		if (freq == 0)
		{
			freq = 1;
		}

		for (i = 0; i < BUDGET_SCOPE_COUNT; i++)
		{
			double totalMs = (double)s_budgetScopeTotal[i] * 1000.0 / (double)freq;
			double perCall = (s_budgetScopeCalls[i] > 0) ? (totalMs / (double)s_budgetScopeCalls[i]) : 0.0;

			Platform_Log("[CTR Budget] at exit:   %-22s %lld call(s), %.3f ms in all, %.5f ms each\n", s_budgetScopeName[i], s_budgetScopeCalls[i],
			             totalMs, perCall);
		}
	}
}
