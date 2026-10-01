#include <common.h>
#include <math.h>

// ===========================================================================
// TRACK PREVIEW (an earlier version flew a camera over the track, which
// looked odd from the air).
//
// With --record-preview (only with --dev, together with --autoload-track)
// the preview shows the track from the normal driver camera. The player seat
// is driven by the AI - the same path as --autopilot (MainInit.c,
// BOTS_Driver_Convert), switched on here by --record-preview so that
// Reload Studio's command line stays the same. The kart is invisible
// (FlyIn_HideDriver), there are no opponents (MainInit_Drivers asks
// NativeFlyIn_PreviewAlone). The fly-in is already over during setup
// (container tracks never fly in, see below; START_OF_RACE is cleared in the
// first camera pass, MainInit.c:794). HUD, title bar and traffic light are off;
// the AI drives from the second tick on. After the warm-up every second frame goes
// to platform/native_preview.c, which writes the file and ends the game.
//
// WITHOUT A NAV PATH the AI cannot drive (BOTS_Driver_Convert aborts,
// BOTS.c:3176-3197). Then, as before, a path camera drives one lap in
// 10 s on a closed path made from the restart points, from the
// start line, at a fixed height. The camera is frozen for that (CAM_ThTick
// returns immediately, CAM.c:1856), the pose is in pushBuffer[0]
// per tick; drawing uses pos/rot (PushBuffer_UpdateFrustum,
// MainFrame_RenderFrame.c:420). If the restart points are missing as well, there
// is no preview.
//
// NO BUILT FLY-IN BEFORE THE TRAFFIC LIGHT: the levels start normally,
// without a camera flight - the path camera is only used for the preview.
// An earlier version also produced the fly-in for
// container tracks without their own camera path (its own
// SpawnType1 table with slot 3, swapped back after the fly-in). That
// path has been removed.
//
// CONTAINER TRACKS ALWAYS START WITHOUT A FLY-IN, even
// with their own camera path in the LEV (SpawnType1 slot 3, count >= 4): directly
// with the driver camera and the traffic light, in single races, Custom Cup, Crystal
// and CTR. That is decided by CAM_FollowDriver_Normal (CAM.c, check
// "st1->count < 4" under CTR_NATIVE): a container track takes the
// retail path of a track without a camera path. Disc tracks keep flying
// their retail fly-in. The LEV is not changed; the finish camera
// (slot 2) stays.
// ===========================================================================

#include <platform/native_preview.h>

void Platform_Log(const char *format, ...);
int NativeTrack_ActiveForLevel(int levelID);
const char *NativeTrack_LoadedName(void);

// --autopilot (platform/native_renderer.c), read in MainInit.c.
extern int g_cfg_autopilot;

#define FLYIN_MAX_POINTS 2048

// Path camera (only without a nav path), camera picture in world units.
#define FLYIN_LOOK_SHARE (1.0 / 24.0) // look ahead, share of the lap
#define FLYIN_LOOK_MIN 0x200
#define FLYIN_LOOK_MAX 0x800
#define FLYIN_TARGET_HEIGHT 0x40
#define FLYIN_SMOOTH 0x300 // smoothing: mean over +-0x300 arc length
#define FLYIN_SMOOTH_TAPS 8

// Warm-up before the first frame, in ticks (30 per second). The checkered flag of
// the load is gone after about 50 (measured: race from VBlank 104, flag until
// about 200). The AI still stands at the traffic light in the first tick (BOTS.c:1069),
// drives from the second one and is up to speed after 3 s. The path camera stands at
// the start line for that long.
#define PREVIEW_WARMUP_FRAMES 90
#define PREVIEW_LAP_FRAMES (NATIVE_PREVIEW_FRAMES * 2) // 10 s at 30 frames/s
#define PREVIEW_HEIGHT 0x500

// No screen: Particle_RenderList only draws a particle when
// driverID is -1 or it hits the camera (Particle.c:1158, cameras 0..3).
#define PREVIEW_NO_CAMERA 4

enum
{
	PREVIEW_OFF,
	PREVIEW_AI,   // the AI drives the invisible player seat, driver camera
	PREVIEW_PATH, // no nav path: path camera from the restart points
};

typedef struct
{
	double x, y, z;
} FlyInVec;

global_variable FlyInVec s_flyPts[FLYIN_MAX_POINTS];
global_variable double s_flyCum[FLYIN_MAX_POINTS + 1];
global_variable int s_flyNum;
global_variable double s_flyLen;

global_variable struct
{
	int mode;
	int tick;
	double anchor; // path camera: start line (arc length at the player's start position)
	double look;
} s_flyRec;

internal double FlyIn_Wrap(double s)
{
	while (s < 0.0)
	{
		s += s_flyLen;
	}
	while (s >= s_flyLen)
	{
		s -= s_flyLen;
	}
	return s;
}

internal FlyInVec FlyIn_PathAt(double s)
{
	FlyInVec r;
	int lo = 0;
	int hi = s_flyNum - 1;
	int i;
	const FlyInVec *a;
	const FlyInVec *b;
	double seg;
	double f;

	s = FlyIn_Wrap(s);

	// last i with s_flyCum[i] <= s
	while (lo < hi)
	{
		int mid = (lo + hi + 1) / 2;
		if (s_flyCum[mid] <= s)
		{
			lo = mid;
		}
		else
		{
			hi = mid - 1;
		}
	}

	i = lo;
	a = &s_flyPts[i];
	b = &s_flyPts[(i + 1) % s_flyNum];
	seg = s_flyCum[i + 1] - s_flyCum[i];
	f = (seg > 0.0) ? ((s - s_flyCum[i]) / seg) : 0.0;

	r.x = a->x + (b->x - a->x) * f;
	r.y = a->y + (b->y - a->y) * f;
	r.z = a->z + (b->z - a->z) * f;
	return r;
}

// Moving average along the curve: corners in the nav path become round,
// regardless of how fast the camera flies there.
internal FlyInVec FlyIn_PathSmooth(double s)
{
	FlyInVec r = {0.0, 0.0, 0.0};
	int k;

	for (k = -FLYIN_SMOOTH_TAPS; k <= FLYIN_SMOOTH_TAPS; k++)
	{
		FlyInVec p = FlyIn_PathAt(s + ((double)FLYIN_SMOOTH * k) / FLYIN_SMOOTH_TAPS);
		r.x += p.x;
		r.y += p.y;
		r.z += p.z;
	}

	r.x /= (2 * FLYIN_SMOOTH_TAPS + 1);
	r.y /= (2 * FLYIN_SMOOTH_TAPS + 1);
	r.z /= (2 * FLYIN_SMOOTH_TAPS + 1);
	return r;
}

// Arc length of the point on the path that lies closest to p.
internal double FlyIn_ArcNearest(const FlyInVec *p)
{
	double best = -1.0;
	double bestArc = 0.0;
	int i;

	for (i = 0; i < s_flyNum; i++)
	{
		const FlyInVec *a = &s_flyPts[i];
		const FlyInVec *b = &s_flyPts[(i + 1) % s_flyNum];
		double dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
		double len2 = dx * dx + dy * dy + dz * dz;
		double t = 0.0;
		double ex, ey, ez, d2;

		if (len2 > 0.0)
		{
			t = ((p->x - a->x) * dx + (p->y - a->y) * dy + (p->z - a->z) * dz) / len2;
			t = (t < 0.0) ? 0.0 : ((t > 1.0) ? 1.0 : t);
		}

		ex = a->x + dx * t - p->x;
		ey = a->y + dy * t - p->y;
		ez = a->z + dz * t - p->z;
		d2 = ex * ex + ey * ey + ez * ez;

		if ((best < 0.0) || (d2 < best))
		{
			best = d2;
			bestArc = s_flyCum[i] + (s_flyCum[i + 1] - s_flyCum[i]) * t;
		}
	}

	return bestArc;
}

internal void FlyIn_AddPoint(const SVec3 *pos)
{
	if (s_flyNum < FLYIN_MAX_POINTS)
	{
		s_flyPts[s_flyNum].x = pos->x;
		s_flyPts[s_flyNum].y = pos->y;
		s_flyPts[s_flyNum].z = pos->z;
		s_flyNum++;
	}
}

// Can the AI drive here? The same check as BOTS_InitNavPath
// (BOTS.c:114-166) and BOTS_Driver_Convert: a bot nav path with magic
// -0x1303 and more than one point.
internal int FlyIn_NavPathUsable(struct Level *lev)
{
	int k;

	if (lev->LevNavTable == NULL)
	{
		return 0;
	}

	for (k = 0; k < 3; k++)
	{
		struct NavHeader *nh = lev->LevNavTable[k];

		if ((nh != NULL) && (nh->magicNumber == -0x1303) && (nh->numPoints > 1))
		{
			return 1;
		}
	}

	return 0;
}

// Restart points: node 0 is the finish line, nextIndex_forward continues
// in driving direction (RB_Warpball.c:85-127).
internal int FlyIn_FromRestartPoints(struct Level *lev, char *source, int sourceSize)
{
	int cnt = lev->cnt_restart_points;
	int node = 0;
	int i;

	if ((lev->ptr_restart_points == NULL) || (cnt < 3))
	{
		return 0;
	}

	s_flyNum = 0;

	for (i = 0; i < cnt; i++)
	{
		FlyIn_AddPoint(&lev->ptr_restart_points[node].pos);
		node = lev->ptr_restart_points[node].nextIndex_forward;

		if ((node == 0) || (node >= cnt))
		{
			break;
		}
	}

	snprintf(source, sourceSize, "restart points (%d of %d on the forward chain)", s_flyNum, cnt);
	return (s_flyNum >= 3);
}

// Arc length per point and lap length of the closed path.
internal int FlyIn_Measure(void)
{
	int i;

	s_flyCum[0] = 0.0;
	for (i = 0; i < s_flyNum; i++)
	{
		const FlyInVec *a = &s_flyPts[i];
		const FlyInVec *b = &s_flyPts[(i + 1) % s_flyNum];
		double dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
		s_flyCum[i + 1] = s_flyCum[i] + sqrt(dx * dx + dy * dy + dz * dz);
	}
	s_flyLen = s_flyCum[s_flyNum];

	return (s_flyLen >= 1.0);
}

internal void FlyIn_RecordPrepare(struct Level *lev)
{
	char source[96];
	FlyInVec spawn;

	memset(&s_flyRec, 0, sizeof(s_flyRec));

	// The AI drives: MainInit_FinalizeInit turns the player seat into a bot
	// (--autopilot) and leaves out the opponents (NativeFlyIn_PreviewAlone). The
	// log line is written by the first tick, once it is certain that the seat drives.
	if (FlyIn_NavPathUsable(lev))
	{
		g_cfg_autopilot = 1;
		s_flyRec.mode = PREVIEW_AI;
		return;
	}

	if (!FlyIn_FromRestartPoints(lev, source, sizeof(source)) || !FlyIn_Measure())
	{
		NativePreview_RecordFail("no nav path and no restart points - no preview");
		return;
	}

	spawn.x = lev->DriverSpawn[0].pos.x;
	spawn.y = lev->DriverSpawn[0].pos.y;
	spawn.z = lev->DriverSpawn[0].pos.z;
	s_flyRec.anchor = FlyIn_ArcNearest(&spawn);
	s_flyRec.look = s_flyLen * FLYIN_LOOK_SHARE;
	s_flyRec.look = (s_flyRec.look < FLYIN_LOOK_MIN) ? FLYIN_LOOK_MIN : ((s_flyRec.look > FLYIN_LOOK_MAX) ? FLYIN_LOOK_MAX : s_flyRec.look);
	s_flyRec.mode = PREVIEW_PATH;

	Platform_Log("[CTR Preview] %s: no nav path - the AI cannot drive, recording with the path camera from the restart points (%s, lap %d) - %d "
	             "frames at %d/s after %d warm-up frames\n",
	             NativeTrack_LoadedName(), source, (int)s_flyLen, NATIVE_PREVIEW_FRAMES, NATIVE_PREVIEW_FPS, PREVIEW_WARMUP_FRAMES);
}

// First tick of the AI recording: does the player seat really drive as a bot?
internal int FlyIn_AiStart(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = gGT->drivers[0];
	char bots[24];
	int path;

	// NativePreview_RecordFail ends the game (Platform_QuitGame, exit) and
	// does not return, so nothing below runs without a bot in the seat.
	if ((d == NULL) || ((d->actionsFlagSet & ACTION_BOT) == 0))
	{
		NativePreview_RecordFail("the AI did not take over the player's seat - see the [CTR Debug] --autopilot line");
	}

	// MainInit_Drivers leaves out opponents; if some were there after all, the
	// line says so instead of claiming "no bots".
	if (gGT->numBotsNextGame == 0)
	{
		snprintf(bots, sizeof(bots), "no bots");
	}
	else
	{
		snprintf(bots, sizeof(bots), "%d bots", (int)gGT->numBotsNextGame);
	}

	path = d->botData.botPath;
	Platform_Log("[CTR Preview] %s: recording with the AI driver on nav path %d (%d points), invisible kart, %s - %d frames at %d/s after %d "
	             "warm-up frames\n",
	             NativeTrack_LoadedName(), path, (int)sdata->NavPath_ptrHeader[path]->numPoints, bots, NATIVE_PREVIEW_FRAMES, NATIVE_PREVIEW_FPS,
	             PREVIEW_WARMUP_FRAMES);
	return 1;
}

// The player seat invisible, per tick after the game logic and thus before
// drawing the same frame.
//
// The way retail hides a driver that the mask picks up from the ground
// (VehStuckProc.c:746, BOTS.c:2939): HIDE_MODEL on instSelf. That takes along
// the kart (RenderBucket_QueueExecute.c:5448), the wheels (DrawTires.c:802),
// the shadow (VehGroundShadow.c:438) and the exhaust smoke
// (VehEmitter.c:96). Anew every tick, because retail clears it when
// re-placing (BOTS.c:2403, VehStuckProc.c:609).
//
// What HIDE_MODEL does not reach, because retail only knows a hidden kart
// in the air:
//   - turbo flames, own instances in the TURBO stack (VehFire.c; the AI
//     starts with a turbo and takes turbo pads), VehTurbo_ThTick shows them
//     again (VehTurbo.c:216);
//   - the bow wave in water, d->wakeInst (VehPhysForce.c:1004);
//   - skid marks, drawn from d->skidmarkEnableFlags
//     (VehGroundSkids.c:237);
//   - dust, sparks, mud and smoke, particles with owner.driverInst ==
//     instSelf (VehEmitter.c, VehFrame.c, VehPhysForce.c, VehStuckProc.c).
//     They keep running, but get a screen that does not exist.
internal void FlyIn_HideDriver(struct Driver *d)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Instance *inst;
	struct Thread *t;
	struct Particle *p;

	if ((d == NULL) || (d->instSelf == NULL))
	{
		return;
	}
	inst = d->instSelf;
	inst->flags |= HIDE_MODEL;

	for (t = gGT->threadBuckets[TURBO].thread; t != NULL; t = t->siblingThread)
	{
		struct Turbo *turbo = (struct Turbo *)t->object;

		if (turbo->driver != d)
		{
			continue;
		}
		if (t->inst != NULL)
		{
			t->inst->flags |= HIDE_MODEL;
		}
		if (turbo->inst != NULL)
		{
			turbo->inst->flags |= HIDE_MODEL;
		}
	}

	if (d->wakeInst != NULL)
	{
		d->wakeInst->flags |= HIDE_MODEL;
	}

	d->skidmarkEnableFlags = 0;

	for (p = gGT->particleList_ordinary; p != NULL; p = p->next)
	{
		if (p->owner.driverInst == inst)
		{
			p->driverID = PREVIEW_NO_CAMERA;
		}
	}
}

// Path camera: pose per tick, p = tick since the warm-up.
internal void FlyIn_PosePathCamera(int p)
{
	struct GameTracker *gGT = sdata->gGT;
	struct PushBuffer *pb = &gGT->pushBuffer[0];
	double s = s_flyRec.anchor + (s_flyLen * (p % PREVIEW_LAP_FRAMES)) / PREVIEW_LAP_FRAMES;
	FlyInVec eye = FlyIn_PathSmooth(s);
	FlyInVec target = FlyIn_PathSmooth(s + s_flyRec.look);
	int dx, dy, dz;

	eye.y += PREVIEW_HEIGHT;
	target.y += FLYIN_TARGET_HEIGHT;

	gGT->cameraDC[0].flags |= CAMERA_FLAG_FROZEN;
	pb->pos.x = (s16)eye.x;
	pb->pos.y = (s16)eye.y;
	pb->pos.z = (s16)eye.z;

	// View direction as in CAM_StartLine_FlyIn (CAM.c:893-899).
	dx = (int)(eye.x - target.x);
	dy = (int)(eye.y - target.y);
	dz = (int)(eye.z - target.z);
	pb->rot.y = (s16)ratan2(dx, dz);
	pb->rot.x = (s16)(0x800 - (s16)ratan2(dy, SquareRoot0(dx * dx + dz * dz)));
	pb->rot.z = 0;
}

internal void FlyIn_RecordTick(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int k = s_flyRec.tick++;
	int p = (k < PREVIEW_WARMUP_FRAMES) ? 0 : (k - PREVIEW_WARMUP_FRAMES);

	if (s_flyRec.mode == PREVIEW_AI)
	{
		// The driver camera runs undisturbed (MainInit_FinalizeInit has
		// thawed it).
		if ((k == 0) && !FlyIn_AiStart())
		{
			s_flyRec.mode = PREVIEW_OFF;
			return;
		}
	}
	else
	{
		FlyIn_PosePathCamera(p);
	}

	FlyIn_HideDriver(gGT->drivers[0]);

	gGT->hudFlags &= ~(HUD_FLAG_RACE_HUD | HUD_FLAG_INTRO_RACE_TITLE_BARS);

	// The traffic light is not a HUD part (DotLights.c, drawn from timer -0x3bf).
	// Its end value (MainMain.c:374) hides it and lets the AI drive:
	// BOTS_ThTick_Drive only holds as long as the timer is above 0
	// (BOTS.c:1069). START_OF_RACE has been gone since setup (see above).
	gGT->trafficLightsTimer = -960;

	if ((k >= PREVIEW_WARMUP_FRAMES) && ((p % 2) == 0) && ((p / 2) < NATIVE_PREVIEW_FRAMES))
	{
		NativePreview_RecordFrameDue(p / 2);
	}
}

// For MainInit_Drivers: the AI preview drives alone, without opponents.
int NativeFlyIn_PreviewAlone(void)
{
	return (s_flyRec.mode == PREVIEW_AI);
}

// Before MainInit_FinalizeInit, freshly loaded as after a restart. Only
// with --record-preview does it set up the recording; otherwise it says in the log
// that the race starts without a fly-in (see above) and whether the LEV carries
// its own camera path, which is not flown then.
void NativeFlyIn_Prepare(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct Level *lev = gGT->level1;
	struct SpawnType1 *st1;

	s_flyRec.mode = PREVIEW_OFF;

	if ((lev == NULL) || !NativeTrack_ActiveForLevel(gGT->levelID) || ((gGT->gameMode1 & MAIN_MENU) != 0))
	{
		return;
	}

	if (g_cfg_recordPreview)
	{
		// The preview is recorded in a race; otherwise a clear reason
		// right away instead of 60 s of waiting (platform/native_preview.c).
		if ((gGT->gameMode1 & (BATTLE_MODE | CRYSTAL_CHALLENGE)) != 0)
		{
			NativePreview_RecordFail("the track started in battle or crystal mode - the preview is recorded in a race");
			return;
		}
		FlyIn_RecordPrepare(lev);
		return;
	}

	st1 = lev->ptrSpawnType1;
	Platform_Log("[CTR FlyIn] %s: %s\n", NativeTrack_LoadedName(),
	             ((st1 != NULL) && (st1->count >= 4) && ((ST1_GETPOINTERS(st1))[ST1_CAMERA_PATH] != NULL))
	                 ? "own camera path in the LEV, not flown - custom tracks start without a fly-in"
	                 : "no own camera path - custom tracks start without a fly-in");
}

// Per tick after the game logic: continues the preview recording.
void NativeFlyIn_Tick(void)
{
	if (s_flyRec.mode != PREVIEW_OFF)
	{
		FlyIn_RecordTick();
	}
}
