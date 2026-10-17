#include <platform/engine.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// ENGINE HIRES POSE - the clip state machine of a hi-res driver seat (round of
// 2026-10-17). See include/platform/engine.h, HI-RES DRIVERS FROM THE .rldchar,
// and the hi-res format notes section 4 (the fixed clip list, its fallback
// chains). PURE: game values come in as numbers (EngHcInput), nothing of the
// game is read or written here; platform/engine_hires_char.c reads the driver
// (read only) and turns the state into a pose. The selftest drives it with
// made-up inputs.
//
// THE STATES (one per seat, stepped once per logic tick, gGT->timer moved):
//   FORCED  a clip given by Engine_HiresSeatForceClip (podium, results) - first
//   FINISHED ACTION_RACE_FINISHED (game/PlayLevel.c:240; the kart drives on
//           under AI): podium_<driverRank + 1> for ranks 0..2, lose from rank 3,
//           looped (the rank is frozen at the finish, PlayLevel.c:249). Not for
//           a file with the kart part (CHRG parts bit 0): its podium and lose
//           clips stand without the kart, so a finished driver of it stays in
//           the states below (seated, the kart drives on).
//   HIT     kartState KS_SPINNING, KS_BLASTED or KS_CRASHING: clip "hit", once
//           from the tick the state is entered, held while it lasts
//   JUMP    ACTION_TOUCH_GROUND clear for ENG_HC_AIR_DELAY ticks or more (a bump
//           stays on the ground, the prototype's rule, engine_hires.c Z4): clip
//           "jump", once from the tick the delay is reached, the last pose held
//   DRIFT   kartState KS_DRIFTING (players and bots): "drift_left" or
//           "drift_right", looped; the side taken at the first drift tick from
//           the first non-zero sign of multDrift, simpTurnState, wheelRotation
//           (positive = left; EngHires_DriftSide of the prototype)
//   BASE    otherwise: "idle" looped, with the steer clip of the side of
//           wheelRotation (positive = left) as a pose scrub - time = fraction x
//           length, fraction = |wheelRotation| / 64 clamped to 1 (0x40 a full
//           lock, VEH_PHYS_PROC_WHEEL_ROTATION_STRENGTH; bots reach about 0x55) -
//           blended over idle with the same fraction: steering blends without a
//           step, no cross-fade inside BASE.
// A change of state (or of the forced clip) starts a cross-fade of
// ENG_HC_FADE_SECONDS game time from a snapshot of the pose drawn at that tick
// (the caller takes it before EngHc_Enter): an interrupted fade starts from
// what was on the screen, so the weight never jumps. Times advance by the
// tick's elapsedTimeMS (game time; a pause stops them with the timer).
//
// FALLBACKS (EngHc_Resolve): a missing clip is replaced along its chain, which
// always ends at the rest pose: steer_*, jump, hit, podium_1, lose, menu ->
// idle; drift_left -> steer_left at full lock (then idle), drift_right ->
// steer_right at full lock; podium_3 -> podium_2 -> podium_1 -> idle; idle ->
// the rest pose. A drawn clip is never undefined, a driver never invisible.

#define ENG_HC_AIR_DELAY 3      // ticks off the ground before the jump clip (ENG_HIRES_AIR_DELAY of the prototype)
#define ENG_HC_STEER_FULL 64    // wheelRotation of a full lock
#define ENG_HC_TICKS_CAP 100000 // counters stop here
#define ENG_HC_FADE_SECONDS 0.15f

#define ENG_HC_STATE_BASE 0
#define ENG_HC_STATE_JUMP 1
#define ENG_HC_STATE_DRIFT_LEFT 2
#define ENG_HC_STATE_DRIFT_RIGHT 3
#define ENG_HC_STATE_HIT 4
#define ENG_HC_STATE_FORCED 5
#define ENG_HC_STATE_FINISHED 6
#define ENG_HC_STATES 7

// How a clip plays (fixed in the game, not in the file).
#define ENG_HC_PLAY_LOOP RLDHIRES_PLAY_LOOP
#define ENG_HC_PLAY_ONCE RLDHIRES_PLAY_ONCE
#define ENG_HC_PLAY_SCRUB RLDHIRES_PLAY_POSE

typedef struct
{
	int touch;         // ACTION_TOUCH_GROUND set
	int kartState;     // driver->kartState
	int wheelRotation; // driver->wheelRotation
	int multDrift;     // driver->multDrift
	int simpTurnState; // driver->simpTurnState
	int elapsedMs;     // gGT->elapsedTimeMS of the tick
	int force;         // the seat's forced clip, -1 none
	int finished;      // ACTION_RACE_FINISHED set
	int rank;          // driver->driverRank (0-based)
	int kartPart;      // the file has the kart part (CHRG parts bit 0): finished keeps the seated race clips
} EngHcInput;

typedef struct
{
	int airTicks;
	int driftTicks;
	int driftSide; // +1 left, -1 right
	int state;     // ENG_HC_STATE_*
	int forceClip; // the clip of FORCED
	float stateTime; // seconds in the state
	float baseTime;  // the idle clock, runs in every state
	int fading;      // a cross-fade from the snapshot runs
	float fadeTime;  // seconds into it
	int prevState;   // the state the fade comes from (the log)
	int prevForce;
	int steerRaw;    // wheelRotation of the last tick (the BASE pose)
	u32 ticks;
} EngHcSeatState;

static const char *const s_engHcStateNames[ENG_HC_STATES] = {"base", "jump", "drift_left", "drift_right", "hit", "forced", "finished"};

// The clip ids, names, chains and playback kinds are include/rldhires.inc's
// (the same for the game, rldpack and Reload Studio); engine.h only repeats the ids.
CTR_STATIC_ASSERT((ENGINE_HIRES_CLIPS == RLDHIRES_CLIP_COUNT) && (ENGINE_HIRES_CLIP_IDLE == RLDHIRES_CLIP_IDLE) &&
                  (ENGINE_HIRES_CLIP_STEER_LEFT == RLDHIRES_CLIP_STEER_LEFT) && (ENGINE_HIRES_CLIP_STEER_RIGHT == RLDHIRES_CLIP_STEER_RIGHT) &&
                  (ENGINE_HIRES_CLIP_JUMP == RLDHIRES_CLIP_JUMP) && (ENGINE_HIRES_CLIP_DRIFT_LEFT == RLDHIRES_CLIP_DRIFT_LEFT) &&
                  (ENGINE_HIRES_CLIP_DRIFT_RIGHT == RLDHIRES_CLIP_DRIFT_RIGHT) && (ENGINE_HIRES_CLIP_HIT == RLDHIRES_CLIP_HIT) &&
                  (ENGINE_HIRES_CLIP_PODIUM_1 == RLDHIRES_CLIP_PODIUM_1) && (ENGINE_HIRES_CLIP_PODIUM_2 == RLDHIRES_CLIP_PODIUM_2) &&
                  (ENGINE_HIRES_CLIP_PODIUM_3 == RLDHIRES_CLIP_PODIUM_3) && (ENGINE_HIRES_CLIP_LOSE == RLDHIRES_CLIP_LOSE) &&
                  (ENGINE_HIRES_CLIP_MENU == RLDHIRES_CLIP_MENU) && (ENGINE_HIRES_CLIP_REST == -1));

const char *Engine_HiresClipName(int clip)
{
	return (clip == ENGINE_HIRES_CLIP_REST) ? "rest" : RldHires_ClipName(clip);
}

internal const char *EngHc_StateName(int state)
{
	return ((state >= 0) && (state < ENG_HC_STATES)) ? s_engHcStateNames[state] : "?";
}

// The clip drawn for clip with the clips present (bit c of mask = clip c in the
// file): itself, else along its chain (RldHires_ClipFallback);
// ENGINE_HIRES_CLIP_REST at the end. *full = 1 when a drift ends on its steer
// clip (drawn at full lock). The rule of RldHires_ClipResolve on a mask, so the
// selftest can run every chain without a file.
internal int EngHc_Resolve(u32 mask, int clip, int *full)
{
	int c = clip;
	int fromDrift = 0;
	int steps;

	if (full != NULL)
	{
		*full = 0;
	}
	for (steps = 0; steps <= ENGINE_HIRES_CLIPS; steps++)
	{
		if ((c < 0) || (c >= ENGINE_HIRES_CLIPS))
		{
			return ENGINE_HIRES_CLIP_REST;
		}
		if ((mask & (1u << (u32)c)) != 0u)
		{
			if ((full != NULL) && fromDrift && (RldHires_ClipPlay(c) == RLDHIRES_PLAY_POSE))
			{
				*full = 1;
			}
			return c;
		}
		fromDrift = fromDrift || (c == ENGINE_HIRES_CLIP_DRIFT_LEFT) || (c == ENGINE_HIRES_CLIP_DRIFT_RIGHT);
		c = RldHires_ClipFallback(c);
	}
	return ENGINE_HIRES_CLIP_REST;
}

// How a (resolved) clip plays (RldHires_ClipPlay).
internal int EngHc_PlayOf(int clip)
{
	return RldHires_ClipPlay(clip);
}

// The clip a state asks for (before its fallback); BASE asks for idle (the
// steer overlay is the caller's).
internal int EngHc_StateClip(const EngHcSeatState *s)
{
	switch (s->state)
	{
	case ENG_HC_STATE_JUMP:
		return ENGINE_HIRES_CLIP_JUMP;
	case ENG_HC_STATE_DRIFT_LEFT:
		return ENGINE_HIRES_CLIP_DRIFT_LEFT;
	case ENG_HC_STATE_DRIFT_RIGHT:
		return ENGINE_HIRES_CLIP_DRIFT_RIGHT;
	case ENG_HC_STATE_HIT:
		return ENGINE_HIRES_CLIP_HIT;
	case ENG_HC_STATE_FORCED:
	case ENG_HC_STATE_FINISHED:
		return s->forceClip;
	default:
		return ENGINE_HIRES_CLIP_IDLE;
	}
}

// The side of a drift from its first tick (the prototype's rule): the first
// non-zero sign of multDrift, simpTurnState, wheelRotation; left when all are 0.
internal int EngHc_DriftSide(int multDrift, int simpTurnState, int wheelRotation)
{
	if (multDrift != 0)
	{
		return (multDrift > 0) ? 1 : -1;
	}
	if (simpTurnState != 0)
	{
		return (simpTurnState > 0) ? 1 : -1;
	}
	return (wheelRotation < 0) ? -1 : 1;
}

internal int EngHc_IsHitState(int kartState)
{
	return (kartState == KS_SPINNING) || (kartState == KS_BLASTED) || (kartState == KS_CRASHING);
}

// The steer fraction 0..1 and the side's clip of a wheelRotation.
internal float EngHc_SteerFraction(int wheelRotation, int *clip)
{
	const int a = (wheelRotation < 0) ? -wheelRotation : wheelRotation;

	*clip = (wheelRotation > 0) ? ENGINE_HIRES_CLIP_STEER_LEFT : ENGINE_HIRES_CLIP_STEER_RIGHT;
	return (a >= ENG_HC_STEER_FULL) ? 1.0f : ((float)a / (float)ENG_HC_STEER_FULL);
}

// The clip of a finished driver: podium_1..3 for ranks 0..2, lose below.
internal int EngHc_FinishedClip(int rank)
{
	return ((rank >= 0) && (rank <= 2)) ? (ENGINE_HIRES_CLIP_PODIUM_1 + rank) : ENGINE_HIRES_CLIP_LOSE;
}

// A new driver or instance starts here.
internal void EngHc_SeatReset(EngHcSeatState *s, int force)
{
	memset(s, 0, sizeof(*s));
	s->driftSide = 1;
	s->forceClip = -1;
	s->prevState = ENG_HC_STATE_BASE;
	s->prevForce = -1;
	if (force >= 0)
	{
		s->state = ENG_HC_STATE_FORCED;
		s->forceClip = force;
	}
}

// Step 1 of a tick: the times of the current state move on by the tick.
internal void EngHc_Advance(EngHcSeatState *s, int elapsedMs)
{
	const float dt = (elapsedMs > 0) ? ((float)elapsedMs / 1000.0f) : 0.0f;

	s->stateTime += dt;
	s->baseTime += dt;
	if (s->fading)
	{
		s->fadeTime += dt;
		if (s->fadeTime >= ENG_HC_FADE_SECONDS)
		{
			s->fading = 0;
		}
	}
	if (s->ticks < (u32)ENG_HC_TICKS_CAP)
	{
		s->ticks++;
	}
}

// Step 2: the counters of the tick and the state it asks for (*forceClip the
// clip of FORCED). Writes the counters only.
internal int EngHc_Decide(EngHcSeatState *s, const EngHcInput *in, int *forceClip)
{
	s->steerRaw = in->wheelRotation;
	if (in->touch)
	{
		s->airTicks = 0;
	}
	else if (s->airTicks < ENG_HC_TICKS_CAP)
	{
		s->airTicks++;
	}
	if (in->kartState == KS_DRIFTING)
	{
		if (s->driftTicks == 0)
		{
			s->driftSide = EngHc_DriftSide(in->multDrift, in->simpTurnState, in->wheelRotation);
		}
		if (s->driftTicks < ENG_HC_TICKS_CAP)
		{
			s->driftTicks++;
		}
	}
	else
	{
		s->driftTicks = 0;
	}
	*forceClip = -1;
	if ((in->force >= 0) && (in->force < ENGINE_HIRES_CLIPS))
	{
		*forceClip = in->force;
		return ENG_HC_STATE_FORCED;
	}
	// A file with the kart part stands in podium_1..3 / lose (made for the podium
	// scene without the kart): in the race it keeps its seated clips below.
	if (in->finished && !in->kartPart)
	{
		*forceClip = EngHc_FinishedClip(in->rank);
		return ENG_HC_STATE_FINISHED;
	}
	if (EngHc_IsHitState(in->kartState))
	{
		return ENG_HC_STATE_HIT;
	}
	if (s->airTicks >= ENG_HC_AIR_DELAY)
	{
		return ENG_HC_STATE_JUMP;
	}
	if (s->driftTicks > 0)
	{
		return (s->driftSide >= 0) ? ENG_HC_STATE_DRIFT_LEFT : ENG_HC_STATE_DRIFT_RIGHT;
	}
	return ENG_HC_STATE_BASE;
}

// 1 = the decided state is a change (the caller snapshots the drawn pose, then
// calls EngHc_Enter).
internal int EngHc_Changes(const EngHcSeatState *s, int state, int forceClip)
{
	return (state != s->state) || (((state == ENG_HC_STATE_FORCED) || (state == ENG_HC_STATE_FINISHED)) && (forceClip != s->forceClip));
}

// Step 3 on a change: the new state from time 0, a cross-fade from the snapshot.
internal void EngHc_Enter(EngHcSeatState *s, int state, int forceClip)
{
	s->prevState = s->state;
	s->prevForce = s->forceClip;
	s->state = state;
	s->forceClip = ((state == ENG_HC_STATE_FORCED) || (state == ENG_HC_STATE_FINISHED)) ? forceClip : -1;
	s->stateTime = 0.0f;
	s->fading = 1;
	s->fadeTime = 0.0f;
}

// The weight of the state's own pose over the snapshot: 1 when no fade runs.
internal float EngHc_FadeWeight(const EngHcSeatState *s)
{
	if (!s->fading)
	{
		return 1.0f;
	}
	return (s->fadeTime >= ENG_HC_FADE_SECONDS) ? 1.0f : (s->fadeTime / ENG_HC_FADE_SECONDS);
}

// The seconds a clip of length `length` is sampled at for a play mode (scrub:
// fraction x length; once: clamped; loop: wrapped) - relative to the clip's
// start (RldAnim_ClipTime adds it).
internal float EngHc_LocalTime(int play, float time, float fraction, float length, int full)
{
	if (play == ENG_HC_PLAY_SCRUB)
	{
		const float f = full ? 1.0f : ((fraction < 0.0f) ? 0.0f : ((fraction > 1.0f) ? 1.0f : fraction));

		return f * length;
	}
	if (play == ENG_HC_PLAY_ONCE)
	{
		return (time < 0.0f) ? 0.0f : ((time > length) ? length : time);
	}
	if (!(length > 0.0f))
	{
		return 0.0f;
	}
	{
		double local = fmod((double)time, (double)length);

		return (float)((local < 0.0) ? (local + (double)length) : local);
	}
}
