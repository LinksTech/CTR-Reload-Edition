// THE EXTERNAL TRACK'S CRATES (--ext-track, see platform/engine_exttrack.c):
// the item crates at the positions of the data. Declared in
// include/platform/engine.h (EXTERNAL TRACK, the parts' entry points).
//
// WHAT. The crates of exttrack.json (section "crates", array "items", each with
// "pos" [x, y, z] in world units and "yaw" 0..4095; 57 on the PoC track) become
// item crates of the game: instances born with INSTANCE_Birth3D on the crate
// model of the loaded level (gGT->modelPtr[PU_RANDOM_CRATE] - every race LEV
// carries it), without an InstDef, drawn like every instance of the pool
// (RenderBucket_QueueNonLevInstances, game/MAIN/MainFrame_RenderFrame.c:754).
// The game's crate code stays as it is: a contact goes through
// RB_CrateWeapon_LInC (game/231/RB_Crate.c:329), which bursts the crate, gives
// the item roulette to a human and nothing to a bot (RB_Crate.c:255) and lets
// it grow back (RB_CrateAny_ThTick_Grow, RB_Crate.c:146). All types of the
// source become item crates (the type stays in the data only).
//
// WHY A CONTACT PASS. A crate without an InstDef has no BSP hitbox, so neither
// COLL_MOVED_PlayerSearch (game/COLL.c:2344) nor BOTS_LevInstColl
// (game/BOTS.c:609) can find it. Once per logic frame the pass below delivers
// the contact the way Sunset Vista delivers it to its bots
// (game/native_trackmod.c:3375-3507): a cube of +-101 units around a point 25
// above the kart, one contact per driver and frame, crates with a thread
// (touched, cooldown running) or with scale 0 (smashed) skipped.
//
// ONLY WITH THE SWITCH: the dispatcher calls these only while the external
// track holds the loaded level.
//
// LOG (cnk\tools\poc_m5_pruef.py, gate D8): one line per crate and one summary
// at the first build of a run, one line per contact, one line at exit.

enum
{
	EXTCRATE_MAX = 64,              // the PoC track has 57; the race pool has 128 instances (MainInit.c:362)
	EXTCRATE_CONTACT_RADIUS = 101,  // game/native_trackmod.c:3287
	EXTCRATE_CONTACT_DIAMETER = EXTCRATE_CONTACT_RADIUS * 2,
	EXTCRATE_CONTACT_Y_OFFSET = 25, // game/native_trackmod.c:3292
	EXTCRATE_DRIVER_SLOTS = 8,
	EXTCRATE_FULL_SCALE = 0x1000,
	EXTCRATE_ANGLE_FULL = 4096,
};

struct ExtCrateDef
{
	s16 pos[3];
	s16 yaw;
};

struct ExtCrate
{
	struct Instance *inst;
	int index; // in the data
	int smashed;
};

global_variable struct ExtCrate s_extCrates[EXTCRATE_MAX];
global_variable int s_extCrateCount;  // born in this build
global_variable int s_extCrateBuilds; // builds of this run
global_variable int s_extCrateReported;
global_variable s64 s_extCrateHitsHuman;
global_variable s64 s_extCrateHitsBot;
global_variable s64 s_extCrateItems;
global_variable s64 s_extCrateRegrown;

// The contact rule: a point within +-101 of the crate on every axis.
internal int ExtCrates_InBox(int dx, int dy, int dz)
{
	return ((u32)(dx + EXTCRATE_CONTACT_RADIUS) <= (u32)EXTCRATE_CONTACT_DIAMETER) &&
	       ((u32)(dy + EXTCRATE_CONTACT_RADIUS) <= (u32)EXTCRATE_CONTACT_DIAMETER) &&
	       ((u32)(dz + EXTCRATE_CONTACT_RADIUS) <= (u32)EXTCRATE_CONTACT_DIAMETER);
}

// The crates of the data: up to max into out, *bad = items left out (not an
// object, no three numbers in pos, a coordinate outside s16, yaw outside
// 0..4095). Returns how many were read.
internal int ExtCrates_ReadItems(const ExtJsonDoc *doc, int section, struct ExtCrateDef *out, int max, int *bad, int *total)
{
	const int items = ExtJson_Get(doc, section, "items");
	int node;
	int n = 0;

	*bad = 0;
	*total = ExtJson_Count(doc, items);
	for (node = ExtJson_First(doc, items); node >= 0; node = ExtJson_Next(doc, node))
	{
		double p[3];
		const double yaw = ExtJson_Number(doc, ExtJson_Get(doc, node, "yaw"), -1.0);
		int k;
		int ok = (ExtJson_Type(doc, node) == EXT_JSON_OBJECT) && (ExtJson_Numbers(doc, ExtJson_Get(doc, node, "pos"), p, 3) == 3) && (yaw >= 0.0) &&
		         (yaw < (double)EXTCRATE_ANGLE_FULL);

		for (k = 0; ok && (k < 3); k++)
		{
			ok = (p[k] >= -32768.0) && (p[k] <= 32767.0);
		}
		if (!ok || (n >= max))
		{
			(*bad)++;
			continue;
		}
		for (k = 0; k < 3; k++)
		{
			out[n].pos[k] = (s16)((p[k] < 0.0) ? (p[k] - 0.5) : (p[k] + 0.5));
		}
		out[n].yaw = (s16)yaw;
		n++;
	}
	return n;
}

internal void ExtCrates_AtExit(void)
{
	Platform_Log("[CTR ExtCrates] at exit: hits %lld (human %lld, bot %lld), items given %lld, regrown %lld; %d build(s)\n",
	             s_extCrateHitsHuman + s_extCrateHitsBot, s_extCrateHitsHuman, s_extCrateHitsBot, s_extCrateItems, s_extCrateRegrown, s_extCrateBuilds);
}

// From the dispatcher at the start of a race (fresh load and restart, game/MAIN/MainMain.c).
void ExtCrates_OnRaceStart(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const ExtJsonDoc *doc = ExtTrack_Json();
	struct ExtCrateDef defs[EXTCRATE_MAX];
	struct Model *model;
	int wanted;
	int bad = 0;
	int total = 0;
	int i;

	s_extCrateCount = 0;
	if (gGT == NULL)
	{
		return;
	}
	s_extCrateBuilds++;
	if (!s_extCrateReported)
	{
		s_extCrateReported = 1;
		Platform_AtExitReport(ExtCrates_AtExit);
	}
	if (doc == NULL)
	{
		Platform_Log("[CTR ExtCrates] no track data - no crates\n");
		return;
	}
	wanted = ExtCrates_ReadItems(doc, ExtTrack_Section("crates"), defs, EXTCRATE_MAX, &bad, &total);
	if (bad != 0)
	{
		Platform_Log("[CTR ExtCrates] %d of %d crate(s) of the data left out (unreadable, outside s16, or more than %d)\n", bad, total, EXTCRATE_MAX);
	}

	model = gGT->modelPtr[PU_RANDOM_CRATE];
	if (model == NULL)
	{
		Platform_Log("[CTR ExtCrates] the loaded level has no crate model 0x%02x - %d crate(s) not born\n", PU_RANDOM_CRATE, wanted);
		return;
	}

	for (i = 0; i < wanted; i++)
	{
		struct ExtCrate *c = &s_extCrates[s_extCrateCount];
		SVec3 rot;
		struct Instance *inst = INSTANCE_Birth3D(model, "crate", NULL);

		if (inst == NULL)
		{
			Platform_Log("[CTR ExtCrates] crate %d: the instance pool is full - not born\n", i);
			continue;
		}
		rot.x = 0;
		rot.y = defs[i].yaw;
		rot.z = 0;
		// As INSTANCE_LevInitAll does for a level crate (game/INSTANCE.c:388-405).
		ConvertRotToMatrix(&inst->matrix, &rot);
		inst->matrix.t[0] = defs[i].pos[0];
		inst->matrix.t[1] = defs[i].pos[1];
		inst->matrix.t[2] = defs[i].pos[2];
		inst->reflectionRGBA = 0x7f7f7f;
		c->inst = inst;
		c->index = i;
		c->smashed = 0;
		s_extCrateCount++;
		if (s_extCrateBuilds == 1)
		{
			Platform_Log("[CTR ExtCrates] crate %d pos %d %d %d yaw %d\n", i, defs[i].pos[0], defs[i].pos[1], defs[i].pos[2], defs[i].yaw);
		}
	}

	Platform_Log("[CTR ExtCrates] spawned %d of %d crate(s) (model 0x%02x, explosion 0x%02x %s), instance pool %d of %d taken, build %d\n", s_extCrateCount,
	             wanted, PU_RANDOM_CRATE, STATIC_CRATE_EXPLOSION, (gGT->modelPtr[STATIC_CRATE_EXPLOSION] != NULL) ? "ok" : "missing",
	             (int)gGT->JitPools.instance.maxItems - (int)gGT->JitPools.instance.free.count, (int)gGT->JitPools.instance.maxItems, s_extCrateBuilds);
}

internal void ExtCrates_Hit(struct ExtCrate *c, struct Driver *d, int seat, int bot)
{
	struct ScratchpadStruct sps;
	const int heldBefore = (int)d->heldItemID;

	// A scratchpad of its own, as game/230/MM_NativeCrystal.c:276-278: the crate
	// callback reads only Input1.modelID - who hit it (RB_Crate.c:29-62).
	memset(&sps, 0, sizeof(sps));
	sps.Input1.modelID = bot ? DYNAMIC_ROBOT_CAR : DYNAMIC_PLAYER;
	RB_CrateWeapon_LInC(c->inst, d->instSelf->thread, &sps);

	if (bot)
	{
		s_extCrateHitsBot++;
	}
	else
	{
		s_extCrateHitsHuman++;
		if ((heldBefore != HELD_ITEM_ROULETTE) && ((int)d->heldItemID == HELD_ITEM_ROULETTE))
		{
			s_extCrateItems++;
		}
	}
	Platform_Log("[CTR ExtCrates] crate %d hit by seat %d (%s)\n", c->index, seat, bot ? "bot" : "human");
}

// From the dispatcher once per logic frame: the drivers have moved.
void ExtCrates_OnLogicFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int seat;
	int i;

	if ((s_extCrateCount == 0) || (gGT == NULL) || ((gGT->gameMode1 & PAUSE_ALL) != 0))
	{
		return;
	}

	for (i = 0; i < s_extCrateCount; i++)
	{
		struct ExtCrate *c = &s_extCrates[i];

		if (c->inst->scale.x == 0)
		{
			c->smashed = 1;
		}
		else if (c->smashed && (c->inst->scale.x == EXTCRATE_FULL_SCALE))
		{
			c->smashed = 0;
			s_extCrateRegrown++;
		}
	}

	for (seat = 0; seat < EXTCRATE_DRIVER_SLOTS; seat++)
	{
		struct Driver *d = gGT->drivers[seat];
		int x;
		int y;
		int z;

		if ((d == NULL) || (d->instSelf == NULL) || (d->instSelf->thread == NULL))
		{
			continue;
		}
		x = d->posCurr.x >> 8;
		y = (d->posCurr.y >> 8) + EXTCRATE_CONTACT_Y_OFFSET;
		z = d->posCurr.z >> 8;

		for (i = 0; i < s_extCrateCount; i++)
		{
			struct ExtCrate *c = &s_extCrates[i];

			if ((c->inst->thread != NULL) || (c->inst->scale.x == 0))
			{
				continue;
			}
			if (!ExtCrates_InBox(x - c->inst->matrix.t[0], y - c->inst->matrix.t[1], z - c->inst->matrix.t[2]))
			{
				continue;
			}
			ExtCrates_Hit(c, d, seat, (d->actionsFlagSet & ACTION_BOT) != 0);
			break;
		}
	}
}

// From the dispatcher when the race lets go of its state (restart, quit; the
// memory of the race's instances is given back right after): every pointer
// kept here would point to foreign ground.
void ExtCrates_OnRestart(void)
{
	memset(s_extCrates, 0, sizeof(s_extCrates));
	s_extCrateCount = 0;
}

// --ext-selftest crates: the contact rule and the read of the data on a made-up
// document; no window, no game, no track data. Prints "ext selftest crates
// passed: N checks, 0 failures" (or FAILED); returns 0 / 1.
int ExtCrates_Selftest(void)
{
	static const char json[] = "{\"crates\": {\"count\": 6, \"items\": ["
	                           "{\"index\": 0, \"pos\": [-1860, 320, -4553], \"yaw\": 1638},"
	                           "{\"index\": 1, \"pos\": [10.4, -10.6, 0], \"yaw\": 0},"
	                           "{\"index\": 2, \"pos\": [40000, 0, 0], \"yaw\": 0},"
	                           "{\"index\": 3, \"pos\": [1, 2], \"yaw\": 0},"
	                           "{\"index\": 4, \"pos\": [1, 2, 3], \"yaw\": 4096},"
	                           "{\"index\": 5, \"pos\": [32767, -32768, 7]}"
	                           "]}}";
	ExtJsonDoc doc;
	struct ExtCrateDef defs[EXTCRATE_MAX];
	char why[128];
	int checks = 0;
	int failures = 0;
	int bad = 0;
	int total = 0;
	int n;

#define EXTCRATE_CHECK(cond)                                                                                                                         \
	do                                                                                                                                               \
	{                                                                                                                                                \
		checks++;                                                                                                                                    \
		if (!(cond))                                                                                                                                 \
		{                                                                                                                                            \
			failures++;                                                                                                                              \
			printf("ext selftest crates: FAILED %s (line %d)\n", #cond, __LINE__);                                                                   \
		}                                                                                                                                            \
	} while (0)

	// The contact cube, its edges and one step outside each.
	EXTCRATE_CHECK(ExtCrates_InBox(0, 0, 0));
	EXTCRATE_CHECK(ExtCrates_InBox(101, -101, 101));
	EXTCRATE_CHECK(ExtCrates_InBox(-101, 101, -101));
	EXTCRATE_CHECK(!ExtCrates_InBox(102, 0, 0));
	EXTCRATE_CHECK(!ExtCrates_InBox(0, -102, 0));
	EXTCRATE_CHECK(!ExtCrates_InBox(0, 0, 102));
	EXTCRATE_CHECK(!ExtCrates_InBox(-40000, 0, 0));

	// The data: two good items, rounding to the nearest unit, four left out.
	EXTCRATE_CHECK(ExtJson_Parse(&doc, json, sizeof(json) - 1, why, (int)sizeof(why)) == 1);
	n = ExtCrates_ReadItems(&doc, ExtJson_Get(&doc, 0, "crates"), defs, EXTCRATE_MAX, &bad, &total);
	EXTCRATE_CHECK((n == 2) && (bad == 4) && (total == 6));
	EXTCRATE_CHECK((defs[0].pos[0] == -1860) && (defs[0].pos[1] == 320) && (defs[0].pos[2] == -4553) && (defs[0].yaw == 1638));
	EXTCRATE_CHECK((defs[1].pos[0] == 10) && (defs[1].pos[1] == -11) && (defs[1].pos[2] == 0) && (defs[1].yaw == 0));
	// No more than max are kept; the rest counts as left out.
	n = ExtCrates_ReadItems(&doc, ExtJson_Get(&doc, 0, "crates"), defs, 1, &bad, &total);
	EXTCRATE_CHECK((n == 1) && (bad == 5));
	// A missing section reads as no crates.
	n = ExtCrates_ReadItems(&doc, -1, defs, EXTCRATE_MAX, &bad, &total);
	EXTCRATE_CHECK((n == 0) && (bad == 0) && (total == 0));
	ExtJson_Free(&doc);

	// Nothing of the running game was touched.
	EXTCRATE_CHECK((s_extCrateCount == 0) && (s_extCrateBuilds == 0));

#undef EXTCRATE_CHECK

	if (failures != 0)
	{
		printf("ext selftest crates FAILED: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	printf("ext selftest crates passed: %d checks, 0 failures\n", checks);
	return 0;
}
