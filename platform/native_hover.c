// ===========================================================================
// THE HOVER ON SCREEN (include/platform/native_hover.h; round H1 step 4).
//
// THE PARTS: the pure maths (lift and bob, the key targets and ramps, the
// class gate); the module's eight seats, filled at the engine's level load
// (NativeHover_LevelSeat, platform/engine_hires_char.c) and stepped by the
// engine's seat tick (NativeHover_EngineTick); the lift the engine's race route
// adds to its world matrix (NativeHover_SeatLift). Draw side only: nothing here
// is read by the game, and the only things written are this file's state and
// the seats' shape key layer (which the game never reads, engine.h THE SHAPE
// KEYS).
//
// THE GATE: every seat starts as a kart at every level load; only a seat the
// level load names hover (its file's class, its hi-res character drawn by the
// hi-res route) ever gets a lift other than 0.0 or a key write.
// ===========================================================================

#include <platform/native_hover.h>
#include <platform/engine.h>

#include <math.h>
#include <string.h>

#define NATIVE_HOVER_TWO_PI 6.28318530717958647692

typedef struct
{
	NativeHoverTune tune;
	NativeHoverSeat seat[NATIVE_HOVER_SEATS];
	int firstDrawLogged[NATIVE_HOVER_SEATS]; // the first lifted draw of a seat in this level
	int tuneReady;
	// The counters of the line at exit.
	u32 seen;        // hover seats seen at level loads (hi-res route)
	u32 classic;     // hover seats seen at level loads on the classic model
	u32 liftedDraws; // route draws with a lift
	u32 ticks;       // seat ticks of hover seats
	u32 keyTicks;    // ticks the coupling wrote
	u32 keyWrites;   // keys written (a key the file has)
	u32 held;        // ticks seat 0 held for the SHAPE KEYS page / --dev-shape-keys / --dev-shape-chain
	double liftMin;
	double liftMax;
} NativeHoverState;

global_variable NativeHoverState s_hover;

// ---------------------------------------------------------------------------
// THE PURE PARTS
// ---------------------------------------------------------------------------

void NativeHover_TuneDefaults(NativeHoverTune *t)
{
	if (t == NULL)
	{
		return;
	}
	t->on = 1;
	t->heightMm = NATIVE_HOVER_HEIGHT_MM;
	t->bobPct = NATIVE_HOVER_BOB_PCT;
	t->bobFrames = NATIVE_HOVER_BOB_FRAMES;
	t->lean = NATIVE_HOVER_STRENGTH;
	t->nose = NATIVE_HOVER_STRENGTH;
}

internal int NativeHover_ClampInt(int v, int lo, int hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void NativeHover_TuneClamp(NativeHoverTune *t)
{
	if (t == NULL)
	{
		return;
	}
	t->on = (t->on != 0) ? 1 : 0;
	t->heightMm = NativeHover_ClampInt(t->heightMm, 0, NATIVE_HOVER_HEIGHT_MM_MAX);
	t->bobPct = NativeHover_ClampInt(t->bobPct, 0, NATIVE_HOVER_BOB_PCT_MAX);
	t->bobFrames = NativeHover_ClampInt(t->bobFrames, NATIVE_HOVER_BOB_FRAMES_MIN, NATIVE_HOVER_BOB_FRAMES_MAX);
	t->lean = NativeHover_ClampInt(t->lean, 0, NATIVE_HOVER_STRENGTH_MAX);
	t->nose = NativeHover_ClampInt(t->nose, 0, NATIVE_HOVER_STRENGTH_MAX);
}

const char *NativeHover_KeyName(int key)
{
	static const char *const names[NATIVE_HOVER_KEYS] = {"lean_left", "lean_right", "nose_up", "nose_down"};

	return ((key >= 0) && (key < NATIVE_HOVER_KEYS)) ? names[key] : "";
}

double NativeHover_HeightWE(const NativeHoverTune *t, int scaleY)
{
	NativeHoverTune c;

	if (t == NULL)
	{
		return 0.0;
	}
	c = *t;
	NativeHover_TuneClamp(&c);
	// kit metres x model units per metre x the driver's scale (as its body: engine_instance.c, R/4096 x diag(scale/4096)).
	return (double)c.heightMm / 1000.0 * (double)RLDHIRES_UNITS_PER_METRE * (double)scaleY / 4096.0;
}

double NativeHover_Lift(int vehicleClass, const NativeHoverTune *t, int scaleY, u32 phase)
{
	NativeHoverTune c;
	double h;

	if ((vehicleClass != (int)RLDCHAR_VEHICLE_HOVER) || (t == NULL) || !t->on)
	{
		return 0.0;
	}
	c = *t;
	NativeHover_TuneClamp(&c);
	h = NativeHover_HeightWE(&c, scaleY);
	return h * (1.0 + (double)c.bobPct / 100.0 * sin(NATIVE_HOVER_TWO_PI * (double)(phase % (u32)c.bobFrames) / (double)c.bobFrames));
}

float NativeHover_Ramp(float cur, float target, float step)
{
	target = (target > 0.0f) ? ((target < 1.0f) ? target : 1.0f) : 0.0f; // NaN -> 0
	step = (step > 0.0f) ? step : 0.0f;
	if (cur < target)
	{
		cur += step;
		return (cur > target) ? target : cur;
	}
	if (cur > target)
	{
		cur -= step;
		return (cur < target) ? target : cur;
	}
	return target;
}

internal float NativeHover_Clamp01(float v)
{
	return (v > 0.0f) ? ((v < 1.0f) ? v : 1.0f) : 0.0f;
}

void NativeHover_Targets(const NativeHoverTune *t, const NativeHoverIn *in, float target[NATIVE_HOVER_KEYS])
{
	NativeHoverTune c;
	float steer;
	int k;

	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		target[k] = 0.0f;
	}
	if ((t == NULL) || (in == NULL) || !t->on)
	{
		return;
	}
	c = *t;
	NativeHover_TuneClamp(&c);
	// wheelRotation: + left, - right, 64 = full (include/namespace_Vehicle.h).
	steer = (float)in->wheelRotation / 64.0f;
	steer = (steer > 1.0f) ? 1.0f : ((steer < -1.0f) ? -1.0f : steer);
	target[NATIVE_HOVER_KEY_LEAN_LEFT] = NativeHover_Clamp01(((steer > 0.0f) ? steer : 0.0f) * (float)c.lean / 1000.0f);
	target[NATIVE_HOVER_KEY_LEAN_RIGHT] = NativeHover_Clamp01(((steer < 0.0f) ? -steer : 0.0f) * (float)c.lean / 1000.0f);
	target[NATIVE_HOVER_KEY_NOSE_UP] = NativeHover_Clamp01((in->gas ? 1.0f : 0.0f) * (float)c.nose / 1000.0f);
	target[NATIVE_HOVER_KEY_NOSE_DOWN] = NativeHover_Clamp01((in->brake ? 1.0f : 0.0f) * (float)c.nose / 1000.0f);
}

int NativeHover_KeyStep(NativeHoverSeat *s, const NativeHoverTune *t, const NativeHoverIn *in, float out[NATIVE_HOVER_KEYS])
{
	float target[NATIVE_HOVER_KEYS];
	int k;

	if ((s == NULL) || (s->vehicle != (int)RLDCHAR_VEHICLE_HOVER) || (t == NULL) || (in == NULL) || (out == NULL))
	{
		return 0;
	}
	NativeHover_Targets(t, in, target);
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		s->w[k] = NativeHover_Ramp(s->w[k], target[k], NATIVE_HOVER_RAMP);
		out[k] = s->w[k];
	}
	return 1;
}

// ---------------------------------------------------------------------------
// THE MODULE
// ---------------------------------------------------------------------------

NativeHoverTune *NativeHover_Tunables(void)
{
	if (!s_hover.tuneReady)
	{
		s_hover.tuneReady = 1;
		NativeHover_TuneDefaults(&s_hover.tune);
	}
	return &s_hover.tune;
}

void NativeHover_LevelReset(void)
{
	int seat;

	for (seat = 0; seat < NATIVE_HOVER_SEATS; seat++)
	{
		memset(&s_hover.seat[seat], 0, sizeof(s_hover.seat[seat]));
		s_hover.seat[seat].vehicle = (int)RLDCHAR_VEHICLE_KART;
		s_hover.firstDrawLogged[seat] = 0;
	}
}

void NativeHover_LevelSeat(int seat, int vehicleClass, const char *file, int hires)
{
	const NativeHoverTune *t = NativeHover_Tunables();
	char keys[96];
	size_t used = 0;
	int k;

	if ((seat < 0) || (seat >= NATIVE_HOVER_SEATS) || (vehicleClass != (int)RLDCHAR_VEHICLE_HOVER))
	{
		return;
	}
	if (file == NULL)
	{
		file = "?";
	}
	if (!hires)
	{
		s_hover.classic++;
		Platform_Log("[CTR Engine] hover: seat %d, %s: drawn by its classic model - on the ground, no lift (the lift is on the hi-res route only)\n", seat, file);
		return;
	}
	s_hover.seen++;
	s_hover.seat[seat].vehicle = (int)RLDCHAR_VEHICLE_HOVER;
	keys[0] = '\0';
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		if ((Engine_HiresShapeKeyFind(seat, NativeHover_KeyName(k)) >= 0) && (used < sizeof(keys)))
		{
			used += (size_t)snprintf(keys + used, sizeof(keys) - used, "%s%s", (used > 0u) ? " " : "", NativeHover_KeyName(k));
		}
	}
	Platform_Log("[CTR Engine] hover: seat %d, %s, height %.3f m = %.2f WE at the driver scale 0x%x (follows inst->scale.y), bob %d%% = %.2f WE over %d ticks, "
	             "lift %s; keys %s, lean %.2f, nose %.2f%s\n",
	             seat, file, (double)t->heightMm / 1000.0, NativeHover_HeightWE(t, 0xccc), 0xccc, t->bobPct,
	             NativeHover_HeightWE(t, 0xccc) * (double)t->bobPct / 100.0, t->bobFrames, t->on ? "on" : "off", (used > 0u) ? keys : "none",
	             (double)t->lean / 1000.0, (double)t->nose / 1000.0,
	             ((seat == 0) && ((g_cfg_engineShapeKeys[0] != '\0') || (g_cfg_engineShapeChain[0] != '\0'))) ? " (seat 0: --dev-shape-keys/--dev-shape-chain hold the keys)" : "");
}

// The pad as the kart's own input reads it (VehPhysProc.c: gamepad[driverID],
// nothing in the end-of-race menu; the gas branch is "square == 0"). A CPU
// driver has no pad.
internal void NativeHover_ReadInput(const struct Driver *d, NativeHoverIn *in)
{
	memset(in, 0, sizeof(*in));
	in->wheelRotation = (int)d->wheelRotation;
	if (((d->actionsFlagSet & ACTION_BOT) == 0) && (sdata != NULL) && (sdata->gGT != NULL) && (sdata->gGamepads != NULL) &&
	    ((sdata->gGT->gameMode1 & END_OF_RACE) == 0) && ((u32)d->driverID < (u32)NATIVE_HOVER_SEATS))
	{
		const u32 held = (u32)sdata->gGamepads->gamepad[(u32)d->driverID].buttonsHeldCurrFrame;

		in->brake = ((held & (u32)BTN_SQUARE) != 0u) ? 1 : 0;
		in->gas = (!in->brake && ((held & (u32)BTN_CROSS) != 0u)) ? 1 : 0;
	}
}

void NativeHover_EngineTick(int seat, const struct Driver *d, int hasKeys)
{
	NativeHoverSeat *s;
	NativeHoverIn in;
	float out[NATIVE_HOVER_KEYS];
	int k;

	if ((seat < 0) || (seat >= NATIVE_HOVER_SEATS) || (d == NULL))
	{
		return;
	}
	s = &s_hover.seat[seat];
	if (s->vehicle != (int)RLDCHAR_VEHICLE_HOVER)
	{
		return; // a kart, a plane, an unknown class: no phase, no key
	}
	s->phase++;
	s_hover.ticks++;
	if (!hasKeys)
	{
		return;
	}
	// PRECEDENCE: the SHAPE KEYS page (while open) and --dev-shape-keys /
	// --dev-shape-chain own seat 0's keys; the coupling takes over what is shown.
	if ((seat == 0) && (DebugMenu_ShapePreviewOpen() || (g_cfg_engineShapeKeys[0] != '\0') || (g_cfg_engineShapeChain[0] != '\0')))
	{
		for (k = 0; k < NATIVE_HOVER_KEYS; k++)
		{
			s->w[k] = Engine_HiresShapeKeyGet(seat, NativeHover_KeyName(k));
		}
		s_hover.held++;
		return;
	}
	NativeHover_ReadInput(d, &in);
	if (!NativeHover_KeyStep(s, NativeHover_Tunables(), &in, out))
	{
		return;
	}
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		s_hover.keyWrites += (u32)Engine_HiresShapeKeySet(seat, NativeHover_KeyName(k), out[k]);
	}
	s_hover.keyTicks++;
}

int NativeHover_SeatVehicle(int seat)
{
	return ((seat >= 0) && (seat < NATIVE_HOVER_SEATS)) ? s_hover.seat[seat].vehicle : (int)RLDCHAR_VEHICLE_KART;
}

double NativeHover_SeatLift(int seat, int scaleY)
{
	double lift;

	if ((seat < 0) || (seat >= NATIVE_HOVER_SEATS) || (s_hover.seat[seat].vehicle != (int)RLDCHAR_VEHICLE_HOVER))
	{
		return 0.0;
	}
	lift = NativeHover_Lift(s_hover.seat[seat].vehicle, NativeHover_Tunables(), scaleY, s_hover.seat[seat].phase);
	if (lift != 0.0)
	{
		if (s_hover.liftedDraws == 0u)
		{
			s_hover.liftMin = s_hover.liftMax = lift;
		}
		s_hover.liftMin = (lift < s_hover.liftMin) ? lift : s_hover.liftMin;
		s_hover.liftMax = (lift > s_hover.liftMax) ? lift : s_hover.liftMax;
		s_hover.liftedDraws++;
		if (!s_hover.firstDrawLogged[seat])
		{
			s_hover.firstDrawLogged[seat] = 1;
			Platform_Log("[CTR Engine] hover: seat %d first lifted draw: scale.y 0x%x, height %.2f WE, lift %.2f WE (tick %u of the bob)\n", seat, (unsigned)scaleY,
			             NativeHover_HeightWE(NativeHover_Tunables(), scaleY), lift, (unsigned)s_hover.seat[seat].phase);
		}
	}
	return lift;
}

void NativeHover_Report(void)
{
	if ((s_hover.seen == 0u) && (s_hover.classic == 0u))
	{
		return;
	}
	Platform_Log("[CTR Engine] hover at exit: seats seen %u (classic model %u); lifted draws %u, lift %.2f..%.2f WE; ticks %u, key ticks %u, key writes %u, "
	             "held for the shape key page/switches %u\n",
	             s_hover.seen, s_hover.classic, s_hover.liftedDraws, s_hover.liftMin, s_hover.liftMax, s_hover.ticks, s_hover.keyTicks, s_hover.keyWrites,
	             s_hover.held);
}
