// ===========================================================================
// FREE FLY: the unit test of its pure parts (ctest fly_selftest). Included by
// main.c after native_fly.c (unity build). Everything here runs on local
// structs. The scope gate's rule is case 2 (NativeFly_ScopeRule); reading the
// driver for it (NativeFly_InScope) needs a race. Every expected number is written out from the
// rules in include/platform/native_fly.h, never computed by the code under test.
// ===========================================================================

global_variable int s_flyStChecks;
global_variable int s_flyStFails;

#define FLY_EXPECT(cond, ...) \
	do \
	{ \
		s_flyStChecks++; \
		if (!(cond)) \
		{ \
			s_flyStFails++; \
			printf("fly selftest: "); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

// Integer compare: the tag names the case, the message gives actual and wanted.
#define FLY_EQ(tag, got, want) \
	do \
	{ \
		const int g_ = (int)(got); \
		const int w_ = (int)(want); \
		FLY_EXPECT(g_ == w_, "%s: got %d, wanted %d", (tag), g_, w_); \
	} while (0)

// A tag with up to two numbers in it (a small ring of buffers).
internal const char *FlySt_Tag(const char *fmt, int a, int b)
{
	static char bufs[8][96];
	static int next = 0;
	char *buf = bufs[next];

	next = (next + 1) & 7;
	snprintf(buf, 96, fmt, a, b);
	return buf;
}

// A tag made of a case name and a field name.
internal const char *FlySt_TagS(const char *a, const char *b)
{
	static char bufs[8][128];
	static int next = 0;
	char *buf = bufs[next];

	next = (next + 1) & 7;
	snprintf(buf, 128, "%s %s", a, b);
	return buf;
}

// ---------------------------------------------------------------------------
// The state machine's rig
// ---------------------------------------------------------------------------

typedef struct
{
	NativeFlyTune tune;
	NativeFlyState st;
} FlyStRig;

internal void FlySt_RigInit(FlyStRig *r)
{
	NativeFly_TuneDefaults(&r->tune);
	NativeFly_StateReset(&r->st);
}

internal NativeFlyOut FlySt_Tick(FlyStRig *r, int eligible, int onGround, int r1Held)
{
	NativeFlyIn in;
	NativeFlyOut out;

	in.eligible = eligible;
	in.onGround = onGround;
	in.r1Held = r1Held;
	memset(&out, 0, sizeof(out));
	NativeFly_Step(&r->st, &r->tune, &in, &out);
	return out;
}

// state, event, flying, climbing (4 checks), and pressed (a 5th) when wantPressed >= 0.
internal void FlySt_Chk(const char *tag, const FlyStRig *r, const NativeFlyOut *o, int state, int event, int wantPressed, int flying, int climbing)
{
	FLY_EQ(FlySt_TagS(tag, "state"), r->st.state, state);
	FLY_EQ(FlySt_TagS(tag, "event"), o->event, event);
	FLY_EQ(FlySt_TagS(tag, "flying"), o->flying, flying);
	FLY_EQ(FlySt_TagS(tag, "climbing"), o->climbing, climbing);
	if (wantPressed >= 0)
	{
		FLY_EQ(FlySt_TagS(tag, "pressed"), o->pressed, wantPressed);
	}
}

// A flight in HOLD with the last press long ago (sinceTap 16 at the end):
// the take-off on the ground, one climbing air tick, 15 released air ticks.
internal void FlySt_ToHold(FlyStRig *r)
{
	int i;

	FlySt_RigInit(r);
	(void)FlySt_Tick(r, 1, 1, 1);
	(void)FlySt_Tick(r, 1, 0, 1);
	for (i = 0; i < 15; i++)
	{
		(void)FlySt_Tick(r, 1, 0, 0);
	}
}

// ---------------------------------------------------------------------------
// Case 1: the tune
// ---------------------------------------------------------------------------

internal void FlySt_TestTune(void)
{
	NativeFlyTune t;

	NativeFly_TuneDefaults(&t);
	FLY_EQ("case 1 default climbRate", t.climbRate, 25);
	FLY_EQ("case 1 default doubleTap", t.doubleTap, 10);
	FLY_EQ("case 1 default band", t.band, 1500);
	FLY_EQ("case 1 default margin", t.margin, 500);

	// Below the minimum.
	t.climbRate = 0;
	t.doubleTap = 0;
	t.band = 0;
	t.margin = -50;
	NativeFly_TuneClamp(&t);
	FLY_EQ("case 1 clamp climbRate below", t.climbRate, 5);
	FLY_EQ("case 1 clamp doubleTap below", t.doubleTap, 2);
	FLY_EQ("case 1 clamp band below", t.band, 100);
	FLY_EQ("case 1 clamp margin below", t.margin, 0);

	// Above the maximum.
	t.climbRate = 1000;
	t.doubleTap = 1000;
	t.band = 99999;
	t.margin = 99999;
	NativeFly_TuneClamp(&t);
	FLY_EQ("case 1 clamp climbRate above", t.climbRate, 80);
	FLY_EQ("case 1 clamp doubleTap above", t.doubleTap, 30);
	FLY_EQ("case 1 clamp band above", t.band, 1850);
	FLY_EQ("case 1 clamp margin above", t.margin, 2000);

	// Inside: untouched.
	t.climbRate = 40;
	t.doubleTap = 7;
	t.band = 900;
	t.margin = 300;
	NativeFly_TuneClamp(&t);
	FLY_EQ("case 1 clamp climbRate inside", t.climbRate, 40);
	FLY_EQ("case 1 clamp doubleTap inside", t.doubleTap, 7);
	FLY_EQ("case 1 clamp band inside", t.band, 900);
	FLY_EQ("case 1 clamp margin inside", t.margin, 300);

	// Exactly on the limits: untouched.
	t.climbRate = 5;
	t.doubleTap = 30;
	t.band = 1850;
	t.margin = 0;
	NativeFly_TuneClamp(&t);
	FLY_EQ("case 1 clamp climbRate at min", t.climbRate, 5);
	FLY_EQ("case 1 clamp doubleTap at max", t.doubleTap, 30);
	FLY_EQ("case 1 clamp band at max", t.band, 1850);
	FLY_EQ("case 1 clamp margin at min", t.margin, 0);
}

// ---------------------------------------------------------------------------
// Case 2: the scope rule (mod, seat 0, human, hover or plane)
// ---------------------------------------------------------------------------

internal void FlySt_TestScope(void)
{
	FLY_EQ("case 2 hover seat 0 human mod on", NativeFly_ScopeRule(1, 0, 0, 1), 1);
	FLY_EQ("case 2 plane seat 0 human mod on", NativeFly_ScopeRule(1, 0, 0, 2), 1);
	FLY_EQ("case 2 kart class", NativeFly_ScopeRule(1, 0, 0, 0), 0);
	FLY_EQ("case 2 reserved class 3 (read as kart)", NativeFly_ScopeRule(1, 0, 0, 3), 0);
	FLY_EQ("case 2 mod off", NativeFly_ScopeRule(0, 0, 0, 1), 0);
	FLY_EQ("case 2 seat 1", NativeFly_ScopeRule(1, 1, 0, 1), 0);
	FLY_EQ("case 2 bot on seat 0", NativeFly_ScopeRule(1, 0, 1, 2), 0);
}

// ---------------------------------------------------------------------------
// Case 3: the state machine
// ---------------------------------------------------------------------------

internal void FlySt_TestStateMachine(void)
{
	FlyStRig r;
	NativeFlyOut o;
	int i;
	int bad;

	// 3a: R1 never pressed, on the ground and in the air: stays GROUND.
	FlySt_RigInit(&r);
	bad = -1;
	for (i = 0; i < 50; i++)
	{
		o = FlySt_Tick(&r, 1, i & 1, 0);
		if ((bad < 0) && ((r.st.state != NATIVE_FLY_GROUND) || (o.event != NATIVE_FLY_EV_NONE) || (o.flying != 0) || (o.climbing != 0)))
		{
			bad = i;
		}
	}
	FLY_EXPECT(bad < 0, "case 3a never pressed: tick %d left GROUND or gave an event (state %d event %d flying %d)", bad, r.st.state, o.event, o.flying);

	// 3b: take-off, climb, hold, climb again (no double tap, sinceTap > 10).
	FlySt_RigInit(&r);
	o = FlySt_Tick(&r, 1, 1, 1);
	FlySt_Chk("case 3b take-off", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_TAKEOFF, 1, 1, 1);
	for (i = 0; i < 5; i++)
	{
		o = FlySt_Tick(&r, 1, 0, 1);
		FlySt_Chk(FlySt_Tag("case 3b held in the air %d [i %d]", i + 1, i), &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 0, 1, 1);
	}
	o = FlySt_Tick(&r, 1, 0, 0);
	FlySt_Chk("case 3b released", &r, &o, NATIVE_FLY_HOLD, NATIVE_FLY_EV_NONE, 0, 1, 0);
	for (i = 0; i < 19; i++)
	{
		o = FlySt_Tick(&r, 1, 0, 0);
	}
	FlySt_Chk("case 3b still holding", &r, &o, NATIVE_FLY_HOLD, NATIVE_FLY_EV_NONE, 0, 1, 0);
	// sinceTap is 25 here (take-off tick 0, this is tick 25); the press at tick 26.
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3b held again after 20 ticks", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 1, 1, 1);

	// 3c: the double tap. Press at t, release t+1..t+9, press at t+10:
	// sinceTap is 10 at that step (<= doubleTap 10) -> FALL.
	FlySt_ToHold(&r);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3c press at t", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 1, 1, 1);
	for (i = 0; i < 9; i++)
	{
		o = FlySt_Tick(&r, 1, 0, 0);
	}
	FlySt_Chk("case 3c released t+9", &r, &o, NATIVE_FLY_HOLD, NATIVE_FLY_EV_NONE, 0, 1, 0);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3c press at t+10 (double tap)", &r, &o, NATIVE_FLY_FALL, NATIVE_FLY_EV_DOUBLE_TAP, 1, 0, 0);
	// Press at t, release t+1..t+10, press at t+11: sinceTap 11 -> no double tap.
	FlySt_ToHold(&r);
	(void)FlySt_Tick(&r, 1, 0, 1);
	for (i = 0; i < 10; i++)
	{
		o = FlySt_Tick(&r, 1, 0, 0);
	}
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3c press at t+11 (no double tap)", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 1, 1, 1);

	// 3d: the take-off press counts as the first tap. Take-off at tick 0,
	// release tick 1 (air), press tick 2: sinceTap 2 -> FALL.
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	o = FlySt_Tick(&r, 1, 0, 0);
	FlySt_Chk("case 3d released after take-off", &r, &o, NATIVE_FLY_HOLD, NATIVE_FLY_EV_NONE, 0, 1, 0);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3d second press", &r, &o, NATIVE_FLY_FALL, NATIVE_FLY_EV_DOUBLE_TAP, 1, 0, 0);

	// 3e: FALL (continues 3d): presses do nothing, the ground ends it, a new press takes off.
	o = FlySt_Tick(&r, 1, 0, 0);
	FlySt_Chk("case 3e fall released", &r, &o, NATIVE_FLY_FALL, NATIVE_FLY_EV_NONE, 0, 0, 0);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3e fall press", &r, &o, NATIVE_FLY_FALL, NATIVE_FLY_EV_NONE, 1, 0, 0);
	o = FlySt_Tick(&r, 1, 0, 0);
	o = FlySt_Tick(&r, 1, 1, 0);
	FlySt_Chk("case 3e ground after air", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_LAND, 0, 0, 0);
	o = FlySt_Tick(&r, 1, 1, 1);
	FlySt_Chk("case 3e new take-off", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_TAKEOFF, 1, 1, 1);

	// 3f: landing from CLIMB and from HOLD (airSeen).
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	(void)FlySt_Tick(&r, 1, 0, 1);
	o = FlySt_Tick(&r, 1, 1, 0);
	FlySt_Chk("case 3f landing from CLIMB", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_LAND, 0, 0, 0);
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	(void)FlySt_Tick(&r, 1, 0, 1);
	o = FlySt_Tick(&r, 1, 0, 0);
	FLY_EQ("case 3f before landing from HOLD state", r.st.state, NATIVE_FLY_HOLD);
	o = FlySt_Tick(&r, 1, 1, 0);
	FlySt_Chk("case 3f landing from HOLD", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_LAND, 0, 0, 0);

	// 3g: the take-off tick and the next tick on the ground, R1 held: no landing.
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	o = FlySt_Tick(&r, 1, 1, 1);
	FlySt_Chk("case 3g still on the ground, held", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 0, 1, 1);
	FLY_EQ("case 3g airSeen 0", r.st.airSeen, 0);
	o = FlySt_Tick(&r, 1, 1, 1);
	FlySt_Chk("case 3g still on the ground, held 2", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 0, 1, 1);
	o = FlySt_Tick(&r, 1, 1, 0);
	FlySt_Chk("case 3g released on the ground (abort)", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_ABORT, 0, 0, 0);

	// 3h: not eligible.
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	(void)FlySt_Tick(&r, 1, 0, 1);
	o = FlySt_Tick(&r, 0, 0, 1);
	FlySt_Chk("case 3h not eligible in CLIMB", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_END, -1, 0, 0);
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	(void)FlySt_Tick(&r, 1, 0, 1);
	(void)FlySt_Tick(&r, 1, 0, 0);
	o = FlySt_Tick(&r, 0, 0, 0);
	FlySt_Chk("case 3h not eligible in HOLD", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_END, -1, 0, 0);
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 1, 1);
	(void)FlySt_Tick(&r, 1, 0, 0);
	(void)FlySt_Tick(&r, 1, 0, 1);
	FLY_EQ("case 3h FALL reached", r.st.state, NATIVE_FLY_FALL);
	o = FlySt_Tick(&r, 0, 0, 0);
	FlySt_Chk("case 3h not eligible in FALL", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_END, -1, 0, 0);
	FlySt_RigInit(&r);
	o = FlySt_Tick(&r, 0, 1, 0);
	FlySt_Chk("case 3h not eligible on GROUND", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_NONE, -1, 0, 0);
	o = FlySt_Tick(&r, 0, 1, 1);
	FlySt_Chk("case 3h press while not eligible", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_NONE, -1, 0, 0);

	// 3i: a press in the air without a flight does not take off.
	FlySt_RigInit(&r);
	(void)FlySt_Tick(&r, 1, 0, 0);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3i press in the air", &r, &o, NATIVE_FLY_GROUND, NATIVE_FLY_EV_NONE, 1, 0, 0);

	// 3j: doubleTap 3. Press at t, press at t+3 (sinceTap 3) ends the flight;
	// press at t+4 (sinceTap 4) does not.
	FlySt_ToHold(&r);
	r.tune.doubleTap = 3;
	(void)FlySt_Tick(&r, 1, 0, 1);
	(void)FlySt_Tick(&r, 1, 0, 0);
	(void)FlySt_Tick(&r, 1, 0, 0);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3j presses 3 apart", &r, &o, NATIVE_FLY_FALL, NATIVE_FLY_EV_DOUBLE_TAP, 1, 0, 0);
	FlySt_ToHold(&r);
	r.tune.doubleTap = 3;
	(void)FlySt_Tick(&r, 1, 0, 1);
	(void)FlySt_Tick(&r, 1, 0, 0);
	(void)FlySt_Tick(&r, 1, 0, 0);
	(void)FlySt_Tick(&r, 1, 0, 0);
	o = FlySt_Tick(&r, 1, 0, 1);
	FlySt_Chk("case 3j presses 4 apart", &r, &o, NATIVE_FLY_CLIMB, NATIVE_FLY_EV_NONE, 1, 1, 1);
}

// ---------------------------------------------------------------------------
// Case 4-7: ceiling, ground note, velocity, highest of
// ---------------------------------------------------------------------------

internal void FlySt_TestCeiling(void)
{
	FLY_EQ("case 4 Ceiling(100, 1500, 3540)", NativeFly_Ceiling(100, 1500, 3540), 1600);
	FLY_EQ("case 4 Ceiling(3000, 1500, 3540)", NativeFly_Ceiling(3000, 1500, 3540), 3540);
	FLY_EQ("case 4 Ceiling(31000, 1500, 40000)", NativeFly_Ceiling(31000, 1500, 40000), 32000);
	FLY_EQ("case 4 Cap(3040, 500)", NativeFly_Cap(3040, 500), 3540);
	FLY_EQ("case 4 Cap(31800, 500)", NativeFly_Cap(31800, 500), 32000);
}

internal void FlySt_TestGroundNote(void)
{
	NativeFlyGround g;

	memset(&g, 0, sizeof(g));

	// A miss with nothing valid yet: the kart's own Y. The header does not say
	// what valid is after that, so only groundY is checked.
	NativeFly_GroundNote(&g, 0, 123, 700);
	FLY_EQ("case 5 miss with no ground yet: kart Y", g.groundY, 700);

	NativeFly_GroundNote(&g, 1, 200, 650);
	FLY_EQ("case 5 hit groundY", g.groundY, 200);
	FLY_EQ("case 5 hit valid", g.valid, 1);

	NativeFly_GroundNote(&g, 0, 555, 900);
	FLY_EQ("case 5 miss keeps the last ground", g.groundY, 200);
	FLY_EQ("case 5 miss keeps valid", g.valid, 1);

	NativeFly_GroundNote(&g, 1, 50, 900);
	FLY_EQ("case 5 next hit", g.groundY, 50);
}

internal void FlySt_TestVelocity(void)
{
	int capped;
	int v;
	const int ceil = 1500 * 256; // 384000

	// 25 WE per tick = 6400.
	capped = -1;
	v = NativeFly_VelocityY(1, 25, 0, ceil, 32, &capped);
	FLY_EQ("case 6 climbing far below: v", v, 6400);
	FLY_EQ("case 6 climbing far below: capped", capped, 0);

	capped = -1;
	v = NativeFly_VelocityY(0, 25, 0, ceil, 32, &capped);
	FLY_EQ("case 6 holding below: v", v, 0);
	FLY_EQ("case 6 holding below: capped", capped, 0);

	// 10 WE room: 2560.
	capped = -1;
	v = NativeFly_VelocityY(1, 25, ceil - 10 * 256, ceil, 32, &capped);
	FLY_EQ("case 6 climbing 10 WE below: v", v, 2560);
	FLY_EQ("case 6 climbing 10 WE below: capped", capped, 1);

	capped = -1;
	v = NativeFly_VelocityY(1, 25, ceil, ceil, 32, &capped);
	FLY_EQ("case 6 climbing at the ceiling: v", v, 0);
	FLY_EQ("case 6 climbing at the ceiling: capped", capped, 1);

	// 100 WE above: 25600 down, at most 20480 per tick.
	capped = -1;
	v = NativeFly_VelocityY(0, 25, ceil + 25600, ceil, 32, &capped);
	FLY_EQ("case 6 holding 100 WE above: v", v, -20480);
	FLY_EQ("case 6 holding 100 WE above: capped", capped, 1);

	// 30 WE above: 7680 down.
	capped = -1;
	v = NativeFly_VelocityY(0, 25, ceil + 7680, ceil, 32, &capped);
	FLY_EQ("case 6 holding 30 WE above: v", v, -7680);
	FLY_EQ("case 6 holding 30 WE above: capped", capped, 1);

	capped = -1;
	v = NativeFly_VelocityY(1, 25, ceil + 7680, ceil, 32, &capped);
	FLY_EQ("case 6 climbing 30 WE above: v", v, -7680);
	FLY_EQ("case 6 climbing 30 WE above: capped", capped, 1);

	// ms 16: the rate stays a velocity per 32 ms; the room is covered in half the time.
	capped = -1;
	v = NativeFly_VelocityY(1, 25, 0, ceil, 16, &capped);
	FLY_EQ("case 6 ms 16 far below: v", v, 6400);
	FLY_EQ("case 6 ms 16 far below: capped", capped, 0);

	// READING: room 10 WE = 2560; at 16 ms the kart moves v*16/32 per tick, so
	// the largest v that reaches the ceiling is 2560 * 32 / 16 = 5120.
	capped = -1;
	v = NativeFly_VelocityY(1, 25, ceil - 10 * 256, ceil, 16, &capped);
	FLY_EQ("case 6 ms 16 with 10 WE room: v", v, 5120);
	FLY_EQ("case 6 ms 16 with 10 WE room: capped", capped, 1);
}

internal void FlySt_TestHighestOf(void)
{
	// Quad 0: GROUND, max 300 at the LAST vertex (all 9 count).
	// Quad 1: NO_COLLISION_RESPONSE (0x10), max 900 -> not track.
	// Quad 2: KILL_PLANE (0x200), max 800 -> not track.
	// Quad 3: COLLISION_SURFACE only (0x2000), max 5000 -> not counted (no GROUND).
	static const unsigned short flags[4] = {0x1000, 0x1000 | 0x10, 0x1000 | 0x200, 0x2000};
	static const short ys[36] = {
		-10, 20, 50, 100, 150, 200, 250, 280, 300, // quad 0
		0, 0, 0, 0, 900, 0, 0, 0, 0,              // quad 1
		0, 800, 0, 0, 0, 0, 0, 0, 0,              // quad 2
		5000, 0, 0, 0, 0, 0, 0, 0, 0              // quad 3
	};
	static const unsigned short negFlags[1] = {0x1000};
	static const short negYs[9] = {-300, -200, -100, -90, -80, -70, -60, -50, -40};
	static const unsigned short noneFlags[2] = {0x1000 | 0x10, 0x2000};
	static const short noneYs[18] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
	int highest;
	int n;

	highest = -12345;
	n = NativeFly_HighestOf(4, flags, ys, &highest);
	FLY_EQ("case 7 four quads: result", n, 1);
	FLY_EQ("case 7 four quads: highest", highest, 300);

	highest = 1234;
	n = NativeFly_HighestOf(0, flags, ys, &highest);
	FLY_EQ("case 7 count 0: result", n, 0);
	FLY_EQ("case 7 count 0: highest untouched", highest, 1234);

	highest = -12345;
	n = NativeFly_HighestOf(1, negFlags, negYs, &highest);
	FLY_EQ("case 7 negative Ys: result", n, 1);
	FLY_EQ("case 7 negative Ys: highest", highest, -40);

	highest = 777;
	n = NativeFly_HighestOf(2, noneFlags, noneYs, &highest);
	FLY_EQ("case 7 no track quad: result", n, 0);
	FLY_EQ("case 7 no track quad: highest untouched", highest, 777);
}

// ---------------------------------------------------------------------------
// Case 8: script parse
// ---------------------------------------------------------------------------

internal void FlySt_TestScriptParse(void)
{
	int codes[NATIVE_FLY_SCRIPT_MAX];
	int args[NATIVE_FLY_SCRIPT_MAX];
	int count;
	char why[200];
	char big[160];
	int n;
	int i;
	int len;
	static const char *const bad[8] = {"", "x", "c", "c0", "c1000", "h,,d", "d,", "c20x"};

	why[0] = '\0';
	count = 0;
	n = NativeFly_ScriptParse("c20,h60,d,l", codes, args, &count, why, (int)sizeof(why));
	FLY_EQ("case 8 'c20,h60,d,l' result", n, 1);
	FLY_EQ("case 8 'c20,h60,d,l' count", count, 4);
	FLY_EQ("case 8 code 0 CLIMB", codes[0], NATIVE_FLY_MAN_CLIMB);
	FLY_EQ("case 8 code 1 HOLD", codes[1], NATIVE_FLY_MAN_HOLD);
	FLY_EQ("case 8 code 2 DOUBLE", codes[2], NATIVE_FLY_MAN_DOUBLE);
	FLY_EQ("case 8 code 3 LAND", codes[3], NATIVE_FLY_MAN_LAND);
	FLY_EQ("case 8 arg 0", args[0], 20);
	FLY_EQ("case 8 arg 1", args[1], 60);
	FLY_EQ("case 8 arg 2", args[2], 0);
	FLY_EQ("case 8 arg 3", args[3], 0);

	for (i = 0; i < 8; i++)
	{
		why[0] = '\0';
		FLY_EQ(FlySt_Tag("case 8 reject [row %d] result [%d]", i, i), NativeFly_ScriptParse(bad[i], codes, args, &count, why, (int)sizeof(why)), 0);
		FLY_EXPECT(why[0] != '\0', "case 8 reject '%s': why is empty", bad[i]);
	}

	// Limits of n: 1 and 999 are accepted.
	n = NativeFly_ScriptParse("c1,h999", codes, args, &count, why, (int)sizeof(why));
	FLY_EQ("case 8 'c1,h999' result", n, 1);
	FLY_EQ("case 8 'c1,h999' arg 0", args[0], 1);
	FLY_EQ("case 8 'c1,h999' arg 1", args[1], 999);

	// 32 manoeuvres pass, 33 do not.
	for (n = 32; n <= 33; n++)
	{
		len = 0;
		for (i = 0; i < n; i++)
		{
			if (i > 0)
			{
				big[len++] = ',';
			}
			big[len++] = 'd';
		}
		big[len] = '\0';
		why[0] = '\0';
		FLY_EQ(FlySt_Tag("case 8 list of %d 'd' result [%d]", n, n), NativeFly_ScriptParse(big, codes, args, &count, why, (int)sizeof(why)),
		       (n == 32) ? 1 : 0);
		if (n == 32)
		{
			FLY_EQ("case 8 list of 32 count", count, 32);
		}
		else
		{
			FLY_EXPECT(why[0] != '\0', "case 8 list of 33: why is empty");
		}
	}

	// NULL codes/args with a valid list.
	count = 0;
	FLY_EQ("case 8 NULL codes/args result", NativeFly_ScriptParse("c5,d,l", NULL, NULL, &count, why, (int)sizeof(why)), 1);
	FLY_EQ("case 8 NULL codes/args count", count, 3);
}

// ---------------------------------------------------------------------------
// Case 9: script step
// ---------------------------------------------------------------------------

internal void FlySt_ScriptInit(NativeFlyScript *p, const int *codes, const int *args, int n)
{
	int i;

	memset(p, 0, sizeof(*p));
	for (i = 0; i < n; i++)
	{
		p->codes[i] = codes[i];
		p->args[i] = args[i];
	}
	p->count = n;
	NativeFly_ScriptReset(p);
}

internal NativeFlyScriptOut FlySt_SS(NativeFlyScript *p, int racing, int onGround, int grabbed)
{
	NativeFlyScriptIn in;
	NativeFlyScriptOut out;

	in.racing = racing;
	in.onGround = onGround;
	in.grabbed = grabbed;
	memset(&out, 0, sizeof(out));
	NativeFly_ScriptStep(p, &in, &out);
	return out;
}

// 59 quiet ground ticks (one check), then the starting tick (returned).
internal NativeFlyScriptOut FlySt_ToStart(NativeFlyScript *p, const char *tag)
{
	NativeFlyScriptOut o;
	int i;
	int bad = -1;

	for (i = 1; i < NATIVE_FLY_SCRIPT_GROUND_TICKS; i++)
	{
		o = FlySt_SS(p, 1, 1, 0);
		if ((bad < 0) && ((o.r1 != 0) || (o.event != 0)))
		{
			bad = i;
		}
	}
	FLY_EXPECT(bad < 0, "%s: ground tick %d before the start was not quiet", tag, bad);
	return FlySt_SS(p, 1, 1, 0);
}

internal void FlySt_TestScriptStep(void)
{
	NativeFlyScript p;
	NativeFlyScriptOut o;
	int i;
	int bad;
	static const int codes4[4] = {NATIVE_FLY_MAN_CLIMB, NATIVE_FLY_MAN_HOLD, NATIVE_FLY_MAN_DOUBLE, NATIVE_FLY_MAN_LAND};
	static const int args4[4] = {3, 2, 0, 0};
	static const int codesCL[2] = {NATIVE_FLY_MAN_CLIMB, NATIVE_FLY_MAN_LAND};
	static const int argsCL[2] = {1, 0};
	static const int dblWant[5] = {0, 1, 0, 1, 0};

	// 9a: the whole list "c3,h2,d,l". Ticks counted from the first ground tick:
	// 1..59 quiet, 60 start (list started, c3 begins: R1 held), 61-62 c3 goes on,
	// 63-64 h2 (63 starts it), 65-69 the double tap (R1 0,1,0,1,0), 70 'l' starts,
	// the ground arrives at tick 73.
	FlySt_ScriptInit(&p, codes4, args4, 4);
	o = FlySt_ToStart(&p, "case 9a start");
	// READING: the start tick reports event 1 (list started), not 2, and its R1 is the first tick of c3.
	FLY_EQ("case 9a start event", o.event, 1);
	FLY_EQ("case 9a start r1", o.r1, 1);
	FLY_EQ("case 9a start phase", p.phase, NATIVE_FLY_PH_RUN);
	FLY_EQ("case 9a start rounds", p.rounds, 1);
	for (i = 2; i <= 3; i++)
	{
		o = FlySt_SS(&p, 1, 0, 0);
		FLY_EQ(FlySt_Tag("case 9a c3 tick %d r1 [i %d]", i, i), o.r1, 1);
		FLY_EQ(FlySt_Tag("case 9a c3 tick %d event [i %d]", i, i), o.event, 0);
	}
	o = FlySt_SS(&p, 1, 0, 0);
	FLY_EQ("case 9a h2 start r1", o.r1, 0);
	FLY_EQ("case 9a h2 start event", o.event, 2);
	o = FlySt_SS(&p, 1, 0, 0);
	FLY_EQ("case 9a h2 tick 2 r1", o.r1, 0);
	FLY_EQ("case 9a h2 tick 2 event", o.event, 0);
	for (i = 0; i < 5; i++)
	{
		o = FlySt_SS(&p, 1, 0, 0);
		FLY_EQ(FlySt_Tag("case 9a double tap tick %d r1 [i %d]", i + 1, i), o.r1, dblWant[i]);
		FLY_EQ(FlySt_Tag("case 9a double tap tick %d event [i %d]", i + 1, i), o.event, (i == 0) ? 2 : 0);
	}
	o = FlySt_SS(&p, 1, 0, 0);
	FLY_EQ("case 9a land start r1", o.r1, 0);
	FLY_EQ("case 9a land start event", o.event, 2);
	for (i = 0; i < 2; i++)
	{
		o = FlySt_SS(&p, 1, 0, 0);
		FLY_EQ(FlySt_Tag("case 9a land air tick %d r1 [i %d]", i + 1, i), o.r1, 0);
		FLY_EQ(FlySt_Tag("case 9a land air tick %d event [i %d]", i + 1, i), o.event, 0);
	}
	o = FlySt_SS(&p, 1, 1, 0);
	FLY_EQ("case 9a landed event", o.event, 3);
	FLY_EQ("case 9a landed r1", o.r1, 0);
	FLY_EQ("case 9a landed phase", p.phase, NATIVE_FLY_PH_WAIT);

	// READING: the landing tick is not counted; 59 quiet ground ticks follow and
	// the 60th starts the list again (as after 'LandingTick' in the glide test).
	bad = -1;
	for (i = 1; i < NATIVE_FLY_SCRIPT_GROUND_TICKS; i++)
	{
		o = FlySt_SS(&p, 1, 1, 0);
		if ((bad < 0) && ((o.r1 != 0) || (o.event != 0)))
		{
			bad = i;
		}
	}
	FLY_EXPECT(bad < 0, "case 9a ground tick %d after the landing was not quiet", bad);
	o = FlySt_SS(&p, 1, 1, 0);
	FLY_EQ("case 9a second start event", o.event, 1);
	FLY_EQ("case 9a second start r1", o.r1, 1);
	FLY_EQ("case 9a second start rounds", p.rounds, 2);

	// 9b: racing 0 / grabbed 1 while running: event 5, WAIT, R1 off.
	FlySt_ScriptInit(&p, codes4, args4, 4);
	(void)FlySt_ToStart(&p, "case 9b racing start");
	o = FlySt_SS(&p, 1, 0, 0);
	FLY_EQ("case 9b running r1", o.r1, 1);
	o = FlySt_SS(&p, 0, 0, 0);
	FLY_EQ("case 9b racing 0 event", o.event, 5);
	FLY_EQ("case 9b racing 0 r1", o.r1, 0);
	FLY_EQ("case 9b racing 0 phase", p.phase, NATIVE_FLY_PH_WAIT);

	FlySt_ScriptInit(&p, codes4, args4, 4);
	(void)FlySt_ToStart(&p, "case 9b grabbed start");
	o = FlySt_SS(&p, 1, 0, 1);
	FLY_EQ("case 9b grabbed event", o.event, 5);
	FLY_EQ("case 9b grabbed r1", o.r1, 0);
	FLY_EQ("case 9b grabbed phase", p.phase, NATIVE_FLY_PH_WAIT);

	// READING: racing 0 while only waiting is a plain reset with no event.
	FlySt_ScriptInit(&p, codes4, args4, 4);
	(void)FlySt_SS(&p, 1, 1, 0);
	o = FlySt_SS(&p, 0, 1, 0);
	FLY_EQ("case 9b racing 0 while waiting: event", o.event, 0);
	FLY_EQ("case 9b racing 0 while waiting: r1", o.r1, 0);
	FLY_EQ("case 9b racing 0 while waiting: phase", p.phase, NATIVE_FLY_PH_WAIT);

	// 9c: 'l' that never lands, list "c1,l". Tick 60 starts (c1), tick 61 starts 'l'.
	// READING: 'l' released ticks count from its start tick (that is 1); 600 of
	// them are allowed (ticks 61..660), the 601st (tick 661) gives up with event 4.
	FlySt_ScriptInit(&p, codesCL, argsCL, 2);
	o = FlySt_ToStart(&p, "case 9c start");
	FLY_EQ("case 9c start event", o.event, 1);
	FLY_EQ("case 9c c1 r1", o.r1, 1);
	o = FlySt_SS(&p, 1, 0, 0);
	FLY_EQ("case 9c 'l' start event", o.event, 2);
	FLY_EQ("case 9c 'l' start r1", o.r1, 0);
	bad = -1;
	for (i = 1; i < NATIVE_FLY_SCRIPT_LAND_MAX; i++)
	{
		o = FlySt_SS(&p, 1, 0, 0);
		if ((bad < 0) && ((o.r1 != 0) || (o.event != 0)))
		{
			bad = i;
		}
	}
	FLY_EXPECT(bad < 0, "case 9c 'l' tick %d of the wait was not quiet", bad);
	o = FlySt_SS(&p, 1, 0, 0);
	FLY_EQ("case 9c 'l' gave up event", o.event, 4);
	FLY_EQ("case 9c 'l' gave up r1", o.r1, 0);
	FLY_EQ("case 9c 'l' gave up phase", p.phase, NATIVE_FLY_PH_WAIT);
}

int NativeFly_SelfTest(void)
{
	s_flyStChecks = 0;
	s_flyStFails = 0;

	FlySt_TestTune();
	FlySt_TestScope();
	FlySt_TestStateMachine();
	FlySt_TestCeiling();
	FlySt_TestGroundNote();
	FlySt_TestVelocity();
	FlySt_TestHighestOf();
	FlySt_TestScriptParse();
	FlySt_TestScriptStep();

	printf("fly selftest %s: %d checks, %d failures\n", (s_flyStFails == 0) ? "passed" : "FAILED", s_flyStChecks, s_flyStFails);
	return (s_flyStFails == 0) ? 0 : 1;
}
