#include <platform/native_extportal.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// THE EXTERNAL TRACK'S TELEPORT PORTALS - see include/platform/native_extportal.h.
//
// Read only from the section "portals"; a track without it has no portal and
// this file logs nothing. The geometry below is plain integer arithmetic on
// the section's values (64-bit products), the heading the one double atan2 of
// the exit direction.

global_variable ExtPortal s_extPortals[EXTPORTAL_MAX];
global_variable int s_extPortalCount;

enum
{
	EXTPORTAL_AXIS_SLACK = 8,  // |axis| = 4096 +- 8
	EXTPORTAL_COORD_MAX = 32767,
	EXTPORTAL_ANGLE_FULL = 4096,
};

int ExtPortal_Count(void)
{
	return s_extPortalCount;
}

const ExtPortal *ExtPortal_Get(int k)
{
	return ((k >= 0) && (k < s_extPortalCount)) ? &s_extPortals[k] : NULL;
}

// Up to n integers of a JSON array, each within [lo, hi]. 1 = all there.
internal int ExtPortal_Ints(const ExtJsonDoc *doc, int node, int *out, int n, int lo, int hi)
{
	double v[3];

	if ((n > 3) || (ExtJson_Type(doc, node) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, node) != n) || (ExtJson_Numbers(doc, node, v, n) != n))
	{
		return 0;
	}
	for (int i = 0; i < n; i++)
	{
		if (((double)(int)v[i] != v[i]) || (v[i] < (double)lo) || (v[i] > (double)hi))
		{
			return 0;
		}
		out[i] = (int)v[i];
	}
	return 1;
}

// The section into s_extPortals. Returns the count (0 = no section), -1 with
// why when the section is there but wrong (then no portal at all).
internal int ExtPortal_Read(const ExtJsonDoc *doc, int sec, char *why, int whySize)
{
	ExtPortal tmp[EXTPORTAL_MAX];
	int items, item, n = 0;

	s_extPortalCount = 0;
	if ((doc == NULL) || (sec < 0))
	{
		return 0;
	}
	if ((int)ExtJson_Number(doc, ExtJson_Get(doc, sec, "version"), -1.0) != 1)
	{
		snprintf(why, (size_t)whySize, "version is not 1");
		return -1;
	}
	items = ExtJson_Get(doc, sec, "items");
	if ((ExtJson_Type(doc, items) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, items) < 1) || (ExtJson_Count(doc, items) > EXTPORTAL_MAX))
	{
		snprintf(why, (size_t)whySize, "items is not an array of 1..%d", EXTPORTAL_MAX);
		return -1;
	}
	memset(tmp, 0, sizeof(tmp));
	for (item = ExtJson_First(doc, items); item >= 0; item = ExtJson_Next(doc, item), n++)
	{
		ExtPortal *p = &tmp[n];
		const int axes = ExtJson_Get(doc, item, "axes");
		int a, k;

		if ((int)ExtJson_Number(doc, ExtJson_Get(doc, item, "index"), -1.0) != n)
		{
			snprintf(why, (size_t)whySize, "portal %d: index is not its place in the list", n);
			return -1;
		}
		if (!ExtPortal_Ints(doc, ExtJson_Get(doc, item, "center"), p->center, 3, -EXTPORTAL_COORD_MAX - 1, EXTPORTAL_COORD_MAX) ||
		    !ExtPortal_Ints(doc, ExtJson_Get(doc, item, "half"), p->half, 3, 1, EXTPORTAL_COORD_MAX))
		{
			snprintf(why, (size_t)whySize, "portal %d: center or half is not 3 integers in range (half 1..32767)", n);
			return -1;
		}
		if ((ExtJson_Type(doc, axes) != EXT_JSON_ARRAY) || (ExtJson_Count(doc, axes) != 3))
		{
			snprintf(why, (size_t)whySize, "portal %d: axes is not 3 vectors", n);
			return -1;
		}
		for (k = 0, a = ExtJson_First(doc, axes); a >= 0; k++, a = ExtJson_Next(doc, a))
		{
			long long len2;
			const long long lo = EXTPORTAL_UNIT - EXTPORTAL_AXIS_SLACK, hi = EXTPORTAL_UNIT + EXTPORTAL_AXIS_SLACK;

			if (!ExtPortal_Ints(doc, a, p->axes[k], 3, -EXTPORTAL_UNIT - EXTPORTAL_AXIS_SLACK, EXTPORTAL_UNIT + EXTPORTAL_AXIS_SLACK))
			{
				snprintf(why, (size_t)whySize, "portal %d: axis %d is not 3 integers", n, k);
				return -1;
			}
			len2 = ((long long)p->axes[k][0] * p->axes[k][0]) + ((long long)p->axes[k][1] * p->axes[k][1]) + ((long long)p->axes[k][2] * p->axes[k][2]);
			if ((len2 < lo * lo) || (len2 > hi * hi))
			{
				snprintf(why, (size_t)whySize, "portal %d: axis %d is not of length 4096 +- %d", n, k, EXTPORTAL_AXIS_SLACK);
				return -1;
			}
		}
		p->target = (int)ExtJson_Number(doc, ExtJson_Get(doc, item, "target"), -1.0);
		p->enabled = ExtJson_Number(doc, ExtJson_Get(doc, item, "enabled"), 0.0) != 0.0;
		p->forward = (int)ExtJson_Number(doc, ExtJson_Get(doc, item, "forward"), -1.0);
		if (p->forward != 1)
		{
			// the source's every portal has forward 1; what 0 means is not known
			snprintf(why, (size_t)whySize, "portal %d: forward is not 1 (only forward portals are known)", n);
			return -1;
		}
	}
	for (int k = 0; k < n; k++)
	{
		if ((tmp[k].target < 0) || (tmp[k].target >= n) || (tmp[k].target == k))
		{
			snprintf(why, (size_t)whySize, "portal %d: target %d is not another portal", k, tmp[k].target);
			return -1;
		}
	}
	memcpy(s_extPortals, tmp, sizeof(tmp));
	s_extPortalCount = n;
	return n;
}

void ExtPortal_OnLevelLoaded(struct Level *lev)
{
	char why[160];
	const ExtJsonDoc *doc = ExtTrack_Json();
	const int sec = (doc != NULL) ? ExtTrack_Section("portals") : -1;
	int n;

	(void)lev;
	why[0] = '\0';
	n = ExtPortal_Read(doc, sec, why, (int)sizeof(why));
	if (n < 0)
	{
		Platform_Log("[CTR ExtPortal] refused: %s - the track has no portal\n", why);
	}
	else if (n > 0)
	{
		Platform_Log("[CTR ExtPortal] %d portal(s) read\n", n);
	}
}

// Local coordinate of pos along axis k of portal p (world units).
internal long long ExtPortal_Local(const ExtPortal *p, const int pos[3], int k)
{
	const long long dx = (long long)pos[0] - p->center[0];
	const long long dy = (long long)pos[1] - p->center[1];
	const long long dz = (long long)pos[2] - p->center[2];

	return ((dx * p->axes[k][0]) + (dy * p->axes[k][1]) + (dz * p->axes[k][2])) / EXTPORTAL_UNIT;
}

int ExtPortal_Entered(const int pos[3], const int vel[3])
{
	for (int k = 0; k < s_extPortalCount; k++)
	{
		const ExtPortal *p = &s_extPortals[k];
		const long long along = ((long long)vel[0] * p->axes[1][0]) + ((long long)vel[1] * p->axes[1][1]) + ((long long)vel[2] * p->axes[1][2]);
		int inside = 1;

		if (!p->enabled || (along >= 0))
		{
			continue;
		}
		for (int a = 0; (a < 3) && inside; a++)
		{
			const long long l = ExtPortal_Local(p, pos, a);

			inside = (l >= -p->half[a]) && (l <= p->half[a]);
		}
		if (inside)
		{
			return k;
		}
	}
	return -1;
}

int ExtPortal_Exit(int k, int speed, int outPos[3], int outVel[3], int *outYaw)
{
	const ExtPortal *p = ExtPortal_Get(k);
	const ExtPortal *t;
	double yaw;

	if ((p == NULL) || (p->target < 0) || (p->target >= s_extPortalCount))
	{
		return 0;
	}
	t = &s_extPortals[p->target];
	for (int a = 0; a < 3; a++)
	{
		outPos[a] = t->center[a];
		outVel[a] = (int)(((long long)t->axes[1][a] * speed) / EXTPORTAL_UNIT);
	}
	// the game's heading of a direction (x, z): ratan2(x, z) in 4096 per turn
	yaw = atan2((double)t->axes[1][0], (double)t->axes[1][2]) * (double)EXTPORTAL_ANGLE_FULL / (2.0 * 3.14159265358979323846);
	*outYaw = ((int)floor(yaw + 0.5) % EXTPORTAL_ANGLE_FULL + EXTPORTAL_ANGLE_FULL) % EXTPORTAL_ANGLE_FULL;
	return 1;
}

// ---------------------------------------------------------------------------
// the frame and the steering help

enum
{
	EXTPORTAL_SEATS = 8,
	EXTPORTAL_COOLDOWN = 30,  // frames after a pass in which the seat enters no portal
	EXTPORTAL_KART_MID = 33,  // the kart's middle above posCurr (0.5 H, H = 66.2): the box test and the exit
	EXTPORTAL_STEER_MAX = 19200, // 300 m: a gate farther than this from the kart is not aimed at
};

global_variable int s_extPortalCooldown[EXTPORTAL_SEATS];

// The camera module's reset at the new place (platform/native_extcam_module.c,
// M2): its next frame starts from the kart's new place instead of springing
// over from the old one; it acts only for seat 0 and only while it owns the view.
void ExtCam_OnTeleport(int seat);
global_variable int s_extPortalFrame;
global_variable int s_extPortalPasses;

internal long long ExtPortal_Dist2(const int a[3], const int b[3])
{
	const long long dx = (long long)a[0] - b[0], dy = (long long)a[1] - b[1], dz = (long long)a[2] - b[2];

	return (dx * dx) + (dy * dy) + (dz * dz);
}

int ExtPortal_SteerAim(const int kartPos[3], int target[3])
{
	const long long kt = ExtPortal_Dist2(kartPos, target);

	for (int k = 0; k < s_extPortalCount; k++)
	{
		const ExtPortal *p = &s_extPortals[k];
		const ExtPortal *t;
		long long kg;

		if (!p->enabled || (p->target < 0) || (p->target >= s_extPortalCount))
		{
			continue;
		}
		t = &s_extPortals[p->target];
		kg = ExtPortal_Dist2(kartPos, p->center);
		// in front of the gate (it is entered along its -y), the gate nearer than the
		// target and within reach, the target nearer to the exit than to the kart
		if ((ExtPortal_Local(p, kartPos, 1) > 0) && (kg < kt) && (kg <= (long long)EXTPORTAL_STEER_MAX * EXTPORTAL_STEER_MAX) &&
		    (ExtPortal_Dist2(target, t->center) < kt))
		{
			target[0] = p->center[0];
			target[1] = p->center[1];
			target[2] = p->center[2];
			return 1;
		}
	}
	return 0;
}

void ExtPortal_OnLogicFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int seats;

	if ((s_extPortalCount == 0) || (gGT == NULL) || ((gGT->gameMode1 & (MAIN_MENU | LOADING)) != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		return;
	}
	s_extPortalFrame++;
	seats = (gGT->numPlyrCurrGame < EXTPORTAL_SEATS) ? (int)gGT->numPlyrCurrGame : EXTPORTAL_SEATS;
	for (int seat = 0; seat < seats; seat++)
	{
		struct Driver *d = gGT->drivers[seat];
		int pos[3], vel[3], outPos[3], outVel[3], yaw = 0, k;
		double speed;

		if (s_extPortalCooldown[seat] > 0)
		{
			s_extPortalCooldown[seat]--;
			continue;
		}
		// a bot, a seat driven as a bot after its finish line, a mask grab: not here
		// (the CPU drivers pass the portals on their nav paths)
		if ((d == NULL) || ((d->actionsFlagSet & ACTION_BOT) != 0) || (d->kartState == KS_MASK_GRABBED))
		{
			continue;
		}
		pos[0] = d->posCurr.x >> 8;
		pos[1] = (d->posCurr.y >> 8) + EXTPORTAL_KART_MID;
		pos[2] = d->posCurr.z >> 8;
		vel[0] = d->velocity.x;
		vel[1] = d->velocity.y;
		vel[2] = d->velocity.z;
		k = ExtPortal_Entered(pos, vel);
		if (k < 0)
		{
			continue;
		}
		speed = sqrt(((double)vel[0] * vel[0]) + ((double)vel[1] * vel[1]) + ((double)vel[2] * vel[2]));
		if (!ExtPortal_Exit(k, (int)(speed + 0.5), outPos, outVel, &yaw))
		{
			continue;
		}
		Platform_Log("[CTR ExtPortal] seat %d frame %d portal %d -> %d pos %d %d %d -> %d %d %d yaw %d -> %d speed %d (%.0f)\n", seat, s_extPortalFrame, k,
		             s_extPortals[k].target, pos[0], pos[1] - EXTPORTAL_KART_MID, pos[2], outPos[0], outPos[1] - EXTPORTAL_KART_MID, outPos[2],
		             d->angle & (EXTPORTAL_ANGLE_FULL - 1), yaw, (int)d->speed, speed / 256.0);
		// THE PASS. The place: the exit's centre is the kart's middle. The way on:
		// the exit's +y at the speed it came with (velocity and the game's heading
		// fields, as VehBirth_TeleportSelf sets them; d->speed stays). The surface
		// frame: world up (the exits lie in gravity volumes, not in zones;
		// ExtPhys_SeedDriver as on a bot's mask grab). The camera: no blend from the
		// old place (CAMERA_FLAG_DIRECTION_CHANGED, as after a respawn).
		d->posCurr.x = outPos[0] << 8;
		d->posCurr.y = (outPos[1] - EXTPORTAL_KART_MID) << 8;
		d->posCurr.z = outPos[2] << 8;
		d->posPrev = d->posCurr;
		d->velocity.x = outVel[0];
		d->velocity.y = outVel[1];
		d->velocity.z = outVel[2];
		d->rotCurr.y = (s16)yaw;
		d->rotPrev.y = (s16)yaw;
		d->angle = (s16)yaw;
		ExtPhys_SeedDriver(d, NULL, 0);
		if (d->driverID < 4)
		{
			gGT->cameraDC[d->driverID].flags |= CAMERA_FLAG_DIRECTION_CHANGED;
			ExtCam_OnTeleport(d->driverID);
		}
		s_extPortalCooldown[seat] = EXTPORTAL_COOLDOWN;
		s_extPortalPasses++;
	}
}

// ---------------------------------------------------------------------------
// selftest (--ext-selftest portal): no window, no data

internal void ExtPortal_Check(int ok, const char *what, int *n, int *failures)
{
	(*n)++;
	if (!ok)
	{
		(*failures)++;
		printf("ext selftest portal: FAIL %s\n", what);
	}
}

// Parses text and reads its section "portals": the count, -1 refused.
internal int ExtPortal_TestRead(const char *text)
{
	ExtJsonDoc doc;
	char why[160];
	int n;

	memset(&doc, 0, sizeof(doc));
	if (!ExtJson_Parse(&doc, text, strlen(text), why, (int)sizeof(why)))
	{
		printf("ext selftest portal: test text does not parse: %s\n", why);
		return -2;
	}
	n = ExtPortal_Read(&doc, ExtJson_Get(&doc, 0, "portals"), why, (int)sizeof(why));
	ExtJson_Free(&doc);
	return n;
}

// Two portals: 0 a gate at (1000, 100, 0) entered moving +x (its local y is -x),
// 1 a small box at (-2000, 300, 5000) left along +z (its local y).
#define EXTPORTAL_TEST_P0 "{\"index\": 0, \"target\": 1, \"enabled\": 1, \"forward\": 1, \"center\": [1000, 100, 0], " \
                          "\"axes\": [[0, 0, 4096], [-4096, 0, 0], [0, 4096, 0]], \"half\": [300, 64, 500]}"
#define EXTPORTAL_TEST_P1 "{\"index\": 1, \"target\": 0, \"enabled\": 1, \"forward\": 1, \"center\": [-2000, 300, 5000], " \
                          "\"axes\": [[4096, 0, 0], [0, 0, 4096], [0, 4096, 0]], \"half\": [100, 100, 100]}"

int ExtPortal_Selftest(void)
{
	char text[2048];
	int n = 0, failures = 0;

	// a whole section
	snprintf(text, sizeof(text), "{\"portals\": {\"version\": 1, \"items\": [%s, %s]}}", EXTPORTAL_TEST_P0, EXTPORTAL_TEST_P1);
	ExtPortal_Check(ExtPortal_TestRead(text) == 2, "two portals read", &n, &failures);
	{
		const int at[3] = {1000, 100, 0}, plusX[3] = {100, 0, 0}, minusX[3] = {-100, 0, 0}, up[3] = {0, 50, 0};
		const int past[3] = {1000 + 70, 100, 0}, before[3] = {1000 - 64, 100, 0};
		const int sideIn[3] = {1000, 100, 299}, sideOut[3] = {1000, 100, 301}, highOut[3] = {1000, 100 + 501, 0};
		const int atExit[3] = {-2000, 300, 5000}, plusZ[3] = {0, 0, 120};
		int pos[3], vel[3], yaw = -1;

		ExtPortal_Check(ExtPortal_Entered(at, plusX) == 0, "the gate's centre moving along its -y (+x): portal 0", &n, &failures);
		ExtPortal_Check(ExtPortal_Entered(at, minusX) == -1, "the same point moving along its +y: no entry", &n, &failures);
		ExtPortal_Check(ExtPortal_Entered(at, up) == -1, "moving across the normal: no entry", &n, &failures);
		ExtPortal_Check((ExtPortal_Entered(past, plusX) == -1) && (ExtPortal_Entered(before, plusX) == 0), "the slab is half 64 thick along the normal",
		                &n, &failures);
		ExtPortal_Check((ExtPortal_Entered(sideIn, plusX) == 0) && (ExtPortal_Entered(sideOut, plusX) == -1) && (ExtPortal_Entered(highOut, plusX) == -1),
		                "the gate's sides: local x half 300, local z half 500", &n, &failures);
		ExtPortal_Check(ExtPortal_Exit(0, 200, pos, vel, &yaw) && (pos[0] == -2000) && (pos[1] == 300) && (pos[2] == 5000) && (vel[0] == 0) &&
		                    (vel[1] == 0) && (vel[2] == 200) && (yaw == 0),
		                "portal 0 leaves at portal 1's centre along its +y (+z), heading 0", &n, &failures);
		ExtPortal_Check(ExtPortal_Exit(1, 4096, pos, vel, &yaw) && (pos[0] == 1000) && (vel[0] == -4096) && (yaw == 3072),
		                "portal 1 leaves at portal 0 along -x, heading 3072", &n, &failures);
		ExtPortal_Check(ExtPortal_Entered(atExit, plusZ) == -1, "a kart just out of the exit (moving along its +y) does not enter it again", &n, &failures);
		ExtPortal_Check(!ExtPortal_Exit(2, 100, pos, vel, &yaw) && !ExtPortal_Exit(-1, 100, pos, vel, &yaw), "no such portal: no exit", &n, &failures);
	}

	// the steering help: a target past the exit is replaced by the gate; a
	// target on this side, a kart behind the gate, or a gate farther than the
	// target are left alone
	{
		const int kart[3] = {200, 100, 0};      // 800 in front of gate 0 (entered moving +x)
		const int behind[3] = {1500, 100, 0};   // past gate 0
		int tgt[3];

		tgt[0] = -1900, tgt[1] = 300, tgt[2] = 5400; // 400 past exit 1
		ExtPortal_Check(ExtPortal_SteerAim(kart, tgt) && (tgt[0] == 1000) && (tgt[1] == 100) && (tgt[2] == 0), "steer: a target past the exit -> the gate",
		                &n, &failures);
		tgt[0] = 600, tgt[1] = 100, tgt[2] = 50;
		ExtPortal_Check(!ExtPortal_SteerAim(kart, tgt) && (tgt[0] == 600), "steer: a target before the gate stays", &n, &failures);
		tgt[0] = -1900, tgt[1] = 300, tgt[2] = 5400;
		ExtPortal_Check(!ExtPortal_SteerAim(behind, tgt) || (tgt[0] != 1000), "steer: a kart past gate 0 is not sent back to it", &n, &failures);
	}

	// refusals: each leaves no portal at all
	{
		static const char *const bad[][2] = {
			{"\"target\": 1", "\"target\": 0"},
			{"[0, 0, 4096], [-4096", "[0, 0, 3000], [-4096"},
			{"\"half\": [300, 64, 500]", "\"half\": [300, 0, 500]"},
			{"\"forward\": 1, \"center\": [1000", "\"forward\": 0, \"center\": [1000"},
			{"\"index\": 0", "\"index\": 1"},
			{"\"center\": [1000, 100, 0]", "\"center\": [1000, 100]"},
		};
		static const char *const what[] = {"a portal targeting itself", "an axis of length 3000", "a half extent 0", "forward 0",
		                                   "an index off its place", "a centre of 2 numbers"};

		for (int b = 0; b < (int)(sizeof(bad) / sizeof(bad[0])); b++)
		{
			char p0[512];
			const char *at = strstr(EXTPORTAL_TEST_P0, bad[b][0]);
			char line[160];

			if (at == NULL)
			{
				ExtPortal_Check(0, "test text has the patch place", &n, &failures);
				continue;
			}
			snprintf(p0, sizeof(p0), "%.*s%s%s", (int)(at - EXTPORTAL_TEST_P0), EXTPORTAL_TEST_P0, bad[b][1], at + strlen(bad[b][0]));
			snprintf(text, sizeof(text), "{\"portals\": {\"version\": 1, \"items\": [%s, %s]}}", p0, EXTPORTAL_TEST_P1);
			snprintf(line, sizeof(line), "refused: %s", what[b]);
			ExtPortal_Check((ExtPortal_TestRead(text) == -1) && (ExtPortal_Count() == 0), line, &n, &failures);
		}
		snprintf(text, sizeof(text), "{\"portals\": {\"version\": 2, \"items\": [%s, %s]}}", EXTPORTAL_TEST_P0, EXTPORTAL_TEST_P1);
		ExtPortal_Check((ExtPortal_TestRead(text) == -1) && (ExtPortal_Count() == 0), "refused: version 2", &n, &failures);
		snprintf(text, sizeof(text), "{\"portals\": {\"version\": 1, \"items\": []}}");
		ExtPortal_Check((ExtPortal_TestRead(text) == -1) && (ExtPortal_Count() == 0), "refused: no items", &n, &failures);
	}

	// no section: no portal, nothing entered
	{
		const int at[3] = {1000, 100, 0}, plusX[3] = {100, 0, 0};

		ExtPortal_Check((ExtPortal_TestRead("{\"race\": {}}") == 0) && (ExtPortal_Count() == 0) && (ExtPortal_Entered(at, plusX) == -1),
		                "no section: no portal", &n, &failures);
	}

	if (failures == 0)
	{
		printf("ext selftest portal passed: %d checks, 0 failures\n", n);
		return 0;
	}
	printf("ext selftest portal FAILED: %d checks, %d failures\n", n, failures);
	return 1;
}
