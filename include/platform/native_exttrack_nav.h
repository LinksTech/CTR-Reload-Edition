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
	const int *up;   // NULL, or 3 ints per frame (see "up" below)
	const int *zone; // NULL, or 1 int per frame (see "zone" below)
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

// ANTI-GRAVITY STRETCHES (optional, per path): "up" - one [x, y, z] per frame,
// the surface normal at the frame (4096 = 1.0, length 4096 +- 64) - and "zone"
// - one integer per frame, 0 = no zone, 1..EXTNAV_ZONES_MAX = the zone the
// frame lies in. A path without them (every retail-like track) has no zone
// frame, and every ExtNav_* query below answers 0 at once: BOTS.c runs its
// original code. With them BOTS.c keeps a bot in a zone on its rail (no air
// phase in world y), turns its up vector to the frames' `up`, never lets it
// slide off in free physics, and takes a respawn in a zone back to the last
// frame before the zone.
enum
{
	EXTNAV_ZONES_MAX = 8,
};
struct NavFrame;
struct Driver;
// The zone of a frame of the external paths (0 = none, or not an external frame).
int ExtNav_FrameZone(const struct NavFrame *frame);
// The up vector between two frames at percentage (4096 = at next), normalised
// to 4096; frames outside a zone count as (0, 4096, 0). 1 = one of the two is a
// zone frame and *up is set, 0 = neither (up untouched).
int ExtNav_FrameUpLerp(const struct NavFrame *curr, const struct NavFrame *next, int percentage, short up[3]);
// The last frame before the zone stretch that contains frame (walking back on
// its path); frame itself when it is not in a zone.
struct NavFrame *ExtNav_ZoneEntryFrame(struct NavFrame *frame);
// Per bot and logic frame (BOTS_ThTick_Drive): one log line when a bot enters
// or leaves a zone.
void ExtNav_BotTick(const struct Driver *bot, const struct NavFrame *curr);
// A bot is mask-grabbed / reset; zone = the zone of its frame. One log line.
void ExtNav_BotRespawn(const struct Driver *bot, int zone, int frameIndex);
// 1 = the level's nav table is the external one (the paths of this file).
int ExtNav_Active(void);
// THE LOCAL UP u (for measuring; it does not depend on a kart): the up of the
// installed nav frame nearest to pos (world units, 3D distance, all paths);
// a frame outside a zone (and every frame of a path without zones) gives
// (0, 4096, 0). 1 = paths are installed (up set; *path / *frame = that frame
// when the pointers are not NULL), 0 = none (up = (0, 4096, 0), path/frame -1).
int ExtNav_UpAt(const double pos[3], short up[3], int *path, int *frame);
// --dev-ext-kart-log (main.c; measuring only, 0 = off): from the dispatcher
// after the game logic of a frame, one "[CTR ExtKart]" line per kart - its
// distance to the ground along the local up (format in native_exttrack_nav.c).
// A log only: no game state is written. Off: returns at once.
extern int g_cfg_extKartLog;
void ExtNav_OnLogicFrame(void);

// From the dispatcher, once per level load of the active external track,
// after the collision and the race part (the checkpoint table) and before
// MainInit_Drivers (game/MAIN/MainInit.c, via MainInit_FinalizeInit).
void ExtNav_OnLevelLoaded(struct Level *lev);
// --ext-selftest nav: prints "ext selftest nav passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtNav_Selftest(void);

#endif
