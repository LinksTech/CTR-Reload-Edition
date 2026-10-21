// THE ZONE PORT, PART I: the kart states 4 (air) and 5 / 6 (sliding): enter,
// update, exit (see include/platform/native_extzonephys_i.h and
// native_extzonephys.h for the conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_i.h>

#include <string.h>

#define I_G_RAY_WORLD 0x006fa190u // global: the object holding the track query handle at +0x74

#define I_ZERO 0x00000000u
#define I_ONE 0x3F800000u         // 1 (the original's constant)
#define I_NEG_ONE 0xBF800000u     // -1
#define I_HALF 0x3F000000u        // 0.5
#define I_NEG_HALF 0xBF000000u    // -0.5
#define I_TWO 0x40000000u         // 2
#define I_TENTH 0x3DCCCCCDu       // 0.1
#define I_HUNDREDTH 0x3C23D70Au   // 0.01

#define RF(a) ZmRF(m, (a))
#define WF(a, v) ZmWF(m, (a), (v))

// The dot product of the xyz parts of the vectors at a and b, as the main
// CPU's multiply-accumulate sequence computes it (x*x first).
static Ps2F I_Dot3(ExtZpMem *m, uint32_t a, uint32_t b)
{
	Ps2Acc acc = Ps2A_Mula(RF(a), RF(b));
	acc = Ps2A_Madda(acc, RF(a + 4), RF(b + 4));
	return Ps2A_Madd(acc, RF(a + 8), RF(b + 8));
}

// The kart's entity E: the first word of the holder at K+0x2c.
static uint32_t I_Entity(ExtZpMem *m, uint32_t k)
{
	return ZmR32(m, ZmR32(m, k + 0x2c));
}

// The "min then clamp" pair of the slide update: with f1 = the smaller of v
// and hi, the lower bound lo wins when f1 <= lo.
static Ps2F I_Clamp(Ps2F v, Ps2F hi, Ps2F lo)
{
	Ps2F f1 = Ps2F_Lt(v, hi) ? v : hi;
	return Ps2F_Le(f1, lo) ? lo : f1;
}

// vec.xyz *= s (the vector unit's broadcast multiply; w kept).
static void I_ScaleXyz(Ps2Vec4 *v, Ps2F s)
{
	Ps2Vec4 x = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&x, 0);
	Ps2V_Mul(v, v, &bc, 0xEu);
}

// State 4 (air) enter: air time 0; the jump lock is set when the previous
// state was already 4 and the wall flag is set; the "no ground below" flag is
// cleared. Returns 0 (enter accepted).
uint32_t ExtZp_State_192860(ExtZpCtx *c, uint32_t k)
{
	ExtZpMem *m = c->m;
	uint32_t v0;
	ZmW32(m, k + 0x498, 0); // air time
	v0 = (ZmR32(m, k + 0x3e0) ^ 4u) < 1u;
	if (v0 != 0)
		v0 = (0u < ZmR8(m, k + 0x451)); // wall hit flag
	ZmW8(m, k + 0x494, (uint8_t)v0);   // jump lock
	ZmW8(m, k + 0x495, 0);             // no ground below
	return 0;
}

// State 5 / 6 (slide) enter: returns 2 (ride on instead) when the kart is too
// low, the frame is tilted or no steering flag is set; sets the slide side
// flag from the two steering flags; clears the slide counters. Returns 0 when
// the slide starts.
uint32_t ExtZp_State_192890(ExtZpCtx *c, uint32_t k)
{
	ExtZpMem *m = c->m;
	Ps2F f1 = RF(k + 0x42c), f0 = RF(k + 0x13c);
	uint32_t e;
	if (Ps2F_Lt(f1, f0))
		return 2;
	e = I_Entity(m, k);
	f1 = I_Dot3(m, e + 0x10, e + 0x50);
	if (Ps2F_Lt(f1, I_NEG_HALF))
		return 2;
	if (ZmR8(m, k + 0x3f8) != 0)
		ZmW8(m, k + 0x4b1, 1); // slide to one side
	else if (ZmR8(m, k + 0x3fa) != 0)
		ZmW8(m, k + 0x4b1, 0); // slide to the other side
	else
		return 2;
	ZmW32(m, k + 0x49c, 0); // slide time
	ZmW32(m, k + 0x4a0, 0);
	ZmW32(m, k + 0x4a4, 0);
	ZmW32(m, k + 0x4ac, 0);
	ZmW8(m, k + 0x4b0, 1);
	ZmW32(m, k + 0x550, 0);
	return 0;
}

// Stores the slide-out parameters of the kart.
void ExtZp_State_1957e0(ExtZpCtx *c, uint32_t k, uint32_t flag, Ps2F f12, Ps2F f13, Ps2F f14)
{
	ExtZpMem *m = c->m;
	WF(k + 0x480, f13);
	WF(k + 0x484, f12);
	ZmW8(m, k + 0x48c, (uint8_t)flag);
	WF(k + 0x488, f14);
}

// Clears the turn request: value 0, flag 1.
void ExtZp_State_195cd0(ExtZpCtx *c, uint32_t k)
{
	ExtZpMem *m = c->m;
	ZmW32(m, k + 0x4ec, 0);
	ZmW8(m, k + 0x4f0, 1);
}

// Sets the turn request (value f12, flag, direction vec3), or clears it when the value is 0.
void ExtZp_State_195c80(ExtZpCtx *c, uint32_t k, uint32_t vec3, uint32_t flag, Ps2F f12)
{
	ExtZpMem *m = c->m;
	if (Ps2F_Eq(I_ZERO, f12))
	{
		ExtZp_State_195cd0(c, k);
		return;
	}
	{
		Ps2F f2 = RF(vec3), f1 = RF(vec3 + 4), f0 = RF(vec3 + 8);
		WF(k + 0x4ec, f12);
		ZmW8(m, k + 0x4f0, (uint8_t)flag);
		WF(k + 0x4f4, f2);
		WF(k + 0x4f8, f1);
		WF(k + 0x4fc, f0);
	}
}

// The slide progress: in the slide states (5, 6) the slide time over the stage
// length while the stage counter is below its limit, else 0; in states 1 and
// 0xd the stored value (+0x4e0); else 0.
Ps2F ExtZp_State_198900(ExtZpCtx *c, uint32_t k)
{
	ExtZpMem *m = c->m;
	int32_t v1 = (int32_t)ZmR32(m, k + 0x3e0);
	if (v1 == 6 || v1 == 5)
	{
		Ps2F f1, f0;
		v1 = (int32_t)ZmR32(m, k + 0x4a0);
		if (!(v1 < (int32_t)ZmR32(m, k + 0x144)))
			return I_ZERO;
		f1 = RF(k + 0x140);
		f0 = RF(k + 0x49c);
		if (!Ps2F_Le(f0, f1))
			return I_ZERO;
		return Ps2F_Div(f0, f1);
	}
	if (v1 == 1 || v1 == 0xd)
		return RF(k + 0x4e0);
	return I_ZERO;
}

// State 4 (air) exit: the landing boost when the wish (+0x490) is set: stage 2
// when the air time is not below +0x12c, stage 1 when not below +0x130, none
// below +0x134, else stage 0 (all with intensity 1). Clears the wish and the
// "no ground" flag. Returns 1.
uint32_t ExtZp_State_194d20(ExtZpCtx *c, uint32_t k)
{
	ExtZpMem *m = c->m;
	if (!Ps2F_Le(RF(k + 0x490), I_ZERO))
	{
		Ps2F f1 = RF(k + 0x498);
		if (!Ps2F_Lt(f1, RF(k + 0x12c)))
			ExtZp_Kart_196890(c, k, 2, I_ONE);
		else if (!Ps2F_Lt(f1, RF(k + 0x130)))
			ExtZp_Kart_196890(c, k, 1, I_ONE);
		else if (!Ps2F_Lt(f1, RF(k + 0x134)))
			ExtZp_Kart_196890(c, k, 0, I_ONE);
	}
	ZmW32(m, k + 0x490, 0);
	ZmW8(m, k + 0x495, 0);
	return 1;
}

// State 5 / 6 (slide) exit: the angle between the frame and the track normal
// frame (signed), scaled by a blend of the two angle limits, becomes the
// slide-out amount; starts the turn request. Returns 1.
uint32_t ExtZp_State_194dd0(ExtZpCtx *c, uint32_t k)
{
	ExtZpMem *m = c->m;
	Ps2F f20, f0, f1, f2, f3, f4, f14, f12;
	uint32_t e, v30;
	if (ZmR8(m, k + 0x450) != 0 || (int32_t)ZmR32(m, k + 0x3e0) == 7)
		return 1;
	e = I_Entity(m, k);
	f12 = I_Dot3(m, e + 0x10, e + 0x50);
	f0 = ExtZp_Math_43bc40(c, f12);
	e = I_Entity(m, k);
	f20 = f0;
	f2 = I_Dot3(m, e + 0x30, e + 0x50);
	if (!Ps2F_Le(f2, I_ZERO))
		f20 = Ps2F_Neg(f0);
	f4 = RF(k + 0x4a8);
	f3 = Ps2F_Neg(f4);
	if (!Ps2F_Le(f4, I_ZERO))
	{
		f3 = RF(k + 0x15c);
		f2 = RF(k + 0x160);
		f1 = RF(k + 0x168);
		f0 = RF(k + 0x16c);
		f2 = Ps2F_Sub(f2, f3);
		f0 = Ps2F_Sub(f0, f1);
		f2 = Ps2F_Mul(f4, f2);
		f0 = Ps2F_Mul(f4, f0);
		f2 = Ps2F_Add(f3, f2);
		f14 = Ps2F_Add(f1, f0);
	}
	else
	{
		f4 = RF(k + 0x15c);
		f2 = RF(k + 0x158);
		f1 = RF(k + 0x168);
		f0 = RF(k + 0x164);
		f2 = Ps2F_Sub(f2, f4);
		f0 = Ps2F_Sub(f0, f1);
		f2 = Ps2F_Mul(f3, f2);
		f0 = Ps2F_Mul(f3, f0);
		f2 = Ps2F_Add(f4, f2);
		f14 = Ps2F_Add(f1, f0);
	}
	f20 = Ps2F_Mul(f20, f2);
	ExtZp_State_1957e0(c, k, 1, RF(k + 0x154), f20, f14);
	v30 = ExtZp_Push(c, 16); // sp+0x30: a zero vector passed on
	ZmW32(m, v30, 0);
	ZmW32(m, v30 + 4, 0);
	ZmW32(m, v30 + 8, 0);
	if (Ps2F_Lt(f20, I_ZERO))
		f20 = Ps2F_Neg(f20);
	f0 = RF(k + 0x154);
	f12 = Ps2F_Div(f20, f0);
	ExtZp_State_195c80(c, k, v30, 0, f12);
	ExtZp_Pop(c, 16);
	return 1;
}

// State 4 (air): turns the kart's frame by a rotation about an axis (built
// from the frame's up row and the half angle), blends the speed and adds a
// damping impulse.
void ExtZp_State_195410(ExtZpCtx *c, uint32_t k, Ps2F dt)
{
	ExtZpMem *m = c->m;
	const uint32_t fr = ExtZp_Push(c, 0x120); // the original's frame; its offsets are kept
	Ps2F f20 = I_TWO, f21, f22, f23, f24 = I_HALF;
	Ps2F f0, f1, f2, f3, f12 = dt;
	uint32_t e, e2, s1, s2, body;

	f3 = RF(k + 0x3e4);
	f2 = RF(k + 0x47c);
	f1 = RF(k + 0x3f4);
	f2 = Ps2F_Mul(f2, f3);
	if (Ps2F_Le(f1, I_ZERO))
	{
		f1 = RF(k + 0xc8);
		f0 = RF(k + 0x3ec);
		f0 = Ps2F_Mul(f1, f0);
		f0 = Ps2F_Mul(f3, f0);
		f23 = Ps2F_Add(f2, f0);
	}
	else
	{
		f0 = RF(k + 0xc4);
		f0 = Ps2F_Mul(f0, f1);
		f0 = Ps2F_Mul(f3, f0);
		f23 = Ps2F_Add(f2, f0);
	}
	f0 = RF(k + 0x42c);
	if (!Ps2F_Le(f0, f20))
	{
		e = I_Entity(m, k);
		f1 = I_Dot3(m, e + 0x10, e + 0x50);
		if (Ps2F_Lt(f1, I_NEG_HALF))
			f23 = Ps2F_Mul(f23, I_NEG_ONE);
	}
	f1 = RF(k + 0x480);
	f22 = Ps2F_Mul(f23, f12);
	if (Ps2F_Lt(f1, I_ZERO))
		f1 = Ps2F_Neg(f1);
	if (!Ps2F_Le(f1, I_HUNDREDTH))
	{
		Ps2F f0b = RF(k + 0x484);
		f21 = RF(k + 0x480);
		f2 = Ps2F_Mul(f0b, f12);
		f1 = Ps2F_Lt(f21, f2) ? f21 : f2;
		f0 = Ps2F_Neg(f0b);
		f0 = Ps2F_Mul(f0, f12);
		if (Ps2F_Le(f1, f0))
			f21 = f0;
		else
			f21 = Ps2F_Lt(f21, f2) ? f21 : f2;
		f0 = Ps2F_Div(f21, f12);
		f23 = Ps2F_Sub(f23, f0);
		f0 = RF(k + 0x480);
		f0 = Ps2F_Sub(f0, f21);
		WF(k + 0x480, f0);
		if (ZmR8(m, k + 0x48c) != 0)
		{
			f0 = ExtZp_Math_43b068(c, RF(k + 0x488));
			if (!Ps2F_Le(f0, I_ZERO))
			{
				f0 = RF(k + 0x488);
				f0 = Ps2F_Mul(f21, f0);
				f22 = Ps2F_Add(f22, f0);
			}
		}
	}
	// the rotation axis: the frame's up row scaled by sin(half the angle); cos(half) kept at +0x100
	{
		uint32_t p = ZmR32(m, k + 0x2c);
		e = ZmR32(m, p);
		f21 = I_ONE;
		s1 = fr + 0x110;
		f12 = Ps2F_Mul(f24, f22);
		s2 = e + 0x20;
		ZmW32(m, fr + 0x11c, I_ONE);
		ZmW32(m, fr + 0x118, 0);
		ZmW32(m, fr + 0x114, 0);
		ZmW32(m, s1, 0);
		f0 = ExtZp_Math_43af80(c, f12);
		ZmW32(m, fr + 0x100, f0);
		ZmWV(m, s1, ZmRV(m, s2));
		f12 = Ps2F_Mul(f24, f22);
		f0 = ExtZp_Math_43b320(c, f12);
		{
			Ps2Vec4 v = ZmRV(m, s1);
			I_ScaleXyz(&v, f0);
			ZmWV(m, s1, v);
		}
	}
	e = I_Entity(m, k);
	s2 = e + 0x40;
	ExtZp_Kart_1207f0(c, fr + 0xc0);
	f1 = RF(fr + 0x100);
	f0 = Ps2F_Mul(f20, f1);
	f0 = Ps2F_Mul(f0, f1);
	ExtZp_Integ_1207c0(c, fr + 0xb0, s2, Ps2F_Sub(f0, f21));
	ExtZp_Integ_1207b0(c, fr + 0xc0, fr + 0xb0);
	f0 = ExtZp_Integ_120780(c, s1, s2);
	ExtZp_Integ_120750(c, fr + 0x80, s1, f0);
	ExtZp_Kart_120720(c, fr + 0x60, s1, s2);
	ExtZp_Integ_120750(c, fr + 0x70, fr + 0x60, RF(fr + 0x100));
	ExtZp_Integ_1206f0(c, fr + 0x90, fr + 0x80, fr + 0x70);
	ExtZp_Integ_120750(c, fr + 0xa0, fr + 0x90, I_TWO);
	ExtZp_Kart_1206d0(c, fr + 0xc0, fr + 0xa0);
	ExtZp_Integ_1206c0(c, fr + 0xd0, fr + 0xc0);
	ZmWV(m, fr + 0xf0, ZmRV(m, fr + 0xd0));
	e2 = ZmR32(m, k + 0x2c);
	s1 = ZmR32(m, e2);
	ExtZp_Kart_136160(c, fr + 0xf0);
	body = ZmR32(m, s1 + 0x94);
	ExtZp_Kart_13dd40(c, body, fr + 0xf0);
	ExtZp_Glue_136050(c, s1);
	f0 = ExtZp_Math_43b068(c, f23);
	f1 = I_ZERO;
	if (!Ps2F_Le(f0, f1))
	{
		Ps2Vec4 v, w;
		uint32_t a0 = fr + 0xe0;
		ZmW32(m, fr + 0xec, f21);
		ZmW32(m, fr + 0xe8, f1);
		ZmW32(m, fr + 0xe4, f1);
		ZmW32(m, fr + 0xe0, f1);
		e = I_Entity(m, k);
		ZmWV(m, a0, ZmRV(m, e + 0x20));
		v = ZmRV(m, a0);
		I_ScaleXyz(&v, f23);
		ZmWV(m, a0, v);
		e = I_Entity(m, k);
		w = ZmRV(m, e + 0x70);
		v = ZmRV(m, a0);
		Ps2V_Add(&v, &v, &w, 0xEu);
		ZmWV(m, a0, v);
		e2 = ZmR32(m, k + 0x2c);
		s1 = ZmR32(m, e2);
		ExtZp_Kart_136160(c, a0);
		ExtZp_Kart_190350(c, ZmR32(m, s1 + 0x94), a0);
		ExtZp_Glue_190280(c, s1);
	}
	ExtZp_Pop(c, 0x120);
}

// State 4 (air) update for one time step dt.
void ExtZp_State_1936b0(ExtZpCtx *c, uint32_t k, Ps2F dt)
{
	ExtZpMem *m = c->m;
	const uint32_t fr = ExtZp_Push(c, 0x40); // sp+0x30 .. sp+0x6f of the original
	const uint32_t p30 = fr, p40 = fr + 0x10, p50 = fr + 0x20, p60 = fr + 0x30;
	Ps2F f0, f1;
	uint32_t e;

	if (ZmR8(m, k + 0x402) != 0)
	{
		if (ZmR8(m, k + 0x3f8) == 0 && ZmR8(m, k + 0x3fa) == 0)
			ExtZp_Kart_1911b0(c, k, 2, 0);
		else
		{
			f1 = RF(k + 0x498);
			if (Ps2F_Le(f1, I_TENTH))
				ExtZp_Kart_1911b0(c, k, 2, 0);
			else
			{
				f0 = RF(k + 0x138);
				f1 = RF(k + 0x3e8);
				if (!Ps2F_Lt(f1, f0))
					ExtZp_Kart_1911b0(c, k, 6, 0);
				else
				{
					f0 = Ps2F_Neg(f0);
					if (Ps2F_Le(f1, f0))
						ExtZp_Kart_1911b0(c, k, 5, 0);
					else
						ExtZp_Kart_1911b0(c, k, 2, 0);
				}
			}
		}
	}
	f0 = RF(k + 0x498);
	f0 = Ps2F_Add(f0, dt);
	WF(k + 0x498, f0);
	ExtZp_State_195e50(c, k);
	ExtZp_State_196b40(c, k, dt);
	ExtZp_State_195410(c, k, dt);
	ZmW32(m, p60, ZmR32(m, k + 0x100));
	ZmW32(m, p60 + 4, ZmR32(m, k + 0x104));
	ZmW32(m, p60 + 8, ZmR32(m, k + 0x108));
	f1 = RF(k + 0x42c);
	f0 = RF(k + 0xfc);
	if (Ps2F_Le(f1, f0))
		ZmW32(m, p60 + 4, 0);
	ExtZp_State_195900(c, k, p60, dt);
	if (ZmR8(m, k + 0x494) == 0 && ZmR8(m, k + 0x495) == 0)
	{
		uint32_t world = ZmR32(m, I_G_RAY_WORLD);
		uint32_t a0 = ZmR32(m, world + 0x74);
		Ps2Vec4 w, pos;
		ExtZpQueryArgs args;
		ExtZpQueryResult r;
		e = I_Entity(m, k);
		ZmWV(m, p50, ZmRV(m, e + 0x20));
		f0 = Ps2F_Neg(RF(k + 0x310));
		w = ZmRV(m, p50);
		I_ScaleXyz(&w, f0);
		ZmWV(m, p50, w);
		e = I_Entity(m, k);
		ZmWV(m, p40, ZmRV(m, ZmR32(m, e + 0x94) + 0x30));
		w = ZmRV(m, p50);
		pos = ZmRV(m, p40);
		Ps2V_Add(&w, &w, &pos, 0xEu);
		ZmWV(m, p50, w);
		e = I_Entity(m, k);
		ZmWV(m, p30, ZmRV(m, ZmR32(m, e + 0x94) + 0x30));
		memset(&args, 0, sizeof(args));
		args.func = 0x338700u; // the track ray test
		args.a[0] = a0;
		args.a[1] = p30;
		args.a[2] = p50;
		args.a[3] = 0;
		args.a[4] = 0;
		r = ExtZp_Query(c, EXT_ZP_Q_RAY, &args);
		if (r.v0 == 0)
			ZmW8(m, k + 0x495, 1); // no ground below
	}
	ExtZp_State_197300(c, k, dt);
	ExtZp_State_197870(c, k, k + 0x510);
	ExtZp_Pop(c, 0x40);
}

// State 5 / 6 (slide) update for one time step dt.
void ExtZp_State_1938d0(ExtZpCtx *c, uint32_t k, Ps2F dt)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3, f4, f20, f22;
	uint32_t e, s1, body;
	uint32_t v1;

	v1 = ZmR8(m, k + 0x4b1);
	if (v1 != 0 ? ZmR8(m, k + 0x3f8) == 0 : ZmR8(m, k + 0x3fa) == 0)
		ExtZp_Kart_1911b0(c, k, 2, 0);
	f1 = RF(k + 0x42c);
	f0 = RF(k + 0x13c);
	if (Ps2F_Lt(f1, f0))
		ExtZp_Kart_1911b0(c, k, 2, 0);
	f0 = RF(k + 0x3f4);
	f20 = I_TENTH;
	if (!Ps2F_Le(f0, f20))
		ExtZp_Kart_1911b0(c, k, 2, 0);
	f2 = RF(k + 0x3e8);
	f1 = Ps2F_Lt(f2, I_ZERO) ? Ps2F_Neg(f2) : f2;
	if (!Ps2F_Le(f1, I_TENTH))
	{
		f1 = RF(k + 0x4ac);
		f0 = Ps2F_Mul(dt, f2);
		f0 = Ps2F_Add(f1, f0);
		WF(k + 0x4ac, f0);
	}
	else
	{
		f0 = RF(k + 0x148);
		f1 = RF(k + 0x4ac);
		f2 = Ps2F_Mul(f0, dt);
		f0 = Ps2F_Neg(f2);
		if (Ps2F_Lt(f1, f0))
		{
			f0 = RF(k + 0x4ac);
			f0 = Ps2F_Add(f0, f2);
			WF(k + 0x4ac, f0);
		}
		else if (!Ps2F_Le(f1, f2))
		{
			f0 = RF(k + 0x4ac);
			f0 = Ps2F_Sub(f0, f2);
			WF(k + 0x4ac, f0);
		}
		else
			ZmW32(m, k + 0x4ac, 0);
	}
	f0 = ExtZp_Math_43b068(c, RF(k + 0x4ac));
	f1 = RF(k + 0x14c);
	if (!Ps2F_Le(f0, f1))
	{
		int32_t st = (int32_t)ZmR32(m, k + 0x3e0);
		if (st == 5)
		{
			f1 = RF(k + 0x4ac);
			if (Ps2F_Lt(f1, I_ZERO))
				ExtZp_Kart_1911b0(c, k, 7, 0);
			else
				ExtZp_Kart_1911b0(c, k, 2, 0);
		}
		else if (st == 6)
		{
			f1 = RF(k + 0x4ac);
			if (Ps2F_Le(f1, I_ZERO))
				ExtZp_Kart_1911b0(c, k, 2, 0);
			else
				ExtZp_Kart_1911b0(c, k, 7, 0);
		}
		else
			ExtZp_Kart_1911b0(c, k, 2, 0);
	}
	f0 = RF(k + 0x49c);
	f0 = Ps2F_Add(f0, dt);
	WF(k + 0x49c, f0);
	if (ZmR8(m, k + 0x3f9) != 0 || ZmR8(m, k + 0x3fb) != 0)
	{
		int32_t a = (int32_t)ZmR32(m, k + 0x4a0), b = (int32_t)ZmR32(m, k + 0x144);
		if (a < b)
		{
			f1 = RF(k + 0x49c);
			f0 = RF(k + 0x140);
			if (Ps2F_Le(f1, f0))
			{
				f0 = ExtZp_State_198900(c, k);
				f2 = I_HALF;
				f1 = I_TWO;
				f0 = Ps2F_Sub(f0, f2);
				f3 = Ps2F_Mul(f1, f0);
				if (!Ps2F_Le(f3, I_ZERO))
				{
					Ps2Acc acc;
					Ps2F f12;
					f2 = RF(k + 0x170);
					v1 = ZmR32(m, k + 0x4a4);
					f1 = RF(k + 0x174);
					f0 = RF(k + 0x178);
					f2 = Ps2F_Mul(f2, f3);
					acc = Ps2A_Mula(f3, f2);
					f1 = Ps2A_Madd(acc, f1, f3);
					f12 = Ps2F_Add(f0, f1);
					if (v1 == 2)
						ExtZp_Kart_196890(c, k, 5, f12);
					else if (v1 == 1)
						ExtZp_Kart_196890(c, k, 4, f12);
					else if (v1 == 0)
						ExtZp_Kart_196890(c, k, 3, f12);
					ZmW32(m, k + 0x4a4, ZmR32(m, k + 0x4a4) + 1u);
					ZmW32(m, k + 0x4ac, 0);
				}
				ZmW32(m, k + 0x4a0, ZmR32(m, k + 0x4a0) + 1u);
				ZmW32(m, k + 0x49c, 0);
			}
		}
	}
	ExtZp_State_195e50(c, k);
	ExtZp_State_196280(c, k);
	f1 = RF(k + 0x3ec);
	if (!Ps2F_Le(f1, I_ZERO))
	{
		Ps2Vec4 va;
		e = I_Entity(m, k);
		f0 = ExtZp_State_197d50(c, k, RF(e + 0x58));
		f4 = RF(k + 0x3ec);
		f3 = RF(k + 0x474);
		f2 = RF(k + 0x42c);
		f1 = RF(k + 0x470);
		f3 = Ps2F_Mul(f4, f3);
		f3 = Ps2F_Mul(f3, f0);
		f0 = Ps2F_Sub(f1, f2);
		f1 = Ps2F_Mul(f3, dt);
		if (!Ps2F_Lt(f1, f0))
			f1 = f0;
		e = I_Entity(m, k);
		body = ZmR32(m, e + 0x94);
		f0 = RF(body + 0x1b0);
		f0 = Ps2F_Mul(f1, f0);
		if (!Ps2F_Le(f0, I_ZERO))
		{
			Ps2F f6, f7, f8;
			va = ZmRV(m, e + 0x50);
			if (ZmR8(m, k + 0x428) == 0)
			{
				va.v[2] = I_ZERO;
				f6 = va.v[0];
				f7 = va.v[1];
				f8 = va.v[2];
				{
					Ps2Acc acc = Ps2A_Mula(f6, f6);
					acc = Ps2A_Madda(acc, f7, f7);
					f1 = Ps2A_Madd(acc, f8, f8);
				}
				if (!Ps2F_Le(f1, f20))
				{
					Ps2Acc acc = Ps2A_Mula(f6, f6);
					acc = Ps2A_Madda(acc, f7, f7);
					f4 = Ps2A_Madd(acc, f8, f8);
					if (!Ps2F_Eq(f4, I_ZERO))
					{
						f4 = Ps2F_Sqrt(f4);
						f3 = va.v[0];
						f2 = va.v[1];
						f1 = va.v[2];
						va.v[0] = Ps2F_Div(f3, f4);
						va.v[1] = Ps2F_Div(f2, f4);
						va.v[2] = Ps2F_Div(f1, f4);
					}
				}
			}
			f0 = Ps2F_Div(f0, dt);
			I_ScaleXyz(&va, f0);
			e = I_Entity(m, k);
			{
				uint32_t t = ZmR32(m, e + 0x94) + 0x80u;
				Ps2Vec4 vel = ZmRV(m, t);
				Ps2V_Add(&vel, &vel, &va, 0xEu);
				ZmWV(m, t, vel);
			}
		}
	}
	// the angle between the frame and the track normal frame, signed
	e = I_Entity(m, k);
	f0 = ExtZp_Math_43bc40(c, I_Dot3(m, e + 0x10, e + 0x50));
	e = I_Entity(m, k);
	f20 = f0;
	f2 = I_Dot3(m, e + 0x30, e + 0x50);
	if (!Ps2F_Le(f2, I_ZERO))
		f20 = Ps2F_Neg(f0);
	f22 = RF(k + 0x17c);
	f0 = ExtZp_Math_43b068(c, f20);
	if (Ps2F_Lt(f0, f22))
		f0 = ExtZp_Math_43b068(c, f20);
	else
		f0 = f22;
	f1 = RF(k + 0x180);
	if (Ps2F_Le(f0, f1))
		f0 = f1;
	else
	{
		f22 = RF(k + 0x17c);
		f0 = ExtZp_Math_43b068(c, f20);
		if (Ps2F_Lt(f0, f22))
			f0 = ExtZp_Math_43b068(c, f20);
		else
			f0 = f22;
	}
	WF(k + 0x4a8, f0);
	f4 = RF(k + 0x180);
	f3 = RF(k + 0x4a8);
	f0 = RF(k + 0x17c);
	f3 = Ps2F_Sub(f3, f4);
	f0 = Ps2F_Sub(f0, f4);
	f0 = Ps2F_Div(f3, f0);
	WF(k + 0x4a8, f0);
	f0 = Ps2F_Mul(I_TWO, f0);
	f0 = Ps2F_Sub(f0, I_ONE);
	WF(k + 0x4a8, f0);
	ExtZp_State_196b40(c, k, dt);
	// the slide turn limit
	if ((int32_t)ZmR32(m, k + 0x3e0) == 6)
	{
		f1 = RF(k + 0x3e4);
		f2 = I_HALF;
		f3 = RF(k + 0x180);
		f0 = RF(k + 0x17c);
		v1 = ZmR8(m, k + 0x4b0);
		f1 = Ps2F_Mul(f2, f1);
		f1 = Ps2F_Add(f2, f1);
		f0 = Ps2F_Sub(f0, f3);
		f0 = Ps2F_Mul(f1, f0);
		f0 = Ps2F_Add(f3, f0);
		f0 = Ps2F_Neg(f0);
		if (v1 != 0)
		{
			f20 = Ps2F_Sub(f20, f0);
			f3 = RF(k + 0x150);
			f0 = RF(k + 0x188);
			f0 = Ps2F_Add(f0, f3);
			f2 = Ps2F_Mul(dt, f0);
			f0 = RF(k + 0x184);
			f0 = Ps2F_Add(f0, f3);
			f0 = Ps2F_Neg(f0);
			f0 = Ps2F_Mul(dt, f0);
			f20 = I_Clamp(f20, f2, f0);
			f0 = ExtZp_Math_43b068(c, f20);
			f1 = RF(k + 0x150);
			f1 = Ps2F_Mul(f1, dt);
			if (Ps2F_Lt(f0, f1))
				ZmW8(m, k + 0x4b0, 0);
		}
		else
		{
			f20 = Ps2F_Sub(f20, f0);
			f0 = RF(k + 0x188);
			f2 = Ps2F_Mul(f0, dt);
			f0 = RF(k + 0x184);
			f0 = Ps2F_Neg(f0);
			f0 = Ps2F_Mul(f0, dt);
			f20 = I_Clamp(f20, f2, f0);
		}
	}
	else
	{
		f2 = RF(k + 0x3e4);
		f3 = I_NEG_HALF;
		f4 = RF(k + 0x180);
		f0 = RF(k + 0x17c);
		f1 = I_HALF;
		f2 = Ps2F_Mul(f3, f2);
		v1 = ZmR8(m, k + 0x4b0);
		f1 = Ps2F_Add(f1, f2);
		f0 = Ps2F_Sub(f0, f4);
		f0 = Ps2F_Mul(f1, f0);
		f0 = Ps2F_Add(f4, f0);
		if (v1 != 0)
		{
			f20 = Ps2F_Sub(f20, f0);
			f3 = RF(k + 0x150);
			f0 = RF(k + 0x184);
			f0 = Ps2F_Add(f0, f3);
			f2 = Ps2F_Mul(dt, f0);
			f0 = RF(k + 0x188);
			f0 = Ps2F_Add(f0, f3);
			f0 = Ps2F_Neg(f0);
			f0 = Ps2F_Mul(dt, f0);
			f20 = I_Clamp(f20, f2, f0);
			f0 = ExtZp_Math_43b068(c, f20);
			f1 = RF(k + 0x150);
			f1 = Ps2F_Mul(f1, dt);
			if (Ps2F_Lt(f0, f1))
				ZmW8(m, k + 0x4b0, 0);
		}
		else
		{
			f20 = Ps2F_Sub(f20, f0);
			f0 = RF(k + 0x184);
			f2 = Ps2F_Mul(f0, dt);
			f0 = RF(k + 0x188);
			f0 = Ps2F_Neg(f0);
			f0 = Ps2F_Mul(f0, dt);
			f20 = I_Clamp(f20, f2, f0);
		}
	}
	// the turn becomes an angular velocity impulse about the frame's up row
	{
		const uint32_t fr = ExtZp_Push(c, 0x40); // sp+0x40 (vector) and sp+0x60 (3 floats)
		const uint32_t v40 = fr, v60 = fr + 0x20;
		Ps2Vec4 v, w;
		uint32_t a2;
		ZmW32(m, v40 + 8, 0);
		ZmW32(m, v40 + 12, I_ONE);
		ZmW32(m, v40 + 4, 0);
		ZmW32(m, v40, 0);
		e = I_Entity(m, k);
		ZmWV(m, v40, ZmRV(m, e + 0x20));
		f0 = Ps2F_Div(f20, dt);
		v = ZmRV(m, v40);
		I_ScaleXyz(&v, f0);
		ZmWV(m, v40, v);
		e = I_Entity(m, k);
		w = ZmRV(m, e + 0x70);
		v = ZmRV(m, v40);
		Ps2V_Add(&v, &v, &w, 0xEu);
		ZmWV(m, v40, v);
		e = I_Entity(m, k);
		s1 = e;
		a2 = ExtZp_Kart_136160(c, v40);
		ExtZp_Kart_190350(c, ZmR32(m, s1 + 0x94), a2);
		ExtZp_Glue_190280(c, s1);
		f2 = RF(k + 0x4a8);
		if (Ps2F_Le(f2, I_ZERO))
		{
			f1 = RF(k + 0x198);
			f0 = RF(k + 0x18c);
			f2 = Ps2F_Neg(f2);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f0 = Ps2F_Add(f1, f0);
			WF(v60, f0);
			f1 = RF(k + 0x19c);
			f0 = RF(k + 0x190);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f0 = Ps2F_Add(f1, f0);
			WF(v60 + 4, f0);
			f1 = RF(k + 0x1a0);
			f0 = RF(k + 0x194);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f0 = Ps2F_Add(f1, f0);
			WF(v60 + 8, f0);
		}
		else
		{
			f1 = RF(k + 0x198);
			f0 = RF(k + 0x1a4);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f0 = Ps2F_Add(f1, f0);
			WF(v60, f0);
			f1 = RF(k + 0x19c);
			f0 = RF(k + 0x1a8);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f0 = Ps2F_Add(f1, f0);
			WF(v60 + 4, f0);
			f1 = RF(k + 0x1a0);
			f0 = RF(k + 0x1ac);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f0 = Ps2F_Add(f1, f0);
			WF(v60 + 8, f0);
		}
		{
			uint32_t v0 = ZmR32(m, k + 0x424);
			f1 = RF(v60 + 4);
			f2 = RF(v60);
			f0 = RF(v60 + 8);
			f3 = RF(v0 + 0x20);
			f2 = Ps2F_Mul(f2, f3);
			f1 = Ps2F_Mul(f1, f3);
			f0 = Ps2F_Mul(f0, f3);
			WF(v60, f2);
			WF(v60 + 4, f1);
			WF(v60 + 8, f0);
		}
		ExtZp_State_195900(c, k, v60, dt);
		ExtZp_Pop(c, 0x40);
	}
	ExtZp_State_197870(c, k, k + 0x510);
}
