// ===========================================================================
// THE HOVER ON SCREEN: the unit test of its pure parts (ctest hover_selftest).
// Included by main.c after native_hover.c (unity build). The pure functions run
// on local structs; the module part drives the module's own state (s_hover)
// and puts it back at the end (level reset, default tune, counters at 0).
// ===========================================================================

#include <math.h>
#include <stdio.h>
#include <string.h>

global_variable int s_hoverStChecks;
global_variable int s_hoverStFails;

#define HOVER_EXPECT(cond, ...) \
	do \
	{ \
		s_hoverStChecks++; \
		if (!(cond)) \
		{ \
			s_hoverStFails++; \
			printf("hover selftest: "); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

// Integer compare: the tag names the case, the message gives actual and wanted.
#define HOVER_EQ(tag, got, want) \
	do \
	{ \
		const int g_ = (int)(got); \
		const int w_ = (int)(want); \
		HOVER_EXPECT(g_ == w_, "%s: got %d, wanted %d", (tag), g_, w_); \
	} while (0)

// Double compare with a tolerance.
#define HOVER_NEAR(tag, got, want, eps) \
	do \
	{ \
		const double g_ = (double)(got); \
		const double w_ = (double)(want); \
		HOVER_EXPECT(fabs(g_ - w_) <= (eps), "%s: got %.9f, wanted %.9f", (tag), g_, w_); \
	} while (0)

// A tag with up to two numbers in it (a small ring of buffers).
internal const char *HoverSt_Tag(const char *fmt, int a, int b)
{
	static char bufs[4][96];
	static int next = 0;
	char *buf = bufs[next];

	next = (next + 1) & 3;
	snprintf(buf, 96, fmt, a, b);
	return buf;
}

// ---------------------------------------------------------------------------
// 1 + 9: the tune and the names
// ---------------------------------------------------------------------------

internal void HoverSt_TestTune(void)
{
	NativeHoverTune t;
	int k;

	NativeHover_TuneDefaults(&t);
	HOVER_EQ("defaults on", t.on, 1);
	HOVER_EQ("defaults heightMm", t.heightMm, 250);
	HOVER_EQ("defaults bobPct", t.bobPct, 10);
	HOVER_EQ("defaults bobFrames", t.bobFrames, 30);
	HOVER_EQ("defaults lean", t.lean, 1000);
	HOVER_EQ("defaults nose", t.nose, 1000);

	NativeHover_TuneDefaults(&t);
	t.heightMm = 5000;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp heightMm 5000", t.heightMm, 1000);
	t.heightMm = -3;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp heightMm -3", t.heightMm, 0);

	NativeHover_TuneDefaults(&t);
	t.bobPct = -5;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp bobPct -5", t.bobPct, 0);
	t.bobPct = 400;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp bobPct 400", t.bobPct, 100);

	NativeHover_TuneDefaults(&t);
	t.bobFrames = 0;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp bobFrames 0", t.bobFrames, 2);
	t.bobFrames = 99999;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp bobFrames 99999", t.bobFrames, 600);

	NativeHover_TuneDefaults(&t);
	t.lean = 99999;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp lean 99999", t.lean, 2000);
	t.lean = -1;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp lean -1", t.lean, 0);

	NativeHover_TuneDefaults(&t);
	t.nose = 99999;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp nose 99999", t.nose, 2000);
	t.nose = -1;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp nose -1", t.nose, 0);

	NativeHover_TuneDefaults(&t);
	t.on = 7;
	NativeHover_TuneClamp(&t);
	HOVER_EQ("clamp on 7", t.on, 1);

	// NULL is a no-op, not a crash.
	NativeHover_TuneDefaults(NULL);
	NativeHover_TuneClamp(NULL);
	HOVER_EXPECT(1, "null tune survived");

	// The names.
	HOVER_EXPECT(strcmp(NativeHover_KeyName(0), "lean_left") == 0, "key name 0: %s", NativeHover_KeyName(0));
	HOVER_EXPECT(strcmp(NativeHover_KeyName(1), "lean_right") == 0, "key name 1: %s", NativeHover_KeyName(1));
	HOVER_EXPECT(strcmp(NativeHover_KeyName(2), "nose_up") == 0, "key name 2: %s", NativeHover_KeyName(2));
	HOVER_EXPECT(strcmp(NativeHover_KeyName(3), "nose_down") == 0, "key name 3: %s", NativeHover_KeyName(3));
	HOVER_EXPECT(strcmp(NativeHover_KeyName(4), "") == 0, "key name 4: %s", NativeHover_KeyName(4));
	HOVER_EXPECT(strcmp(NativeHover_KeyName(-1), "") == 0, "key name -1: %s", NativeHover_KeyName(-1));
	k = NATIVE_HOVER_KEYS;
	HOVER_EQ("key count", k, 4);
}

// ---------------------------------------------------------------------------
// 2 + 3 + 4: the height and the lift
// ---------------------------------------------------------------------------

internal void HoverSt_TestLift(void)
{
	NativeHoverTune t;
	NativeHoverTune z;
	const int classes[5] = {0, 2, 3, 255, -1};
	const int scales[3] = {0xccc, 0x1000, 0x2000};
	double h;
	double v;
	double mx;
	double mn;
	int ci;
	int si;
	int ph;
	int bad;
	int firstBad;
	u32 p;

	NativeHover_TuneDefaults(&t);

	// Height.
	NativeHover_TuneDefaults(&t);
	HOVER_NEAR("height 0xccc", NativeHover_HeightWE(&t, 0xccc), 12.796875, 1e-9);
	HOVER_NEAR("height 0x1000", NativeHover_HeightWE(&t, 0x1000), 16.0, 1e-9);
	HOVER_NEAR("height 0x800", NativeHover_HeightWE(&t, 0x800), 8.0, 1e-9);
	HOVER_NEAR("height NULL tune", NativeHover_HeightWE(NULL, 0xccc), 0.0, 0.0);

	// The class gate: exactly 0.0 (and +0.0) for every class but hover.
	for (ci = 0; ci < 5; ci++)
	{
		bad = 0;
		firstBad = 0;
		for (si = 0; si < 3; si++)
		{
			for (ph = 0; ph < 60; ph++)
			{
				v = NativeHover_Lift(classes[ci], &t, scales[si], (u32)ph);
				if (!(v == 0.0) || signbit(v))
				{
					if (!bad)
					{
						firstBad = si * 100 + ph;
					}
					bad++;
				}
			}
		}
		HOVER_EXPECT(bad == 0, "%s: %d lifts not exactly 0.0 (first at scale %d phase %d)", HoverSt_Tag("class gate %d (index %d)", classes[ci], ci), bad, firstBad / 100,
		             firstBad % 100);
	}

	// Hover with no tune / tune off / height 0.
	v = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, NULL, 0xccc, 3u);
	HOVER_EXPECT((v == 0.0) && !signbit(v), "hover with NULL tune: %.9f", v);
	z = t;
	z.on = 0;
	bad = 0;
	for (ph = 0; ph < 60; ph++)
	{
		v = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, (u32)ph);
		if (!(v == 0.0) || signbit(v))
		{
			bad++;
		}
	}
	HOVER_EXPECT(bad == 0, "hover with the tune off: %d lifts not 0.0", bad);
	z = t;
	z.heightMm = 0;
	v = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 7u);
	HOVER_EXPECT((v == 0.0) && !signbit(v), "hover with height 0: %.9f", v);

	// Hover, defaults.
	h = NativeHover_HeightWE(&t, 0xccc);
	HOVER_NEAR("lift phase 0", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &t, 0xccc, 0u), h, 1e-9);
	HOVER_NEAR("lift phase 15 (sin pi)", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &t, 0xccc, 15u), h, 1e-9);

	// Period 4: the four corners of the sine.
	z = t;
	z.bobFrames = 4;
	HOVER_NEAR("lift p4 phase 1", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 1u), h * 1.1, 1e-9);
	HOVER_NEAR("lift p4 phase 2", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 2u), h, 1e-9);
	HOVER_NEAR("lift p4 phase 3", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 3u), h * 0.9, 1e-9);
	HOVER_NEAR("lift p4 phase 5 = phase 1", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 5u), NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 1u), 0.0);
	HOVER_NEAR("lift p4 phase 4", NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, 4u), h, 1e-9);

	// Default period 30: the extremes.
	mx = -1e30;
	mn = 1e30;
	for (p = 0u; p < 30u; p++)
	{
		v = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &t, 0xccc, p);
		mx = (v > mx) ? v : mx;
		mn = (v < mn) ? v : mn;
	}
	HOVER_EXPECT(mx <= h * 1.1 + 1e-9, "period 30 max %.9f above %.9f", mx, h * 1.1);
	HOVER_EXPECT(mx > h * 1.09, "period 30 max %.9f not above %.9f", mx, h * 1.09);
	HOVER_EXPECT(mn >= h * 0.9 - 1e-9, "period 30 min %.9f below %.9f", mn, h * 0.9);
	HOVER_EXPECT(mn < h * 0.91, "period 30 min %.9f not below %.9f", mn, h * 0.91);

	// The lift scales with the driver scale.
	bad = 0;
	for (p = 0u; p < 30u; p++)
	{
		const double a = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &t, 0x1000, p);
		const double b = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &t, 0x800, p);

		if ((b == 0.0) || (fabs(a / b - 2.0) > 1e-9))
		{
			bad++;
		}
	}
	HOVER_EXPECT(bad == 0, "scale 0x1000 / 0x800 is not 2.0 in %d phases", bad);

	// bobFrames 0 in the tune (clamped to 2): finite.
	z = t;
	z.bobFrames = 0;
	bad = 0;
	for (p = 0u; p < 8u; p++)
	{
		v = NativeHover_Lift(RLDCHAR_VEHICLE_HOVER, &z, 0xccc, p);
		if (!((v == v) && (fabs(v) < 1e9)))
		{
			bad++;
		}
	}
	HOVER_EXPECT(bad == 0, "bobFrames 0: %d non-finite lifts", bad);
}

// ---------------------------------------------------------------------------
// 5: the ramp
// ---------------------------------------------------------------------------

internal void HoverSt_TestRamp(void)
{
	float v;
	int i;

	HOVER_NEAR("ramp 0->1", NativeHover_Ramp(0.0f, 1.0f, 0.25f), 0.25, 1e-6);
	v = 0.0f;
	for (i = 0; i < 4; i++)
	{
		v = NativeHover_Ramp(v, 1.0f, 0.25f);
	}
	HOVER_NEAR("ramp 4 steps reach 1", v, 1.0, 1e-6);
	HOVER_EXPECT(v == 1.0f, "ramp 4 steps not exactly 1.0: %.9f", (double)v);
	v = NativeHover_Ramp(v, 1.0f, 0.25f);
	HOVER_EXPECT(v == 1.0f, "ramp 5th step left 1.0: %.9f", (double)v);
	HOVER_NEAR("ramp 1->0", NativeHover_Ramp(1.0f, 0.0f, 0.25f), 0.75, 1e-6);
	HOVER_NEAR("ramp no overshoot", NativeHover_Ramp(0.9f, 1.0f, 0.25f), 1.0, 1e-6);
	HOVER_NEAR("ramp no undershoot", NativeHover_Ramp(0.1f, 0.0f, 0.25f), 0.0, 1e-6);
	HOVER_NEAR("ramp target 2 clamped to 1", NativeHover_Ramp(0.5f, 2.0f, 0.25f), 0.75, 1e-6);
	HOVER_NEAR("ramp target -1 clamped to 0", NativeHover_Ramp(0.5f, -1.0f, 0.25f), 0.25, 1e-6);
	HOVER_NEAR("ramp target NaN is 0", NativeHover_Ramp(0.5f, (float)NAN, 0.25f), 0.25, 1e-6);
	HOVER_NEAR("ramp step -1 stands", NativeHover_Ramp(0.5f, 1.0f, -1.0f), 0.5, 1e-6);
}

// ---------------------------------------------------------------------------
// 6: the targets
// ---------------------------------------------------------------------------

internal void HoverSt_SetIn(NativeHoverIn *in, int wheel, int gas, int brake)
{
	in->wheelRotation = wheel;
	in->gas = gas;
	in->brake = brake;
}

// All four targets against four wanted values (eps 1e-6).
internal void HoverSt_CheckTargets(const char *tag, const NativeHoverTune *t, int wheel, int gas, int brake, float ll, float lr, float nu, float nd)
{
	NativeHoverIn in;
	float tg[NATIVE_HOVER_KEYS];

	HoverSt_SetIn(&in, wheel, gas, brake);
	NativeHover_Targets(t, &in, tg);
	HOVER_EXPECT((fabs((double)tg[NATIVE_HOVER_KEY_LEAN_LEFT] - (double)ll) <= 1e-6) && (fabs((double)tg[NATIVE_HOVER_KEY_LEAN_RIGHT] - (double)lr) <= 1e-6) &&
	                 (fabs((double)tg[NATIVE_HOVER_KEY_NOSE_UP] - (double)nu) <= 1e-6) && (fabs((double)tg[NATIVE_HOVER_KEY_NOSE_DOWN] - (double)nd) <= 1e-6),
	             "targets %s: got %.4f %.4f %.4f %.4f, wanted %.4f %.4f %.4f %.4f", tag, (double)tg[0], (double)tg[1], (double)tg[2], (double)tg[3], (double)ll,
	             (double)lr, (double)nu, (double)nd);
}

internal void HoverSt_TestTargets(void)
{
	NativeHoverTune t;
	NativeHoverTune z;
	NativeHoverIn in;
	float tg[NATIVE_HOVER_KEYS];
	int k;
	int bad;

	NativeHover_TuneDefaults(&t);

	HoverSt_CheckTargets("left 64", &t, 64, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
	HoverSt_CheckTargets("right -64", &t, -64, 0, 0, 0.0f, 1.0f, 0.0f, 0.0f);
	HoverSt_CheckTargets("left 32", &t, 32, 0, 0, 0.5f, 0.0f, 0.0f, 0.0f);
	HoverSt_CheckTargets("left 200 clamped", &t, 200, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
	HoverSt_CheckTargets("straight", &t, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
	HoverSt_CheckTargets("gas", &t, 0, 1, 0, 0.0f, 0.0f, 1.0f, 0.0f);
	HoverSt_CheckTargets("brake", &t, 0, 0, 1, 0.0f, 0.0f, 0.0f, 1.0f);
	// The struct allows both; Targets does not arbitrate (ReadInput does).
	HoverSt_CheckTargets("gas + brake", &t, 0, 1, 1, 0.0f, 0.0f, 1.0f, 1.0f);

	z = t;
	z.lean = 500;
	HoverSt_CheckTargets("lean 500, left 64", &z, 64, 0, 0, 0.5f, 0.0f, 0.0f, 0.0f);
	z = t;
	z.lean = 2000;
	HoverSt_CheckTargets("lean 2000, left 32", &z, 32, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f);
	z = t;
	z.lean = 0;
	HoverSt_CheckTargets("lean 0, left 64", &z, 64, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
	z = t;
	z.nose = 500;
	HoverSt_CheckTargets("nose 500, gas", &z, 0, 1, 0, 0.0f, 0.0f, 0.5f, 0.0f);
	z = t;
	z.on = 0;
	HoverSt_CheckTargets("tune off", &z, 64, 1, 1, 0.0f, 0.0f, 0.0f, 0.0f);

	// NULL in: the array is cleared, not left alone.
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		tg[k] = -7.0f;
	}
	NativeHover_Targets(&t, NULL, tg);
	bad = 0;
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		bad += (tg[k] != 0.0f);
	}
	HOVER_EXPECT(bad == 0, "NULL in: %d targets not 0", bad);

	// NULL tune: same.
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		tg[k] = -7.0f;
	}
	HoverSt_SetIn(&in, 64, 1, 1);
	NativeHover_Targets(NULL, &in, tg);
	bad = 0;
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		bad += (tg[k] != 0.0f);
	}
	HOVER_EXPECT(bad == 0, "NULL tune: %d targets not 0", bad);
}

// ---------------------------------------------------------------------------
// 7 + 8: one tick of a seat's keys
// ---------------------------------------------------------------------------

internal void HoverSt_TestKeyStep(void)
{
	NativeHoverTune t;
	NativeHoverSeat s;
	NativeHoverIn in;
	float out[NATIVE_HOVER_KEYS];
	const int classes[5] = {0, 2, 3, 255, -1};
	int ci;
	int k;
	int i;
	int r;
	int bad;

	NativeHover_TuneDefaults(&t);

	// The class gate: nothing changes, nothing is written.
	for (ci = 0; ci < 5; ci++)
	{
		memset(&s, 0, sizeof(s));
		s.vehicle = classes[ci];
		for (k = 0; k < NATIVE_HOVER_KEYS; k++)
		{
			s.w[k] = 0.3f;
			out[k] = -7.0f;
		}
		HoverSt_SetIn(&in, 64, 1, 0);
		r = NativeHover_KeyStep(&s, &t, &in, out);
		bad = 0;
		for (k = 0; k < NATIVE_HOVER_KEYS; k++)
		{
			bad += (s.w[k] != 0.3f) + (out[k] != -7.0f);
		}
		HOVER_EXPECT((r == 0) && (bad == 0), "%s: returned %d, %d values changed", HoverSt_Tag("key step gate class %d (index %d)", classes[ci], ci), r, bad);
	}

	// A hover: left input ramps lean_left.
	memset(&s, 0, sizeof(s));
	s.vehicle = (int)RLDCHAR_VEHICLE_HOVER;
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		out[k] = -7.0f;
	}
	HoverSt_SetIn(&in, 64, 0, 0);
	r = NativeHover_KeyStep(&s, &t, &in, out);
	HOVER_EQ("key step hover returns 1", r, 1);
	HOVER_NEAR("key step 1: lean_left", out[NATIVE_HOVER_KEY_LEAN_LEFT], 0.25, 1e-6);
	bad = 0;
	for (k = 0; k < NATIVE_HOVER_KEYS; k++)
	{
		bad += (out[k] != s.w[k]);
	}
	HOVER_EXPECT(bad == 0, "key step 1: out differs from s.w in %d keys", bad);
	for (i = 0; i < 3; i++)
	{
		NativeHover_KeyStep(&s, &t, &in, out);
	}
	HOVER_NEAR("key step 4: lean_left", out[NATIVE_HOVER_KEY_LEAN_LEFT], 1.0, 1e-6);
	HOVER_NEAR("key step 4: lean_right stays 0", out[NATIVE_HOVER_KEY_LEAN_RIGHT], 0.0, 1e-6);

	// Back to straight: 4 ticks to 0.
	HoverSt_SetIn(&in, 0, 0, 0);
	for (i = 0; i < 4; i++)
	{
		NativeHover_KeyStep(&s, &t, &in, out);
	}
	HOVER_NEAR("key step straight x4: lean_left", out[NATIVE_HOVER_KEY_LEAN_LEFT], 0.0, 1e-6);

	// The sign: right input never raises lean_left.
	HoverSt_SetIn(&in, -64, 0, 0);
	for (i = 0; i < 4; i++)
	{
		NativeHover_KeyStep(&s, &t, &in, out);
	}
	HOVER_NEAR("key step right x4: lean_right", out[NATIVE_HOVER_KEY_LEAN_RIGHT], 1.0, 1e-6);
	HOVER_NEAR("key step right x4: lean_left stays 0", out[NATIVE_HOVER_KEY_LEAN_LEFT], 0.0, 1e-6);

	// Fresh seat, left input: lean_right stays 0.
	memset(&s, 0, sizeof(s));
	s.vehicle = (int)RLDCHAR_VEHICLE_HOVER;
	HoverSt_SetIn(&in, 64, 0, 0);
	for (i = 0; i < 4; i++)
	{
		NativeHover_KeyStep(&s, &t, &in, out);
	}
	HOVER_NEAR("key step left x4: lean_right stays 0", out[NATIVE_HOVER_KEY_LEAN_RIGHT], 0.0, 1e-6);

	// NULL arguments.
	HoverSt_SetIn(&in, 64, 1, 0);
	HOVER_EQ("key step NULL seat", NativeHover_KeyStep(NULL, &t, &in, out), 0);
	HOVER_EQ("key step NULL tune", NativeHover_KeyStep(&s, NULL, &in, out), 0);
	HOVER_EQ("key step NULL in", NativeHover_KeyStep(&s, &t, NULL, out), 0);
	HOVER_EQ("key step NULL out", NativeHover_KeyStep(&s, &t, &in, NULL), 0);
}

// ---------------------------------------------------------------------------
// 10: the module (s_hover)
// ---------------------------------------------------------------------------

internal void HoverSt_TestModule(void)
{
	static struct Driver d;
	const int classes[5] = {0, 2, 3, 255, -1};
	const char *const names[5] = {"a", "b", "c", "d", "e"};
	u32 seen0;
	u32 classic0;
	u32 lifted0;
	u32 ticks0;
	u32 keyTicks0;
	u32 keyWrites0;
	u32 held0;
	int seat;
	int bad;
	double v;
	double want;

	memset(&d, 0, sizeof(d));
	NativeHover_TuneDefaults(NativeHover_Tunables());
	NativeHover_LevelReset();

	// After a reset every seat is a kart without a lift.
	bad = 0;
	for (seat = 0; seat < NATIVE_HOVER_SEATS; seat++)
	{
		if ((NativeHover_SeatVehicle(seat) != 0) || (NativeHover_SeatLift(seat, 0xccc) != 0.0))
		{
			bad++;
		}
	}
	HOVER_EXPECT(bad == 0, "after reset: %d seats with a class or a lift", bad);
	HOVER_EQ("seat vehicle -1", NativeHover_SeatVehicle(-1), 0);
	HOVER_EQ("seat vehicle 8", NativeHover_SeatVehicle(8), 0);
	v = NativeHover_SeatLift(-1, 0xccc);
	HOVER_EXPECT(v == 0.0, "seat lift -1: %.9f", v);
	v = NativeHover_SeatLift(8, 0xccc);
	HOVER_EXPECT(v == 0.0, "seat lift 8: %.9f", v);

	// The level load names a non-hover class: still a kart, no lift.
	for (seat = 0; seat < 5; seat++)
	{
		NativeHover_LevelSeat(seat, classes[seat], names[seat], 1);
	}
	bad = 0;
	for (seat = 0; seat < 5; seat++)
	{
		if ((NativeHover_SeatVehicle(seat) != 0) || (NativeHover_SeatLift(seat, 0xccc) != 0.0))
		{
			bad++;
		}
	}
	HOVER_EXPECT(bad == 0, "level seats of kart/plane/other classes: %d seats with a class or a lift", bad);

	// Their ticks do nothing at all.
	seen0 = s_hover.seen;
	classic0 = s_hover.classic;
	ticks0 = s_hover.ticks;
	keyTicks0 = s_hover.keyTicks;
	keyWrites0 = s_hover.keyWrites;
	held0 = s_hover.held;
	for (seat = 0; seat < 5; seat++)
	{
		NativeHover_EngineTick(seat, &d, 1);
	}
	bad = 0;
	for (seat = 0; seat < 5; seat++)
	{
		bad += (s_hover.seat[seat].phase != 0u);
	}
	HOVER_EXPECT(bad == 0, "non-hover ticks moved the phase of %d seats", bad);
	HOVER_EXPECT((s_hover.ticks == ticks0) && (s_hover.keyTicks == keyTicks0) && (s_hover.keyWrites == keyWrites0) && (s_hover.held == held0),
	             "non-hover ticks changed the counters");
	HOVER_EXPECT((s_hover.seen == seen0) && (s_hover.classic == classic0), "non-hover level seats changed seen/classic");

	// NULL driver / seat out of range: nothing.
	NativeHover_EngineTick(0, NULL, 1);
	NativeHover_EngineTick(9, &d, 1);
	NativeHover_EngineTick(-1, &d, 1);
	HOVER_EXPECT((s_hover.ticks == ticks0) && (s_hover.keyTicks == keyTicks0) && (s_hover.keyWrites == keyWrites0) && (s_hover.held == held0),
	             "NULL driver / seat out of range changed the counters");

	// A hover on the classic model: logged, counted, not lifted.
	classic0 = s_hover.classic;
	NativeHover_LevelSeat(5, 1, "dummy-hover-hover.rldchar", 0);
	HOVER_EQ("classic hover seat stays a kart", NativeHover_SeatVehicle(5), 0);
	v = NativeHover_SeatLift(5, 0xccc);
	HOVER_EXPECT(v == 0.0, "classic hover seat lift: %.9f", v);
	HOVER_EXPECT(s_hover.classic == classic0 + 1u, "classic counter: %u, wanted %u", (unsigned)s_hover.classic, (unsigned)(classic0 + 1u));

	// A hover seat (set directly: no engine character in the selftest).
	s_hover.seat[6].vehicle = (int)RLDCHAR_VEHICLE_HOVER;
	s_hover.seat[6].phase = 0u;
	want = NativeHover_HeightWE(NativeHover_Tunables(), 0xccc);
	lifted0 = s_hover.liftedDraws;
	v = NativeHover_SeatLift(6, 0xccc);
	HOVER_NEAR("hover seat lift at phase 0", v, want, 1e-9);
	HOVER_EXPECT(s_hover.liftedDraws == lifted0 + 1u, "lifted draws: %u, wanted %u", (unsigned)s_hover.liftedDraws, (unsigned)(lifted0 + 1u));

	d.actionsFlagSet = ACTION_BOT;
	ticks0 = s_hover.ticks;
	keyTicks0 = s_hover.keyTicks;
	keyWrites0 = s_hover.keyWrites;
	NativeHover_EngineTick(6, &d, 0);
	HOVER_EQ("hover tick without keys: phase", s_hover.seat[6].phase, 1);
	HOVER_EXPECT(s_hover.ticks == ticks0 + 1u, "hover tick: ticks %u, wanted %u", (unsigned)s_hover.ticks, (unsigned)(ticks0 + 1u));
	HOVER_EXPECT(s_hover.keyTicks == keyTicks0, "hover tick without keys changed keyTicks");
	NativeHover_EngineTick(6, &d, 1);
	HOVER_EQ("hover tick with keys: phase", s_hover.seat[6].phase, 2);
	HOVER_EXPECT(s_hover.keyTicks == keyTicks0 + 1u, "hover tick with keys: keyTicks %u, wanted %u", (unsigned)s_hover.keyTicks, (unsigned)(keyTicks0 + 1u));
	HOVER_EXPECT(s_hover.keyWrites == keyWrites0, "hover tick wrote keys without an engine character: %u", (unsigned)(s_hover.keyWrites - keyWrites0));

	// The tune off: no lift.
	NativeHover_Tunables()->on = 0;
	v = NativeHover_SeatLift(6, 0xccc);
	HOVER_EXPECT((v == 0.0) && !signbit(v), "hover seat lift with the tune off: %.9f", v);
	NativeHover_Tunables()->on = 1;
}

// ---------------------------------------------------------------------------
// The entry point
// ---------------------------------------------------------------------------

int NativeHover_SelfTest(void)
{
	s_hoverStChecks = 0;
	s_hoverStFails = 0;

	HoverSt_TestTune();
	HoverSt_TestLift();
	HoverSt_TestRamp();
	HoverSt_TestTargets();
	HoverSt_TestKeyStep();
	HoverSt_TestModule();

	// Leave nothing behind.
	NativeHover_LevelReset();
	NativeHover_TuneDefaults(NativeHover_Tunables());
	s_hover.seen = 0u;
	s_hover.classic = 0u;
	s_hover.liftedDraws = 0u;
	s_hover.ticks = 0u;
	s_hover.keyTicks = 0u;
	s_hover.keyWrites = 0u;
	s_hover.held = 0u;
	s_hover.liftMin = 0.0;
	s_hover.liftMax = 0.0;

	printf("hover selftest %s: %d checks, %d failures\n", (s_hoverStFails == 0) ? "passed" : "FAILED", s_hoverStChecks, s_hoverStFails);
	return (s_hoverStFails == 0) ? 0 : 1;
}
