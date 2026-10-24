#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ENGINE HIRES SELFTEST (animation half) - checks of the pose maths in
// include/rldhires_anim.inc (round of 2026-10-17).
//
// Included in the unity build after cgltf.h, include/platform/engine.h,
// rldhires_anim.inc and engine_hires_testglb.c. Every expected number below is
// written down as a closed form (sin / cos of a known angle, a hand-worked
// matrix product); the code under test is never used to compute an expectation.
// The test GLB is made in memory by EngHiresTest_Glb; no file is touched.
//
// Entry point: EngHires_SelfTestAnim(&checks) returns the number of failures.

#define ANIM_CHECK(cond, what)                                              \
	do                                                                      \
	{                                                                       \
		(*checks)++;                                                        \
		if (!(cond))                                                        \
		{                                                                   \
			fails++;                                                        \
			printf("engine selftest: hires anim %s FAILED\n", (what));      \
		}                                                                   \
	} while (0)

#define ANIM_PI 3.14159265358979323846

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_Near(double a, double b, double tol)
{
	return fabs(a - b) <= tol; // false for NaN
}

internal void EngHiresAnimT_SetQ(float q[4], double x, double y, double z, double w)
{
	q[0] = (float)x;
	q[1] = (float)y;
	q[2] = (float)z;
	q[3] = (float)w;
}

// Unit quaternion of a rotation by deg degrees about the axis (ax, ay, az).
internal void EngHiresAnimT_AxisQ(float q[4], double ax, double ay, double az, double deg)
{
	const double len = sqrt(ax * ax + ay * ay + az * az);
	const double h = deg * ANIM_PI / 180.0 * 0.5;
	const double s = sin(h) / len;

	EngHiresAnimT_SetQ(q, ax * s, ay * s, az * s, cos(h));
}

internal int EngHiresAnimT_NearV(const float *a, const double *e, int n, double tol)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if (!EngHiresAnimT_Near((double)a[i], e[i], tol))
		{
			return 0;
		}
	}
	return 1;
}

internal int EngHiresAnimT_NearQ(const float a[4], double x, double y, double z, double w, double tol)
{
	const double e[4] = {x, y, z, w};

	return EngHiresAnimT_NearV(a, e, 4, tol);
}

// Two quaternions equal up to sign (q and -q are the same rotation).
internal int EngHiresAnimT_NearQSign(const float a[4], const float b[4], double tol)
{
	double e[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		e[i] = (double)b[i];
	}
	if (EngHiresAnimT_NearV(a, e, 4, tol))
	{
		return 1;
	}
	for (i = 0; i < 4; i++)
	{
		e[i] = -(double)b[i];
	}
	return EngHiresAnimT_NearV(a, e, 4, tol);
}

internal double EngHiresAnimT_QLen(const float q[4])
{
	return sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3]);
}

// p' = M * (p, 1), column-major M.
internal void EngHiresAnimT_Point(const float m[16], double x, double y, double z, double out[3])
{
	int r;

	for (r = 0; r < 3; r++)
	{
		out[r] = (double)m[0 * 4 + r] * x + (double)m[1 * 4 + r] * y + (double)m[2 * 4 + r] * z + (double)m[3 * 4 + r];
	}
}

internal int EngHiresAnimT_PointIs(const float m[16], double x, double y, double z, double ex, double ey, double ez, double tol)
{
	double o[3];

	EngHiresAnimT_Point(m, x, y, z, o);
	return EngHiresAnimT_Near(o[0], ex, tol) && EngHiresAnimT_Near(o[1], ey, tol) && EngHiresAnimT_Near(o[2], ez, tol);
}

internal int EngHiresAnimT_MatNear(const float a[16], const float b[16], double tol)
{
	int i;

	for (i = 0; i < 16; i++)
	{
		if (!EngHiresAnimT_Near((double)a[i], (double)b[i], tol))
		{
			return 0;
		}
	}
	return 1;
}

internal void EngHiresAnimT_MakeCh(RldAnimChannel *ch, u8 path, u8 interp, u32 keys, const float *times, const float *values)
{
	memset(ch, 0, sizeof(*ch));
	ch->node = 0;
	ch->path = path;
	ch->interp = interp;
	ch->keys = keys;
	ch->times = times;
	ch->values = values;
}

// ---------------------------------------------------------------------------
// Case 1: quaternions
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_CaseQuats(int *checks)
{
	int fails = 0;
	float id[4];
	float b[4];
	float nb[4];
	float o[4];
	float o2[4];
	float near1[4];
	float z[4];
	const double s45 = sin(ANIM_PI / 4.0);
	const double c45 = cos(ANIM_PI / 4.0);
	const double s225 = sin(ANIM_PI / 8.0); // 22.5 degrees
	const double c225 = cos(ANIM_PI / 8.0);

	EngHiresAnimT_SetQ(id, 0, 0, 0, 1);
	EngHiresAnimT_AxisQ(b, 0, 0, 1, 90.0);
	EngHiresAnimT_SetQ(nb, -(double)b[0], -(double)b[1], -(double)b[2], -(double)b[3]);

	// slerp identity -> 90 deg Z at u = 0.5 is 45 deg Z: (0, 0, sin 22.5, cos 22.5)
	RldAnim_QuatSlerp(id, b, 0.5f, o);
	ANIM_CHECK(EngHiresAnimT_NearQ(o, 0, 0, s225, c225, 1e-6), "quat slerp 0.5 is 45 deg Z");
	// the ends
	RldAnim_QuatSlerp(id, b, 0.0f, o);
	ANIM_CHECK(EngHiresAnimT_NearQ(o, 0, 0, 0, 1, 1e-6), "quat slerp u=0 is a");
	RldAnim_QuatSlerp(id, b, 1.0f, o);
	ANIM_CHECK(EngHiresAnimT_NearQ(o, 0, 0, s45, c45, 1e-6), "quat slerp u=1 is b");
	// shorter arc: -b is the same rotation as b, so the same result
	RldAnim_QuatSlerp(id, b, 0.5f, o);
	RldAnim_QuatSlerp(id, nb, 0.5f, o2);
	ANIM_CHECK(EngHiresAnimT_NearV(o2, (const double[4]){(double)o[0], (double)o[1], (double)o[2], (double)o[3]}, 4, 1e-6), "quat slerp shorter arc (-b equals b)");
	RldAnim_QuatSlerp(id, nb, 1.0f, o2);
	ANIM_CHECK(EngHiresAnimT_NearQ(o2, 0, 0, s45, c45, 1e-6), "quat slerp u=1 on -b ends at b (hemisphere of a)");

	// nlerp 0.5: (0, 0, 0.5 sin45, 0.5 (1 + cos45)) / length
	{
		const double zz = 0.5 * s45;
		const double ww = 0.5 * (1.0 + c45);
		const double len = sqrt(zz * zz + ww * ww);

		RldAnim_QuatNlerp(id, b, 0.5f, o);
		ANIM_CHECK(EngHiresAnimT_NearQ(o, 0, 0, zz / len, ww / len, 1e-6), "quat nlerp 0.5 closed form");
		ANIM_CHECK(EngHiresAnimT_Near(EngHiresAnimT_QLen(o), 1.0, 1e-6), "quat nlerp unit length");
	}

	// nearly equal quaternions: finite and unit
	EngHiresAnimT_SetQ(near1, 0, 0, 1e-5, 1);
	RldAnim_QuatNormalize(near1);
	RldAnim_QuatSlerp(id, near1, 0.5f, o);
	ANIM_CHECK(EngHiresAnimT_Near(o[0], 0.0, 1e-9) && EngHiresAnimT_Near(o[1], 0.0, 1e-9) && EngHiresAnimT_Near(o[2], 0.5e-5, 1e-7) &&
	               EngHiresAnimT_Near(o[3], 1.0, 1e-6),
	           "quat slerp of nearly equal quaternions");
	ANIM_CHECK(EngHiresAnimT_Near(EngHiresAnimT_QLen(o), 1.0, 1e-6), "quat slerp of nearly equal is unit");

	// zero quaternion normalises to identity
	z[0] = z[1] = z[2] = z[3] = 0.0f;
	RldAnim_QuatNormalize(z);
	ANIM_CHECK(EngHiresAnimT_NearQ(z, 0, 0, 0, 1, 0.0), "quat normalize of zero is identity");
	return fails;
}

// ---------------------------------------------------------------------------
// Case 2: channel sampling
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_CaseChannel(int *checks)
{
	static const float times[3] = {0.0f, 1.0f, 3.0f};
	static const float vals[9] = {0, 0, 0, 2, 0, 0, 2, 4, 0};
	static const float jt[4] = {0.0f, 1.0f, 1.0f, 2.0f};
	static const float jv[12] = {0, 0, 0, 1, 0, 0, 5, 0, 0, 9, 0, 0};
	static const float one[3] = {7, 8, 9};
	static const float oneT[1] = {2.0f};
	int fails = 0;
	RldAnimChannel ch;
	float o[4];
	float rt[2];
	float rv[8];
	const double s225 = sin(ANIM_PI / 8.0);
	const double c225 = cos(ANIM_PI / 8.0);
	int pass;

	// LINEAR (pass 0) and CUBIC (pass 1: only the kept values, read linearly) agree
	for (pass = 0; pass < 2; pass++)
	{
		const u8 interp = (pass == 0) ? RLDANIM_INTERP_LINEAR : RLDANIM_INTERP_CUBIC;
		const char *tag = (pass == 0) ? "linear" : "cubic";
		char what[96];

		EngHiresAnimT_MakeCh(&ch, RLDANIM_PATH_TRANSLATION, interp, 3, times, vals);
		RldAnim_SampleChannel(&ch, -1.0f, o);
		snprintf(what, sizeof(what), "channel %s before first key", tag);
		ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){0, 0, 0}, 3, 0.0), what);
		RldAnim_SampleChannel(&ch, 0.5f, o);
		snprintf(what, sizeof(what), "channel %s t=0.5 is (1,0,0)", tag);
		ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){1, 0, 0}, 3, 1e-6), what);
		// t = 2 between keys 1 and 2: u = (2-1)/(3-1) = 0.5 -> (2, 2, 0)
		RldAnim_SampleChannel(&ch, 2.0f, o);
		snprintf(what, sizeof(what), "channel %s t=2 is (2,2,0)", tag);
		ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){2, 2, 0}, 3, 1e-6), what);
		RldAnim_SampleChannel(&ch, 3.0f, o);
		snprintf(what, sizeof(what), "channel %s t=3 is last", tag);
		ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){2, 4, 0}, 3, 0.0), what);
		RldAnim_SampleChannel(&ch, 5.0f, o);
		snprintf(what, sizeof(what), "channel %s t=5 is last", tag);
		ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){2, 4, 0}, 3, 0.0), what);
	}

	// STEP: the value of the key at or before t
	EngHiresAnimT_MakeCh(&ch, RLDANIM_PATH_TRANSLATION, RLDANIM_INTERP_STEP, 3, times, vals);
	RldAnim_SampleChannel(&ch, 0.99f, o);
	ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){0, 0, 0}, 3, 0.0), "channel step t=0.99 holds key 0");
	RldAnim_SampleChannel(&ch, 1.0f, o);
	ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){2, 0, 0}, 3, 0.0), "channel step t=1 holds key 1");
	RldAnim_SampleChannel(&ch, 2.9f, o);
	ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){2, 0, 0}, 3, 0.0), "channel step t=2.9 holds key 1");

	// rotation LINEAR: identity -> 90 deg X at t = 0.5 of [0, 1] is 45 deg X (slerp)
	rt[0] = 0.0f;
	rt[1] = 1.0f;
	EngHiresAnimT_SetQ(&rv[0], 0, 0, 0, 1);
	EngHiresAnimT_AxisQ(&rv[4], 1, 0, 0, 90.0);
	EngHiresAnimT_MakeCh(&ch, RLDANIM_PATH_ROTATION, RLDANIM_INTERP_LINEAR, 2, rt, rv);
	RldAnim_SampleChannel(&ch, 0.5f, o);
	ANIM_CHECK(EngHiresAnimT_NearQ(o, s225, 0, 0, c225, 1e-6), "channel rotation t=0.5 is 45 deg X");

	// one key: that key everywhere
	EngHiresAnimT_MakeCh(&ch, RLDANIM_PATH_TRANSLATION, RLDANIM_INTERP_LINEAR, 1, oneT, one);
	RldAnim_SampleChannel(&ch, -4.0f, o);
	ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){7, 8, 9}, 3, 0.0), "channel one key before");
	RldAnim_SampleChannel(&ch, 2.0f, o);
	ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){7, 8, 9}, 3, 0.0), "channel one key at");
	RldAnim_SampleChannel(&ch, 50.0f, o);
	ANIM_CHECK(EngHiresAnimT_NearV(o, (const double[3]){7, 8, 9}, 3, 0.0), "channel one key after");

	// a jump: times {0, 1, 1, 2}, x values {0, 1, 5, 9}
	EngHiresAnimT_MakeCh(&ch, RLDANIM_PATH_TRANSLATION, RLDANIM_INTERP_LINEAR, 4, jt, jv);
	RldAnim_SampleChannel(&ch, 0.5f, o); // between key 0 (x 0) and key 1 (x 1): 0.5
	ANIM_CHECK(EngHiresAnimT_Near(o[0], 0.5, 1e-6), "channel jump t=0.5 between key 0 and 1");
	RldAnim_SampleChannel(&ch, 1.5f, o); // between key 2 (x 5) and key 3 (x 9): 7
	ANIM_CHECK(EngHiresAnimT_Near(o[0], 7.0, 1e-6), "channel jump t=1.5 between key 2 and 3");
	RldAnim_SampleChannel(&ch, 1.0f, o); // the value of one of the two keys with time 1
	ANIM_CHECK(EngHiresAnimT_Near(o[0], 1.0, 1e-6) || EngHiresAnimT_Near(o[0], 5.0, 1e-6), "channel jump t=1 is a key with time 1");
	return fails;
}

// ---------------------------------------------------------------------------
// Case 3: clip time
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_CaseClipTime(int *checks)
{
	int fails = 0;
	RldAnimClip clip;
	RldAnimClip flat;

	memset(&clip, 0, sizeof(clip));
	clip.start = 0.5f;
	clip.end = 2.5f; // length 2
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipLength(&clip), 2.0, 0.0), "cliptime length 2");
	// LOOP: local = fmod(t, 2) (+ 2 when negative), result = 0.5 + local
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, 0.0f, RLDANIM_PLAY_LOOP), 0.5, 1e-6), "cliptime loop at 0 is 0.5");
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, 2.25f, RLDANIM_PLAY_LOOP), 0.75, 1e-6), "cliptime loop at 2.25 is 0.75");
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, -0.5f, RLDANIM_PLAY_LOOP), 2.0, 1e-6), "cliptime loop at -0.5 is 2.0"); // fmod -0.5 + 2 = 1.5 -> 2.0
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, 4.0f, RLDANIM_PLAY_LOOP), 0.5, 1e-6), "cliptime loop at 4 is 0.5");
	// ONCE: clamped to [0, 2], result = 0.5 + local
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, -1.0f, RLDANIM_PLAY_ONCE), 0.5, 1e-6), "cliptime once at -1 is 0.5");
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, 1.0f, RLDANIM_PLAY_ONCE), 1.5, 1e-6), "cliptime once at 1 is 1.5");
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&clip, 9.0f, RLDANIM_PLAY_ONCE), 2.5, 1e-6), "cliptime once at 9 is 2.5");

	memset(&flat, 0, sizeof(flat));
	flat.start = 1.25f;
	flat.end = 1.25f;
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipLength(&flat), 0.0, 0.0), "cliptime flat length 0");
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&flat, 7.0f, RLDANIM_PLAY_LOOP), 1.25, 0.0), "cliptime flat loop is start");
	ANIM_CHECK(EngHiresAnimT_Near(RldAnim_ClipTime(&flat, -7.0f, RLDANIM_PLAY_ONCE), 1.25, 0.0), "cliptime flat once is start");
	return fails;
}

// ---------------------------------------------------------------------------
// Case 4: blend
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_CaseBlend(int *checks)
{
	int fails = 0;
	RldAnimTrs a[2];
	RldAnimTrs b[2];
	RldAnimTrs o[2];
	int i;
	int same;
	const double s225 = sin(ANIM_PI / 8.0);
	const double c225 = cos(ANIM_PI / 8.0);

	RldAnim_TrsIdentity(&a[0]);
	RldAnim_TrsIdentity(&a[1]);
	RldAnim_TrsIdentity(&b[0]);
	RldAnim_TrsIdentity(&b[1]);
	// a: node 0 t (1,2,3) s (1,1,1) identity; node 1 t 0 s (2,2,2) identity
	a[0].t[0] = 1;
	a[0].t[1] = 2;
	a[0].t[2] = 3;
	a[1].s[0] = a[1].s[1] = a[1].s[2] = 2.0f;
	// b: node 0 t (5,6,7) s (3,3,3) 90 deg Y; node 1 t (0,-4,0) s (4,4,4) 180 deg Y
	b[0].t[0] = 5;
	b[0].t[1] = 6;
	b[0].t[2] = 7;
	b[0].s[0] = b[0].s[1] = b[0].s[2] = 3.0f;
	EngHiresAnimT_AxisQ(b[0].r, 0, 1, 0, 90.0);
	b[1].t[1] = -4;
	b[1].s[0] = b[1].s[1] = b[1].s[2] = 4.0f;
	EngHiresAnimT_AxisQ(b[1].r, 0, 1, 0, 180.0);

	// w = 0 is a
	RldAnim_BlendPose(a, b, 0.0f, o, 2);
	same = 1;
	for (i = 0; i < 2; i++)
	{
		const double ea[3] = {a[i].t[0], a[i].t[1], a[i].t[2]};
		const double es[3] = {a[i].s[0], a[i].s[1], a[i].s[2]};

		same = same && EngHiresAnimT_NearV(o[i].t, ea, 3, 1e-6) && EngHiresAnimT_NearV(o[i].s, es, 3, 1e-6) &&
		       EngHiresAnimT_NearQ(o[i].r, a[i].r[0], a[i].r[1], a[i].r[2], a[i].r[3], 1e-6);
	}
	ANIM_CHECK(same, "blend w=0 equals a");

	// w = 1 is b
	RldAnim_BlendPose(a, b, 1.0f, o, 2);
	same = 1;
	for (i = 0; i < 2; i++)
	{
		const double eb[3] = {b[i].t[0], b[i].t[1], b[i].t[2]};
		const double es[3] = {b[i].s[0], b[i].s[1], b[i].s[2]};

		same = same && EngHiresAnimT_NearV(o[i].t, eb, 3, 1e-6) && EngHiresAnimT_NearV(o[i].s, es, 3, 1e-6) &&
		       EngHiresAnimT_NearQSign(o[i].r, b[i].r, 1e-6);
	}
	ANIM_CHECK(same, "blend w=1 equals b");

	// w = 0.25: node 0 t = 1 + 4 * 0.25 = 2, 2 + 4 * 0.25 = 3, 3 + 4 * 0.25 = 4; s = 1 + 2 * 0.25 = 1.5
	//           node 1 t y = 0 + (-4) * 0.25 = -1; s = 2 + 2 * 0.25 = 2.5
	RldAnim_BlendPose(a, b, 0.25f, o, 2);
	ANIM_CHECK(EngHiresAnimT_NearV(o[0].t, (const double[3]){2, 3, 4}, 3, 1e-6), "blend w=0.25 translation node 0");
	ANIM_CHECK(EngHiresAnimT_NearV(o[0].s, (const double[3]){1.5, 1.5, 1.5}, 3, 1e-6), "blend w=0.25 scale node 0");
	ANIM_CHECK(EngHiresAnimT_NearV(o[1].t, (const double[3]){0, -1, 0}, 3, 1e-6), "blend w=0.25 translation node 1");
	ANIM_CHECK(EngHiresAnimT_NearV(o[1].s, (const double[3]){2.5, 2.5, 2.5}, 3, 1e-6), "blend w=0.25 scale node 1");
	ANIM_CHECK(EngHiresAnimT_Near(EngHiresAnimT_QLen(o[0].r), 1.0, 1e-6) && EngHiresAnimT_Near(EngHiresAnimT_QLen(o[1].r), 1.0, 1e-6),
	           "blend w=0.25 rotation unit length");

	// rotation: nlerp of identity and 90 deg Y at 0.5 equals the slerp: 45 deg Y
	RldAnim_BlendPose(a, b, 0.5f, o, 2);
	ANIM_CHECK(EngHiresAnimT_NearQ(o[0].r, 0, s225, 0, c225, 1e-5), "blend w=0.5 rotation is 45 deg Y");
	ANIM_CHECK(EngHiresAnimT_Near(EngHiresAnimT_QLen(o[1].r), 1.0, 1e-6), "blend w=0.5 identity vs 180 deg Y unit length");

	// out may be a
	{
		RldAnimTrs c[2];

		c[0] = a[0];
		c[1] = a[1];
		RldAnim_BlendPose(c, b, 0.25f, c, 2);
		ANIM_CHECK(EngHiresAnimT_NearV(c[0].t, (const double[3]){2, 3, 4}, 3, 1e-6), "blend in place (out is a)");
	}
	return fails;
}

// ---------------------------------------------------------------------------
// Case 5: matrices
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_CaseMatrix(int *checks)
{
	int fails = 0;
	RldAnimTrs trs;
	float m[16];
	float ma[16];
	float mb[16];
	float mo[16];
	float expect[16];
	RldAnimTrs back;
	int i;

	// T(1,2,3) R(90 deg about Z) S(2,1,1). R maps x -> y and y -> -x.
	//   (1,0,0): S -> (2,0,0), R -> (0,2,0), T -> (1,4,3)
	//   (0,1,0): S -> (0,1,0), R -> (-1,0,0), T -> (0,2,3)
	RldAnim_TrsIdentity(&trs);
	trs.t[0] = 1;
	trs.t[1] = 2;
	trs.t[2] = 3;
	EngHiresAnimT_AxisQ(trs.r, 0, 0, 1, 90.0);
	trs.s[0] = 2;
	RldAnim_TrsMatrix(&trs, m);
	ANIM_CHECK(EngHiresAnimT_PointIs(m, 1, 0, 0, 1, 4, 3, 1e-6), "trs matrix maps (1,0,0) to (1,4,3)");
	ANIM_CHECK(EngHiresAnimT_PointIs(m, 0, 1, 0, 0, 2, 3, 1e-6), "trs matrix maps (0,1,0) to (0,2,3)");
	ANIM_CHECK(EngHiresAnimT_Near(m[12], 1.0, 0.0) && EngHiresAnimT_Near(m[13], 2.0, 0.0) && EngHiresAnimT_Near(m[14], 3.0, 0.0) &&
	               EngHiresAnimT_Near(m[15], 1.0, 0.0) && m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f,
	           "trs matrix column-major layout (translation in 12..14)");

	// T(1,0,0) * T(0,2,0) = T(1,2,0)
	RldAnim_TrsIdentity(&trs);
	trs.t[0] = 1;
	RldAnim_TrsMatrix(&trs, ma);
	trs.t[0] = 0;
	trs.t[1] = 2;
	RldAnim_TrsMatrix(&trs, mb);
	RldAnim_Mat4Mul(ma, mb, mo);
	RldAnim_Mat4Identity(expect);
	expect[12] = 1;
	expect[13] = 2;
	ANIM_CHECK(EngHiresAnimT_MatNear(mo, expect, 1e-6), "mat4 mul T(1,0,0) * T(0,2,0) = T(1,2,0)");
	// order matters: R(90 Z) * T(1,0,0) maps the origin to R(1,0,0) = (0,1,0)
	RldAnim_TrsIdentity(&trs);
	EngHiresAnimT_AxisQ(trs.r, 0, 0, 1, 90.0);
	RldAnim_TrsMatrix(&trs, ma);
	RldAnim_TrsIdentity(&trs);
	trs.t[0] = 1;
	RldAnim_TrsMatrix(&trs, mb);
	RldAnim_Mat4Mul(ma, mb, mo);
	ANIM_CHECK(EngHiresAnimT_PointIs(mo, 0, 0, 0, 0, 1, 0, 1e-6), "mat4 mul order R * T");

	// decompose: T(4,-5,6) R(30 deg about (1,2,3)) S(1.5,0.5,2)
	RldAnim_TrsIdentity(&trs);
	trs.t[0] = 4;
	trs.t[1] = -5;
	trs.t[2] = 6;
	EngHiresAnimT_AxisQ(trs.r, 1, 2, 3, 30.0);
	trs.s[0] = 1.5f;
	trs.s[1] = 0.5f;
	trs.s[2] = 2.0f;
	RldAnim_TrsMatrix(&trs, m);
	RldAnim_DecomposeMatrix(m, &back);
	ANIM_CHECK(EngHiresAnimT_NearV(back.t, (const double[3]){4, -5, 6}, 3, 1e-5), "decompose translation");
	ANIM_CHECK(EngHiresAnimT_NearV(back.s, (const double[3]){1.5, 0.5, 2.0}, 3, 1e-5), "decompose scale");
	ANIM_CHECK(EngHiresAnimT_NearQSign(back.r, trs.r, 1e-5), "decompose rotation (up to sign)");
	RldAnim_TrsMatrix(&back, mo);
	ANIM_CHECK(EngHiresAnimT_MatNear(m, mo, 1e-5), "decompose then recompose equals the matrix");

	// other rotation branches of the decomposition: 180 deg about X, Y, Z and a 120 deg turn about (1,1,1)
	{
		static const double axes[4][4] = {{1, 0, 0, 180}, {0, 1, 0, 180}, {0, 0, 1, 180}, {1, 1, 1, 120}};

		for (i = 0; i < 4; i++)
		{
			char what[96];

			RldAnim_TrsIdentity(&trs);
			EngHiresAnimT_AxisQ(trs.r, axes[i][0], axes[i][1], axes[i][2], axes[i][3]);
			trs.s[0] = 2.0f;
			trs.s[2] = 0.5f;
			RldAnim_TrsMatrix(&trs, m);
			RldAnim_DecomposeMatrix(m, &back);
			RldAnim_TrsMatrix(&back, mo);
			snprintf(what, sizeof(what), "decompose roundtrip rotation case %d", i);
			ANIM_CHECK(EngHiresAnimT_MatNear(m, mo, 1e-5), what);
		}
	}

	// negative determinant: S = (-1,1,1)
	RldAnim_TrsIdentity(&trs);
	trs.t[0] = 1;
	trs.t[1] = 2;
	trs.t[2] = 3;
	EngHiresAnimT_AxisQ(trs.r, 1, 2, 3, 30.0);
	trs.s[0] = -1.0f;
	RldAnim_TrsMatrix(&trs, m);
	RldAnim_DecomposeMatrix(m, &back);
	ANIM_CHECK(back.s[0] < 0.0f && EngHiresAnimT_Near(back.s[0], -1.0, 1e-5) && EngHiresAnimT_Near(back.s[1], 1.0, 1e-5) &&
	               EngHiresAnimT_Near(back.s[2], 1.0, 1e-5),
	           "decompose negative determinant gives s[0] < 0");
	RldAnim_TrsMatrix(&back, mo);
	ANIM_CHECK(EngHiresAnimT_MatNear(m, mo, 1e-5), "decompose negative determinant recomposes the matrix");
	return fails;
}

// ---------------------------------------------------------------------------
// Case 6: node matrices, joint matrices, palette, skinning (hand-made skeleton)
// ---------------------------------------------------------------------------

internal int EngHiresAnimT_CaseSkin(int *checks)
{
	int fails = 0;
	RldAnimSkeleton skel;
	RldAnimTrs pose[3];
	float world[3 * 16];
	float j0[16];
	float j1[16];
	float pal[24];
	float pal64[24];
	float pre[16];
	float outP[3];
	float outN[3];
	u8 joint[4];
	float weight[4];
	float p[3];
	float n[3];
	int i;

	ANIM_CHECK(RldAnim_SkeletonAlloc(&skel, 3, 2) == 1, "skin skeleton alloc");
	if (skel.block == NULL)
	{
		return fails + 1;
	}
	skel.parent[0] = -1;
	skel.parent[1] = 0;
	skel.parent[2] = 1;
	skel.order[0] = 0; // RldAnim_BuildOrder is not reachable here: parents first by hand
	skel.order[1] = 1;
	skel.order[2] = 2;
	skel.rest[1].t[1] = 1.0f;
	skel.rest[2].t[1] = 1.0f;
	EngHiresAnimT_AxisQ(skel.rest[2].r, 0, 0, 1, 90.0);
	skel.jointNode[0] = 1;
	skel.jointNode[1] = 2;
	skel.inverseBind[13] = -1.0f;      // joint 0: T(0,-1,0)
	skel.inverseBind[16 + 13] = -2.0f; // joint 1: T(0,-2,0)

	RldAnim_PoseRest(&skel, pose);
	RldAnim_NodeMatrices(&skel, pose, world);
	// world[2] = T(0,1,0) T(0,1,0) R90Z = T(0,2,0) R90Z: (0,0,0) -> (0,2,0); (1,0,0) -> R = (0,1,0) -> (0,3,0)
	ANIM_CHECK(EngHiresAnimT_PointIs(&world[2 * 16], 0, 0, 0, 0, 2, 0, 1e-6), "node matrix 2 maps origin to (0,2,0)");
	ANIM_CHECK(EngHiresAnimT_PointIs(&world[2 * 16], 1, 0, 0, 0, 3, 0, 1e-6), "node matrix 2 maps (1,0,0) to (0,3,0)");
	ANIM_CHECK(EngHiresAnimT_PointIs(&world[1 * 16], 0, 0, 0, 0, 1, 0, 1e-6), "node matrix 1 is T(0,1,0)");
	ANIM_CHECK(EngHiresAnimT_PointIs(&world[0 * 16], 5, 6, 7, 5, 6, 7, 1e-6), "node matrix 0 is identity");

	// J0 = T(0,1,0) * T(0,-1,0) = identity
	RldAnim_JointMatrix(&skel, world, 0, j0);
	RldAnim_Mat4Identity(pre);
	ANIM_CHECK(EngHiresAnimT_MatNear(j0, pre, 1e-6), "joint matrix 0 is identity");
	// J1 = T(0,2,0) R90Z T(0,-2,0): (0,2,0) -> (0,0,0) -> (0,0,0) -> (0,2,0); (1,2,0) -> (1,0,0) -> (0,1,0) -> (0,3,0)
	RldAnim_JointMatrix(&skel, world, 1, j1);
	ANIM_CHECK(EngHiresAnimT_PointIs(j1, 0, 2, 0, 0, 2, 0, 1e-6), "joint matrix 1 keeps (0,2,0)");
	ANIM_CHECK(EngHiresAnimT_PointIs(j1, 1, 2, 0, 0, 3, 0, 1e-6), "joint matrix 1 maps (1,2,0) to (0,3,0)");

	// palette without a pre matrix; entry 0 is the identity rows
	RldAnim_PaletteEntry(NULL, j0, &pal[0]);
	RldAnim_PaletteEntry(NULL, j1, &pal[12]);
	{
		static const float idRows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
		int same = 1;

		for (i = 0; i < 12; i++)
		{
			same = same && EngHiresAnimT_Near((double)pal[i], (double)idRows[i], 1e-6);
		}
		ANIM_CHECK(same, "palette entry 0 (no pre) is the identity rows");
	}
	// palette with pre = uniform scale 64
	RldAnim_Mat4Identity(pre);
	pre[0] = pre[5] = pre[10] = 64.0f;
	RldAnim_PaletteEntry(pre, j0, &pal64[0]);
	RldAnim_PaletteEntry(pre, j1, &pal64[12]);
	ANIM_CHECK(EngHiresAnimT_Near(pal64[0], 64.0, 1e-4) && EngHiresAnimT_Near(pal64[5], 64.0, 1e-4) && EngHiresAnimT_Near(pal64[10], 64.0, 1e-4) &&
	               EngHiresAnimT_Near(pal64[3], 0.0, 1e-6),
	           "palette entry 0 with pre 64 is the scale rows");

	// skin (1,2,0), joints (0,1), weights (0.5,0.5): 0.5 * (1,2,0) + 0.5 * (0,3,0) = (0.5, 2.5, 0)
	p[0] = 1;
	p[1] = 2;
	p[2] = 0;
	n[0] = 1;
	n[1] = 0;
	n[2] = 0;
	joint[0] = 0;
	joint[1] = 1;
	joint[2] = 0;
	joint[3] = 0;
	weight[0] = 0.5f;
	weight[1] = 0.5f;
	weight[2] = 0.0f;
	weight[3] = 0.0f;
	RldAnim_SkinVertex(pal, joint, weight, p, n, outP, outN);
	ANIM_CHECK(EngHiresAnimT_NearV(outP, (const double[3]){0.5, 2.5, 0}, 3, 1e-6), "skin position half-half");
	// normal: 0.5 * (1,0,0) + 0.5 * R(1,0,0) = (0.5, 0.5, 0) -> normalised (0.7071, 0.7071, 0)
	ANIM_CHECK(EngHiresAnimT_NearV(outN, (const double[3]){sqrt(0.5), sqrt(0.5), 0}, 3, 1e-6), "skin normal half-half");
	// with pre 64 the position is 64 times larger, the normal is normalised again
	RldAnim_SkinVertex(pal64, joint, weight, p, n, outP, outN);
	ANIM_CHECK(EngHiresAnimT_NearV(outP, (const double[3]){32, 160, 0}, 3, 1e-3), "skin position with pre 64");
	ANIM_CHECK(EngHiresAnimT_NearV(outN, (const double[3]){sqrt(0.5), sqrt(0.5), 0}, 3, 1e-6), "skin normal with pre 64 is normalised");
	// all weight on joint 1: (1,2,0) -> (0,3,0)
	weight[0] = 1.0f;
	weight[1] = 0.0f;
	joint[0] = 1;
	RldAnim_SkinVertex(pal, joint, weight, p, n, outP, outN);
	ANIM_CHECK(EngHiresAnimT_NearV(outP, (const double[3]){0, 3, 0}, 3, 1e-6), "skin all weight on joint 1");
	// zero-weight slots are skipped: joint index 200 with weight 0 is never read (the palette has 2 entries)
	joint[0] = 0;
	joint[1] = 200;
	joint[2] = 1;
	joint[3] = 200;
	weight[0] = 0.5f;
	weight[1] = 0.0f;
	weight[2] = 0.5f;
	weight[3] = 0.0f;
	RldAnim_SkinVertex(pal, joint, weight, p, n, outP, outN);
	ANIM_CHECK(EngHiresAnimT_NearV(outP, (const double[3]){0.5, 2.5, 0}, 3, 1e-6), "skin skips zero weights (joint 200 not read)");

	RldAnim_FreeSkeleton(&skel);
	ANIM_CHECK(skel.block == NULL && skel.nodeCount == 0, "skin skeleton freed");
	return fails;
}

// ---------------------------------------------------------------------------
// Case 7 / 8: the build half on the test GLB
// ---------------------------------------------------------------------------

// Builds the GLB of a description, parses it and loads its buffers. 1 = made
// (the caller does cgltf_free(*data) and free(*glb)); *glb is set even on 0.
internal int EngHiresAnimT_Parse(const EngHiresTestGlbDesc *d, u8 **glb, cgltf_options *opt, cgltf_data **gltf)
{
	size_t size = 0;

	*gltf = NULL;
	*glb = EngHiresTest_Glb(d, &size);
	if (*glb == NULL)
	{
		return 0;
	}
	memset(opt, 0, sizeof(*opt));
	opt->file.read = EngHiresTest_NoFileRead;
	if (cgltf_parse(opt, *glb, (cgltf_size)size, gltf) != cgltf_result_success)
	{
		*gltf = NULL;
		return 0;
	}
	if (cgltf_load_buffers(opt, *gltf, NULL) != cgltf_result_success)
	{
		cgltf_free(*gltf);
		*gltf = NULL;
		return 0;
	}
	return 1;
}

internal void EngHiresAnimT_FillDesc(EngHiresTestGlbDesc *d, int interpolation, u32 clipMask)
{
	memset(d, 0, sizeof(*d));
	d->skinned = 1;
	d->wheels = 4;
	d->clipMask = clipMask;
	d->interpolation = interpolation;
	d->texture = 0;
}

internal int EngHiresAnimT_CaseGltf(int *checks)
{
	int fails = 0;
	EngHiresTestGlbDesc d;
	cgltf_options optL;
	cgltf_options optS;
	cgltf_options optC;
	cgltf_data *dataL = NULL;
	cgltf_data *dataS = NULL;
	cgltf_data *dataC = NULL;
	u8 *glbL = NULL;
	u8 *glbS = NULL;
	u8 *glbC = NULL;
	RldAnimSkeleton skel;
	const char *why = "";
	int okL;
	int okS;
	int okC;
	u32 k;

	memset(&skel, 0, sizeof(skel));
	EngHiresAnimT_FillDesc(&d, 0, 0xfffu);
	okL = EngHiresAnimT_Parse(&d, &glbL, &optL, &dataL);
	ANIM_CHECK(okL, "gltf linear build+parse+load");
	EngHiresAnimT_FillDesc(&d, 1, 0xfffu);
	okS = EngHiresAnimT_Parse(&d, &glbS, &optS, &dataS);
	ANIM_CHECK(okS, "gltf step build+parse+load");
	EngHiresAnimT_FillDesc(&d, 2, 0xfffu);
	okC = EngHiresAnimT_Parse(&d, &glbC, &optC, &dataC);
	ANIM_CHECK(okC, "gltf cubic build+parse+load");

	if (okL)
	{
		const cgltf_accessor *pos;

		ANIM_CHECK(dataL->skins_count == 1 && dataL->animations_count == 12, "gltf 1 skin, 12 animations");
		if (dataL->skins_count == 1)
		{
			ANIM_CHECK(RldAnim_SkeletonFromGltf(dataL, &dataL->skins[0], &skel, &why) == 1, "gltf skeleton build");
		}
		if (skel.block != NULL)
		{
			ANIM_CHECK(skel.nodeCount == 8, "gltf skeleton 8 nodes");
			ANIM_CHECK(skel.jointCount == 2, "gltf skeleton 2 joints");
			ANIM_CHECK(skel.jointNode[0] == 1 && skel.jointNode[1] == 2, "gltf skeleton joint nodes {1,2}");
			ANIM_CHECK(skel.parent[2] == 1 && skel.parent[1] == 0 && skel.parent[0] == -1, "gltf skeleton parents");
			ANIM_CHECK(EngHiresAnimT_NearV(skel.rest[2].t, (const double[3]){0, 0.5, 0}, 3, 1e-9), "gltf skeleton bone rest translation");
			ANIM_CHECK(EngHiresAnimT_Near(skel.inverseBind[16 + 12], 0.0, 0.0) && EngHiresAnimT_Near(skel.inverseBind[16 + 13], -0.5, 0.0) &&
			               EngHiresAnimT_Near(skel.inverseBind[16 + 14], 0.0, 0.0) && EngHiresAnimT_Near(skel.inverseBind[16 + 15], 1.0, 0.0),
			           "gltf skeleton inverse bind 1 column 3 is (0,-0.5,0,1)");
			{
				float idm[16];

				RldAnim_Mat4Identity(idm);
				ANIM_CHECK(EngHiresAnimT_MatNear(&skel.inverseBind[0], idm, 0.0), "gltf skeleton inverse bind 0 is identity");
			}
			{
				// the order lists every node once with every parent before its children
				int seen[8] = {0, 0, 0, 0, 0, 0, 0, 0};
				int orderOk = 1;
				u32 i;

				for (i = 0; i < skel.nodeCount; i++)
				{
					const u32 nd = skel.order[i];

					if (nd >= 8u || seen[nd] || (skel.parent[nd] >= 0 && !seen[skel.parent[nd]]))
					{
						orderOk = 0;
						break;
					}
					seen[nd] = 1;
				}
				ANIM_CHECK(orderOk, "gltf skeleton order parents first");
			}
		}

		// every clip, three interpolation modes
		for (k = 0; k < 12 && skel.block != NULL && okS && okC; k++)
		{
			const double D = (k == 3) ? 0.5 : 1.0;
			const double A = (double)(k + 1) * 10.0 * ANIM_PI / 180.0; // the bone's end angle
			RldAnimClip cl;
			RldAnimClip cs;
			RldAnimClip cc;
			RldAnimTrs pose[8];
			RldAnimTrs poseB[8];
			u32 skipped = 99;
			char what[96];

			// ----- LINEAR -----
			snprintf(what, sizeof(what), "gltf clip %u linear build", k);
			ANIM_CHECK(RldAnim_ClipFromGltf(dataL, &dataL->animations[k], &cl, &skipped, &why) == 1, what);
			if (cl.block != NULL)
			{
				snprintf(what, sizeof(what), "gltf clip %u 2 channels, skipped 0", k);
				ANIM_CHECK(cl.channelCount == 2 && skipped == 0, what);
				snprintf(what, sizeof(what), "gltf clip %u start 0, end D", k);
				ANIM_CHECK(EngHiresAnimT_Near(cl.start, 0.0, 0.0) && EngHiresAnimT_Near(cl.end, D, 1e-9), what);
				// at t = D: bone rotation (0, 0, sin(A/2), cos(A/2))
				RldAnim_PoseRest(&skel, pose);
				RldAnim_SampleClip(&cl, (float)D, pose, 8);
				snprintf(what, sizeof(what), "gltf clip %u bone rotation at end", k);
				ANIM_CHECK(EngHiresAnimT_NearQ(pose[2].r, 0, 0, sin(A * 0.5), cos(A * 0.5), 1e-6), what);
				snprintf(what, sizeof(what), "gltf clip %u root back at 0 at end", k);
				ANIM_CHECK(EngHiresAnimT_NearV(pose[1].t, (const double[3]){0, 0, 0}, 3, 1e-6), what);
				snprintf(what, sizeof(what), "gltf clip %u untouched node keeps rest", k);
				ANIM_CHECK(EngHiresAnimT_NearV(pose[0].t, (const double[3]){0, 0, 0}, 3, 0.0) && EngHiresAnimT_Near(pose[0].r[3], 1.0, 0.0), what);
				// at t = D/2: root up by 0.01 * (k + 1), bone at the half angle (slerp: A/2 about Z, so sin(A/4), cos(A/4))
				RldAnim_PoseRest(&skel, pose);
				RldAnim_SampleClip(&cl, (float)(D * 0.5), pose, 8);
				snprintf(what, sizeof(what), "gltf clip %u root translation at D/2", k);
				ANIM_CHECK(EngHiresAnimT_NearV(pose[1].t, (const double[3]){0, 0.01 * (double)(k + 1), 0}, 3, 1e-6), what);
				snprintf(what, sizeof(what), "gltf clip %u bone half rotation at D/2", k);
				ANIM_CHECK(EngHiresAnimT_NearQ(pose[2].r, 0, 0, sin(A * 0.25), cos(A * 0.25), 1e-6), what);
				// a quarter of the clip, kept for the CUBICSPLINE comparison
				RldAnim_PoseRest(&skel, poseB);
				RldAnim_SampleClip(&cl, (float)(D * 0.25), poseB, 8);
			}

			// ----- STEP -----
			snprintf(what, sizeof(what), "gltf clip %u step build", k);
			ANIM_CHECK(RldAnim_ClipFromGltf(dataS, &dataS->animations[k], &cs, &skipped, &why) == 1, what);
			if (cs.block != NULL)
			{
				snprintf(what, sizeof(what), "gltf clip %u step interp flag", k);
				ANIM_CHECK(cs.channelCount == 2 && cs.channels[0].interp == RLDANIM_INTERP_STEP && cs.channels[1].interp == RLDANIM_INTERP_STEP, what);
				RldAnim_PoseRest(&skel, pose);
				RldAnim_SampleClip(&cs, (float)(D * 0.99), pose, 8);
				snprintf(what, sizeof(what), "gltf clip %u step bone identity at 0.99 D", k);
				ANIM_CHECK(EngHiresAnimT_NearQ(pose[2].r, 0, 0, 0, 1, 1e-6), what);
				// STEP holds key 1 of the root (D/2) from D/2 on: y = 0.01 * (k + 1)
				snprintf(what, sizeof(what), "gltf clip %u step root holds key 1 at 0.99 D", k);
				ANIM_CHECK(EngHiresAnimT_NearV(pose[1].t, (const double[3]){0, 0.01 * (double)(k + 1), 0}, 3, 1e-6), what);
				RldAnim_FreeClip(&cs);
			}

			// ----- CUBICSPLINE: the kept values, read linearly: same as LINEAR -----
			snprintf(what, sizeof(what), "gltf clip %u cubic build", k);
			ANIM_CHECK(RldAnim_ClipFromGltf(dataC, &dataC->animations[k], &cc, &skipped, &why) == 1, what);
			if (cc.block != NULL)
			{
				snprintf(what, sizeof(what), "gltf clip %u cubic interp flag", k);
				ANIM_CHECK(cc.channelCount == 2 && cc.channels[0].interp == RLDANIM_INTERP_CUBIC, what);
				RldAnim_PoseRest(&skel, pose);
				RldAnim_SampleClip(&cc, (float)(D * 0.25), pose, 8);
				snprintf(what, sizeof(what), "gltf clip %u cubic equals linear at D/4", k);
				ANIM_CHECK(cl.block != NULL && EngHiresAnimT_NearV(pose[2].r, (const double[4]){poseB[2].r[0], poseB[2].r[1], poseB[2].r[2], poseB[2].r[3]}, 4, 1e-6) &&
				               EngHiresAnimT_NearV(pose[1].t, (const double[3]){poseB[1].t[0], poseB[1].t[1], poseB[1].t[2]}, 3, 1e-6),
				           what);
				RldAnim_FreeClip(&cc);
			}
			if (cl.block != NULL)
			{
				RldAnim_FreeClip(&cl);
			}
		}

		// ----- the full chain: rest pose + "idle" at t = 1 -----
		if (skel.block != NULL)
		{
			RldAnimClip idle;
			RldAnimTrs pose[8];
			float world[8 * 16];
			float j0[16];
			float j1[16];
			float pal[24];
			float v[3];
			float nrm[3];
			float outP[3];
			float outN[3];
			u8 joint[4];
			float weight[4];
			u32 skipped = 0;
			const double a10 = 10.0 * ANIM_PI / 180.0;
			const double c10 = cos(a10);
			const double s10 = sin(a10);

			// the vertex 3 of the body as the file stores it
			pos = EngHiresTest_FindAttr(dataL, 0, cgltf_attribute_type_position);
			v[0] = v[1] = v[2] = -9.0f;
			ANIM_CHECK(pos != NULL && cgltf_accessor_read_float(pos, 3, v, 3) != 0 && EngHiresAnimT_NearV(v, (const double[3]){0.1, 0.5, 0}, 3, 1e-6),
			           "gltf body vertex 3 is (0.1,0.5,0)");
			nrm[0] = 0.0f;
			nrm[1] = 0.0f;
			nrm[2] = 1.0f;
			joint[0] = 0;
			joint[1] = 1;
			joint[2] = 0;
			joint[3] = 0;
			weight[0] = 0.5f;
			weight[1] = 0.5f;
			weight[2] = 0.0f;
			weight[3] = 0.0f;

			ANIM_CHECK(RldAnim_ClipFromGltf(dataL, &dataL->animations[0], &idle, &skipped, &why) == 1, "gltf chain idle clip build");
			if (idle.block != NULL)
			{
				RldAnim_PoseRest(&skel, pose);
				RldAnim_SampleClip(&idle, RldAnim_ClipTime(&idle, 1.0f, RLDANIM_PLAY_ONCE), pose, 8);
				RldAnim_NodeMatrices(&skel, pose, world);
				RldAnim_JointMatrix(&skel, world, 0, j0);
				RldAnim_JointMatrix(&skel, world, 1, j1);
				// root at (0,0,0); bone rotated 10 deg about Z around its origin (0,0.5,0).
				// Joint 0 (root, inverse bind identity) = identity: the point stays (0.1,0.5,0).
				// Joint 1 = T(0,0.5,0) R10 T(0,-0.5,0): (0.1,0.5,0) -> (0.1,0,0) -> (0.1 cos10, 0.1 sin10, 0) -> (0.1 cos10, 0.5 + 0.1 sin10, 0)
				ANIM_CHECK(EngHiresAnimT_PointIs(j0, 0.1, 0.5, 0, 0.1, 0.5, 0, 1e-6), "gltf chain joint 0 keeps the point");
				ANIM_CHECK(EngHiresAnimT_PointIs(j1, 0.1, 0.5, 0, 0.1 * c10, 0.5 + 0.1 * s10, 0, 1e-6), "gltf chain joint 1 rotates the point");
				RldAnim_PaletteEntry(NULL, j0, &pal[0]);
				RldAnim_PaletteEntry(NULL, j1, &pal[12]);
				RldAnim_SkinVertex(pal, joint, weight, v, nrm, outP, outN);
				// mean of the two joint results
				ANIM_CHECK(EngHiresAnimT_NearV(outP, (const double[3]){0.5 * (0.1 + 0.1 * c10), 0.5 * (0.5 + 0.5 + 0.1 * s10), 0}, 3, 1e-6),
				           "gltf chain skinned vertex 3 at t=1");
				// the normal (0,0,1) is unchanged by rotations about Z
				ANIM_CHECK(EngHiresAnimT_NearV(outN, (const double[3]){0, 0, 1}, 3, 1e-6), "gltf chain skinned normal unchanged");
				RldAnim_FreeClip(&idle);
			}
		}
	}

	if (skel.block != NULL)
	{
		RldAnim_FreeSkeleton(&skel);
	}
	if (dataL != NULL)
	{
		cgltf_free(dataL);
	}
	if (dataS != NULL)
	{
		cgltf_free(dataS);
	}
	if (dataC != NULL)
	{
		cgltf_free(dataC);
	}
	free(glbL);
	free(glbS);
	free(glbC);
	return fails;
}

internal int EngHiresAnimT_CaseRefusals(int *checks)
{
	int fails = 0;
	RldAnimSkeleton skel;
	EngHiresTestGlbDesc d;
	cgltf_options opt;
	cgltf_data *gltf = NULL;
	u8 *glb = NULL;

	// a skeleton without nodes, with too many nodes or too many joints is refused (and made empty)
	ANIM_CHECK(RldAnim_SkeletonAlloc(&skel, 0, 0) == 0 && skel.block == NULL, "refusal skeleton with 0 nodes");
	ANIM_CHECK(RldAnim_SkeletonAlloc(&skel, RLDANIM_NODES_MAX + 1u, 0) == 0 && skel.block == NULL, "refusal skeleton with too many nodes");
	ANIM_CHECK(RldAnim_SkeletonAlloc(&skel, 2, RLDANIM_JOINTS_MAX + 1u) == 0 && skel.block == NULL, "refusal skeleton with too many joints");

	// a skeleton whose parents form a cycle: BuildOrder says no
	ANIM_CHECK(RldAnim_SkeletonAlloc(&skel, 2, 0) == 1, "refusal cycle skeleton alloc");
	if (skel.block != NULL)
	{
		skel.parent[0] = 1;
		skel.parent[1] = 0;
		ANIM_CHECK(RldAnim_BuildOrder(&skel) == 0, "refusal parent cycle");
		skel.parent[0] = 7; // outside
		skel.parent[1] = -1;
		ANIM_CHECK(RldAnim_BuildOrder(&skel) == 0, "refusal parent outside");
		skel.parent[0] = 1; // a chain: 1 is the root
		ANIM_CHECK(RldAnim_BuildOrder(&skel) == 1 && skel.order[0] == 1 && skel.order[1] == 0, "build order puts the parent first");
		RldAnim_FreeSkeleton(&skel);
	}

	// descending key times in the first sampler input of animation 0
	EngHiresAnimT_FillDesc(&d, 0, 0xfffu);
	if (EngHiresAnimT_Parse(&d, &glb, &opt, &gltf))
	{
		const cgltf_accessor *in = gltf->animations[0].samplers[0].input;
		RldAnimClip clip;
		u32 skipped = 0;
		const char *why = "";

		ANIM_CHECK(in != NULL && in->buffer_view != NULL && in->count == 2, "refusal times accessor found");
		if (in != NULL && in->buffer_view != NULL && in->count == 2)
		{
			// the buffer data lives in the GLB copy owned here: write the times 1, 0
			u8 *base = (u8 *)cgltf_buffer_view_data(in->buffer_view);
			float *tm = (float *)(base + in->offset);

			tm[0] = 1.0f;
			tm[1] = 0.0f;
			ANIM_CHECK(RldAnim_ClipFromGltf(gltf, &gltf->animations[0], &clip, &skipped, &why) == 0, "refusal descending key times");
			ANIM_CHECK(why != NULL && why[0] != '\0', "refusal descending key times names the reason");
			ANIM_CHECK(clip.block == NULL && clip.channelCount == 0, "refusal descending key times leaves no clip");
			// the next animation is not touched by it
			ANIM_CHECK(RldAnim_ClipFromGltf(gltf, &gltf->animations[1], &clip, &skipped, &why) == 1, "refusal other clip still builds");
			RldAnim_FreeClip(&clip);
		}
		cgltf_free(gltf);
	}
	else
	{
		ANIM_CHECK(0, "refusal build+parse of the GLB");
	}
	free(glb);
	return fails;
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

// Returns the number of failed checks of the pose maths; *checks += number of checks.
internal int EngHires_SelfTestAnim(int *checks)
{
	int fails = 0;

	fails += EngHiresAnimT_CaseQuats(checks);
	fails += EngHiresAnimT_CaseChannel(checks);
	fails += EngHiresAnimT_CaseClipTime(checks);
	fails += EngHiresAnimT_CaseBlend(checks);
	fails += EngHiresAnimT_CaseMatrix(checks);
	fails += EngHiresAnimT_CaseSkin(checks);
	fails += EngHiresAnimT_CaseGltf(checks);
	fails += EngHiresAnimT_CaseRefusals(checks);
	return fails;
}

#undef ANIM_CHECK
#undef ANIM_PI

// ===========================================================================
// ENGINE HIRES SELFTEST (shape keys) - checks of include/rldhires_morph.inc
// (round "Dummy Glider" phase 1b, 2026-10-24).
//
// Same rules as above: every expected number is written down in closed form, the
// code under test never computes an expectation. The test GLBs are made in memory
// by EngHiresTest_Glb (desc.morph 1..4), no file is touched.
//
// Entry point: EngHires_SelfTestMorph(&checks) returns the number of failures.
// It is NOT called from here: engine_hires_char.c adds
//     failures += EngHires_SelfTestMorph(checks);
// next to the call of EngHires_SelfTestAnim.
// ===========================================================================

#define MORPH_CHECK(cond, what)                                             \
	do                                                                      \
	{                                                                       \
		(*checks)++;                                                        \
		if (!(cond))                                                        \
		{                                                                   \
			fails++;                                                        \
			printf("engine selftest: hires morph %s FAILED\n", (what));     \
		}                                                                   \
	} while (0)

internal int EngMorphT_Near(float a, float b)
{
	return fabsf(a - b) <= 1e-6f;
}

internal int EngMorphT_Near3(const float *a, float x, float y, float z)
{
	return EngMorphT_Near(a[0], x) && EngMorphT_Near(a[1], y) && EngMorphT_Near(a[2], z);
}

// What RldMorph_Reach reports, in call order.
typedef struct
{
	u32 n;
	u32 vertex[8];
	float lo[8][3];
	float hi[8][3];
} EngMorphTReach;

internal void EngMorphT_Grow(void *user, u32 vertex, const float lo[3], const float hi[3])
{
	EngMorphTReach *r = (EngMorphTReach *)user;

	if (r->n < 8u)
	{
		r->vertex[r->n] = vertex;
		memcpy(r->lo[r->n], lo, 3u * sizeof(float));
		memcpy(r->hi[r->n], hi, 3u * sizeof(float));
	}
	r->n++;
}

// Builds the code-made GLB of `morph` (desc.morph), parses it and makes the set
// with RldMorph_Build*: mesh 0 (the body) with first vertex 10, mesh 1 (the wheel,
// no targets) with first vertex 20. *endResult = RldMorph_BuildEnd's return.
// 1 = built and parsed; everything cgltf made is freed here (the set owns copies).
internal int EngMorphT_BuildSet(int morph, RldMorphSet *set, int *endResult)
{
	EngHiresTestGlbDesc d;
	cgltf_options opt;
	cgltf_data *gltf = NULL;
	u8 *glb = NULL;
	RldMorphBuild b;
	int ok;

	memset(&d, 0, sizeof(d));
	d.skinned = 0;
	d.wheels = 4;
	d.morph = morph;
	*endResult = -1;
	ok = EngHiresAnimT_Parse(&d, &glb, &opt, &gltf);
	if (ok && gltf->meshes_count >= 1 && gltf->meshes[0].primitives_count >= 1)
	{
		RldMorph_BuildBegin(&b, set);
		RldMorph_BuildPrimitive(&b, &gltf->meshes[0], &gltf->meshes[0].primitives[0], 10u);
		if (gltf->meshes_count >= 2 && gltf->meshes[1].primitives_count >= 1)
		{
			RldMorph_BuildPrimitive(&b, &gltf->meshes[1], &gltf->meshes[1].primitives[0], 20u);
		}
		*endResult = RldMorph_BuildEnd(&b);
	}
	else
	{
		ok = 0;
	}
	if (gltf != NULL)
	{
		cgltf_free(gltf);
	}
	free(glb);
	return ok;
}

// (a) the chain's weights at a position, closed forms
internal int EngMorphT_CaseChainWeights(int *checks)
{
	static const int chain[3] = {0, 1, 2};
	// pos, then the expected weights of key 0, 1, 2 (all exact in float)
	static const float table[9][4] = {
		{-1.0f, 0.0f, 0.0f, 0.0f},   // before the Basis: nothing
		{0.0f, 0.0f, 0.0f, 0.0f},    // the Basis: nothing
		{0.5f, 0.5f, 0.0f, 0.0f},    // t = 0 + 0.5: first chain key with 0.5
		{1.0f, 1.0f, 0.0f, 0.0f},    // t = 1: first chain key with 1
		{1.25f, 0.75f, 0.25f, 0.0f}, // t = 1 + 0.25: 0.75 and 0.25
		{2.0f, 0.0f, 1.0f, 0.0f},
		{2.5f, 0.0f, 0.5f, 0.5f},
		{3.0f, 0.0f, 0.0f, 1.0f},    // t = n: the last key with 1
		{3.5f, 0.0f, 0.0f, 1.0f}     // beyond n: the same
	};
	int fails = 0;
	int i;
	float w[3];

	for (i = 0; i < 9; i++)
	{
		w[0] = w[1] = w[2] = 0.0f;
		RldMorph_ChainWeightsAdd(chain, 3, table[i][0], w);
		MORPH_CHECK(w[0] == table[i][1] && w[1] == table[i][2] && w[2] == table[i][3], "chain weights at t = -1 0 0.5 1 1.25 2 2.5 3 3.5");
	}
	w[0] = w[1] = w[2] = 0.0f;
	RldMorph_ChainWeightsAdd(chain, 0, 2.0f, w);
	MORPH_CHECK(w[0] == 0.0f && w[1] == 0.0f && w[2] == 0.0f, "chain weights with count 0 add nothing");
	// it adds on top of what is there
	w[0] = 0.25f;
	w[1] = 0.0f;
	w[2] = 0.0f;
	RldMorph_ChainWeightsAdd(chain, 3, 0.5f, w);
	MORPH_CHECK(w[0] == 0.75f && w[1] == 0.0f && w[2] == 0.0f, "chain weights add to existing weights");
	return fails;
}

// (b) the state: frames, play, refusals, same chain again
internal int EngMorphT_CaseState(int *checks)
{
	static const int keys[3] = {0, 1, 2};
	static const int keysOther[3] = {2, 1, 0};
	static const int keysNeg[3] = {0, -1, 2};
	static const int keysHigh[3] = {0, 1, 7};
	static const int keys17[RLDMORPH_CHAIN_MAX + 1] = {0};
	RldMorphState st;
	RldMorphState copy;
	int fails = 0;
	int i;

	RldMorph_StateReset(&st);
	MORPH_CHECK(st.chainCount == 0 && st.frames == 1 && st.frame == 0 && st.dir == 0, "state reset: no chain, frames 1, frame 0, dir 0");
	MORPH_CHECK(RldMorph_ChainSet(&st, keys, 3, 6, 7u) == 1, "chain set {0,1,2} over 6 frames");
	MORPH_CHECK(RldMorph_ChainPos(&st) == 0.0f, "chain pos 0 after set");
	MORPH_CHECK(RldMorph_ChainPlay(&st, 1) == 1, "chain play +1");
	for (i = 0; i < 3; i++)
	{
		RldMorph_ChainStep(&st);
	}
	MORPH_CHECK(RldMorph_ChainPos(&st) == 1.5f && RldMorph_ChainDone(&st) == 0, "3 steps of 6 frames: pos 1.5, not done");
	for (i = 0; i < 3; i++)
	{
		RldMorph_ChainStep(&st);
	}
	MORPH_CHECK(RldMorph_ChainPos(&st) == 3.0f && RldMorph_ChainDone(&st) == 1, "6 steps of 6 frames: pos 3, done");
	RldMorph_ChainStep(&st);
	MORPH_CHECK(RldMorph_ChainPos(&st) == 3.0f && RldMorph_ChainDone(&st) == 1, "one more step stays at 3");
	MORPH_CHECK(RldMorph_ChainPlay(&st, -1) == 1, "chain play -1");
	for (i = 0; i < 10; i++)
	{
		RldMorph_ChainStep(&st);
	}
	MORPH_CHECK(RldMorph_ChainPos(&st) == 0.0f && RldMorph_ChainDone(&st) == 1, "10 steps backwards: pos 0, done");

	// refusals leave the state byte-identical
	memcpy(&copy, &st, sizeof(st));
	MORPH_CHECK(RldMorph_ChainSet(&st, keys, 0, 6, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: count 0");
	MORPH_CHECK(RldMorph_ChainSet(&st, keys17, RLDMORPH_CHAIN_MAX + 1, 6, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: count CHAIN_MAX + 1");
	MORPH_CHECK(RldMorph_ChainSet(&st, keys, 3, 0, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: frames 0");
	MORPH_CHECK(RldMorph_ChainSet(&st, keys, 3, RLDMORPH_FRAMES_MAX + 1, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: frames FRAMES_MAX + 1");
	MORPH_CHECK(RldMorph_ChainSet(&st, keysNeg, 3, 6, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: key -1");
	MORPH_CHECK(RldMorph_ChainSet(&st, keysHigh, 3, 6, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: key == keyCount");
	MORPH_CHECK(RldMorph_ChainSet(&st, NULL, 3, 6, 7u) == 0 && memcmp(&copy, &st, sizeof(st)) == 0, "refusal: NULL keys");

	// the same chain again keeps the position (3 of 6 frames = 1.5 -> 2 of 4 frames), the direction is cleared
	MORPH_CHECK(RldMorph_ChainSet(&st, keys, 3, 6, 7u) == 1 && RldMorph_ChainSetPos(&st, 1.5f) == 1 && RldMorph_ChainPos(&st) == 1.5f, "set pos 1.5 of 6 frames");
	RldMorph_ChainPlay(&st, 1);
	MORPH_CHECK(RldMorph_ChainSet(&st, keys, 3, 4, 7u) == 1, "same chain again over 4 frames");
	MORPH_CHECK(st.frame == 2 && st.frames == 4 && RldMorph_ChainPos(&st) == 1.5f, "same chain keeps pos 1.5 (frame 2 of 4)");
	MORPH_CHECK(st.dir == 0 && RldMorph_ChainDone(&st) == 1, "chain set clears the direction (done)");
	// a different chain starts at the Basis
	MORPH_CHECK(RldMorph_ChainSet(&st, keysOther, 3, 4, 7u) == 1 && RldMorph_ChainPos(&st) == 0.0f && st.chain[0] == 2 && st.chain[2] == 0, "other chain resets pos to 0");

	// set pos: frames 4, count 3
	MORPH_CHECK(RldMorph_ChainSetPos(&st, 1.0f) == 1 && st.frame == 1 && RldMorph_ChainPos(&st) == 0.75f, "set pos 1.0 -> frame 1 -> pos 0.75");
	MORPH_CHECK(RldMorph_ChainSetPos(&st, -5.0f) == 1 && RldMorph_ChainPos(&st) == 0.0f, "set pos -5 -> 0");
	MORPH_CHECK(RldMorph_ChainSetPos(&st, 99.0f) == 1 && RldMorph_ChainPos(&st) == 3.0f, "set pos 99 -> 3");

	// no chain
	RldMorph_StateReset(&st);
	MORPH_CHECK(RldMorph_ChainPlay(&st, 1) == 0, "no chain: play returns 0");
	MORPH_CHECK(RldMorph_ChainSetPos(&st, 1.0f) == 0, "no chain: set pos returns 0");
	MORPH_CHECK(RldMorph_ChainPos(&st) == 0.0f && RldMorph_ChainDone(&st) == 1, "no chain: pos 0, done");
	return fails;
}

// (c) the final weights: key layer + chain, clamped
internal int EngMorphT_CaseWeights(int *checks)
{
	static const int one[1] = {0};
	static const u32 nanBits = 0x7fc00000u;
	RldMorphState st;
	float out[4];
	float nanF;
	int fails = 0;
	int n;
	int i;

	memcpy(&nanF, &nanBits, sizeof(nanF));

	// key layer 0.8 on key 0 plus chain {0} at pos 1 (weight 1): 1.8 is clamped to 1
	RldMorph_StateReset(&st);
	st.key[0] = 0.8f;
	RldMorph_ChainSet(&st, one, 1, 1, 2u);
	RldMorph_ChainSetPos(&st, 1.0f);
	for (i = 0; i < 4; i++)
	{
		out[i] = 9.0f;
	}
	n = RldMorph_Weights(&st, 2u, out);
	MORPH_CHECK(n == 1 && out[0] == 1.0f && out[1] == 0.0f && out[2] == 9.0f, "weights: 0.8 + chain 1 is clamped to 1, returns 1");

	// the key layer alone passes below 1
	RldMorph_StateReset(&st);
	st.key[1] = 0.5f;
	n = RldMorph_Weights(&st, 2u, out);
	MORPH_CHECK(n == 1 && out[0] == 0.0f && out[1] == 0.5f, "weights: key layer 0.5 passes");

	// NaN and negative become 0
	RldMorph_StateReset(&st);
	st.key[0] = nanF;
	st.key[1] = -0.5f;
	n = RldMorph_Weights(&st, 3u, out);
	MORPH_CHECK(n == 0 && out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f, "weights: NaN and negative give 0, returns 0");

	// nothing set: the Basis
	RldMorph_StateReset(&st);
	n = RldMorph_Weights(&st, 4u, out);
	MORPH_CHECK(n == 0 && out[0] == 0.0f && out[3] == 0.0f, "weights: all 0, no chain returns 0");

	// no keys: nothing written, 0
	RldMorph_StateReset(&st);
	st.key[0] = 1.0f;
	for (i = 0; i < 4; i++)
	{
		out[i] = 9.0f;
	}
	n = RldMorph_Weights(&st, 0u, out);
	MORPH_CHECK(n == 0 && out[0] == 9.0f, "weights: keyCount 0 returns 0");
	return fails;
}

// The sets of morph 1 and morph 2 (sparse) must be the same in every array.
internal int EngMorphT_SetSame(const RldMorphSet *a, const RldMorphSet *b)
{
	u32 k;

	if (a->keyCount != b->keyCount || a->normalMask != b->normalMask || a->vertCount != b->vertCount || a->kept != b->kept)
	{
		return 0;
	}
	if (a->vert == NULL || b->vert == NULL || a->baseP == NULL || b->baseP == NULL || a->baseN == NULL || b->baseN == NULL)
	{
		return 0;
	}
	if (memcmp(a->name, b->name, sizeof(a->name)) != 0 || memcmp(a->entries, b->entries, sizeof(a->entries)) != 0)
	{
		return 0;
	}
	if (memcmp(a->vert, b->vert, (size_t)a->vertCount * sizeof(u32)) != 0)
	{
		return 0;
	}
	if (memcmp(a->baseP, b->baseP, (size_t)a->vertCount * 3u * sizeof(float)) != 0 || memcmp(a->baseN, b->baseN, (size_t)a->vertCount * 3u * sizeof(float)) != 0)
	{
		return 0;
	}
	for (k = 0; k < a->keyCount; k++)
	{
		if (memcmp(a->at[k], b->at[k], (size_t)a->entries[k] * sizeof(u32)) != 0)
		{
			return 0;
		}
		if (memcmp(a->delta[k], b->delta[k], (size_t)a->entries[k] * 6u * sizeof(float)) != 0)
		{
			return 0;
		}
	}
	return 1;
}

// (d) the build half and the blend on the code-made GLBs
internal int EngMorphT_CaseBuild(int *checks)
{
	// The body of the test GLB: vertex 3 lies at (0.1, 0.5, 0), its normal is (0,0,1),
	// vertex 0 at (0,0,0), vertex 1 at (0.1,0,0). "lift" moves v0 and v1 by (0,0.25,0);
	// "bend" moves v3 by (0.5,0,0) and its normal by (1,0,-1). The body starts at
	// vertex 10 of the driver, so the set's vertices are 10, 11, 13 (slots 0, 1, 2).
	RldMorphSet set1;
	RldMorphSet set2;
	RldMorphSet set3;
	RldMorphSet set4;
	int fails = 0;
	int end1 = -1;
	int end2 = -1;
	int end3 = -1;
	int end4 = -1;
	int ok1;
	int ok2;
	int ok3;
	int ok4;

	memset(&set1, 0, sizeof(set1));
	memset(&set2, 0, sizeof(set2));
	memset(&set3, 0, sizeof(set3));
	memset(&set4, 0, sizeof(set4));

	// ----- morph 1: two named targets -----
	ok1 = EngMorphT_BuildSet(1, &set1, &end1);
	MORPH_CHECK(ok1 && end1 == 1, "morph 1: build and BuildEnd == 1");
	if (ok1 && end1 == 1)
	{
		float w[2];
		float p[9];
		float n[9];
		EngMorphTReach reach;

		MORPH_CHECK(set1.keyCount == 2 && strcmp(set1.name[0], "lift") == 0 && strcmp(set1.name[1], "bend") == 0, "morph 1: keys lift, bend");
		MORPH_CHECK(RldMorph_Find(&set1, "bend") == 1 && RldMorph_Find(&set1, "lift") == 0 && RldMorph_Find(&set1, "x") == -1 && RldMorph_Find(&set1, NULL) == -1, "morph 1: find");
		MORPH_CHECK(set1.targets == 2 && set1.unnamed == 0 && set1.overKeys == 0 && set1.overBudget == 0 && set1.badAccessor == 0, "morph 1: counters");
		MORPH_CHECK(set1.normalMask == 2u, "morph 1: normal mask 2 (only bend)");
		MORPH_CHECK(set1.vertCount == 3 && set1.vert != NULL && set1.vert[0] == 10u && set1.vert[1] == 11u && set1.vert[2] == 13u, "morph 1: vertices 10, 11, 13");
		MORPH_CHECK(set1.kept == 3 && set1.entries[0] == 2 && set1.entries[1] == 1, "morph 1: entries 2 and 1");
		MORPH_CHECK(set1.at[0] != NULL && set1.at[0][0] == 0u && set1.at[0][1] == 1u && set1.at[1] != NULL && set1.at[1][0] == 2u, "morph 1: slots lift {0,1}, bend {2}");
		MORPH_CHECK(set1.baseP != NULL && EngMorphT_Near3(&set1.baseP[6], 0.1f, 0.5f, 0.0f), "morph 1: base position of slot 2 (0.1,0.5,0)");
		MORPH_CHECK(set1.baseN != NULL && EngMorphT_Near3(&set1.baseN[6], 0.0f, 0.0f, 1.0f), "morph 1: base normal of slot 2 (0,0,1)");
		MORPH_CHECK(set1.delta[0] != NULL && EngMorphT_Near3(&set1.delta[0][0], 0.0f, 0.25f, 0.0f) && EngMorphT_Near3(&set1.delta[0][3], 0.0f, 0.0f, 0.0f), "morph 1: lift delta of slot 0 (0,0.25,0), no normal");
		MORPH_CHECK(set1.delta[1] != NULL && EngMorphT_Near3(&set1.delta[1][0], 0.5f, 0.0f, 0.0f) && EngMorphT_Near3(&set1.delta[1][3], 1.0f, 0.0f, -1.0f), "morph 1: bend delta (0.5,0,0) and normal (1,0,-1)");

		// blend, weights {1, 0}
		w[0] = 1.0f;
		w[1] = 0.0f;
		RldMorph_Blend(&set1, w, p, n);
		MORPH_CHECK(EngMorphT_Near3(&p[0], 0.0f, 0.25f, 0.0f) && EngMorphT_Near3(&p[3], 0.1f, 0.25f, 0.0f), "blend {1,0}: slots 0 and 1 up by 0.25");
		MORPH_CHECK(EngMorphT_Near3(&p[6], 0.1f, 0.5f, 0.0f) && EngMorphT_Near3(&n[6], 0.0f, 0.0f, 1.0f), "blend {1,0}: slot 2 unchanged, normal (0,0,1)");
		// blend, weights {0.5, 1}
		w[0] = 0.5f;
		w[1] = 1.0f;
		RldMorph_Blend(&set1, w, p, n);
		MORPH_CHECK(EngMorphT_Near3(&p[0], 0.0f, 0.125f, 0.0f), "blend {0.5,1}: slot 0 up by 0.125");
		MORPH_CHECK(EngMorphT_Near3(&p[6], 0.6f, 0.5f, 0.0f), "blend {0.5,1}: slot 2 x + 0.5");
		MORPH_CHECK(EngMorphT_Near3(&n[6], 1.0f, 0.0f, 0.0f), "blend {0.5,1}: slot 2 normal (0,0,1) + (1,0,-1) = (1,0,0)");
		MORPH_CHECK(EngMorphT_Near3(&n[0], 0.0f, 0.0f, 1.0f), "blend {0.5,1}: slot 0 normal unchanged (lift has none)");

		// reach: lo = base, hi = base + the positive deltas
		memset(&reach, 0, sizeof(reach));
		RldMorph_Reach(&set1, EngMorphT_Grow, &reach);
		MORPH_CHECK(reach.n == 3 && reach.vertex[0] == 10u && reach.vertex[1] == 11u && reach.vertex[2] == 13u, "reach: 3 vertices 10, 11, 13");
		MORPH_CHECK(EngMorphT_Near3(reach.lo[0], 0.0f, 0.0f, 0.0f) && EngMorphT_Near3(reach.hi[0], 0.0f, 0.25f, 0.0f), "reach: slot 0 lo (0,0,0) hi (0,0.25,0)");
		MORPH_CHECK(EngMorphT_Near3(reach.lo[1], 0.1f, 0.0f, 0.0f) && EngMorphT_Near3(reach.hi[1], 0.1f, 0.25f, 0.0f), "reach: slot 1 lo (0.1,0,0) hi (0.1,0.25,0)");
		MORPH_CHECK(EngMorphT_Near3(reach.lo[2], 0.1f, 0.5f, 0.0f) && EngMorphT_Near3(reach.hi[2], 0.6f, 0.5f, 0.0f), "reach: slot 2 lo (0.1,0.5,0) hi (0.6,0.5,0)");
	}

	// ----- morph 2: the same set from a sparse POSITION accessor -----
	ok2 = EngMorphT_BuildSet(2, &set2, &end2);
	MORPH_CHECK(ok2 && end2 == 1, "morph 2 (sparse): build and BuildEnd == 1");
	if (ok1 && end1 == 1 && ok2 && end2 == 1)
	{
		MORPH_CHECK(EngMorphT_SetSame(&set1, &set2), "morph 2 (sparse): every array equals the morph 1 set");
	}

	// ----- morph 3: unnamed targets -----
	ok3 = EngMorphT_BuildSet(3, &set3, &end3);
	MORPH_CHECK(ok3 && end3 == 1, "morph 3: build and BuildEnd == 1");
	MORPH_CHECK(set3.keyCount == 0 && set3.unnamed == 2 && set3.targets == 2 && set3.block == NULL && set3.vertCount == 0, "morph 3: no keys, 2 unnamed, no block");

	// ----- morph 4: 33 targets, 32 keys kept -----
	ok4 = EngMorphT_BuildSet(4, &set4, &end4);
	MORPH_CHECK(ok4 && end4 == 1, "morph 4: build and BuildEnd == 1");
	if (ok4 && end4 == 1)
	{
		u32 k;
		int allOne = 1;

		MORPH_CHECK(set4.keyCount == RLDMORPH_KEYS_MAX && set4.keyCount == 32 && set4.overKeys == 1 && set4.targets == 33, "morph 4: 32 keys, 1 over, 33 targets");
		MORPH_CHECK(strcmp(set4.name[0], "k0") == 0 && strcmp(set4.name[31], "k31") == 0, "morph 4: names k0 and k31");
		MORPH_CHECK(set4.vertCount == 1 && set4.vert != NULL && set4.vert[0] == 10u, "morph 4: one vertex (10)");
		for (k = 0; k < set4.keyCount; k++)
		{
			if (set4.entries[k] != 1u)
			{
				allOne = 0;
			}
		}
		MORPH_CHECK(allOne, "morph 4: every key has 1 entry");
		// Key 5 is target k5: the builder writes z = 0.01f * (float)(5 + 1), i.e. the float product
		// 0.01f * 6.0f; compared with a tolerance of 1e-9 so a compiler folding it in another
		// precision still passes.
		MORPH_CHECK(set4.delta[5] != NULL && fabsf(set4.delta[5][2] - 0.01f * 6.0f) < 1e-9f && set4.delta[5][0] == 0.0f && set4.delta[5][1] == 0.0f, "morph 4: key 5 delta z = 0.06");
		MORPH_CHECK(set4.delta[31] != NULL && fabsf(set4.delta[31][2] - 0.01f * 32.0f) < 1e-9f, "morph 4: key 31 delta z = 0.32");
	}

	RldMorph_Free(&set1);
	RldMorph_Free(&set2);
	RldMorph_Free(&set3);
	RldMorph_Free(&set4);
	MORPH_CHECK(set1.block == NULL && set1.keyCount == 0, "set freed: block NULL");
	return fails;
}

// Returns the number of failed checks of the shape keys; *checks += number of checks.
internal int EngHires_SelfTestMorph(int *checks)
{
	int fails = 0;

	fails += EngMorphT_CaseChainWeights(checks);
	fails += EngMorphT_CaseState(checks);
	fails += EngMorphT_CaseWeights(checks);
	fails += EngMorphT_CaseBuild(checks);
	return fails;
}

#undef MORPH_CHECK
