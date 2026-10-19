#ifndef NATIVE_EXTTRACK_NAV_H
#define NATIVE_EXTTRACK_NAV_H

// THE EXTERNAL TRACK'S CPU PATHS (platform/native_exttrack_nav.c; --ext-track,
// see platform/engine_exttrack.c). Without --ext-track none of this is reached.
//
// The section "nav" of exttrack.json carries three paths of finished nav
// frames - 13 integers each, in the order of struct NavFrame
// (include/namespace_Bots.h): x, y, z, rot0, rot1, rot2, rot3, dist_xyz,
// dist_xz, flags, path_change, checkpoint, special - and "checkpoint_count",
// the number of checkpoint nodes they were made against. The converter
// computed and checked them; this part checks and copies.

enum
{
	EXTNAV_PATHS = 3,         // BOTS_NAV_PATH_COUNT (game/BOTS.c:28)
	EXTNAV_FIELDS = 13,       // integers per frame
	EXTNAV_FRAMES_MAX = 1023, // the frame field of pathChangeOpcode is 10 bits (BOTS.c:42-43)
};

// One path as handed to ExtNav_Install: count frames of EXTNAV_FIELDS ints.
struct ExtNavPathSrc
{
	int count;
	const int *fields;
};

struct ExtNavSrc
{
	int pathCount;
	int checkpointCount; // must equal lev->cnt_restart_points
	struct ExtNavPathSrc path[EXTNAV_PATHS];
};

struct Level;

// Checks src, writes three NavHeader blocks and sets lev->LevNavTable. On any
// problem lev->LevNavTable = NULL - no bots, never the donor's paths - and one
// log line says why. 1 = installed.
int ExtNav_Install(struct Level *lev, const struct ExtNavSrc *src);

// From the dispatcher, once per level load of the active external track,
// after the collision and the race part (the checkpoint table) and before
// MainInit_Drivers (game/MAIN/MainInit.c, via MainInit_FinalizeInit).
void ExtNav_OnLevelLoaded(struct Level *lev);
// --ext-selftest nav: prints "ext selftest nav passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtNav_Selftest(void);

#endif
