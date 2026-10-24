// ===========================================================================
// FREE FLY (include/platform/native_fly.h; round "Free Fly simple").
//
// THE PARTS: the state machine, the ceiling and the script (pure); the hooks
// the game asks (NativeFly_KartButtons from the kart's pad reads,
// NativeFly_Gravity and NativeFly_Velocity from VehPhysForce_OnGravity,
// NativeFly_AirTimeStill from the glide's air-time hook); the script on the
// kart steerer's pad word; the lines of the log.
//
// THE RULE OF THE HOOKS: every hook asks the scope gate (NativeFly_InScope)
// first - or a state only the gate sets - and hands its input back at once for
// every driver outside it, so a race without an in-scope driver takes the
// same paths with the same numbers as without this file.
// ===========================================================================

#include <platform/native_fly.h>
#include <platform/native_mods.h>
#include <psx/gtereg.h>

int g_cfg_flyLog = 0;
char g_cfg_flyScript[NATIVE_FLY_ARG_MAX];

// ---------------------------------------------------------------------------
// THE PURE PARTS
// ---------------------------------------------------------------------------

internal int NativeFly_Clamp(int v, int lo, int hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void NativeFly_TuneDefaults(NativeFlyTune *t)
{
	t->climbRate = NATIVE_FLY_CLIMB_DEFAULT;
	t->doubleTap = NATIVE_FLY_TAP_DEFAULT;
	t->band = NATIVE_FLY_BAND_DEFAULT;
	t->margin = NATIVE_FLY_MARGIN_DEFAULT;
}

void NativeFly_TuneClamp(NativeFlyTune *t)
{
	t->climbRate = NativeFly_Clamp(t->climbRate, NATIVE_FLY_CLIMB_MIN, NATIVE_FLY_CLIMB_MAX);
	t->doubleTap = NativeFly_Clamp(t->doubleTap, NATIVE_FLY_TAP_MIN, NATIVE_FLY_TAP_MAX);
	t->band = NativeFly_Clamp(t->band, NATIVE_FLY_BAND_MIN, NATIVE_FLY_BAND_MAX);
	t->margin = NativeFly_Clamp(t->margin, NATIVE_FLY_MARGIN_MIN, NATIVE_FLY_MARGIN_MAX);
}

int NativeFly_ScopeRule(int modActive, int driverID, int bot, int vehicle)
{
	return modActive && (driverID == 0) && !bot && ((vehicle == (int)RLDCHAR_VEHICLE_HOVER) || (vehicle == (int)RLDCHAR_VEHICLE_PLANE));
}

void NativeFly_StateReset(NativeFlyState *s)
{
	memset(s, 0, sizeof(*s));
	s->state = NATIVE_FLY_GROUND;
	s->sinceTap = NATIVE_FLY_TAP_NONE;
}

void NativeFly_Step(NativeFlyState *s, const NativeFlyTune *tune, const NativeFlyIn *in, NativeFlyOut *out)
{
	NativeFlyTune t = *tune;
	const int pressed = in->r1Held && !s->heldPrev;

	NativeFly_TuneClamp(&t);
	memset(out, 0, sizeof(*out));
	out->pressed = pressed;
	s->heldPrev = in->r1Held ? 1 : 0;
	if (s->sinceTap < NATIVE_FLY_TAP_NONE)
	{
		s->sinceTap++;
	}

	if (!in->eligible)
	{
		if (s->state != NATIVE_FLY_GROUND)
		{
			s->state = NATIVE_FLY_GROUND;
			out->event = NATIVE_FLY_EV_END;
		}
		return;
	}

	switch (s->state)
	{
	case NATIVE_FLY_GROUND:
		if (pressed && in->onGround)
		{
			s->state = NATIVE_FLY_CLIMB;
			s->sinceTap = 0;
			s->airSeen = 0;
			out->event = NATIVE_FLY_EV_TAKEOFF;
		}
		break;

	case NATIVE_FLY_CLIMB:
	case NATIVE_FLY_HOLD:
		if (in->onGround && s->airSeen)
		{
			s->state = NATIVE_FLY_GROUND;
			out->event = NATIVE_FLY_EV_LAND;
			break;
		}
		if (!in->onGround)
		{
			s->airSeen = 1;
		}
		if (pressed)
		{
			if (s->sinceTap <= t.doubleTap)
			{
				s->state = NATIVE_FLY_FALL;
				out->event = NATIVE_FLY_EV_DOUBLE_TAP;
				break;
			}
			s->sinceTap = 0;
		}
		s->state = in->r1Held ? NATIVE_FLY_CLIMB : NATIVE_FLY_HOLD;
		if ((s->state == NATIVE_FLY_HOLD) && in->onGround && !s->airSeen)
		{
			s->state = NATIVE_FLY_GROUND;
			out->event = NATIVE_FLY_EV_ABORT;
		}
		break;

	case NATIVE_FLY_FALL:
		if (!in->onGround)
		{
			s->airSeen = 1;
		}
		else
		{
			s->state = NATIVE_FLY_GROUND;
			out->event = s->airSeen ? NATIVE_FLY_EV_LAND : NATIVE_FLY_EV_ABORT;
		}
		break;

	default:
		NativeFly_StateReset(s);
		break;
	}

	out->flying = (s->state == NATIVE_FLY_CLIMB) || (s->state == NATIVE_FLY_HOLD);
	out->climbing = (s->state == NATIVE_FLY_CLIMB);
}

void NativeFly_GroundNote(NativeFlyGround *g, int hit, int groundY, int kartY)
{
	if (hit)
	{
		g->valid = 1;
		g->groundY = groundY;
	}
	else if (!g->valid)
	{
		g->groundY = kartY;
	}
}

int NativeFly_Ceiling(int groundY, int band, int cap)
{
	const int relative = groundY + band;
	const int ceiling = (relative < cap) ? relative : cap;

	return (ceiling > NATIVE_FLY_CEILING_MAX) ? NATIVE_FLY_CEILING_MAX : ceiling;
}

int NativeFly_Cap(int highestY, int margin)
{
	const int cap = highestY + margin;

	return (cap > NATIVE_FLY_CEILING_MAX) ? NATIVE_FLY_CEILING_MAX : cap;
}

int NativeFly_VelocityY(int climbing, int climbRate, int posY, int ceiling, int ms, int *capped)
{
	// The position moves by velocity x ms / 32 (game/COLL.c
	// CollMoved_PlayerSearch_StepVelocity), so room x 32 / ms reaches the
	// ceiling in this tick and not further.
	const long long room = (long long)ceiling - (long long)posY;
	const long long limit = room * 32 / ((ms > 0) ? ms : 32);
	long long v = climbing ? ((long long)climbRate << 8) : 0;

	*capped = 0;
	if (v > limit)
	{
		v = limit;
		*capped = 1;
	}
	if (v < -NATIVE_FLY_SINK_MAX)
	{
		v = -NATIVE_FLY_SINK_MAX;
	}
	return (int)v;
}

// The track rule of phase 0 F8: what the player's probe lands on.
internal int NativeFly_TrackQuad(unsigned int flags)
{
	return ((flags & QUADBLOCK_FLAG_GROUND) != 0) && ((flags & (QUADBLOCK_FLAG_NO_COLLISION_RESPONSE | QUADBLOCK_FLAG_KILL_PLANE)) == 0);
}

int NativeFly_HighestOf(int count, const unsigned short *flags, const short *ys, int *highest)
{
	int found = 0;
	int best = 0;
	int i;
	int k;

	for (i = 0; i < count; i++)
	{
		if (!NativeFly_TrackQuad(flags[i]))
		{
			continue;
		}
		for (k = 0; k < 9; k++)
		{
			const int y = ys[(9 * i) + k];

			if (!found || (y > best))
			{
				best = y;
				found = 1;
			}
		}
	}
	if (found)
	{
		*highest = best;
	}
	return found;
}

// ---------------------------------------------------------------------------
// THE SCRIPT (--dev-fly-script; pure)
// ---------------------------------------------------------------------------

int NativeFly_ScriptParse(const char *text, int *codes, int *args, int *count, char *why, int whySize)
{
	const char *p = text;
	int n = 0;

	if (count != NULL)
	{
		*count = 0;
	}
	if ((text == NULL) || (text[0] == '\0'))
	{
		snprintf(why, (size_t)whySize, "switch --dev-fly-script expects a comma list of c<n> h<n> d l");
		return 0;
	}
	while (1)
	{
		int code;
		int arg = 0;

		if ((*p == 'c') || (*p == 'h'))
		{
			int digits = 0;

			code = (*p == 'c') ? NATIVE_FLY_MAN_CLIMB : NATIVE_FLY_MAN_HOLD;
			p++;
			while ((*p >= '0') && (*p <= '9') && (digits < 4))
			{
				arg = (arg * 10) + (*p - '0');
				p++;
				digits++;
			}
			if ((digits == 0) || (arg < 1) || (arg > 999))
			{
				snprintf(why, (size_t)whySize, "switch --dev-fly-script: '%s' needs c<n> / h<n> with n 1..999", text);
				return 0;
			}
		}
		else if ((*p == 'd') || (*p == 'l'))
		{
			code = (*p == 'd') ? NATIVE_FLY_MAN_DOUBLE : NATIVE_FLY_MAN_LAND;
			p++;
		}
		else
		{
			snprintf(why, (size_t)whySize, "switch --dev-fly-script: '%s' has a word that is no manoeuvre (c<n> h<n> d l)", text);
			return 0;
		}
		if ((*p != ',') && (*p != '\0'))
		{
			snprintf(why, (size_t)whySize, "switch --dev-fly-script: '%s' has a word that is no manoeuvre (c<n> h<n> d l)", text);
			return 0;
		}
		if (n >= NATIVE_FLY_SCRIPT_MAX)
		{
			snprintf(why, (size_t)whySize, "switch --dev-fly-script: more than %d manoeuvres", NATIVE_FLY_SCRIPT_MAX);
			return 0;
		}
		if (codes != NULL)
		{
			codes[n] = code;
		}
		if (args != NULL)
		{
			args[n] = arg;
		}
		n++;
		if (*p == '\0')
		{
			break;
		}
		p++; // the comma
		if (*p == '\0')
		{
			snprintf(why, (size_t)whySize, "switch --dev-fly-script: '%s' ends with a comma", text);
			return 0;
		}
	}
	if (count != NULL)
	{
		*count = n;
	}
	return 1;
}

void NativeFly_ScriptReset(NativeFlyScript *p)
{
	p->index = 0;
	p->ticks = 0;
	p->phase = NATIVE_FLY_PH_WAIT;
	p->rounds = 0;
}

// The R1 of the manoeuvre playing at its tick p->ticks (0 = its first);
// 1 when the manoeuvre is done after this tick.
internal int NativeFly_ScriptMan(const NativeFlyScript *p, const NativeFlyScriptIn *in, NativeFlyScriptOut *out)
{
	const int code = p->codes[p->index];
	const int arg = p->args[p->index];

	switch (code)
	{
	case NATIVE_FLY_MAN_CLIMB:
		out->r1 = 1;
		return (p->ticks + 1) >= arg;
	case NATIVE_FLY_MAN_HOLD:
		return (p->ticks + 1) >= arg;
	case NATIVE_FLY_MAN_DOUBLE:
		out->r1 = ((p->ticks == 1) || (p->ticks == 3)) ? 1 : 0;
		return (p->ticks + 1) >= NATIVE_FLY_SCRIPT_TAP_TICKS;
	default: // NATIVE_FLY_MAN_LAND
		if (in->onGround)
		{
			out->event = 3;
			return 1;
		}
		if (p->ticks >= NATIVE_FLY_SCRIPT_LAND_MAX)
		{
			out->event = 4;
			return 1;
		}
		return 0;
	}
}

void NativeFly_ScriptStep(NativeFlyScript *p, const NativeFlyScriptIn *in, NativeFlyScriptOut *out)
{
	memset(out, 0, sizeof(*out));

	if (!in->racing || in->grabbed)
	{
		if (p->phase == NATIVE_FLY_PH_RUN)
		{
			out->event = 5;
		}
		p->phase = NATIVE_FLY_PH_WAIT;
		p->ticks = 0;
		p->index = 0;
		return;
	}

	if (p->phase == NATIVE_FLY_PH_WAIT)
	{
		p->ticks = in->onGround ? (p->ticks + 1) : 0;
		if ((p->count <= 0) || (p->ticks < NATIVE_FLY_SCRIPT_GROUND_TICKS))
		{
			return;
		}
		p->phase = NATIVE_FLY_PH_RUN;
		p->index = 0;
		p->ticks = 0;
		p->rounds++;
		out->event = 1;
	}
	else if (p->ticks == 0)
	{
		out->event = 2;
	}

	{
		const int ev = out->event;

		if (NativeFly_ScriptMan(p, in, out))
		{
			p->index++;
			p->ticks = 0;
			if (p->index >= p->count)
			{
				p->phase = NATIVE_FLY_PH_WAIT;
				p->index = 0;
			}
		}
		else
		{
			p->ticks++;
		}
		// A start event stays the tick's event unless 'l' ended in it.
		if ((out->event != 3) && (out->event != 4))
		{
			out->event = ev;
		}
	}
}

// ---------------------------------------------------------------------------
// THE MODULE (the game's side)
// ---------------------------------------------------------------------------

global_variable struct
{
	int inited;
	NativeFlyTune tune;
	NativeFlyState st;
	NativeFlyGround ground;
	// What a reset is keyed on: seat 0's driver, the level, the level's frames.
	const struct Driver *driver;
	const struct Level *level;
	int frames;
	int cap;
	int jumpsPrev;
	// The tick the gravity hook stepped (the velocity and air-time hooks ask).
	const struct Driver *tickDriver;
	int tickTimer;
	int flying;
	int climbing;
	// The script.
	NativeFlyScript script;
	int scriptReady;
	const struct Driver *scriptDriver;
	const struct Level *scriptLevel;
	int scriptFrames;
	// Counters for the line at exit.
	long long ticks, takeoffs, climbTicks, holdTicks, doubleTaps, landings, ends, aborts, stripped, hopStarts, driftTicks;
	long long ceilingHits, exceeded;
	int maxAbove, maxExcess;
	int reportArmed;
} s_fly;

internal void NativeFly_Ensure(void)
{
	if (!s_fly.inited)
	{
		s_fly.inited = 1;
		NativeFly_TuneDefaults(&s_fly.tune);
		NativeFly_StateReset(&s_fly.st);
	}
}

NativeFlyTune *NativeFly_Tunables(void)
{
	NativeFly_Ensure();
	return &s_fly.tune;
}

internal void NativeFly_Report(void)
{
	Platform_Log("[CTR Fly] at exit: in scope %lld tick(s); take-offs %lld, climb ticks %lld, hold ticks %lld, double taps %lld, landings %lld, ends %lld, "
	             "aborts %lld; R1/L1 presses taken from the kart %lld, hop starts %lld, drift ticks %lld; max height above ground %d WE, ceiling hits %lld, "
	             "ceiling exceeded %lld (max %d WE)\n",
	             s_fly.ticks, s_fly.takeoffs, s_fly.climbTicks, s_fly.holdTicks, s_fly.doubleTaps, s_fly.landings, s_fly.ends, s_fly.aborts, s_fly.stripped,
	             s_fly.hopStarts, s_fly.driftTicks, s_fly.maxAbove, s_fly.ceilingHits, s_fly.exceeded, s_fly.maxExcess);
}

int NativeFly_InScope(const struct Driver *d)
{
	// The cheap words first; the mod and the class only for seat 0 as a human.
	if ((d == NULL) || (d->driverID != 0) || ((d->actionsFlagSet & ACTION_BOT) != 0))
	{
		return 0;
	}
	return NativeFly_ScopeRule(NativeMods_Active(NATIVE_MODS_FREEFLY), d->driverID, 0, NativeChar_SeatVehicleClass(0));
}

unsigned int NativeFly_KartButtons(const struct Driver *d, unsigned int buttons, int tapped)
{
	if (((buttons & (BTN_R1 | BTN_L1)) == 0) || !NativeFly_InScope(d))
	{
		return buttons;
	}
	if (tapped)
	{
		s_fly.stripped++;
	}
	return buttons & ~(unsigned int)(BTN_R1 | BTN_L1);
}

// The highest track point of the loaded level (phase 0 F8's rule), straight
// from its quadblocks and vertices. 0 = none.
internal int NativeFly_LevelHighest(const struct Level *level, int *highest)
{
	const struct mesh_info *mesh = (level != NULL) ? level->ptr_mesh_info : NULL;
	int found = 0;
	int best = 0;
	int i;
	int k;

	if ((mesh == NULL) || (mesh->ptrQuadBlockArray == NULL) || (mesh->ptrVertexArray == NULL))
	{
		return 0;
	}
	for (i = 0; i < mesh->numQuadBlock; i++)
	{
		const struct QuadBlock *q = &mesh->ptrQuadBlockArray[i];

		if (!NativeFly_TrackQuad(q->quadFlags))
		{
			continue;
		}
		for (k = 0; k < 9; k++)
		{
			int y;

			if ((int)q->index[k] >= mesh->numVertex)
			{
				continue;
			}
			y = mesh->ptrVertexArray[q->index[k]].pos.y;
			if (!found || (y > best))
			{
				best = y;
				found = 1;
			}
		}
	}
	if (found)
	{
		*highest = best;
	}
	return found;
}

internal void NativeFly_LevelLoad(const struct Driver *d, struct GameTracker *gGT)
{
	NativeFlyTune t = s_fly.tune;
	int highest = 0;
	const int found = NativeFly_LevelHighest(gGT->level1, &highest);
	const int vehicle = NativeChar_SeatVehicleClass(0);
	char highText[16];

	NativeFly_TuneClamp(&t);
	NativeFly_StateReset(&s_fly.st);
	memset(&s_fly.ground, 0, sizeof(s_fly.ground));
	s_fly.driver = d;
	s_fly.level = gGT->level1;
	s_fly.cap = found ? NativeFly_Cap(highest, t.margin) : NATIVE_FLY_CEILING_MAX;
	s_fly.jumpsPrev = d->numberOfJumps;
	if (found)
	{
		snprintf(highText, sizeof(highText), "%d WE", highest);
	}
	else
	{
		snprintf(highText, sizeof(highText), "none");
	}
	Platform_Log("[CTR Fly] level: seat 0 class %s, band %d WE, margin %d WE, highest track point %s, ceiling cap %d WE; band + camera height %d + %d = %d WE "
	             "< %d WE: %s\n",
	             (vehicle == (int)RLDCHAR_VEHICLE_PLANE) ? "plane" : "hover", t.band, t.margin, highText, s_fly.cap, t.band, NATIVE_FLY_CAM_HEIGHT,
	             t.band + NATIVE_FLY_CAM_HEIGHT, NATIVE_FLY_PVS_LIMIT, ((t.band + NATIVE_FLY_CAM_HEIGHT) < NATIVE_FLY_PVS_LIMIT) ? "yes" : "NO");
}

internal const char *NativeFly_EventName(int ev)
{
	static const char *const names[6] = {"-", "takeoff", "double-tap", "land", "end", "abort"};

	return ((ev >= 0) && (ev < 6)) ? names[ev] : "?";
}

int NativeFly_Gravity(const struct Driver *d, int gravityY)
{
	struct GameTracker *gGT;
	const struct GamepadBuffer *pad;
	NativeFlyIn in;
	NativeFlyOut out;

	if (!NativeFly_InScope(d) || (sdata->gGT == NULL) || (sdata->gGamepads == NULL))
	{
		return gravityY;
	}
	gGT = sdata->gGT;
	NativeFly_Ensure();
	if (!s_fly.reportArmed)
	{
		s_fly.reportArmed = 1;
		Platform_AtExitReport(NativeFly_Report);
	}

	// No state survives a load or another driver on seat 0.
	if ((d != s_fly.driver) || (gGT->level1 != s_fly.level) || (gGT->framesInThisLEV < s_fly.frames))
	{
		NativeFly_LevelLoad(d, gGT);
	}
	s_fly.frames = gGT->framesInThisLEV;

	// R1 as the kart's own input reads its pad (VehPhysProc.c PhysLinear):
	// nothing in the end-of-race menu. Read only.
	pad = &sdata->gGamepads->gamepad[0];
	memset(&in, 0, sizeof(in));
	in.eligible = (d->kartState == KS_NORMAL) && ((gGT->gameMode1 & END_OF_RACE) == 0);
	in.onGround = (d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0;
	in.r1Held = ((gGT->gameMode1 & END_OF_RACE) == 0) && ((pad->buttonsHeldCurrFrame & BTN_R1) != 0);
	NativeFly_Step(&s_fly.st, &s_fly.tune, &in, &out);

	s_fly.ticks++;
	s_fly.takeoffs += (out.event == NATIVE_FLY_EV_TAKEOFF);
	s_fly.doubleTaps += (out.event == NATIVE_FLY_EV_DOUBLE_TAP);
	s_fly.landings += (out.event == NATIVE_FLY_EV_LAND);
	s_fly.ends += (out.event == NATIVE_FLY_EV_END);
	s_fly.aborts += (out.event == NATIVE_FLY_EV_ABORT);
	s_fly.climbTicks += out.climbing;
	s_fly.holdTicks += (out.flying && !out.climbing);
	s_fly.driftTicks += (d->kartState == KS_DRIFTING);
	if (d->numberOfJumps != s_fly.jumpsPrev)
	{
		s_fly.hopStarts += (s16)(d->numberOfJumps - s_fly.jumpsPrev);
		s_fly.jumpsPrev = d->numberOfJumps;
	}

	s_fly.tickDriver = d;
	s_fly.tickTimer = gGT->timer;
	s_fly.flying = out.flying;
	s_fly.climbing = out.climbing;

	if (g_cfg_flyLog && (out.event != NATIVE_FLY_EV_NONE))
	{
		Platform_Log("[CTR Fly] tick %d: %s y %d ground %d valid %d ks %d\n", gGT->timer, NativeFly_EventName(out.event), (int)(d->posCurr.y >> 8),
		             s_fly.ground.groundY, s_fly.ground.valid, (int)d->kartState);
	}

	// While flying the game's gravity does not pull: the velocity hook below
	// writes the vertical speed.
	return out.flying ? 0 : gravityY;
}

// The ground below the kart: the game's ray as the bots ask it (game/BOTS.c
// BOTS_ThTick_Drive, the probe of ESTIMATE_NAV / FREE_PHYSICS), with the
// scratchpad and the GTE registers put back afterwards, so nothing the game
// reads later sees the search.
internal int NativeFly_GroundBelow(const struct Driver *d, int *groundY)
{
	static u8 saved[CTR_SCRATCHPAD_SIZE];
	struct GameTracker *gGT = sdata->gGT;
	const GTERegisters gte = gteRegs;
	struct ScratchpadStruct *sps;
	const int x = d->posCurr.x >> 8;
	const int y = d->posCurr.y >> 8;
	const int z = d->posCurr.z >> 8;
	SVec3 low;
	SVec3 high;
	int hit;

	if ((gGT->level1 == NULL) || (gGT->level1->ptr_mesh_info == NULL) || (gGT->level1->ptr_mesh_info->bspRoot == NULL))
	{
		return 0;
	}
	memcpy(saved, CTR_SCRATCHPAD_BASE, CTR_SCRATCHPAD_SIZE);
	sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);
	low.x = high.x = (s16)NativeFly_Clamp(x, -32768, 32767);
	low.z = high.z = (s16)NativeFly_Clamp(z, -32768, 32767);
	high.y = (s16)NativeFly_Clamp(y + NATIVE_FLY_RAY_UP, -32768, 32767);
	low.y = (s16)NativeFly_Clamp(y - NATIVE_FLY_RAY_DOWN, -32768, 32767);
	sps->ptr_mesh_info = gGT->level1->ptr_mesh_info;
	sps->Union.QuadBlockColl.quadFlagsWanted = QUADBLOCK_FLAG_GROUND;
	sps->Union.QuadBlockColl.quadFlagsIgnored = QUADBLOCK_FLAG_NO_COLLISION_RESPONSE;
	sps->Union.QuadBlockColl.searchFlags = COLL_SEARCH_HIGH_LOD;
	// The ray runs from the second point to the first (the bots' order).
	COLL_SearchBSP_CallbackQUADBLK(&low, &high, sps, 0);
	hit = sps->boolDidTouchQuadblock != 0;
	if (hit)
	{
		*groundY = sps->Union.QuadBlockColl.hitPos.y;
	}
	memcpy(CTR_SCRATCHPAD_BASE, saved, CTR_SCRATCHPAD_SIZE);
	gteRegs = gte;
	return hit;
}

void NativeFly_Velocity(const struct Driver *d, int *velocityY)
{
	NativeFlyTune t;
	int groundY = 0;
	int hit;
	int ceiling;
	int capped;
	int v;
	int after;
	int above;
	const int ms = sdata->gGT->elapsedTimeMS;

	if (!s_fly.flying || (d == NULL) || (d != s_fly.tickDriver) || (sdata->gGT->timer != s_fly.tickTimer))
	{
		return;
	}
	t = s_fly.tune;
	NativeFly_TuneClamp(&t);
	hit = NativeFly_GroundBelow(d, &groundY);
	NativeFly_GroundNote(&s_fly.ground, hit, groundY, (int)(d->posCurr.y >> 8));
	ceiling = NativeFly_Ceiling(s_fly.ground.groundY, t.band, s_fly.cap);
	v = NativeFly_VelocityY(s_fly.climbing, t.climbRate, d->posCurr.y, ceiling << 8, ms, &capped);
	*velocityY = v;

	s_fly.ceilingHits += (s_fly.climbing && capped);
	after = (int)((d->posCurr.y + (((long long)v * ms) / 32)) >> 8);
	if (after > ceiling)
	{
		s_fly.exceeded++;
		if ((after - ceiling) > s_fly.maxExcess)
		{
			s_fly.maxExcess = after - ceiling;
		}
	}
	above = after - s_fly.ground.groundY;
	if (above > s_fly.maxAbove)
	{
		s_fly.maxAbove = above;
	}
}

int NativeFly_AirTimeStill(const struct Driver *d)
{
	return s_fly.flying && (d != NULL) && (d == s_fly.tickDriver) && (sdata->gGT != NULL) && (sdata->gGT->timer == s_fly.tickTimer);
}

// ---------------------------------------------------------------------------
// THE SCRIPT ON THE PAD WORD
// ---------------------------------------------------------------------------

unsigned short NativeFly_DevPadWord(const struct GameTracker *gGT, const struct Driver *d, unsigned short word)
{
	NativeFlyScriptIn in;
	NativeFlyScriptOut out;

	if ((g_cfg_flyScript[0] == '\0') || (gGT == NULL) || (d == NULL))
	{
		return word;
	}
	if (!s_fly.scriptReady)
	{
		char why[200];

		s_fly.scriptReady = 1;
		if (!NativeFly_ScriptParse(g_cfg_flyScript, s_fly.script.codes, s_fly.script.args, &s_fly.script.count, why, (int)sizeof(why)))
		{
			s_fly.script.count = 0; // checked before any window; never here
		}
		NativeFly_ScriptReset(&s_fly.script);
	}
	if ((d != s_fly.scriptDriver) || (gGT->level1 != s_fly.scriptLevel) || (gGT->framesInThisLEV < s_fly.scriptFrames))
	{
		s_fly.scriptDriver = d;
		s_fly.scriptLevel = gGT->level1;
		NativeFly_ScriptReset(&s_fly.script);
	}
	s_fly.scriptFrames = gGT->framesInThisLEV;

	memset(&in, 0, sizeof(in));
	in.racing = ((d->actionsFlagSet & ACTION_BOT) == 0) && ((gGT->gameMode1 & END_OF_RACE) == 0);
	in.onGround = (d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0;
	in.grabbed = d->kartState == KS_MASK_GRABBED;
	NativeFly_ScriptStep(&s_fly.script, &in, &out);
	if (g_cfg_flyLog && (out.event != 0))
	{
		static const char *const what[6] = {"", "list start", "manoeuvre", "landed", "no landing", "reset"};

		Platform_Log("[CTR Fly] script tick %d: %s, round %d, manoeuvre %d, y %d\n", gGT->timer, what[out.event], s_fly.script.rounds, s_fly.script.index,
		             (int)(d->posCurr.y >> 8));
	}
	if (out.r1)
	{
		word &= (unsigned short)~RAW_BTN_R1;
	}
	return word;
}
