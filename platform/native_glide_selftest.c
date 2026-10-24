// ===========================================================================
// THE GLIDE ABILITY (TEST): the unit test of its pure parts (ctest
// glide_selftest). Included by main.c after native_glide.c (unity build).
// Everything here runs on local structs; the module's own state is only
// touched by the two hooks with the ability off (they hand their value back).
// ===========================================================================

global_variable int s_glideStChecks;
global_variable int s_glideStFails;

#define GLIDE_EXPECT(cond, ...) \
	do \
	{ \
		s_glideStChecks++; \
		if (!(cond)) \
		{ \
			s_glideStFails++; \
			printf("glide selftest: "); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

// Integer compare: the tag names the case, the message gives actual and wanted.
#define GLIDE_EQ(tag, got, want) \
	do \
	{ \
		const int g_ = (int)(got); \
		const int w_ = (int)(want); \
		GLIDE_EXPECT(g_ == w_, "%s: got %d, wanted %d", (tag), g_, w_); \
	} while (0)

// Unsigned compare (pad masks), printed in hex.
#define GLIDE_EQU(tag, got, want) \
	do \
	{ \
		const unsigned int g_ = (unsigned int)(got); \
		const unsigned int w_ = (unsigned int)(want); \
		GLIDE_EXPECT(g_ == w_, "%s: got 0x%x, wanted 0x%x", (tag), g_, w_); \
	} while (0)

// A tag with up to two numbers in it (a small ring of buffers).
internal const char *GlideSt_Tag(const char *fmt, int a, int b)
{
	static char bufs[4][96];
	static int next = 0;
	char *buf = bufs[next];

	next = (next + 1) & 3;
	snprintf(buf, 96, fmt, a, b);
	return buf;
}

// ---------------------------------------------------------------------------
// The state machine's rig
// ---------------------------------------------------------------------------

typedef struct
{
	NativeGlideTune tune;
	NativeGlideState st;
} GlideStRig;

internal void GlideSt_RigInit(GlideStRig *r)
{
	NativeGlide_TuneDefaults(&r->tune);
	NativeGlide_StateReset(&r->st);
}

internal NativeGlideOut GlideSt_Tick(GlideStRig *r, int eligible, int airborne, int held, int ly)
{
	NativeGlideIn in;
	NativeGlideOut out;

	in.eligible = eligible;
	in.airborne = airborne;
	in.r3Held = held;
	in.stickLY = ly;
	NativeGlide_Step(&r->st, &r->tune, &in, &out);
	return out;
}

// The free air tick, the press tick and its followers: n opening ticks in all
// (the press is the first), then optionally the 6 ticks of the flap, stick at
// rest.
internal void GlideSt_OpenN(GlideStRig *r, int n, int waitFlap)
{
	int i;

	(void)GlideSt_Tick(r, 1, 1, 0, 0x80);
	for (i = 0; i < n; i++)
	{
		(void)GlideSt_Tick(r, 1, 1, 1, 0x80);
	}
	if (waitFlap)
	{
		for (i = 0; i < NATIVE_GLIDE_FLAP_TICKS; i++)
		{
			(void)GlideSt_Tick(r, 1, 1, 1, 0x80);
		}
	}
}

internal void GlideSt_OpenFully(GlideStRig *r)
{
	GlideSt_OpenN(r, 6, 1);
}

// Open fully, then the stick fully forward until the dive share is 1000.
internal void GlideSt_OpenDive(GlideStRig *r)
{
	int i;

	GlideSt_OpenFully(r);
	for (i = 0; i < 6; i++)
	{
		(void)GlideSt_Tick(r, 1, 1, 1, 0x00);
	}
}

internal int GlideSt_Lcg(unsigned int *x)
{
	*x = *x * 1664525u + 1013904223u;
	return (int)((*x >> 16) & 0x7fff);
}

// A pseudo-random input sequence (fixed seed). heldRandom 0: R3 never held.
internal NativeGlideIn GlideSt_RandomIn(unsigned int *x, NativeGlideIn prev, int heldRandom)
{
	NativeGlideIn in = prev;
	const int a = GlideSt_Lcg(x);
	const int b = GlideSt_Lcg(x);

	if ((a % 8) == 0)
	{
		in.airborne ^= 1;
	}
	if (heldRandom && ((a % 5) == 0))
	{
		in.r3Held ^= 1;
	}
	if (!heldRandom)
	{
		in.r3Held = 0;
	}
	in.eligible = ((b % 16) != 0) ? 1 : 0;
	in.stickLY = (b >> 4) & 0xff;
	return in;
}

internal void GlideSt_TestBasics(void)
{
	NativeGlideTune t;
	int i;
	static const int rateFrames[8] = {1, 2, 3, 4, 6, 20, 0, 99};
	static const int rateWant[8] = {1000, 500, 334, 250, 167, 50, 1000, 50};
	static const int gIn[6][3] = {{-900, 300, -270}, {-900, 1000, -900}, {-369, 300, -110}, {0, 300, 0}, {-900, 0, 0}, {-900, 200, -180}};
	static const int fwdIn[5] = {0x80, 0x70, 0x00, 0x38, -5};
	static const int fwdWant[5] = {0, 0, 1000, 500, 1000};
	static const int backIn[5] = {0x80, 0x90, 0xFF, 0xC7, 300};
	static const int backWant[5] = {0, 0, 1000, 495, 1000};
	static const int flapWant[9] = {0, 0, 333, 666, 1000, 666, 333, 0, 0};

	// Case 1: defaults and clamps.
	NativeGlide_TuneDefaults(&t);
	GLIDE_EQ("case 1 default glide", t.glide, 300);
	GLIDE_EQ("case 1 default dive", t.dive, 1000);
	GLIDE_EQ("case 1 default pullUp", t.pullUp, 200);
	GLIDE_EQ("case 1 default openFrames", t.openFrames, 6);
	GLIDE_EQ("case 1 default closeRelease", t.closeRelease, 4);
	GLIDE_EQ("case 1 default closeLand", t.closeLand, 2);
	t.glide = -5;
	t.dive = 9999;
	t.openFrames = 0;
	t.closeLand = 50;
	NativeGlide_TuneClamp(&t);
	GLIDE_EQ("case 1 clamp glide -5", t.glide, 0);
	GLIDE_EQ("case 1 clamp dive 9999", t.dive, 1500);
	GLIDE_EQ("case 1 clamp openFrames 0", t.openFrames, 1);
	GLIDE_EQ("case 1 clamp closeLand 50", t.closeLand, 20);
	GLIDE_EQ("case 1 clamp pullUp untouched", t.pullUp, 200);
	GLIDE_EQ("case 1 clamp closeRelease untouched", t.closeRelease, 4);

	// Case 2: the rate.
	for (i = 0; i < 8; i++)
	{
		GLIDE_EQ(GlideSt_Tag("case 2 Rate(%d) [row %d]", rateFrames[i], i), NativeGlide_Rate(rateFrames[i]), rateWant[i]);
	}

	// Case 3: gravity scaling, towards zero.
	for (i = 0; i < 6; i++)
	{
		GLIDE_EQ(GlideSt_Tag("case 3 ScaleGravity(%d, %d)", gIn[i][0], gIn[i][1]), NativeGlide_ScaleGravity(gIn[i][0], gIn[i][1]), gIn[i][2]);
	}

	// Case 4: the stick shares.
	for (i = 0; i < 5; i++)
	{
		GLIDE_EQ(GlideSt_Tag("case 4 StickForward(%d) [row %d]", fwdIn[i], i), NativeGlide_StickForward(fwdIn[i]), fwdWant[i]);
		GLIDE_EQ(GlideSt_Tag("case 4 StickBack(%d) [row %d]", backIn[i], i), NativeGlide_StickBack(backIn[i]), backWant[i]);
	}

	// Case 5: the flap weights for flapStep -1..7.
	for (i = 0; i < 9; i++)
	{
		GLIDE_EQ(GlideSt_Tag("case 5 FlapWeight(%d) [row %d]", i - 1, i), NativeGlide_FlapWeight(i - 1), flapWant[i]);
	}
}

internal void GlideSt_TestOpenAndFlight(void)
{
	GlideStRig r;
	NativeGlideOut o;
	int i;
	static const int chainWant[6] = {167, 334, 501, 668, 835, 1000};
	static const int flapWant[6] = {333, 666, 1000, 666, 333, 0};
	static const int fwdUp[6] = {167, 334, 501, 668, 835, 1000};
	static const int fwdDown[6] = {833, 666, 499, 332, 165, 0};

	// Case 6: a press on the ground does not open.
	GlideSt_RigInit(&r);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x80);
	GLIDE_EQ("case 6 pressed", o.pressed, 1);
	GLIDE_EQ("case 6 gliding", o.gliding, 0);
	GLIDE_EQ("case 6 factor", o.factor, 1000);
	GLIDE_EQ("case 6 chain", o.chain, 0);
	GLIDE_EQ("case 6 event", o.event, NATIVE_GLIDE_EV_NONE);
	GLIDE_EQ("case 6 state", r.st.state, NATIVE_GLIDE_FOLDED);

	// Case 7: held from the ground into the air: no edge, no opening.
	GlideSt_RigInit(&r);
	(void)GlideSt_Tick(&r, 1, 0, 1, 0x80);
	o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
	GLIDE_EQ("case 7 state", r.st.state, NATIVE_GLIDE_FOLDED);
	GLIDE_EQ("case 7 gliding", o.gliding, 0);
	GLIDE_EQ("case 7 pressed", o.pressed, 0);

	// Case 8: the opening.
	GlideSt_RigInit(&r);
	o = GlideSt_Tick(&r, 1, 1, 0, 0x80);
	GLIDE_EQ("case 8 free air tick gliding", o.gliding, 0);
	o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
	GLIDE_EQ("case 8 press event", o.event, NATIVE_GLIDE_EV_OPEN);
	GLIDE_EQ("case 8 press state", r.st.state, NATIVE_GLIDE_OPENING);
	GLIDE_EQ("case 8 press gliding", o.gliding, 1);
	GLIDE_EQ("case 8 press factor", o.factor, 300);
	GLIDE_EQ("case 8 press chain", o.chain, chainWant[0]);
	for (i = 1; i < 6; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
		GLIDE_EQ(GlideSt_Tag("case 8 opening tick %d chain [i %d]", i + 1, i), o.chain, chainWant[i]);
		GLIDE_EQ(GlideSt_Tag("case 8 opening tick %d gliding [i %d]", i + 1, i), o.gliding, 1);
		if (i < 5)
		{
			GLIDE_EQ(GlideSt_Tag("case 8 opening tick %d event [i %d]", i + 1, i), o.event, NATIVE_GLIDE_EV_NONE);
			GLIDE_EQ(GlideSt_Tag("case 8 opening tick %d state [i %d]", i + 1, i), r.st.state, NATIVE_GLIDE_OPENING);
		}
	}
	GLIDE_EQ("case 8 6th tick event", o.event, NATIVE_GLIDE_EV_OPENED);
	GLIDE_EQ("case 8 6th tick state", r.st.state, NATIVE_GLIDE_OPEN);
	GLIDE_EQ("case 8 6th tick flap (starts at 0)", o.flap, 0);

	// Case 9: the flap after the opening (continues case 8).
	for (i = 0; i < 6; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
		GLIDE_EQ(GlideSt_Tag("case 9 flap tick %d [i %d]", i + 1, i), o.flap, flapWant[i]);
		GLIDE_EQ(GlideSt_Tag("case 9 gliding tick %d [i %d]", i + 1, i), o.gliding, 1);
		GLIDE_EQ(GlideSt_Tag("case 9 factor tick %d [i %d]", i + 1, i), o.factor, 300);
		GLIDE_EQ(GlideSt_Tag("case 9 chain tick %d [i %d]", i + 1, i), o.chain, 1000);
	}

	// Case 10: dive with the stick forward, then back to rest.
	GlideSt_RigInit(&r);
	GlideSt_OpenFully(&r);
	for (i = 0; i < 6; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 1, 0x00);
		GLIDE_EQ(GlideSt_Tag("case 10 fwd tick %d [i %d]", i + 1, i), r.st.fwd, fwdUp[i]);
		GLIDE_EQ(GlideSt_Tag("case 10 dive tick %d [i %d]", i + 1, i), o.dive, fwdUp[i]);
		GLIDE_EQ(GlideSt_Tag("case 10 factor tick %d [i %d]", i + 1, i), o.factor, 300 + 700 * fwdUp[i] / 1000);
	}
	GLIDE_EQ("case 10 final factor", o.factor, 1000);
	for (i = 0; i < 6; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
		GLIDE_EQ(GlideSt_Tag("case 10 release fwd tick %d [i %d]", i + 1, i), r.st.fwd, fwdDown[i]);
		GLIDE_EQ(GlideSt_Tag("case 10 release factor tick %d [i %d]", i + 1, i), o.factor, 300 + 700 * fwdDown[i] / 1000);
	}
	GLIDE_EQ("case 10 final factor back at glide", o.factor, 300);

	// Case 11: pull up.
	GlideSt_RigInit(&r);
	GlideSt_OpenFully(&r);
	for (i = 0; i < 6; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 1, 0xFF);
	}
	GLIDE_EQ("case 11 back", r.st.back, 1000);
	GLIDE_EQ("case 11 pullUp", o.pullUp, 1000);
	GLIDE_EQ("case 11 factor", o.factor, 200);
	GLIDE_EQ("case 11 dive stays 0", o.dive, 0);
}

internal void GlideSt_TestClosing(void)
{
	GlideStRig r;
	NativeGlideOut o;
	int i;
	static const int closeWant[3] = {500, 250, 0};
	static const int endWant[4] = {750, 500, 250, 0};

	// Case 12: close on release with dive 1.
	GlideSt_RigInit(&r);
	GlideSt_OpenDive(&r);
	o = GlideSt_Tick(&r, 1, 1, 0, 0x00);
	GLIDE_EQ("case 12 release event", o.event, NATIVE_GLIDE_EV_CLOSE_RELEASE);
	GLIDE_EQ("case 12 release state", r.st.state, NATIVE_GLIDE_CLOSING);
	GLIDE_EQ("case 12 release gliding", o.gliding, 0);
	GLIDE_EQ("case 12 release factor", o.factor, 1000);
	GLIDE_EQ("case 12 release chain", o.chain, 750);
	GLIDE_EQ("case 12 release dive", o.dive, 750);
	for (i = 0; i < 3; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 0, 0x80);
		GLIDE_EQ(GlideSt_Tag("case 12 closing tick %d chain [i %d]", i + 2, i), o.chain, closeWant[i]);
		GLIDE_EQ(GlideSt_Tag("case 12 closing tick %d dive [i %d]", i + 2, i), o.dive, closeWant[i]);
		GLIDE_EQ(GlideSt_Tag("case 12 closing tick %d gliding [i %d]", i + 2, i), o.gliding, 0);
		if (i < 2)
		{
			GLIDE_EQ(GlideSt_Tag("case 12 closing tick %d state [i %d]", i + 2, i), r.st.state, NATIVE_GLIDE_CLOSING);
			GLIDE_EQ(GlideSt_Tag("case 12 closing tick %d event [i %d]", i + 2, i), o.event, NATIVE_GLIDE_EV_NONE);
		}
	}
	GLIDE_EQ("case 12 4th tick state", r.st.state, NATIVE_GLIDE_FOLDED);
	GLIDE_EQ("case 12 4th tick event", o.event, NATIVE_GLIDE_EV_FOLDED);

	// Case 13: close on landing with dive 1 (R3 still held).
	GlideSt_RigInit(&r);
	GlideSt_OpenDive(&r);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x00);
	GLIDE_EQ("case 13 land event", o.event, NATIVE_GLIDE_EV_CLOSE_LAND);
	GLIDE_EQ("case 13 land gliding", o.gliding, 0);
	GLIDE_EQ("case 13 land chain", o.chain, 500);
	GLIDE_EQ("case 13 land dive", o.dive, 500);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x00);
	GLIDE_EQ("case 13 2nd tick chain", o.chain, 0);
	GLIDE_EQ("case 13 2nd tick dive", o.dive, 0);
	GLIDE_EQ("case 13 2nd tick state", r.st.state, NATIVE_GLIDE_FOLDED);
	GLIDE_EQ("case 13 2nd tick event", o.event, NATIVE_GLIDE_EV_FOLDED);

	// Case 14: landing with a running flap (333 * 500 / 1000 = 166).
	GlideSt_RigInit(&r);
	GlideSt_OpenN(&r, 6, 0);
	o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
	GLIDE_EQ("case 14 flap before landing", o.flap, 333);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x80);
	GLIDE_EQ("case 14 land event", o.event, NATIVE_GLIDE_EV_CLOSE_LAND);
	GLIDE_EQ("case 14 flap at land tick", o.flap, 166);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x80);
	GLIDE_EQ("case 14 flap next tick", o.flap, 0);

	// Case 15: the end (not eligible) closes over closeRelease.
	GlideSt_RigInit(&r);
	GlideSt_OpenFully(&r);
	o = GlideSt_Tick(&r, 0, 1, 1, 0x80);
	GLIDE_EQ("case 15 end event", o.event, NATIVE_GLIDE_EV_CLOSE_END);
	GLIDE_EQ("case 15 tick 1 chain", o.chain, endWant[0]);
	for (i = 1; i < 4; i++)
	{
		o = GlideSt_Tick(&r, 0, 1, 1, 0x80);
		GLIDE_EQ(GlideSt_Tag("case 15 tick %d chain [i %d]", i + 1, i), o.chain, endWant[i]);
	}
	GLIDE_EQ("case 15 4th tick state", r.st.state, NATIVE_GLIDE_FOLDED);
	GLIDE_EQ("case 15 4th tick event", o.event, NATIVE_GLIDE_EV_FOLDED);

	// Case 16: re-open while closing (a new edge).
	GlideSt_RigInit(&r);
	GlideSt_OpenFully(&r);
	o = GlideSt_Tick(&r, 1, 1, 0, 0x80);
	GLIDE_EQ("case 16 release chain", o.chain, 750);
	o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
	GLIDE_EQ("case 16 reopen event", o.event, NATIVE_GLIDE_EV_OPEN);
	GLIDE_EQ("case 16 reopen state", r.st.state, NATIVE_GLIDE_OPENING);
	GLIDE_EQ("case 16 reopen chain", o.chain, 917);
	o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
	GLIDE_EQ("case 16 next chain", o.chain, 1000);
	GLIDE_EQ("case 16 next state", r.st.state, NATIVE_GLIDE_OPEN);

	// Case 17: no re-open on the ground.
	GlideSt_RigInit(&r);
	GlideSt_OpenFully(&r);
	o = GlideSt_Tick(&r, 1, 0, 0, 0x80);
	GLIDE_EQ("case 17 land chain", o.chain, 500);
	GLIDE_EQ("case 17 land state", r.st.state, NATIVE_GLIDE_CLOSING);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x80);
	GLIDE_EQ("case 17 press on ground pressed", o.pressed, 1);
	GLIDE_EQ("case 17 press on ground gliding", o.gliding, 0);
	GLIDE_EQ("case 17 press on ground event", o.event, NATIVE_GLIDE_EV_FOLDED);
	GLIDE_EQ("case 17 press on ground state", r.st.state, NATIVE_GLIDE_FOLDED);

	// Case 18: landing while opening.
	GlideSt_RigInit(&r);
	(void)GlideSt_Tick(&r, 1, 1, 0, 0x80);
	(void)GlideSt_Tick(&r, 1, 1, 1, 0x80);
	o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
	GLIDE_EQ("case 18 chain before landing", o.chain, 334);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x80);
	GLIDE_EQ("case 18 land event", o.event, NATIVE_GLIDE_EV_CLOSE_LAND);
	GLIDE_EQ("case 18 land chain", o.chain, 0);
	GLIDE_EQ("case 18 land state", r.st.state, NATIVE_GLIDE_FOLDED);
	GLIDE_EQ("case 18 land gliding", o.gliding, 0);
}

internal void GlideSt_TestTunablesAndRuns(void)
{
	GlideStRig r;
	NativeGlideOut o;
	static NativeGlideOut outsA[2000];
	NativeGlideIn in;
	unsigned int x;
	int i;
	int bad;
	int gliding;
	static const int chain3[3] = {334, 668, 1000};

	// Case 19: tunables.
	GlideSt_RigInit(&r);
	r.tune.glide = 500;
	r.tune.openFrames = 3;
	(void)GlideSt_Tick(&r, 1, 1, 0, 0x80);
	for (i = 0; i < 3; i++)
	{
		o = GlideSt_Tick(&r, 1, 1, 1, 0x80);
		GLIDE_EQ(GlideSt_Tag("case 19 tick %d factor [i %d]", i + 1, i), o.factor, 500);
		GLIDE_EQ(GlideSt_Tag("case 19 tick %d chain [i %d]", i + 1, i), o.chain, chain3[i]);
	}
	GLIDE_EQ("case 19 3rd tick state", r.st.state, NATIVE_GLIDE_OPEN);
	GlideSt_RigInit(&r);
	r.tune.openFrames = 3;
	r.tune.closeLand = 1;
	GlideSt_OpenN(&r, 3, 1);
	o = GlideSt_Tick(&r, 1, 0, 1, 0x80);
	GLIDE_EQ("case 19 closeLand 1 event", o.event, NATIVE_GLIDE_EV_CLOSE_LAND);
	GLIDE_EQ("case 19 closeLand 1 state", r.st.state, NATIVE_GLIDE_FOLDED);
	GLIDE_EQ("case 19 closeLand 1 chain", o.chain, 0);

	// Case 20: never pressed. ONE check for the whole run of 2000 ticks (the
	// message names the first tick that broke).
	GlideSt_RigInit(&r);
	x = 12345u;
	memset(&in, 0, sizeof(in));
	bad = -1;
	for (i = 0; i < 2000; i++)
	{
		in = GlideSt_RandomIn(&x, in, 0);
		o = GlideSt_Tick(&r, in.eligible, in.airborne, in.r3Held, in.stickLY);
		if ((bad < 0) && ((o.gliding != 0) || (o.factor != 1000) || (o.chain != 0) || (o.flap != 0) || (o.dive != 0) || (o.pullUp != 0) ||
		                  (r.st.state != NATIVE_GLIDE_FOLDED)))
		{
			bad = i;
		}
	}
	GLIDE_EXPECT(bad < 0, "case 20 never pressed: tick %d left the folded state (last tick: gliding %d factor %d chain %d state %d)", bad, o.gliding,
	             o.factor, o.chain, r.st.state);

	// Case 21: determinism over all four inputs, twice from a reset state.
	GlideSt_RigInit(&r);
	x = 987654321u;
	memset(&in, 0, sizeof(in));
	gliding = 0;
	for (i = 0; i < 2000; i++)
	{
		in = GlideSt_RandomIn(&x, in, 1);
		outsA[i] = GlideSt_Tick(&r, in.eligible, in.airborne, in.r3Held, in.stickLY);
		gliding += outsA[i].gliding;
	}
	GlideSt_RigInit(&r);
	x = 987654321u;
	memset(&in, 0, sizeof(in));
	bad = -1;
	for (i = 0; i < 2000; i++)
	{
		in = GlideSt_RandomIn(&x, in, 1);
		o = GlideSt_Tick(&r, in.eligible, in.airborne, in.r3Held, in.stickLY);
		if ((bad < 0) && (memcmp(&o, &outsA[i], sizeof(o)) != 0))
		{
			bad = i;
		}
	}
	GLIDE_EXPECT(bad < 0, "case 21 determinism: the second run differs at tick %d", bad);
	GLIDE_EXPECT(gliding > 0, "case 21 determinism: the sequence never glided (%d glide ticks of 2000)", gliding);
}

// ---------------------------------------------------------------------------
// The script
// ---------------------------------------------------------------------------

internal void GlideSt_ScriptInit(NativeGlideScript *p, const int *codes, int n)
{
	int i;

	memset(p, 0, sizeof(*p));
	for (i = 0; i < n; i++)
	{
		p->codes[i] = codes[i];
	}
	p->count = n;
	NativeGlide_ScriptReset(p);
}

internal NativeGlideScriptOut GlideSt_SS(NativeGlideScript *p, int racing, int airborne, int grabbed)
{
	NativeGlideScriptIn in;
	NativeGlideScriptOut out;

	in.racing = racing;
	in.airborne = airborne;
	in.grabbed = grabbed;
	NativeGlide_ScriptStep(p, &in, &out);
	return out;
}

// 59 quiet ground ticks (one check), then the starting tick (returned).
internal NativeGlideScriptOut GlideSt_ToStart(NativeGlideScript *p, const char *tag)
{
	NativeGlideScriptOut o;
	int i;
	int bad = -1;

	for (i = 1; i < NATIVE_GLIDE_SCRIPT_GROUND_TICKS; i++)
	{
		o = GlideSt_SS(p, 1, 0, 0);
		if ((bad < 0) && ((o.press != 0) || (o.release != 0) || (o.event != 0)))
		{
			bad = i;
		}
	}
	GLIDE_EXPECT(bad < 0, "%s: ground tick %d before the start was not quiet", tag, bad);
	return GlideSt_SS(p, 1, 0, 0);
}

// n air ticks; returns the last. Checks nothing.
internal NativeGlideScriptOut GlideSt_Air(NativeGlideScript *p, int n)
{
	NativeGlideScriptOut o;
	int i;

	memset(&o, 0, sizeof(o));
	for (i = 0; i < n; i++)
	{
		o = GlideSt_SS(p, 1, 1, 0);
	}
	return o;
}

internal void GlideSt_TestScriptParse(void)
{
	int codes[NATIVE_GLIDE_SCRIPT_MAX];
	int count;
	char why[200];
	char big[256];
	int i;
	int n;

	n = NativeGlide_ScriptParse("h,dl,dr,DL,DR,sl,sr,pu,ol,or", codes, &count, why, (int)sizeof(why));
	GLIDE_EQ("case 22 full list result", n, 1);
	GLIDE_EQ("case 22 full list count", count, 10);
	for (i = 0; i < 10; i++)
	{
		GLIDE_EQ(GlideSt_Tag("case 22 full list code %d [i %d]", i, i), codes[i], i);
	}
	n = NativeGlide_ScriptParse("dl", codes, &count, why, (int)sizeof(why));
	GLIDE_EQ("case 22 'dl' result", n, 1);
	GLIDE_EQ("case 22 'dl' count", count, 1);
	GLIDE_EQ("case 22 'dl' code", codes[0], NATIVE_GLIDE_MAN_DL);
	GLIDE_EQ("case 22 '' result", NativeGlide_ScriptParse("", codes, &count, why, (int)sizeof(why)), 0);
	GLIDE_EQ("case 22 'x' result", NativeGlide_ScriptParse("x", codes, &count, why, (int)sizeof(why)), 0);
	GLIDE_EQ("case 22 'h,,dl' result", NativeGlide_ScriptParse("h,,dl", codes, &count, why, (int)sizeof(why)), 0);
	GLIDE_EQ("case 22 'dl,' result", NativeGlide_ScriptParse("dl,", codes, &count, why, (int)sizeof(why)), 0);
	GLIDE_EQ("case 22 too long a word result", NativeGlide_ScriptParse("toolongword", codes, &count, why, (int)sizeof(why)), 0);
	GLIDE_EQ("case 22 NULL codes ok", NativeGlide_ScriptParse("h,sl", NULL, &count, why, (int)sizeof(why)), 1);
	GLIDE_EQ("case 22 NULL codes count", count, 2);
	for (n = 32; n <= 33; n++)
	{
		int len = 0;

		for (i = 0; i < n; i++)
		{
			if (i > 0)
			{
				big[len++] = ',';
			}
			big[len++] = 'h';
		}
		big[len] = '\0';
		GLIDE_EQ(GlideSt_Tag("case 22 list of %d 'h' result [%d]", n, n), NativeGlide_ScriptParse(big, codes, &count, why, (int)sizeof(why)),
		         (n == 32) ? 1 : 0);
	}
	GLIDE_EQ("case 22 ManName(3) is DL", strcmp(NativeGlide_ManName(3), "DL"), 0);
	GLIDE_EQ("case 22 ManName(-1) is -", strcmp(NativeGlide_ManName(-1), "-"), 0);
	GLIDE_EQ("case 22 ManName(10) is -", strcmp(NativeGlide_ManName(10), "-"), 0);
}

internal void GlideSt_TestScriptPlayer(void)
{
	NativeGlideScript p;
	NativeGlideScriptOut o;
	int codes[1];
	int i;
	const unsigned int sideL = RAW_BTN_LEFT;

	// Case 23: DL (the code is NATIVE_GLIDE_MAN_DIVE_L, the name "DL").
	codes[0] = NATIVE_GLIDE_MAN_DIVE_L;
	GlideSt_ScriptInit(&p, codes, 1);
	o = GlideSt_ToStart(&p, "case 23 DL start");
	GLIDE_EQ("case 23 start event", o.event, 1);
	GLIDE_EQU("case 23 start press", o.press, RAW_BTN_R1);
	for (i = 0; i < 2; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQU(GlideSt_Tag("case 23 hop wait tick %d press [i %d]", i + 1, i), o.press, RAW_BTN_R1);
		GLIDE_EQ(GlideSt_Tag("case 23 hop wait tick %d event [i %d]", i + 1, i), o.event, 0);
	}
	o = GlideSt_SS(&p, 1, 1, 0);
	GLIDE_EQ("case 23 air tick 1 event", o.event, 2);
	GLIDE_EQU("case 23 air tick 1 press", o.press, RAW_BTN_R1);
	o = GlideSt_SS(&p, 1, 1, 0);
	GLIDE_EQ("case 23 air tick 2 event", o.event, 0);
	GLIDE_EQU("case 23 air tick 2 press", o.press, RAW_BTN_R1);
	GLIDE_EQU("case 23 air tick 2 release", o.release, 0);
	for (i = 3; i <= 10; i++)
	{
		o = GlideSt_SS(&p, 1, 1, 0);
		GLIDE_EQU(GlideSt_Tag("case 23 air tick %d press [i %d]", i, i), o.press, RAW_BTN_R3 | RAW_BTN_R1 | RAW_BTN_LEFT | RAW_BTN_UP);
		GLIDE_EQU(GlideSt_Tag("case 23 air tick %d release [i %d]", i, i), o.release, RAW_BTN_RIGHT | RAW_BTN_DOWN);
		GLIDE_EQ(GlideSt_Tag("case 23 air tick %d event [i %d]", i, i), o.event, 0);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 23 landing event", o.event, 3);
	GLIDE_EQU("case 23 landing press", o.press, RAW_BTN_R1 | sideL);
	for (i = 0; i < 19; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQU(GlideSt_Tag("case 23 after tick %d press [i %d]", i + 2, i), o.press, RAW_BTN_R1 | sideL);
		GLIDE_EQ(GlideSt_Tag("case 23 after tick %d event [i %d]", i + 2, i), o.event, 0);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 23 end event", o.event, 4);
	GLIDE_EQU("case 23 end press", o.press, 0);
	o = GlideSt_ToStart(&p, "case 23 59 quiet ticks after the end");
	GLIDE_EQ("case 23 next start event (the 60th tick)", o.event, 1);

	// Case 24: h.
	codes[0] = NATIVE_GLIDE_MAN_H;
	GlideSt_ScriptInit(&p, codes, 1);
	o = GlideSt_ToStart(&p, "case 24 h start");
	GLIDE_EQU("case 24 start press", o.press, RAW_BTN_R1);
	o = GlideSt_Air(&p, 1);
	GLIDE_EQU("case 24 air tick 1 press", o.press, RAW_BTN_R1);
	o = GlideSt_Air(&p, 1);
	GLIDE_EQU("case 24 air tick 2 press", o.press, RAW_BTN_R1);
	for (i = 3; i <= 6; i++)
	{
		o = GlideSt_Air(&p, 1);
		GLIDE_EQU(GlideSt_Tag("case 24 air tick %d press [i %d]", i, i), o.press, RAW_BTN_R3);
		GLIDE_EQU(GlideSt_Tag("case 24 air tick %d release [i %d]", i, i), o.release, 0);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQU("case 24 landing press", o.press, 0);
	GLIDE_EQ("case 24 landing event", o.event, 3);
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQU("case 24 after press", o.press, 0);

	// Case 25: sl.
	codes[0] = NATIVE_GLIDE_MAN_SL;
	GlideSt_ScriptInit(&p, codes, 1);
	(void)GlideSt_ToStart(&p, "case 25 sl start");
	(void)GlideSt_Air(&p, 2);
	o = GlideSt_Air(&p, 1);
	GLIDE_EQU("case 25 air tick 3 press", o.press, RAW_BTN_R3 | RAW_BTN_LEFT);
	GLIDE_EQU("case 25 air tick 3 release", o.release, RAW_BTN_RIGHT);
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 25 landing event", o.event, 3);
	GLIDE_EQU("case 25 landing press", o.press, RAW_BTN_LEFT);
	for (i = 2; i <= 20; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQU(GlideSt_Tag("case 25 after tick %d press [i %d]", i, i), o.press, RAW_BTN_LEFT);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 25 end event", o.event, 4);
	GLIDE_EQU("case 25 end press", o.press, 0);

	// Case 26: pu.
	codes[0] = NATIVE_GLIDE_MAN_PU;
	GlideSt_ScriptInit(&p, codes, 1);
	(void)GlideSt_ToStart(&p, "case 26 pu start");
	(void)GlideSt_Air(&p, 2);
	o = GlideSt_Air(&p, 1);
	GLIDE_EQU("case 26 air tick 3 press", o.press, RAW_BTN_R3 | RAW_BTN_DOWN);
	GLIDE_EQU("case 26 air tick 3 release", o.release, RAW_BTN_UP);

	// Case 27: ol.
	codes[0] = NATIVE_GLIDE_MAN_OL;
	GlideSt_ScriptInit(&p, codes, 1);
	o = GlideSt_ToStart(&p, "case 27 ol start");
	GLIDE_EQ("case 27 start event", o.event, 1);
	GLIDE_EQU("case 27 start press", o.press, RAW_BTN_LEFT);
	GLIDE_EQU("case 27 start release", o.release, RAW_BTN_RIGHT);
	for (i = 1; i <= 45; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQU(GlideSt_Tag("case 27 off tick %d press [i %d]", i, i), o.press, RAW_BTN_LEFT);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 27 gave up event", o.event, 5);
	GLIDE_EQU("case 27 gave up press", o.press, 0);
	// Second run: into the air at the 10th tick after the start.
	GlideSt_ScriptInit(&p, codes, 1);
	(void)GlideSt_ToStart(&p, "case 27 ol 2nd start");
	for (i = 1; i <= 9; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
	}
	o = GlideSt_SS(&p, 1, 1, 0);
	GLIDE_EQ("case 27 flight event", o.event, 2);
	GLIDE_EQU("case 27 air tick 1 press", o.press, RAW_BTN_LEFT);
	o = GlideSt_Air(&p, 1);
	GLIDE_EQU("case 27 air tick 2 press", o.press, RAW_BTN_LEFT);
	o = GlideSt_Air(&p, 1);
	GLIDE_EQU("case 27 air tick 3 press", o.press, RAW_BTN_R3 | RAW_BTN_LEFT);

	// Case 28: the hop gives up.
	codes[0] = NATIVE_GLIDE_MAN_H;
	GlideSt_ScriptInit(&p, codes, 1);
	(void)GlideSt_ToStart(&p, "case 28 h start");
	for (i = 1; i <= 15; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQU(GlideSt_Tag("case 28 hop tick %d press [i %d]", i, i), o.press, RAW_BTN_R1);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 28 give-up event", o.event, 5);
	GLIDE_EQU("case 28 give-up press", o.press, 0);

	// Case 29: grabbed in flight.
	codes[0] = NATIVE_GLIDE_MAN_DIVE_L;
	GlideSt_ScriptInit(&p, codes, 1);
	(void)GlideSt_ToStart(&p, "case 29 DL start");
	(void)GlideSt_Air(&p, 5);
	o = GlideSt_SS(&p, 1, 1, 1);
	GLIDE_EQ("case 29 grabbed event", o.event, 4);
	GLIDE_EQU("case 29 grabbed press", o.press, 0);
	GLIDE_EQ("case 29 grabbed phase", p.phase, NATIVE_GLIDE_PH_WAIT);
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQU("case 29 next tick press", o.press, 0);
	GLIDE_EQ("case 29 next tick counts 1", p.ticks, 1);
	for (i = 2; i < NATIVE_GLIDE_SCRIPT_GROUND_TICKS; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQ(GlideSt_Tag("case 29 wait tick %d event [i %d]", i, i), o.event, 0);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 29 start 60 ticks after the grab", o.event, 1);

	// Case 30: a natural flight.
	codes[0] = NATIVE_GLIDE_MAN_H;
	GlideSt_ScriptInit(&p, codes, 1);
	for (i = 0; i < 10; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
	}
	for (i = 1; i <= 2; i++)
	{
		o = GlideSt_SS(&p, 1, 1, 0);
		GLIDE_EQU(GlideSt_Tag("case 30 air tick %d press [i %d]", i, i), o.press, 0);
		GLIDE_EQ(GlideSt_Tag("case 30 air tick %d event [i %d]", i, i), o.event, 0);
	}
	for (i = 3; i <= 5; i++)
	{
		o = GlideSt_SS(&p, 1, 1, 0);
		GLIDE_EQU(GlideSt_Tag("case 30 air tick %d press [i %d]", i, i), o.press, RAW_BTN_R3);
		GLIDE_EQ(GlideSt_Tag("case 30 air tick %d event [i %d]", i, i), o.event, 0);
	}
	GLIDE_EQ("case 30 phase in the flight", p.phase, NATIVE_GLIDE_PH_NATURAL);
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQU("case 30 landing press", o.press, 0);
	GLIDE_EQ("case 30 manoeuvres used", p.started, 0);
	for (i = 1; i < NATIVE_GLIDE_SCRIPT_GROUND_TICKS; i++)
	{
		o = GlideSt_SS(&p, 1, 0, 0);
		GLIDE_EQ(GlideSt_Tag("case 30 ground tick %d after the landing event [i %d]", i, i), o.event, 0);
	}
	o = GlideSt_SS(&p, 1, 0, 0);
	GLIDE_EQ("case 30 start 60 ticks after the landing", o.event, 1);

	// Case 31: racing 0 in the middle of a flight.
	GlideSt_ScriptInit(&p, codes, 1);
	(void)GlideSt_ToStart(&p, "case 31 h start");
	(void)GlideSt_Air(&p, 5);
	o = GlideSt_SS(&p, 0, 1, 0);
	GLIDE_EQU("case 31 racing 0 press", o.press, 0);
	GLIDE_EQU("case 31 racing 0 release", o.release, 0);
	GLIDE_EQ("case 31 racing 0 phase", p.phase, NATIVE_GLIDE_PH_WAIT);
	GLIDE_EQ("case 31 racing 0 event (the manoeuvre ended)", o.event, 4);
}

int NativeGlide_SelfTest(void)
{
	s_glideStChecks = 0;
	s_glideStFails = 0;

	// The hooks with the ability off hand their value back untouched.
	GLIDE_EQ("ability is off", NativeGlide_Ability(), 0);
	GLIDE_EQ("Gravity(NULL, -900) with the ability off", NativeGlide_Gravity(NULL, -900), -900);
	GLIDE_EQ("LandingBoost(NULL, 5, 37) with the ability off", NativeGlide_LandingBoost(NULL, 5, 37), 37);

	GlideSt_TestBasics();
	GlideSt_TestOpenAndFlight();
	GlideSt_TestClosing();
	GlideSt_TestTunablesAndRuns();
	GlideSt_TestScriptParse();
	GlideSt_TestScriptPlayer();

	printf("glide selftest %s: %d checks, %d failures\n", (s_glideStFails == 0) ? "passed" : "FAILED", s_glideStChecks, s_glideStFails);
	return (s_glideStFails == 0) ? 0 : 1;
}
