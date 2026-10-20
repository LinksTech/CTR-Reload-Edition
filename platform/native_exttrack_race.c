#include <platform/native_exttrack_race.h>
#include <platform/native_extcoll.h>
#include <platform/native_extphys.h>
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
// TWO OPTIONAL PARTS, each only when the data has it (without them: exactly
// the behaviour before they existed):
//   - "respawn" in a checkpoint: the node a kart is put back at when its last
//     ground named this node (default: the node itself). The table stays the
//     lap's distance anchors; only the respawn point moves. The converter uses
//     it for the finish line (node 0 -> the restart point the source gives the
//     line) and for nodes on surfaces a kart cannot be set upright on (back to
//     the last upright node before them, never forward: no short cut). Read by
//     game/Vehicle/VehStuckProc.c through ExtRace_RespawnNode.
//   - "kill_y" in the section: a kart of a human player below this height is
//     grabbed by the mask (for a track without a kill floor under it; the
//     game's own fall limit lies 64 under the lowest collision record,
//     COLL.c:1563, which can be far below the lowest road).
//
// MEASURING (logs only, plus the triggers of --dev-ext-respawn):
//   [CTR ExtTrack] race: ...                  the section taken (or refused, and why)
//   [CTR ExtTrack] race start: ...            laps, nodes, lap units
//   [CTR ExtTrack] spawn seat S slot I ...    D4: where the kart stands against the data
//   [CTR ExtTrack] lap driver D: A -> B ...   D6: every lap change of every driver
//   [CTR ExtTrack] respawn asked ...          D7: an item of --dev-ext-respawn was reached
//   [CTR ExtTrack] respawn seat 0 ...         D7: where the kart landed against the node
//   [CTR ExtTrack] respawn seat S ... (bot)   a CPU driver left a mask grab
//   [CTR ExtTrack] respawn seat S placed ...  B5: where the teleport put the kart (before its fall)
//   [CTR ExtTrack] respawn seat S seeded ...  B5: the zone state set after the teleport (tracks with zones)
//   [CTR ExtTrack] respawn seat 0 ground ...  B5: frames until the kart has ground again, its up
//                                             against the surface normal there
//   [CTR ExtTrack] respawn seat 0 stable ...  B5: 90 frames after that without a new mask grab
//   [CTR ExtTrack] kill height: ...           kill_y grabbed a kart
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
	EXTRACE_RESPAWN_ITEMS = 8,     // --dev-ext-respawn: items of the list
	EXTRACE_RESPAWN_FRAME_MAX = 1000000,
	EXTRACE_DRIVERS = 8,
	EXTRACE_GROUND_WAIT = 300,     // B5: frames the ground watch waits after a respawn
	EXTRACE_STABLE_FRAMES = 90,    // B5: frames without a new mask grab after the ground contact
	EXTRACE_RESPAWN_Y_OFFSET = 0x80, // the respawn's height over the surface (VehStuckProc.c, VEH_STUCK_RESPAWN_Y_OFFSET)
	EXTRACE_SEED_PROBE_LEN = 384,  // B5: the surface probe after a respawn: from the kart's place (node + 0x80) this far down (to node - 0x100)
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
	u8 respawn[EXTRACE_NODES_MAX];  // respawn node of each node (itself unless the data says otherwise)
	int respawnMoved;               // nodes whose respawn node is another one
	int hasKillY;
	int killY;                      // world units
} ExtRaceData;

// --dev-ext-respawn <item>[,<item>...]: a race frame (0..1000000) or a node
// "cpN" (when the last ground of seat 0 names node N).
typedef struct
{
	int count;
	int isNode[EXTRACE_RESPAWN_ITEMS];
	int value[EXTRACE_RESPAWN_ITEMS];
} ExtRaceRespawnList;

global_variable ExtRaceData s_extRace;
global_variable int s_extRaceReady;     // 1 = the table of s_extRace is in the level
global_variable u32 s_extRaceConfigBefore;
global_variable int s_extRaceFrame;     // logic frames of the race (no pause, no loading) since the level load
global_variable int s_extRaceStarted;
global_variable int s_extRaceLap[8];
global_variable int s_extRaceGrabbed[EXTRACE_DRIVERS];
global_variable int s_extRaceGrabCp[EXTRACE_DRIVERS];
global_variable int s_extRaceRespawnState; // 0 waiting, 1 asked, 2 done or given up (per item)
global_variable int s_extRaceRespawnTries;
global_variable int s_extRaceRespawnItem;  // the item of the list now waited for
global_variable ExtRaceRespawnList s_extRaceRespawnList;
global_variable int s_extRaceRespawnListSet;
global_variable int s_extRaceKilled;
global_variable int s_extRaceWatchPhase;  // seat 0 after a respawn: 0 nothing, 1 waits for ground, 2 counts stable frames
global_variable int s_extRaceWatchFrames;
global_variable int s_extRaceWatchCp;
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
		{
			const int rnode = ExtJson_Get(doc, c, "respawn");
			int target = i;

			if ((rnode >= 0) && (!ExtRace_Int(doc, rnode, &target) || (target < 0) || (target >= n)))
			{
				EXTRACE_FAIL("checkpoint %d: respawn must be a node 0..%d", i, n - 1);
			}
			out->respawn[i] = (u8)target;
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

	// A respawn node is a line/restart node with a heading, and it keeps its own
	// respawn (no chains: one step, then the game's own walk).
	for (i = 0; i < n; i++)
	{
		const int t = out->respawn[i];

		if (t == i)
		{
			continue;
		}
		if ((out->kind[t] == EXTRACE_KIND_DIR) || (out->wantYaw[t] < 0) || (out->respawn[t] != t))
		{
			EXTRACE_FAIL("checkpoint %d: respawn node %d is a dir node or moves on itself", i, t);
		}
		out->respawnMoved++;
	}

	k = ExtJson_Get(doc, race, "kill_y");
	if (k >= 0)
	{
		if (!ExtRace_Int(doc, k, &out->killY) || (out->killY < -EXTRACE_COORD_LIMIT) || (out->killY > EXTRACE_COORD_LIMIT))
		{
			EXTRACE_FAIL("kill_y must be a whole number within +-%d", EXTRACE_COORD_LIMIT);
		}
		out->hasKillY = 1;
	}
	return 1;
}

// --dev-ext-respawn: "900", "cp14", "900,cp30,cp31" - up to 8 items. 1 = read
// (into out when given); 0 = not a list of that form.
internal int ExtRace_ParseRespawnList(const char *text, ExtRaceRespawnList *out)
{
	ExtRaceRespawnList list;
	const char *p = text;

	memset(&list, 0, sizeof(list));
	if ((p == NULL) || (*p == '\0'))
	{
		return 0;
	}
	while (1)
	{
		int isNode = 0;
		long v = 0;
		int digits = 0;

		if (list.count >= EXTRACE_RESPAWN_ITEMS)
		{
			return 0;
		}
		if ((p[0] == 'c') && (p[1] == 'p'))
		{
			isNode = 1;
			p += 2;
		}
		while ((*p >= '0') && (*p <= '9'))
		{
			v = v * 10 + (*p - '0');
			p++;
			if (++digits > 7)
			{
				return 0;
			}
		}
		if ((digits == 0) || (isNode && (v >= EXTRACE_NODES_MAX)) || (!isNode && (v > EXTRACE_RESPAWN_FRAME_MAX)))
		{
			return 0;
		}
		list.isNode[list.count] = isNode;
		list.value[list.count] = (int)v;
		list.count++;
		if (*p == '\0')
		{
			break;
		}
		if (*p != ',')
		{
			return 0;
		}
		p++;
	}
	if (out != NULL)
	{
		*out = list;
	}
	return 1;
}

// main.c, the check of --dev-ext-respawn before any window: 1 = a valid list.
int ExtRace_RespawnListValid(const char *text)
{
	return ExtRace_ParseRespawnList(text, NULL);
}

// main.c, --dev-ext-respawn (the value was checked before any window).
void ExtRace_SetRespawnList(const char *text)
{
	s_extRaceRespawnListSet = ExtRace_ParseRespawnList(text, &s_extRaceRespawnList);
}

// game/Vehicle/VehStuckProc.c, right after the respawn teleport (VehBirth_TeleportSelf)
// of a human kart: one log line, and - on a track with anti-gravity zones - the
// place and the physics part's zone state set for the new place (B5).
// THE OWN PROBE. The teleport's vertical probe starts at the node's height
// (VehBirth.c:261-265); where the converter's node lies a little under the game's
// surface (measured: a 29 degree ramp 14 units above its node) it finds nothing,
// keeps world up as the normal and the old lastValid (VehBirth.c:267-274). So on a
// track with zone records a probe of the external collision runs from the kart's
// place (node + 0x80) 384 units down (ExtColl_ProbeSurface, the game's ray test):
// with a hit the kart stands 0x80 over it, lastValid is its record, and the
// zone state is seeded with its normal and zone (zone 0 = world: an old zone up
// must not pull the kart toward a wall while it drops; outside a zone this is
// what the teleport does with a hit - the normals of the ground). Without a hit
// the teleport's result stays (logged). On a track without zone records nothing
// is set (the drive is the same as before, gate A2).
void ExtRace_OnRespawned(struct Driver *d)
{
	struct GameTracker *gGT = sdata->gGT;
	int seat;
	int zone = 0;
	int seeded = 0;

	if (!s_extRaceReady || (d == NULL) || (gGT == NULL) || (gGT->level1 == NULL) || (gGT->level1->ptr_restart_points != s_extRace.nodes))
	{
		return;
	}
	for (seat = 0; seat < EXTRACE_DRIVERS; seat++)
	{
		if (gGT->drivers[seat] == d)
		{
			break;
		}
	}
	if (ExtColl_ZoneRecords() > 0)
	{
		const int before = ExtPhys_DriverZone(d);
		SVec3 from;
		SVec3 down;
		ExtCollHit hit;

		from.x = (s16)(d->posCurr.x >> 8);
		from.y = (s16)(d->posCurr.y >> 8);
		from.z = (s16)(d->posCurr.z >> 8);
		down.x = 0;
		down.y = -4096;
		down.z = 0;
		memset(&hit, 0, sizeof(hit));
		if (ExtColl_ProbeSurface(&from, &down, EXTRACE_SEED_PROBE_LEN, &hit))
		{
			const int oldY = d->posCurr.y >> 8;

			d->posCurr.x = (int)hit.pos.x << 8;
			d->posCurr.y = ((int)hit.pos.y + EXTRACE_RESPAWN_Y_OFFSET) << 8;
			d->posCurr.z = (int)hit.pos.z << 8;
			d->posPrev = d->posCurr;
			d->quadBlockHeight = (int)hit.pos.y << 8;
			if (hit.record != NULL)
			{
				d->lastValid = (struct QuadBlock *)hit.record;
			}
			zone = hit.zone;
			ExtPhys_SeedDriver(d, &hit.normal, zone);
			seeded = 1;
			Platform_Log("[CTR ExtTrack] respawn seat %d seeded zone %d -> %d up %d %d %d (surface y %d, teleport had y %d)\n", seat, before, zone,
			             hit.normal.x, hit.normal.y, hit.normal.z, hit.pos.y, oldY);
		}
		else
		{
			Platform_Log("[CTR ExtTrack] respawn seat %d no surface under the place (probe from y %d, %d down) - the teleport's result stays\n", seat,
			             from.y, EXTRACE_SEED_PROBE_LEN);
		}
	}
	Platform_Log("[CTR ExtTrack] respawn seat %d placed frame %d pos %d %d %d normal %d %d %d cp %d zone %d%s\n", seat, s_extRaceFrame, d->posCurr.x >> 8,
	             d->posCurr.y >> 8, d->posCurr.z >> 8, d->AxisAngle3_normalVec.x, d->AxisAngle3_normalVec.y, d->AxisAngle3_normalVec.z,
	             (d->lastValid != NULL) ? (int)d->lastValid->checkpointIndex : -1, zone, seeded ? "" : " (no zones)");
}

// game/Vehicle/VehStuckProc.c: the node a kart is put back at for the node its
// last ground names. Outside the external track's table: the index unchanged.
int ExtRace_RespawnNode(const struct Level *lev, int index)
{
	if (!s_extRaceReady || (lev == NULL) || (lev->ptr_restart_points != s_extRace.nodes) || (index < 0) || (index >= s_extRace.nodeCount))
	{
		return index;
	}
	return s_extRace.respawn[index];
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
	memset(s_extRaceGrabbed, 0, sizeof(s_extRaceGrabbed));
	memset(s_extRaceGrabCp, 0xff, sizeof(s_extRaceGrabCp));
	s_extRaceRespawnState = 0;
	s_extRaceRespawnTries = 0;
	s_extRaceRespawnItem = 0;
	s_extRaceKilled = 0;
	s_extRaceWatchPhase = 0;
	s_extRaceWatchFrames = 0;
	s_extRaceWatchCp = -1;
	if (!s_extRaceRespawnListSet && (g_cfg_extRespawnFrame >= 0))
	{
		s_extRaceRespawnList.count = 1;
		s_extRaceRespawnList.isNode[0] = 0;
		s_extRaceRespawnList.value[0] = g_cfg_extRespawnFrame;
		s_extRaceRespawnListSet = 1;
	}
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
		if (s_extRace.respawnMoved != 0)
		{
			Platform_Log("[CTR ExtTrack] race: %d node(s) put a kart back at another node (respawn), node 0 -> %d\n", s_extRace.respawnMoved,
			             (int)s_extRace.respawn[0]);
		}
		if (s_extRace.hasKillY)
		{
			Platform_Log("[CTR ExtTrack] race: kill height y %d (the game's own fall limit y %d)\n", s_extRace.killY,
			             ((COLL_WorldMesh(lev) != NULL) && (COLL_WorldMesh(lev)->bspRoot != NULL)) ? COLL_WorldMesh(lev)->bspRoot->box.min.y - 0x40 : 0);
		}
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
	const ExtRaceRespawnList *list = &s_extRaceRespawnList;
	int item, isNode, value;
	char wanted[24];

	if (s_extRaceRespawnState == 2)
	{
		// The item is done: the next one waits.
		s_extRaceRespawnItem++;
		s_extRaceRespawnState = 0;
		s_extRaceRespawnTries = 0;
	}
	item = s_extRaceRespawnItem;
	if (!s_extRaceRespawnListSet || (item >= list->count) || (s_extRaceRespawnState != 0) || (d == NULL))
	{
		return;
	}
	isNode = list->isNode[item];
	value = list->value[item];
	if (isNode)
	{
		snprintf(wanted, sizeof(wanted), "cp%d", value);
	}
	else
	{
		snprintf(wanted, sizeof(wanted), "%d", value);
	}
	if (!isNode && (s_extRaceFrame < value))
	{
		return;
	}
	if (isNode && ((d->lastValid == NULL) || ((int)d->lastValid->checkpointIndex != value) || (d->kartState == KS_MASK_GRABBED)))
	{
		return;
	}
	// The game's own conditions for a mask grab (COLL.c:1798-1800).
	if ((d->kartState == KS_MASK_GRABBED) || (d->lastValid == NULL) || (d->instSelf == NULL) || (d->instSelf->thread == NULL))
	{
		if (++s_extRaceRespawnTries > EXTRACE_RESPAWN_TRIES)
		{
			s_extRaceRespawnState = 2;
			Platform_LogWarn("[CTR ExtTrack] respawn asked at %s: given up after %d frames (state %d, lastValid %s)\n", wanted, EXTRACE_RESPAWN_TRIES,
			                 (int)d->kartState, (d->lastValid != NULL) ? "set" : "none");
		}
		return;
	}
	Platform_Log("[CTR ExtTrack] respawn asked frame %d (wanted %s): seat 0 cp %d pos %d %d %d lap %d\n", s_extRaceFrame, wanted,
	             (int)d->lastValid->checkpointIndex, d->posCurr.x >> 8, d->posCurr.y >> 8, d->posCurr.z >> 8, (int)d->lapIndex);
	s_extRaceRespawnState = 1;
	VehStuckProc_MaskGrab_Init(d->instSelf->thread, d);
}

// kill_y: a human kart below it is grabbed, under the game's own conditions
// for a mask grab (COLL.c:1799-1800). CPU drivers keep the nav's recovery.
internal void ExtRace_KillHeight(struct GameTracker *gGT)
{
	int i;

	if (!s_extRace.hasKillY)
	{
		return;
	}
	for (i = 0; i < EXTRACE_DRIVERS; i++)
	{
		struct Driver *d = gGT->drivers[i];

		if ((d == NULL) || ((d->actionsFlagSet & ACTION_BOT) != 0) || (d->kartState == KS_MASK_GRABBED) || (d->lastValid == NULL) ||
		    (d->instSelf == NULL) || (d->instSelf->thread == NULL) || ((sdata->HudAndDebugFlags & 0x1000) != 0) ||
		    ((d->stepFlagSet & COLL_STEP_TRIGGER_SUPPRESS_MASK_GRAB) != 0) || ((d->posCurr.y >> 8) >= s_extRace.killY))
		{
			continue;
		}
		s_extRaceKilled++;
		Platform_Log("[CTR ExtTrack] kill height: seat %d frame %d y %d below %d - mask grab (cp %d)\n", i, s_extRaceFrame, d->posCurr.y >> 8,
		             s_extRace.killY, (int)d->lastValid->checkpointIndex);
		VehStuckProc_MaskGrab_Init(d->instSelf->thread, d);
	}
}

// A driver leaves a mask grab (any, forced or not). Seat 0: where it stands
// against the node it was put back at (the respawn node of the node its last
// ground named); CPU drivers: one line with the place.
internal void ExtRace_WatchRespawn(struct GameTracker *gGT)
{
	int seat;

	for (seat = 0; seat < EXTRACE_DRIVERS; seat++)
	{
		const struct Driver *d = gGT->drivers[seat];
		const int grabbed = (d != NULL) && (d->kartState == KS_MASK_GRABBED);
		const int bot = (d != NULL) && ((d->actionsFlagSet & ACTION_BOT) != 0);

		if (d == NULL)
		{
			s_extRaceGrabbed[seat] = 0;
			continue;
		}
		if (grabbed && !s_extRaceGrabbed[seat])
		{
			if (bot)
			{
				s_extRaceGrabCp[seat] = (int)d->botData.ai_quadblock_checkpointIndex;
			}
			else
			{
				s_extRaceGrabCp[seat] = (d->lastValid != NULL) ? (int)d->lastValid->checkpointIndex : -1;
			}
		}
		if (!grabbed && s_extRaceGrabbed[seat])
		{
			const int x = d->posCurr.x >> 8;
			const int y = d->posCurr.y >> 8;
			const int z = d->posCurr.z >> 8;
			const int yaw = d->rotCurr.y & (EXTRACE_ANGLE_FULL - 1);
			const int cp = s_extRaceGrabCp[seat];

			if (bot)
			{
				Platform_Log("[CTR ExtTrack] respawn seat %d frame %d cp %d pos %d %d %d (bot)\n", seat, s_extRaceFrame, cp, x, y, z);
			}
			else if ((seat == 0) && (cp >= 0) && (cp < s_extRace.nodeCount))
			{
				const int t = s_extRace.respawn[cp];
				const struct CheckpointNode *cn = &s_extRace.nodes[t];
				const int dx = x - cn->pos.x;
				const int dz = z - cn->pos.z;
				const int dxz = (int)SquareRoot0(dx * dx + dz * dz);

				Platform_Log("[CTR ExtTrack] respawn seat 0 frame %d cp %d pos %d %d %d yaw %d | node %d pos %d %d %d want_yaw %d | dxz %d dy %d err %d.%d deg%s\n",
				             s_extRaceFrame, cp, x, y, z, yaw, t, cn->pos.x, cn->pos.y, cn->pos.z, s_extRace.wantYaw[t], dxz, y - cn->pos.y,
				             ExtRace_AngleDiffTenths(yaw, s_extRace.wantYaw[t]) / 10, ExtRace_AngleDiffTenths(yaw, s_extRace.wantYaw[t]) % 10,
				             (s_extRace.wantYaw[t] < 0) ? " (no heading at this node)" : "");
			}
			else
			{
				Platform_Log("[CTR ExtTrack] respawn seat %d frame %d cp %d (no node) pos %d %d %d yaw %d\n", seat, s_extRaceFrame, cp, x, y, z, yaw);
			}
			if ((seat == 0) && (s_extRaceRespawnState == 1))
			{
				s_extRaceRespawnState = 2;
			}
			if (seat == 0)
			{
				s_extRaceWatchPhase = 1;
				s_extRaceWatchFrames = 0;
				s_extRaceWatchCp = cp;
			}
		}
		s_extRaceGrabbed[seat] = grabbed;
	}
}

// Angle between two 4096-scaled vectors, in tenths of a degree (float only here: a log value).
internal int ExtRace_VecAngleTenths(int ax, int ay, int az, int bx, int by, int bz)
{
	const double la = sqrt((double)ax * ax + (double)ay * ay + (double)az * az);
	const double lb = sqrt((double)bx * bx + (double)by * by + (double)bz * bz);
	double c;

	if ((la <= 0.0) || (lb <= 0.0))
	{
		return -1;
	}
	c = ((double)ax * bx + (double)ay * by + (double)az * bz) / (la * lb);
	c = (c > 1.0) ? 1.0 : ((c < -1.0) ? -1.0 : c);
	return (int)(acos(c) * 1800.0 / 3.14159265358979323846 + 0.5);
}

// B5: seat 0 after a respawn - frames until ground (ACTION_TOUCH_GROUND: the collision flag
// DRIVER_COLL_FLAG_GROUNDED can still hold the frame before the respawn), its up (matrixFacingDir column 1, the kart's
// drawn up) against the surface normal it stands on (AxisAngle1, set by the ground hits), then 90
// frames without a new mask grab. Logs only.
internal void ExtRace_WatchAfterRespawn(struct GameTracker *gGT)
{
	const struct Driver *d = gGT->drivers[0];

	if ((s_extRaceWatchPhase == 0) || (d == NULL))
	{
		return;
	}
	s_extRaceWatchFrames++;
	if (d->kartState == KS_MASK_GRABBED)
	{
		Platform_Log("[CTR ExtTrack] respawn seat 0 NOT stable: a new mask grab %d frame(s) after the respawn at cp %d\n", s_extRaceWatchFrames,
		             s_extRaceWatchCp);
		s_extRaceWatchPhase = 0;
		return;
	}
	if (s_extRaceWatchPhase == 1)
	{
		if ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0)
		{
			const int upTenths = ExtRace_VecAngleTenths(d->matrixFacingDir.m[0][1], d->matrixFacingDir.m[1][1], d->matrixFacingDir.m[2][1],
			                                            d->AxisAngle1_normalVec.x, d->AxisAngle1_normalVec.y, d->AxisAngle1_normalVec.z);
			const int worldTenths = ExtRace_VecAngleTenths(d->AxisAngle1_normalVec.x, d->AxisAngle1_normalVec.y, d->AxisAngle1_normalVec.z, 0, 4096, 0);

			Platform_Log("[CTR ExtTrack] respawn seat 0 ground after %d frame(s): kart up to surface normal %d.%d deg, surface normal to world up %d.%d deg, "
			             "pos %d %d %d cp %d\n",
			             s_extRaceWatchFrames, upTenths / 10, upTenths % 10, worldTenths / 10, worldTenths % 10, d->posCurr.x >> 8, d->posCurr.y >> 8,
			             d->posCurr.z >> 8, (d->lastValid != NULL) ? (int)d->lastValid->checkpointIndex : -1);
			s_extRaceWatchPhase = 2;
			s_extRaceWatchFrames = 0;
		}
		else if (s_extRaceWatchFrames > EXTRACE_GROUND_WAIT)
		{
			Platform_Log("[CTR ExtTrack] respawn seat 0 NO ground within %d frames after the respawn at cp %d (pos %d %d %d)\n", EXTRACE_GROUND_WAIT,
			             s_extRaceWatchCp, d->posCurr.x >> 8, d->posCurr.y >> 8, d->posCurr.z >> 8);
			s_extRaceWatchPhase = 0;
		}
		return;
	}
	if (s_extRaceWatchFrames >= EXTRACE_STABLE_FRAMES)
	{
		Platform_Log("[CTR ExtTrack] respawn seat 0 stable %d frames after the ground contact (no mask grab), cp %d\n", EXTRACE_STABLE_FRAMES,
		             (d->lastValid != NULL) ? (int)d->lastValid->checkpointIndex : -1);
		s_extRaceWatchPhase = 0;
	}
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
	ExtRace_KillHeight(gGT);
	ExtRace_WatchAfterRespawn(gGT);
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
	ExtRace_Check((ExtRace_VecAngleTenths(0, 4096, 0, 0, 4096, 0) == 0) && (ExtRace_VecAngleTenths(0, 4096, 0, 4096, 0, 0) == 900) &&
	                  (ExtRace_VecAngleTenths(0, 4096, 0, 0, -4096, 0) == 1800) && (ExtRace_VecAngleTenths(0, 0, 0, 1, 0, 0) == -1) &&
	                  (ExtRace_VecAngleTenths(0, 4096, 0, 0, 2896, 2896) == 450),
	              "vector angle (B5 up against the surface normal)", &n, &failures);
	ExtRace_Check((rd.respawn[0] == 0) && (rd.respawn[2] == 2) && (rd.respawnMoved == 0) && !rd.hasKillY,
	              "without respawn/kill_y: every node puts back at itself, no kill height", &n, &failures);

	// respawn and kill_y
	strcpy(bad, text);
	{
		char *p = strstr(bad, "\"kind\":\"line\"");
		memmove(p + 12, p, strlen(p) + 1);
		memcpy(p, "\"respawn\":2,", 12);
	}
	ExtRace_TestText(text, sizeof(text), "kill_y", "-1200");
	{
		char both[4096];
		char *p;

		strcpy(both, text);
		p = strstr(both, "\"kind\":\"line\"");
		memmove(p + 12, p, strlen(p) + 1);
		memcpy(p, "\"respawn\":2,", 12);
		ExtRace_Check(ExtRace_TestParse(both, &rd, why, (int)sizeof(why)) && (rd.respawn[0] == 2) && (rd.respawn[2] == 2) && (rd.respawnMoved == 1) &&
		                  rd.hasKillY && (rd.killY == -1200),
		              "node 0 puts back at node 2, kill height read", &n, &failures);
	}
	ExtRace_Check(ExtRace_TestParse(bad, &rd, why, (int)sizeof(why)) && (rd.respawn[0] == 2) && !rd.hasKillY, "respawn without kill_y", &n, &failures);
	{
		char *p = strstr(bad, "\"respawn\":2,");
		p[10] = '1'; // node 1 is a dir node
	}
	ExtRace_TestRefused(bad, "dir node", &n, &failures);
	{
		char *p = strstr(bad, "\"respawn\":1,");
		p[10] = '7';
	}
	ExtRace_TestRefused(bad, "respawn must be a node", &n, &failures);
	ExtRace_TestText(bad, sizeof(bad), "kill_y", "-1.5");
	ExtRace_TestRefused(bad, "kill_y", &n, &failures);
	{
		static struct Level other;
		ExtRace_Check(ExtRace_RespawnNode(&other, 3) == 3, "respawn node outside the external table: unchanged", &n, &failures);
	}

	// --dev-ext-respawn lists
	{
		ExtRaceRespawnList list;

		ExtRace_Check(ExtRace_ParseRespawnList("900", &list) && (list.count == 1) && !list.isNode[0] && (list.value[0] == 900), "list: one frame", &n,
		              &failures);
		ExtRace_Check(ExtRace_ParseRespawnList("900,cp30,cp253,1000000", &list) && (list.count == 4) && list.isNode[1] && (list.value[1] == 30) &&
		                  (list.value[2] == 253) && !list.isNode[3],
		              "list: frames and nodes", &n, &failures);
		ExtRace_Check(!ExtRace_ParseRespawnList("", NULL) && !ExtRace_ParseRespawnList("cp254", NULL) && !ExtRace_ParseRespawnList("1000001", NULL) &&
		                  !ExtRace_ParseRespawnList("900,", NULL) && !ExtRace_ParseRespawnList("9x", NULL) && !ExtRace_ParseRespawnList("cp", NULL) &&
		                  !ExtRace_ParseRespawnList("-5", NULL) && !ExtRace_ParseRespawnList("1,2,3,4,5,6,7,8,9", NULL),
		              "list: refusals", &n, &failures);
	}
	ExtRace_TestText(text, sizeof(text), NULL, NULL);
	ExtRace_Check(ExtRace_TestParse(text, &rd, why, (int)sizeof(why)), "the valid section again", &n, &failures);

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
