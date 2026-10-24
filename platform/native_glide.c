// ===========================================================================
// THE GLIDE ABILITY (TEST) (include/platform/native_glide.h; round "Dummy
// Glider", phase 1b, step C).
//
// THE PARTS: the state machine (NativeGlide_Step, pure); the two hooks the
// game asks (NativeGlide_Gravity from VehPhysForce_OnGravity, which steps the
// machine once per logic tick of seat 0, and NativeGlide_LandingBoost from
// COLL_FIXED_PlayerSearch); the wings on screen (NativeGlide_EngineTick, from
// the hi-res driver's tick); the dev switches (the log, the script for
// --dev-kart-steer, the dump of a landing's drawn vertices).
//
// THE RULE OF THE HOOKS: with the switch off the first line returns the value
// given and nothing here is touched. With it on, only seat 0 as a human driver
// steps the machine; every other driver gets its value back at once. The
// machine leaves FOLDED only on the edge of R3 in the air, so a race in which
// R3 is never pressed gets every value back unchanged.
// ===========================================================================

#include <platform/native_glide.h>
#include <platform/engine.h>

int g_cfg_glideLog = 0;
char g_cfg_glideScript[NATIVE_GLIDE_ARG_MAX];
char g_cfg_glideDump[NATIVE_GLIDE_ARG_MAX];

// ---------------------------------------------------------------------------
// THE STATE MACHINE (pure)
// ---------------------------------------------------------------------------

void NativeGlide_TuneDefaults(NativeGlideTune *t)
{
	t->glide = NATIVE_GLIDE_GLIDE_DEFAULT;
	t->dive = NATIVE_GLIDE_DIVE_DEFAULT;
	t->pullUp = NATIVE_GLIDE_PULL_UP_DEFAULT;
	t->openFrames = NATIVE_GLIDE_OPEN_DEFAULT;
	t->closeRelease = NATIVE_GLIDE_CLOSE_RELEASE_DEFAULT;
	t->closeLand = NATIVE_GLIDE_CLOSE_LAND_DEFAULT;
}

internal int NativeGlide_Clamp(int v, int lo, int hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void NativeGlide_TuneClamp(NativeGlideTune *t)
{
	t->glide = NativeGlide_Clamp(t->glide, NATIVE_GLIDE_FACTOR_MIN, NATIVE_GLIDE_FACTOR_MAX);
	t->dive = NativeGlide_Clamp(t->dive, NATIVE_GLIDE_FACTOR_MIN, NATIVE_GLIDE_FACTOR_MAX);
	t->pullUp = NativeGlide_Clamp(t->pullUp, NATIVE_GLIDE_FACTOR_MIN, NATIVE_GLIDE_FACTOR_MAX);
	t->openFrames = NativeGlide_Clamp(t->openFrames, NATIVE_GLIDE_FRAMES_MIN, NATIVE_GLIDE_FRAMES_MAX);
	t->closeRelease = NativeGlide_Clamp(t->closeRelease, NATIVE_GLIDE_FRAMES_MIN, NATIVE_GLIDE_FRAMES_MAX);
	t->closeLand = NativeGlide_Clamp(t->closeLand, NATIVE_GLIDE_FRAMES_MIN, NATIVE_GLIDE_FRAMES_MAX);
}

void NativeGlide_StateReset(NativeGlideState *s)
{
	memset(s, 0, sizeof(*s));
	s->state = NATIVE_GLIDE_FOLDED;
	s->flapStep = -1;
}

int NativeGlide_StickForward(int stickLY)
{
	if (stickLY >= NATIVE_GLIDE_STICK_DEAD_LOW)
	{
		return 0;
	}
	if (stickLY < 0)
	{
		stickLY = 0;
	}
	return (NATIVE_GLIDE_STICK_DEAD_LOW - stickLY) * 1000 / NATIVE_GLIDE_STICK_DEAD_LOW;
}

int NativeGlide_StickBack(int stickLY)
{
	if (stickLY <= NATIVE_GLIDE_STICK_DEAD_HIGH)
	{
		return 0;
	}
	if (stickLY > 0xff)
	{
		stickLY = 0xff;
	}
	return (stickLY - NATIVE_GLIDE_STICK_DEAD_HIGH) * 1000 / (0xff - NATIVE_GLIDE_STICK_DEAD_HIGH);
}

int NativeGlide_Rate(int frames)
{
	frames = NativeGlide_Clamp(frames, NATIVE_GLIDE_FRAMES_MIN, NATIVE_GLIDE_FRAMES_MAX);
	return (1000 + frames - 1) / frames;
}

int NativeGlide_ScaleGravity(int gravityY, int factor)
{
	return (int)(((long long)gravityY * (long long)factor) / 1000);
}

int NativeGlide_FlapWeight(int flapStep)
{
	const int half = NATIVE_GLIDE_FLAP_TICKS / 2;

	if ((flapStep <= 0) || (flapStep >= NATIVE_GLIDE_FLAP_TICKS))
	{
		return 0;
	}
	return (flapStep <= half) ? (flapStep * 1000 / half) : ((NATIVE_GLIDE_FLAP_TICKS - flapStep) * 1000 / half);
}

// A share moves towards its target by at most NATIVE_GLIDE_STICK_STEP.
internal int NativeGlide_Follow(int now, int target)
{
	if (now < target)
	{
		now += NATIVE_GLIDE_STICK_STEP;
		return (now > target) ? target : now;
	}
	if (now > target)
	{
		now -= NATIVE_GLIDE_STICK_STEP;
		return (now < target) ? target : now;
	}
	return now;
}

internal void NativeGlide_StartClose(NativeGlideState *s, int frames)
{
	s->latchDive = (int)((long long)s->fwd * s->prog / 1000);
	s->latchPull = (int)((long long)s->back * s->prog / 1000);
	s->latchFlap = NativeGlide_FlapWeight(s->flapStep);
	s->closeFrom = s->prog;
	s->closeRate = NativeGlide_Rate(frames);
	s->state = NATIVE_GLIDE_CLOSING;
	s->fwd = 0;
	s->back = 0;
	s->flapStep = -1;
}

void NativeGlide_Step(NativeGlideState *s, const NativeGlideTune *tune, const NativeGlideIn *in, NativeGlideOut *out)
{
	NativeGlideTune t = *tune;
	const int pressed = in->r3Held && !s->heldPrev;

	NativeGlide_TuneClamp(&t);
	memset(out, 0, sizeof(*out));
	out->factor = 1000;
	out->pressed = pressed;
	s->heldPrev = in->r3Held ? 1 : 0;

	// Open: the edge of R3 in the air, from folded or while closing.
	if (((s->state == NATIVE_GLIDE_FOLDED) || (s->state == NATIVE_GLIDE_CLOSING)) && in->eligible && in->airborne && pressed)
	{
		s->state = NATIVE_GLIDE_OPENING;
		s->fwd = 0;
		s->back = 0;
		s->flapStep = -1;
		s->latchDive = 0;
		s->latchPull = 0;
		s->latchFlap = 0;
		out->event = NATIVE_GLIDE_EV_OPEN;
	}
	else if ((s->state == NATIVE_GLIDE_OPENING) || (s->state == NATIVE_GLIDE_OPEN))
	{
		if (!in->eligible)
		{
			NativeGlide_StartClose(s, t.closeRelease);
			out->event = NATIVE_GLIDE_EV_CLOSE_END;
		}
		else if (!in->airborne)
		{
			NativeGlide_StartClose(s, t.closeLand);
			out->event = NATIVE_GLIDE_EV_CLOSE_LAND;
		}
		else if (!in->r3Held)
		{
			NativeGlide_StartClose(s, t.closeRelease);
			out->event = NATIVE_GLIDE_EV_CLOSE_RELEASE;
		}
	}

	switch (s->state)
	{
	case NATIVE_GLIDE_OPENING:
	case NATIVE_GLIDE_OPEN:
	{
		const int wantFwd = NativeGlide_StickForward(in->stickLY);
		const int wantBack = NativeGlide_StickBack(in->stickLY);

		if (s->state == NATIVE_GLIDE_OPENING)
		{
			s->prog += NativeGlide_Rate(t.openFrames);
			if (s->prog >= 1000)
			{
				s->prog = 1000;
				s->state = NATIVE_GLIDE_OPEN;
				s->flapStep = 0;
				if (out->event == NATIVE_GLIDE_EV_NONE)
				{
					out->event = NATIVE_GLIDE_EV_OPENED;
				}
			}
		}
		else if ((s->flapStep >= 0) && (s->flapStep < NATIVE_GLIDE_FLAP_TICKS))
		{
			s->flapStep++;
			if (s->flapStep >= NATIVE_GLIDE_FLAP_TICKS)
			{
				s->flapStep = -1;
			}
		}
		s->fwd = NativeGlide_Follow(s->fwd, wantFwd);
		s->back = NativeGlide_Follow(s->back, wantBack);
		out->gliding = 1;
		out->factor = t.glide + (int)((long long)(t.dive - t.glide) * s->fwd / 1000) + (int)((long long)(t.pullUp - t.glide) * s->back / 1000);
		out->factor = NativeGlide_Clamp(out->factor, NATIVE_GLIDE_FACTOR_MIN, NATIVE_GLIDE_FACTOR_MAX);
		out->chain = s->prog;
		out->flap = NativeGlide_FlapWeight(s->flapStep);
		out->dive = (int)((long long)s->fwd * s->prog / 1000);
		out->pullUp = (int)((long long)s->back * s->prog / 1000);
		break;
	}
	case NATIVE_GLIDE_CLOSING:
		s->prog -= s->closeRate;
		if (s->prog <= 0)
		{
			s->prog = 0;
			s->state = NATIVE_GLIDE_FOLDED;
			s->latchDive = 0;
			s->latchPull = 0;
			s->latchFlap = 0;
			s->closeFrom = 0;
			if (out->event == NATIVE_GLIDE_EV_NONE)
			{
				out->event = NATIVE_GLIDE_EV_FOLDED;
			}
			break;
		}
		out->chain = s->prog;
		if (s->closeFrom > 0)
		{
			out->dive = (int)((long long)s->latchDive * s->prog / s->closeFrom);
			out->pullUp = (int)((long long)s->latchPull * s->prog / s->closeFrom);
			out->flap = (int)((long long)s->latchFlap * s->prog / s->closeFrom);
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// THE SCRIPT (--dev-glide-script; pure)
// ---------------------------------------------------------------------------

global_variable const char *const s_glideManNames[NATIVE_GLIDE_MAN_COUNT] = {"h", "dl", "dr", "DL", "DR", "sl", "sr", "pu", "ol", "or"};

const char *NativeGlide_ManName(int man)
{
	return ((man >= 0) && (man < NATIVE_GLIDE_MAN_COUNT)) ? s_glideManNames[man] : "-";
}

int NativeGlide_ScriptParse(const char *text, int *codes, int *count, char *why, int whySize)
{
	int n = 0;
	const char *p = text;

	if (count != NULL)
	{
		*count = 0;
	}
	if ((text == NULL) || (text[0] == '\0'))
	{
		snprintf(why, (size_t)whySize, "switch --dev-glide-script expects a comma list of h dl dr DL DR sl sr pu ol or");
		return 0;
	}
	while (1)
	{
		char word[8];
		int len = 0;
		int man;

		while ((*p != '\0') && (*p != ','))
		{
			if (len >= (int)sizeof(word) - 1)
			{
				snprintf(why, (size_t)whySize, "switch --dev-glide-script: '%s' has a word that is no manoeuvre (h dl dr DL DR sl sr pu ol or)", text);
				return 0;
			}
			word[len++] = *p++;
		}
		word[len] = '\0';
		for (man = 0; man < NATIVE_GLIDE_MAN_COUNT; man++)
		{
			if (strcmp(word, s_glideManNames[man]) == 0)
			{
				break;
			}
		}
		if (man >= NATIVE_GLIDE_MAN_COUNT)
		{
			snprintf(why, (size_t)whySize, "switch --dev-glide-script: '%s' is no manoeuvre (h dl dr DL DR sl sr pu ol or)", word);
			return 0;
		}
		if (n >= NATIVE_GLIDE_SCRIPT_MAX)
		{
			snprintf(why, (size_t)whySize, "switch --dev-glide-script: more than %d manoeuvres", NATIVE_GLIDE_SCRIPT_MAX);
			return 0;
		}
		if (codes != NULL)
		{
			codes[n] = man;
		}
		n++;
		if (*p == '\0')
		{
			break;
		}
		p++; // the comma
	}
	if (count != NULL)
	{
		*count = n;
	}
	return 1;
}

void NativeGlide_ScriptReset(NativeGlideScript *p)
{
	p->next = 0;
	p->current = -1;
	p->phase = NATIVE_GLIDE_PH_WAIT;
	p->ticks = 0;
	p->air = 0;
	p->started = 0;
}

internal int NativeGlide_ManDrift(int man)
{
	return (man == NATIVE_GLIDE_MAN_DL) || (man == NATIVE_GLIDE_MAN_DR) || (man == NATIVE_GLIDE_MAN_DIVE_L) || (man == NATIVE_GLIDE_MAN_DIVE_R);
}

// -1 LEFT, +1 RIGHT, 0 the steerer's own.
internal int NativeGlide_ManSide(int man)
{
	switch (man)
	{
	case NATIVE_GLIDE_MAN_DL:
	case NATIVE_GLIDE_MAN_DIVE_L:
	case NATIVE_GLIDE_MAN_SL:
	case NATIVE_GLIDE_MAN_OL:
		return -1;
	case NATIVE_GLIDE_MAN_DR:
	case NATIVE_GLIDE_MAN_DIVE_R:
	case NATIVE_GLIDE_MAN_SR:
	case NATIVE_GLIDE_MAN_OR:
		return 1;
	default:
		return 0;
	}
}

internal void NativeGlide_ScriptSide(int side, NativeGlideScriptOut *out)
{
	if (side < 0)
	{
		out->press |= RAW_BTN_LEFT;
		out->release |= RAW_BTN_RIGHT;
	}
	else if (side > 0)
	{
		out->press |= RAW_BTN_RIGHT;
		out->release |= RAW_BTN_LEFT;
	}
}

// The buttons of a flight tick (air = 1 at the first air tick).
internal void NativeGlide_ScriptFlight(const NativeGlideScript *p, NativeGlideScriptOut *out)
{
	const int man = p->current;
	const int glide = (p->air >= NATIVE_GLIDE_SCRIPT_R3_TICK);
	const int off = (man == NATIVE_GLIDE_MAN_OL) || (man == NATIVE_GLIDE_MAN_OR);

	if (glide)
	{
		out->press |= RAW_BTN_R3;
	}
	if (!off && (NativeGlide_ManDrift(man) || !glide))
	{
		out->press |= RAW_BTN_R1;
	}
	if (off || glide)
	{
		NativeGlide_ScriptSide(NativeGlide_ManSide(man), out);
	}
	if (glide && ((man == NATIVE_GLIDE_MAN_DIVE_L) || (man == NATIVE_GLIDE_MAN_DIVE_R)))
	{
		out->press |= RAW_BTN_UP;
		out->release |= RAW_BTN_DOWN;
	}
	if (glide && (man == NATIVE_GLIDE_MAN_PU))
	{
		out->press |= RAW_BTN_DOWN;
		out->release |= RAW_BTN_UP;
	}
}

void NativeGlide_ScriptStep(NativeGlideScript *p, const NativeGlideScriptIn *in, NativeGlideScriptOut *out)
{
	memset(out, 0, sizeof(*out));

	if (!in->racing || in->grabbed)
	{
		if (p->current >= 0)
		{
			out->event = 4;
		}
		p->phase = NATIVE_GLIDE_PH_WAIT;
		p->ticks = 0;
		p->air = 0;
		p->current = -1;
		return;
	}

	switch (p->phase)
	{
	case NATIVE_GLIDE_PH_WAIT:
		if (in->airborne)
		{
			p->phase = NATIVE_GLIDE_PH_NATURAL;
			p->air = 1;
			break;
		}
		p->ticks++;
		if ((p->count > 0) && (p->ticks >= NATIVE_GLIDE_SCRIPT_GROUND_TICKS))
		{
			p->current = p->codes[p->next];
			p->next = (p->next + 1) % p->count;
			p->started++;
			p->ticks = 0;
			out->event = 1;
			if ((p->current == NATIVE_GLIDE_MAN_OL) || (p->current == NATIVE_GLIDE_MAN_OR))
			{
				p->phase = NATIVE_GLIDE_PH_OFF;
				NativeGlide_ScriptSide(NativeGlide_ManSide(p->current), out);
			}
			else
			{
				p->phase = NATIVE_GLIDE_PH_HOP;
				out->press |= RAW_BTN_R1;
			}
		}
		break;

	case NATIVE_GLIDE_PH_HOP:
	case NATIVE_GLIDE_PH_OFF:
		if (in->airborne)
		{
			p->phase = NATIVE_GLIDE_PH_FLIGHT;
			p->air = 1;
			p->ticks = 0;
			out->event = 2;
			NativeGlide_ScriptFlight(p, out);
			break;
		}
		p->ticks++;
		if (p->ticks > ((p->phase == NATIVE_GLIDE_PH_HOP) ? NATIVE_GLIDE_SCRIPT_HOP_WAIT : NATIVE_GLIDE_SCRIPT_OFF_TICKS))
		{
			p->phase = NATIVE_GLIDE_PH_WAIT;
			p->ticks = 0;
			p->current = -1;
			out->event = 5;
			break;
		}
		if (p->phase == NATIVE_GLIDE_PH_HOP)
		{
			out->press |= RAW_BTN_R1;
		}
		else
		{
			NativeGlide_ScriptSide(NativeGlide_ManSide(p->current), out);
		}
		break;

	case NATIVE_GLIDE_PH_FLIGHT:
		if (!in->airborne)
		{
			p->phase = NATIVE_GLIDE_PH_AFTER;
			p->ticks = 0;
			out->event = 3;
		}
		else
		{
			p->air++;
			NativeGlide_ScriptFlight(p, out);
			break;
		}
		// fall through: the landing tick is the first AFTER tick
	case NATIVE_GLIDE_PH_AFTER:
		p->ticks++;
		if (p->ticks > NATIVE_GLIDE_SCRIPT_AFTER_TICKS)
		{
			p->phase = NATIVE_GLIDE_PH_WAIT;
			p->ticks = 0;
			p->air = 0;
			p->current = -1;
			if (out->event == 0)
			{
				out->event = 4;
			}
			break;
		}
		if (NativeGlide_ManDrift(p->current))
		{
			out->press |= RAW_BTN_R1;
			NativeGlide_ScriptSide(NativeGlide_ManSide(p->current), out);
		}
		else if ((p->current == NATIVE_GLIDE_MAN_SL) || (p->current == NATIVE_GLIDE_MAN_SR))
		{
			NativeGlide_ScriptSide(NativeGlide_ManSide(p->current), out);
		}
		break;

	case NATIVE_GLIDE_PH_NATURAL:
		if (!in->airborne)
		{
			p->phase = NATIVE_GLIDE_PH_WAIT;
			p->ticks = 0;
			p->air = 0;
			break;
		}
		p->air++;
		if (p->air >= NATIVE_GLIDE_SCRIPT_R3_TICK)
		{
			out->press |= RAW_BTN_R3;
		}
		break;

	default:
		NativeGlide_ScriptReset(p);
		break;
	}
}

// ---------------------------------------------------------------------------
// THE MODULE (the game's side)
// ---------------------------------------------------------------------------

global_variable struct
{
	int inited;
	int ability;
	NativeGlideTune tune;
	NativeGlideState st;
	NativeGlideOut out; // of the last step
	// What a reset is keyed on: seat 0's driver, the level, the level's frames.
	const struct Driver *driver;
	const struct Level *level;
	int frames;
	u32 steps; // steps made (the wings on screen ask whether the machine still steps)
	// The tick at which the glide gravity applied (the air-time hook asks).
	const struct Driver *glidedDriver;
	int glidedTimer;
	int glided;
	// The wings on screen.
	u32 seenSteps;
	int bound;
	const struct Driver *visDriver;
	const struct Level *visLevel;
	int visChain, visFlap, visDive, visPull;
	int decaying, decFrom, decFlap, decDive, decPull;
	int chainLogged;
	// The dump window (--dev-glide-dump).
	int dumpWin, dumpAfter, dumpTick, landing;
	int dumpTimer;
	int dumpDone;
	// The script (--dev-glide-script).
	NativeGlideScript script;
	int scriptReady;
	const struct Driver *scriptDriver;
	const struct Level *scriptLevel;
	int scriptFrames;
	// Counters for the line at exit.
	long long calls, airTicks, presses, airPresses, glideTicks, frozen, opened, closeLand, closeRelease, closeEnd, visualCloses, resets, dumps;
	int reportArmed;
} s_glide;

internal void NativeGlide_Ensure(void)
{
	if (!s_glide.inited)
	{
		s_glide.inited = 1;
		NativeGlide_TuneDefaults(&s_glide.tune);
		NativeGlide_StateReset(&s_glide.st);
		s_glide.script.current = -1;
	}
}

internal void NativeGlide_Report(void)
{
	Platform_Log("[CTR Glide] at exit: ability %s; seat 0 steps %lld (in the air %lld), R3 presses %lld (in the air %lld), glide ticks %lld, "
	             "air time held %lld tick(s), opened %lld, closed on landing %lld, on release %lld, by an end %lld, closed on screen alone %lld, "
	             "resets %lld, dumps %lld\n",
	             s_glide.ability ? "on" : "off", s_glide.calls, s_glide.airTicks, s_glide.presses, s_glide.airPresses, s_glide.glideTicks, s_glide.frozen,
	             s_glide.opened, s_glide.closeLand, s_glide.closeRelease, s_glide.closeEnd, s_glide.visualCloses, s_glide.resets, s_glide.dumps);
}

int NativeGlide_Ability(void)
{
	return s_glide.ability;
}

void NativeGlide_SetAbility(int on)
{
	NativeGlide_Ensure();
	s_glide.ability = on ? 1 : 0;
	NativeGlide_StateReset(&s_glide.st);
	memset(&s_glide.out, 0, sizeof(s_glide.out));
	s_glide.glided = 0;
	s_glide.glidedDriver = NULL;
	if (on && !s_glide.reportArmed)
	{
		s_glide.reportArmed = 1;
		Platform_AtExitReport(NativeGlide_Report);
	}
	Platform_Log("[CTR Glide] glide ability (test) %s\n", on ? "on" : "off");
}

NativeGlideTune *NativeGlide_Tunables(void)
{
	NativeGlide_Ensure();
	return &s_glide.tune;
}

internal const char *NativeGlide_StateName(int state)
{
	static const char *const names[4] = {"folded", "opening", "open", "closing"};

	return ((state >= 0) && (state < 4)) ? names[state] : "?";
}

internal const char *NativeGlide_EventName(int ev)
{
	static const char *const names[7] = {"-", "open", "opened", "close-release", "close-land", "close-end", "folded"};

	return ((ev >= 0) && (ev < 7)) ? names[ev] : "?";
}

int NativeGlide_Gravity(const struct Driver *d, int gravityY)
{
	struct GameTracker *gGT;
	const struct GamepadBuffer *pad;
	NativeGlideIn in;
	NativeGlideOut out;
	int result = gravityY;

	if (!s_glide.ability)
	{
		return gravityY;
	}
	if ((d == NULL) || (d->driverID != 0) || ((d->actionsFlagSet & ACTION_BOT) != 0) || (sdata == NULL) || (sdata->gGT == NULL) ||
	    (sdata->gGamepads == NULL))
	{
		return gravityY;
	}
	gGT = sdata->gGT;

	// No state survives a load or another driver on seat 0.
	if ((d != s_glide.driver) || (gGT->level1 != s_glide.level) || (gGT->framesInThisLEV < s_glide.frames))
	{
		if (s_glide.driver != NULL)
		{
			s_glide.resets++;
		}
		NativeGlide_StateReset(&s_glide.st);
		s_glide.driver = d;
		s_glide.level = gGT->level1;
	}
	s_glide.frames = gGT->framesInThisLEV;

	pad = &sdata->gGamepads->gamepad[0];
	memset(&in, 0, sizeof(in));
	in.eligible = ((d->kartState == KS_NORMAL) || (d->kartState == KS_DRIFTING)) && ((gGT->gameMode1 & END_OF_RACE) == 0);
	in.airborne = (d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0;
	// As the kart's own input reads its pad (VehPhysProc.c PhysLinear): nothing in the end-of-race menu.
	in.r3Held = ((gGT->gameMode1 & END_OF_RACE) == 0) && ((pad->buttonsHeldCurrFrame & BTN_R3) != 0);
	in.stickLY = ((gGT->gameMode1 & END_OF_RACE) == 0) ? (int)pad->stickLY : 0x80;

	NativeGlide_Step(&s_glide.st, &s_glide.tune, &in, &out);
	s_glide.out = out;
	s_glide.steps++;
	s_glide.calls++;
	s_glide.airTicks += in.airborne ? 1 : 0;
	s_glide.presses += out.pressed ? 1 : 0;
	s_glide.airPresses += (out.pressed && in.airborne) ? 1 : 0;
	s_glide.glideTicks += out.gliding ? 1 : 0;
	s_glide.opened += (out.event == NATIVE_GLIDE_EV_OPEN) ? 1 : 0;
	s_glide.closeLand += (out.event == NATIVE_GLIDE_EV_CLOSE_LAND) ? 1 : 0;
	s_glide.closeRelease += (out.event == NATIVE_GLIDE_EV_CLOSE_RELEASE) ? 1 : 0;
	s_glide.closeEnd += (out.event == NATIVE_GLIDE_EV_CLOSE_END) ? 1 : 0;

	s_glide.glided = out.gliding;
	s_glide.glidedDriver = d;
	s_glide.glidedTimer = gGT->timer;

	if (out.gliding)
	{
		result = NativeGlide_ScaleGravity(gravityY, out.factor);
	}

	if (g_cfg_glideLog && ((s_glide.st.state != NATIVE_GLIDE_FOLDED) || (out.event != NATIVE_GLIDE_EV_NONE) || out.pressed))
	{
		Platform_Log("[CTR Glide] tick %d state %s ev %s air %d r3 %d ly %d fwd %d back %d f %d grav %d->%d prog %d flap %d dive %d pull %d "
		             "hang %d ks %d y %d r1 %d man %s\n",
		             gGT->timer, NativeGlide_StateName(s_glide.st.state), NativeGlide_EventName(out.event), in.airborne, in.r3Held, in.stickLY,
		             s_glide.st.fwd, s_glide.st.back, out.factor, gravityY, result, out.chain, out.flap, out.dive, out.pullUp, (int)d->jump_LandingBoost,
		             (int)d->kartState, (int)(d->posCurr.y >> 8), ((pad->buttonsHeldCurrFrame & (BTN_R1 | BTN_L1)) != 0) ? 1 : 0,
		             NativeGlide_ManName(s_glide.script.current));
	}
	return result;
}

int NativeGlide_LandingBoost(const struct Driver *d, int before, int added)
{
	if (!s_glide.ability || !s_glide.glided || (d == NULL) || (d != s_glide.glidedDriver) || (sdata == NULL) || (sdata->gGT == NULL) ||
	    (sdata->gGT->timer != s_glide.glidedTimer))
	{
		return added;
	}
	s_glide.frozen++;
	return before;
}

// ---------------------------------------------------------------------------
// THE WINGS ON SCREEN
// ---------------------------------------------------------------------------

global_variable const char *const s_glideChain[3] = {"wings_fold_33", "wings_fold_66", "wings_open"};
#define NATIVE_GLIDE_CHAIN_FRAMES 3000 // a thousandth of a key per frame: the chain's way of the state exactly
#define NATIVE_GLIDE_DUMP_AFTER 6      // ticks dumped after the wings are folded again

internal void NativeGlide_WriteKeys(int chain, int flap, int dive, int pull)
{
	(void)Engine_HiresShapeChainSetPos(0, 3.0f * (float)chain / 1000.0f);
	(void)Engine_HiresShapeKeySet(0, "flap", (float)flap / 1000.0f);
	(void)Engine_HiresShapeKeySet(0, "dive", (float)dive / 1000.0f);
	(void)Engine_HiresShapeKeySet(0, "pull_up", (float)pull / 1000.0f);
}

void NativeGlide_EngineTick(int seat, const struct Driver *d)
{
	struct GameTracker *gGT;
	int touch;
	int any;

	if ((seat != 0) || (d == NULL) || (sdata == NULL) || (sdata->gGT == NULL))
	{
		return;
	}
	gGT = sdata->gGT;
	if (!s_glide.ability)
	{
		if (s_glide.bound)
		{
			// The switch went off: put the keys back once, then never again.
			s_glide.bound = 0;
			if (Engine_HiresShapeChainPos(0) >= 0.0f)
			{
				NativeGlide_WriteKeys(0, 0, 0, 0);
			}
		}
		s_glide.dumpWin = 0;
		return;
	}

	// A new level or another driver: nothing of before is drawn.
	if ((d != s_glide.visDriver) || (gGT->level1 != s_glide.visLevel))
	{
		s_glide.visDriver = d;
		s_glide.visLevel = gGT->level1;
		s_glide.visChain = s_glide.visFlap = s_glide.visDive = s_glide.visPull = 0;
		s_glide.decaying = 0;
		s_glide.seenSteps = s_glide.steps;
		s_glide.chainLogged = 0;
		s_glide.dumpWin = 0;
		s_glide.dumpDone = 0;
	}

	if ((s_glide.steps != s_glide.seenSteps) && (s_glide.driver == d))
	{
		// The machine stepped since the last tick: draw what it says.
		s_glide.visChain = s_glide.out.chain;
		s_glide.visFlap = s_glide.out.flap;
		s_glide.visDive = s_glide.out.dive;
		s_glide.visPull = s_glide.out.pullUp;
		s_glide.decaying = 0;
	}
	else if ((s_glide.visChain > 0) || (s_glide.visFlap > 0) || (s_glide.visDive > 0) || (s_glide.visPull > 0))
	{
		// It did not (seat 0 drives as a bot since the finish): the wings
		// close on screen alone, over "close on release".
		NativeGlideTune t = s_glide.tune;

		NativeGlide_TuneClamp(&t);
		if (!s_glide.decaying)
		{
			s_glide.decaying = 1;
			s_glide.decFrom = (s_glide.visChain > 0) ? s_glide.visChain : 1;
			s_glide.decFlap = s_glide.visFlap;
			s_glide.decDive = s_glide.visDive;
			s_glide.decPull = s_glide.visPull;
			s_glide.visualCloses++;
			if (g_cfg_glideLog)
			{
				Platform_Log("[CTR Glide] tick %d: seat 0 no longer steps (a bot?) - the wings close on screen\n", gGT->timer);
			}
		}
		s_glide.visChain -= NativeGlide_Rate(t.closeRelease);
		if (s_glide.visChain < 0)
		{
			s_glide.visChain = 0;
		}
		s_glide.visFlap = (int)((long long)s_glide.decFlap * s_glide.visChain / s_glide.decFrom);
		s_glide.visDive = (int)((long long)s_glide.decDive * s_glide.visChain / s_glide.decFrom);
		s_glide.visPull = (int)((long long)s_glide.decPull * s_glide.visChain / s_glide.decFrom);
	}
	s_glide.seenSteps = s_glide.steps;

	if (Engine_HiresShapeChainPos(0) < 0.0f)
	{
		if (!Engine_HiresShapeChainSet(0, s_glideChain, 3, NATIVE_GLIDE_CHAIN_FRAMES))
		{
			if (!s_glide.chainLogged)
			{
				s_glide.chainLogged = 1;
				Platform_Log("[CTR Glide] seat 0's hi-res driver has no keys wings_fold_33, wings_fold_66, wings_open - no wings to move\n");
			}
			return;
		}
	}
	NativeGlide_WriteKeys(s_glide.visChain, s_glide.visFlap, s_glide.visDive, s_glide.visPull);
	s_glide.bound = 1;

	// The dump window (C-N1): from the first tick on the ground with the
	// wings not folded until NATIVE_GLIDE_DUMP_AFTER ticks after they are.
	if (g_cfg_glideDump[0] != '\0')
	{
		touch = (d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0;
		any = (s_glide.visChain > 0) || (s_glide.visFlap > 0) || (s_glide.visDive > 0) || (s_glide.visPull > 0);
		if (!s_glide.dumpWin)
		{
			if (any && touch)
			{
				s_glide.dumpWin = 1;
				s_glide.dumpAfter = 0;
				s_glide.dumpTick = 0;
				s_glide.landing++;
			}
		}
		else
		{
			s_glide.dumpTick++;
			if (any)
			{
				s_glide.dumpAfter = 0;
			}
			else if (++s_glide.dumpAfter > NATIVE_GLIDE_DUMP_AFTER)
			{
				s_glide.dumpWin = 0;
			}
		}
		s_glide.dumpTimer = gGT->timer;
		s_glide.dumpDone = 0;
	}
}

int NativeGlide_DumpWanted(const struct Driver *d, int info[NATIVE_GLIDE_DUMP_WORDS])
{
	const struct GamepadBuffer *pad;

	if ((g_cfg_glideDump[0] == '\0') || !s_glide.ability || !s_glide.dumpWin || s_glide.dumpDone || (d == NULL) || (sdata == NULL) ||
	    (sdata->gGT == NULL) || (sdata->gGT->timer != s_glide.dumpTimer) || (sdata->gGamepads == NULL))
	{
		return 0;
	}
	s_glide.dumpDone = 1;
	s_glide.dumps++;
	pad = &sdata->gGamepads->gamepad[0];
	info[0] = s_glide.landing;
	info[1] = s_glide.dumpTick;
	info[2] = s_glide.st.state;
	info[3] = s_glide.visChain;
	info[4] = s_glide.visFlap;
	info[5] = s_glide.visDive;
	info[6] = s_glide.visPull;
	info[7] = s_glide.script.current;
	info[8] = ((pad->buttonsHeldCurrFrame & (BTN_R1 | BTN_L1)) != 0) ? 1 : 0;
	info[9] = ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0) ? 1 : 0;
	info[10] = (int)d->kartState;
	info[11] = (int)d->wheelRotation;
	return 1;
}

// ---------------------------------------------------------------------------
// THE SCRIPT ON THE PAD WORD
// ---------------------------------------------------------------------------

unsigned short NativeGlide_DevPadWord(const struct GameTracker *gGT, const struct Driver *d, unsigned short word)
{
	NativeGlideScriptIn in;
	NativeGlideScriptOut out;

	if ((g_cfg_glideScript[0] == '\0') || (gGT == NULL) || (d == NULL))
	{
		return word;
	}
	NativeGlide_Ensure();
	if (!s_glide.scriptReady)
	{
		char why[200];

		s_glide.scriptReady = 1;
		if (!NativeGlide_ScriptParse(g_cfg_glideScript, s_glide.script.codes, &s_glide.script.count, why, (int)sizeof(why)))
		{
			s_glide.script.count = 0; // checked before any window; never here
		}
		NativeGlide_ScriptReset(&s_glide.script);
	}
	if ((d != s_glide.scriptDriver) || (gGT->level1 != s_glide.scriptLevel) || (gGT->framesInThisLEV < s_glide.scriptFrames))
	{
		s_glide.scriptDriver = d;
		s_glide.scriptLevel = gGT->level1;
		NativeGlide_ScriptReset(&s_glide.script);
	}
	s_glide.scriptFrames = gGT->framesInThisLEV;

	memset(&in, 0, sizeof(in));
	in.racing = ((d->actionsFlagSet & ACTION_BOT) == 0) && ((gGT->gameMode1 & END_OF_RACE) == 0);
	in.airborne = (d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0;
	in.grabbed = d->kartState == KS_MASK_GRABBED;
	{
		const int was = s_glide.script.current;

		NativeGlide_ScriptStep(&s_glide.script, &in, &out);
		if (out.event != 0)
		{
			static const char *const what[6] = {"", "start", "flight", "landed", "end", "no flight"};
			const int man = (s_glide.script.current >= 0) ? s_glide.script.current : was;

			Platform_Log("[CTR Glide] script tick %d: manoeuvre %d '%s' %s, ks %d, drift %d, steer %d, y %d\n", gGT->timer, s_glide.script.started,
			             NativeGlide_ManName(man), what[(out.event >= 0) && (out.event < 6) ? out.event : 0], (int)d->kartState, (int)d->multDrift,
			             (int)d->wheelRotation, (int)(d->posCurr.y >> 8));
		}
	}
	word = (unsigned short)((word & (unsigned short)~out.press) | (unsigned short)out.release);
	return word;
}
