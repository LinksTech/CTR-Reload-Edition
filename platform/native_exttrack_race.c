#include <platform/native_exttrack_race.h>
#include <platform/engine.h>

// THE EXTERNAL TRACK'S RACE LOGIC - see include/platform/native_exttrack_race.h.
//
// WHAT. The section "race" of exttrack.json (written by the converter, every
// value already in world units and game angles) replaces three things of the
// donor level and nothing else:
//   - Level.DriverSpawn[8]  (include/namespace_Level.h:776-781): pos on the
//     collision floor, rot = (0, heading - 1024, 0) - VehBirth.c:153 adds the
//     quarter turn back;
//   - Level.ptr_restart_points / cnt_restart_points (:849-852): the checkpoint
//     table, which in this game is the lap's distance anchors AND the respawn
//     points at once (VehLap.c:139-180, VehStuckProc.c:158-202);
//   - Level.configFlags bit 2 (:797-801): "mask grab under y -128" of an
//     underwater level (COLL.c:1793) - the external track lies below it.
// The game logic reads them as from any retail level: VehBirth, VehLap,
// PlayLevel, COLL, VehStuckProc, CAM and BOTS are not changed. The checkpoint
// index of every quadblock comes with the collision (native_extcoll.c,
// attribute _CHECKPOINT of collision.glb).
//
// THE TABLE (variant R, P0_M3.md 2.3): node 0 on the finish line; per CNK
// restart point a node, and after each a direction node 1.5 m ahead along the
// AI line - the respawn looks from a node toward its next one
// (VehStuckProc.c:202), so it looks along the driving line. Quadblocks and nav
// frames name only line/restart nodes (restart_nodes).
//
// MEASURING (logs only, plus the one trigger of --dev-ext-respawn):
//   [CTR ExtTrack] race: ...                  the section taken (or refused, and why)
//   [CTR ExtTrack] race start: ...            laps, nodes, lap units
//   [CTR ExtTrack] spawn seat S slot I ...    D4: where the kart stands against the data
//   [CTR ExtTrack] lap driver D: A -> B ...   D6: every lap change of every driver
//   [CTR ExtTrack] respawn asked ...          D7: --dev-ext-respawn reached its frame
//   [CTR ExtTrack] respawn seat 0 ...         D7: where the kart landed against the node
//   [CTR ExtTrack] results screen ...         D6: the arcade results screen is up (+ snapshot)
//
// WITHOUT --ext-track nothing here is reached (the dispatcher returns first).

void Platform_ShotRequest(const char *path); // platform/native_platform.c
int Platform_GetVBlankCount(void);           // platform/native_platform.c
extern char g_cfg_shotName[PLATFORM_ARG_PATH_MAX];

enum
{
	EXTRACE_NODES_MAX = 254,
	EXTRACE_NO_NODE = 0xFF,
	EXTRACE_SPAWNS = 8,
	EXTRACE_COORD_LIMIT = 32700,
	EXTRACE_LINE_FAR = 32000,      // PlayLevel.c:8
	EXTRACE_LINE_MARGIN = 192,     // two ticks at top speed (tools/collision_gen/progress_sim.py:184)
	EXTRACE_ANGLE_FULL = 4096,
	EXTRACE_QUARTER_TURN = 1024,
	EXTRACE_CONFIG_UNDERWATER = 2, // Level.configFlags & 2, COLL.c:1793
	EXTRACE_RESPAWN_TRIES = 300,   // frames the trigger waits for a kart that can be grabbed
	EXTRACE_KIND_LINE = 0,
	EXTRACE_KIND_RESTART = 1,
	EXTRACE_KIND_DIR = 2,
};

typedef struct
{
	int nodeCount;
	int trackUnits;
	struct CheckpointNode nodes[EXTRACE_NODES_MAX];
	s16 wantYaw[EXTRACE_NODES_MAX]; // the AI line's heading at a line/restart node, -1 at a dir node
	u8 kind[EXTRACE_NODES_MAX];
	u8 carries[EXTRACE_NODES_MAX];  // 1 = listed in restart_nodes
	SVec3 spawnPos[EXTRACE_SPAWNS];
	s16 spawnYaw[EXTRACE_SPAWNS];
} ExtRaceData;

global_variable ExtRaceData s_extRace;
global_variable int s_extRaceReady;     // 1 = the table of s_extRace is in the level
global_variable u32 s_extRaceConfigBefore;
global_variable int s_extRaceFrame;     // logic frames of the race (no pause, no loading) since the level load
global_variable int s_extRaceStarted;
global_variable int s_extRaceLap[8];
global_variable int s_extRaceGrabbed;
global_variable int s_extRaceGrabCp;
global_variable int s_extRaceRespawnState; // 0 waiting, 1 asked, 2 done or given up
global_variable int s_extRaceRespawnTries;
global_variable int s_extRaceEndLogged;
global_variable int s_extRaceResultsLogged;

// --------------------------------------------------------------------------
// reading the section (pure: no level, no game state - the selftest runs it)

internal int ExtRace_Int(const ExtJsonDoc *doc, int node, int *out)
{
	const double v = ExtJson_Number(doc, node, 1e300);

	if ((ExtJson_Type(doc, node) != EXT_JSON_NUMBER) || (v != (double)(int)v))
	{
		return 0;
	}
	*out = (int)v;
	return 1;
}

internal int ExtRace_Pos(const ExtJsonDoc *doc, int node, SVec3 *out)
{
	double v[3];
	int k;

	if ((ExtJson_Count(doc, node) != 3) || (ExtJson_Numbers(doc, node, v, 3) != 3))
	{
		return 0;
	}
	for (k = 0; k < 3; k++)
	{
		if ((v[k] != (double)(int)v[k]) || (v[k] < -EXTRACE_COORD_LIMIT) || (v[k] > EXTRACE_COORD_LIMIT))
		{
			return 0;
		}
	}
	out->x = (s16)v[0];
	out->y = (s16)v[1];
	out->z = (s16)v[2];
	return 1;
}

#define EXTRACE_FAIL(...)                              \
	do                                                 \
	{                                                  \
		snprintf(why, (size_t)whySize, __VA_ARGS__);   \
		return 0;                                      \
	} while (0)

// The section "race" (node race of doc) into out. 1 = taken; 0 with why.
internal int ExtRace_ParseSection(const ExtJsonDoc *doc, int race, ExtRaceData *out, char *why, int whySize)
{
	int start, cps, rn, n, i, k, guard;

	memset(out, 0, sizeof(*out));
	if (ExtJson_Type(doc, race) != EXT_JSON_OBJECT)
	{
		EXTRACE_FAIL("no section \"race\"");
	}
	if (!ExtRace_Int(doc, ExtJson_Get(doc, race, "track_units"), &out->trackUnits) || ((out->trackUnits % 8) != 0) ||
	    (out->trackUnits <= EXTRACE_LINE_FAR + EXTRACE_LINE_MARGIN) || ((out->trackUnits / 8) > 0xFFFF))
	{
		EXTRACE_FAIL("track_units must be a multiple of 8 above %d and at most %d", EXTRACE_LINE_FAR + EXTRACE_LINE_MARGIN, 0xFFFF * 8);
	}

	start = ExtJson_Get(doc, race, "start");
	if ((ExtJson_Type(doc, start) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, start) != EXTRACE_SPAWNS))
	{
		EXTRACE_FAIL("start must hold exactly %d spots", EXTRACE_SPAWNS);
	}
	for (i = 0; i < EXTRACE_SPAWNS; i++)
	{
		const int s = ExtJson_At(doc, start, i);
		int yaw;

		if (!ExtRace_Pos(doc, ExtJson_Get(doc, s, "pos"), &out->spawnPos[i]))
		{
			EXTRACE_FAIL("start spot %d: pos must be 3 whole numbers within +-%d", i, EXTRACE_COORD_LIMIT);
		}
		if (!ExtRace_Int(doc, ExtJson_Get(doc, s, "yaw"), &yaw) || (yaw < 0) || (yaw >= EXTRACE_ANGLE_FULL))
		{
			EXTRACE_FAIL("start spot %d: yaw must be 0..4095", i);
		}
		out->spawnYaw[i] = (s16)yaw;
	}

	cps = ExtJson_Get(doc, race, "checkpoints");
	n = ExtJson_Count(doc, cps);
	if ((ExtJson_Type(doc, cps) != EXT_JSON_ARRAY) || (n < 3) || (n > EXTRACE_NODES_MAX))
	{
		EXTRACE_FAIL("checkpoints must hold 3..%d nodes (has %d)", EXTRACE_NODES_MAX, n);
	}
	out->nodeCount = n;
	for (i = 0; i < n; i++)
	{
		const int c = ExtJson_At(doc, cps, i);
		const char *kind = ExtJson_String(doc, ExtJson_Get(doc, c, "kind"), "");
		int dist8, next, left, back, right, want;
		struct CheckpointNode *cn = &out->nodes[i];

		if (!ExtRace_Pos(doc, ExtJson_Get(doc, c, "pos"), &cn->pos))
		{
			EXTRACE_FAIL("checkpoint %d: pos must be 3 whole numbers within +-%d", i, EXTRACE_COORD_LIMIT);
		}
		if (!ExtRace_Int(doc, ExtJson_Get(doc, c, "dist8"), &dist8) || (dist8 < 1) || (dist8 > 0xFFFF))
		{
			EXTRACE_FAIL("checkpoint %d: dist8 must be 1..65535", i);
		}
		if (!ExtRace_Int(doc, ExtJson_Get(doc, c, "next"), &next) || !ExtRace_Int(doc, ExtJson_Get(doc, c, "left"), &left) ||
		    !ExtRace_Int(doc, ExtJson_Get(doc, c, "back"), &back) || !ExtRace_Int(doc, ExtJson_Get(doc, c, "right"), &right))
		{
			EXTRACE_FAIL("checkpoint %d: next, left, back and right must be whole numbers", i);
		}
		if ((next < 0) || (next >= n) || (((left < 0) || (left >= n)) && (left != EXTRACE_NO_NODE)) ||
		    (((back < 0) || (back >= n)) && (back != EXTRACE_NO_NODE)) || (((right < 0) || (right >= n)) && (right != EXTRACE_NO_NODE)))
		{
			EXTRACE_FAIL("checkpoint %d: a link points outside the %d nodes", i, n);
		}
		if (strcmp(kind, "line") == 0)
		{
			out->kind[i] = EXTRACE_KIND_LINE;
		}
		else if (strcmp(kind, "restart") == 0)
		{
			out->kind[i] = EXTRACE_KIND_RESTART;
		}
		else if (strcmp(kind, "dir") == 0)
		{
			out->kind[i] = EXTRACE_KIND_DIR;
		}
		else
		{
			EXTRACE_FAIL("checkpoint %d: kind must be line, restart or dir", i);
		}
		if (!ExtRace_Int(doc, ExtJson_Get(doc, c, "want_yaw"), &want) || (want < -1) || (want >= EXTRACE_ANGLE_FULL))
		{
			EXTRACE_FAIL("checkpoint %d: want_yaw must be -1..4095", i);
		}
		cn->distToFinish = (u16)dist8;
		cn->nextIndex_forward = (u8)next;
		cn->nextIndex_left = (u8)left;
		cn->nextIndex_backward = (u8)back;
		cn->nextIndex_right = (u8)right;
		out->wantYaw[i] = (s16)want;
	}
	if ((out->kind[0] != EXTRACE_KIND_LINE) || ((int)out->nodes[0].distToFinish * 8 != out->trackUnits))
	{
		EXTRACE_FAIL("node 0 must be the line, with dist8 = track_units / 8");
	}

	// The lap: from node 0 along next back to node 0, the distance falling at
	// every step (VehLap.c:166-180 takes node 0's distance as the lap).
	i = 0;
	for (guard = 0; guard <= n; guard++)
	{
		k = out->nodes[i].nextIndex_forward;
		if (k == 0)
		{
			break;
		}
		if (out->nodes[k].distToFinish >= out->nodes[i].distToFinish)
		{
			EXTRACE_FAIL("the distance does not fall from node %d to node %d", i, k);
		}
		i = k;
	}
	if (guard > n)
	{
		EXTRACE_FAIL("the chain from node 0 does not come back to node 0");
	}

	rn = ExtJson_Get(doc, race, "restart_nodes");
	if ((ExtJson_Type(doc, rn) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, rn) < 1))
	{
		EXTRACE_FAIL("restart_nodes missing");
	}
	for (k = ExtJson_First(doc, rn); k >= 0; k = ExtJson_Next(doc, k))
	{
		int r = -1;

		if (!ExtRace_Int(doc, k, &r) || (r < 0) || (r >= n) || (out->kind[r] == EXTRACE_KIND_DIR) || (out->wantYaw[r] < 0))
		{
			EXTRACE_FAIL("restart_nodes: %d is no line/restart node with a heading", r);
		}
		out->carries[r] = 1;
	}
	if (!out->carries[0])
	{
		EXTRACE_FAIL("restart_nodes must hold node 0");
	}
	return 1;
}

// The data into a level (pure: the selftest gives it a level of its own).
internal void ExtRace_Apply(struct Level *lev, ExtRaceData *rd)
{
	int i;

	for (i = 0; i < EXTRACE_SPAWNS; i++)
	{
		lev->DriverSpawn[i].pos = rd->spawnPos[i];
		lev->DriverSpawn[i].rot.x = 0;
		lev->DriverSpawn[i].rot.y = (s16)(((rd->spawnYaw[i] - EXTRACE_QUARTER_TURN) + EXTRACE_ANGLE_FULL) % EXTRACE_ANGLE_FULL);
		lev->DriverSpawn[i].rot.z = 0;
	}
	lev->ptr_restart_points = rd->nodes;
	lev->cnt_restart_points = rd->nodeCount;
	lev->configFlags &= ~(u32)EXTRACE_CONFIG_UNDERWATER;
}

// Heading difference of two game angles, in tenths of a degree (0..1800).
internal int ExtRace_AngleDiffTenths(int a, int b)
{
	int d = (a - b) & (EXTRACE_ANGLE_FULL - 1);

	if (d > EXTRACE_ANGLE_FULL / 2)
	{
		d = EXTRACE_ANGLE_FULL - d;
	}
	return (d * 3600 + EXTRACE_ANGLE_FULL / 2) / EXTRACE_ANGLE_FULL;
}

// --------------------------------------------------------------------------
// the hooks

void ExtRace_OnLevelLoaded(struct Level *lev)
{
	const ExtJsonDoc *doc = ExtTrack_Json();
	char why[160];

	s_extRaceReady = 0;
	s_extRaceFrame = 0;
	s_extRaceStarted = 0;
	s_extRaceGrabbed = 0;
	s_extRaceGrabCp = -1;
	s_extRaceRespawnState = 0;
	s_extRaceRespawnTries = 0;
	s_extRaceEndLogged = 0;
	s_extRaceResultsLogged = 0;

	if ((lev == NULL) || (doc == NULL))
	{
		Platform_LogWarn("[CTR ExtTrack] race: no exttrack.json - the donor's start, checkpoints and respawn points stay\n");
		return;
	}
	if (!ExtRace_ParseSection(doc, ExtTrack_Section("race"), &s_extRace, why, (int)sizeof(why)))
	{
		Platform_LogWarn("[CTR ExtTrack] race: section refused (%s) - the donor's start, checkpoints and respawn points stay\n", why);
		return;
	}
	s_extRaceConfigBefore = lev->configFlags;
	ExtRace_Apply(lev, &s_extRace);
	s_extRaceReady = 1;
	{
		int carried = 0;
		int i;

		for (i = 0; i < s_extRace.nodeCount; i++)
		{
			carried += s_extRace.carries[i];
		}
		Platform_Log("[CTR ExtTrack] race: %d checkpoints (%d carry quadblocks), lap %d units, %d start spots, configFlags 0x%x -> 0x%x\n",
		             s_extRace.nodeCount, carried, s_extRace.trackUnits, EXTRACE_SPAWNS, (unsigned)s_extRaceConfigBefore,
		             (unsigned)lev->configFlags);
	}
}

internal void ExtRace_LogStart(struct GameTracker *gGT)
{
	int i;

	Platform_Log("[CTR ExtTrack] race start: laps %d, %d checkpoints, lap %d units (level %d)\n", (int)gGT->numLaps,
	             (int)gGT->level1->cnt_restart_points, (int)gGT->level1->ptr_restart_points[0].distToFinish * 8, (int)gGT->levelID);
	for (i = 0; i < 8; i++)
	{
		const struct Driver *d = gGT->drivers[i];
		int slot, x, y, z, dx, dz, yaw;

		if (d == NULL)
		{
			continue;
		}
		slot = sdata->kartSpawnOrderArray[i];
		x = d->posCurr.x >> 8;
		y = d->posCurr.y >> 8;
		z = d->posCurr.z >> 8;
		yaw = d->rotCurr.y & (EXTRACE_ANGLE_FULL - 1);
		if ((slot < 0) || (slot >= EXTRACE_SPAWNS))
		{
			Platform_Log("[CTR ExtTrack] spawn seat %d slot %d (outside 0..7) pos %d %d %d yaw %d\n", i, slot, x, y, z, yaw);
			continue;
		}
		dx = x - s_extRace.spawnPos[slot].x;
		dz = z - s_extRace.spawnPos[slot].z;
		Platform_Log("[CTR ExtTrack] spawn seat %d slot %d pos %d %d %d yaw %d | data %d %d %d yaw %d | dx %d dz %d dy %d dyaw %d\n", i, slot, x, y, z,
		             yaw, s_extRace.spawnPos[slot].x, s_extRace.spawnPos[slot].y, s_extRace.spawnPos[slot].z, s_extRace.spawnYaw[slot], dx, dz,
		             y - s_extRace.spawnPos[slot].y, ExtRace_AngleDiffTenths(yaw, s_extRace.spawnYaw[slot]) / 10);
		s_extRaceLap[i] = d->lapIndex;
	}
}

internal void ExtRace_RespawnTrigger(struct GameTracker *gGT)
{
	struct Driver *d = gGT->drivers[0];

	if ((g_cfg_extRespawnFrame < 0) || (s_extRaceRespawnState != 0) || (s_extRaceFrame < g_cfg_extRespawnFrame) || (d == NULL))
	{
		return;
	}
	// The game's own conditions for a mask grab (COLL.c:1798-1800).
	if ((d->kartState == KS_MASK_GRABBED) || (d->lastValid == NULL) || (d->instSelf == NULL) || (d->instSelf->thread == NULL))
	{
		if (++s_extRaceRespawnTries > EXTRACE_RESPAWN_TRIES)
		{
			s_extRaceRespawnState = 2;
			Platform_LogWarn("[CTR ExtTrack] respawn asked at frame %d: given up after %d frames (state %d, lastValid %s)\n", g_cfg_extRespawnFrame,
			                 EXTRACE_RESPAWN_TRIES, (int)d->kartState, (d->lastValid != NULL) ? "set" : "none");
		}
		return;
	}
	Platform_Log("[CTR ExtTrack] respawn asked frame %d (wanted %d): seat 0 cp %d pos %d %d %d lap %d\n", s_extRaceFrame, g_cfg_extRespawnFrame,
	             (int)d->lastValid->checkpointIndex, d->posCurr.x >> 8, d->posCurr.y >> 8, d->posCurr.z >> 8, (int)d->lapIndex);
	s_extRaceRespawnState = 1;
	VehStuckProc_MaskGrab_Init(d->instSelf->thread, d);
}

// Seat 0 leaves a mask grab (any, forced or not): where it stands against the node.
internal void ExtRace_WatchRespawn(struct GameTracker *gGT)
{
	const struct Driver *d = gGT->drivers[0];
	const int grabbed = (d != NULL) && (d->kartState == KS_MASK_GRABBED);

	if (d == NULL)
	{
		return;
	}
	if (grabbed && !s_extRaceGrabbed)
	{
		s_extRaceGrabCp = (d->lastValid != NULL) ? (int)d->lastValid->checkpointIndex : -1;
	}
	if (!grabbed && s_extRaceGrabbed)
	{
		const int x = d->posCurr.x >> 8;
		const int y = d->posCurr.y >> 8;
		const int z = d->posCurr.z >> 8;
		const int yaw = d->rotCurr.y & (EXTRACE_ANGLE_FULL - 1);
		const int cp = s_extRaceGrabCp;

		if ((cp >= 0) && (cp < s_extRace.nodeCount))
		{
			const struct CheckpointNode *cn = &s_extRace.nodes[cp];
			const int dx = x - cn->pos.x;
			const int dz = z - cn->pos.z;
			const int dxz = (int)SquareRoot0(dx * dx + dz * dz);

			Platform_Log("[CTR ExtTrack] respawn seat 0 frame %d cp %d pos %d %d %d yaw %d | node %d pos %d %d %d want_yaw %d | dxz %d dy %d err %d.%d deg%s\n",
			             s_extRaceFrame, cp, x, y, z, yaw, cp, cn->pos.x, cn->pos.y, cn->pos.z, s_extRace.wantYaw[cp], dxz, y - cn->pos.y,
			             ExtRace_AngleDiffTenths(yaw, s_extRace.wantYaw[cp]) / 10, ExtRace_AngleDiffTenths(yaw, s_extRace.wantYaw[cp]) % 10,
			             (s_extRace.wantYaw[cp] < 0) ? " (no heading at this node)" : "");
		}
		else
		{
			Platform_Log("[CTR ExtTrack] respawn seat 0 frame %d cp %d (no node) pos %d %d %d yaw %d\n", s_extRaceFrame, cp, x, y, z, yaw);
		}
		if (s_extRaceRespawnState == 1)
		{
			s_extRaceRespawnState = 2;
		}
	}
	s_extRaceGrabbed = grabbed;
}

internal void ExtRace_WatchEnd(struct GameTracker *gGT)
{
	const struct Driver *d0 = gGT->drivers[0];

	if (((gGT->gameMode1 & END_OF_RACE) != 0) && !s_extRaceEndLogged)
	{
		s_extRaceEndLogged = 1;
		Platform_Log("[CTR ExtTrack] race end at frame %d: seat 0 lap %d of %d, rank %d, finished %s\n", s_extRaceFrame,
		             (d0 != NULL) ? (int)d0->lapIndex : -1, (int)gGT->numLaps, (d0 != NULL) ? (int)d0->driverRank : -1,
		             ((d0 != NULL) && ((d0->actionsFlagSet & ACTION_RACE_FINISHED) != 0)) ? "yes" : "no");
	}
	// The arcade results screen (game/native_uidecl.c:56-67: END_OF_RACE and end overlay 1).
	if (((gGT->gameMode1 & END_OF_RACE) != 0) && (gGT->overlayIndex_EndOfRace == 1) && !s_extRaceResultsLogged)
	{
		char path[PLATFORM_ARG_PATH_MAX + 64];

		s_extRaceResultsLogged = 1;
		Platform_Log("[CTR ExtTrack] results screen at frame %d vblank %d\n", s_extRaceFrame, Platform_GetVBlankCount());
		if ((g_cfg_shotName[0] != '\0') && EngineTour_OracleShotName(path, sizeof(path), g_cfg_shotName, "ext-results"))
		{
			Platform_ShotRequest(path);
			Platform_Log("[CTR ExtTrack] snapshot %s asked for\n", path);
		}
	}
}

void ExtRace_OnLogicFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int i;

	if (!s_extRaceReady || (gGT == NULL) || (gGT->level1 == NULL) || ((gGT->gameMode1 & (LOADING | MAIN_MENU)) != 0) ||
	    (gGT->level1->ptr_restart_points != s_extRace.nodes))
	{
		return;
	}
	if (!s_extRaceStarted)
	{
		s_extRaceStarted = 1;
		ExtRace_LogStart(gGT);
	}
	if ((gGT->gameMode1 & PAUSE_ALL) != 0)
	{
		return;
	}
	s_extRaceFrame++;

	for (i = 0; i < 8; i++)
	{
		const struct Driver *d = gGT->drivers[i];

		if ((d != NULL) && (d->lapIndex != s_extRaceLap[i]))
		{
			Platform_Log("[CTR ExtTrack] lap driver %d: %d -> %d frame %d time %d %s%s\n", i, s_extRaceLap[i], (int)d->lapIndex, s_extRaceFrame,
			             (int)gGT->elapsedEventTime, ((d->actionsFlagSet & ACTION_BOT) != 0) ? "bot" : "human",
			             ((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0) ? ", finished" : "");
			s_extRaceLap[i] = d->lapIndex;
		}
	}
	ExtRace_RespawnTrigger(gGT);
	ExtRace_WatchRespawn(gGT);
	ExtRace_WatchEnd(gGT);
}

// --------------------------------------------------------------------------
// --ext-selftest race: the reader and the level write, without a window or data

internal void ExtRace_Check(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest race: FAILED %s\n", what);
	}
}

// A small valid section: node 0 (line), 1 (dir), 2 (restart), 3 (dir) - lap 40000 units.
internal void ExtRace_TestText(char *out, size_t size, const char *patchKey, const char *patchValue)
{
	char spots[1024] = "";
	char extra[128] = "";
	int i;

	for (i = 0; i < 8; i++)
	{
		char one[96];

		snprintf(one, sizeof(one), "%s{\"slot\":%d,\"pos\":[%d,0,-300],\"yaw\":1000}", i ? "," : "", i, -200 + i * 50);
		strcat(spots, one);
	}
	if (patchKey != NULL)
	{
		snprintf(extra, sizeof(extra), ",\"%s\":%s", patchKey, patchValue);
	}
	snprintf(out, size,
	         "{\"race\":{\"version\":1,\"track_units\":40000%s,\"start\":[%s],"
	         "\"checkpoints\":["
	         "{\"pos\":[0,0,0],\"dist8\":5000,\"next\":1,\"left\":255,\"back\":3,\"right\":255,\"kind\":\"line\",\"want_yaw\":1024},"
	         "{\"pos\":[96,0,0],\"dist8\":4988,\"next\":2,\"left\":255,\"back\":0,\"right\":255,\"kind\":\"dir\",\"want_yaw\":-1},"
	         "{\"pos\":[9000,0,0],\"dist8\":3800,\"next\":3,\"left\":255,\"back\":1,\"right\":255,\"kind\":\"restart\",\"want_yaw\":1024},"
	         "{\"pos\":[9096,0,0],\"dist8\":3788,\"next\":0,\"left\":255,\"back\":2,\"right\":255,\"kind\":\"dir\",\"want_yaw\":-1}],"
	         "\"restart_nodes\":[0,2]}}",
	         extra, spots);
}

internal int ExtRace_TestParse(const char *text, ExtRaceData *rd, char *why, int whySize)
{
	ExtJsonDoc doc;
	char perr[128];
	int ok;

	if (!ExtJson_Parse(&doc, text, strlen(text), perr, (int)sizeof(perr)))
	{
		snprintf(why, (size_t)whySize, "json: %s", perr);
		return 0;
	}
	ok = ExtRace_ParseSection(&doc, ExtJson_Get(&doc, 0, "race"), rd, why, whySize);
	ExtJson_Free(&doc);
	return ok;
}

internal void ExtRace_TestRefused(const char *text, const char *reason, int *n, int *failures)
{
	static ExtRaceData rd;
	char why[160];
	char what[256];
	const int ok = ExtRace_TestParse(text, &rd, why, (int)sizeof(why));

	snprintf(what, sizeof(what), "refusal '%s' (got %s: %s)", reason, ok ? "accepted" : "refused", ok ? "" : why);
	ExtRace_Check(!ok && (strstr(why, reason) != NULL), what, n, failures);
}

int ExtRace_Selftest(void)
{
	static ExtRaceData rd;
	static struct Level lev;
	char text[4096];
	char bad[4096];
	char why[160];
	int n = 0;
	int failures = 0;

	ExtRace_TestText(text, sizeof(text), NULL, NULL);
	ExtRace_Check(ExtRace_TestParse(text, &rd, why, (int)sizeof(why)), "the valid section is taken", &n, &failures);
	ExtRace_Check((rd.nodeCount == 4) && (rd.trackUnits == 40000) && (rd.nodes[2].distToFinish == 3800) && (rd.nodes[3].nextIndex_forward == 0) &&
	                  (rd.nodes[0].nextIndex_left == EXTRACE_NO_NODE) && (rd.kind[1] == EXTRACE_KIND_DIR) && rd.carries[2] && !rd.carries[1] &&
	                  (rd.spawnPos[7].x == 150) && (rd.spawnYaw[3] == 1000),
	              "values read", &n, &failures);

	memset(&lev, 0, sizeof(lev));
	lev.configFlags = 0x7;
	ExtRace_Apply(&lev, &rd);
	ExtRace_Check((lev.ptr_restart_points == rd.nodes) && (lev.cnt_restart_points == 4) && (lev.configFlags == 0x5),
	              "level: table and count set, only bit 2 of configFlags cleared", &n, &failures);
	ExtRace_Check((lev.DriverSpawn[3].rot.y == 4072) && (lev.DriverSpawn[3].rot.x == 0) && (lev.DriverSpawn[3].pos.z == -300),
	              "level: DriverSpawn rot.y = yaw - 1024 (mod 4096), VehBirth.c:153 adds it back", &n, &failures);
	ExtRace_Check((ExtRace_AngleDiffTenths(10, 4090) == 14) && (ExtRace_AngleDiffTenths(0, 2048) == 1800) && (ExtRace_AngleDiffTenths(1024, 1024) == 0),
	              "heading difference", &n, &failures);

	// refusals (each must name its reason)
	strcpy(bad, text);
	{
		char *p = strstr(bad, "40000");
		memcpy(p, "32000", 5);
	}
	ExtRace_TestRefused(bad, "track_units", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, "\"dist8\":3800");
		memcpy(p, "\"dist8\":4999", 12);
	}
	ExtRace_TestRefused(bad, "does not fall", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, "\"next\":3");
		memcpy(p, "\"next\":9", 8);
	}
	ExtRace_TestRefused(bad, "outside the 4 nodes", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, "\"kind\":\"line\"");
		memcpy(p, "\"kind\":\"dir\" ", 13);
	}
	ExtRace_TestRefused(bad, "node 0 must be the line", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, "[0,2]");
		memcpy(p, "[0,1]", 5);
	}
	ExtRace_TestRefused(bad, "restart_nodes", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, "[-200,0,-300]");
		memcpy(p, "[-200,0,-1.5]", 13);
	}
	ExtRace_TestRefused(bad, "start spot 0", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, ",{\"slot\":7");
		*p = ']';
		p[1] = '\0';
		strcat(bad, ",\"checkpoints\":[],\"restart_nodes\":[]}}");
	}
	ExtRace_TestRefused(bad, "exactly 8", &n, &failures);
	strcpy(bad, text);
	{
		char *p = strstr(bad, "\"next\":0");
		memcpy(p, "\"next\":2", 8);
	}
	ExtRace_TestRefused(bad, "does not fall", &n, &failures);

	printf("ext selftest race %s: %d checks, %d failures\n", failures ? "FAILED" : "passed", n, failures);
	return failures ? 1 : 0;
}
