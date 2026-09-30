#include <common.h>

// ===========================================================================
// TRACK-SPECIFIC CODE, AS A MODULE OF ITS OWN.
//
// Sunset Vista is a custom track by Tramadoll. The track and its extras are
// his work; this module is only the compatibility layer that lets them run in
// the native port. The track itself is not part of this repository.
//
// Sunset Vista brings things no disc track has: a wandering door,
// three moving platforms, twelve bats, five fire bowls. On the
// PS1 these hang off entry points in the middle of the game code (the track's
// patch map, PATCH_MAP.md: main -> CTR_Main, post-logic 0x805F4340, seal
// constructor 0x8008141C). Such addresses do not exist here, and they are not
// supposed to be rebuilt here either.
//
// Instead: ONE module with four entry points that the host calls.
//
//   NativeTrackMod_Reset     after loading AND after a restart
//   NativeTrackMod_Release   when the memory under the actors goes away
//   NativeTrackMod_PreLogic  per tick, BEFORE the game logic
//   NativeTrackMod_PostLogic per tick, AFTER the game logic
//
// At the time this was written, exactly one actor lived here, the wandering
// door. The rest of the scaffold is there so the next actor needs no new
// entry point - not because more was already running here.
//
// WHY BEFORE AND AFTER. The track's integration notes (INTEGRATION.md
// section 2) set the order: flames and platforms BEFORE the game logic, the
// wall AFTER it - "update wall pose and resolve its collisions after driver
// movement". A wall that resolves its collision before the driving physics
// pushes a driver to a spot that the physics leaves again in the same frame.
//
// WHY PAUSE IS CHECKED TWICE. The same place says: "recheck pause
// state (the native game may open pause this tick)". The game logic can open
// the pause in THIS frame; whoever carries over the decision from the
// pre-logic lets the wall run on for one more frame.
// ===========================================================================

void Platform_Log(const char *format, ...);
void Platform_LogWarn(const char *format, ...);
void Platform_AtExitReport(void (*report)(void));

// From platform/native_assets.c. Declared locally here instead of pulled in
// through a header - DrawSky.c and MainInit.c do the same, and this
// file pulls nothing else from the host side.
int NativeTrack_ActiveForLevel(int levelID);
const char *NativeTrack_LoadedName(void);

// ---------------------------------------------------------------------------
// THE WANDERING DOOR.
//
// Source: the track's source (ps1_trackrom/src/wallstone_sunset.c), numbers
// confirmed in the track's integration notes (INTEGRATION.md section 3). Model
// "wallstone_test", instance "wallstone#0", scale 0x5240, two end points, four
// states 2500/700/2000/700 ms, linear translation in between, sound OtherFX 0x75
// on every start of a move.
//
// THE DOOR IS NOT AN INSTDEF. In the container "wallstone_test" carries the
// model ID -1: no serialized InstDef points to it (INTEGRATION.md
// section 1: "The moving wall and three platforms are created by custom
// runtime code, so they are not four additional serialized InstDefs"). Whoever
// wants to see it has to create it.
// ---------------------------------------------------------------------------

enum
{
	WALLSTONE_SCALE = 0x5240,

	WALLSTONE_POS1_X = -7603,
	WALLSTONE_POS1_Y = 3223,
	WALLSTONE_POS1_Z = -985,
	WALLSTONE_POS2_X = -7596,
	WALLSTONE_POS2_Y = 3223,
	WALLSTONE_POS2_Z = 296,

	WALLSTONE_LOCAL_MIN_X = -7,
	WALLSTONE_LOCAL_MIN_Y = -640,
	WALLSTONE_LOCAL_MIN_Z = -961,
	WALLSTONE_LOCAL_MAX_X = 647,
	WALLSTONE_LOCAL_MAX_Y = 640,
	WALLSTONE_LOCAL_MAX_Z = 958,

	WALLSTONE_DRIVER_RADIUS = 0x60,

	WALLSTONE_HOLD1_MS = 2500,
	WALLSTONE_MOVE_MS = 700,
	WALLSTONE_HOLD2_MS = 2000,

	WALLSTONE_SOUND_ID = 0x75,

	// The largest step one frame may take. The source clamps at
	// 250 ms, and that matters more here than there: on a PC a frame can take
	// very long when the window is in the background or a load path stalls.
	// Without the clamp the door then jumps over its own movement.
	WALLSTONE_STEP_MAX_MS = 250,
};

enum WallStoneState
{
	WALLSTONE_AT_POS1 = 0,
	WALLSTONE_MOVING_TO_POS2 = 1,
	WALLSTONE_AT_POS2 = 2,
	WALLSTONE_MOVING_TO_POS1 = 3,
	WALLSTONE_STATE_COUNT = 4,
};

global_variable const char s_trackModWallModel[] = "wallstone_test";
global_variable const char s_trackModWallInstName[] = "wallstone#0";

global_variable int s_trackModActive;
global_variable int s_trackModLevelID;
global_variable struct Model *s_wallModel;
global_variable struct Instance *s_wallInst;
global_variable int s_wallState;
global_variable int s_wallTimerMS;

// --- THE COUNTING ----------------------------------------------------------
//
// The same rule as for the bounds check in VehLap.c: a branch that is never
// executed is not tested. So we count not only what happens but also
// what does NOT happen - declined tracks, frames in
// pause, frames during loading. An actor path that stays silent on a stock track
// must be able to prove that, not just claim it.
global_variable s64 s_modResets;         // how often Reset was called
global_variable s64 s_modReleases;       // how often Release was called
global_variable s64 s_modDetected;       // of those: track recognised
global_variable s64 s_modDeclined;       // of those: track declined
global_variable s64 s_modResetWhileLive; // Reset hit a scaffold that was still alive
global_variable s64 s_modBirths;         // instances created
global_variable s64 s_modBirthFails;     // births that failed
global_variable s64 s_modPreTicks;       // pre-logic, ran
global_variable s64 s_modPostTicks;      // post-logic, ran
global_variable s64 s_modPausedTicks;    // ticks swallowed by the pause
global_variable s64 s_modLoadingTicks;   // ticks during loading
global_variable s64 s_modIdleTicks;      // ticks without a recognised track
global_variable s64 s_modLevelLeft;      // ticks in which the level had changed under the scaffold

global_variable s64 s_wallStateEnters[WALLSTONE_STATE_COUNT];
global_variable int s_wallDwellMin[WALLSTONE_STATE_COUNT];
global_variable int s_wallDwellMax[WALLSTONE_STATE_COUNT];
global_variable int s_wallPosXMin;
global_variable int s_wallPosXMax;
global_variable int s_wallPosZMin;
global_variable int s_wallPosZMax;
global_variable int s_wallHavePos;
global_variable int s_wallStepMaxMS;
global_variable s64 s_wallSounds;
global_variable s64 s_wallPushes;
global_variable s64 s_wallPushesBot;

global_variable int s_modWasPaused;
global_variable s64 s_modPauseSpans;
global_variable s64 s_modPauseSpanTicks;
global_variable s64 s_modPauseLongestTicks;
global_variable s64 s_modPauseWallMoved; // frames in pause in which the door still stood somewhere else
global_variable int s_modPauseEnterState;
global_variable int s_modPauseEnterTimer;
global_variable int s_modPauseEnterPosZ;
global_variable int s_modResumeStepMS;

global_variable int s_modReportArmed;

// The live instances with our name, from the same list the renderer takes
// its own from (MainFrame_RenderFrame.c takes
// gGT->JitPools.instance.taken.first). A counter of our own would count what
// we BELIEVE we create; this list counts what is really there.
internal int TrackMod_CountLiveInstances(const char *name)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Item *item;
	int found = 0;

	if (gGT == NULL)
	{
		return 0;
	}

	for (item = (struct Item *)LIST_GetFirstItem(&gGT->JitPools.instance.taken); item != NULL; item = (struct Item *)LIST_GetNextItem(item))
	{
		const struct Instance *inst = (const struct Instance *)item;

		if (strncmp(inst->name, name, sizeof(inst->name)) == 0)
		{
			found++;
		}
	}

	return found;
}

internal int TrackMod_CountLiveWallInstances(void)
{
	return TrackMod_CountLiveInstances(s_trackModWallInstName);
}

// Forward-declared because the names of the platform instances only come
// further down - but the report above already needs the number.
internal int TrackMod_CountLivePlatInstances(void);

// The platform and the bat parts of the report are further down, next to
// their actors.
internal void TrackMod_PlatReport(void);
internal void TrackMod_BatReport(void);
internal void TrackMod_FlameReport(void);
internal void TrackMod_SurfReport(void);
internal void TrackMod_LastThreeReport(void);
void NativeTrackMod_BudgetStatic(void);

// From game/native_budget.c, which comes AFTER this file in game_unity.h.
// Four clocks, summing only: 0 pre-logic, 1 post-logic without crates,
// 2 crate loop, 3 pass through the visibility field.
void NativeBudget_ScopeBegin(int scope);
void NativeBudget_ScopeEnd(int scope);

internal void TrackMod_Report(void)
{
	local_persist const char *const names[WALLSTONE_STATE_COUNT] = {"hold A", "move A->B", "hold B", "move B->A"};
	local_persist const int targets[WALLSTONE_STATE_COUNT] = {WALLSTONE_HOLD1_MS, WALLSTONE_MOVE_MS, WALLSTONE_HOLD2_MS, WALLSTONE_MOVE_MS};
	int state;

	Platform_Log("[CTR TrackMod] at exit: %lld reset(s), %lld release(s) - %lld track(s) recognised, %lld declined, %lld reset(s) hit a live "
	             "scaffold\n",
	             s_modResets, s_modReleases, s_modDetected, s_modDeclined, s_modResetWhileLive);
	Platform_Log("[CTR TrackMod] at exit: %lld instance(s) born, %lld birth(s) failed, %d alive now\n", s_modBirths, s_modBirthFails,
	             TrackMod_CountLiveWallInstances());
	Platform_Log("[CTR TrackMod] at exit: %lld pre-logic tick(s), %lld post-logic tick(s); %lld skipped for pause, %lld for loading, %lld with no "
	             "track of ours, %lld after the level changed under the scaffold\n",
	             s_modPreTicks, s_modPostTicks, s_modPausedTicks, s_modLoadingTicks, s_modIdleTicks, s_modLevelLeft);

	Platform_Log("[CTR Wall] at exit: state dwell in ms, measured, target in brackets\n");
	for (state = 0; state < WALLSTONE_STATE_COUNT; state++)
	{
		Platform_Log("[CTR Wall]   %-9s entered %lld time(s), dwell %d..%d ms [%d]\n", names[state], s_wallStateEnters[state],
		             (s_wallStateEnters[state] > 1) ? s_wallDwellMin[state] : -1, (s_wallStateEnters[state] > 1) ? s_wallDwellMax[state] : -1,
		             targets[state]);
	}

	Platform_Log("[CTR Wall] at exit: translation x %d..%d [%d..%d], z %d..%d [%d..%d]\n", s_wallHavePos ? s_wallPosXMin : 0,
	             s_wallHavePos ? s_wallPosXMax : 0, WALLSTONE_POS1_X, WALLSTONE_POS2_X, s_wallHavePos ? s_wallPosZMin : 0,
	             s_wallHavePos ? s_wallPosZMax : 0, WALLSTONE_POS1_Z, WALLSTONE_POS2_Z);
	Platform_Log("[CTR Wall] at exit: %lld sound(s) at 0x%02x, %lld driver push(es) (%lld from bots), largest frame step %d ms\n", s_wallSounds,
	             (unsigned)WALLSTONE_SOUND_ID, s_wallPushes, s_wallPushesBot, s_wallStepMaxMS);
	Platform_Log("[CTR Wall] at exit: %lld pause span(s), %lld tick(s) in the last one, longest %lld tick(s); the door moved during pause %lld "
	             "time(s); largest first step after a resume %d ms\n",
	             s_modPauseSpans, s_modPauseSpanTicks, s_modPauseLongestTicks, s_modPauseWallMoved, s_modResumeStepMS);

	TrackMod_PlatReport();
	TrackMod_BatReport();
	TrackMod_FlameReport();
	TrackMod_SurfReport();
	TrackMod_LastThreeReport();
	NativeTrackMod_BudgetStatic();
}

internal void TrackMod_ArmReport(void)
{
	if (!s_modReportArmed)
	{
		s_modReportArmed = 1;
		Platform_AtExitReport(TrackMod_Report);
	}
}

// ---------------------------------------------------------------------------
// TRACK DETECTION.
//
// Two questions, both must say yes.
//
//   1. Is this load one from a container at all?
//      NativeTrack_ActiveForLevel. That is the question the load path and
//      the sound path ask too - not the host slot. Whoever checks for "level 0"
//      hits Dingo Canyon as well.
//
//   2. Does the loaded LEV carry the model this actor needs?
//      "wallstone_test". No disc level has it, and of 15 containers tested
//      exactly the two Sunset Vista versions have it (13 without, 2 with).
//
// WHAT THIS DOES NOT RELY ON, AND WHY NOT:
//
//   * On a fingerprint. An earlier version took one from the container
//     signature; it went away together with the signature.
//   * On the name from META. That is the author's promise and nothing more -
//     two containers may carry the same one today.
//   * On the LEVD hash from the directory. It is stored in the container and checked
//     on reading (rldtrack.inc), but it does not survive loading:
//     Rld_Close clears the reader away (native_assets.c), and nothing copies
//     the 32 bytes out. It would have to be retrofitted - and even then it would
//     be the wrong measure here, because it changes with EVERY repack. The
//     two Sunset Vista containers at hand already have two different ones:
//     838e59a0... and c4aed959... A table of known hashes would have to be
//     updated for every new version from the author.
//
// THE WEAKNESS OF THIS, written down openly: the model name is not an
// identity but a capability. Whoever builds a track of their own and puts a
// model "wallstone_test" into it gets this door - at the coordinates
// of Sunset Vista, where their track probably has nothing. That is chosen
// deliberately: the question the actor really has to ask is "can I run
// here", and the model name answers that correctly. The question "am I on
// exactly this track" has no reliable answer today, and inventing one
// would be a separate piece of work.
// ---------------------------------------------------------------------------

internal struct Model *TrackMod_FindModel(const char *name)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	u32 i;

	if (gGT == NULL)
	{
		return NULL;
	}

	level = gGT->level1;
	if ((level == NULL) || (level->ptrModelsPtrArray == NULL))
	{
		return NULL;
	}

	for (i = 0; i < level->numModels; i++)
	{
		struct Model *model = level->ptrModelsPtrArray[i];

		if ((model != NULL) && (strncmp(model->name, name, MODEL_NAME_BYTE_COUNT) == 0))
		{
			return model;
		}
	}

	return NULL;
}

internal int TrackMod_Lerp(int from, int to, int numerator, int denominator)
{
	if (numerator < 0)
	{
		numerator = 0;
	}
	if (numerator > denominator)
	{
		numerator = denominator;
	}

	return from + (((to - from) * numerator) / denominator);
}

internal void TrackMod_WallSetProgress(int numerator, int denominator)
{
	if (s_wallInst == NULL)
	{
		return;
	}

	s_wallInst->matrix.t[0] = TrackMod_Lerp(WALLSTONE_POS1_X, WALLSTONE_POS2_X, numerator, denominator);
	s_wallInst->matrix.t[1] = TrackMod_Lerp(WALLSTONE_POS1_Y, WALLSTONE_POS2_Y, numerator, denominator);
	s_wallInst->matrix.t[2] = TrackMod_Lerp(WALLSTONE_POS1_Z, WALLSTONE_POS2_Z, numerator, denominator);

	if (!s_wallHavePos)
	{
		s_wallHavePos = 1;
		s_wallPosXMin = s_wallInst->matrix.t[0];
		s_wallPosXMax = s_wallInst->matrix.t[0];
		s_wallPosZMin = s_wallInst->matrix.t[2];
		s_wallPosZMax = s_wallInst->matrix.t[2];
	}

	if (s_wallInst->matrix.t[0] < s_wallPosXMin)
	{
		s_wallPosXMin = s_wallInst->matrix.t[0];
	}
	if (s_wallInst->matrix.t[0] > s_wallPosXMax)
	{
		s_wallPosXMax = s_wallInst->matrix.t[0];
	}
	if (s_wallInst->matrix.t[2] < s_wallPosZMin)
	{
		s_wallPosZMin = s_wallInst->matrix.t[2];
	}
	if (s_wallInst->matrix.t[2] > s_wallPosZMax)
	{
		s_wallPosZMax = s_wallInst->matrix.t[2];
	}
}

internal void TrackMod_WallPlaySound(void)
{
	if (s_wallInst != NULL)
	{
		s_wallSounds++;
		PlaySound3D(WALLSTONE_SOUND_ID, s_wallInst);
	}
}

// THE DRIVER IS PUSHED TO THE SIDE HE CAME FROM.
//
// The slab is thin in X and moves in Z. Whoever touches it is put out in X,
// namely on the side where he stood in the PREVIOUS frame -
// not on the nearest one. Otherwise a door that is just closing throws
// the driver around a Z end face to the other side.
internal void TrackMod_WallPushDriver(struct Driver *driver)
{
	int x;
	int y;
	int z;
	int prevX;
	int xMin;
	int xMax;
	int yMin;
	int yMax;
	int zMin;
	int zMax;
	int pushValue;

	if ((driver == NULL) || (driver->instSelf == NULL) || (s_wallInst == NULL))
	{
		return;
	}

	x = driver->posCurr.x >> 8;
	y = driver->posCurr.y >> 8;
	z = driver->posCurr.z >> 8;
	prevX = driver->posPrev.x >> 8;

	xMin = s_wallInst->matrix.t[0] + WALLSTONE_LOCAL_MIN_X - WALLSTONE_DRIVER_RADIUS;
	xMax = s_wallInst->matrix.t[0] + WALLSTONE_LOCAL_MAX_X + WALLSTONE_DRIVER_RADIUS;
	yMin = s_wallInst->matrix.t[1] + WALLSTONE_LOCAL_MIN_Y - WALLSTONE_DRIVER_RADIUS;
	yMax = s_wallInst->matrix.t[1] + WALLSTONE_LOCAL_MAX_Y + WALLSTONE_DRIVER_RADIUS;
	zMin = s_wallInst->matrix.t[2] + WALLSTONE_LOCAL_MIN_Z - WALLSTONE_DRIVER_RADIUS;
	zMax = s_wallInst->matrix.t[2] + WALLSTONE_LOCAL_MAX_Z + WALLSTONE_DRIVER_RADIUS;

	if ((y < yMin) || (y > yMax) || (z < zMin) || (z > zMax) || ((x < xMin) && (prevX < xMin)) || ((x > xMax) && (prevX > xMax)))
	{
		return;
	}

	pushValue = (prevX <= ((xMin + xMax) / 2)) ? (xMin - 1) : (xMax + 1);

	driver->posCurr.x = pushValue * 0x100;
	driver->posPrev.x = driver->posCurr.x;
	driver->instSelf->matrix.t[0] = pushValue;
	driver->velocity.x = 0;
	driver->xSpeed = 0;

	s_wallPushes++;

	// A BOT NEEDS MORE THAN ONE PUSH.
	//
	// Its position is recomputed from its progress on the navigation line;
	// the visible push would be gone again in the next frame, and it would
	// drive through the slab. So the progress is halted.
	//
	// THE FIELD NAMES ARE THOSE OF THIS TREE, not those of the PS1 source. There
	// they appear as botData.unk5bc.ai_speedLinear; here they are called
	// botData.aiPhysics.speedLinear (namespace_Vehicle.h, struct BotPhysics)
	// and botData.ai_progress_cooldown. Same place, name as read.
	if ((driver->actionsFlagSet & ACTION_BOT) != 0)
	{
		s_wallPushesBot++;
		driver->botData.aiPhysics.speedLinear = 0;
		driver->botData.ai_progress_cooldown = 2;
	}

	driver->actionsFlagSet |= ACTION_DRIVING_AGAINST_WALL;
}

internal void TrackMod_WallResolveCollision(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int driverIndex;

	for (driverIndex = 0; driverIndex < 8; driverIndex++)
	{
		TrackMod_WallPushDriver(gGT->drivers[driverIndex]);
	}
}

internal void TrackMod_WallEnterState(int state)
{
	// The dwell time that just ended. On the VERY FIRST entry into
	// a state there is none - the initial state is drawn at random and starts
	// in the middle. That is why the report measures each state only from the
	// second entry on.
	if (s_wallStateEnters[s_wallState] == 1)
	{
		s_wallDwellMin[s_wallState] = s_wallTimerMS;
		s_wallDwellMax[s_wallState] = s_wallTimerMS;
	}
	else if (s_wallStateEnters[s_wallState] > 1)
	{
		if (s_wallTimerMS < s_wallDwellMin[s_wallState])
		{
			s_wallDwellMin[s_wallState] = s_wallTimerMS;
		}
		if (s_wallTimerMS > s_wallDwellMax[s_wallState])
		{
			s_wallDwellMax[s_wallState] = s_wallTimerMS;
		}
	}

	s_wallState = state;
	s_wallTimerMS = 0;
	s_wallStateEnters[state]++;
}

internal void TrackMod_WallUpdate(int elapsedMS)
{
	if ((s_wallModel == NULL) || (s_wallInst == NULL))
	{
		return;
	}

	if (elapsedMS < 0)
	{
		elapsedMS = 0;
	}
	if (elapsedMS > WALLSTONE_STEP_MAX_MS)
	{
		elapsedMS = WALLSTONE_STEP_MAX_MS;
	}
	if (elapsedMS > s_wallStepMaxMS)
	{
		s_wallStepMaxMS = elapsedMS;
	}

	s_wallTimerMS += elapsedMS;

	switch (s_wallState)
	{
	case WALLSTONE_AT_POS1:
		TrackMod_WallSetProgress(0, 1);
		if (s_wallTimerMS >= WALLSTONE_HOLD1_MS)
		{
			TrackMod_WallEnterState(WALLSTONE_MOVING_TO_POS2);
			TrackMod_WallPlaySound();
		}
		break;

	case WALLSTONE_MOVING_TO_POS2:
		TrackMod_WallSetProgress(s_wallTimerMS, WALLSTONE_MOVE_MS);
		if (s_wallTimerMS >= WALLSTONE_MOVE_MS)
		{
			TrackMod_WallEnterState(WALLSTONE_AT_POS2);
			TrackMod_WallSetProgress(1, 1);
		}
		break;

	case WALLSTONE_AT_POS2:
		TrackMod_WallSetProgress(1, 1);
		if (s_wallTimerMS >= WALLSTONE_HOLD2_MS)
		{
			TrackMod_WallEnterState(WALLSTONE_MOVING_TO_POS1);
			TrackMod_WallPlaySound();
		}
		break;

	case WALLSTONE_MOVING_TO_POS1:
		TrackMod_WallSetProgress(WALLSTONE_MOVE_MS - s_wallTimerMS, WALLSTONE_MOVE_MS);
		if (s_wallTimerMS >= WALLSTONE_MOVE_MS)
		{
			TrackMod_WallEnterState(WALLSTONE_AT_POS1);
			TrackMod_WallSetProgress(0, 1);
		}
		break;

	default:
		break;
	}

	TrackMod_WallResolveCollision();
}

// Measured: door and platforms behave the same in the funnel (both ~85 % cull,
// both write, same writer, same texture word) - and only the door
// is in the picture. What remains is the model itself: scale, flags and the
// frame origin. A model built around its origin has the
// origin near zero; one in world coordinates carries it at the world position,
// and the instance position then comes on top - drawn, but far away.
internal void TrackMod_LogModelShape(const struct Model *model, const char *who)
{
	const struct ModelHeader *mh;

	if ((model == NULL) || (model->headers == NULL) || (model->numHeaders <= 0))
	{
		Platform_Log("[CTR Plat] model shape (%s): no headers\n", who);
		return;
	}

	mh = &model->headers[0];
	if (mh->ptrFrameData == NULL)
	{
		Platform_Log("[CTR Plat] model shape (%s): '%s' %d header(s), flags 0x%x, lod %d, scale %d/%d/%d, ANIMATED (no static frame)\n", who,
		             mh->name, model->numHeaders, mh->flags, mh->maxDistanceLOD, mh->scale.x, mh->scale.y, mh->scale.z);
		return;
	}

	Platform_Log("[CTR Plat] model shape (%s): '%s' %d header(s), flags 0x%x, lod %d, scale %d/%d/%d, frame origin %d/%d/%d\n", who, mh->name,
	             model->numHeaders, mh->flags, mh->maxDistanceLOD, mh->scale.x, mh->scale.y, mh->scale.z, mh->ptrFrameData->pos.x,
	             mh->ptrFrameData->pos.y, mh->ptrFrameData->pos.z);
}

internal void TrackMod_WallBirth(void)
{
	SVec3 rotation;

	s_wallModel = TrackMod_FindModel(s_trackModWallModel);
	if (s_wallModel == NULL)
	{
		return;
	}
	TrackMod_LogModelShape(s_wallModel, "door");

	s_wallInst = INSTANCE_Birth3D(s_wallModel, s_trackModWallInstName, NULL);
	if (s_wallInst == NULL)
	{
		s_modBirthFails++;
		Platform_LogWarn("[CTR Wall] no instance - the pool is full, the door stays away this time\n");
		return;
	}

	s_modBirths++;

	rotation.x = 0;
	rotation.y = 0;
	rotation.z = 0;
	ConvertRotToMatrix(&s_wallInst->matrix, &rotation);

	s_wallInst->scale.x = WALLSTONE_SCALE;
	s_wallInst->scale.y = WALLSTONE_SCALE;
	s_wallInst->scale.z = WALLSTONE_SCALE;

	// The two end states are 0 and 2 - the door starts on one side or
	// the other, with equal probability. That is how the source has it,
	// and it is the reason why the report does not count the first dwell time
	// per state.
	s_wallState = MixRNG_Scramble() & WALLSTONE_AT_POS2;
	s_wallTimerMS = 0;
	s_wallStateEnters[s_wallState]++;
	TrackMod_WallSetProgress(s_wallState, WALLSTONE_AT_POS2);
}

internal void TrackMod_WallForget(void)
{
	s_wallModel = NULL;
	s_wallInst = NULL;
	s_wallState = WALLSTONE_AT_POS1;
	s_wallTimerMS = 0;
}

// ---------------------------------------------------------------------------
// THE THREE MOVING PLATFORMS.
//
// Source: the track's source (ps1_trackrom/src/platform_sunset.c), numbers from
// the track's authoring reference (platform_layout_data.h). Model "sunset_plat",
// three instances "platform#0..2", scale 0x5000, four states
// 1800 / 1600 / 2500 / 1600 ms, where the two moving states are made up of
// 700 ms of travel and 450 ms of stagger per platform.
//
// WHAT IS DIFFERENT HERE FROM THE DOOR. The door is a pure model: it
// pushes drivers away itself, and the LEV knows nothing about it. The platforms
// are TWO things that have to be kept congruent:
//
//   1. One model per platform. That is what you see.
//   2. One pre-baked collision frame per position - 41 quadblocks per
//      platform, 123 in total, fixed in the LEV, each at its place. That is
//      what you stand on.
//
// At runtime NO geometry is moved. The only thing switched is which
// of the 41 frames currently carries - one write to quadFlags, nothing else.
// No vertices, no BSP bounds, no driver state. That is what the track's
// integration notes (INTEGRATION.md section 2) require, and what the source does.
//
// THE GRID IS SHARED. Both - model and frame - are set from THE SAME
// rounded index:
//
//     frame = round(clamp(progress, 0, 700) * 40 / 700)
//
// So the model does not take the smooth progress but the same step
// as the collision. It therefore jerks in 41 steps over 1280 units, i.e.
// 32 units per step. That is intended: a smoothly moving model over a
// grid-stepped collision would be exactly the bug you notice while driving as
// "I am standing next to the platform".
//
// THE FRAMES ARE INVISIBLE, BUT NOT EMPTY. Unlike the 99 faces of the zero
// rule, all 123 carry a real texture assignment (0 of 5 references zeroed,
// for all 123). So the zero rule does not apply here. They are kept
// invisible by route 1: the visibility bit per quadblock, cleared after the
// rebuild of the field. See NativeTrackMod_HideLevelFaces at the end of this file.
// ---------------------------------------------------------------------------

enum
{
	PLATFORM_COUNT = 3,
	PLATFORM_FRAME_COUNT = 41,
	PLATFORM_QUAD_COUNT = PLATFORM_COUNT * PLATFORM_FRAME_COUNT,

	PLATFORM_SCALE = 0x5000,

	PLATFORM_RETRACTED_HOLD_MS = 1800,
	PLATFORM_MOVE_MS = 700,
	PLATFORM_STAGGER_MS = 450,
	PLATFORM_EXTENDED_HOLD_MS = 2500,

	// The same clamp as for the door, for the same reason.
	PLATFORM_STEP_MAX_MS = 250,
};

// THE FLAG VALUES VIA RELOAD'S NAMES.
//
// The source writes 0x4001 and 0x1801 as raw numbers. Here they appear
// as what they mean - otherwise every reader has to lay the bit list from
// namespace_Level.h next to them:
//
//   parked   0x4001 = NO_CAMERA_RESPAWN_PROBE | REFLECT_SPLIT_LINE_1
//            No GROUND, no COLLISION_SURFACE. So the frame falls through
//            every filter of the collision search (COLL.c), because all
//            searchers there want at least one of the two. In addition
//            NO_CAMERA_RESPAWN_PROBE tells the stuck recovery (VehStuckProc.c,
//            which explicitly ignores this bit) that it should not set anyone
//            down here.
//
//   carrying 0x1801 = GROUND | CAMERA_SEARCH | REFLECT_SPLIT_LINE_1
//            GROUND makes it ground - for drivers (COLL.c), for bots
//            (BOTS.c) and for items resting on it (VehPickupItem.c).
//            CAMERA_SEARCH lets the camera find it (CAM.c).
//
// REFLECT_SPLIT_LINE_1 is in BOTH and never changes. It picks the
// diagonal along which the quad splits into two triangles (COLL.c) - a
// property of the geometry, not of the state.
enum
{
	PLATFORM_FLAGS_PARKED = QUADBLOCK_FLAG_NO_CAMERA_RESPAWN_PROBE | QUADBLOCK_FLAG_REFLECT_SPLIT_LINE_1,
	PLATFORM_FLAGS_LIVE = QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_CAMERA_SEARCH | QUADBLOCK_FLAG_REFLECT_SPLIT_LINE_1,
};

// The proof that the names give the same numbers the author baked:
// platform_collision_layout.json names disabled_quad_flags 16385 and
// enabled_quad_flags 6145, and all 123 quadblocks carry 0x4001 in the container.
CTR_STATIC_ASSERT(PLATFORM_FLAGS_PARKED == 16385);
CTR_STATIC_ASSERT(PLATFORM_FLAGS_LIVE == 6145);

enum SunsetPlatformState
{
	PLATFORM_RETRACTED = 0,
	PLATFORM_EXTENDING = 1,
	PLATFORM_EXTENDED = 2,
	PLATFORM_RETRACTING = 3,
	PLATFORM_STATE_COUNT = 4,
};

global_variable const char s_trackModPlatModel[] = "sunset_plat";
global_variable const char s_trackModPlatInstName[PLATFORM_COUNT][MODEL_NAME_BYTE_COUNT] = {
    "platform#0",
    "platform#1",
    "platform#2",
};

// Start and end position per platform, from platform_layout_data.h. The three lie
// side by side in z (640 units apart) and all travel the same 1280
// units in -x.
global_variable const s16 s_platStart[PLATFORM_COUNT][3] = {
    {-6007, 2673, 9584},
    {-6007, 2673, 8944},
    {-6007, 2673, 8304},
};
global_variable const s16 s_platEnd[PLATFORM_COUNT][3] = {
    {-7287, 2673, 9584},
    {-7287, 2673, 8944},
    {-7287, 2673, 8304},
};

// Which quadblock carries which position. From platform_layout_data.h; the same
// list is in the track's platform_collision_layout.json, there with
// names (SUNSET_PLATFORM_COLLISION_P1_F00 ...) and a bounding volume per entry.
// The numbers are not contiguous - between position 25 and 26 every
// platform jumps, because other track faces are in the way there.
global_variable const u16 s_platQuadIndex[PLATFORM_COUNT][PLATFORM_FRAME_COUNT] = {
    {3645, 3646, 3647, 3648, 3649, 3650, 3651, 3652, 3653, 3654, 3655, 3656, 3657, 3658, 3659, 3660, 3661, 3662, 3663, 3664, 3665,
     3666, 3667, 3668, 3669, 3670, 3676, 3677, 3678, 3679, 3680, 3681, 3682, 3683, 3684, 3685, 3686, 3687, 3688, 3689, 3690},
    {3693, 3694, 3695, 3696, 3697, 3698, 3699, 3700, 3701, 3702, 3703, 3704, 3705, 3706, 3707, 3708, 3709, 3710, 3711, 3712, 3713,
     3714, 3715, 3716, 3717, 3718, 3761, 3762, 3763, 3764, 3765, 3766, 3767, 3768, 3769, 3770, 3771, 3772, 3773, 3774, 3775},
    {3719, 3720, 3721, 3722, 3723, 3724, 3725, 3726, 3727, 3728, 3729, 3730, 3731, 3732, 3733, 3734, 3735, 3736, 3737, 3738, 3739,
     3740, 3741, 3742, 3743, 3744, 3776, 3777, 3778, 3779, 3780, 3781, 3782, 3783, 3784, 3785, 3786, 3787, 3788, 3789, 3790},
};

global_variable struct Model *s_platModel;
global_variable struct Instance *s_platInst[PLATFORM_COUNT];
global_variable int s_platFrame[PLATFORM_COUNT]; // -1 = no position set yet
global_variable int s_platState;
global_variable int s_platTimerMS;
global_variable int s_platLive; // the frames have been touched, Reset must restore them

// --- THE COUNTING ----------------------------------------------------------
global_variable s64 s_platBirths;
global_variable s64 s_platBirthFails;
global_variable s64 s_platFlagWrites;    // writes to quadFlags, in total
global_variable s64 s_platTicks;         // ticks in which the platforms computed
global_variable s64 s_platStateEnters[PLATFORM_STATE_COUNT];
global_variable int s_platDwellMin[PLATFORM_STATE_COUNT];
global_variable int s_platDwellMax[PLATFORM_STATE_COUNT];
global_variable int s_platFrameSeenMin[PLATFORM_COUNT];
global_variable int s_platFrameSeenMax[PLATFORM_COUNT];
global_variable int s_platHaveFrameSeen;
global_variable int s_platStepMaxMS;

// --- BEHIND THE QUEUEING -----------------------------------------------------
// The platforms were seen to get queued (DRAW_SUCCESSFUL,
// projected near the centre of the screen) and leave zero pixels. Between
// queueing and draw function RenderBucket_QueueExecute.c has six
// exits that discard without a word; here we count which one it was.
// Only wall and platforms are counted - the bats come from the
// LEV and are demonstrably not affected. Reason 0 means: a
// draw function was reached. If there is a number there and the picture stays empty,
// the cause is in the draw function itself, not before it.
// Because all visits reached the draw handler, the counting also goes one
// level deeper, into DrawFunc_Normal per primitive: 13 DrawFunc_Normal entered (detail: first command word,
// 0xffffffff = empty list), 8 projection rejected (detail: GTE flag), 9
// texture table null, 10 primitive writer not ported (detail:
// funcPtr[1]), 11 primitive memory full, 12 primitive written (detail:
// projected corner 0 as x | y << 16 - tells WHERE the last one lay).
// Further: 14/15/16 split the rejection into GTE flag, NCLIP/cull and
// screen window; 17 OT range null; 18/19 depth slot below/above the
// window depthOffset[0..1] of the instance (the writer does NOT clamp - a
// slot outside lies in foreign OT memory, written, never drawn);
// 20/21 written with/without texture (detail: texture word 1 or colour);
// 22 which primitive writer. Plus the bbox of all written corners 0.
#define TRACKMOD_DRAW_EXIT_COUNT 23
global_variable s64 s_drawExitCount[TRACKMOD_DRAW_EXIT_COUNT];
global_variable u32 s_drawExitDetail[TRACKMOD_DRAW_EXIT_COUNT]; // last detail per reason
global_variable s64 s_drawExitWall;                             // of those, the wandering door
global_variable s64 s_drawExitPlat[PLATFORM_COUNT];             // of those, per platform, all reasons
global_variable int s_drawExitXMin, s_drawExitXMax, s_drawExitYMin, s_drawExitYMax; // bbox of the written corner 0
global_variable int s_drawExitHaveXY;
global_variable s64 s_drawExitWallByReason[TRACKMOD_DRAW_EXIT_COUNT]; // door and platforms kept apart
global_variable s64 s_drawExitPlatByReason[TRACKMOD_DRAW_EXIT_COUNT];

// The measure "exactly one of the 41 positions active". It is measured TWICE per tick:
// once at the start (catches it when someone else touches the flags) and
// once after the switch (catches it when this module itself is wrong).
global_variable s64 s_platAudits;
global_variable s64 s_platAuditBadCount;   // platform did not have exactly one carrying position
global_variable s64 s_platAuditBadFrame;   // the carrying position was not the recorded one
global_variable s64 s_platAuditBadFlags;   // a frame carried neither 0x4001 nor 0x1801
global_variable int s_platAuditWorstCount; // largest observed number of carrying positions per platform

// The measure "model and frame coincide". Compared are the position the
// model carries against the position the collision switch recorded, and
// the model position against the one that follows from that position.
global_variable s64 s_platPoseChecks;
global_variable s64 s_platPoseMismatch;
global_variable int s_platPoseWorstDelta;

// The measure "invisible". See NativeTrackMod_HideLevelFaces.
global_variable s64 s_platHideCalls;        // calls in total
global_variable s64 s_platHideRebuiltFrames; // of those: the field was rebuilt in this frame
global_variable s64 s_platHideStaleFrames;   // of those: it was NOT rebuilt
global_variable s64 s_platHidePasses;        // player passes
global_variable s64 s_platBitsCleared;       // bits that were set and got cleared
global_variable s64 s_platBitsClearedStale;  // of those, in a frame without rebuild
global_variable s64 s_platBitsAlreadyClear;  // bits that were already clear
global_variable s64 s_platBitsSet;           // only with --show-platform-frames: bits set

// THE CROSS-CHECK FOR THE VISIBILITY FIELD.
//
// A counter reporting "0 bits cleared" says two things on its own
// and you do not know which: either the bits were already clear - or
// the field is empty, and then the whole measurement would be worthless. That is why
// the same pass also reads the bit of the face the driver is currently
// standing on. It must be set; if it is, the field is populated and the
// zero for the platform frames really means "not in the visibility list".
global_variable s64 s_platCtrlChecks;
global_variable s64 s_platCtrlSet;

// FOR MEASURING ONLY. If it is 1, the 123 bits are SET instead of cleared.
// That shows what the draw path would do with the frames if they were in
// the visibility list - and only that proves that the clearing decides
// anything at all. Default 0; it is set from main.c
// (--show-platform-frames).
int g_cfg_showPlatformFrames = 0;

// Pause.
global_variable int s_platPauseFrame[PLATFORM_COUNT];
global_variable int s_platPausePosX[PLATFORM_COUNT];
global_variable int s_platPauseTimer;
global_variable int s_platPauseState;
global_variable int s_platWasPaused;
global_variable s64 s_platPauseTicks;
global_variable s64 s_platPauseSpanTicks;
global_variable s64 s_platPauseMoved; // frames in pause in which something moved anyway
global_variable int s_platResumeStepMS;

internal struct mesh_info *TrackMod_Mesh(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return NULL;
	}

	return gGT->level1->ptr_mesh_info;
}

// round(clamp(p, 0, 700) * 40 / 700), in integers. Half the denominator is
// added before the division; that is the rounding, and the source has it this way.
internal int TrackMod_PlatFrameFromProgress(int progressMS)
{
	if (progressMS < 0)
	{
		progressMS = 0;
	}
	if (progressMS > PLATFORM_MOVE_MS)
	{
		progressMS = PLATFORM_MOVE_MS;
	}

	return ((progressMS * (PLATFORM_FRAME_COUNT - 1)) + (PLATFORM_MOVE_MS / 2)) / PLATFORM_MOVE_MS;
}

internal int TrackMod_PlatCoordinate(int index, int axis, int frame)
{
	int from = s_platStart[index][axis];
	int to = s_platEnd[index][axis];

	return from + (((to - from) * frame) / (PLATFORM_FRAME_COUNT - 1));
}

// The only write to quadFlags in this module. It switches the
// old position off and the new one on - never more than two quadblocks per call, and
// only when the position has really changed.
internal void TrackMod_PlatSetCollisionFrame(int index, int frame)
{
	struct mesh_info *mesh = TrackMod_Mesh();
	int previous = s_platFrame[index];

	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL) || (previous == frame))
	{
		return;
	}

	if ((previous >= 0) && (previous < PLATFORM_FRAME_COUNT))
	{
		mesh->ptrQuadBlockArray[s_platQuadIndex[index][previous]].quadFlags = PLATFORM_FLAGS_PARKED;
		s_platFlagWrites++;
	}

	mesh->ptrQuadBlockArray[s_platQuadIndex[index][frame]].quadFlags = PLATFORM_FLAGS_LIVE;
	s_platFlagWrites++;
	s_platFrame[index] = frame;

	if (!s_platHaveFrameSeen)
	{
		int i;

		s_platHaveFrameSeen = 1;
		for (i = 0; i < PLATFORM_COUNT; i++)
		{
			s_platFrameSeenMin[i] = frame;
			s_platFrameSeenMax[i] = frame;
		}
	}
	if (frame < s_platFrameSeenMin[index])
	{
		s_platFrameSeenMin[index] = frame;
	}
	if (frame > s_platFrameSeenMax[index])
	{
		s_platFrameSeenMax[index] = frame;
	}
}

// All 123 to parked. Called at setup and on every Reset - even though the
// container already ships them that way (measured: all 123 carry 0x4001 in the
// container). The case where it matters is the restart: there they stand the way
// the last run left them.
internal int TrackMod_PlatParkAll(void)
{
	struct mesh_info *mesh = TrackMod_Mesh();
	int index;
	int frame;

	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL))
	{
		return 0;
	}

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		for (frame = 0; frame < PLATFORM_FRAME_COUNT; frame++)
		{
			if ((int)s_platQuadIndex[index][frame] >= mesh->numQuadBlock)
			{
				return 0;
			}
		}
	}

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		for (frame = 0; frame < PLATFORM_FRAME_COUNT; frame++)
		{
			mesh->ptrQuadBlockArray[s_platQuadIndex[index][frame]].quadFlags = PLATFORM_FLAGS_PARKED;
			s_platFlagWrites++;
		}
		s_platFrame[index] = -1;
	}

	return 1;
}

// How many frames are carrying right now, and which one. Needed for the counting
// and for the proof that a restart really resets them.
internal int TrackMod_PlatCountLive(int index, int *firstLiveFrame, int *strayFlags)
{
	struct mesh_info *mesh = TrackMod_Mesh();
	int frame;
	int live = 0;

	*firstLiveFrame = -1;
	*strayFlags = 0;

	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL))
	{
		return 0;
	}

	for (frame = 0; frame < PLATFORM_FRAME_COUNT; frame++)
	{
		u16 flags;

		// The range check is here and not only in TrackMod_PlatParkAll,
		// because this counter also runs BEFORE setup - the census
		// on Reset asks what the last run left behind.
		if ((int)s_platQuadIndex[index][frame] >= mesh->numQuadBlock)
		{
			continue;
		}

		flags = mesh->ptrQuadBlockArray[s_platQuadIndex[index][frame]].quadFlags;

		if (flags == PLATFORM_FLAGS_LIVE)
		{
			if (*firstLiveFrame < 0)
			{
				*firstLiveFrame = frame;
			}
			live++;
		}
		else if (flags != PLATFORM_FLAGS_PARKED)
		{
			(*strayFlags)++;
		}
	}

	return live;
}

// THE MEASURE, RECOUNTED ONE BY ONE.
//
// Not "I switched correctly" but "the LEV holds what it should
// hold". The difference is exactly the case one otherwise overlooks: if
// any other part of the game writes quadFlags, it only shows up here.
internal void TrackMod_PlatAudit(void)
{
	int index;

	if (!s_platLive)
	{
		return;
	}

	s_platAudits++;

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		int firstLive = -1;
		int stray = 0;
		int live = TrackMod_PlatCountLive(index, &firstLive, &stray);

		if (live > s_platAuditWorstCount)
		{
			s_platAuditWorstCount = live;
		}
		if (live != 1)
		{
			s_platAuditBadCount++;
		}
		if (firstLive != s_platFrame[index])
		{
			s_platAuditBadFrame++;
		}
		if (stray != 0)
		{
			s_platAuditBadFlags += stray;
		}
	}
}

// DO MODEL AND FRAME COINCIDE.
//
// The comparison is not "roughly at the same place" but the position: the model
// must stand exactly where the recorded collision position requires. Because
// both are set from the same rounded index, the allowed
// deviation is zero.
internal void TrackMod_PlatPoseCheck(void)
{
	int index;

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		struct Instance *inst = s_platInst[index];
		int frame = s_platFrame[index];
		int axis;

		if ((inst == NULL) || (frame < 0))
		{
			continue;
		}

		s_platPoseChecks++;

		for (axis = 0; axis < 3; axis++)
		{
			int want = TrackMod_PlatCoordinate(index, axis, frame);
			int have = inst->matrix.t[axis];
			int delta = (have > want) ? (have - want) : (want - have);

			if (delta != 0)
			{
				s_platPoseMismatch++;
				if (delta > s_platPoseWorstDelta)
				{
					s_platPoseWorstDelta = delta;
				}
			}
		}
	}
}

internal void TrackMod_PlatSetProgress(int index, int progressMS)
{
	struct Instance *inst = s_platInst[index];
	int frame = TrackMod_PlatFrameFromProgress(progressMS);

	// First the collision, then the model - and both from the same frame.
	TrackMod_PlatSetCollisionFrame(index, frame);

	if (inst == NULL)
	{
		return;
	}

	inst->matrix.t[0] = TrackMod_PlatCoordinate(index, 0, frame);
	inst->matrix.t[1] = TrackMod_PlatCoordinate(index, 1, frame);
	inst->matrix.t[2] = TrackMod_PlatCoordinate(index, 2, frame);
}

internal void TrackMod_PlatEnterState(int state)
{
	int previous = s_platState;

	if (s_platStateEnters[previous] > 0)
	{
		if ((s_platStateEnters[previous] == 1) || (s_platTimerMS < s_platDwellMin[previous]))
		{
			s_platDwellMin[previous] = s_platTimerMS;
		}
		if ((s_platStateEnters[previous] == 1) || (s_platTimerMS > s_platDwellMax[previous]))
		{
			s_platDwellMax[previous] = s_platTimerMS;
		}
	}

	s_platState = state;
	s_platTimerMS = 0;
	s_platStateEnters[state]++;
}

internal void TrackMod_PlatUpdate(int elapsedMS)
{
	int movementMS = (PLATFORM_COUNT - 1) * PLATFORM_STAGGER_MS + PLATFORM_MOVE_MS;
	int index;

	if (s_platModel == NULL)
	{
		return;
	}

	if (elapsedMS < 0)
	{
		elapsedMS = 0;
	}
	if (elapsedMS > PLATFORM_STEP_MAX_MS)
	{
		elapsedMS = PLATFORM_STEP_MAX_MS;
	}
	if (elapsedMS > s_platStepMaxMS)
	{
		s_platStepMaxMS = elapsedMS;
	}

	s_platTimerMS += elapsedMS;
	s_platTicks++;

	switch (s_platState)
	{
	case PLATFORM_RETRACTED:
		for (index = 0; index < PLATFORM_COUNT; index++)
		{
			TrackMod_PlatSetProgress(index, 0);
		}
		if (s_platTimerMS >= PLATFORM_RETRACTED_HOLD_MS)
		{
			TrackMod_PlatEnterState(PLATFORM_EXTENDING);
		}
		break;

	case PLATFORM_EXTENDING:
		// The three start staggered, 450 ms apart. Platform 0 first.
		for (index = 0; index < PLATFORM_COUNT; index++)
		{
			TrackMod_PlatSetProgress(index, s_platTimerMS - (index * PLATFORM_STAGGER_MS));
		}
		if (s_platTimerMS >= movementMS)
		{
			TrackMod_PlatEnterState(PLATFORM_EXTENDED);
		}
		break;

	case PLATFORM_EXTENDED:
		for (index = 0; index < PLATFORM_COUNT; index++)
		{
			TrackMod_PlatSetProgress(index, PLATFORM_MOVE_MS);
		}
		if (s_platTimerMS >= PLATFORM_EXTENDED_HOLD_MS)
		{
			TrackMod_PlatEnterState(PLATFORM_RETRACTING);
		}
		break;

	case PLATFORM_RETRACTING:
		// Back in reverse order: platform 2 first.
		for (index = 0; index < PLATFORM_COUNT; index++)
		{
			int delay = (PLATFORM_COUNT - 1 - index) * PLATFORM_STAGGER_MS;

			TrackMod_PlatSetProgress(index, PLATFORM_MOVE_MS - (s_platTimerMS - delay));
		}
		if (s_platTimerMS >= movementMS)
		{
			TrackMod_PlatEnterState(PLATFORM_RETRACTED);
		}
		break;

	default:
		break;
	}

	TrackMod_PlatPoseCheck();
}

// Forward-declared: the census comes below the setup, because that is
// where it belongs by content - but the setup already needs it.
internal void TrackMod_PlatLogCensus(const char *when);

internal void TrackMod_PlatBirth(void)
{
	SVec3 rotation;
	int index;

	s_platModel = TrackMod_FindModel(s_trackModPlatModel);
	if (s_platModel == NULL)
	{
		return;
	}
	TrackMod_LogModelShape(s_platModel, "platform");

	if (!TrackMod_PlatParkAll())
	{
		Platform_LogWarn("[CTR Plat] the collision frames do not fit this level - the platforms stay away\n");
		s_platModel = NULL;
		return;
	}

	// The intermediate state, recorded on purpose: here all 123 are parked, not a
	// single one carries. Only the setup below switches one on again per
	// platform. Without this line the promise "after a restart all 123 are
	// reset" could only be inferred from the two other lines.
	s_platLive = 1;
	TrackMod_PlatLogCensus("after park ");

	s_platState = PLATFORM_RETRACTED;
	s_platTimerMS = 0;
	s_platStateEnters[PLATFORM_RETRACTED]++;

	rotation.x = 0;
	rotation.y = 0;
	rotation.z = 0;

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		s_platInst[index] = INSTANCE_Birth3D(s_platModel, s_trackModPlatInstName[index], NULL);
		if (s_platInst[index] == NULL)
		{
			s_platBirthFails++;
			Platform_LogWarn("[CTR Plat] no instance for platform %d - the pool is full\n", index);
			continue;
		}

		s_platBirths++;
		ConvertRotToMatrix(&s_platInst[index]->matrix, &rotation);
		s_platInst[index]->scale.x = PLATFORM_SCALE;
		s_platInst[index]->scale.y = PLATFORM_SCALE;
		s_platInst[index]->scale.z = PLATFORM_SCALE;
	}

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		TrackMod_PlatSetProgress(index, 0);
	}

}

// What a Reset finds before it restores. The proof for the measure
// "after Retry all 123 are parked again" - it is only worth something if
// it also shows that things were different before.
internal int TrackMod_CountLivePlatInstances(void)
{
	int index;
	int found = 0;

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		found += TrackMod_CountLiveInstances(s_trackModPlatInstName[index]);
	}

	return found;
}

internal void TrackMod_PlatLogCensus(const char *when)
{
	int index;
	int live[PLATFORM_COUNT];
	int frame[PLATFORM_COUNT];
	int stray = 0;

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		int first = -1;
		int oddments = 0;

		live[index] = TrackMod_PlatCountLive(index, &first, &oddments);
		frame[index] = first;
		stray += oddments;
	}

	Platform_Log("[CTR Plat] %s: platform 0 %d live (frame %d), 1 %d live (frame %d), 2 %d live (frame %d), %d frame(s) with neither flag word\n", when,
	             live[0], frame[0], live[1], frame[1], live[2], frame[2], stray);
}

// Restore the frames, forget the pointers. Kept apart, because the restart
// needs both and the level change may only do the second: after a
// MEMPACK_PopState ptrQuadBlockArray points into foreign ground.
internal void TrackMod_PlatForget(void)
{
	int index;

	s_platModel = NULL;
	s_platLive = 0;
	s_platState = PLATFORM_RETRACTED;
	s_platTimerMS = 0;
	s_platWasPaused = 0;

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		s_platInst[index] = NULL;
		s_platFrame[index] = -1;
	}
}

// ---------------------------------------------------------------------------
// THE TWELVE BATS.
//
// Source: the track's source (ps1_trackrom/src/sunset_bat_native.c and
// sunset_bat_audio.c), numbers confirmed in the track's integration notes
// (INTEGRATION.md section 5).
//
// HOW THIS IS DONE ON THE PS1, AND WHY NOT HERE. The patch at
// 0x8008141C swaps the LInB constructor of the seal model (slot 0x4C,
// DYNAMIC_SEAL, otherwise Polar Pass) for one of its own - an address in
// a fixed table, overwritten for as long as the game runs. Reload has
// this table too (zGlobal_DATA.c), but it is the same for ALL tracks.
// Whoever bends it bends it for Polar Pass as well and has to put it
// back afterwards; whoever forgets that has a bug that only shows up two
// tracks later.
//
// Instead a post-step hangs off the birth here: INSTANCE.c calls the
// retail constructor as always, and AFTER that it asks this module whether it
// still wants the instance just born. The table stays untouched, and
// the question "is this Sunset Vista" is asked in exactly one place.
//
// THE RETAIL CONSTRUCTOR RUNS AS WELL, and that is on purpose - the
// source does the same. It creates the thread and the seal object, reads the number from
// the name and sets the draw matrix. Only what happens after that is taken
// over: the tick function, the start state and the waiting time.
//
// THE NAME CASE seal#: AND seal#; NEEDS NOTHING HERE. The author encodes 10
// and 11 as ':' and ';', because retail reads ONE character at name[5]. Reload
// reads the LAST character (RB_Seal.c, inst->name[strlen(inst->name) - 1]).
// For seal#0..seal#9 that is the same character, for seal#: and seal#; the result
// is 10 and 11. All twelve names in the container give 0..11.
// ---------------------------------------------------------------------------

enum
{
	BAT_PATH_POINTS = 8,
	BAT_PATH_SEGMENTS = 7,

	// Only segments 0..4 hurt. 5 and 6 are the visible
	// climb back up - it is meant to be seen, but no longer hit anyone.
	BAT_RECOVERY_SEGMENT = 5,

	BAT_SEGMENT_TICKS = 20,
	BAT_WAIT_TICKS = 90,
	BAT_STAGGER_TICKS = 8,

	// LinkedCollide_Radius gets the SQUARE. 256 for the first group,
	// 208 for the second: there the corridor is only 640 units wide, and the
	// bats fly 162 units above the road. 256 would close both
	// shoulders.
	BAT_HIT_RADIUS = 256,
	BAT_HIT_RADIUS_SECOND = 208,
	BAT_SECOND_GROUP_FIRST_ID = 6,

	BAT_FLY_ANIM_FRAMES = 14,
	BAT_PITCH_STEP = 0x300,

	BAT_COUNT = 12,

	// The two decoration bats. The packed model has 16 frames, not
	// the 48 of the source animation; after that it holds the calm pose 0 for 75 ticks.
	BAT_IDLE_ANIM_FRAMES = 16,
	BAT_IDLE_HOLD_TICKS = 75,
	BAT_IDLE_COUNT = 2,

	BAT_SOUND_ID = 0x7f,
};

global_variable const char s_trackModBatModel[] = "sunset_bat";
global_variable const char s_trackModBatIdleModel[] = "sunset_idle";

global_variable struct Instance *s_batInst[BAT_COUNT];
global_variable struct Instance *s_batIdleInst[BAT_IDLE_COUNT];
global_variable int s_batIdleCycle[BAT_IDLE_COUNT];
global_variable u32 s_batAudioHandle;

// --- THE COUNTING ----------------------------------------------------------
global_variable s64 s_batConverted;   // instances this module took over
global_variable s64 s_batDeclined;    // births that were passed through
global_variable s64 s_batTicks;       // tick calls of our own tick function
global_variable s64 s_batFlights;     // take-offs (waiting time elapsed)
global_variable s64 s_batWaitTicks;   // ticks in the waiting time
global_variable s64 s_batHurtPlayer;  // hits on a player
global_variable s64 s_batHurtBot;     // hits on a bot
global_variable s64 s_batSounds;      // frames in which the positional sound ran
global_variable s64 s_batMutes;       // frames in which it was muted
global_variable s64 s_batIdleTicks;   // ticks of the decoration animation
global_variable s64 s_batBadPath;     // ticks without a usable path
global_variable int s_batSeenIDs;     // bit mask of the numbers taken over
global_variable int s_batAudioLogged;

internal int TrackMod_BatIsTrackActive(void)
{
	struct GameTracker *gGT = sdata->gGT;

	return (gGT != NULL) && NativeTrack_ActiveForLevel(gGT->levelID);
}

internal int TrackMod_InstanceHasModel(const struct Instance *inst, const char *name)
{
	if ((inst == NULL) || (inst->instDef == NULL) || (inst->instDef->model == NULL))
	{
		return 0;
	}

	return strncmp(inst->instDef->model->name, name, MODEL_NAME_BYTE_COUNT) == 0;
}

// THE PATH COMES FROM THE LEV, NOT FROM THE CODE. Eight points per bat,
// seven segments, in ptrSpawnType2 - the same table from which the
// retail constructor reads the seal route (RB_Seal.c). The author names
// reference/bat-paths.json as the source and adds that it was pulled from the finished
// LEV; the table holds twelve entries of eight points each.
internal const s16 *TrackMod_BatPath(int sealID)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	struct SpawnType2 *path;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return NULL;
	}

	level = gGT->level1;
	if ((sealID < 0) || (sealID >= level->numSpawnType2) || (level->ptrSpawnType2 == NULL))
	{
		return NULL;
	}

	path = &level->ptrSpawnType2[sealID];
	if ((path->coords.posCoords == NULL) || (path->numCoords < BAT_PATH_POINTS))
	{
		return NULL;
	}

	return path->coords.posCoords;
}

internal void TrackMod_BatHurt(struct Instance *victim, int isBot)
{
	if ((victim == NULL) || (victim->thread == NULL) || (victim->thread->object == NULL))
	{
		return;
	}

	// Damage type 1 is the spin-out - the same one a hedgehog or a mine
	// deals. No attacker, no reason: the bat belongs to nobody.
	RB_Hazard_HurtDriver((struct Driver *)victim->thread->object, 1, NULL, 0);

	if (isBot)
	{
		s_batHurtBot++;
	}
	else
	{
		s_batHurtPlayer++;
	}
}

internal void TrackMod_BatThTick(struct Thread *t)
{
	struct Instance *inst;
	struct Seal *bat;
	struct GameTracker *gGT = sdata->gGT;
	const s16 *path;
	const s16 *start;
	const s16 *end;
	struct Instance *hit;
	int phase;
	int timer;
	int nextAnimFrame;
	int axis;

	if ((t == NULL) || (t->inst == NULL) || (t->object == NULL))
	{
		return;
	}

	inst = t->inst;
	bat = (struct Seal *)t->object;
	s_batTicks++;

	path = TrackMod_BatPath(bat->sealID);
	if (path == NULL)
	{
		s_batBadPath++;
		return;
	}

	phase = bat->direction;
	timer = bat->distFromSpawn;

	// The flight loop always keeps running, also during the waiting time - the
	// bat does not hang rigidly in the air, it is only invisible. The
	// offset from the constructor keeps the twelve out of lockstep.
	nextAnimFrame = inst->animFrame + 1;
	inst->animFrame = (s16)((nextAnimFrame == BAT_FLY_ANIM_FRAMES) ? 0 : nextAnimFrame);

	if (phase == BAT_PATH_SEGMENTS)
	{
		timer++;
		if (timer < BAT_WAIT_TICKS)
		{
			bat->distFromSpawn = (s16)timer;
			s_batWaitTicks++;
			return;
		}

		// Take-off: put it on the first point and become visible.
		bat->direction = 0;
		bat->distFromSpawn = 0;
		inst->flags &= ~(u32)HIDE_MODEL;
		s_batFlights++;

		for (axis = 0; axis < 3; axis++)
		{
			inst->matrix.t[axis] = path[axis];
		}
		return;
	}

	if ((phase < 0) || (phase >= BAT_PATH_SEGMENTS))
	{
		s_batBadPath++;
		return;
	}

	start = &path[phase * 3];
	end = start + 3;

	// PITCH FROM THE PATH, YAW FROM THE DATA. When sinking, nose
	// down, level above the road, when climbing, nose up -
	// sign of dY times 0x300.
	//
	// DEVIATION FROM THE SOURCE, on purpose: there the pitch is WRITTEN into
	// inst->instDef->rot[0], i.e. into the loaded level data, and
	// never put back. The track's integration notes (INTEGRATION.md:44) demand the
	// opposite ("reload pristine mutable LEV state"). Here it lives in a local rotation;
	// the baked yaw is carried along unchanged.
	{
		SVec3 rot;
		int rising = (end[1] > start[1]) - (end[1] < start[1]);

		rot.x = (s16)(rising * BAT_PITCH_STEP);
		rot.y = inst->instDef->rot.y;
		rot.z = inst->instDef->rot.z;
		ConvertRotToMatrix(&inst->matrix, &rot);
	}

	timer++;
	for (axis = 0; axis < 3; axis++)
	{
		inst->matrix.t[axis] = start[axis] + (((int)end[axis] - (int)start[axis]) * timer) / BAT_SEGMENT_TICKS;
	}

	if (phase < BAT_RECOVERY_SEGMENT)
	{
		u32 hitRadius = (bat->sealID >= BAT_SECOND_GROUP_FIRST_ID) ? (u32)(BAT_HIT_RADIUS_SECOND * BAT_HIT_RADIUS_SECOND)
		                                                          : (u32)(BAT_HIT_RADIUS * BAT_HIT_RADIUS);

		hit = LinkedCollide_Radius(inst, t, gGT->threadBuckets[PLAYER].thread, hitRadius);
		if (hit != NULL)
		{
			TrackMod_BatHurt(hit, 0);
		}
		else
		{
			hit = LinkedCollide_Radius(inst, t, gGT->threadBuckets[ROBOT].thread, hitRadius);
			if (hit != NULL)
			{
				TrackMod_BatHurt(hit, 1);
			}
		}
	}

	if (timer >= BAT_SEGMENT_TICKS)
	{
		bat->direction = (s16)(phase + 1);
		bat->distFromSpawn = 0;

		// At the end of the last segment it disappears again and starts
		// waiting from the beginning.
		if (phase == BAT_PATH_SEGMENTS - 1)
		{
			inst->flags |= (u32)HIDE_MODEL;
		}
	}
	else
	{
		bat->distFromSpawn = (s16)timer;
	}
}

// THE POST-STEP AT BIRTH. Called from INSTANCE.c, right after the
// retail constructor, for every instance with a model slot. On every
// other track it returns at once.
void NativeTrackMod_LevInstanceBorn(struct Instance *inst, int modelID)
{
	struct Thread *t;
	struct Seal *bat;

	if ((modelID != DYNAMIC_SEAL) || (inst == NULL))
	{
		return;
	}

	// TWO QUESTIONS, BOTH MUST SAY YES - the same rule as when setting up the
	// scaffold. The host releases the track, and the model releases the actor.
	// The model name is needed because a container track may also bring
	// a real seal.
	//
	// WHY NOT VIA s_trackModActive: this call comes from loading,
	// i.e. BEFORE NativeTrackMod_Reset. The scaffold does not stand yet
	// at this point.
	if (!TrackMod_BatIsTrackActive() || !TrackMod_InstanceHasModel(inst, s_trackModBatModel))
	{
		s_batDeclined++;
		return;
	}

	t = inst->thread;
	if ((t == NULL) || (t->object == NULL))
	{
		// The retail constructor did not get a thread - then there is
		// nothing to take over either.
		s_batDeclined++;
		return;
	}

	bat = (struct Seal *)t->object;

	if ((bat->sealID < 0) || (bat->sealID >= BAT_COUNT))
	{
		Platform_LogWarn("[CTR Bat] '%s' gives id %d, outside 0..%d - left as a seal\n", inst->name, (int)bat->sealID, BAT_COUNT - 1);
		s_batDeclined++;
		return;
	}

	t->funcThTick = TrackMod_BatThTick;

	// The start state: segment 7 means "still waiting". The waiting time is
	// offset by 8 ticks per number, so the twelve do not take off in
	// lockstep - number 0 starts at once, number 11 after 88 ticks.
	bat->direction = BAT_PATH_SEGMENTS;
	bat->distFromSpawn = (s16)(BAT_WAIT_TICKS - (bat->sealID * BAT_STAGGER_TICKS));
	inst->flags |= (u32)HIDE_MODEL;
	inst->animFrame = bat->sealID;

	s_batInst[bat->sealID] = inst;
	s_batSeenIDs |= 1 << bat->sealID;
	s_batConverted++;
}

// WHAT LIES DIFFERENTLY IN Reload THAN IN THE SOURCE. sunset_bat_audio.c takes the
// instances by their place in the list: index 0 the decoration bat, 1..6 the
// first group. In the container at hand that is not true - there TWO
// decoration bats sit at 0 and 1, and seal#0..seal#; follow at 2..13.
// Whoever takes over the indices makes the sound follow a decoration
// bat that never sets its visibility bit.
//
// So here we search for what was meant: the model decides,
// and for the flying ones the number from the name decides.
internal void TrackMod_BatCollect(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	u32 i;
	int idle = 0;

	for (i = 0; i < BAT_COUNT; i++)
	{
		s_batInst[i] = NULL;
	}
	for (i = 0; i < BAT_IDLE_COUNT; i++)
	{
		s_batIdleInst[i] = NULL;
		s_batIdleCycle[i] = 0;
	}

	if ((gGT == NULL) || (gGT->level1 == NULL) || (gGT->level1->ptrInstDefs == NULL))
	{
		return;
	}

	level = gGT->level1;

	for (i = 0; i < level->numInstances; i++)
	{
		struct InstDef *instDef = &level->ptrInstDefs[i];
		struct Instance *inst = instDef->ptrInstance;

		if ((inst == NULL) || (instDef->model == NULL))
		{
			continue;
		}

		if (strncmp(instDef->model->name, s_trackModBatIdleModel, MODEL_NAME_BYTE_COUNT) == 0)
		{
			if (idle < BAT_IDLE_COUNT)
			{
				s_batIdleInst[idle] = inst;
				idle++;
			}
			continue;
		}

		if ((strncmp(instDef->model->name, s_trackModBatModel, MODEL_NAME_BYTE_COUNT) == 0) && (inst->thread != NULL) &&
		    (inst->thread->object != NULL))
		{
			struct Seal *bat = (struct Seal *)inst->thread->object;

			if ((bat->sealID >= 0) && (bat->sealID < BAT_COUNT))
			{
				s_batInst[bat->sealID] = inst;
			}
		}
	}
}

// THE FINDING ON THE SOUND ASSIGNMENT, WRITTEN DOWN ONCE PER RUN.
//
// The track's README and integration notes (INTEGRATION.md:91) say explicitly:
// check, do not assume. The number 0x7F is a slot in a table built from the
// HEADER OF THE RETAIL FILE KART.HWL (howl_ParseHeader), not from the container.
// The container replaces exactly one bank and one sequence (native_assets.c);
// whether the bank holding the bat sample is among them is decided by
// whoever packs it - not by this code. So here we only report what
// is there: whether the number lies in the table at all, which
// SPU slot it points to, and whether that slot is filled.
internal void TrackMod_BatLogSound(void)
{
	struct OtherFX *fx;
	int count;

	if (s_batAudioLogged)
	{
		return;
	}
	s_batAudioLogged = 1;

	if ((sdata->ptrHowlHeader == NULL) || (sdata->howl_metaOtherFX == NULL) || (sdata->howl_spuAddrs == NULL))
	{
		Platform_Log("[CTR Bat] OtherFX 0x%02x: no HOWL tables loaded - not determined\n", (unsigned)BAT_SOUND_ID);
		return;
	}

	count = sdata->ptrHowlHeader->numOtherFX;
	if (BAT_SOUND_ID >= count)
	{
		Platform_Log("[CTR Bat] OtherFX 0x%02x: out of range, the table holds %d entries - the sound cannot play\n", (unsigned)BAT_SOUND_ID, count);
		return;
	}

	fx = &sdata->howl_metaOtherFX[BAT_SOUND_ID];
	Platform_Log("[CTR Bat] OtherFX 0x%02x of %d: spuIndex %u, spuAddr 0x%04x, size 0x%04x, volume %u, pitch %u, duration %u, flags 0x%02x - %s\n",
	             (unsigned)BAT_SOUND_ID, count, (unsigned)fx->spuIndex, (unsigned)Howl_SpuAddr(fx->spuIndex),
	             (unsigned)sdata->howl_spuAddrs[fx->spuIndex].spuSize, (unsigned)fx->volume, (unsigned)fx->pitch, (unsigned)fx->duration,
	             (unsigned)fx->flags, (Howl_SpuAddr(fx->spuIndex) != 0) ? "loaded" : "NOT LOADED, it will stay silent");
}

internal void TrackMod_BatReset(void)
{
	int i;

	OtherFX_RecycleMute(&s_batAudioHandle);
	s_batAudioHandle = 0;

	for (i = 0; i < BAT_IDLE_COUNT; i++)
	{
		s_batIdleCycle[i] = 0;
	}

	TrackMod_BatCollect();
	TrackMod_BatLogSound();
}

internal void TrackMod_BatForget(void)
{
	int i;

	s_batAudioHandle = 0;
	for (i = 0; i < BAT_COUNT; i++)
	{
		s_batInst[i] = NULL;
	}
	for (i = 0; i < BAT_IDLE_COUNT; i++)
	{
		s_batIdleInst[i] = NULL;
		s_batIdleCycle[i] = 0;
	}
}

// AFTER THE GAME LOGIC, as the track's integration notes (INTEGRATION.md
// section 2) list it: "update bat audio/idle pose". The flight ticks themselves
// run in the thread system and are already done at this point.
internal void TrackMod_BatPostLogic(void)
{
	int i;

	// THE DECORATION BATS. The whole loop once, then 2.5 seconds of the
	// calm pose 0. The source keeps a counter for this in Instance+0x50;
	// in Reload that is depthBiasNormal (namespace_Instance.h), i.e. a
	// field in use. That is why the counter lives here in the module.
	for (i = 0; i < BAT_IDLE_COUNT; i++)
	{
		struct Instance *idle = s_batIdleInst[i];
		int cycle;

		if (idle == NULL)
		{
			continue;
		}

		cycle = s_batIdleCycle[i] + 1;
		if (cycle >= (BAT_IDLE_ANIM_FRAMES + BAT_IDLE_HOLD_TICKS))
		{
			cycle = 0;
		}
		s_batIdleCycle[i] = cycle;
		idle->animFrame = (s16)((cycle < BAT_IDLE_ANIM_FRAMES) ? cycle : 0);
		s_batIdleTicks++;
	}

	// THE POSITIONAL SOUND follows the first VISIBLE one of the first group. As soon as
	// none is flying any more, the slot is given back - otherwise a
	// channel hangs on a bat that is not there at all.
	for (i = 0; i < BAT_SECOND_GROUP_FIRST_ID; i++)
	{
		struct Instance *bat = s_batInst[i];

		if ((bat != NULL) && ((bat->flags & (u32)HIDE_MODEL) == 0))
		{
			PlaySound3D_Flags(&s_batAudioHandle, BAT_SOUND_ID, bat);
			s_batSounds++;
			return;
		}
	}

	OtherFX_RecycleMute(&s_batAudioHandle);
	s_batMutes++;
}

internal void TrackMod_BatReport(void)
{
	int i;
	int alive = 0;

	for (i = 0; i < BAT_COUNT; i++)
	{
		if (s_batInst[i] != NULL)
		{
			alive++;
		}
	}

	Platform_Log("[CTR Bat] at exit: %lld instance(s) taken over (ids 0x%03x of 0x%03x, %d still held), %lld birth(s) left as they were\n",
	             s_batConverted, (unsigned)s_batSeenIDs, (1u << BAT_COUNT) - 1u, alive, s_batDeclined);
	Platform_Log("[CTR Bat] at exit: %lld tick(s) - %lld waiting, %lld take-off(s), %lld without a usable path\n", s_batTicks, s_batWaitTicks,
	             s_batFlights, s_batBadPath);
	Platform_Log("[CTR Bat] at exit: %lld hit(s) on a player, %lld on a bot; %lld frame(s) with the positional sound, %lld muted; %lld idle "
	             "animation tick(s)\n",
	             s_batHurtPlayer, s_batHurtBot, s_batSounds, s_batMutes, s_batIdleTicks);
}

// ---------------------------------------------------------------------------
// THE FIVE FIRE BOWLS.
//
// Source: the track's source (ps1_trackrom/src/flamejet_sunset.c), numbers
// confirmed in the track's integration notes (INTEGRATION.md section 6).
//
// The stone bowls and their drivable lids are already in the LEV.
// What comes in here is the fire: a vertical column of short-lived
// billboards, a schedule and the damage.
//
// THE CYCLE, offset by one fifth per bowl:
//
//   warning   27 ticks   the column grows from 0x2800 to 0x6000
//   active    64 ticks   full height, and ONLY HERE does it hurt
//   fade-out  16 ticks   it collapses again, every second tick
//   off      120 ticks   nothing
//
// 227 ticks in total, so at 30 ticks per second a good seven and a half
// seconds. The source wraps every number in FPS_DOUBLE, for a build with 60
// ticks per second; Reload computes with 30 (MainFrame.c sets 32 ms per
// tick), so the numbers here are not doubled.
//
// WHERE THE IMAGES COME FROM. The flame is not in the LEV as a finished billboard,
// but as FOUR animated faces - records 40..43 of the
// AnimTex chain, ten frames each. Each face is one quarter of the image. Whoever
// takes one of them alone gets a quarter flame. So for each
// frame ONE layout is built from the common hull of all four: smallest u, largest
// u, likewise v, plus CLUT and page of the first. For this track that gives
// exactly 32 x 32 texels ten times, with a changing CLUT per frame (0x6635..0x6672)
// and two pages (0x7f for frames 0..3, 0x78 for 4..9).
//
// WHERE THEY GO. Into gGT->iconGroup[0xA] - the same slot from which
// RB_FlameJet_Particles (231/RB_FlameJet.c) gets its flames. The old
// pointer is remembered and put back on leaving.
//
// THE EMITTER. The track's PATCH_MAP lists 0x800B6C20 as "not a PC API". That is
// emSet_fjFire, and Reload has it in 231/RB_FlameJet.c. It is used here just
// as in RB_FlameJet_Particles - the same set, the same icon slot. Nothing rebuilt.
// ---------------------------------------------------------------------------

// From game/231/RB_FlameJet.c, which comes further down in this translation
// unit. The flame set of the flame jet, taken over unchanged.
extern struct ParticleEmitter emSet_fjFire[];

enum
{
	FLAME_BRAZIER_COUNT = 5,

	FLAME_WARNING_TICKS = 27,
	FLAME_ACTIVE_TICKS = 64,
	FLAME_FADE_TICKS = 16,
	FLAME_OFF_TICKS = 120,

	FLAME_ACTIVE_START = FLAME_WARNING_TICKS,
	FLAME_ACTIVE_END = FLAME_ACTIVE_START + FLAME_ACTIVE_TICKS,
	FLAME_FADE_END = FLAME_ACTIVE_END + FLAME_FADE_TICKS,
	FLAME_CYCLE_TICKS = FLAME_FADE_END + FLAME_OFF_TICKS,

	// The hit volume: horizontally a square of 0x180 around the centre, vertically
	// from 0x60 below the floor to 0x300 above the rim. Not the column is
	// measured but this box - the column is what you see.
	FLAME_HALF_WIDTH = 0x180,
	FLAME_BELOW_FLOOR = 0x60,
	FLAME_ABOVE_RIM = 0x300,

	FLAME_DAMAGE_TYPE = 4,

	FLAME_SPEED_MIN = 0x2800,
	FLAME_SPEED_MAX = 0x6000,

	FLAME_ANIM_FIRST_RECORD = 40,
	FLAME_ANIM_FACE_COUNT = 4,
	FLAME_ICON_COUNT = 10,
	FLAME_ICON_GROUP_ID = 0xA,

	// The check windows for the hull. A frame is 32 or 64 texels wide
	// and high; anything else means the records are not the ones
	// we take them for - then the fire had better stay off.
	FLAME_SPAN_SMALL = 31,
	FLAME_SPAN_LARGE = 63,
};

struct TrackModBrazier
{
	s16 x;
	s16 y; // height of the rim
	s16 z;
	s16 floor;
};

// From the highest rim points of Feu_Instance.001-.005, converted to
// CTR (X*320, Z*320, -Y*320). The same table is in the track's integration
// notes (INTEGRATION.md section 6).
global_variable const struct TrackModBrazier s_flameBrazier[FLAME_BRAZIER_COUNT] = {
    {-3435, 2527, 9741, 2332},
    {4302, 2990, 3002, 2635},
    {3742, -868, 9199, -1063},
    {-7966, 2458, -2573, 2263},
    {4302, 2990, 1793, 2635},
};

// DEVIATION FROM THE SOURCE: there this block comes from MEMPACK_AllocMem. Here
// it is fixed in the module. It is 0x17c bytes, it is needed exactly once,
// and so it has no lifetime one could get wrong -
// a restart gives the MEMPACK memory back, and a pointer to it in
// gGT->iconGroup[0xA] would afterwards be a pointer into foreign ground.
struct TrackModFlameIcons
{
	struct IconGroup group;
	struct Icon *iconPtrs[FLAME_ICON_COUNT];
	struct Icon icons[FLAME_ICON_COUNT];
};

// The pointer list must lie directly behind the group - ICONGROUP_GETICONS
// (namespace_Decal.h) computes exactly that way.
CTR_STATIC_ASSERT(offsetof(struct TrackModFlameIcons, iconPtrs) == sizeof(struct IconGroup));

global_variable struct TrackModFlameIcons s_flameIcons;
global_variable struct IconGroup *s_flameIconGroupSaved;
global_variable int s_flameIconGroupTaken;
global_variable int s_flameReady;
global_variable int s_flameTimer;

// --- THE COUNTING ----------------------------------------------------------
global_variable s64 s_flameTicks;      // ticks in which the actor computed
global_variable s64 s_flameSpawns;     // particles requested
global_variable s64 s_flameSpawnFails; // of those, rejected (pool full)
global_variable s64 s_flameWarnTicks;
global_variable s64 s_flameActiveTicks;
global_variable s64 s_flameFadeTicks;
global_variable s64 s_flameHurts;
global_variable int s_flameIconsBuilt;
global_variable s64 s_flameIconTakes;
global_variable s64 s_flameIconRestores;
global_variable s64 s_flameIconForeign;
global_variable int s_flameSetupFail; // at which check the setup failed

internal struct AnimTex *TrackMod_FlameNextAnimTex(struct AnimTex *animTex)
{
	struct IconGroup4 **frames = ANIMTEX_GETARRAY(animTex);

	return (struct AnimTex *)&frames[animTex->numFrames];
}

// Ten billboards from four quarters. If one of the checks fails,
// s_flameReady stays 0 and nothing burns - better no fire than a quarter
// flame in the wrong place.
internal void TrackMod_FlameBuildIcons(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	struct AnimTex *animTex;
	struct AnimTex *faces[FLAME_ANIM_FACE_COUNT];
	int i;
	int face;
	int frame;

	s_flameReady = 0;
	s_flameSetupFail = 0;

	if ((gGT == NULL) || (gGT->level1 == NULL) || (gGT->level1->ptr_anim_tex == NULL))
	{
		s_flameSetupFail = 1;
		return;
	}
	level = gGT->level1;

	memset(&s_flameIcons, 0, sizeof(s_flameIcons));
	s_flameIcons.group.groupID = FLAME_ICON_GROUP_ID;
	s_flameIcons.group.numIcons = FLAME_ICON_COUNT;
	for (i = 0; i < FLAME_ICON_COUNT; i++)
	{
		s_flameIcons.iconPtrs[i] = &s_flameIcons.icons[i];
	}

	animTex = level->ptr_anim_tex;
	for (i = 0; i < FLAME_ANIM_FIRST_RECORD; i++)
	{
		if ((animTex->numFrames < 1) || (animTex->numFrames > 64))
		{
			s_flameSetupFail = 2;
			return;
		}
		animTex = TrackMod_FlameNextAnimTex(animTex);
	}

	for (face = 0; face < FLAME_ANIM_FACE_COUNT; face++)
	{
		if (animTex->numFrames != FLAME_ICON_COUNT)
		{
			s_flameSetupFail = 3;
			return;
		}
		faces[face] = animTex;
		animTex = TrackMod_FlameNextAnimTex(animTex);
	}

	for (frame = 0; frame < FLAME_ICON_COUNT; frame++)
	{
		int minU = 0xff;
		int minV = 0xff;
		int maxU = 0;
		int maxV = 0;
		struct TextureLayout *first = NULL;

		for (face = 0; face < FLAME_ANIM_FACE_COUNT; face++)
		{
			struct IconGroup4 **frames = ANIMTEX_GETARRAY(faces[face]);
			struct TextureLayout *layout = &frames[frame]->near;
			u8 u[4];
			u8 v[4];
			int corner;

			u[0] = layout->u0;
			u[1] = layout->u1;
			u[2] = layout->u2;
			u[3] = layout->u3;
			v[0] = layout->v0;
			v[1] = layout->v1;
			v[2] = layout->v2;
			v[3] = layout->v3;

			if (first == NULL)
			{
				first = layout;
			}

			for (corner = 0; corner < 4; corner++)
			{
				if (u[corner] < minU)
				{
					minU = u[corner];
				}
				if (u[corner] > maxU)
				{
					maxU = u[corner];
				}
				if (v[corner] < minV)
				{
					minV = v[corner];
				}
				if (v[corner] > maxV)
				{
					maxV = v[corner];
				}
			}
		}

		if ((first == NULL) || (((maxU - minU) != FLAME_SPAN_SMALL) && ((maxU - minU) != FLAME_SPAN_LARGE)) ||
		    (((maxV - minV) != FLAME_SPAN_SMALL) && ((maxV - minV) != FLAME_SPAN_LARGE)))
		{
			s_flameSetupFail = 4;
			return;
		}

		s_flameIcons.icons[frame].global_IconArray_Index = -1;
		s_flameIcons.icons[frame].texLayout.u0 = (u8)minU;
		s_flameIcons.icons[frame].texLayout.v0 = (u8)minV;
		s_flameIcons.icons[frame].texLayout.clut = first->clut;
		s_flameIcons.icons[frame].texLayout.u1 = (u8)maxU;
		s_flameIcons.icons[frame].texLayout.v1 = (u8)minV;
		s_flameIcons.icons[frame].texLayout.tpage = first->tpage;
		s_flameIcons.icons[frame].texLayout.u2 = (u8)minU;
		s_flameIcons.icons[frame].texLayout.v2 = (u8)maxV;
		s_flameIcons.icons[frame].texLayout.u3 = (u8)maxU;
		s_flameIcons.icons[frame].texLayout.v3 = (u8)maxV;
	}

	if (!s_flameIconGroupTaken)
	{
		s_flameIconGroupSaved = gGT->iconGroup[FLAME_ICON_GROUP_ID];
		s_flameIconGroupTaken = 1;
		s_flameIconTakes++;
	}
	gGT->iconGroup[FLAME_ICON_GROUP_ID] = &s_flameIcons.group;

	s_flameReady = 1;
	s_flameIconsBuilt++;

	Platform_Log("[CTR Flame] %d billboard(s) built from anim records %d..%d, icon group %d taken (was %p)\n", FLAME_ICON_COUNT,
	             FLAME_ANIM_FIRST_RECORD, FLAME_ANIM_FIRST_RECORD + FLAME_ANIM_FACE_COUNT - 1, FLAME_ICON_GROUP_ID,
	             (void *)s_flameIconGroupSaved);
}

// Give the icon slot back. Must run before another track
// needs it - the track's integration notes (INTEGRATION.md section 6): "Scope
// its replacement icon group to this level."
// COUNTED. A restore alone does not prove that it fires. Three counters
// answer that: how often the
// slot was taken, how often it was really put back, and how often
// a FOREIGN pointer sat in it on release - the last case would be the
// interesting one, because then someone else would have written in between and the
// restore would overwrite their work.
internal void TrackMod_FlameReleaseIcons(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (s_flameIconGroupTaken && (gGT != NULL))
	{
		if (gGT->iconGroup[FLAME_ICON_GROUP_ID] == &s_flameIcons.group)
		{
			gGT->iconGroup[FLAME_ICON_GROUP_ID] = s_flameIconGroupSaved;
			s_flameIconRestores++;
		}
		else
		{
			s_flameIconForeign++;
		}
	}

	s_flameIconGroupTaken = 0;
	s_flameIconGroupSaved = NULL;
	s_flameReady = 0;
}

// A VERTICAL COLUMN, NO FLYING SPARKS. Axis 5 is the width, axis 6 the
// height, and both hang on the same speed - that is why the
// flame grows in the warning phase and collapses again in the fade-out. The particle
// lives two ticks and is set anew every tick; it flies nowhere.
internal void TrackMod_FlameSpawn(const struct TrackModBrazier *brazier, int cycle, int speed)
{
	struct Particle *particle;
	struct Icon *icon;
	int height;

	s_flameSpawns++;

	particle = Particle_Init(0, &s_flameIcons.group, &emSet_fjFire[0]);
	if (particle == NULL)
	{
		s_flameSpawnFails++;
		return;
	}

	icon = s_flameIcons.iconPtrs[cycle % FLAME_ICON_COUNT];
	particle->ptrIconArray = icon;

	memset(particle->axis, 0, sizeof(particle->axis));
	particle->funcPtr = NULL;

	// Axes 0,1,2 (position), 5 (width), 6 (height).
	particle->flagsAxis = 0x67;
	particle->flagsSetColor |= 0x400;
	particle->framesLeftInLife = 2;

	height = (speed * 7) / 16;

	particle->axis[0].startVal = brazier->x * 0x100;
	particle->axis[2].startVal = brazier->z * 0x100;
	particle->axis[5].startVal = (speed * 3) / 16;
	particle->axis[6].startVal = height;

	// The column stands ON the rim: its centre lies half the
	// billboard height above it, and that comes from the texel height times the
	// height axis.
	particle->axis[1].startVal = (brazier->y + (((icon->texLayout.v2 - icon->texLayout.v0 + 1) * height) >> 10)) * 0x100;

	particle->renderDepthLimit = 0x1e00;
	particle->otIndexOffset = 0;
}

internal void TrackMod_FlameHurt(const struct TrackModBrazier *brazier)
{
	struct GameTracker *gGT = sdata->gGT;
	int i;

	for (i = 0; i < 8; i++)
	{
		struct Driver *driver = gGT->drivers[i];
		int x;
		int y;
		int z;

		if ((driver == NULL) || (driver->instSelf == NULL))
		{
			continue;
		}

		x = driver->posCurr.x >> 8;
		y = driver->posCurr.y >> 8;
		z = driver->posCurr.z >> 8;

		if ((x < brazier->x - FLAME_HALF_WIDTH) || (x > brazier->x + FLAME_HALF_WIDTH) || (y < brazier->floor - FLAME_BELOW_FLOOR) ||
		    (y > brazier->y + FLAME_ABOVE_RIM) || (z < brazier->z - FLAME_HALF_WIDTH) || (z > brazier->z + FLAME_HALF_WIDTH))
		{
			continue;
		}

		RB_Hazard_HurtDriver(driver, FLAME_DAMAGE_TYPE, NULL, 0);
		s_flameHurts++;
	}
}

// BEFORE THE GAME LOGIC, and as the very first thing. The track's integration notes
// (INTEGRATION.md section 2) list "update flames" before "update platform poses"
// and before the game logic.
internal void TrackMod_FlameUpdate(void)
{
	int i;

	if (!s_flameReady)
	{
		return;
	}

	s_flameTicks++;

	for (i = 0; i < FLAME_BRAZIER_COUNT; i++)
	{
		const struct TrackModBrazier *brazier = &s_flameBrazier[i];
		int cycle = (s_flameTimer + ((FLAME_CYCLE_TICKS * i) / FLAME_BRAZIER_COUNT)) % FLAME_CYCLE_TICKS;

		if (cycle < FLAME_ACTIVE_START)
		{
			int speed = FLAME_SPEED_MIN + (((FLAME_SPEED_MAX - FLAME_SPEED_MIN) * cycle) / FLAME_WARNING_TICKS);

			TrackMod_FlameSpawn(brazier, cycle, speed);
			s_flameWarnTicks++;
		}
		else if (cycle < FLAME_ACTIVE_END)
		{
			TrackMod_FlameSpawn(brazier, cycle, FLAME_SPEED_MAX);
			TrackMod_FlameHurt(brazier);
			s_flameActiveTicks++;
		}
		else if ((cycle < FLAME_FADE_END) && ((cycle & 1) == 0))
		{
			int remaining = FLAME_FADE_END - cycle;
			int speed = FLAME_SPEED_MIN + (((FLAME_SPEED_MAX - FLAME_SPEED_MIN) * remaining) / FLAME_FADE_TICKS);

			TrackMod_FlameSpawn(brazier, cycle, speed);
			s_flameFadeTicks++;
		}
	}

	s_flameTimer++;
	if (s_flameTimer >= FLAME_CYCLE_TICKS)
	{
		s_flameTimer = 0;
	}
}

internal void TrackMod_FlameReset(void)
{
	s_flameTimer = 0;
	TrackMod_FlameBuildIcons();

	if (!s_flameReady)
	{
		Platform_LogWarn("[CTR Flame] the anim records are not what they should be (check %d) - no fire on this track\n", s_flameSetupFail);
	}
}

internal void TrackMod_FlameForget(void)
{
	TrackMod_FlameReleaseIcons();
	s_flameTimer = 0;
}

internal void TrackMod_FlameReport(void)
{
	Platform_Log("[CTR Flame] at exit: icon set(s) built %d, last setup check %d; %lld tick(s) computed\n", s_flameIconsBuilt, s_flameSetupFail,
	             s_flameTicks);
	Platform_Log("[CTR Flame] at exit: %lld particle(s) asked for, %lld refused by the pool; %lld warning, %lld active, %lld fade spawn(s)\n",
	             s_flameSpawns, s_flameSpawnFails, s_flameWarnTicks, s_flameActiveTicks, s_flameFadeTicks);
	Platform_Log("[CTR Flame] at exit: %lld driver hit(s) with damage type %d\n", s_flameHurts, FLAME_DAMAGE_TYPE);
	Platform_Log("[CTR Flame] at exit: icon slot %d taken %lld time(s), put back %lld time(s), %lld release(s) found a foreign pointer\n",
	             FLAME_ICON_GROUP_ID, s_flameIconTakes, s_flameIconRestores, s_flameIconForeign);
}

// ---------------------------------------------------------------------------
// THE SURFACES: MASKGRABS, INVISIBLE WALLS, WATER.
//
// Source: the track's reference/collision-surfaces.json (bound to the SHA-256 of
// the delivered LEV), reference/authoring/visible_maskgrab_layout.json and
// the track's integration notes (INTEGRATION.md section 7). Six groups, plus the
// turbo triggers, which must explicitly be left alone.
//
// WHAT THIS PART DOES NOT BUILD A SECOND TIME. An earlier change extended the draw
// path with the zero rule: a face whose five texture references all point to a
// zeroed assignment is not drawn (226_00_DrawLevelOvr1P.c).
// Exactly 99 quadblocks of the track are built that way - the 18 blocking walls and the
// 81 kill planes. So the visibility question for two of the six groups
// is already answered, and here it is only RE-CHECKED, not decided
// anew.
//
// WHY THE GROUPS STAND SEPARATELY AND NOT AS ONE BIT RULE. INTEGRATION.md:128
// says it literally: "Use the explicit surface list for this port; a broad rule
// based on 0x8000 alone also affects native turbo-trigger QuadBlocks (0xA040)
// and is not a complete rendering policy." That is no caution formula
// but can be recomputed in this container:
//
//   * 0x8000 is carried by the 81 kill planes AND the 72 turbo triggers. Whoever
//     hides by it deletes 72 boost faces as well.
//   * 0x0200 (KILL_PLANE, in the author's usage "MaskGrab") is carried by 658
//     quadblocks - 81 invisible, 106 named visible and 471 further
//     visible recovery faces, water included. Whoever hides by it
//     takes the track's water away.
//   * 0x4001 is carried by 148 - the 123 collision frames of the platforms
//     and the 25 remaining carriers. The two need different
//     treatment: for the frames an actor switches in rhythm, the carriers
//     nobody touches.
//
// That is why there is a table with SEVEN rows below, one per group, each with
// its own index list and its own rule. 773 indices are fixed in the
// code; the 123 platform frames come from the list the platform actor already
// brings.
//
// WHAT IS NEWLY BUILT HERE is exactly one thing: the 25 remaining invisible
// carriers. They are the only case covered neither by the zero rule nor by the
// platform actor - they carry REAL texture assignments (0 of 25 are
// zeroed), so the zero rule does not apply, and they are not in the index list of the
// platforms. They all lie at the same place: 25
// horizontal slabs of 64 x 64 units, stacked on top of each other at
// (-7603, -985), from y 3223 to y 5143 in steps of 80 - a tower next to the
// wandering door, at about 26 % of the lap. The author lists them as "neither
// rendered nor solid". They are not solid by themselves: 0x4001 has neither GROUND
// nor COLLISION_SURFACE, and so every quadblock falls through the filter of
// every searcher (COLL.c). They are kept from being drawn by
// the same route as the platform frames - the visibility bit, cleared
// after the rebuild of the field.
//
// WHAT IS ONLY COUNTED HERE. Everything else. The six groups are fully
// checked once at track start (flags, terrain, checkpoint,
// texture yes/no) and after that every tick for unchanged flags - except the
// 123 frames, which are SUPPOSED to change. Plus the bookkeeping of the
// MaskGrab recovery: every capture and every completed respawn, with the
// group whose face triggered the recovery.
// ---------------------------------------------------------------------------

enum SunsetSurfPolicy
{
	// Invisible because without texture - the zero rule. Here
	// nothing is done, it is only re-checked that it is so.
	SURF_POLICY_BLANK_HIDDEN = 0,

	// Visible and catches. Nothing done, only re-checked.
	SURF_POLICY_VISIBLE_RECOVERY,

	// The 123 frames. They belong to the platform actor; the entry is only
	// here so that the table is complete.
	SURF_POLICY_FRAME_SWITCHED,

	// The 25 carriers. Clear the bit, every frame.
	SURF_POLICY_HIDDEN_CARRIER,

	// The turbo triggers. Explicitly untouched; the only thing counted is that they
	// stay that way.
	SURF_POLICY_UNTOUCHED,
};

enum
{
	SURF_GROUP_WALLS = 0,
	SURF_GROUP_KILLPLANES,
	SURF_GROUP_NAMED_GRABS,
	SURF_GROUP_OTHER_GRABS,
	SURF_GROUP_PLAT_FRAMES,
	SURF_GROUP_CARRIERS,
	SURF_GROUP_TURBO,
	SURF_GROUP_COUNT,
};

enum
{
	SURF_NAMED_GRAB_COUNT = 106,
	SURF_CARRIER_COUNT = 25,
	SURF_CHECKPOINT_NONE = 0xff,

	// Four drivers in split screen; gGT->drivers has no more slots either.
	SURF_MAX_DRIVERS = 4,
};

struct TrackModSurfGroup
{
	const char *name;
	const u16 *index;
	s16 count;
	u16 flagsA;    // allowed flag value
	u16 flagsB;    // second allowed value; equal to flagsA if there is only one
	u8 terrainA;   // allowed terrain
	u8 terrainB;
	u8 policy;
	u8 blankExpected; // 1: all five texture references zeroed. 0: real texture.
	u8 isMaskGrab;    // carries QUADBLOCK_FLAG_KILL_PLANE
	u8 flagsMayChange; // 1: the platform actor legitimately switches here
};

// THE SIX INDEX LISTS. Generated from the track's reference/collision-surfaces.json and
// reference/authoring/visible_maskgrab_layout.json, except the last one: the author
// does not list the 72 turbo triggers one by one, they are read from the container
// itself (all quadblocks with flags 0xa040). The numbers are
// serialized quadblock indices, not the indices of the Blender source.

global_variable const u16 s_surfWallIndex[18] = {
	1142, 1143, 1144, 1145, 1146, 1147, 2280, 2308, 3191, 3192, 5958, 6117,
	6118, 7501, 9445, 11383, 11384, 12004,
};

global_variable const u16 s_surfKillplaneIndex[81] = {
	1028, 1088, 1089, 1090, 1091, 1092, 1093, 1094, 1095, 1096, 1097, 1098,
	1099, 1100, 1742, 1781, 1810, 1938, 2001, 2130, 2993, 2994, 2995, 2996,
	2997, 2998, 3080, 3081, 3379, 3380, 3381, 3382, 3524, 3583, 3584, 4127,
	4128, 4400, 4401, 5143, 5144, 5152, 5153, 5172, 5173, 5345, 5567, 5611,
	5763, 5764, 5772, 6483, 6509, 6677, 7077, 7391, 7392, 7393, 7394, 7395,
	7407, 7417, 7476, 9307, 9308, 9309, 9310, 9399, 9638, 9641, 10628, 10640,
	10655, 10657, 10701, 10873, 10928, 10960, 11050, 11142, 12598,
};

global_variable const u16 s_surfNamedGrabIndex[106] = {
	1101, 1102, 1103, 1106, 1110, 1124, 1125, 1126, 1128, 1132, 1138, 1148,
	1150, 1961, 1963, 1965, 1966, 1967, 1970, 2032, 2033, 2034, 2035, 2036,
	2037, 2052, 2054, 4600, 4622, 4623, 4624, 4629, 4633, 4634, 4635, 4637,
	4665, 4666, 4667, 4672, 4679, 4680, 4686, 4687, 4688, 4710, 4722, 4723,
	4724, 4739, 4741, 5261, 5273, 5274, 5283, 5285, 5297, 5303, 5342, 5344,
	6256, 6257, 6691, 6692, 6713, 6714, 6718, 6719, 8484, 8485, 8486, 8497,
	9501, 9511, 9512, 9543, 10344, 10355, 10377, 10378, 10379, 10395, 10396, 10397,
	10496, 10506, 10529, 10530, 10584, 10585, 10708, 10925, 10926, 10952, 10953, 11025,
	11036, 11169, 11175, 11176, 11177, 11178, 12055, 12065, 12079, 12114,
};

global_variable const u16 s_surfOtherGrabIndex[471] = {
	15, 23, 27, 28, 60, 61, 62, 85, 105, 106, 107, 108,
	109, 121, 122, 123, 126, 127, 128, 162, 163, 164, 165, 185,
	324, 331, 408, 412, 420, 422, 425, 427, 428, 430, 432, 434,
	436, 441, 442, 451, 453, 455, 457, 459, 484, 487, 488, 489,
	516, 524, 525, 533, 534, 563, 564, 573, 574, 613, 616, 617,
	653, 725, 726, 747, 748, 785, 786, 787, 788, 819, 820, 861,
	862, 863, 935, 936, 968, 969, 984, 990, 991, 1006, 1017, 1062,
	1063, 1087, 1184, 1185, 1186, 1187, 1188, 1189, 1196, 1197, 1198, 1219,
	1220, 1227, 1233, 1234, 1312, 1313, 1314, 1316, 1865, 1866, 1880, 1881,
	1920, 1921, 2173, 2174, 2182, 2215, 2242, 2261, 2306, 2352, 2433, 2434,
	2435, 2436, 2437, 2438, 2439, 2440, 2470, 2471, 2472, 2485, 2486, 2497,
	2498, 2531, 2532, 2533, 2534, 2756, 2783, 2784, 2785, 2816, 2817, 2818,
	2823, 2824, 2833, 2834, 2837, 3880, 3936, 4017, 4038, 4116, 4120, 4121,
	4141, 4144, 4153, 4154, 4155, 4210, 4211, 4243, 4260, 4279, 4280, 4297,
	4299, 4300, 4301, 4302, 4304, 4307, 4315, 4842, 4844, 4892, 4914, 4947,
	4948, 4949, 4950, 4967, 4968, 5047, 5048, 5049, 5074, 5086, 5090, 5498,
	5565, 5617, 5618, 5654, 5686, 5692, 5762, 5769, 5770, 5771, 5824, 5854,
	5855, 6188, 6189, 6190, 6312, 6345, 6769, 6770, 6771, 6772, 6773, 6873,
	6874, 6875, 6879, 6891, 6892, 6893, 6894, 6910, 6916, 6932, 6934, 6940,
	6941, 6981, 6982, 7003, 7015, 7016, 7017, 7036, 7058, 7059, 7073, 7076,
	7124, 7137, 7138, 7244, 7258, 7259, 7260, 7282, 7290, 7294, 7303, 7304,
	7324, 7325, 7338, 7339, 7344, 7557, 7558, 7586, 7606, 7607, 7608, 7625,
	7628, 7629, 7720, 7775, 7891, 7897, 8069, 8083, 8095, 8114, 8115, 8116,
	8117, 8121, 8122, 8123, 8124, 8125, 8126, 8165, 8182, 8183, 8204, 8218,
	8219, 8220, 8240, 8289, 8296, 8297, 8310, 8311, 8312, 8336, 8351, 8352,
	8353, 8362, 8363, 8364, 8397, 8419, 8420, 8421, 8445, 8446, 8447, 8452,
	8453, 8463, 8464, 8465, 8467, 8488, 8489, 8490, 8504, 8509, 8510, 8538,
	8539, 8540, 8551, 8616, 8617, 8630, 8660, 8661, 8662, 8684, 8690, 8692,
	8711, 8712, 8713, 8769, 8789, 8818, 8957, 9046, 9059, 9060, 9079, 9080,
	9081, 9639, 9640, 9782, 9783, 9784, 9785, 9790, 9799, 9800, 9801, 9802,
	9803, 9841, 9842, 9863, 9864, 9865, 9866, 9931, 9932, 9933, 9934, 9959,
	9960, 9961, 9977, 9981, 9982, 10067, 10129, 10130, 10131, 10682, 10683, 10684,
	10685, 10686, 10691, 10698, 10699, 10700, 10778, 10842, 10979, 10980, 10981, 11000,
	11016, 11017, 11018, 11263, 11385, 11386, 11387, 11419, 11420, 11421, 11434, 11437,
	11469, 11510, 11511, 11567, 11586, 11637, 11638, 11639, 11736, 11737, 11738, 11739,
	11755, 11821, 11822, 11980, 12030, 12176, 12177, 12178, 12191, 12245, 12282, 12307,
	12308, 12321, 12363, 12376, 12463, 12464, 12465, 12507, 12526, 12531, 12532, 12533,
	12540, 12655, 12656, 12659, 12668, 12669, 12670, 12677, 12695, 12706, 12707, 12708,
	12731, 12732, 12858, 12859, 12866, 12867, 12874, 12878, 12879, 12930, 12956, 12957,
	12981, 12982, 12983,
};

global_variable const u16 s_surfCarrierIndex[25] = {
	10399, 10400, 10401, 10402, 10403, 10404, 10405, 10406, 10407, 10408, 10409, 10410,
	10411, 10421, 10422, 10423, 10424, 10425, 10426, 10427, 10428, 10429, 10430, 10431,
	10432,
};

global_variable const u16 s_surfTurboIndex[72] = {
	374, 970, 3122, 3193, 3614, 3884, 4664, 5369, 6076, 6077, 6078, 6423,
	6542, 6543, 6676, 6909, 8129, 8130, 8771, 8772, 8958, 8962, 9347, 9348,
	9421, 9422, 10295, 10296, 10297, 10298, 10310, 10311, 10312, 10328, 10329, 10345,
	10346, 10380, 10381, 10382, 10480, 10481, 10497, 10498, 10795, 10796, 10811, 10812,
	11239, 11240, 11264, 11547, 11757, 11758, 11986, 12050, 12051, 12052, 12378, 12799,
	12800, 12801, 12814, 12815, 12816, 12832, 12833, 12834, 12851, 12852, 12853, 12854,
};

// THE TABLE. Terrain 5 is TERRAIN_STONE, 10 is TERRAIN_NONE, 0 is
// TERRAIN_ASPHALT, 1 and 2 are DIRT and GRASS (namespace_Level.h). All
// seven groups carry checkpoint 255 - that is what the track's integration notes
// require (INTEGRATION.md section 7: "All listed protections and platform helpers
// have checkpoint 255"), and that is what is in the container.
global_variable const struct TrackModSurfGroup s_surfGroup[SURF_GROUP_COUNT] = {
    {"invisible blocking walls", s_surfWallIndex, 18, 0x2001, 0x2001, 5, 5, SURF_POLICY_BLANK_HIDDEN, 1, 0, 0},
    {"invisible recovery killplanes", s_surfKillplaneIndex, 81, 0xe210, 0xe210, 0, 0, SURF_POLICY_BLANK_HIDDEN, 1, 1, 0},
    {"named visible maskgrabs", s_surfNamedGrabIndex, 106, 0x1a00, 0x2200, 0, 0, SURF_POLICY_VISIBLE_RECOVERY, 0, 1, 0},
    {"other recovery surfaces incl. water", s_surfOtherGrabIndex, 471, 0x1a00, 0x1a00, 0, 0, SURF_POLICY_VISIBLE_RECOVERY, 0, 1, 0},
    {"platform collision frames", &s_platQuadIndex[0][0], PLATFORM_QUAD_COUNT, 0x4001, 0x1801, 5, 5, SURF_POLICY_FRAME_SWITCHED, 0, 0, 1},
    {"other hidden carriers", s_surfCarrierIndex, 25, 0x4001, 0x4001, 10, 10, SURF_POLICY_HIDDEN_CARRIER, 0, 0, 0},
    {"native turbo triggers", s_surfTurboIndex, 72, 0xa040, 0xa040, 1, 2, SURF_POLICY_UNTOUCHED, 0, 0, 0},
};

// The proof that Reload's names give the same numbers as the raw values in
// collision-surfaces.json. It is here and not in a comment, so that a
// rename in namespace_Level.h stops the build instead of silently making the
// table wrong.
CTR_STATIC_ASSERT((QUADBLOCK_FLAG_COLLISION_SURFACE | QUADBLOCK_FLAG_REFLECT_SPLIT_LINE_1) == 0x2001);
CTR_STATIC_ASSERT((QUADBLOCK_FLAG_SKIP_WATER_LIST | QUADBLOCK_FLAG_NO_CAMERA_RESPAWN_PROBE | QUADBLOCK_FLAG_COLLISION_SURFACE |
                   QUADBLOCK_FLAG_KILL_PLANE | QUADBLOCK_FLAG_NO_COLLISION_RESPONSE) == 0xe210);
CTR_STATIC_ASSERT((QUADBLOCK_FLAG_GROUND | QUADBLOCK_FLAG_CAMERA_SEARCH | QUADBLOCK_FLAG_KILL_PLANE) == 0x1a00);
CTR_STATIC_ASSERT((QUADBLOCK_FLAG_COLLISION_SURFACE | QUADBLOCK_FLAG_KILL_PLANE) == 0x2200);
CTR_STATIC_ASSERT((QUADBLOCK_FLAG_SKIP_WATER_LIST | QUADBLOCK_FLAG_COLLISION_SURFACE | QUADBLOCK_FLAG_TRIGGER) == 0xa040);

// The flag distribution of the whole track, from the track's
// reference/authoring/static_wall_collision_layout.json. It is recounted at
// track start; if it differs, the container is a different one from
// the one this table is valid for, and the report says so.
struct TrackModSurfHistEntry
{
	u16 flags;
	s16 expected;
};

global_variable const struct TrackModSurfHistEntry s_surfHist[] = {
    {0x1800, 2429}, {0x1880, 208}, {0x1a00, 559}, {0x2000, 9601}, {0x2001, 18}, {0x2200, 18}, {0x4001, 148}, {0xa040, 72}, {0xe210, 81},
};

enum
{
	SURF_HIST_COUNT = sizeof(s_surfHist) / sizeof(s_surfHist[0]),
	SURF_QUADBLOCK_COUNT = 13134,
};

// --- THE COUNTING ----------------------------------------------------------
global_variable s64 s_surfAudits;                       // full checks at track start
global_variable s64 s_surfAuditQuads;                   // checked during them
global_variable s64 s_surfBadFlags[SURF_GROUP_COUNT];   // flag value outside the group
global_variable s64 s_surfBadTerrain[SURF_GROUP_COUNT]; // terrain outside the group
global_variable s64 s_surfBadCheckpoint[SURF_GROUP_COUNT];
global_variable s64 s_surfBadTexture[SURF_GROUP_COUNT]; // texture where there should be none, or the other way round
global_variable s64 s_surfBadRange[SURF_GROUP_COUNT];   // index outside the track
global_variable u16 s_surfSeenFlags[SURF_GROUP_COUNT];  // the first deviating value, for the report

global_variable s64 s_surfHistChecks;
global_variable s64 s_surfHistBad;      // a flag class did not have its count
global_variable s64 s_surfBlankSeen;    // zeroed quadblocks of the whole track
global_variable s64 s_surfBlankInGroup; // of those, with the flags of groups 1 or 2

// The watch per tick: the flags of the faces that must NOT change.
global_variable s64 s_surfWatchTicks;
global_variable s64 s_surfWatchQuads;
global_variable s64 s_surfWatchChanged[SURF_GROUP_COUNT];

// The visibility field.
global_variable s64 s_surfVisPasses;
global_variable s64 s_surfCarrierBitsCleared;      // bit was set and got cleared
global_variable s64 s_surfCarrierBitsClearedStale; // of those, in a frame without rebuild
global_variable s64 s_surfCarrierBitsAlreadyClear;
global_variable s64 s_surfCarrierBitsSet; // only with --show-platform-frames
global_variable s64 s_surfNamedBitsSet;   // named MaskGrabs that were in the visibility list
global_variable s64 s_surfNamedBitsClear;
global_variable u8 s_surfCarrierEverSeen[SURF_CARRIER_COUNT];
global_variable u8 s_surfNamedEverSeen[SURF_NAMED_GRAB_COUNT];

// The recovery.
global_variable s64 s_surfGrabArmed; // a face has set MASK_GRAB_REQUEST
global_variable s64 s_surfGrabArmedGroup[SURF_GROUP_COUNT];
global_variable s64 s_surfGrabArmedUnknown;     // triggered by a face outside all groups
global_variable s64 s_surfGrabStarts;           // capture: VehStuckProc_MaskGrab_Init
global_variable s64 s_surfGrabDone;             // completed respawn
global_variable s64 s_surfGrabDoneWithoutStart; // must not happen
global_variable int s_surfGrabInFlight[SURF_MAX_DRIVERS];
global_variable int s_surfGrabOpen;
global_variable int s_surfGrabOpenMax;

// Which group a quadblock belongs to, or -1. Linear over 896 entries;
// it is only asked on a recovery, and that happens no more than a few
// times per lap.
internal int TrackMod_SurfGroupOfIndex(int quadIndex)
{
	int group;
	int i;

	for (group = 0; group < SURF_GROUP_COUNT; group++)
	{
		const struct TrackModSurfGroup *g = &s_surfGroup[group];

		for (i = 0; i < g->count; i++)
		{
			if ((int)g->index[i] == quadIndex)
			{
				return group;
			}
		}
	}

	return -1;
}

internal int TrackMod_SurfQuadIndex(const struct QuadBlock *quad)
{
	struct GameTracker *gGT = sdata->gGT;
	struct mesh_info *mesh;
	int index;

	if ((quad == NULL) || (gGT == NULL) || (gGT->level1 == NULL))
	{
		return -1;
	}

	mesh = gGT->level1->ptr_mesh_info;
	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL))
	{
		return -1;
	}

	index = (int)(quad - mesh->ptrQuadBlockArray);
	if ((index < 0) || (index >= mesh->numQuadBlock))
	{
		return -1;
	}

	return index;
}

// THE FULL CHECK, once per track start.
//
// It decides nothing and it writes nothing. It reads through the seven groups
// and records where the container is not what the table takes it
// for - and it recounts the flag distribution of the whole track, so that
// a swapped container shows up before a rule goes at it.
internal void TrackMod_SurfAudit(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct mesh_info *mesh;
	int group;
	int i;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}

	mesh = gGT->level1->ptr_mesh_info;
	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL))
	{
		return;
	}

	s_surfAudits++;

	for (group = 0; group < SURF_GROUP_COUNT; group++)
	{
		const struct TrackModSurfGroup *g = &s_surfGroup[group];

		for (i = 0; i < g->count; i++)
		{
			int quadIndex = (int)g->index[i];
			const struct QuadBlock *quad;
			u16 flags;
			int blank;

			if (quadIndex >= mesh->numQuadBlock)
			{
				s_surfBadRange[group]++;
				continue;
			}

			quad = &mesh->ptrQuadBlockArray[quadIndex];
			flags = quad->quadFlags;
			s_surfAuditQuads++;

			if ((flags != g->flagsA) && (flags != g->flagsB))
			{
				if (s_surfBadFlags[group] == 0)
				{
					s_surfSeenFlags[group] = flags;
				}
				s_surfBadFlags[group]++;
			}

			if ((quad->terrain_type != g->terrainA) && (quad->terrain_type != g->terrainB))
			{
				s_surfBadTerrain[group]++;
			}

			if ((u8)quad->checkpointIndex != SURF_CHECKPOINT_NONE)
			{
				s_surfBadCheckpoint[group]++;
			}

			// The question of the zero rule, only asked here: does the face carry a
			// texture? The answer must fit the group - otherwise either
			// an invisible face would have become visible or a visible one
			// mute.
			blank = DrawLevelOvr1P_QuadBlockHasNoTexture(quad) ? 1 : 0;
			if (blank != (int)g->blankExpected)
			{
				s_surfBadTexture[group]++;
			}
		}
	}

	// The flag distribution of the whole track.
	{
		s64 counted[SURF_HIST_COUNT];
		int h;

		for (h = 0; h < SURF_HIST_COUNT; h++)
		{
			counted[h] = 0;
		}

		s_surfHistChecks++;

		for (i = 0; i < mesh->numQuadBlock; i++)
		{
			const struct QuadBlock *quad = &mesh->ptrQuadBlockArray[i];

			for (h = 0; h < SURF_HIST_COUNT; h++)
			{
				if (quad->quadFlags == s_surfHist[h].flags)
				{
					counted[h]++;
					break;
				}
			}

			if (DrawLevelOvr1P_QuadBlockHasNoTexture(quad))
			{
				s_surfBlankSeen++;

				// In groups 1 and 2 ALL entries must be zeroed.
				// If this number ends at 99 and the two groups report
				// no texture deviation, then they are exactly the same 99.
				if ((quad->quadFlags == 0x2001u) || (quad->quadFlags == 0xe210u))
				{
					s_surfBlankInGroup++;
				}
			}
		}

		if (mesh->numQuadBlock != SURF_QUADBLOCK_COUNT)
		{
			s_surfHistBad++;
		}

		for (h = 0; h < SURF_HIST_COUNT; h++)
		{
			if (counted[h] != (s64)s_surfHist[h].expected)
			{
				s_surfHistBad++;
			}
		}
	}
}

// THE WATCH, per tick.
//
// All groups except the 123 platform frames must not change their flags.
// That is the promise "the 72 turbo triggers stay unchanged" and at the same time
// the same promise for the other five - measured instead of claimed. 673
// comparisons per tick.
internal void TrackMod_SurfWatch(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct mesh_info *mesh;
	int group;
	int i;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}

	mesh = gGT->level1->ptr_mesh_info;
	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL))
	{
		return;
	}

	s_surfWatchTicks++;

	for (group = 0; group < SURF_GROUP_COUNT; group++)
	{
		const struct TrackModSurfGroup *g = &s_surfGroup[group];

		if (g->flagsMayChange)
		{
			continue;
		}

		for (i = 0; i < g->count; i++)
		{
			int quadIndex = (int)g->index[i];
			u16 flags;

			if (quadIndex >= mesh->numQuadBlock)
			{
				continue;
			}

			flags = mesh->ptrQuadBlockArray[quadIndex].quadFlags;
			s_surfWatchQuads++;

			if ((flags != g->flagsA) && (flags != g->flagsB))
			{
				s_surfWatchChanged[group]++;
			}
		}
	}
}

// THE PASS THROUGH THE VISIBILITY FIELD, per player and frame.
//
// Two things in one pass: the 25 carriers are cleared, the 106
// named MaskGrabs are only READ. The reading is the cross-check for the
// requirement "the 106 named ones stay visible": if not a single bit were set there over the whole
// run, either something about them would be broken or the measurement
// worthless.
internal void TrackMod_SurfVisPass(int *visFaceList, struct mesh_info *mesh, int visMemWasRebuilt)
{
	int i;

	s_surfVisPasses++;

	for (i = 0; i < SURF_CARRIER_COUNT; i++)
	{
		int quadIndex = (int)s_surfCarrierIndex[i];
		int bit;

		if (quadIndex >= mesh->numQuadBlock)
		{
			continue;
		}

		bit = 1 << (quadIndex & 0x1f);

		if (g_cfg_showPlatformFrames)
		{
			visFaceList[quadIndex >> 5] |= bit;
			s_surfCarrierBitsSet++;
			continue;
		}

		if ((visFaceList[quadIndex >> 5] & bit) == 0)
		{
			s_surfCarrierBitsAlreadyClear++;
			continue;
		}

		visFaceList[quadIndex >> 5] &= ~bit;
		s_surfCarrierEverSeen[i] = 1;
		s_surfCarrierBitsCleared++;
		if (!visMemWasRebuilt)
		{
			s_surfCarrierBitsClearedStale++;
		}
	}

	for (i = 0; i < SURF_NAMED_GRAB_COUNT; i++)
	{
		int quadIndex = (int)s_surfNamedGrabIndex[i];

		if (quadIndex >= mesh->numQuadBlock)
		{
			continue;
		}

		if ((visFaceList[quadIndex >> 5] & (1 << (quadIndex & 0x1f))) != 0)
		{
			s_surfNamedEverSeen[i] = 1;
			s_surfNamedBitsSet++;
		}
		else
		{
			s_surfNamedBitsClear++;
		}
	}
}

internal void TrackMod_SurfForget(void)
{
	int i;

	for (i = 0; i < SURF_MAX_DRIVERS; i++)
	{
		s_surfGrabInFlight[i] = 0;
	}

	s_surfGrabOpen = 0;
}

internal void TrackMod_SurfReset(void)
{
	TrackMod_SurfForget();
	TrackMod_SurfAudit();
}

// THE BOOKKEEPING OF THE RECOVERY.
//
// Three notifications, three places in the host:
//
//   armed   COLL.c - a face with KILL_PLANE was touched and has
//           set DRIVER_COLL_FLAG_MASK_GRAB_REQUEST. Here the face is
//           still known, so it is assigned to its group.
//   start   COLL.c - the capture. That is the one funnel through which
//           EVERY recovery goes, no matter which of the five places set the
//           bit.
//   done    VehStuckProc.c - the completed respawn: target search,
//           set-down, engine on. After that the driver drives again.
//
// Counting only happens on our track. On a disc track all
// three stay at zero - that is the cross-check.
internal int TrackMod_SurfTracking(void)
{
	struct GameTracker *gGT = sdata->gGT;

	return s_trackModActive && (gGT != NULL) && (gGT->levelID == s_trackModLevelID);
}

void NativeTrackMod_NoteMaskGrabArmed(const struct Driver *driver, const struct QuadBlock *quad)
{
	int quadIndex;
	int group;

	(void)driver;

	if (!TrackMod_SurfTracking())
	{
		return;
	}

	s_surfGrabArmed++;

	quadIndex = TrackMod_SurfQuadIndex(quad);
	group = (quadIndex >= 0) ? TrackMod_SurfGroupOfIndex(quadIndex) : -1;

	if (group < 0)
	{
		s_surfGrabArmedUnknown++;
		return;
	}

	s_surfGrabArmedGroup[group]++;
}

void NativeTrackMod_NoteMaskGrabStart(const struct Driver *driver)
{
	int id;

	if (!TrackMod_SurfTracking() || (driver == NULL))
	{
		return;
	}

	s_surfGrabStarts++;

	id = driver->driverID;
	if ((id >= 0) && (id < SURF_MAX_DRIVERS))
	{
		s_surfGrabInFlight[id]++;
	}

	s_surfGrabOpen++;
	if (s_surfGrabOpen > s_surfGrabOpenMax)
	{
		s_surfGrabOpenMax = s_surfGrabOpen;
	}
}

void NativeTrackMod_NoteMaskGrabDone(const struct Driver *driver)
{
	int id;

	if (!TrackMod_SurfTracking() || (driver == NULL))
	{
		return;
	}

	s_surfGrabDone++;

	id = driver->driverID;
	if ((id >= 0) && (id < SURF_MAX_DRIVERS) && (s_surfGrabInFlight[id] > 0))
	{
		s_surfGrabInFlight[id]--;
		s_surfGrabOpen--;
	}
	else
	{
		// A completed respawn without a capture. Can happen when the
		// capture was still on the previous track; in a closed
		// run it would be a bug.
		s_surfGrabDoneWithoutStart++;
	}
}

internal void TrackMod_SurfReport(void)
{
	int group;
	int i;
	int carriersSeen = 0;
	int namedSeen = 0;
	s64 badTotal = 0;

	for (i = 0; i < SURF_CARRIER_COUNT; i++)
	{
		carriersSeen += s_surfCarrierEverSeen[i];
	}

	for (i = 0; i < SURF_NAMED_GRAB_COUNT; i++)
	{
		namedSeen += s_surfNamedEverSeen[i];
	}

	for (group = 0; group < SURF_GROUP_COUNT; group++)
	{
		badTotal += s_surfBadFlags[group] + s_surfBadTerrain[group] + s_surfBadCheckpoint[group] + s_surfBadTexture[group] + s_surfBadRange[group];
	}

	Platform_Log("[CTR Surf] at exit: %lld audit(s) over %d group(s), %lld quadblock(s) checked, %lld deviation(s); %lld histogram check(s), %lld "
	             "mismatch(es); %lld blank quadblock(s), %lld of them in the two blank groups\n",
	             s_surfAudits, SURF_GROUP_COUNT, s_surfAuditQuads, badTotal, s_surfHistChecks, s_surfHistBad, s_surfBlankSeen, s_surfBlankInGroup);

	for (group = 0; group < SURF_GROUP_COUNT; group++)
	{
		const struct TrackModSurfGroup *g = &s_surfGroup[group];

		Platform_Log("[CTR Surf] at exit:   %-36s %4d quad(s), flags 0x%04x%s - bad flags %lld (first 0x%04x), terrain %lld, checkpoint %lld, "
		             "texture %lld, range %lld; %lld flag change(s) over %lld tick(s)\n",
		             g->name, (int)g->count, (unsigned)g->flagsA, (g->flagsB != g->flagsA) ? "/see below" : "", s_surfBadFlags[group],
		             (unsigned)s_surfSeenFlags[group], s_surfBadTerrain[group], s_surfBadCheckpoint[group], s_surfBadTexture[group],
		             s_surfBadRange[group], s_surfWatchChanged[group], s_surfWatchTicks);
	}

	Platform_Log("[CTR Surf] at exit: hidden carriers - %lld vis pass(es), %lld bit(s) cleared (%lld of them without a rebuild), %lld already "
	             "clear, %lld forced on; %d of %d carrier(s) were ever in the list\n",
	             s_surfVisPasses, s_surfCarrierBitsCleared, s_surfCarrierBitsClearedStale, s_surfCarrierBitsAlreadyClear, s_surfCarrierBitsSet,
	             carriersSeen, SURF_CARRIER_COUNT);

	Platform_Log("[CTR Surf] at exit: named maskgrabs - %lld bit(s) set, %lld clear; %d of %d were ever in the list\n", s_surfNamedBitsSet,
	             s_surfNamedBitsClear, namedSeen, SURF_NAMED_GRAB_COUNT);

	Platform_Log("[CTR Surf] at exit: recovery - %lld armed by a surface (killplanes %lld, named %lld, other %lld, outside every group %lld), %lld "
	             "capture(s), %lld completed respawn(s), %d still open (worst %d), %lld completed without a capture\n",
	             s_surfGrabArmed, s_surfGrabArmedGroup[SURF_GROUP_KILLPLANES], s_surfGrabArmedGroup[SURF_GROUP_NAMED_GRABS],
	             s_surfGrabArmedGroup[SURF_GROUP_OTHER_GRABS], s_surfGrabArmedUnknown, s_surfGrabStarts, s_surfGrabDone, s_surfGrabOpen,
	             s_surfGrabOpenMax, s_surfGrabDoneWithoutStart);
}

// ---------------------------------------------------------------------------
// THE LAST THREE: BOT CRATES, ROUTE BIT, SPRAY.
//
// Sources: the track's source (ps1_trackrom/src/sunset_crate_bots.c,
// sunset_crate_hit.c, wheel_spray_sunset.s) and the binary patch
// src/patches/bot_maskgrab_keep_route.bin, all three described in the track's
// integration notes (INTEGRATION.md sections 7 and 9).
//
// They have nothing to do with each other except their size: each is a few
// lines, and none builds anything new. All three hook into something Reload
// already has.
//
// 1. THE BOT CRATES. The game's crate callback already treats bots
//    correctly - RB_Crate.c lets the crate burst and afterwards hands out
//    NO item when ACTION_BOT is set, and
//    RB_CrateAny_GetDriver returns 1 for DYNAMIC_ROBOT_CAR, to which
//    the callback answers with "done, without reward". What is missing is the
//    CONTACT: bots do not go through the searcher that hits crates. This
//    block supplies it, with a box of 101 units around the bot,
//    whose centre sits 25 units above the vehicle centre.
//
// 2. THE ROUTE BIT. The author's patch changes, at PS1 address 0x80013924,
//    a load constant from 0xFFB0 to 0xFFF0 - that is exactly bit 0x40,
//    BOT_FLAG_BOSS_PATH_REQUESTED, which the MaskGrab recovery otherwise wipes
//    away as well. In Reload the same mask is a list of names in BOTS.c.
//    "It matters at platform branches" (INTEGRATION.md section 7): a bot
//    that has requested the branch and gets recovered on the way there
//    otherwise forgets the wish and keeps driving the old route.
//
//    TRACK-LOCAL, NOT GLOBAL. The bit is not dead in Reload: PickupBots.c
//    sets it for the boss in adventure, and BOTS.c reads it. A global change would thus also alter boss races on 25
//    disc tracks - for a gain that only arises on the three bot routes of
//    Sunset Vista. So the recovery asks, and outside
//    our track the answer is the same as before: keep nothing.
//
// 3. THE SPRAY. Cosmetic, and kept cheap as in the source: only
//    every fourth frame, only while fewer than 96 of the 128 particle slots
//    are in use, only for driver 0, only on a face with
//    weather_intensity other than zero. It uses the game's terrain emitter
//    with the existing set emSet_SnowLR. It turns NO road into
//    water and changes no grip - the faces keep their terrain.
// ---------------------------------------------------------------------------

enum
{
	// 76 + 25, from sunset_crate_bots.c. The box is cubic: the same
	// span in x, y and z.
	CRATE_CONTACT_RADIUS = 101,
	CRATE_CONTACT_DIAMETER = CRATE_CONTACT_RADIUS * 2,

	// The box does not sit around the vehicle centre, but 25 units
	// above it - at crate height.
	CRATE_CONTACT_Y_OFFSET = 25,

	// gGT->drivers has eight slots; the source goes through all eight.
	CRATE_DRIVER_SLOTS = 8,

	// The scratchpad slot the source writes as the raw address 0x1f800108.
	// VehStuckProc.c uses the same place for the same
	// kind of call.
	CRATE_SPS_OFFSET = 0x108,
};

enum
{
	// wheel_spray_sunset.s: frameCounter & 3, and 96 of 128 slots.
	SPRAY_FRAME_MASK = 3,
	SPRAY_PARTICLE_LIMIT = 96,

	// Four players at the console; gGT->drivers has no more slots for humans.
	SPRAY_MAX_PLAYERS = 4,
};

// --- THE COUNTING ----------------------------------------------------------
global_variable s64 s_crateTicks;
global_variable s64 s_crateBotsSeen;     // bots the pass looked at
global_variable s64 s_crateQuadsSeen;    // crates it looked at
global_variable s64 s_crateSkipBroken;   // skipped: scale zero, i.e. already smashed
global_variable s64 s_crateSkipHasThread; // skipped: the crate already has a thread
global_variable s64 s_crateHits;         // contacts delivered
global_variable s64 s_crateHitsWeapon;   // of those, on a question mark crate
global_variable s64 s_crateHitsFruit;    // of those, on a fruit crate
global_variable int s_crateCount;        // crates the setup found

// A SNAPSHOT VALUE IS NOT ENOUGH HERE. In one run, over 2599 ticks
// 5203 crates were skipped because of a thread, and still at
// two individually sampled moments the value was zero. A crate thread lives only briefly -
// it is created on contact and goes away again when the crate is done.
// So it is counted through ONCE per tick and the peak is kept; it is
// reset on every setup so that the restart cycles are comparable.
global_variable int s_crateThreadNow;
global_variable int s_crateThreadPeak;

global_variable s64 s_botRouteAsks;      // requests from the recovery
global_variable s64 s_botRouteKept;      // of those: the bit was set and stays
global_variable s64 s_botRouteAsksOther; // requests outside our track

global_variable s64 s_sprayTicks;
global_variable s64 s_spraySkipFrame;    // not the fourth frame
global_variable s64 s_spraySkipBusy;     // 96 or more particles in use
global_variable s64 s_spraySkipGround;   // no ground under the driver
global_variable s64 s_spraySkipDry;      // ground without weather_intensity
global_variable s64 s_sprayEmits;        // emissions
global_variable int s_sprayWorstWeather; // highest weather_intensity seen

// WHICH INSTANCES ARE CRATES.
//
// The source takes the last 28 of the 42 instances
// (crate_runtime_layout.h: SUNSET_LEV_INSTANCE_COUNT 42,
// SUNSET_CRATE_INSTANCE_COUNT 28) and reads them from ptrInstDefPtrArray, which
// after LevInstDef_UnPack carries instance pointers instead of InstDef pointers.
//
// Here we go over the InstDef list itself instead and select by
// MODEL. Two reasons: the list is never rewritten, so there is
// no pointer whose kind one has to guess - and "the last 28" is an
// assumption about the order in the container, while "model 7 or 8" is a
// statement about the thing itself. For this track both give the same
// 28: instances 14..41, of which 24 wcrate#00..#23 on crate_question (model ID 8 =
// PU_RANDOM_CRATE) and 4 fcrate#24..#27 on crate_fruit (ID 7 =
// PU_FRUIT_CRATE).
internal int TrackMod_CrateIsCrateModel(int modelID)
{
	return (modelID == PU_RANDOM_CRATE) || (modelID == PU_FRUIT_CRATE);
}

// THE CONTACT, as sunset_crate_hit.c delivers it.
//
// sps->Input1.modelID tells the callback WHAT hit the crate.
// DYNAMIC_ROBOT_CAR makes RB_CrateAny_GetDriver (RB_Crate.c) return
// 1 - and to that the callback answers with "crate burst,
// nobody gets anything". That is exactly what is wanted.
// Forward-declared: the counting sits with the memory measurement further down,
// it is needed here.
internal int TrackMod_BudgetCrateThreads(void);

internal void TrackMod_CrateHitBot(struct Instance *crate, struct Thread *botThread)
{
	struct ScratchpadStruct *sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, CRATE_SPS_OFFSET);

	sps->Input1.modelID = DYNAMIC_ROBOT_CAR;

	s_crateHits++;

	if (crate->model->id == PU_RANDOM_CRATE)
	{
		s_crateHitsWeapon++;
		RB_CrateWeapon_LInC(crate, botThread, sps);
	}
	else
	{
		s_crateHitsFruit++;
		RB_CrateFruit_LInC(crate, botThread, sps);
	}
}

internal void TrackMod_CrateBots(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	struct InstDef *defs;
	int numInstances;
	int driverIndex;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}

	level = gGT->level1;
	defs = level->ptrInstDefs;
	numInstances = (int)level->numInstances;

	if ((defs == NULL) || (numInstances <= 0))
	{
		return;
	}

	s_crateTicks++;

	s_crateThreadNow = TrackMod_BudgetCrateThreads();
	if (s_crateThreadNow > s_crateThreadPeak)
	{
		s_crateThreadPeak = s_crateThreadNow;
	}

	for (driverIndex = 0; driverIndex < CRATE_DRIVER_SLOTS; driverIndex++)
	{
		struct Driver *bot = gGT->drivers[driverIndex];
		int botX;
		int botY;
		int botZ;
		int i;

		if (bot == NULL)
		{
			continue;
		}

		if ((bot->actionsFlagSet & ACTION_BOT) == 0)
		{
			continue;
		}

		if ((bot->instSelf == NULL) || (bot->instSelf->thread == NULL))
		{
			continue;
		}

		s_crateBotsSeen++;

		botX = bot->posCurr.x >> 8;
		botY = (bot->posCurr.y >> 8) + CRATE_CONTACT_Y_OFFSET;
		botZ = bot->posCurr.z >> 8;

		for (i = 0; i < numInstances; i++)
		{
			struct InstDef *def = &defs[i];
			struct Instance *crate;
			int dx;
			int dy;
			int dz;

			if ((def->model == NULL) || !TrackMod_CrateIsCrateModel(def->model->id))
			{
				continue;
			}

			crate = def->ptrInstance;
			if (crate == NULL)
			{
				continue;
			}

			s_crateQuadsSeen++;

			// A thread means: the crate has already been touched once and
			// has its own collision. That is how the source handles it.
			if (crate->thread != NULL)
			{
				s_crateSkipHasThread++;
				continue;
			}

			if (crate->scale.x == 0)
			{
				s_crateSkipBroken++;
				continue;
			}

			dx = botX - crate->matrix.t[0];
			dy = botY - crate->matrix.t[1];
			dz = botZ - crate->matrix.t[2];

			// The same box test as in the source, with the same shift
			// into unsigned: a distance outside -101..+101 becomes larger than 202
			// when 101 is added, or wraps around below zero.
			if (((unsigned int)(dx + CRATE_CONTACT_RADIUS) > CRATE_CONTACT_DIAMETER) ||
			    ((unsigned int)(dy + CRATE_CONTACT_RADIUS) > CRATE_CONTACT_DIAMETER) ||
			    ((unsigned int)(dz + CRATE_CONTACT_RADIUS) > CRATE_CONTACT_DIAMETER))
			{
				continue;
			}

			TrackMod_CrateHitBot(crate, bot->instSelf->thread);
			break;
		}
	}
}

// THE SPRAY.
//
// wheel_spray_sunset.s reads sdata->frameCounter (gp + 0xA04),
// gGT->numParticles (gGT + 0x1CA4), gGT->drivers[0] (gGT + 0x24EC),
// d->currBlockTouching (Driver + 0xA0) and its weather_intensity
// (QuadBlock + 0x39), sets the matrix of the driver instance and calls
// VehEmitter_Terrain_Ground with data.emSet_SnowLR. All five fields have
// the same names in Reload; the assembly is only the reference here, not the
// template.
internal void TrackMod_SprayUpdate(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int playerCount;
	int player;

	if (gGT == NULL)
	{
		return;
	}

	s_sprayTicks++;

	if ((sdata->frameCounter & SPRAY_FRAME_MASK) != 0)
	{
		s_spraySkipFrame++;
		return;
	}

	// NOT ONLY DRIVER 0, BUT EVERY PLAYER AT THE CONSOLE.
	//
	// The source reads gGT->drivers[0] and stops there. That is enough as long as
	// exactly one is driving; in split screen player 2 would not see the spray
	// while player 1 has it - a visible difference between two
	// halves of the same picture. The loop runs over the players at the
	// console, at most four, and only every fourth frame. Bots stay
	// out: they would empty the particle pool that the limit below
	// is meant to protect.
	//
	// Side finding: in a demo run gGT->drivers[0] is a
	// bot, and bots have no currBlockTouching (COLL.c sets it only in the
	// player searcher). A demo run therefore never shows spray, no matter how many
	// faces carry weather.
	playerCount = gGT->numPlyrCurrGame;
	if (playerCount > SPRAY_MAX_PLAYERS)
	{
		playerCount = SPRAY_MAX_PLAYERS;
	}

	for (player = 0; player < playerCount; player++)
	{
		struct Driver *d = gGT->drivers[player];
		struct QuadBlock *ground;

		// 32 of the 128 slots stay free for exhaust, hazards and weapons.
		// The question is inside the loop, not before it: otherwise four players
		// emit four times after the limit held once.
		if (gGT->numParticles >= SPRAY_PARTICLE_LIMIT)
		{
			s_spraySkipBusy++;
			break;
		}

		if ((d == NULL) || (d->instSelf == NULL))
		{
			continue;
		}

		ground = d->currBlockTouching;
		if (ground == NULL)
		{
			s_spraySkipGround++;
			continue;
		}

		if (ground->weather_intensity == 0)
		{
			s_spraySkipDry++;
			continue;
		}

		if ((int)ground->weather_intensity > s_sprayWorstWeather)
		{
			s_sprayWorstWeather = (int)ground->weather_intensity;
		}

		gte_SetRotMatrix(&d->instSelf->matrix);
		gte_SetTransMatrix(&d->instSelf->matrix);
		VehEmitter_Terrain_Ground(d, &data.emSet_SnowLR[0]);

		s_sprayEmits++;
	}
}

// THE ROUTE BIT, asked from the recovery.
//
// Answer: the bits BOTS.c should NOT clear. Outside our
// track zero - there the recovery stays word for word the old one.
u32 NativeTrackMod_BotRecoveryKeepFlags(const struct Driver *bot)
{
	if (!TrackMod_SurfTracking())
	{
		s_botRouteAsksOther++;
		return 0;
	}

	s_botRouteAsks++;

	if ((bot != NULL) && ((bot->botData.botFlags & BOT_FLAG_BOSS_PATH_REQUESTED) != 0))
	{
		s_botRouteKept++;
	}

	return BOT_FLAG_BOSS_PATH_REQUESTED;
}

internal void TrackMod_LastThreeForget(void)
{
	s_crateCount = 0;
	s_crateThreadNow = 0;
	s_crateThreadPeak = 0;
}

internal void TrackMod_LastThreeReset(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	int i;

	s_crateCount = 0;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return;
	}

	level = gGT->level1;
	if (level->ptrInstDefs == NULL)
	{
		return;
	}

	for (i = 0; i < (int)level->numInstances; i++)
	{
		struct InstDef *def = &level->ptrInstDefs[i];

		if ((def->model != NULL) && TrackMod_CrateIsCrateModel(def->model->id))
		{
			s_crateCount++;
		}
	}

	Platform_Log("[CTR Crate] %d crate instance(s) found among %d LEV instance(s)\n", s_crateCount, (int)level->numInstances);
}

internal void TrackMod_LastThreeReport(void)
{
	Platform_Log("[CTR Crate] at exit: %d crate(s) in the level; %lld tick(s), %lld bot pass(es), %lld crate look(s) - %lld skipped for a thread, "
	             "%lld for a broken crate; %lld contact(s) delivered (%lld weapon, %lld fruit); %d crate(s) had a thread at most in the last "
	             "cycle\n",
	             s_crateCount, s_crateTicks, s_crateBotsSeen, s_crateQuadsSeen, s_crateSkipHasThread, s_crateSkipBroken, s_crateHits,
	             s_crateHitsWeapon, s_crateHitsFruit, s_crateThreadPeak);

	Platform_Log("[CTR BotRoute] at exit: %lld recovery ask(s) on this track, %lld of them kept a pending route request; %lld ask(s) elsewhere, "
	             "all answered with nothing kept\n",
	             s_botRouteAsks, s_botRouteKept, s_botRouteAsksOther);

	Platform_Log("[CTR Spray] at exit: %lld tick(s) - %lld not the fourth frame, %lld with 96+ particles busy, %lld with no ground, %lld on dry "
	             "ground; %lld emit(s), strongest weather intensity seen %d\n",
	             s_sprayTicks, s_spraySkipFrame, s_spraySkipBusy, s_spraySkipGround, s_spraySkipDry, s_sprayEmits, s_sprayWorstWeather);
}


// ---------------------------------------------------------------------------
// THE SNAPSHOT FOR THE MEMORY MEASUREMENT. READ ONLY.
//
// native_budget.c calls this before and after every restart and every
// track change. Here is what a leak in the track code would show:
// live instances per model, crates with a thread, the state of the
// icon slot and the visibility bits of the 148 faces this module
// clears.
//
// The instances are counted from THE SAME list the renderer takes
// its own from - gGT->JitPools.instance.taken. A counter of our own would
// count what we BELIEVE we create.
// ---------------------------------------------------------------------------

internal int TrackMod_BudgetCountByModel(const char *modelName)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Item *item;
	int found = 0;

	if (gGT == NULL)
	{
		return 0;
	}

	for (item = (struct Item *)LIST_GetFirstItem(&gGT->JitPools.instance.taken); item != NULL; item = (struct Item *)LIST_GetNextItem(item))
	{
		const struct Instance *inst = (const struct Instance *)item;

		if ((inst->model != NULL) && (strncmp(inst->model->name, modelName, MODEL_NAME_BYTE_COUNT) == 0))
		{
			found++;
		}
	}

	// AND THE SECOND LIST. The 42 instances of the LEV do NOT hang on
	// pool.taken: INSTANCE_LevInitAll takes them out of the free list with
	// LIST_RemoveFront and does not link them in anywhere again. They can only be
	// found through their InstDefs - bats and crates would otherwise be invisible to
	// this count.
	if ((gGT->level1 != NULL) && (gGT->level1->ptrInstDefs != NULL))
	{
		int i;

		for (i = 0; i < (int)gGT->level1->numInstances; i++)
		{
			const struct InstDef *def = &gGT->level1->ptrInstDefs[i];

			if ((def->ptrInstance != NULL) && (def->model != NULL) && (strncmp(def->model->name, modelName, MODEL_NAME_BYTE_COUNT) == 0))
			{
				found++;
			}
		}
	}

	return found;
}

internal int TrackMod_BudgetCrateThreads(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *level;
	int withThread = 0;
	int i;

	if ((gGT == NULL) || (gGT->level1 == NULL) || (gGT->level1->ptrInstDefs == NULL))
	{
		return 0;
	}

	level = gGT->level1;

	for (i = 0; i < (int)level->numInstances; i++)
	{
		const struct InstDef *def = &level->ptrInstDefs[i];

		if ((def->model == NULL) || !TrackMod_CrateIsCrateModel(def->model->id))
		{
			continue;
		}

		if ((def->ptrInstance != NULL) && (def->ptrInstance->thread != NULL))
		{
			withThread++;
		}
	}

	return withThread;
}

// How many of the 148 faces this module keeps invisible have their bit
// set RIGHT NOW. Across a track change this is the question of
// whether cleared bits stick; the answer must be that after the
// change nothing can be read here any more, because the field belongs to the
// new level.
internal int TrackMod_BudgetVisBitsSet(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct VisMem *visMem;
	struct mesh_info *mesh;
	const int *visFaceList;
	int set = 0;
	int index;
	int frame;

	if ((gGT == NULL) || (gGT->level1 == NULL))
	{
		return -1;
	}

	visMem = gGT->visMem1;
	mesh = gGT->level1->ptr_mesh_info;
	if ((visMem == NULL) || (mesh == NULL))
	{
		return -1;
	}

	visFaceList = visMem->visFaceList[0];
	if (visFaceList == NULL)
	{
		return -1;
	}

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		for (frame = 0; frame < PLATFORM_FRAME_COUNT; frame++)
		{
			int quadIndex = (int)s_platQuadIndex[index][frame];

			if ((quadIndex < mesh->numQuadBlock) && ((visFaceList[quadIndex >> 5] & (1 << (quadIndex & 0x1f))) != 0))
			{
				set++;
			}
		}
	}

	for (index = 0; index < SURF_CARRIER_COUNT; index++)
	{
		int quadIndex = (int)s_surfCarrierIndex[index];

		if ((quadIndex < mesh->numQuadBlock) && ((visFaceList[quadIndex >> 5] & (1 << (quadIndex & 0x1f))) != 0))
		{
			set++;
		}
	}

	return set;
}

void NativeTrackMod_BudgetLine(const char *what)
{
	struct GameTracker *gGT = sdata->gGT;
	int wall;
	int plat;
	int bat;
	int idle;
	int crateQ;
	int crateF;

	if (gGT == NULL)
	{
		return;
	}

	wall = TrackMod_BudgetCountByModel(s_trackModWallModel);
	plat = TrackMod_BudgetCountByModel(s_trackModPlatModel);
	bat = TrackMod_BudgetCountByModel(s_trackModBatModel);
	idle = TrackMod_BudgetCountByModel(s_trackModBatIdleModel);
	crateQ = TrackMod_BudgetCountByModel("crate_question");
	crateF = TrackMod_BudgetCountByModel("crate_fruit");

	Platform_Log("[CTR TrackMod] %-22s active %d; instances wall %d, plat %d, bat %d, idle %d, crate %d+%d; crate thread(s) %d now, %d at most "
	             "this cycle; icon slot %s; vis bits set %d of %d\n",
	             what, s_trackModActive, wall, plat, bat, idle, crateQ, crateF, TrackMod_BudgetCrateThreads(), s_crateThreadPeak,
	             (gGT->iconGroup[FLAME_ICON_GROUP_ID] == (struct IconGroup *)&s_flameIcons.group) ? "ours" : "not ours",
	             TrackMod_BudgetVisBitsSet(), PLATFORM_QUAD_COUNT + SURF_CARRIER_COUNT);
}

// HOW MUCH MEMORY THIS MODULE ITSELF OCCUPIES. Everything fixed in the module, none
// of it grows at runtime.
void NativeTrackMod_BudgetStatic(void)
{
	unsigned platList = (unsigned)sizeof(s_platQuadIndex);
	unsigned surfList = (unsigned)(sizeof(s_surfWallIndex) + sizeof(s_surfKillplaneIndex) + sizeof(s_surfNamedGrabIndex) +
	                               sizeof(s_surfOtherGrabIndex) + sizeof(s_surfCarrierIndex) + sizeof(s_surfTurboIndex));
	unsigned icons = (unsigned)sizeof(s_flameIcons);
	unsigned tables = (unsigned)(sizeof(s_platStart) + sizeof(s_platEnd) + sizeof(s_flameBrazier) + sizeof(s_surfGroup) + sizeof(s_surfHist) +
	                             sizeof(s_surfCarrierEverSeen) + sizeof(s_surfNamedEverSeen));

	Platform_Log("[CTR TrackMod] at exit: static bytes - platform index list %u, surface index lists %u (773 entries), flame icon block %u, other "
	             "tables %u; %u in all\n",
	             platList, surfList, icons, tables, platList + surfList + icons + tables);
}

// ---------------------------------------------------------------------------
// THE ENTRY POINTS
// ---------------------------------------------------------------------------

// THROW AWAY, DO NOT CLEAN UP.
//
// Called from MainMain.c, in the restart branch right after
// MEMPACK_PopState. At this point the memory the instance lived in
// has already been given back - cleaning up on it would be an access to
// foreign ground. So the pointers are only forgotten; the host has already
// cleared things away.
void NativeTrackMod_Release(void)
{
	TrackMod_ArmReport();
	s_modReleases++;

	s_trackModActive = 0;
	s_modWasPaused = 0;
	TrackMod_WallForget();
	TrackMod_BatForget();
	TrackMod_FlameForget();
	TrackMod_SurfForget();
	TrackMod_LastThreeForget();

	// The frames are NOT restored here. They lie in the LEV, which
	// survives the PopState (MainMain.c repacks the InstDefs of the
	// same mesh_info right after) - but the restore still belongs in the
	// setup, not in the teardown: only there is it certain that the track is
	// ours again afterwards. Whoever restores here and then loads another track
	// has written into foreign level data.
	TrackMod_PlatForget();
}

// SET UP.
//
// Called from MainMain.c, in state 1 - the first frame after loading
// is done. This state is reached on BOTH paths: after a
// fresh load and after a restart (state 2 sets
// mainGameState = 1 at its end). One place, two paths - that is why
// the setup lives here and not the same code twice.
//
// FORGETTING ALWAYS COMES FIRST. Even if Release already ran. The case where
// that matters is the track change: leave Sunset Vista, load another
// track - then Reset hits a scaffold that still holds pointers into the
// old LEV, and Release never got its turn. s_modResetWhileLive counts exactly
// this case, so that it is visible in the report and not only caught.
void NativeTrackMod_Reset(void)
{
	struct GameTracker *gGT = sdata->gGT;

	TrackMod_ArmReport();
	s_modResets++;

	if (s_trackModActive)
	{
		s_modResetWhileLive++;
	}

	s_trackModActive = 0;
	s_modWasPaused = 0;
	TrackMod_WallForget();
	TrackMod_PlatForget();
	TrackMod_BatForget();
	TrackMod_FlameForget();
	TrackMod_SurfForget();
	TrackMod_LastThreeForget();

	if ((gGT == NULL) || !NativeTrack_ActiveForLevel(gGT->levelID))
	{
		s_modDeclined++;
		return;
	}

	// SINCE THE PLATFORMS WERE ADDED THERE ARE SEVERAL ACTORS HERE, and they ask
	// separately. Before that the wall model alone decided whether any track code
	// runs at all; that would no longer work, because a track may have platforms without a door.
	// The rule has stayed the same, only one step finer: the host releases the
	// track (NativeTrack_ActiveForLevel), and each actor checks its
	// own model. Whoever finds none stays away - the others run anyway.
	if ((TrackMod_FindModel(s_trackModWallModel) == NULL) && (TrackMod_FindModel(s_trackModPlatModel) == NULL) &&
	    (TrackMod_FindModel(s_trackModBatModel) == NULL))
	{
		s_modDeclined++;
		Platform_Log("[CTR TrackMod] level %d carries a container ('%s') but none of '%s', '%s', '%s' - no track code runs here\n",
		             gGT->levelID, NativeTrack_LoadedName(), s_trackModWallModel, s_trackModPlatModel, s_trackModBatModel);
		return;
	}

	s_trackModActive = 1;
	s_trackModLevelID = gGT->levelID;
	s_modDetected++;

	// THE SURFACES ARE CHECKED FIRST, BEFORE ANY ACTOR.
	//
	// The check describes the container as it was loaded. If it stood
	// further down, TrackMod_PlatBirth would already have switched three of the 123 frames from
	// 0x4001 to 0x1801, and the flag distribution of the track would
	// no longer be that of the container - an earlier version of this code did
	// that and reported one deviation: 145 instead of 148 at 0x4001.
	// The surfaces create nothing, so moving them forward costs nothing either.
	TrackMod_SurfReset();

	// The crates are already in the LEV; here we only recount how many
	// there are, so the report can say so.
	TrackMod_LastThreeReset();

	TrackMod_WallBirth();

	// Recount BEFORE the setup what the last run left behind. After
	// a restart that is the position the platforms were in when the
	// driver gave up - MEMPACK_PopState only gives back the memory ABOVE the
	// LEV, the flags in the 123 frames are still as they were. That is exactly
	// the reason why they have to be restored here, and the two lines in the
	// log are the proof of it.
	if (TrackMod_FindModel(s_trackModPlatModel) != NULL)
	{
		TrackMod_PlatLogCensus("before reset");
		TrackMod_PlatBirth();
		TrackMod_PlatLogCensus("after reset ");
	}

	// The bats are ALREADY BORN at this point - their post-step
	// hangs off INSTANCE.c and ran during loading. Here only their
	// pointers are collected and the sound slot is reset.
	TrackMod_BatReset();

	// The fire bowls need no model - they need the AnimTex records
	// 40..43. Whether they are there is checked by the setup itself.
	TrackMod_FlameReset();

	Platform_Log("[CTR TrackMod] level %d is '%s' - track code is on; wall instance '%s' %s (%d alive), %d platform instance(s)\n", gGT->levelID,
	             NativeTrack_LoadedName(), s_trackModWallInstName, (s_wallInst != NULL) ? "born" : "away", TrackMod_CountLiveWallInstances(),
	             (s_platInst[0] != NULL) + (s_platInst[1] != NULL) + (s_platInst[2] != NULL));
}

// THE LEVEL UNDER THE SCAFFOLD HAS CHANGED.
//
// The exit via CHANGE LEVEL does NOT go through the restart branch - no
// MEMPACK_PopState, hence no Release either. Until the next level is loaded
// and Reset runs again, the scaffold would still be "active", and its
// pointers point to a LEV that is going away. In practice
// this window is closed, because no game logic runs during loading -
// but that is a property of the host's state machine and not a promise
// to this module. The level number is cheap and turns it into one.
internal int TrackMod_LevelStillOurs(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!s_trackModActive)
	{
		return 0;
	}

	if ((gGT == NULL) || (gGT->levelID != s_trackModLevelID))
	{
		s_modLevelLeft++;
		s_trackModActive = 0;
		s_modWasPaused = 0;
		TrackMod_WallForget();
		TrackMod_PlatForget();
		TrackMod_BatForget();
		TrackMod_FlameForget();
		TrackMod_SurfForget();
		TrackMod_LastThreeForget();
		return 0;
	}

	return 1;
}

// BEFORE THE GAME LOGIC. The three platforms move here. They
// must have set their position BEFORE the driving physics computes against it -
// the track's integration notes (INTEGRATION.md section 2) say so explicitly, and the
// reason is solid: whoever switches only after the physics leaves a driver standing for one
// frame on a frame that no longer carries.
//
// THE PAUSE IS MEASURED SEPARATELY HERE, not via a plain early return, for
// the same reason as with the door: what must be proven is not only "nothing was
// done", but that the platforms really stand still DURING the pause -
// model and carrying frame.
void NativeTrackMod_PreLogic(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int index;

	if (!TrackMod_LevelStillOurs())
	{
		s_modIdleTicks++;
		return;
	}

	if ((gGT->gameMode1 & LOADING) != 0)
	{
		s_modLoadingTicks++;
		return;
	}

	// The surface watch comes BEFORE the pause barrier: the flags of the
	// 673 unchangeable faces must not change during pause either,
	// and a counter that is blind during pause does not prove that.
	TrackMod_SurfWatch();

	if ((gGT->gameMode1 & PAUSE_ALL) != 0)
	{
		s_modPausedTicks++;
		s_platPauseTicks++;

		if (!s_platWasPaused)
		{
			s_platWasPaused = 1;
			s_platPauseSpanTicks = 0;
			s_platPauseState = s_platState;
			s_platPauseTimer = s_platTimerMS;
			for (index = 0; index < PLATFORM_COUNT; index++)
			{
				s_platPauseFrame[index] = s_platFrame[index];
				s_platPausePosX[index] = (s_platInst[index] != NULL) ? s_platInst[index]->matrix.t[0] : 0;
			}
			Platform_Log("[CTR Plat] pause began: state %d, timer %d ms, frames %d/%d/%d\n", s_platPauseState, s_platPauseTimer,
			             s_platPauseFrame[0], s_platPauseFrame[1], s_platPauseFrame[2]);
		}

		s_platPauseSpanTicks++;

		for (index = 0; index < PLATFORM_COUNT; index++)
		{
			int posX = (s_platInst[index] != NULL) ? s_platInst[index]->matrix.t[0] : 0;

			if ((s_platFrame[index] != s_platPauseFrame[index]) || (posX != s_platPausePosX[index]))
			{
				s_platPauseMoved++;
			}
		}

		// The recount also happens during pause. If another part of the game
		// touches the flags, it then shows up here and not only afterwards.
		TrackMod_PlatAudit();
		return;
	}

	if (s_platWasPaused)
	{
		s_platWasPaused = 0;

		if (gGT->elapsedTimeMS > s_platResumeStepMS)
		{
			s_platResumeStepMS = gGT->elapsedTimeMS;
		}

		Platform_Log("[CTR Plat] pause ended after %lld tick(s): state %d (was %d), timer %d ms (was %d), frames %d/%d/%d (were %d/%d/%d), "
		             "first step %d ms\n",
		             s_platPauseSpanTicks, s_platState, s_platPauseState, s_platTimerMS, s_platPauseTimer, s_platFrame[0], s_platFrame[1],
		             s_platFrame[2], s_platPauseFrame[0], s_platPauseFrame[1], s_platPauseFrame[2], gGT->elapsedTimeMS);
	}

	s_modPreTicks++;

	// The flames first, as the track's integration notes (INTEGRATION.md section 2)
	// list it: "update flames" comes before "update platform poses" and before the game logic.
	TrackMod_FlameUpdate();

	// Count, COMPUTE, count. The first pass checks what the previous tick
	// left behind and whether someone else has written into it since; the
	// second checks what this tick has just set.
	TrackMod_PlatAudit();
	TrackMod_PlatUpdate(gGT->elapsedTimeMS);
	TrackMod_PlatAudit();
}

// AFTER THE GAME LOGIC. Here the door moves and resolves its collision,
// after the drivers have been moved.
void NativeTrackMod_PostLogic(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int paused;
	int elapsedMS;

	if (!TrackMod_LevelStillOurs())
	{
		s_modIdleTicks++;
		return;
	}

	// THE PAUSE, MEASURED SEPARATELY.
	//
	// Not via a plain early return, because there is more to prove here than "nothing
	// was done": that the door stands still DURING the pause, and that no jump
	// occurs on resume. gGT->elapsedTimeMS keeps running during pause,
	// you see - MainFrame.c sets it to a fixed 32 ms there. Whoever does not
	// gate it moves the door right through the whole pause.
	paused = ((gGT->gameMode1 & PAUSE_ALL) != 0);

	if (paused)
	{
		s_modPausedTicks++;

		if (!s_modWasPaused)
		{
			s_modWasPaused = 1;
			s_modPauseSpans++;
			s_modPauseSpanTicks = 0;
			s_modPauseEnterState = s_wallState;
			s_modPauseEnterTimer = s_wallTimerMS;
			s_modPauseEnterPosZ = (s_wallInst != NULL) ? s_wallInst->matrix.t[2] : 0;

			Platform_Log("[CTR Wall] pause began: state %d, timer %d ms, z %d\n", s_modPauseEnterState, s_modPauseEnterTimer, s_modPauseEnterPosZ);
		}

		s_modPauseSpanTicks++;
		if (s_modPauseSpanTicks > s_modPauseLongestTicks)
		{
			s_modPauseLongestTicks = s_modPauseSpanTicks;
		}

		if ((s_wallInst != NULL) && (s_wallInst->matrix.t[2] != s_modPauseEnterPosZ))
		{
			s_modPauseWallMoved++;
		}

		return;
	}

	if ((gGT->gameMode1 & LOADING) != 0)
	{
		s_modLoadingTicks++;
		return;
	}

	elapsedMS = gGT->elapsedTimeMS;

	if (s_modWasPaused)
	{
		s_modWasPaused = 0;

		if (elapsedMS > s_modResumeStepMS)
		{
			s_modResumeStepMS = elapsedMS;
		}

		Platform_Log("[CTR Wall] pause ended after %lld tick(s): state %d (was %d), timer %d ms (was %d), z %d (was %d), first step %d ms\n",
		             s_modPauseSpanTicks, s_wallState, s_modPauseEnterState, s_wallTimerMS, s_modPauseEnterTimer,
		             (s_wallInst != NULL) ? s_wallInst->matrix.t[2] : 0, s_modPauseEnterPosZ, elapsedMS);
	}

	s_modPostTicks++;

	TrackMod_WallUpdate(elapsedMS);
	TrackMod_BatPostLogic();

	// INTEGRATION.md section 2 lists after that "optional rain-wheel effect" and
	// then "apply bot-crate contacts". Both need the moved driver, so
	// they are in the post-logic.
	TrackMod_SprayUpdate();

	NativeBudget_ScopeBegin(2);
	TrackMod_CrateBots();
	NativeBudget_ScopeEnd(2);
}

// ---------------------------------------------------------------------------
// ROUTE 1: MAKE A FACE INVISIBLE WITHOUT TAKING IT OUT OF THE COLLISION.
//
// Of the possible routes, this one was chosen. In short, why:
//
//   * It cannot narrow the collision. COLL.c reads the visibility field
//     nowhere - not even by accident, because it is never even passed
//     to the collision search. The promise "the face stays drivable" does not
//     depend on care here, but on the fact that there is no way there.
//   * It is already there and already per frame. The field is built anyway; here
//     only one pass with 123 clears follows.
//   * It does not touch the track data. No redirected texture pointers, no
//     modified container - an index list in the track code, nothing else.
//
// WHERE THE BIT IS READ: Ovr226_800a0f0c_SeedFullDynamicVisibilityScratch
// (226_00_DrawLevelOvr1P.c) fetches the word via
// visFaceList + ((blockID >> 3) & ~3) (retail masks with 0x1fc, see there). All four
// draw paths go through there - full-dynamic, split-ground, the BSP list by role
// and the water list - and the two-player path uses the same functions
// (227_00_DrawLevelOvr2P.c). A cleared bit
// means at all four places: not into the draw list.
//
// WHY THE PASS COMES AFTER THE if ON RENDER_FLAG_VISMEM_REFRESH_MASK.
// The field is not rewritten every frame, and it is also not touched the same way
// every frame:
//
//   * MainFrame_ReplacePackedVisList (MainFrame.c) OVERWRITES the whole
//     field - but only when the source has changed (visFaceSrc != camDC).
//     After that all bits are back the way the track ships them.
//   * MainFrame_VisMemAddDriverPVS (MainFrame.c) ORs the view of the
//     driver into it - that can happen in any frame and can set a cleared
//     bit again.
//   * And when RENDER_FLAG_VISMEM_REFRESH_MASK is not set, neither
//     of the two happens.
//
// A clearing pass INSIDE the if would thus not cover exactly the third case.
// That nothing goes wrong there today, because RENDER_FLAG_DRAW_LEVEL
// is itself part of the mask - a frame without rebuild does not draw a level either -,
// is a property of today's mask (namespace_Main.h) and not a
// promise. So the pass comes after the if and runs in EVERY frame.
// It costs nothing there: whatever is already cleared is read and
// skipped (s_platBitsAlreadyClear counts exactly that).
// ---------------------------------------------------------------------------

void NativeTrackMod_HideLevelFaces(int visMemWasRebuilt)
{
	struct GameTracker *gGT = sdata->gGT;
	struct VisMem *visMem;
	struct mesh_info *mesh;
	int player;
	int playerCount;
	int index;
	int frame;

	if (visMemWasRebuilt)
	{
		s_platHideRebuiltFrames++;
	}
	else
	{
		s_platHideStaleFrames++;
	}

	// THE PLATFORM ACTOR NO LONGER DECIDES ALONE. The
	// pass now carries two concerns: the 123 collision frames that belong to the
	// actor, and the 25 invisible carriers that belong to nobody.
	// So the barrier only asks whether any track code runs at all;
	// whether the platform part gets its turn is decided by s_platLive further down.
	if (!s_trackModActive)
	{
		return;
	}

	if ((gGT == NULL) || (gGT->levelID != s_trackModLevelID) || (gGT->level1 == NULL))
	{
		return;
	}

	visMem = gGT->visMem1;
	mesh = gGT->level1->ptr_mesh_info;
	if ((visMem == NULL) || (mesh == NULL))
	{
		return;
	}

	s_platHideCalls++;

	playerCount = gGT->numPlyrCurrGame;
	if (playerCount > 4)
	{
		playerCount = 4;
	}

	for (player = 0; player < playerCount; player++)
	{
		int *visFaceList = visMem->visFaceList[player];

		if (visFaceList == NULL)
		{
			continue;
		}

		s_platHidePasses++;

		{
			struct Driver *driver = gGT->drivers[player];

			if ((driver != NULL) && (driver->underDriver != NULL) && (mesh->ptrQuadBlockArray != NULL))
			{
				int underIndex = (int)(driver->underDriver - mesh->ptrQuadBlockArray);

				if ((underIndex >= 0) && (underIndex < mesh->numQuadBlock))
				{
					s_platCtrlChecks++;
					if ((visFaceList[underIndex >> 5] & (1 << (underIndex & 0x1f))) != 0)
					{
						s_platCtrlSet++;
					}
				}
			}
		}

		TrackMod_SurfVisPass(visFaceList, mesh, visMemWasRebuilt);

		if (!s_platLive)
		{
			continue;
		}

		for (index = 0; index < PLATFORM_COUNT; index++)
		{
			for (frame = 0; frame < PLATFORM_FRAME_COUNT; frame++)
			{
				int quadIndex = (int)s_platQuadIndex[index][frame];
				int bit;

				if (quadIndex >= mesh->numQuadBlock)
				{
					continue;
				}

				bit = 1 << (quadIndex & 0x1f);

				if (g_cfg_showPlatformFrames)
				{
					visFaceList[quadIndex >> 5] |= bit;
					s_platBitsSet++;
					continue;
				}

				if ((visFaceList[quadIndex >> 5] & bit) == 0)
				{
					s_platBitsAlreadyClear++;
					continue;
				}

				visFaceList[quadIndex >> 5] &= ~bit;
				s_platBitsCleared++;
				if (!visMemWasRebuilt)
				{
					s_platBitsClearedStale++;
				}
			}
		}
	}
}

void NativeTrackMod_NoteDrawExit(struct Instance *inst, int reason, u32 detail)
{
	int index;
	int isOurs = 0;

	if (!s_trackModActive || (inst == NULL) || (reason < 0) || (reason >= TRACKMOD_DRAW_EXIT_COUNT))
	{
		return;
	}

	if ((s_wallInst != NULL) && (inst == s_wallInst))
	{
		s_drawExitWall++;
		s_drawExitWallByReason[reason]++;
		isOurs = 1;
	}

	for (index = 0; index < PLATFORM_COUNT; index++)
	{
		if ((s_platInst[index] != NULL) && (inst == s_platInst[index]))
		{
			s_drawExitPlat[index]++;
			s_drawExitPlatByReason[reason]++;
			isOurs = 1;
		}
	}

	if (!isOurs)
	{
		return;
	}

	s_drawExitCount[reason]++;
	s_drawExitDetail[reason] = detail;

	if (reason == 12)
	{
		int x = (int)(s16)(detail & 0xffff);
		int y = (int)(s16)(detail >> 16);

		if (!s_drawExitHaveXY || (x < s_drawExitXMin))
		{
			s_drawExitXMin = x;
		}
		if (!s_drawExitHaveXY || (x > s_drawExitXMax))
		{
			s_drawExitXMax = x;
		}
		if (!s_drawExitHaveXY || (y < s_drawExitYMin))
		{
			s_drawExitYMin = y;
		}
		if (!s_drawExitHaveXY || (y > s_drawExitYMax))
		{
			s_drawExitYMax = y;
		}
		s_drawExitHaveXY = 1;
	}
}

internal void TrackMod_DrawExitReport(void)
{
	local_persist const char *const names[TRACKMOD_DRAW_EXIT_COUNT] = {
	    "reached a draw handler       ", "no model header at execute   ", "command list or colours null ", "no current frame            ",
	    "HIDE_MODEL set at execute    ", "setup callback not ported    ", "draw handler not ported      ", "not queued (no DRAW_SUCCESS) ",
	    "prim: projection rejected    ", "prim: texture table null     ", "prim: writer not ported      ", "prim: primitive memory full  ",
	    "prim: WRITTEN (detail x|y<<16)", "Normal entered (first word)  ",
	    "prim: rejected - GTE flag    ", "prim: rejected - NCLIP/cull  ", "prim: rejected - screen window", "prim: OT range null at writer",
	    "prim: bin BELOW window (bin|min<<16)", "prim: bin ABOVE window (bin|max<<16)", "prim: written WITH texture (word1)", "prim: written WITHOUT texture (colour)",
	    "prim: writer used (funcPtr[1])",
	};
	int reason;

	Platform_Log("[CTR Plat] at exit: draw funnel for the runtime-born instances (door %lld, platforms %lld/%lld/%lld visits)\n", s_drawExitWall,
	             s_drawExitPlat[0], s_drawExitPlat[1], s_drawExitPlat[2]);
	for (reason = 0; reason < TRACKMOD_DRAW_EXIT_COUNT; reason++)
	{
		Platform_Log("[CTR Plat]   %d %s %lld time(s) (door %lld, platforms %lld), last detail 0x%x\n", reason, names[reason], s_drawExitCount[reason],
		             s_drawExitWallByReason[reason], s_drawExitPlatByReason[reason], s_drawExitDetail[reason]);
	}
	Platform_Log("[CTR Plat]   written corner 0 over the run: x %d..%d, y %d..%d (%s)\n", s_drawExitXMin, s_drawExitXMax, s_drawExitYMin, s_drawExitYMax,
	             s_drawExitHaveXY ? "seen" : "none written");
}

internal void TrackMod_PlatReport(void)
{
	local_persist const char *const names[PLATFORM_STATE_COUNT] = {"retracted", "extending", "extended ", "retracting"};
	local_persist const int targets[PLATFORM_STATE_COUNT] = {
	    PLATFORM_RETRACTED_HOLD_MS,
	    (PLATFORM_COUNT - 1) * PLATFORM_STAGGER_MS + PLATFORM_MOVE_MS,
	    PLATFORM_EXTENDED_HOLD_MS,
	    (PLATFORM_COUNT - 1) * PLATFORM_STAGGER_MS + PLATFORM_MOVE_MS,
	};
	int state;

	Platform_Log("[CTR Plat] at exit: %lld instance(s) born, %lld birth(s) failed, %d alive now, %lld tick(s) computed, largest frame step %d ms\n",
	             s_platBirths, s_platBirthFails, TrackMod_CountLivePlatInstances(), s_platTicks, s_platStepMaxMS);
	TrackMod_DrawExitReport();

	Platform_Log("[CTR Plat] at exit: state dwell in ms, measured, target in brackets\n");
	for (state = 0; state < PLATFORM_STATE_COUNT; state++)
	{
		Platform_Log("[CTR Plat]   %-10s entered %lld time(s), dwell %d..%d ms [%d]\n", names[state], s_platStateEnters[state],
		             (s_platStateEnters[state] > 1) ? s_platDwellMin[state] : -1, (s_platStateEnters[state] > 1) ? s_platDwellMax[state] : -1,
		             targets[state]);
	}

	Platform_Log("[CTR Plat] at exit: %lld quadFlags write(s); collision frames reached %d..%d / %d..%d / %d..%d [0..%d]\n", s_platFlagWrites,
	             s_platHaveFrameSeen ? s_platFrameSeenMin[0] : -1, s_platHaveFrameSeen ? s_platFrameSeenMax[0] : -1,
	             s_platHaveFrameSeen ? s_platFrameSeenMin[1] : -1, s_platHaveFrameSeen ? s_platFrameSeenMax[1] : -1,
	             s_platHaveFrameSeen ? s_platFrameSeenMin[2] : -1, s_platHaveFrameSeen ? s_platFrameSeenMax[2] : -1, PLATFORM_FRAME_COUNT - 1);

	Platform_Log("[CTR Plat] at exit: %lld audit(s) over 3 platform(s) - %lld with other than exactly one live frame (worst %d), %lld where the live "
	             "frame was not the recorded one, %lld frame(s) carrying neither flag word\n",
	             s_platAudits, s_platAuditBadCount, s_platAuditWorstCount, s_platAuditBadFrame, s_platAuditBadFlags);

	Platform_Log("[CTR Plat] at exit: %lld pose check(s), %lld coordinate(s) off the collision frame, worst %d unit(s)\n", s_platPoseChecks,
	             s_platPoseMismatch, s_platPoseWorstDelta);

	Platform_Log("[CTR Plat] at exit: %lld hide call(s) - %lld frame(s) with a vismem rebuild, %lld without; %lld player pass(es), %lld bit(s) "
	             "cleared (%lld of them in a frame without a rebuild), %lld already clear\n",
	             s_platHideCalls, s_platHideRebuiltFrames, s_platHideStaleFrames, s_platHidePasses, s_platBitsCleared, s_platBitsClearedStale,
	             s_platBitsAlreadyClear);

	Platform_Log("[CTR Plat] at exit: control - the quad under the driver had its visibility bit set in %lld of %lld pass(es); "
	             "%lld bit(s) SET by --show-platform-frames\n",
	             s_platCtrlSet, s_platCtrlChecks, s_platBitsSet);

	Platform_Log("[CTR Plat] at exit: %lld tick(s) in pause, %lld in the last span; a platform moved during pause %lld time(s); largest first step "
	             "after a resume %d ms\n",
	             s_platPauseTicks, s_platPauseSpanTicks, s_platPauseMoved, s_platResumeStepMS);
}
