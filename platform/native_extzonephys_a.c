// THE ZONE PORT, PART A: the kart's physics glue and the physics world's step (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone too
// (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_a.h>

#define STEP_G_WORLD 0x006FA190u      // global: pointer to the game world
#define STEP_G_SUBSTEP_ON 0x006FB2B0u // global byte: 0 = the subspace step is skipped

#define A_ONE PS2F_ONE
#define A_HALF 0x3F000000u     // 0.5 (the original's constant)
#define A_BIG 0x7149F2CAu      // 1.00000002e+30 (the original's constant)
#define A_EPS_UNIT 0x3A83126Fu // 0.00100000005 (the original's constant)
#define A_EPS_STEP 0x3727C5ACu // 9.99999975e-06 (the original's constant)

// Query helper: a call that leaves the path (only the registers the original sets are filled).
static ExtZpQueryResult StepQuery(ExtZpCtx *c, int kind, uint32_t func, uint32_t a0, uint32_t a1, Ps2F f12)
{
	ExtZpQueryArgs args;
	int i;
	for (i = 0; i < 8; i++)
	{
		args.a[i] = 0u;
		args.f[i] = 0u;
	}
	args.a[0] = a0;
	args.a[1] = a1;
	args.f[0] = f12;
	args.func = func;
	return ExtZp_Query(c, kind, &args);
}

// ceilf of the executable's math library (round towards +infinity, on the bit pattern).
Ps2F ExtZp_Step_43ae78(ExtZpCtx *c, Ps2F f12)
{
	uint32_t a0 = f12;
	int32_t a1;
	uint32_t a2;
	Ps2F f0;
	(void)c;
	a1 = (int32_t)((a0 >> 23) & 0xFFu) - 0x7F;
	if (!(a1 < 0x17))
	{
		if (a1 != 0x80)
			return f12;
		return Ps2F_Add(f12, f12);
	}
	if (a1 < 0)
	{
		f0 = Ps2F_Add(f12, A_BIG);
		if (!Ps2F_Lt(0u, f0))
			return a0;
		if ((int32_t)a0 >= 0)
		{
			if (a0 != 0u)
				a0 = 0x3F800000u;
		}
		else
			a0 = 0x80000000u;
		return a0;
	}
	a2 = 0x007FFFFFu >> (a1 & 31);
	if ((a0 & a2) == 0u)
		return f12;
	f0 = Ps2F_Add(f12, A_BIG);
	if (!Ps2F_Lt(0u, f0))
		return a0;
	if ((int32_t)a0 > 0)
		a0 += 0x00800000u >> (a1 & 31);
	a0 &= ~a2;
	return a0;
}

// floorf of the executable's math library (round towards -infinity, on the bit pattern).
Ps2F ExtZp_Step_43b0b0(ExtZpCtx *c, Ps2F f12)
{
	uint32_t a0 = f12;
	int32_t a1;
	uint32_t a2;
	Ps2F f0;
	(void)c;
	a1 = (int32_t)((a0 >> 23) & 0xFFu) - 0x7F;
	if (!(a1 < 0x17))
	{
		if (a1 != 0x80)
			return f12;
		return Ps2F_Add(f12, f12);
	}
	if (a1 < 0)
	{
		f0 = Ps2F_Add(f12, A_BIG);
		if (!Ps2F_Lt(0u, f0))
			return a0;
		if ((int32_t)a0 < 0)
		{
			if ((a0 & 0x7FFFFFFFu) != 0u)
				a0 = 0xBF800000u;
		}
		else
			a0 = 0u;
		return a0;
	}
	a2 = 0x007FFFFFu >> (a1 & 31);
	if ((a0 & a2) == 0u)
		return f12;
	f0 = Ps2F_Add(f12, A_BIG);
	if (!Ps2F_Lt(0u, f0))
		return a0;
	if ((int32_t)a0 < 0)
		a0 += 0x00800000u >> (a1 & 31);
	a0 &= ~a2;
	return a0;
}

// Returns a0 + 8.
uint32_t ExtZp_Step_345a10(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0 + 8u;
}

// Returns the float at (*(a0 + 0x14)) + 0x14.
Ps2F ExtZp_Step_380f80(ExtZpCtx *c, uint32_t a0)
{
	uint32_t v0 = ZmR32(c->m, a0 + 0x14u);
	return ZmRF(c->m, v0 + 0x14u);
}

// Time pair (a1 seconds, a1hi fraction) into the body (+0x5C / +0x60) and its table entry.
void ExtZp_Step_38eaf0(ExtZpCtx *c, uint32_t a0, uint64_t a1)
{
	uint32_t a1lo = (uint32_t)a1, a1hi = (uint32_t)(a1 >> 32);
	ExtZpMem *m = c->m;
	uint32_t a2, a5, v3;
	ZmW32(m, a0 + 0x5Cu, a1lo);
	ZmWF(m, a0 + 0x60u, a1hi);
	a2 = ZmR32(m, a0 + 0x68u) << 3; // index * 8
	a5 = ZmR32(m, a0 + 0x40u) + a2; // table + index * 8
	v3 = ZmR32(m, a0 + 0x5Cu);
	ZmW32(m, a5, v3);
	ZmWF(m, a5 + 4u, ZmRF(m, a0 + 0x60u));
}

// Collision detector tolerance of the subspace: 0 without a detector.
Ps2F ExtZp_Step_3b2600(ExtZpCtx *c, uint32_t a0)
{
	uint32_t det = ZmR32(c->m, a0 + 0x70u);
	if (det != 0u)
		return ExtZp_Step_380f80(c, det);
	return 0u;
}

// Sets the tolerance (+0x18 of the detector's inner object).
void ExtZp_Step_3b2640(ExtZpCtx *c, uint32_t a0, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t v3 = ZmR32(m, a0 + 0x70u);
	if (v3 != 0u)
	{
		v3 = ZmR32(m, v3 + 0x14u);
		ZmWF(m, v3 + 0x18u, f12);
	}
}

// Time pair += f12; fraction into (-1, 1) by moving whole units into the seconds. Returns a0.
uint32_t ExtZp_Step_3b21e0(ExtZpCtx *c, uint32_t a0, Ps2F f12)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2;
	int32_t whole;
	f1 = ZmRF(m, a0 + 4u);
	f12 = Ps2F_Add(f1, f12);
	ZmWF(m, a0 + 4u, f12);
	if (!Ps2F_Lt(Ps2F_Abs(f12), A_ONE))
	{
		if (Ps2F_Lt(0u, f12))
			f0 = ExtZp_Step_43b0b0(c, f12);
		else
			f0 = ExtZp_Step_43ae78(c, f12);
		whole = Ps2F_ToInt(f0);
		f2 = ZmRF(m, a0 + 4u);
		f0 = Ps2F_Sub(f2, f0);
		ZmWF(m, a0 + 4u, f0);
		ZmW32(m, a0, ZmR32(m, a0) + (uint32_t)whole);
	}
	return a0;
}

// Calls slot +0x20 of every listener with (listener, subspace, subspace time).
void ExtZp_Step_3b2280(ExtZpCtx *c, uint32_t a0)
{
	ExtZpMem *m = c->m;
	uint32_t count = ZmR32(m, a0 + 0x4Cu);
	uint32_t s1 = ZmR32(m, a0 + 0x50u);
	uint32_t s0 = s1 + (count << 2);
	if (s1 != s0)
	{
		do
		{
			uint32_t obj = ZmR32(m, s1);
			uint32_t target = ZmR32(m, ZmR32(m, obj) + 0x20u);
			// no listener is part of the ported path: the call is not followed
			ExtZp_Leave(c, target);
			s1 += 4u;
		} while (s1 != s0);
	}
}

// Subspace step: h = (target - time) / n, n times the substep (slot +0x30), then the listeners.
void ExtZp_Step_3b1750(ExtZpCtx *c, uint32_t a0, uint64_t a1w, uint32_t a2)
{
	uint32_t a1 = (uint32_t)a1w, a1hi = (uint32_t)(a1w >> 32);
	ExtZpMem *m = c->m;
	uint32_t s2 = a0;
	int32_t s1 = (int32_t)a2;
	int32_t s0;
	Ps2F f0, f1, f20;
	uint32_t s3;
	if (ZmR8(m, STEP_G_SUBSTEP_ON) == 0u)
		return;
	f1 = a1hi;
	f0 = ZmRF(m, s2 + 0x5Cu);
	f0 = Ps2F_Sub(f1, f0);
	f1 = Ps2F_FromInt((int32_t)(a1 - ZmR32(m, s2 + 0x58u)));
	f0 = Ps2F_Add(f1, f0);
	ZmWF(m, s2 + 0x64u, f0);
	for (s0 = 0; s0 < (int32_t)ZmR32(m, s2 + 0x34u); s0++)
	{
		(void)ExtZp_Step_345a10(c, s2 + 0x30u);
		(void)ExtZp_Step_345a10(c, s2 + 0x30u);
	}
	{
		ExtZpQueryResult r = StepQuery(c, EXT_ZP_Q_OTHER, 0x003687A0u, 0u, 0u, 0u);
		(void)StepQuery(c, EXT_ZP_Q_OTHER, 0x0036A290u, r.v0, 0u, 0u);
	}
	f1 = Ps2F_FromInt(s1);
	f0 = ZmRF(m, s2 + 0x64u);
	f0 = Ps2F_Div(f0, f1);
	ZmWF(m, s2 + 0x64u, f0);
	s0 = 0;
	if (0 < s1)
	{
		do
		{
			uint32_t buf, target;
			f20 = ZmRF(m, s2 + 0x5Cu);
			f1 = ZmRF(m, s2 + 0x64u);
			f20 = Ps2F_Add(f20, f1);
			s3 = ZmR32(m, s2 + 0x58u);
			if (!Ps2F_Lt(Ps2F_Abs(f20), A_ONE))
			{
				if (Ps2F_Lt(0u, f20))
					f0 = ExtZp_Step_43b0b0(c, f20);
				else
					f0 = ExtZp_Step_43ae78(c, f20);
				f20 = Ps2F_Sub(f20, f0);
				s3 += (uint32_t)Ps2F_ToInt(f0);
			}
			buf = ExtZp_Push(c, 16u);
			target = ZmR32(m, ZmR32(m, s2) + 0x30u);
			if (target == 0x003B1B20u)
				ExtZp_Step_3b1b20(c, buf, s2, ((uint64_t)f20 << 32) | s3);
			else if (target == 0x003AF920u)
				ExtZp_Step_3af920(c, buf, s2, ((uint64_t)f20 << 32) | s3);
			else
				ExtZp_Leave(c, target);
			ExtZp_Pop(c, 16u);
			s0++;
		} while (s0 < s1);
	}
	ExtZp_Step_3b2280(c, s2);
}

// Environment step.
void ExtZp_Step_3b2db0(ExtZpCtx *c, uint32_t a0, uint32_t a1, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t s1 = a0, s0 = a1;
	uint32_t s3, s2, v3;
	Ps2F f0;
	f12 = Ps2F_Add(ZmRF(m, s1 + 0x24u), f12);
	ZmWF(m, s1 + 0x24u, f12);
	if (!Ps2F_Lt(Ps2F_Abs(f12), A_ONE))
	{
		uint32_t whole;
		if (Ps2F_Lt(0u, f12))
			f0 = ExtZp_Step_43b0b0(c, f12);
		else
			f0 = ExtZp_Step_43ae78(c, f12);
		whole = (uint32_t)Ps2F_ToInt(f0);
		f0 = Ps2F_Sub(ZmRF(m, s1 + 0x24u), f0);
		ZmWF(m, s1 + 0x24u, f0);
		v3 = ZmR32(m, s1 + 0x20u);
		ZmW32(m, s1 + 0x20u, v3 + whole);
	}
	v3 = ZmR32(m, s1 + 0x18u);
	s3 = ZmR32(m, s1 + 0x1Cu);
	s2 = s3 + (v3 << 2);
	if (s3 != s2)
	{
		do
		{
			uint32_t sub = ZmR32(m, s3);
			uint32_t tlo = ZmR32(m, s1 + 0x20u);
			uint32_t thi = ZmR32(m, s1 + 0x24u);
			ExtZp_Step_3b1750(c, sub, ((uint64_t)thi << 32) | tlo, s0);
			s3 += 4u;
		} while (s3 != s2);
	}
}

// World step: dt capped, substep count from the world's rate and cap.
void ExtZp_Step_336b60(ExtZpCtx *c, uint32_t a0, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t v3 = ZmR32(m, a0 + 0x64u);
	Ps2F f0, f1;
	int32_t a1, lim, v0;
	if (v3 == 0u)
		return;
	if (Ps2F_Le(f12, 0u))
		return;
	f0 = ZmRF(m, a0 + 0x5Cu);
	if (!Ps2F_Lt(f12, f0))
		f12 = f0;
	f1 = ZmRF(m, a0 + 0x58u);
	f0 = A_HALF;
	f1 = Ps2F_Mul(f12, f1);
	lim = (int32_t)ZmR32(m, a0 + 0x60u);
	f0 = Ps2F_Add(f0, f1);
	a1 = Ps2F_ToInt(f0);
	v0 = (a1 < lim) ? a1 : lim;
	if (v0 < 2)
		a1 = 1;
	else if (!(a1 < lim))
		a1 = lim;
	ExtZp_Step_3b2db0(c, ZmR32(m, v3 + 8u), (uint32_t)a1, f12);
}

// Refreshes a shell's three rows from its body's matrix at body + 0xE0.
void ExtZp_Glue_190370(ExtZpCtx *c, uint32_t shell)
{
	ExtZpMem *m = c->m;
	static const Ps2F rowIn[3][4] = {{0u, PS2F_ONE, 0u, PS2F_ONE}, {0u, 0u, PS2F_ONE, PS2F_ONE}, {PS2F_ONE, 0u, 0u, PS2F_ONE}};
	static const uint32_t rowOut[3] = {0x10u, 0x20u, 0x30u};
	int k;
	for (k = 0; k < 3; k++)
	{
		uint32_t mat = ZmR32(m, shell + 0x94u) + 0xE0u;
		Ps2Vec4 v, r0, r1, r2, bx, by, bz;
		Ps2VAcc acc;
		int j;
		for (j = 0; j < 4; j++)
			v.v[j] = rowIn[k][j];
		r0 = ZmRV(m, mat);
		r1 = ZmRV(m, mat + 0x10u);
		r2 = ZmRV(m, mat + 0x20u);
		bx = Ps2V_Bc(&v, 0);
		by = Ps2V_Bc(&v, 1);
		bz = Ps2V_Bc(&v, 2);
		for (j = 0; j < 4; j++)
		{
			acc.lane[j].v = 0u;
			acc.lane[j].overflow = 0;
		}
		Ps2V_Mula(&acc, &r0, &bx, 0xEu);
		Ps2V_Madda(&acc, &r1, &by, 0xEu);
		Ps2V_MaddAcc(&v, &acc, &r2, &bz, 0xEu);
		ZmWV(m, shell + rowOut[k], v);
	}
}

// Unit direction + length of a vector in memory: x*x + y*y + z*z through the accumulator,
// divide by the root unless the sum is exactly zero. Returns the (root) length.
static Ps2F GlueNormalise(ExtZpMem *m, uint32_t at)
{
	Ps2F f6 = ZmRF(m, at), f7 = ZmRF(m, at + 4u), f8 = ZmRF(m, at + 8u);
	Ps2Acc acc;
	Ps2F f3, f2, f1, f0;
	acc = Ps2A_Mula(f6, f6);
	acc = Ps2A_Madda(acc, f7, f7);
	f3 = Ps2A_Madd(acc, f8, f8);
	if (!Ps2F_Eq(f3, 0u))
	{
		f3 = Ps2F_Sqrt(f3);
		f2 = ZmRF(m, at);
		f1 = ZmRF(m, at + 4u);
		f0 = ZmRF(m, at + 8u);
		f2 = Ps2F_Div(f2, f3);
		f1 = Ps2F_Div(f1, f3);
		f0 = Ps2F_Div(f0, f3);
		ZmWF(m, at, f2);
		ZmWF(m, at + 4u, f1);
		ZmWF(m, at + 8u, f0);
	}
	return f3;
}

// Velocity of the body into the shell, unit direction and speed.
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t shell)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 t = ZmRV(m, ZmR32(m, shell + 0x94u) + 0x60u);
	Ps2F f3;
	ZmWV(m, shell + 0x40u, t);
	ZmWV(m, shell + 0x50u, ZmRV(m, shell + 0x40u));
	f3 = GlueNormalise(m, shell + 0x50u);
	ZmWF(m, shell + 0x60u, f3);
	if (Ps2F_Le(f3, A_EPS_UNIT))
		ZmWV(m, shell + 0x50u, ZmRV(m, shell + 0x10u));
}

// Angular velocity of the body into the shell, unit direction and magnitude.
void ExtZp_Glue_190280(ExtZpCtx *c, uint32_t shell)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 t = ZmRV(m, ZmR32(m, shell + 0x94u) + 0x70u);
	Ps2F f3;
	ZmWV(m, shell + 0x70u, t);
	ZmWV(m, shell + 0x80u, ZmRV(m, shell + 0x70u));
	f3 = GlueNormalise(m, shell + 0x80u);
	ZmWF(m, shell + 0x90u, f3);
	if (Ps2F_Le(f3, A_EPS_UNIT))
	{
		Ps2Vec4 u = {{0u, 0u, 0u, PS2F_ONE}};
		ZmWV(m, shell + 0x80u, u);
	}
}

// Kart action apply (see the header).
void ExtZp_Glue_33afa0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint64_t a2w)
{
	uint32_t a2 = (uint32_t)a2w, a2hi = (uint32_t)(a2w >> 32);
	ExtZpMem *m = c->m;
	uint32_t s2 = a0;
	Ps2F f0, f1, f2, f20;
	uint32_t v1, target;
	int32_t cnt;
	(void)a1;
	f1 = a2hi;
	f0 = ZmRF(m, s2 + 0x18u);
	cnt = (int32_t)ZmR32(m, s2 + 0x30u);
	v1 = a2 - ZmR32(m, s2 + 0x14u);
	f2 = f0;
	f0 = Ps2F_FromInt((int32_t)v1);
	f1 = Ps2F_Sub(f1, f2);
	f20 = Ps2F_Add(f0, f1);
	if (0 < cnt)
	{
		int32_t s1 = 0;
		uint32_t s0 = 0u;
		do
		{
			uint32_t s3 = ZmR32(m, ZmR32(m, s2 + 0x2Cu) + s0);
			ExtZp_Glue_190370(c, s3);
			ExtZp_Glue_136050(c, s3);
			ExtZp_Glue_190280(c, s3);
			s1++;
			s0 += 4u;
		} while (s1 < (int32_t)ZmR32(m, s2 + 0x30u));
	}
	target = ZmR32(m, ZmR32(m, s2) + 0x20u);
	if (target == 0x00190670u)
		ExtZp_Kart_190670(c, s2, f20);
	else
		ExtZp_Leave(c, target);
	if (ZmR8(m, s2 + 0x28u) == 0u)
		return;
	{
		Ps2Vec4 g = ZmRV(m, ZmR32(m, STEP_G_WORLD) + 0x30u);
		Ps2Vec4 v2 = {{PS2F_ONE | PS2F_SIGN, 0u, 0u, 0u}}; // -1.0 in lane x
		Ps2Vec4 bc = Ps2V_Bc(&v2, 0);
		int32_t a3;
		Ps2V_Mul(&g, &g, &bc, 0xEu);
		if (!(0 < (int32_t)ZmR32(m, s2 + 0x30u)))
			return;
		a3 = 0;
		do
		{
			uint32_t ent = ZmR32(m, ZmR32(m, s2 + 0x2Cu) + ((uint32_t)a3 << 2));
			uint32_t h = ZmR32(m, ent + 0x94u);
			Ps2F mass = ZmRF(m, h + 0x1B0u);
			Ps2Vec4 tmp = g, mv = {{mass, 0u, 0u, 0u}}, force;
			Ps2Vec4 mb = Ps2V_Bc(&mv, 0);
			Ps2V_Mul(&tmp, &tmp, &mb, 0xEu);
			ent = ZmR32(m, ZmR32(m, s2 + 0x2Cu) + ((uint32_t)a3 << 2));
			h = ZmR32(m, ent + 0x94u);
			force = ZmRV(m, h + 0x80u);
			Ps2V_Add(&force, &force, &tmp, 0xEu);
			ZmWV(m, h + 0x80u, force);
			a3++;
		} while (a3 < (int32_t)ZmR32(m, s2 + 0x30u));
	}
}

// Normalises (seconds, fraction) as the original does inline: fraction into (-1, 1).
static void StepNorm(ExtZpCtx *c, uint32_t *secs, Ps2F *frac)
{
	Ps2F f0;
	if (!Ps2F_Lt(Ps2F_Abs(*frac), A_ONE))
	{
		if (Ps2F_Lt(0u, *frac))
			f0 = ExtZp_Step_43b0b0(c, *frac);
		else
			f0 = ExtZp_Step_43ae78(c, *frac);
		*frac = Ps2F_Sub(*frac, f0);
		*secs += (uint32_t)Ps2F_ToInt(f0);
	}
}

// The range [begin, end) of a list object at sub + off (count at off + 4, table pointer at off + 8),
// read the way the original does (two calls of the pointer slot helper).
static void StepList(ExtZpCtx *c, uint32_t sub, uint32_t off, uint32_t *begin, uint32_t *end)
{
	ExtZpMem *m = c->m;
	uint32_t r1, r2, cnt;
	r1 = ExtZp_Step_345a10(c, sub + off);
	*begin = ZmR32(m, r1);
	r2 = ExtZp_Step_345a10(c, sub + off);
	cnt = ZmR32(m, sub + off + 4u);
	*end = ZmR32(m, r2) + (cnt << 2);
}

// Subspace substep up to the target time.
void ExtZp_Step_3b1b20(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint64_t a2w)
{
	uint32_t a2 = (uint32_t)a2w, a2hi = (uint32_t)(a2w >> 32);
	ExtZpMem *m = c->m;
	uint32_t s6 = a0, s5 = a1;
	int32_t ti = (int32_t)a2;
	Ps2F tf = a2hi;
	int32_t nowi = (int32_t)ZmR32(m, s5 + 0x58u);
	Ps2F f20 = 0u, f21, f22, f23, f1, f0;
	uint32_t s1, s2, v3, det, target, pen = 0u, s4;
	int32_t s0;

	if (ti < nowi)
		v3 = 1u;
	else if (nowi != ti)
		v3 = 0u;
	else
		v3 = Ps2F_Lt(ZmRF(m, s5 + 0x5Cu), tf) ? 0u : 1u;
	if (v3 != 0u)
	{
		// the target is not ahead of the subspace time: nothing to do
		ZmW32(m, s6, (uint32_t)nowi);
		ZmWF(m, s6 + 4u, ZmRF(m, s5 + 0x5Cu));
		return;
	}
	if (ZmR8(m, s5 + 0x14u) == 0u)
	{
		// no simulation in this subspace: the bodies just get the new time
		uint32_t b = ZmR32(m, s5 + 0x38u);
		uint32_t e = b + (ZmR32(m, s5 + 0x34u) << 2);
		if (b != e)
		{
			do
			{
				ExtZp_Step_38eaf0(c, ZmR32(m, b), ((uint64_t)tf << 32) | (uint32_t)ti);
				b += 4u;
			} while (b != e);
		}
		ZmW32(m, s5 + 0x58u, (uint32_t)ti);
		ZmWF(m, s5 + 0x5Cu, tf);
		ZmW32(m, s6, ZmR32(m, s5 + 0x58u));
		ZmWF(m, s6 + 4u, ZmRF(m, s5 + 0x5Cu));
		return;
	}
	f22 = tf;
	det = ZmR32(m, s5 + 0x70u);
	s0 = 0;
	f1 = Ps2F_Sub(f22, ZmRF(m, s5 + 0x5Cu));
	f0 = Ps2F_FromInt((int32_t)((uint32_t)ti - (uint32_t)nowi));
	f21 = Ps2F_Add(f0, f1);
	if (det != 0u)
		(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003771C0u, ZmR32(m, det + 0x14u), 0u, 0u);
	target = ZmR32(m, ZmR32(m, s5) + 0x20u);
	if (target == 0x003B2600u)
		f20 = ExtZp_Step_3b2600(c, s5);
	else
		ExtZp_Leave(c, target);

	for (;;) // until the subspace time has reached the target
	{
		det = ZmR32(m, s5 + 0x70u);
		if (det != 0u)
		{
			uint32_t a1v = ZmR32(m, s5 + 0x74u);
			if (a1v != 0u)
				(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x00377620u, ZmR32(m, det + 0x14u), a1v, 0u);
		}
		// contact solver (slot +0x2C)
		target = ZmR32(m, ZmR32(m, s5) + 0x2Cu);
		if (target == 0x003B1960u)
			(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003B1960u, s5, 0u, f21);
		else
			ExtZp_Leave(c, target);
		do // repeated while the bodies penetrate
		{
			uint32_t pair, tlo, b, e;
			Ps2F thi;
			target = ZmR32(m, ZmR32(m, s5) + 0x24u);
			f20 = Ps2F_Mul(f20, A_HALF);
			if (target == 0x003B2640u)
				ExtZp_Step_3b2640(c, s5, f20);
			else
				ExtZp_Leave(c, target);
			ZmW8(m, s5 + 0x60u, 1u);
			// integrate every system to t + h
			StepList(c, s5, 0x30u, &s1, &s2);
			pair = ExtZp_Push(c, 8u);
			ZmW32(m, pair, ZmR32(m, s5 + 0x58u));
			ZmWF(m, pair + 4u, ZmRF(m, s5 + 0x5Cu));
			ExtZp_Step_3b21e0(c, pair, f21);
			tlo = ZmR32(m, pair);
			thi = ZmRF(m, pair + 4u);
			ExtZp_Pop(c, 8u);
			if (s1 != s2)
			{
				do
				{
					uint32_t sys = ZmR32(m, s1);
					target = ZmR32(m, ZmR32(m, sys) + 0x3Cu);
					if (target == 0x0039ADB0u)
						ExtZp_Integ_39adb0(c, sys, ((uint64_t)thi << 32) | tlo);
					else
						ExtZp_Leave(c, target);
					s1 += 4u;
				} while (s1 != s2);
			}
			ZmW8(m, s5 + 0x60u, 0u);
			// the subspace's actions (list at +0x3C)
			StepList(c, s5, 0x3Cu, &b, &e);
			if (b != e)
			{
				do
				{
					Ps2F f23a = ZmRF(m, s5 + 0x5Cu);
					uint32_t s3 = ZmR32(m, s5 + 0x58u), act;
					f23a = Ps2F_Add(f23a, f21);
					StepNorm(c, &s3, &f23a);
					act = ZmR32(m, b);
					target = ZmR32(m, ZmR32(m, act) + 0x1Cu);
					if (target == 0x0033AFA0u)
						ExtZp_Glue_33afa0(c, act, s5, ((uint64_t)f23a << 32) | s3);
					else
						ExtZp_Leave(c, target);
					b += 4u;
				} while (b != e);
			}
			// derived data of every system
			StepList(c, s5, 0x30u, &b, &e);
			if (b != e)
			{
				do
				{
					uint32_t sys = ZmR32(m, b);
					target = ZmR32(m, ZmR32(m, sys) + 0x40u);
					if (target == 0x0039AD50u)
						ExtZp_Integ_39ad50(c, sys);
					else
						ExtZp_Leave(c, target);
					b += 4u;
				} while (b != e);
			}
			// collision detection (slot +0x28)
			target = ZmR32(m, ZmR32(m, s5) + 0x28u);
			if (target == 0x003B18F0u)
				pen = StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003B18F0u, s5, 0u, 0u).v0 & 0xFFu;
			else
			{
				ExtZp_Leave(c, target);
				pen = 0u;
			}
			v3 = (0u < pen) ? 1u : 0u;
			if (v3 != 0u)
				v3 = Ps2F_Le(f21, A_EPS_STEP) ? 0u : 1u;
			if (v3 != 0u)
				s4 = (s0 < 0x1E) ? 1u : 0u;
			else
				s4 = v3 & 0xFFu;
			if (s4 == 1u)
			{
				// penetration: every system steps back, h is halved (not below 1e-5)
				StepList(c, s5, 0x30u, &b, &e);
				if (b != e)
				{
					do
					{
						uint32_t sys = ZmR32(m, b);
						target = ZmR32(m, ZmR32(m, sys) + 0x38u);
						if (target == 0x0038E3E0u)
							ExtZp_Integ_38e3e0(c, sys);
						else
							ExtZp_Leave(c, target);
						b += 4u;
					} while (b != e);
				}
				f21 = Ps2F_Mul(f21, A_HALF);
				f0 = A_EPS_STEP;
				if (Ps2F_Lt(f21, f0))
					f21 = f0;
				s0++;
			}
		} while (s4 == 1u);
		if (pen != 0u)
		{
			uint32_t d2;
			target = ZmR32(m, ZmR32(m, s5) + 0x28u);
			if (target == 0x003B18F0u)
				(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003B18F0u, s5, 0u, 0u);
			else
				ExtZp_Leave(c, target);
			d2 = ZmR32(m, s5 + 0x70u);
			(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x0036F840u, ZmR32(m, d2 + 0x14u), ZmR32(m, s5 + 0x78u), 0u);
		}
		// forces and controllers (slot +0x4C of every system)
		{
			uint32_t b, e;
			StepList(c, s5, 0x30u, &b, &e);
			if (b != e)
			{
				do
				{
					uint32_t s3 = ZmR32(m, s5 + 0x58u), sys;
					f23 = Ps2F_Add(ZmRF(m, s5 + 0x5Cu), f21);
					StepNorm(c, &s3, &f23);
					sys = ZmR32(m, b);
					target = ZmR32(m, ZmR32(m, sys) + 0x4Cu);
					if (target == 0x0038E7C0u)
						ExtZp_Integ_38e7c0(c, sys, ((uint64_t)f23 << 32) | s3);
					else
						ExtZp_Leave(c, target);
					b += 4u;
				} while (b != e);
			}
		}
		// advance the subspace time by h
		{
			uint32_t secs;
			Ps2F f12 = Ps2F_Add(ZmRF(m, s5 + 0x5Cu), f21);
			ZmWF(m, s5 + 0x5Cu, f12);
			if (!Ps2F_Lt(Ps2F_Abs(f12), A_ONE))
			{
				if (Ps2F_Lt(0u, f12))
					f0 = ExtZp_Step_43b0b0(c, f12);
				else
					f0 = ExtZp_Step_43ae78(c, f12);
				secs = (uint32_t)Ps2F_ToInt(f0);
				f0 = Ps2F_Sub(ZmRF(m, s5 + 0x5Cu), f0);
				ZmWF(m, s5 + 0x5Cu, f0);
				ZmW32(m, s5 + 0x58u, ZmR32(m, s5 + 0x58u) + secs);
			}
		}
		{
			int32_t a1n = (int32_t)ZmR32(m, s5 + 0x58u);
			f1 = ZmRF(m, s5 + 0x5Cu);
			f1 = Ps2F_Sub(f22, f1);
			f21 = Ps2F_Add(Ps2F_FromInt((int32_t)((uint32_t)ti - (uint32_t)a1n)), f1);
			if (Ps2F_Lt(f21, A_EPS_STEP))
			{
				// closer than 1e-5: snap onto the target
				ZmW32(m, s5 + 0x58u, (uint32_t)ti);
				ZmWF(m, s5 + 0x5Cu, f22);
			}
		}
		{
			int32_t a0n = (int32_t)ZmR32(m, s5 + 0x58u);
			if (a0n < ti)
				v3 = 1u;
			else if (a0n != ti)
				v3 = 0u;
			else
				v3 = Ps2F_Lt(ZmRF(m, s5 + 0x5Cu), f22) ? 1u : 0u;
		}
		if (v3 == 0u)
			break;
	}
	det = ZmR32(m, s5 + 0x70u);
	if (det != 0u)
	{
		uint32_t a1v = ZmR32(m, s5 + 0x78u);
		if (a1v != 0u)
			(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x00377290u, ZmR32(m, det + 0x14u), a1v, 0u);
	}
	ZmW32(m, s6, ZmR32(m, s5 + 0x58u));
	ZmWF(m, s6 + 4u, ZmRF(m, s5 + 0x5Cu));
}

// Run-time substep of the subspace subclass (slot +0x30 of the class with the detector): without
// a collision detector it is the generic substep; with one it does a single pass up to the target
// time (no halving): detector tolerance / 2, contact solver, integration, actions, derived data,
// collision (on penetration the class' own correction), forces and controllers (the kart update),
// time += h, tolerance := 0. a0 = result buffer (the subspace time pair afterwards), a1 = subspace,
// a2 = the target time pair.
void ExtZp_Step_3af920(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint64_t a2w)
{
	ExtZpMem *m = c->m;
	uint32_t s2 = a0, s1 = a1;
	int32_t ti = (int32_t)(uint32_t)a2w;
	Ps2F tf = (Ps2F)(a2w >> 32);
	int32_t nowi;
	uint32_t v3, target, b, e, s4;
	Ps2F f20, f21, f0, f1;

	if (ZmR32(m, s1 + 0x70u) == 0u)
	{
		ExtZp_Step_3b1b20(c, a0, a1, a2w);
		return;
	}
	nowi = (int32_t)ZmR32(m, s1 + 0x58u);
	if (ti < nowi)
		v3 = 1u;
	else if (nowi != ti)
		v3 = 0u;
	else
		v3 = Ps2F_Lt(ZmRF(m, s1 + 0x5Cu), tf) ? 0u : 1u;
	if (v3 != 0u)
	{
		ZmW32(m, s2, (uint32_t)nowi);
		ZmWF(m, s2 + 4u, ZmRF(m, s1 + 0x5Cu));
		return;
	}
	if (ZmR8(m, s1 + 0x14u) == 0u)
	{
		// no simulation: the bodies just get the new time
		b = ZmR32(m, s1 + 0x38u);
		e = b + (ZmR32(m, s1 + 0x34u) << 2);
		if (b != e)
		{
			do
			{
				ExtZp_Step_38eaf0(c, ZmR32(m, b), a2w);
				b += 4u;
			} while (b != e);
		}
		ZmW32(m, s1 + 0x58u, (uint32_t)ti);
		ZmWF(m, s1 + 0x5Cu, tf);
	}
	else
	{
		uint32_t det;
		f1 = ZmRF(m, s1 + 0x5Cu);
		f0 = Ps2F_Sub(tf, f1);
		f20 = Ps2F_Add(Ps2F_FromInt((int32_t)((uint32_t)ti - (uint32_t)nowi)), f0);
		target = ZmR32(m, ZmR32(m, s1) + 0x20u);
		if (target == 0x003B2600u)
			f0 = ExtZp_Step_3b2600(c, s1);
		else
		{
			ExtZp_Leave(c, target);
			f0 = 0u;
		}
		f0 = Ps2F_Mul(A_HALF, f0);
		det = ZmR32(m, ZmR32(m, s1 + 0x70u) + 0x14u);
		ZmWF(m, det + 0x18u, f0);
		// contact solver (slot +0x2C)
		target = ZmR32(m, ZmR32(m, s1) + 0x2Cu);
		if (target == 0x003B1960u)
			(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003B1960u, s1, 0u, f20);
		else
			ExtZp_Leave(c, target);
		ZmW8(m, s1 + 0x60u, 1u);
		// integrate every system to t + h
		b = ZmR32(m, s1 + 0x38u);
		e = b + (ZmR32(m, s1 + 0x34u) << 2);
		if (b != e)
		{
			do
			{
				uint32_t sys = ZmR32(m, b);
				f21 = Ps2F_Add(ZmRF(m, s1 + 0x5Cu), f20);
				s4 = ZmR32(m, s1 + 0x58u);
				StepNorm(c, &s4, &f21);
				target = ZmR32(m, ZmR32(m, sys) + 0x3Cu);
				if (target == 0x0039ADB0u)
					ExtZp_Integ_39adb0(c, sys, ((uint64_t)f21 << 32) | s4);
				else
					ExtZp_Leave(c, target);
				b += 4u;
			} while (b != e);
		}
		ZmW8(m, s1 + 0x60u, 0u);
		// actions (list at +0x3C)
		e = ZmR32(m, s1 + 0x40u);
		b = ZmR32(m, s1 + 0x44u);
		e = b + (e << 2);
		if (b != e)
		{
			do
			{
				uint32_t act = ZmR32(m, b);
				f21 = Ps2F_Add(ZmRF(m, s1 + 0x5Cu), f20);
				s4 = ZmR32(m, s1 + 0x58u);
				StepNorm(c, &s4, &f21);
				target = ZmR32(m, ZmR32(m, act) + 0x1Cu);
				if (target == 0x0033AFA0u)
					ExtZp_Glue_33afa0(c, act, s1, ((uint64_t)f21 << 32) | s4);
				else
					ExtZp_Leave(c, target);
				b += 4u;
			} while (b != e);
		}
		// derived data of every system
		e = ZmR32(m, s1 + 0x34u);
		b = ZmR32(m, s1 + 0x38u);
		e = b + (e << 2);
		if (b != e)
		{
			do
			{
				uint32_t sys = ZmR32(m, b);
				target = ZmR32(m, ZmR32(m, sys) + 0x40u);
				if (target == 0x0039AD50u)
					ExtZp_Integ_39ad50(c, sys);
				else
					ExtZp_Leave(c, target);
				b += 4u;
			} while (b != e);
		}
		v3 = ZmR32(m, s1 + 0x70u);
		if (v3 != 0u)
			(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003771C0u, ZmR32(m, v3 + 0x14u), 0u, 0u);
		// collision detection (slot +0x28); on penetration the class' own correction
		target = ZmR32(m, ZmR32(m, s1) + 0x28u);
		if (target == 0x003B18F0u)
			v3 = StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003B18F0u, s1, 0u, 0u).v0 & 0xFFu;
		else
		{
			ExtZp_Leave(c, target);
			v3 = 0u;
		}
		if (v3 != 0u)
			(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x003AFE10u, s1, 0u, 0u);
		// forces and controllers (slot +0x4C of every system)
		e = ZmR32(m, s1 + 0x34u);
		b = ZmR32(m, s1 + 0x38u);
		e = b + (e << 2);
		if (b != e)
		{
			do
			{
				uint32_t sys = ZmR32(m, b);
				f21 = Ps2F_Add(ZmRF(m, s1 + 0x5Cu), f20);
				s4 = ZmR32(m, s1 + 0x58u);
				StepNorm(c, &s4, &f21);
				target = ZmR32(m, ZmR32(m, sys) + 0x4Cu);
				if (target == 0x0038E7C0u)
					ExtZp_Integ_38e7c0(c, sys, ((uint64_t)f21 << 32) | s4);
				else
					ExtZp_Leave(c, target);
				b += 4u;
			} while (b != e);
		}
		// detector pair lists after the step
		v3 = ZmR32(m, s1 + 0x70u);
		if (v3 != 0u)
		{
			uint32_t a1v = ZmR32(m, s1 + 0x78u);
			if (a1v != 0u)
				(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x00377290u, ZmR32(m, v3 + 0x14u), a1v, 0u);
			a1v = ZmR32(m, s1 + 0x74u);
			if (a1v != 0u)
				(void)StepQuery(c, EXT_ZP_Q_COLLIDE, 0x00377620u, ZmR32(m, ZmR32(m, s1 + 0x70u) + 0x14u), a1v, 0u);
		}
		// time += h
		{
			Ps2F f12 = Ps2F_Add(ZmRF(m, s1 + 0x5Cu), f20);
			ZmWF(m, s1 + 0x5Cu, f12);
			if (!Ps2F_Lt(Ps2F_Abs(f12), A_ONE))
			{
				uint32_t whole;
				if (Ps2F_Lt(0u, f12))
					f0 = ExtZp_Step_43b0b0(c, f12);
				else
					f0 = ExtZp_Step_43ae78(c, f12);
				whole = (uint32_t)Ps2F_ToInt(f0);
				f0 = Ps2F_Sub(ZmRF(m, s1 + 0x5Cu), f0);
				ZmWF(m, s1 + 0x5Cu, f0);
				ZmW32(m, s1 + 0x58u, ZmR32(m, s1 + 0x58u) + whole);
			}
		}
	}
	// detector tolerance := 0
	ZmW32(m, ZmR32(m, ZmR32(m, s1 + 0x70u) + 0x14u) + 0x18u, 0u);
	ZmW32(m, s2, ZmR32(m, s1 + 0x58u));
	ZmWF(m, s2 + 4u, ZmRF(m, s1 + 0x5Cu));
}
