#include <platform/engine.h>
#include <stdio.h>
#include <string.h>

// ENGINE TOUR - see include/platform/engine.h, TRACK TOUR (round of 2026-10-15).
//
// Two parts.
//
// THE REGISTRY. A package that wants per-track lines in a --level-tour run
// registers one function (Engine_TourRegister) at its first load under
// --engine. The tour (game/DebugMenu.c) calls Engine_TourNotify at the first
// race frame of an entry (phase 0) and after its last (phase 1); without
// --engine it returns after one compare and nothing is ever registered.
//
// THE TOUR'S PURE HELPERS (EngineTour_*). The parts of the tour that are plain
// arithmetic - the --level-tour-shots list, the burst schedule, the file name,
// the progress fold over the lap line, the fingerprint - so that
// Engine_SelfTestTour can check them without a game. They run in every tour,
// with and without --engine. Nothing here reads or writes game state: the tour
// reads the values (read only) and hands them in.

#define ENGINE_TOUR_SHOTS_MAX 32      // entries of --level-tour-shots
#define ENGINE_TOUR_BURST_FRAMES 12   // snapshots of consecutive race frames per burst
#define ENGINE_TOUR_ALL_LEVELS (-1)   // an entry without "L<level>@": every level
#define ENGINE_TOUR_FRAME_MAX 1000000 // a race frame beyond this is a typo, not a plan
#define ENGINE_TOUR_LEVEL_MAX 999
#define ENGINE_TOUR_FNV_START 2166136261u

// --------------------------------------------------------------------------
// Registry

global_variable EngineTourReportFn s_engTourReports[ENGINE_TOUR_REPORTS_MAX];
global_variable int s_engTourReportCount;

int Engine_TourRegister(EngineTourReportFn fn)
{
	int i;

	if (fn == NULL)
	{
		return 0;
	}
	for (i = 0; i < s_engTourReportCount; i++)
	{
		if (s_engTourReports[i] == fn)
		{
			return 1;
		}
	}
	if (s_engTourReportCount >= ENGINE_TOUR_REPORTS_MAX)
	{
		return 0;
	}
	s_engTourReports[s_engTourReportCount++] = fn;
	return 1;
}

void Engine_TourNotify(int entry, int levelID, int phase)
{
	int i;

	if (!g_cfg_engine)
	{
		return;
	}
	for (i = 0; i < s_engTourReportCount; i++)
	{
		s_engTourReports[i](entry, levelID, phase);
	}
}

// --------------------------------------------------------------------------
// The shot list: "300,750,L3@420,L12@610". A plain number is a burst start (race
// frame, from 1) on every level, L<level>@<frame> one on that level only. Spaces
// around an entry are allowed, an empty entry is not. Writes up to max entries
// (levels[i] = ENGINE_TOUR_ALL_LEVELS for a plain one) and returns their count;
// -1 refuses the whole list (*badAt = the 1-based entry that was wrong). An empty
// text is 0 entries.

internal int EngineTour_Number(const char **at, int maxValue, int *out)
{
	const char *p = *at;
	int value = 0;
	int digits = 0;

	while ((*p >= '0') && (*p <= '9'))
	{
		value = (value * 10) + (*p - '0');
		digits++;
		p++;
		if (value > maxValue)
		{
			return 0;
		}
	}
	if (digits == 0)
	{
		return 0;
	}
	*out = value;
	*at = p;
	return 1;
}

int EngineTour_ParseShots(const char *text, int *levels, int *frames, int max, int *badAt)
{
	const char *at = text;
	int count = 0;
	int entry = 0;

	if (badAt != NULL)
	{
		*badAt = 0;
	}
	if ((text == NULL) || (levels == NULL) || (frames == NULL))
	{
		return 0;
	}
	while (*at == ' ')
	{
		at++;
	}
	if (*at == '\0')
	{
		return 0;
	}
	for (;;)
	{
		int level = ENGINE_TOUR_ALL_LEVELS;
		int frame = 0;

		entry++;
		while (*at == ' ')
		{
			at++;
		}
		if ((*at == 'L') || (*at == 'l'))
		{
			at++;
			if (!EngineTour_Number(&at, ENGINE_TOUR_LEVEL_MAX, &level) || (*at != '@'))
			{
				break;
			}
			at++;
		}
		if (!EngineTour_Number(&at, ENGINE_TOUR_FRAME_MAX, &frame) || (frame < 1))
		{
			break;
		}
		while (*at == ' ')
		{
			at++;
		}
		if ((*at != ',') && (*at != '\0'))
		{
			break;
		}
		if (count >= max)
		{
			break;
		}
		levels[count] = level;
		frames[count] = frame;
		count++;
		if (*at == '\0')
		{
			return count;
		}
		at++; // the comma
	}
	if (badAt != NULL)
	{
		*badAt = entry;
	}
	return -1;
}

// The bursts of one level: the start frames of every entry meant for all levels
// or for this one, ascending. A start inside the burst before it (closer than
// ENGINE_TOUR_BURST_FRAMES, the same frame included) is dropped and counted in
// *overlapped (may be NULL). Returns the count written to starts (at most max).
int EngineTour_BurstsForLevel(const int *levels, const int *frames, int count, int level, int *starts, int max, int *overlapped)
{
	int n = 0;
	int i;

	if (overlapped != NULL)
	{
		*overlapped = 0;
	}
	for (i = 0; i < count; i++)
	{
		int j;

		if ((levels[i] != ENGINE_TOUR_ALL_LEVELS) && (levels[i] != level))
		{
			continue;
		}
		if (n >= max)
		{
			if (overlapped != NULL)
			{
				(*overlapped)++;
			}
			continue;
		}
		// Insertion, ascending.
		j = n;
		while ((j > 0) && (starts[j - 1] > frames[i]))
		{
			starts[j] = starts[j - 1];
			j--;
		}
		starts[j] = frames[i];
		n++;
	}
	// Drop the starts that fall inside the burst kept before them.
	{
		int kept = 0;

		for (i = 0; i < n; i++)
		{
			if ((kept > 0) && (starts[i] < starts[kept - 1] + ENGINE_TOUR_BURST_FRAMES))
			{
				if (overlapped != NULL)
				{
					(*overlapped)++;
				}
				continue;
			}
			starts[kept++] = starts[i];
		}
		n = kept;
	}
	return n;
}

// The 0-based burst a race frame belongs to, -1 when none.
int EngineTour_BurstOf(const int *starts, int n, int raceFrame)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if ((raceFrame >= starts[i]) && (raceFrame < starts[i] + ENGINE_TOUR_BURST_FRAMES))
		{
			return i;
		}
	}
	return -1;
}

// <shotName without .bmp>-t<NN>-L<level>-<burst>-<raceFrame>.bmp; "tour" when
// shotName is empty. flicker_check.py takes <base>-t<NN>-L<level>-<burst> as the
// burst name and the race frame as the number. 0 when it does not fit.
int EngineTour_ShotName(char *out, size_t size, const char *shotName, int entry, int level, int burst, int raceFrame)
{
	char base[1024];
	size_t len;
	int written;

	if ((out == NULL) || (size == 0))
	{
		return 0;
	}
	snprintf(base, sizeof(base), "%s", ((shotName != NULL) && (shotName[0] != '\0')) ? shotName : "tour");
	len = strlen(base);
	if ((len > 4) && ((strcmp(base + len - 4, ".bmp") == 0) || (strcmp(base + len - 4, ".BMP") == 0)))
	{
		base[len - 4] = '\0';
	}
	written = snprintf(out, size, "%s-t%02d-L%d-%d-%d.bmp", base, entry, level, burst, raceFrame);
	return ((written > 0) && ((size_t)written < size)) ? 1 : 0;
}

// --------------------------------------------------------------------------
// The oracle frames of the material comparison (round 18, W2; ENGINE ORACLE
// (tour) in include/platform/engine.h). --level-tour-oracle has the syntax of
// --level-tour-shots and is read with EngineTour_ParseShots; every listed race
// frame is one oracle frame (no burst), next to the tour's own frames: the
// start (the first race frame after the countdown), before and after the line
// (the first forward fold of the drive progress), a weapon in view and the
// models of --level-tour-oracle-see in view (game/DebugMenu.c, DebugMenu_TourOracle).

// 1 when raceFrame is listed for this level (a plain entry or L<level>@).
int EngineTour_OracleAt(const int *levels, const int *frames, int count, int level, int raceFrame)
{
	int i;

	for (i = 0; i < count; i++)
	{
		if (((levels[i] == ENGINE_TOUR_ALL_LEVELS) || (levels[i] == level)) && (frames[i] == raceFrame))
		{
			return 1;
		}
	}
	return 0;
}

// The tag of an oracle frame: t<NN>-L<level>-<kind> (kind "f330", "start",
// "line", "wpn"). 0 when it does not fit.
int EngineTour_OracleTag(char *out, size_t size, int entry, int level, const char *kind)
{
	int written;

	if ((out == NULL) || (size == 0) || (kind == NULL) || (kind[0] == '\0'))
	{
		return 0;
	}
	written = snprintf(out, size, "t%02d-L%d-%s", entry, level, kind);
	return ((written > 0) && ((size_t)written < size)) ? 1 : 0;
}

// The snapshot of an oracle frame: <shotName without .bmp>-<tag>.bmp ("tour"
// when shotName is empty). 0 when it does not fit.
int EngineTour_OracleShotName(char *out, size_t size, const char *shotName, const char *tag)
{
	char base[1024];
	size_t len;
	int written;

	if ((out == NULL) || (size == 0) || (tag == NULL))
	{
		return 0;
	}
	snprintf(base, sizeof(base), "%s", ((shotName != NULL) && (shotName[0] != '\0')) ? shotName : "tour");
	len = strlen(base);
	if ((len > 4) && ((strcmp(base + len - 4, ".bmp") == 0) || (strcmp(base + len - 4, ".BMP") == 0)))
	{
		base[len - 4] = '\0';
	}
	written = snprintf(out, size, "%s-%s.bmp", base, tag);
	return ((written > 0) && ((size_t)written < size)) ? 1 : 0;
}

// The line is near: at the speed of the last race frame (step = the fall of
// distanceToFinish_curr, > 0 forward) the remaining distance to the line is
// covered within frames race frames. The tour's estimate for the oracle frame
// before the line (the line itself is the first forward fold of the drive
// progress below); a kart that stands or rolls back is never near.
int EngineTour_LineSoon(s32 remaining, s32 step, int frames)
{
	if ((step <= 0) || (remaining < 0) || (frames <= 0))
	{
		return 0;
	}
	return ((s64)remaining <= (s64)step * (s64)frames) ? 1 : 0;
}

// --level-tour-oracle-see: "L4:flamejet+tikka,L5:plant" - per level the model
// names that must all be in view at once for that entry's "see" oracle frames.
// Up to ENGINE_TOUR_SEE_NAMES names joined by '+', each 1 to
// ENGINE_TOUR_SEE_NAME_BYTES - 1 characters of letters, digits and '_'. Writes
// up to max entries and returns their count; -1 refuses the whole list
// (*badAt = the 1-based entry). An empty text is 0 entries.
int EngineTour_ParseSee(const char *text, int *levels, char (*names)[ENGINE_TOUR_SEE_NAMES][ENGINE_TOUR_SEE_NAME_BYTES], int *counts, int max, int *badAt)
{
	const char *at = text;
	int count = 0;
	int entry = 0;

	if (badAt != NULL)
	{
		*badAt = 0;
	}
	if ((text == NULL) || (levels == NULL) || (names == NULL) || (counts == NULL) || (*text == '\0'))
	{
		return 0;
	}
	for (;;)
	{
		int level = 0;
		int n = 0;

		entry++;
		if ((count >= max) || ((*at != 'L') && (*at != 'l')))
		{
			break;
		}
		at++;
		if (!EngineTour_Number(&at, ENGINE_TOUR_LEVEL_MAX, &level) || (*at != ':'))
		{
			break;
		}
		at++;
		for (;;)
		{
			int len = 0;

			while (((*at >= 'a') && (*at <= 'z')) || ((*at >= 'A') && (*at <= 'Z')) || ((*at >= '0') && (*at <= '9')) || (*at == '_'))
			{
				if ((len >= ENGINE_TOUR_SEE_NAME_BYTES - 1) || (n >= ENGINE_TOUR_SEE_NAMES))
				{
					len = -1;
					break;
				}
				names[count][n][len++] = *at++;
			}
			if (len <= 0)
			{
				n = -1;
				break;
			}
			names[count][n][len] = '\0';
			n++;
			if (*at != '+')
			{
				break;
			}
			at++;
		}
		if ((n <= 0) || ((*at != ',') && (*at != '\0')))
		{
			break;
		}
		levels[count] = level;
		counts[count] = n;
		count++;
		if (*at == '\0')
		{
			return count;
		}
		at++; // the comma
	}
	if (badAt != NULL)
	{
		*badAt = entry;
	}
	return -1;
}

// --level-tour-frames: "900" or "900,L80@2100" - exactly one plain count (every
// entry) and any number of L<level>@<count> (that level). The syntax of the shot
// list; -1 refuses it (*badAt = the 1-based entry, the second plain count or 1
// when there is none).
int EngineTour_ParseFrames(const char *text, int *levels, int *frames, int max, int *badAt)
{
	const int count = EngineTour_ParseShots(text, levels, frames, max, badAt);
	int plain = 0;
	int i;

	if (count < 0)
	{
		return -1;
	}
	for (i = 0; i < count; i++)
	{
		if (levels[i] == ENGINE_TOUR_ALL_LEVELS)
		{
			plain++;
			if (plain > 1)
			{
				if (badAt != NULL)
				{
					*badAt = i + 1;
				}
				return -1;
			}
		}
	}
	if (plain == 0)
	{
		if (badAt != NULL)
		{
			*badAt = 1;
		}
		return -1;
	}
	return count;
}

// The race frames of one level: its L<level>@ count, else the plain one, else
// fallback (an unread list).
int EngineTour_FramesForLevel(const int *levels, const int *frames, int count, int level, int fallback)
{
	int plain = fallback;
	int i;

	for (i = 0; i < count; i++)
	{
		if (levels[i] == level)
		{
			return frames[i];
		}
		if (levels[i] == ENGINE_TOUR_ALL_LEVELS)
		{
			plain = frames[i];
		}
	}
	return plain;
}

// --------------------------------------------------------------------------
// Drive progress of the player's seat. The game keeps distanceToFinish_curr as
// the progress modulo the track length (game/Vehicle/VehLap.c:166-179), so it
// falls while the kart drives forward and jumps up by about one track length at
// the line. A frame step is folded into (-L/2, L/2]: crossing the line forward
// counts as the small forward step it is. Progress is the sum of the steps; a
// stall is a run of samples without a new maximum of that sum, counted from the
// first forward step on (the wait on the grid until the start is not a stall).
// One tour at a time, so the state is the file's own.

global_variable struct
{
	s64 sum;
	s64 best;
	u32 prev;
	u32 trackLength;
	int primed;
	int stall;
	int longestStall;
	int samples;
	int forwardFolds; // steps folded forward: the line crossed forward
} s_engTourDrive;

void EngineTour_DriveBegin(u32 trackLength)
{
	memset(&s_engTourDrive, 0, sizeof(s_engTourDrive));
	s_engTourDrive.trackLength = trackLength;
}

void EngineTour_DriveSample(u32 distanceToFinish)
{
	s64 step;

	s_engTourDrive.samples++;
	if (!s_engTourDrive.primed)
	{
		s_engTourDrive.primed = 1;
		s_engTourDrive.prev = distanceToFinish;
		return;
	}
	step = (s64)s_engTourDrive.prev - (s64)distanceToFinish;
	s_engTourDrive.prev = distanceToFinish;
	if (s_engTourDrive.trackLength != 0u)
	{
		const s64 length = (s64)s_engTourDrive.trackLength;

		if (step > length / 2)
		{
			step -= length;
		}
		else if (step <= -(length / 2))
		{
			step += length;
			s_engTourDrive.forwardFolds++;
		}
	}
	s_engTourDrive.sum += step;
	if (s_engTourDrive.sum > s_engTourDrive.best)
	{
		s_engTourDrive.best = s_engTourDrive.sum;
		s_engTourDrive.stall = 0;
		return;
	}
	if (s_engTourDrive.best <= 0)
	{
		return; // not moved yet: still on the grid
	}
	s_engTourDrive.stall++;
	if (s_engTourDrive.stall > s_engTourDrive.longestStall)
	{
		s_engTourDrive.longestStall = s_engTourDrive.stall;
	}
}

// Progress in thousandths of a lap (negative = backwards), 0 without a track length.
int EngineTour_DriveMilliLaps(void)
{
	if (s_engTourDrive.trackLength == 0u)
	{
		return 0;
	}
	return (int)((s_engTourDrive.sum * 1000) / (s64)s_engTourDrive.trackLength);
}

int EngineTour_DriveLongestStall(void)
{
	return s_engTourDrive.longestStall;
}

int EngineTour_DriveSamples(void)
{
	return s_engTourDrive.samples;
}

// How often the drive progress folded a step forward: the player crossed the
// start/finish line forward (the tour's line event; 0 without a track length).
int EngineTour_DriveForwardFolds(void)
{
	return s_engTourDrive.forwardFolds;
}

// FNV-1a over bytes, continuing from hash (ENGINE_TOUR_FNV_START to begin).
u32 EngineTour_Fnv(u32 hash, const void *bytesIn, u32 bytes)
{
	const u8 *p = (const u8 *)bytesIn;
	u32 i;

	for (i = 0; i < bytes; i++)
	{
		hash ^= p[i];
		hash *= 16777619u;
	}
	return hash;
}

u32 EngineTour_FnvStart(void)
{
	return ENGINE_TOUR_FNV_START;
}

// --------------------------------------------------------------------------
// Selftest

#define EGTOUR_CHECK(cond, ...)                       \
	do                                                \
	{                                                 \
		(*checks)++;                                  \
		if (!(cond))                                  \
		{                                             \
			printf("engine selftest: tour " __VA_ARGS__); \
			printf("\n");                             \
			failures++;                               \
		}                                             \
	} while (0)

global_variable int s_engTourTestCalls;
global_variable int s_engTourTestLast[3];

internal void EngineTour_TestReportA(int entry, int levelID, int phase)
{
	s_engTourTestCalls++;
	s_engTourTestLast[0] = entry;
	s_engTourTestLast[1] = levelID;
	s_engTourTestLast[2] = phase;
}

internal void EngineTour_TestReportB(int entry, int levelID, int phase)
{
	(void)entry;
	(void)levelID;
	(void)phase;
	s_engTourTestCalls += 10;
}

internal void EngineTour_TestReportC(int entry, int levelID, int phase)
{
	(void)entry;
	(void)levelID;
	(void)phase;
	s_engTourTestCalls += 100;
}

int Engine_SelfTestTour(int *checks)
{
	int failures = 0;
	int levels[ENGINE_TOUR_SHOTS_MAX];
	int frames[ENGINE_TOUR_SHOTS_MAX];
	int starts[ENGINE_TOUR_SHOTS_MAX];
	int bad = 0;
	int n;
	int over = 0;

	// (1) the shot list
	n = EngineTour_ParseShots("300,750,L3@420,L12@610", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad);
	EGTOUR_CHECK((n == 4) && (bad == 0), "parse: %d entries (bad %d), expected 4", n, bad);
	EGTOUR_CHECK((levels[0] == ENGINE_TOUR_ALL_LEVELS) && (frames[0] == 300) && (levels[1] == ENGINE_TOUR_ALL_LEVELS) && (frames[1] == 750) &&
	                 (levels[2] == 3) && (frames[2] == 420) && (levels[3] == 12) && (frames[3] == 610),
	             "parse: entries read wrong");
	n = EngineTour_ParseShots(" 5 , l0@7 ", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad);
	EGTOUR_CHECK((n == 2) && (frames[0] == 5) && (levels[1] == 0) && (frames[1] == 7), "parse: spaces / lower-case l not taken (%d)", n);
	EGTOUR_CHECK(EngineTour_ParseShots("", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == 0, "parse: empty text is not 0 entries");
	EGTOUR_CHECK((EngineTour_ParseShots("300,,750", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1) && (bad == 2), "parse: empty entry not refused at 2 (%d)",
	             bad);
	EGTOUR_CHECK((EngineTour_ParseShots("300,L3420", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1) && (bad == 2), "parse: L without @ not refused");
	EGTOUR_CHECK(EngineTour_ParseShots("0", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1, "parse: frame 0 not refused");
	EGTOUR_CHECK(EngineTour_ParseShots("12x", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1, "parse: trailing text not refused");
	EGTOUR_CHECK(EngineTour_ParseShots("-5", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1, "parse: a sign not refused");
	EGTOUR_CHECK(EngineTour_ParseShots("99999999", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1, "parse: a frame past the maximum not refused");
	EGTOUR_CHECK(EngineTour_ParseShots("1,2,3", levels, frames, 2, &bad) == -1, "parse: more entries than room not refused");
	EGTOUR_CHECK(EngineTour_ParseShots("300,", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1, "parse: trailing comma not refused");

	// (2) bursts per level: ascending, the level's own entries in, overlaps out
	n = EngineTour_ParseShots("750,L3@420,300,L12@610,L3@305", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad);
	EGTOUR_CHECK(n == 5, "bursts: parse gave %d", n);
	n = EngineTour_BurstsForLevel(levels, frames, 5, 3, starts, ENGINE_TOUR_SHOTS_MAX, &over);
	EGTOUR_CHECK((n == 3) && (starts[0] == 300) && (starts[1] == 420) && (starts[2] == 750) && (over == 1),
	             "bursts level 3: %d starts, overlapped %d (expected 300, 420, 750 and 1)", n, over);
	n = EngineTour_BurstsForLevel(levels, frames, 5, 12, starts, ENGINE_TOUR_SHOTS_MAX, &over);
	EGTOUR_CHECK((n == 3) && (starts[0] == 300) && (starts[1] == 610) && (starts[2] == 750) && (over == 0), "bursts level 12: %d starts", n);
	n = EngineTour_BurstsForLevel(levels, frames, 5, 0, starts, ENGINE_TOUR_SHOTS_MAX, &over);
	EGTOUR_CHECK((n == 2) && (starts[0] == 300) && (starts[1] == 750), "bursts level 0: %d starts", n);
	{
		const int sameLevels[2] = {ENGINE_TOUR_ALL_LEVELS, 5};
		const int sameFrames[2] = {100, 100};

		n = EngineTour_BurstsForLevel(sameLevels, sameFrames, 2, 5, starts, ENGINE_TOUR_SHOTS_MAX, &over);
		EGTOUR_CHECK((n == 1) && (starts[0] == 100) && (over == 1), "bursts: the same start twice is not one burst");
		n = EngineTour_BurstsForLevel(sameLevels, sameFrames, 2, 5, starts, 1, &over);
		EGTOUR_CHECK((n == 1) && (over == 1), "bursts: a full table is not counted");
	}

	// (3) burst membership: start .. start + 11
	{
		const int s[2] = {300, 750};

		EGTOUR_CHECK((EngineTour_BurstOf(s, 2, 299) == -1) && (EngineTour_BurstOf(s, 2, 300) == 0) && (EngineTour_BurstOf(s, 2, 311) == 0) &&
		                 (EngineTour_BurstOf(s, 2, 312) == -1) && (EngineTour_BurstOf(s, 2, 761) == 1) && (EngineTour_BurstOf(s, 2, 762) == -1),
		             "burst membership wrong");
		EGTOUR_CHECK(EngineTour_BurstOf(s, 0, 300) == -1, "burst membership without bursts");
	}

	// (4) file names
	{
		char name[64];

		EGTOUR_CHECK(EngineTour_ShotName(name, sizeof(name), "dir/e.bmp", 3, 12, 2, 751) && (strcmp(name, "dir/e-t03-L12-2-751.bmp") == 0), "name: '%s'",
		             name);
		EGTOUR_CHECK(EngineTour_ShotName(name, sizeof(name), "", 1, 0, 1, 300) && (strcmp(name, "tour-t01-L0-1-300.bmp") == 0), "name without base: '%s'",
		             name);
		EGTOUR_CHECK(EngineTour_ShotName(name, sizeof(name), "x.BMP", 18, 17, 1, 9) && (strcmp(name, "x-t18-L17-1-9.bmp") == 0), "name .BMP: '%s'", name);
		EGTOUR_CHECK(!EngineTour_ShotName(name, 8, "dir/e.bmp", 3, 12, 2, 751), "name: a too small buffer is not refused");
	}

	// (4b) oracle frames: membership, tags, snapshot names, the line, the frames list
	{
		char name[64];
		const int ol[3] = {ENGINE_TOUR_ALL_LEVELS, 3, ENGINE_TOUR_ALL_LEVELS};
		const int of[3] = {300, 330, 600};

		EGTOUR_CHECK(EngineTour_OracleAt(ol, of, 3, 3, 330) && EngineTour_OracleAt(ol, of, 3, 3, 300) && EngineTour_OracleAt(ol, of, 3, 12, 600),
		             "oracle: a listed frame not found");
		EGTOUR_CHECK(!EngineTour_OracleAt(ol, of, 3, 12, 330) && !EngineTour_OracleAt(ol, of, 3, 3, 301) && !EngineTour_OracleAt(ol, of, 0, 3, 300),
		             "oracle: an unlisted frame found");
		EGTOUR_CHECK(EngineTour_OracleTag(name, sizeof(name), 4, 3, "f330") && (strcmp(name, "t04-L3-f330") == 0), "oracle tag: '%s'", name);
		EGTOUR_CHECK(EngineTour_OracleTag(name, sizeof(name), 27, 80, "start") && (strcmp(name, "t27-L80-start") == 0), "oracle tag: '%s'", name);
		EGTOUR_CHECK(!EngineTour_OracleTag(name, 8, 4, 3, "start") && !EngineTour_OracleTag(name, sizeof(name), 4, 3, ""),
		             "oracle tag: a short buffer or an empty kind not refused");
		EGTOUR_CHECK(EngineTour_OracleShotName(name, sizeof(name), "dir/o.bmp", "t04-L3-line") && (strcmp(name, "dir/o-t04-L3-line.bmp") == 0),
		             "oracle shot: '%s'", name);
		EGTOUR_CHECK(EngineTour_OracleShotName(name, sizeof(name), "", "menu") && (strcmp(name, "tour-menu.bmp") == 0), "oracle shot without base: '%s'", name);
		EGTOUR_CHECK(EngineTour_LineSoon(200, 10, 20) && EngineTour_LineSoon(0, 1, 20), "line soon: not near at 20 frames");
		EGTOUR_CHECK(!EngineTour_LineSoon(201, 10, 20) && !EngineTour_LineSoon(50, 0, 20) && !EngineTour_LineSoon(50, -5, 20) && !EngineTour_LineSoon(-3, 10, 20),
		             "line soon: near where it is not");
		{
			int sl[4];
			char sn[4][ENGINE_TOUR_SEE_NAMES][ENGINE_TOUR_SEE_NAME_BYTES];
			int sc[4];

			n = EngineTour_ParseSee("L4:flamejet+tikka,L5:plant", sl, sn, sc, 4, &bad);
			EGTOUR_CHECK((n == 2) && (sl[0] == 4) && (sc[0] == 2) && (strcmp(sn[0][0], "flamejet") == 0) && (strcmp(sn[0][1], "tikka") == 0) && (sl[1] == 5) &&
			                 (sc[1] == 1) && (strcmp(sn[1][0], "plant") == 0),
			             "see: list read wrong (%d)", n);
			EGTOUR_CHECK(EngineTour_ParseSee("", sl, sn, sc, 4, &bad) == 0, "see: empty text is not 0 entries");
			EGTOUR_CHECK((EngineTour_ParseSee("L4:a+b+c+d", sl, sn, sc, 4, &bad) == -1) && (bad == 1), "see: a fourth name not refused");
			EGTOUR_CHECK((EngineTour_ParseSee("L4:tikka,L5:", sl, sn, sc, 4, &bad) == -1) && (bad == 2), "see: an empty name not refused");
			EGTOUR_CHECK((EngineTour_ParseSee("L4:tikka+", sl, sn, sc, 4, &bad) == -1) && (EngineTour_ParseSee("4:tikka", sl, sn, sc, 4, &bad) == -1) &&
			                 (EngineTour_ParseSee("L4:a b", sl, sn, sc, 4, &bad) == -1) && (EngineTour_ParseSee("L4:abcdefghijklmnop", sl, sn, sc, 4, &bad) == -1),
			             "see: a broken entry not refused");
			EGTOUR_CHECK(EngineTour_ParseSee("L1:a,L2:b", sl, sn, sc, 1, &bad) == -1, "see: more entries than room not refused");
		}
		n = EngineTour_ParseFrames("900,L80@2100,L3@1200", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad);
		EGTOUR_CHECK((n == 3) && (EngineTour_FramesForLevel(levels, frames, n, 80, 600) == 2100) && (EngineTour_FramesForLevel(levels, frames, n, 3, 600) == 1200) &&
		                 (EngineTour_FramesForLevel(levels, frames, n, 5, 600) == 900),
		             "frames: list read wrong (%d)", n);
		n = EngineTour_ParseFrames("900", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad);
		EGTOUR_CHECK((n == 1) && (EngineTour_FramesForLevel(levels, frames, n, 17, 600) == 900), "frames: a plain count not read");
		EGTOUR_CHECK((EngineTour_ParseFrames("900,1200", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1) && (bad == 2), "frames: two plain counts not refused");
		EGTOUR_CHECK((EngineTour_ParseFrames("L3@900", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1) && (bad == 1), "frames: no plain count not refused");
		EGTOUR_CHECK(EngineTour_ParseFrames("0", levels, frames, ENGINE_TOUR_SHOTS_MAX, &bad) == -1, "frames: 0 not refused");
		EGTOUR_CHECK(EngineTour_FramesForLevel(levels, frames, 0, 3, 600) == 600, "frames: no list is not the fallback");
	}

	// (5) progress: forward, over the line, backwards, stall
	EngineTour_DriveBegin(1000u);
	EngineTour_DriveSample(600u);
	EngineTour_DriveSample(600u); // standing on the grid: no stall yet
	EngineTour_DriveSample(610u); // rolled back on the grid: no stall either
	EngineTour_DriveSample(600u);
	EGTOUR_CHECK((EngineTour_DriveLongestStall() == 0) && (EngineTour_DriveMilliLaps() == 0), "progress: the grid wait counted as a stall (%d)",
	             EngineTour_DriveLongestStall());
	EngineTour_DriveSample(500u);
	EngineTour_DriveSample(100u);
	EGTOUR_CHECK((EngineTour_DriveMilliLaps() == 500) && (EngineTour_DriveLongestStall() == 0), "progress forward: %d milli-laps", EngineTour_DriveMilliLaps());
	EngineTour_DriveSample(950u); // over the line: 100 -> 950 is 150 forward
	EGTOUR_CHECK(EngineTour_DriveMilliLaps() == 650, "progress over the line: %d milli-laps (expected 650)", EngineTour_DriveMilliLaps());
	EGTOUR_CHECK(EngineTour_DriveForwardFolds() == 1, "progress over the line: %d forward fold(s), expected 1", EngineTour_DriveForwardFolds());
	EngineTour_DriveSample(990u); // 40 backwards
	EngineTour_DriveSample(990u); // standing
	EngineTour_DriveSample(960u); // 30 forward, still below the best
	EGTOUR_CHECK((EngineTour_DriveMilliLaps() == 640) && (EngineTour_DriveLongestStall() == 3), "progress backwards/stall: %d milli-laps, stall %d",
	             EngineTour_DriveMilliLaps(), EngineTour_DriveLongestStall());
	EngineTour_DriveSample(900u); // new best: the stall ends
	EngineTour_DriveSample(899u);
	EGTOUR_CHECK((EngineTour_DriveLongestStall() == 3) && (EngineTour_DriveSamples() == 12), "stall reset or samples wrong (%d, %d)", EngineTour_DriveLongestStall(),
	             EngineTour_DriveSamples());
	EngineTour_DriveSample(10u); // a step of more than half a lap: 111 backwards over the line (899 -> 1000 -> 0 -> 10)
	EGTOUR_CHECK(EngineTour_DriveMilliLaps() == 590, "progress backwards over the line: %d milli-laps (expected 590)", EngineTour_DriveMilliLaps());
	EGTOUR_CHECK(EngineTour_DriveForwardFolds() == 1, "backwards over the line counted as a forward fold (%d)", EngineTour_DriveForwardFolds());
	EngineTour_DriveBegin(0u);
	EngineTour_DriveSample(10u);
	EngineTour_DriveSample(5u);
	EGTOUR_CHECK(EngineTour_DriveMilliLaps() == 0, "progress without a track length is not 0");

	// (6) fingerprint: FNV-1a reference values
	{
		const u8 a = 'a';

		EGTOUR_CHECK(EngineTour_Fnv(EngineTour_FnvStart(), "", 0) == 2166136261u, "fnv of nothing");
		EGTOUR_CHECK(EngineTour_Fnv(EngineTour_FnvStart(), &a, 1) == 0xe40c292cu, "fnv of 'a' is %08x", EngineTour_Fnv(EngineTour_FnvStart(), &a, 1));
		EGTOUR_CHECK(EngineTour_Fnv(EngineTour_Fnv(EngineTour_FnvStart(), "fo", 2), "obar", 4) == EngineTour_Fnv(EngineTour_FnvStart(), "foobar", 6),
		             "fnv does not continue");
	}

	// (7) registry: dedupe, order, notify only with --engine
	{
		const int engineWas = g_cfg_engine;
		const int countWas = s_engTourReportCount;
		EngineTourReportFn saved[ENGINE_TOUR_REPORTS_MAX];
		int i;

		memcpy(saved, s_engTourReports, sizeof(saved));
		s_engTourReportCount = 0;
		s_engTourTestCalls = 0;
		EGTOUR_CHECK(Engine_TourRegister(EngineTour_TestReportA) && Engine_TourRegister(EngineTour_TestReportA) && (s_engTourReportCount == 1),
		             "registry: a second registration is not ignored");
		EGTOUR_CHECK(!Engine_TourRegister(NULL), "registry: NULL registered");
		EGTOUR_CHECK(Engine_TourRegister(EngineTour_TestReportB) && (s_engTourReportCount == 2), "registry: second function not registered");
		g_cfg_engine = 0;
		Engine_TourNotify(1, 3, 0);
		EGTOUR_CHECK(s_engTourTestCalls == 0, "registry: notify without --engine called %d", s_engTourTestCalls);
		g_cfg_engine = 1;
		Engine_TourNotify(4, 12, 1);
		EGTOUR_CHECK((s_engTourTestCalls == 11) && (s_engTourTestLast[0] == 4) && (s_engTourTestLast[1] == 12) && (s_engTourTestLast[2] == 1),
		             "registry: notify with --engine: calls %d, last %d %d %d", s_engTourTestCalls, s_engTourTestLast[0], s_engTourTestLast[1],
		             s_engTourTestLast[2]);
		for (i = s_engTourReportCount; i < ENGINE_TOUR_REPORTS_MAX; i++)
		{
			s_engTourReports[s_engTourReportCount++] = EngineTour_TestReportB;
		}
		EGTOUR_CHECK(Engine_TourRegister(EngineTour_TestReportA) && !Engine_TourRegister(EngineTour_TestReportC) &&
		                 (s_engTourReportCount == ENGINE_TOUR_REPORTS_MAX),
		             "registry: full table: a known function not accepted or a new one registered");
		g_cfg_engine = engineWas;
		memcpy(s_engTourReports, saved, sizeof(saved));
		s_engTourReportCount = countWas;
	}

	return failures;
}
