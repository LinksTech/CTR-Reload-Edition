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
// THE CRATE'S MIDDLE (only when the data asks for it with crates.center_y):
// a retail item crate stands with its instance origin on the floor, and its
// hitbox is centred 48 units above that origin (the BSP hitboxes of the 321
// item crates of the 18 race tracks: box x/z +-76, y -28..+124, median). Data
// that gives the middle of a hovering crate writes pos = middle - center_y and
// center_y; the contact cube is then centred on origin + center_y, which is the
// retail contact of a kart against such a hitbox (cube +-101 = 76 + 25 around a
// point 25 above the kart). Without center_y the cube stays around the origin,
// exactly as before.
//
// LOG (gate D8): one line per crate and one summary
// at the first build of a run, one line per contact, one line at exit. With
// --dev-ext-crate-log in addition the measuring lines "[CTR ExtCrateLog]"
// (report, hit, back; format at g_cfg_extCrateLog below).

// platform/engine_exttrack.c (later in the unity build): the drawing part's tint of an
// instance from the floor around it (render.instance_tint); 0 and no change without the field.
int ExtDraw_TintInstance(struct Instance *inst);

enum
{
	EXTCRATE_MAX = 128,             // the race pool has 128 instances (MainInit.c:362); the larger external track has 81
	EXTCRATE_CONTACT_RADIUS = 101,  // game/native_trackmod.c:3287
	EXTCRATE_CONTACT_DIAMETER = EXTCRATE_CONTACT_RADIUS * 2,
	EXTCRATE_CONTACT_Y_OFFSET = 25, // game/native_trackmod.c:3292
	EXTCRATE_DRIVER_SLOTS = 8,
	EXTCRATE_FULL_SCALE = 0x1000,
	EXTCRATE_ANGLE_FULL = 4096,
	EXTCRATE_CENTER_Y_MAX = 255,    // crates.center_y: units from the instance origin up to the crate's middle
};

struct ExtCrateDef
{
	s16 pos[3];
	s16 yaw;
	s16 dataY;  // with "capped": the middle the data asked for (world units)
	u8 capped;  // the converter lowered the middle (written by the data, logged only)
	int srcIndex; // the item's "index" of the data (the source's numbering), -1 without; logged only
};

struct ExtCrate
{
	struct Instance *inst;
	int index; // in the data
	int smashed;
	int srcIndex;
};

// --dev-ext-crate-log (main.c; measuring only, 0 = off): THE CRATE LINES - a
// log, no game state written, no random number drawn. Off: nothing below runs.
//   [CTR ExtCrateLog] report build B crate I data D origin X Y Z centre CX CY CZ u UX UY UZ node P:N ground t T at HX HY HZ n NX NY NZ front 0|1 zone Z terrain K
//   [CTR ExtCrateLog] report build B crate I data D origin X Y Z centre CX CY CZ u UX UY UZ node P:N ground none
//       at the race start (frame 0), one per crate born: origin = the instance's
//       position, centre = origin + EXTCRATE_MODEL_HALF_Y up (the middle of the
//       crate model), u = ExtNav_UpAt(centre) (the up of the nearest nav frame;
//       (0, 1, 0) outside zones), the line from the centre along -u up to
//       EXTCRATE_LOG_REACH: the first ground record (ExtColl_LineQuery, ground
//       class only - no wall, pad or kill record) at distance T; the checker
//       takes bottom = centre - half height along u, gap = T - half height.
//   [CTR ExtCrateLog] hit crate I seat S human|bot frame F ground G2 G1 G0 jumpago J
//       at each contact: G2 G1 G0 = the kart's ground contact (ACTION_TOUCH_GROUND)
//       after the logic of frames F-2, F-1, F (1 / 0, - = before the start); J =
//       frames since R1 or L1 (the jump) was last held on the pad of seat 0 (0 =
//       in frame F, -1 = not since the start; only seat 0, else -1).
//   [CTR ExtCrateLog] back crate I frame F hit H delay D
//       when a smashed crate is at full size again (scale 0x1000): F = that
//       frame, H = the frame of the contact that smashed it, D = F - H.
// F = logic frames since the race start (frame 0 = the report), paused frames
// not counted.
int g_cfg_extCrateLog = 0;

enum
{
	EXTCRATE_MODEL_HALF_Y = 54,    // the retail item crate (model 8) spans y 0..108.3 above its origin, the same in all 18 race LEVs
	EXTCRATE_LOG_REACH = 2048,     // the checker cuts at its own reach (8 H)
	EXTCRATE_LOG_RING = 4,         // ground contact kept for the last frames (3 needed)
};

global_variable struct ExtCrate s_extCrates[EXTCRATE_MAX];
global_variable int s_extCrateCount;  // born in this build
global_variable int s_extCrateCenterY; // crates.center_y of the data; 0 = contact around the origin
global_variable int s_extCrateBuilds; // builds of this run
global_variable int s_extCrateReported;
global_variable s64 s_extCrateHitsHuman;
global_variable s64 s_extCrateHitsBot;
global_variable s64 s_extCrateItems;
global_variable s64 s_extCrateRegrown;
// --dev-ext-crate-log only (written by nothing else, read by nothing of the game)
global_variable int s_extCrateFrame;
global_variable s8 s_extCrateGround[EXTCRATE_DRIVER_SLOTS][EXTCRATE_LOG_RING]; // -1 = not seen
global_variable int s_extCrateGroundFrame[EXTCRATE_DRIVER_SLOTS][EXTCRATE_LOG_RING];
global_variable int s_extCrateJumpFrame; // the last frame with R1 or L1 held on pad 0, -1 none
global_variable int s_extCrateHitFrame[EXTCRATE_MAX];

// The ground contact of seat after frame f, from the ring: 1 / 0, -1 = not recorded.
internal int ExtCrates_GroundAt(int seat, int f)
{
	const int k = ((f % EXTCRATE_LOG_RING) + EXTCRATE_LOG_RING) % EXTCRATE_LOG_RING;

	if ((f < 0) || (seat < 0) || (seat >= EXTCRATE_DRIVER_SLOTS) || (s_extCrateGroundFrame[seat][k] != f))
	{
		return -1;
	}
	return s_extCrateGround[seat][k];
}

internal void ExtCrates_GroundSet(int seat, int f, int onGround)
{
	const int k = ((f % EXTCRATE_LOG_RING) + EXTCRATE_LOG_RING) % EXTCRATE_LOG_RING;

	s_extCrateGround[seat][k] = (s8)(onGround ? 1 : 0);
	s_extCrateGroundFrame[seat][k] = f;
}

internal void ExtCrates_LogReset(void)
{
	int s;
	int k;

	s_extCrateFrame = 0;
	s_extCrateJumpFrame = -1;
	for (s = 0; s < EXTCRATE_DRIVER_SLOTS; s++)
	{
		for (k = 0; k < EXTCRATE_LOG_RING; k++)
		{
			s_extCrateGround[s][k] = -1;
			s_extCrateGroundFrame[s][k] = -1;
		}
	}
	for (k = 0; k < EXTCRATE_MAX; k++)
	{
		s_extCrateHitFrame[k] = -1;
	}
}

internal const char *ExtCrates_GroundText(int g)
{
	return (g < 0) ? "-" : ((g != 0) ? "1" : "0");
}

// The report line of one crate at the race start (see the format above).
internal void ExtCrates_LogReport(const struct ExtCrate *c, int build)
{
	double centre[3];
	double u[3];
	double len;
	short up[3];
	int path = -1;
	int frame = -1;
	ExtCollLineHit hit;
	char head[256];

	centre[0] = c->inst->matrix.t[0];
	centre[1] = (double)c->inst->matrix.t[1] + EXTCRATE_MODEL_HALF_Y;
	centre[2] = c->inst->matrix.t[2];
	(void)ExtNav_UpAt(centre, up, &path, &frame);
	u[0] = up[0];
	u[1] = up[1];
	u[2] = up[2];
	len = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	if (len <= 0.0)
	{
		u[0] = 0.0;
		u[1] = 1.0;
		u[2] = 0.0;
	}
	else
	{
		u[0] /= len;
		u[1] /= len;
		u[2] /= len;
	}
	snprintf(head, sizeof(head), "[CTR ExtCrateLog] report build %d crate %d data %d origin %d %d %d centre %.1f %.1f %.1f u %.4f %.4f %.4f node %d:%d", build, c->index,
	         c->srcIndex, (int)c->inst->matrix.t[0], (int)c->inst->matrix.t[1], (int)c->inst->matrix.t[2], centre[0], centre[1], centre[2], u[0], u[1], u[2], path, frame);
	{
		const double down[3] = {-u[0], -u[1], -u[2]};

		if (ExtColl_LineQuery(centre, down, 0.0, (double)EXTCRATE_LOG_REACH, EXT_COLL_LINE_GROUND, 0, &hit))
		{
			Platform_Log("%s ground t %.2f at %.1f %.1f %.1f n %.4f %.4f %.4f front %d zone %d terrain %d\n", head, hit.t, hit.pos[0], hit.pos[1], hit.pos[2], hit.normal[0],
			             hit.normal[1], hit.normal[2], hit.front, hit.zone, hit.terrain);
		}
		else
		{
			Platform_Log("%s ground none\n", head);
		}
	}
}

// The contact rule: a point within +-101 of the crate on every axis.
internal int ExtCrates_InBox(int dx, int dy, int dz)
{
	return ((u32)(dx + EXTCRATE_CONTACT_RADIUS) <= (u32)EXTCRATE_CONTACT_DIAMETER) &&
	       ((u32)(dy + EXTCRATE_CONTACT_RADIUS) <= (u32)EXTCRATE_CONTACT_DIAMETER) &&
	       ((u32)(dz + EXTCRATE_CONTACT_RADIUS) <= (u32)EXTCRATE_CONTACT_DIAMETER);
}

// A kart (world units of posCurr) against a crate whose middle lies centerY
// above its instance origin: the point 25 above the kart in the cube around
// the middle.
internal int ExtCrates_KartTouches(int x, int y, int z, int ox, int oy, int oz, int centerY)
{
	return ExtCrates_InBox(x - ox, (y + EXTCRATE_CONTACT_Y_OFFSET) - (oy + centerY), z - oz);
}

// crates.center_y: missing = 0 (contact around the origin, the behaviour before
// the field existed); an integer 0..255 is taken; anything else is refused
// (returns -1, the caller keeps 0 and logs it).
internal int ExtCrates_ReadCenterY(const ExtJsonDoc *doc, int section)
{
	const int node = ExtJson_Get(doc, section, "center_y");
	double v;

	if (node < 0)
	{
		return 0;
	}
	v = ExtJson_Number(doc, node, -1.0);
	if ((v < 0.0) || (v > (double)EXTCRATE_CENTER_Y_MAX) || (v != (double)(int)v))
	{
		return -1;
	}
	return (int)v;
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
		{
			const double src = ExtJson_Number(doc, ExtJson_Get(doc, node, "index"), -1.0);

			out[n].srcIndex = ((src >= 0.0) && (src <= 1000000.0)) ? (int)src : -1;
		}
		out[n].capped = (u8)(ExtJson_Bool(doc, ExtJson_Get(doc, node, "capped"), 0) != 0);
		{
			const double dataY = ExtJson_Number(doc, ExtJson_Get(doc, node, "dataY"), 0.0);
			out[n].dataY = (s16)((dataY < -32768.0) ? -32768 : ((dataY > 32767.0) ? 32767 : ((dataY < 0.0) ? (dataY - 0.5) : (dataY + 0.5))));
		}
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
	int capped = 0;
	int centerY;
	int i;

	s_extCrateCount = 0;
	s_extCrateCenterY = 0;
	ExtCrates_LogReset();
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
	centerY = ExtCrates_ReadCenterY(doc, ExtTrack_Section("crates"));
	if (centerY < 0)
	{
		Platform_Log("[CTR ExtCrates] crates.center_y is not an integer 0..%d - contact around the instance origin\n", EXTCRATE_CENTER_Y_MAX);
		centerY = 0;
	}
	s_extCrateCenterY = centerY;

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
		ExtDraw_TintInstance(inst);
		c->inst = inst;
		c->index = i;
		c->smashed = 0;
		c->srcIndex = defs[i].srcIndex;
		s_extCrateCount++;
		capped += defs[i].capped;
		if ((s_extCrateBuilds == 1) && (s_extCrateCenterY == 0))
		{
			Platform_Log("[CTR ExtCrates] crate %d pos %d %d %d yaw %d\n", i, defs[i].pos[0], defs[i].pos[1], defs[i].pos[2], defs[i].yaw);
		}
		else if ((s_extCrateBuilds == 1) && defs[i].capped)
		{
			Platform_Log("[CTR ExtCrates] crate %d pos %d %d %d yaw %d centre %d capped (data centre %d)\n", i, defs[i].pos[0], defs[i].pos[1], defs[i].pos[2],
			             defs[i].yaw, defs[i].pos[1] + s_extCrateCenterY, defs[i].dataY);
		}
		else if (s_extCrateBuilds == 1)
		{
			Platform_Log("[CTR ExtCrates] crate %d pos %d %d %d yaw %d centre %d\n", i, defs[i].pos[0], defs[i].pos[1], defs[i].pos[2], defs[i].yaw,
			             defs[i].pos[1] + s_extCrateCenterY);
		}
	}
	if ((s_extCrateBuilds == 1) && (s_extCrateCenterY != 0))
	{
		Platform_Log("[CTR ExtCrates] contact around the crate's middle, %d above the instance origin (crates.center_y); %d crate(s) capped by the data\n",
		             s_extCrateCenterY, capped);
	}

	Platform_Log("[CTR ExtCrates] spawned %d of %d crate(s) (model 0x%02x, explosion 0x%02x %s), instance pool %d of %d taken, build %d\n", s_extCrateCount,
	             wanted, PU_RANDOM_CRATE, STATIC_CRATE_EXPLOSION, (gGT->modelPtr[STATIC_CRATE_EXPLOSION] != NULL) ? "ok" : "missing",
	             (int)gGT->JitPools.instance.maxItems - (int)gGT->JitPools.instance.free.count, (int)gGT->JitPools.instance.maxItems, s_extCrateBuilds);
	if (g_cfg_extCrateLog)
	{
		for (i = 0; i < s_extCrateCount; i++)
		{
			ExtCrates_LogReport(&s_extCrates[i], s_extCrateBuilds);
		}
	}
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
	// THE SAME FRAME AS RETAIL: a retail contact happens inside the drivers'
	// buckets, and the crate thread it gives birth to runs in the STATIC bucket
	// of the same frame (game/PROC.c:258-262 links it at the head, the buckets run
	// in order, game/MAIN/MainFrame.c:362-402): its first tick takes the cooldown
	// 30 -> 29 in the frame of the contact, and the crate is full size again 45
	// frames after it (RB_Crate.c:146-191). This pass runs after the game logic, so
	// a thread born here would first run one frame later (46). Its first tick runs
	// here instead. Every thread found here was born by this contact: the pass
	// skips crates that already have one (ExtCrates_OnLogicFrame).
	if (c->inst->thread != NULL)
	{
		RB_CrateAny_ThTick_Grow(c->inst->thread);
	}

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
	if (g_cfg_extCrateLog)
	{
		const int f = s_extCrateFrame;
		const int jumpAgo = ((seat == 0) && (s_extCrateJumpFrame >= 0)) ? (f - s_extCrateJumpFrame) : -1;

		s_extCrateHitFrame[c->index] = f;
		Platform_Log("[CTR ExtCrateLog] hit crate %d seat %d %s frame %d ground %s %s %s jumpago %d\n", c->index, seat, bot ? "bot" : "human", f,
		             ExtCrates_GroundText(ExtCrates_GroundAt(seat, f - 2)), ExtCrates_GroundText(ExtCrates_GroundAt(seat, f - 1)),
		             ExtCrates_GroundText(ExtCrates_GroundAt(seat, f)), jumpAgo);
	}
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

	if (g_cfg_extCrateLog)
	{
		// The state after the logic of this frame, before the contacts below.
		s_extCrateFrame++;
		for (seat = 0; seat < EXTCRATE_DRIVER_SLOTS; seat++)
		{
			const struct Driver *d = gGT->drivers[seat];

			if (d != NULL)
			{
				ExtCrates_GroundSet(seat, s_extCrateFrame, (d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0);
			}
		}
		if ((sdata->gGamepads != NULL) && ((sdata->gGamepads->gamepad[0].buttonsHeldCurrFrame & (BTN_R1 | BTN_L1)) != 0))
		{
			s_extCrateJumpFrame = s_extCrateFrame;
		}
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
			if (g_cfg_extCrateLog)
			{
				const int h = s_extCrateHitFrame[c->index];

				Platform_Log("[CTR ExtCrateLog] back crate %d frame %d hit %d delay %d\n", c->index, s_extCrateFrame, h, (h >= 0) ? (s_extCrateFrame - h) : -1);
			}
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
		y = d->posCurr.y >> 8;
		z = d->posCurr.z >> 8;

		for (i = 0; i < s_extCrateCount; i++)
		{
			struct ExtCrate *c = &s_extCrates[i];

			if ((c->inst->thread != NULL) || (c->inst->scale.x == 0))
			{
				continue;
			}
			if (!ExtCrates_KartTouches(x, y, z, c->inst->matrix.t[0], c->inst->matrix.t[1], c->inst->matrix.t[2], s_extCrateCenterY))
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
	s_extCrateCenterY = 0;
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
	static const char jsonMiddle[] = "{\"crates\": {\"center_y\": 48, \"items\": ["
	                                 "{\"index\": 0, \"pos\": [0, 387, 0], \"yaw\": 0},"
	                                 "{\"index\": 1, \"pos\": [0, 62, 0], \"yaw\": 0, \"capped\": true, \"dataY\": 230.4}"
	                                 "]}, \"bad1\": {\"center_y\": 300}, \"bad2\": {\"center_y\": 4.5}, \"bad3\": {\"center_y\": -1}}";
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

	// The kart against the crate (origin at y 0). Without center_y the kart
	// reaches origins from 126 below to 76 above it (as before); with the middle
	// 48 above the origin from 124 below to 78 above - the retail hitbox.
	EXTCRATE_CHECK(ExtCrates_KartTouches(0, -126, 0, 0, 0, 0, 0) && !ExtCrates_KartTouches(0, -127, 0, 0, 0, 0, 0));
	EXTCRATE_CHECK(ExtCrates_KartTouches(0, 76, 0, 0, 0, 0, 0) && !ExtCrates_KartTouches(0, 77, 0, 0, 0, 0, 0));
	EXTCRATE_CHECK(ExtCrates_KartTouches(0, -78, 0, 0, 0, 0, 48) && !ExtCrates_KartTouches(0, -79, 0, 0, 0, 0, 48));
	EXTCRATE_CHECK(ExtCrates_KartTouches(0, 124, 0, 0, 0, 0, 48) && !ExtCrates_KartTouches(0, 125, 0, 0, 0, 0, 48));
	EXTCRATE_CHECK(ExtCrates_KartTouches(101, 0, -101, 0, 0, 0, 48) && !ExtCrates_KartTouches(102, 0, 0, 0, 0, 0, 48));
	// A middle 110 above the floor (the converter's cap) is reached by a kart on that floor.
	EXTCRATE_CHECK(ExtCrates_KartTouches(0, 0, 0, 0, 110 - 48, 0, 48));

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
	// center_y missing = 0 (the old contact).
	EXTCRATE_CHECK(ExtCrates_ReadCenterY(&doc, ExtJson_Get(&doc, 0, "crates")) == 0);
	ExtJson_Free(&doc);

	// center_y and the cap of the data.
	EXTCRATE_CHECK(ExtJson_Parse(&doc, jsonMiddle, sizeof(jsonMiddle) - 1, why, (int)sizeof(why)) == 1);
	EXTCRATE_CHECK(ExtCrates_ReadCenterY(&doc, ExtJson_Get(&doc, 0, "crates")) == 48);
	EXTCRATE_CHECK(ExtCrates_ReadCenterY(&doc, ExtJson_Get(&doc, 0, "bad1")) == -1);
	EXTCRATE_CHECK(ExtCrates_ReadCenterY(&doc, ExtJson_Get(&doc, 0, "bad2")) == -1);
	EXTCRATE_CHECK(ExtCrates_ReadCenterY(&doc, ExtJson_Get(&doc, 0, "bad3")) == -1);
	n = ExtCrates_ReadItems(&doc, ExtJson_Get(&doc, 0, "crates"), defs, EXTCRATE_MAX, &bad, &total);
	EXTCRATE_CHECK((n == 2) && (bad == 0) && (defs[0].capped == 0) && (defs[1].capped == 1) && (defs[1].dataY == 230) && (defs[1].pos[1] == 62));
	ExtJson_Free(&doc);

	// The data's own numbering is kept for the log (missing = -1).
	EXTCRATE_CHECK(ExtJson_Parse(&doc, json, sizeof(json) - 1, why, (int)sizeof(why)) == 1);
	n = ExtCrates_ReadItems(&doc, ExtJson_Get(&doc, 0, "crates"), defs, EXTCRATE_MAX, &bad, &total);
	EXTCRATE_CHECK((n == 2) && (defs[0].srcIndex == 0) && (defs[1].srcIndex == 1));
	ExtJson_Free(&doc);

	// The measuring lines: off unless asked for; the ground ring answers only for
	// the frames it holds (the last EXTCRATE_LOG_RING), -1 before and after.
	EXTCRATE_CHECK(g_cfg_extCrateLog == 0);
	ExtCrates_LogReset();
	EXTCRATE_CHECK((ExtCrates_GroundAt(0, 0) == -1) && (ExtCrates_GroundAt(0, -1) == -1) && (s_extCrateJumpFrame == -1));
	ExtCrates_GroundSet(3, 10, 1);
	ExtCrates_GroundSet(3, 11, 0);
	ExtCrates_GroundSet(3, 12, 1);
	EXTCRATE_CHECK((ExtCrates_GroundAt(3, 10) == 1) && (ExtCrates_GroundAt(3, 11) == 0) && (ExtCrates_GroundAt(3, 12) == 1));
	EXTCRATE_CHECK((ExtCrates_GroundAt(3, 9) == -1) && (ExtCrates_GroundAt(2, 12) == -1) && (ExtCrates_GroundAt(8, 12) == -1));
	ExtCrates_GroundSet(3, 14, 0);
	EXTCRATE_CHECK((ExtCrates_GroundAt(3, 10) == -1) && (ExtCrates_GroundAt(3, 14) == 0) && (ExtCrates_GroundAt(3, 12) == 1));
	EXTCRATE_CHECK((strcmp(ExtCrates_GroundText(-1), "-") == 0) && (strcmp(ExtCrates_GroundText(0), "0") == 0) && (strcmp(ExtCrates_GroundText(1), "1") == 0));
	ExtCrates_LogReset();
	EXTCRATE_CHECK((ExtCrates_GroundAt(3, 12) == -1) && (s_extCrateHitFrame[0] == -1) && (s_extCrateHitFrame[EXTCRATE_MAX - 1] == -1));

	// Nothing of the running game was touched.
	EXTCRATE_CHECK((s_extCrateCount == 0) && (s_extCrateBuilds == 0) && (s_extCrateCenterY == 0));

#undef EXTCRATE_CHECK

	if (failures != 0)
	{
		printf("ext selftest crates FAILED: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	printf("ext selftest crates passed: %d checks, 0 failures\n", checks);
	return 0;
}
