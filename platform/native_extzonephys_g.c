// THE ZONE PORT, PART G: the driving state of a kart (states 2 and 3) and its
// friction / grip helpers (see include/platform/native_extzonephys.h for the
// conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_g.h>

// Fields of the kart object K used by this part.
enum
{
	G_K_FRAME = 0x2C,        // ptr: pointer to the frame pointer (frame = **(K + 0x2C))
	G_K_OWNER = 0x38,        // ptr: owner object (may be 0); owner + 0x88 = effect bit field
	G_K_STATE = 0x3E0,       // s32: driving state (2 forward, 3 backward)
	G_K_SPEED = 0x42C,       // float: speed
	G_K_SURF = 0x424,        // ptr: surface factor table
	G_F_ROWS = 0x10,         // frame: row "forward" (vec4)
	G_F_VELDIR = 0x50,       // frame: unit vector of the velocity (vec4)
	G_F_BODY = 0x94,         // frame: pointer to the rigid body
	G_B_FORCE = 0x80,        // body: force accumulator (vec4)
	G_B_MASS = 0x1B0,        // body: mass
};

// ------------------------------------------------------------------ small shared pieces
// The frame object of a kart: **(K + 0x2C).
static uint32_t g_Frame(ExtZpCtx *c, uint32_t k)
{
	return ZmR32(c->m, ZmR32(c->m, k + G_K_FRAME));
}
// The rigid body of a kart: frame + 0x94.
static uint32_t g_Body(ExtZpCtx *c, uint32_t k)
{
	return ZmR32(c->m, g_Frame(c, k) + G_F_BODY);
}
// body.force.xyz += v.xyz * s  (vmulx.xyz then vadd.xyz; w of both kept).
static void g_ForceAddScaled(ExtZpCtx *c, uint32_t k, Ps2Vec4 v, Ps2F s)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 sv = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&sv, 0);
	Ps2Vec4 f;
	uint32_t at;
	Ps2V_Mul(&v, &v, &bc, 0xEu);
	at = g_Body(c, k) + G_B_FORCE;
	f = ZmRV(m, at);
	Ps2V_Add(&f, &f, &v, 0xEu);
	ZmWV(m, at, f);
}

// Returns f0 = the lower speed limit (K+0x2E4) when the speed is below it, else the
// upper one (K+0x2E8); the caller does not use the value.
Ps2F ExtZp_State_195e20(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	Ps2F speed = ZmRF(m, kart + G_K_SPEED);
	Ps2F f0 = ZmRF(m, kart + 0x2E4u); // lower speed limit
	if (Ps2F_Lt(speed, f0))
	{
		return f0;
	}
	f0 = ZmRF(m, kart + 0x2E8u); // upper speed limit
	return f0;
}

// Spin damping. When the speed (K+0x42C) is above K+0xE8: f2 = (speed - K+0xE8) * |K+0x3E4|;
// if f2 > 0 the body gets the force  -min(f2 * K+0xEC, K+0xF0) * mass  along the velocity
// direction (frame + 0x50).
void ExtZp_State_195820(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	Ps2F f1 = ZmRF(m, kart + 0xE8u); // threshold speed
	Ps2F f0 = ZmRF(m, kart + G_K_SPEED);
	Ps2F f2, mass, s;
	Ps2Vec4 v;
	if (Ps2F_Le(f0, f1))
	{
		return;
	}
	f2 = Ps2F_Sub(f0, f1);
	f1 = ZmRF(m, kart + 0x3E4u); // steering input
	f0 = 0u;
	if (Ps2F_Lt(f1, f0))
	{
		f1 = Ps2F_Neg(f1);
	}
	f2 = Ps2F_Mul(f2, f1);
	f0 = 0u;
	if (Ps2F_Le(f2, f0))
	{
		return;
	}
	f1 = ZmRF(m, kart + 0xECu);
	f0 = ZmRF(m, kart + 0xF0u);
	f1 = Ps2F_Mul(f2, f1);
	if (!Ps2F_Lt(f1, f0))
	{
		f1 = f0;
	}
	mass = ZmRF(m, g_Body(c, kart) + G_B_MASS);
	v = ZmRV(m, g_Frame(c, kart) + G_F_VELDIR);
	f0 = Ps2F_Mul(f1, mass);
	s = Ps2F_Neg(f0);
	g_ForceAddScaled(c, kart, v, s);
}

// Friction parameters. d = dot(frame row 1, velocity direction); if d <= K+0xCC the triple
// at a3 is copied to out, else the one at a2. Then out.x *= S[6], out.y *= S[7], out.z *= S[6]
// (S = surface table K+0x424), out.x *= (1 - K+0x3F4); and, when K+0x451 is set and the speed
// K+0x438 is not below K+0x2D8, out.x and out.z are scaled by 1 - K+0x2DC * t with t = 1 or
// (speed - K+0x2D8) / (K+0x2D4 - K+0x2D8).
void ExtZp_State_195ce0(ExtZpCtx *c, uint32_t kart, uint32_t out, uint32_t fricA, uint32_t fricB)
{
	ExtZpMem *m = c->m;
	uint32_t frame = g_Frame(c, kart);
	uint32_t rowA = frame + G_F_ROWS;
	uint32_t rowB = frame + G_F_VELDIR;
	Ps2F f2, f0, f1, f3, d;
	Ps2Acc acc;
	uint32_t srf;
	f2 = ZmRF(m, rowA);
	f0 = ZmRF(m, rowB);
	f1 = ZmRF(m, rowA + 4u);
	acc = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, rowB + 4u);
	f2 = ZmRF(m, rowA + 8u);
	acc = Ps2A_Madda(acc, f1, f0);
	f0 = ZmRF(m, rowB + 8u);
	d = Ps2A_Madd(acc, f2, f0);
	f0 = ZmRF(m, kart + 0xCCu);
	if (Ps2F_Le(d, f0))
	{
		f2 = ZmRF(m, fricB);
		f1 = ZmRF(m, fricB + 4u);
		f0 = ZmRF(m, fricB + 8u);
		ZmWF(m, out, f2);
		ZmWF(m, out + 4u, f1);
		ZmWF(m, out + 8u, f0);
	}
	else
	{
		f2 = ZmRF(m, fricA);
		f1 = ZmRF(m, fricA + 4u);
		f0 = ZmRF(m, fricA + 8u);
		ZmWF(m, out, f2);
		ZmWF(m, out + 4u, f1);
		ZmWF(m, out + 8u, f0);
	}
	srf = ZmR32(m, kart + G_K_SURF);
	f3 = PS2F_ONE;
	f0 = ZmRF(m, out);
	f2 = ZmRF(m, srf + 0x18u);
	f1 = ZmRF(m, srf + 0x1Cu);
	f0 = Ps2F_Mul(f0, f2);
	ZmWF(m, out, f0);
	f0 = ZmRF(m, out + 4u);
	f0 = Ps2F_Mul(f0, f1);
	ZmWF(m, out + 4u, f0);
	f0 = ZmRF(m, out + 8u);
	f0 = Ps2F_Mul(f0, f2);
	ZmWF(m, out + 8u, f0);
	f1 = ZmRF(m, kart + 0x3F4u); // brake input
	f0 = ZmRF(m, out);
	f1 = Ps2F_Sub(f3, f1);
	f0 = Ps2F_Mul(f1, f0);
	ZmWF(m, out, f0);
	if (ZmR8(m, kart + 0x451u) == 0)
	{
		return;
	}
	f1 = ZmRF(m, kart + 0x438u);
	f2 = ZmRF(m, kart + 0x2D8u);
	if (Ps2F_Lt(f1, f2))
	{
		return;
	}
	f0 = ZmRF(m, kart + 0x2D4u);
	if (Ps2F_Lt(f1, f0))
	{
		f1 = Ps2F_Sub(f1, f2);
		f0 = Ps2F_Sub(f0, f2);
		f3 = Ps2F_Div(f1, f0);
		f2 = ZmRF(m, kart + 0x2DCu);
	}
	else
	{
		f2 = ZmRF(m, kart + 0x2DCu);
	}
	f1 = PS2F_ONE;
	f0 = ZmRF(m, out);
	f2 = Ps2F_Mul(f2, f3);
	f1 = Ps2F_Sub(f1, f2);
	f0 = Ps2F_Mul(f1, f0);
	ZmWF(m, out, f0);
	f0 = ZmRF(m, out + 8u);
	f0 = Ps2F_Mul(f1, f0);
	ZmWF(m, out + 8u, f0);
}

// Speed limit. Does nothing when the owner (K+0x38) has effect bit 2 set. Otherwise:
// f1 = K+0x470 * S[0]; nothing if f1 <= 0 or speed >= f1. f3 = min(K+0xE0 * S[1],
// K+0x474 * S[2]); nothing if f3 <= 0. The body gets the force  -1 * f3 * mass  along the
// velocity direction; then K+0x3EC (state 3: K+0x3F0) is set to 0.
void ExtZp_State_196280(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	uint32_t owner = ZmR32(m, kart + G_K_OWNER);
	uint32_t srf;
	Ps2F f0, f1, f2, f3, mass;
	Ps2Vec4 v;
	if (owner != 0)
	{
		if (ExtZp_Kart_1c1ff0(c, ZmR32(m, owner + 0x88u), 2u) != 0)
		{
			return;
		}
	}
	srf = ZmR32(m, kart + G_K_SURF);
	f1 = ZmRF(m, kart + 0x470u);
	f0 = 0u;
	f2 = ZmRF(m, srf);
	f1 = Ps2F_Mul(f1, f2);
	if (Ps2F_Le(f1, f0))
	{
		return;
	}
	f0 = ZmRF(m, kart + G_K_SPEED);
	if (Ps2F_Le(f0, f1))
	{
		return;
	}
	f3 = ZmRF(m, srf + 4u);
	f2 = ZmRF(m, kart + 0xE0u);
	f1 = ZmRF(m, srf + 8u);
	f0 = ZmRF(m, kart + 0x474u);
	f3 = Ps2F_Mul(f2, f3);
	f0 = Ps2F_Mul(f0, f1);
	if (Ps2F_Le(f3, f0))
	{
		f3 = f0;
	}
	f2 = 0u;
	if (Ps2F_Le(f3, f2))
	{
		return;
	}
	v = ZmRV(m, g_Frame(c, kart) + G_F_VELDIR);
	f0 = 0xBF800000u; // -1.0 (the original's constant)
	f0 = Ps2F_Mul(f0, f3);
	mass = ZmRF(m, g_Body(c, kart) + G_B_MASS);
	f0 = Ps2F_Mul(f0, mass);
	g_ForceAddScaled(c, kart, v, f0);
	if (ZmR32(m, kart + G_K_STATE) != 3u)
	{
		ZmWF(m, kart + 0x3ECu, f2);
	}
	else
	{
		ZmWF(m, kart + 0x3F0u, f2);
	}
}

// Grip / friction of the driving kart. p = the vec3 at `vec3` (a limit per axis, copied);
// K+0x4EC is a timer: when it is above dt it runs down by dt and p becomes p blended towards
// (K+0x4F4..0x4FC) (or just that vector when K+0x4F0 is 0), else the timer is set to 0 and p is kept.
// Then three clamped speed parts give forces on the body: along frame row 3 (+0x30), scaled
// by -clamp(v . row 3 / dt) * mass; along row 2 (+0x10), by the absolute value or the limit
// p.z; and again along row 2 with the clamp of p.y.
void ExtZp_State_195900(ExtZpCtx *c, uint32_t kart, uint32_t vec3, Ps2F dt)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f21, f22;
	Ps2F px, py, pz;
	uint32_t frame;
	Ps2Acc acc;
	Ps2Vec4 v;

	f6 = PS2F_ONE;
	f1 = 0u;
	f0 = ZmRF(m, kart + 0x4ECu);
	f4 = ZmRF(m, vec3);
	f3 = ZmRF(m, vec3 + 4u);
	f2 = ZmRF(m, vec3 + 8u);
	px = f4;
	py = f3;
	pz = f2;
	if (Ps2F_Le(f0, dt))
	{
		ZmWF(m, kart + 0x4ECu, f1);
	}
	else
	{
		f0 = ZmRF(m, kart + 0x4ECu);
		f0 = Ps2F_Sub(f0, dt);
		ZmWF(m, kart + 0x4ECu, f0);
		if (ZmR8(m, kart + 0x4F0u) != 0)
		{
			f7 = ZmRF(m, kart + 0x4ECu);
			f3 = py;
			f2 = ZmRF(m, kart + 0x4F8u);
			f5 = px;
			f4 = ZmRF(m, kart + 0x4F4u);
			f0 = ZmRF(m, kart + 0x4FCu);
			f6 = Ps2F_Div(f6, f7);
			f1 = pz;
			f6 = Ps2F_Mul(f7, f6);
			f4 = Ps2F_Sub(f4, f5);
			f2 = Ps2F_Sub(f2, f3);
			f0 = Ps2F_Sub(f0, f1);
			f4 = Ps2F_Mul(f6, f4);
			f2 = Ps2F_Mul(f6, f2);
			f0 = Ps2F_Mul(f6, f0);
			f4 = Ps2F_Add(f5, f4);
			f2 = Ps2F_Add(f3, f2);
			f0 = Ps2F_Add(f1, f0);
			px = f4;
			py = f2;
			pz = f0;
		}
		else
		{
			px = ZmRF(m, kart + 0x4F4u);
			py = ZmRF(m, kart + 0x4F8u);
			pz = ZmRF(m, kart + 0x4FCu);
		}
	}

	// force 1: along frame row 3 (+0x30), with the part of the speed along it clamped to +-p.x
	frame = g_Frame(c, kart);
	f2 = ZmRF(m, frame + 0x40u);
	f0 = ZmRF(m, frame + 0x30u);
	f1 = ZmRF(m, frame + 0x44u);
	acc = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, frame + 0x34u);
	f2 = ZmRF(m, frame + 0x48u);
	acc = Ps2A_Madda(acc, f1, f0);
	f0 = ZmRF(m, frame + 0x38u);
	f0 = Ps2A_Madd(acc, f2, f0);
	f22 = Ps2F_Div(f0, dt);
	f2 = px;
	if (Ps2F_Lt(f22, f2))
	{
		f2 = f22;
	}
	f1 = px;
	f0 = Ps2F_Neg(f1);
	if (Ps2F_Le(f2, f0))
	{
		f22 = f0;
	}
	else
	{
		if (!Ps2F_Lt(f22, f1))
		{
			f22 = f1;
		}
	}
	v = ZmRV(m, frame + 0x30u);
	f0 = Ps2F_Neg(f22);
	f1 = ZmRF(m, g_Body(c, kart) + G_B_MASS);
	f0 = Ps2F_Mul(f0, f1);
	g_ForceAddScaled(c, kart, v, f0);

	// force 2: along row 2 (+0x10), by |f22| when that is below p.z, else by p.z
	f21 = pz;
	f0 = ExtZp_Math_43b068(c, f22);
	if (Ps2F_Lt(f0, f21))
	{
		f0 = ExtZp_Math_43b068(c, f22);
	}
	else
	{
		f0 = pz;
	}
	frame = g_Frame(c, kart);
	v = ZmRV(m, frame + G_F_ROWS);
	f1 = ZmRF(m, g_Body(c, kart) + G_B_MASS);
	f0 = Ps2F_Mul(f0, f1);
	g_ForceAddScaled(c, kart, v, f0);

	// force 3: along row 2 (+0x10), with the speed part along it clamped to +-p.y
	frame = g_Frame(c, kart);
	f2 = ZmRF(m, frame + 0x40u);
	f0 = ZmRF(m, frame + 0x10u);
	f1 = ZmRF(m, frame + 0x44u);
	acc = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, frame + 0x14u);
	f2 = ZmRF(m, frame + 0x48u);
	acc = Ps2A_Madda(acc, f1, f0);
	f0 = ZmRF(m, frame + 0x18u);
	f0 = Ps2A_Madd(acc, f2, f0);
	f3 = Ps2F_Div(f0, dt);
	f1 = py;
	f2 = f1;
	if (Ps2F_Lt(f3, f1))
	{
		f2 = f3;
	}
	f0 = Ps2F_Neg(f1);
	if (Ps2F_Le(f2, f0))
	{
		f3 = f0;
	}
	else
	{
		if (!Ps2F_Lt(f3, f1))
		{
			f3 = f1;
		}
	}
	v = ZmRV(m, frame + G_F_ROWS);
	f0 = Ps2F_Neg(f3);
	f1 = ZmRF(m, g_Body(c, kart) + G_B_MASS);
	f0 = Ps2F_Mul(f0, f1);
	g_ForceAddScaled(c, kart, v, f0);
}

// The brake / drag part shared by both states: f = K+0x88 * max(K+0x3F0 .. see callers) scaled by the
// surface factor S[5] unless the owner has effect bit 2. Returns the value (f21 of the original).
static Ps2F g_BrakeBase(ExtZpCtx *c, uint32_t kart, Ps2F f1)
{
	ExtZpMem *m = c->m;
	Ps2F f0 = ZmRF(m, kart + 0x88u); // brake force parameter
	Ps2F f21 = Ps2F_Mul(f0, f1);
	uint32_t owner = ZmR32(m, kart + G_K_OWNER);
	int scale = 1;
	if (owner != 0)
	{
		if (ExtZp_Kart_1c1ff0(c, ZmR32(m, owner + 0x88u), 2u) != 0)
		{
			scale = 0;
		}
	}
	if (scale)
	{
		f0 = ZmRF(m, ZmR32(m, kart + G_K_SURF) + 0x14u);
		f21 = Ps2F_Mul(f21, f0);
	}
	return f21;
}

// Resets the body's velocity to zero and renormalises it (the kart is standing and not jumping).
static void g_StandStill(ExtZpCtx *c, uint32_t kart)
{
	uint32_t zero = ExtZp_Push(c, 16u); // a copy of the original's vector at 0x6A2160 (read from memory)
	uint32_t frame = g_Frame(c, kart);
	ZmWV(c->m, zero, ZmRV(c->m, 0x006A2160u));
	uint32_t a0 = ExtZp_Kart_136160(c, zero);
	ExtZp_Kart_13dd40(c, ZmR32(c->m, frame + G_F_BODY), a0);
	ExtZp_Glue_136050(c, frame);
	ExtZp_Pop(c, 16u);
}

// dot(frame row 2 (+0x10), velocity direction (+0x50)) of the kart's frame
static Ps2F g_DotRowVel(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	uint32_t frame = g_Frame(c, kart);
	uint32_t v0 = frame + G_F_ROWS;
	uint32_t v1 = frame + G_F_VELDIR;
	Ps2F f2 = ZmRF(m, v0);
	Ps2F f0 = ZmRF(m, v1);
	Ps2F f1 = ZmRF(m, v0 + 4u);
	Ps2Acc acc = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, v1 + 4u);
	f2 = ZmRF(m, v0 + 8u);
	acc = Ps2A_Madda(acc, f1, f0);
	f0 = ZmRF(m, v1 + 8u);
	return Ps2A_Madd(acc, f2, f0);
}

// body.force.xyz += v.xyz * s where s = (num / dt), v = a vec4 read from the frame at `off`
static void g_ForceFromFrame(ExtZpCtx *c, uint32_t kart, uint32_t off, Ps2F s)
{
	Ps2Vec4 v = ZmRV(c->m, g_Frame(c, kart) + off);
	g_ForceAddScaled(c, kart, v, s);
}

// State 2 / 3 update of the driving kart (see the header).
void ExtZp_State_192fc0(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3, f4, f21;
	uint32_t out;
	int backward;

	ExtZp_State_195e50(c, kart);
	ExtZp_State_196280(c, kart);
	backward = (ZmR32(m, kart + G_K_STATE) == 3u);

	if (backward)
	{
		int skipPush = 0;
		f0 = ZmRF(m, kart + 0x3E4u);
		f2 = 0xBF800000u; // -1.0 (the original's constant)
		f1 = 0u;
		f0 = Ps2F_Mul(f0, f2);
		ZmWF(m, kart + 0x3E4u, f0);
		f0 = ZmRF(m, kart + 0x3E8u);
		f0 = Ps2F_Mul(f0, f2);
		ZmWF(m, kart + 0x3E8u, f0);
		f0 = ZmRF(m, kart + 0x3F0u);
		if (Ps2F_Le(f0, f1))
		{
			ZmW8(m, kart + 0x429u, 0u);
			skipPush = 1;
		}
		else
		{
			ZmW8(m, kart + 0x429u, 1u);
			f0 = ZmRF(m, kart + 0x3ECu);
			if (!Ps2F_Le(f0, f1))
			{
				ZmWF(m, kart + 0x3F0u, f1);
			}
		}
		if (!skipPush)
		{
			Ps2F slope = Ps2F_Neg(ZmRF(m, g_Frame(c, kart) + 0x18u));
			f0 = ExtZp_State_197d50(c, kart, slope);
			f4 = ZmRF(m, kart + 0x3F0u);
			f3 = ZmRF(m, kart + 0x84u); // reverse gain
			f2 = ZmRF(m, kart + G_K_SPEED);
			f1 = ZmRF(m, kart + 0x78u); // max reverse speed
			f3 = Ps2F_Mul(f4, f3);
			f3 = Ps2F_Mul(f3, f0);
			f0 = Ps2F_Sub(f1, f2);
			f2 = Ps2F_Mul(f3, dt);
			if (!Ps2F_Lt(f2, f0))
			{
				f2 = f0;
			}
			f0 = 0u;
			f1 = ZmRF(m, g_Body(c, kart) + G_B_MASS);
			f1 = Ps2F_Mul(f2, f1);
			if (!Ps2F_Le(f1, f0))
			{
				g_ForceFromFrame(c, kart, G_F_ROWS, Ps2F_Div(Ps2F_Neg(f1), dt));
			}
		}
		f0 = ZmRF(m, kart + 0x3ECu);
		f1 = ZmRF(m, kart + 0x3F4u); // brake input
		if (Ps2F_Le(f1, f0))
		{
			f1 = f0;
		}
		f21 = g_BrakeBase(c, kart, f1);
		f0 = ZmRF(m, kart + G_K_SPEED);
		f1 = Ps2F_Mul(f21, dt);
		if (Ps2F_Lt(f1, f0))
		{
			f0 = 0u;
			if (!Ps2F_Le(f1, f0))
			{
				f0 = Ps2F_Mul(f1, ZmRF(m, g_Body(c, kart) + G_B_MASS));
				f0 = Ps2F_Neg(f0);
				g_ForceFromFrame(c, kart, G_F_VELDIR, Ps2F_Div(f0, dt));
			}
		}
		else
		{
			f1 = ZmRF(m, kart + 0x490u); // jump timer
			f0 = 0u;
			if (Ps2F_Le(f1, f0))
			{
				g_StandStill(c, kart);
			}
			f0 = ZmRF(m, kart + 0x3ECu);
			f1 = 0u;
			if (!Ps2F_Le(f0, f1))
			{
				f0 = ZmRF(m, kart + 0x3F0u);
				if (Ps2F_Le(f0, f1))
				{
					ExtZp_Kart_1911b0(c, kart, 2u, 0u); // a2: 0 or left over in the original (the state handlers on this path do not read it)
				}
			}
		}
		f1 = g_DotRowVel(c, kart);
		f0 = 0x3F000000u; // 0.5 (the original's constant)
		if (Ps2F_Le(f1, f0))
		{
			goto after;
		}
		f0 = ZmRF(m, kart + 0x3ECu);
		f1 = 0u;
		if (!Ps2F_Le(f0, f1))
		{
			f0 = ZmRF(m, kart + 0x3F0u);
			if (Ps2F_Le(f0, f1))
			{
				ExtZp_Kart_1911b0(c, kart, 2u, 0u); // a2: 0 or left over in the original (the state handlers on this path do not read it)
			}
		}
		goto after;
	}

	// state 2: forward
	ZmW8(m, kart + 0x429u, 0u);
	f1 = 0u;
	f2 = ZmRF(m, kart + 0x3ECu);
	if (!Ps2F_Le(f2, f1))
	{
		f0 = ZmRF(m, kart + 0x3F4u);
		if (!Ps2F_Le(f0, f1))
		{
			f0 = 0x3E800000u; // 0.25 (the original's constant)
			f0 = Ps2F_Mul(f2, f0);
			ZmWF(m, kart + 0x3ECu, f0);
		}
		f0 = ExtZp_State_197d50(c, kart, ZmRF(m, g_Frame(c, kart) + 0x18u));
		f4 = ZmRF(m, kart + 0x3ECu);
		f3 = ZmRF(m, kart + 0x474u);
		f2 = ZmRF(m, kart + G_K_SPEED);
		f1 = ZmRF(m, kart + 0x470u);
		f3 = Ps2F_Mul(f4, f3);
		f3 = Ps2F_Mul(f3, f0);
		f0 = Ps2F_Sub(f1, f2);
		f2 = Ps2F_Mul(f3, dt);
		if (!Ps2F_Lt(f2, f0))
		{
			f2 = f0;
		}
		f0 = 0u;
		f1 = Ps2F_Mul(f2, ZmRF(m, g_Body(c, kart) + G_B_MASS));
		if (!Ps2F_Le(f1, f0))
		{
			g_ForceFromFrame(c, kart, G_F_ROWS, Ps2F_Div(f1, dt));
		}
	}
	f0 = ZmRF(m, kart + 0x3F0u);
	f1 = ZmRF(m, kart + 0x3F4u);
	if (Ps2F_Le(f1, f0))
	{
		f1 = f0;
	}
	f21 = g_BrakeBase(c, kart, f1);
	f0 = ZmRF(m, kart + G_K_SPEED);
	f21 = Ps2F_Mul(f21, dt);
	if (Ps2F_Lt(f21, f0))
	{
		f0 = 0u;
		if (!Ps2F_Le(f21, f0))
		{
			f0 = Ps2F_Mul(f21, ZmRF(m, g_Body(c, kart) + G_B_MASS));
			f0 = Ps2F_Neg(f0);
			g_ForceFromFrame(c, kart, G_F_VELDIR, Ps2F_Div(f0, dt));
		}
	}
	else
	{
		f1 = ZmRF(m, kart + 0x490u); // jump timer
		f0 = 0u;
		if (Ps2F_Le(f1, f0))
		{
			g_StandStill(c, kart);
		}
		f0 = ZmRF(m, kart + 0x3F0u);
		f1 = 0u;
		if (!Ps2F_Le(f0, f1) && Ps2F_Le(ZmRF(m, kart + 0x3ECu), f1) && Ps2F_Le(ZmRF(m, kart + 0x4C8u), f1))
		{
			// no gas, backward input, no boost: the speed becomes the brake value, state 3
			ZmWF(m, kart + G_K_SPEED, f21);
			ExtZp_Kart_1911b0(c, kart, 3u, 0u); // a2: 0 or left over in the original (the state handlers on this path do not read it)
		}
	}
	f1 = g_DotRowVel(c, kart);
	f0 = 0xBF000000u; // -0.5 (the original's constant)
	if (Ps2F_Lt(f1, f0))
	{
		f0 = ZmRF(m, kart + 0x3ECu);
		f1 = 0u;
		if (Ps2F_Le(f0, f1))
		{
			f0 = ZmRF(m, kart + 0x3F0u);
			if (!Ps2F_Le(f0, f1))
			{
				f0 = ZmRF(m, kart + 0x4C8u);
				if (Ps2F_Le(f0, f1))
				{
					ExtZp_Kart_1911b0(c, kart, 3u, 0u); // a2: 0 or left over in the original (the state handlers on this path do not read it)
				}
			}
		}
	}
	(void)ExtZp_State_195e20(c, kart);

after:
	ExtZp_State_196b40(c, kart, dt);
	ExtZp_State_194fd0(c, kart, dt);
	ExtZp_State_195820(c, kart);
	out = ExtZp_Push(c, 16u);
	ExtZp_State_195ce0(c, kart, out, kart + 0xDCu, kart + 0xD0u);
	ExtZp_State_195900(c, kart, out, dt);
	ExtZp_State_197300(c, kart, dt);
	ExtZp_State_197870(c, kart, kart + 0x510u);
	ExtZp_Pop(c, 16u);
	if (ZmR8(m, kart + 0x402u) == 0)
	{
		ExtZp_Kart_1911b0(c, kart, 4u, 0u); // a2: 0 or left over in the original (the state handlers on this path do not read it)
	}
}

// Steering and yaw response. f22 = K+0x478 * K+0x3E4 + (K+0xC4 * K+0x3F4 * K+0x3E4 when braking, else
// K+0xC8 * K+0x3EC * K+0x3E4), set to 0 at low speed in the states 2/3 when the gas / reverse input is
// small. The turn rate K+0x480 moves towards f22 (limited by K+0x484 * dt) and, when K+0x48C is set and
// K+0x488 is not 0, the body's velocity is rotated about the frame axis (Y+0x20) by half the angle
// (a quaternion style rotation with sin / cos of half the angle), the angular velocity gets
// Y+0x20 * f22 + the old one. dt = step in seconds.
void ExtZp_State_194fd0(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3, f4, f20, f21, f22, f23;
	uint32_t base, frame, st, s0;
	const uint32_t L_B0 = 0xB0u, L_C0 = 0xC0u, L_60 = 0x60u, L_70 = 0x70u, L_80 = 0x80u, L_90 = 0x90u, L_A0 = 0xA0u;
	const uint32_t L_D0 = 0xD0u, L_E0 = 0xE0u, L_F0 = 0xF0u, L_110 = 0x110u;

	f23 = dt;
	f3 = ZmRF(m, kart + 0x3E4u);
	f1 = ZmRF(m, kart + 0x478u);
	f4 = ZmRF(m, kart + 0x3F4u);
	f0 = 0u;
	f2 = Ps2F_Mul(f1, f3);
	if (!Ps2F_Le(f4, f0))
	{
		f0 = ZmRF(m, kart + 0xC4u);
		f0 = Ps2F_Mul(f0, f4);
		f0 = Ps2F_Mul(f3, f0);
		f22 = Ps2F_Add(f2, f0);
	}
	else
	{
		f1 = ZmRF(m, kart + 0xC8u);
		f0 = ZmRF(m, kart + 0x3ECu);
		f0 = Ps2F_Mul(f1, f0);
		f0 = Ps2F_Mul(f3, f0);
		f22 = Ps2F_Add(f2, f0);
	}
	f0 = ZmRF(m, kart + G_K_SPEED);
	f20 = PS2F_ONE;
	if (Ps2F_Lt(f0, f20))
	{
		st = ZmR32(m, kart + G_K_STATE);
		if (st == 3u)
		{
			f0 = ZmRF(m, kart + 0x3F0u);
			f1 = 0x3DCCCCCDu; // 0.1 (the original's constant)
			if (Ps2F_Lt(f0, f1) || !Ps2F_Le(f4, f1))
			{
				f22 = 0u;
			}
		}
		else if (st == 2u)
		{
			f0 = ZmRF(m, kart + 0x3ECu);
			f1 = 0x3DCCCCCDu; // 0.1 (the original's constant)
			if (Ps2F_Lt(f0, f1) || !Ps2F_Le(f4, f1))
			{
				f22 = 0u;
			}
		}
		else
		{
			f22 = 0u;
		}
	}
	f1 = ZmRF(m, kart + 0x480u); // turn rate
	f0 = 0u;
	if (Ps2F_Lt(f1, f0))
	{
		f1 = Ps2F_Neg(f1);
	}
	f0 = 0x3C23D70Au; // 0.01 (the original's constant)
	if (!Ps2F_Le(f1, f0))
	{
		Ps2F f24, f25, f26, sinHalf, cosHalf;
		f0 = ZmRF(m, kart + 0x484u); // turn rate step limit
		f21 = ZmRF(m, kart + 0x480u);
		f2 = Ps2F_Mul(f0, f23);
		f1 = f2;
		if (Ps2F_Lt(f21, f2))
		{
			f1 = f21;
		}
		f0 = Ps2F_Neg(f0);
		f0 = Ps2F_Mul(f0, f23);
		if (Ps2F_Le(f1, f0))
		{
			f21 = f0;
		}
		else if (!Ps2F_Lt(f21, f2))
		{
			f21 = f2;
		}
		f0 = ZmRF(m, kart + 0x480u);
		f0 = Ps2F_Sub(f0, f21);
		ZmWF(m, kart + 0x480u, f0);
		if (ZmR8(m, kart + 0x48Cu) != 0)
		{
			f0 = ExtZp_Math_43b068(c, ZmRF(m, kart + 0x488u));
			f1 = 0u;
			if (!Ps2F_Le(f0, f1))
			{
				Ps2Acc acc;
				f0 = ZmRF(m, kart + 0x488u);
				frame = g_Frame(c, kart);
				f26 = 0x3F000000u; // 0.5 (the original's constant)
				f25 = PS2F_ONE;
				acc = Ps2A_Mula(f21, f0);
				f24 = Ps2A_Madd(acc, f22, f23);
				base = ExtZp_Push(c, 0x130u);
				// the local at +0x110 is first built as (0,0,0,1), then overwritten by the vector at Y+0x20
				ZmW32(m, base + L_110, 0u);
				ZmW32(m, base + L_110 + 4u, 0u);
				ZmW32(m, base + L_110 + 8u, 0u);
				ZmW32(m, base + L_110 + 12u, PS2F_ONE);
				sinHalf = ExtZp_Math_43af80(c, Ps2F_Mul(f26, f24));
				ZmWV(m, base + L_110, ZmRV(m, frame + 0x20u));
				cosHalf = ExtZp_Math_43b320(c, Ps2F_Mul(f26, f24));
				{
					Ps2Vec4 v = ZmRV(m, base + L_110);
					Ps2Vec4 sv = {{cosHalf, 0u, 0u, 0u}};
					Ps2Vec4 bc = Ps2V_Bc(&sv, 0);
					Ps2V_Mul(&v, &v, &bc, 0xEu);
					ZmWV(m, base + L_110, v);
				}
				frame = g_Frame(c, kart);
				s0 = frame + 0x40u;
				ExtZp_Kart_1207f0(c, base + L_C0);
				f1 = sinHalf;
				f0 = 0x40000000u; // 2.0 (the original's constant)
				f0 = Ps2F_Mul(f0, f1);
				f0 = Ps2F_Mul(f0, f1);
				ExtZp_Integ_1207c0(c, base + L_B0, s0, Ps2F_Sub(f0, f25));
				ExtZp_Integ_1207b0(c, base + L_C0, base + L_B0);
				f0 = ExtZp_Integ_120780(c, base + L_110, s0);
				ExtZp_Integ_120750(c, base + L_80, base + L_110, f0);
				ExtZp_Kart_120720(c, base + L_60, base + L_110, s0);
				ExtZp_Integ_120750(c, base + L_70, base + L_60, sinHalf);
				ExtZp_Integ_1206f0(c, base + L_90, base + L_80, base + L_70);
				ExtZp_Integ_120750(c, base + L_A0, base + L_90, 0x40000000u);
				ExtZp_Kart_1206d0(c, base + L_C0, base + L_A0);
				ExtZp_Integ_1206c0(c, base + L_D0, base + L_C0);
				ZmWV(m, base + L_F0, ZmRV(m, base + L_D0));
				{
					uint32_t y = g_Frame(c, kart);
					uint32_t v = ExtZp_Kart_136160(c, base + L_F0);
					ExtZp_Kart_13dd40(c, ZmR32(m, y + G_F_BODY), v);
					ExtZp_Glue_136050(c, y);
				}
				ExtZp_Pop(c, 0x130u);
			}
			f0 = Ps2F_Div(f21, f23);
			f22 = Ps2F_Sub(f22, f0);
		}
		else
		{
			f0 = Ps2F_Div(f21, f23);
			f22 = Ps2F_Sub(f22, f0);
		}
	}
	f0 = ExtZp_Math_43b068(c, f22);
	f1 = 0u;
	if (Ps2F_Le(f0, f1))
	{
		return;
	}
	base = ExtZp_Push(c, 0x130u);
	ZmW32(m, base + L_E0, 0u);
	ZmW32(m, base + L_E0 + 4u, 0u);
	ZmW32(m, base + L_E0 + 8u, 0u);
	ZmW32(m, base + L_E0 + 12u, f20);
	frame = g_Frame(c, kart);
	ZmWV(m, base + L_E0, ZmRV(m, frame + 0x20u));
	{
		Ps2Vec4 v = ZmRV(m, base + L_E0);
		Ps2Vec4 sv = {{f22, 0u, 0u, 0u}};
		Ps2Vec4 bc = Ps2V_Bc(&sv, 0);
		Ps2Vec4 w;
		Ps2V_Mul(&v, &v, &bc, 0xEu);
		w = ZmRV(m, g_Frame(c, kart) + 0x70u);
		Ps2V_Add(&v, &v, &w, 0xEu);
		ZmWV(m, base + L_E0, v);
	}
	{
		uint32_t y = g_Frame(c, kart);
		uint32_t v = ExtZp_Kart_136160(c, base + L_E0);
		ExtZp_Kart_190350(c, ZmR32(m, y + G_F_BODY), v);
		ExtZp_Glue_190280(c, y);
	}
	ExtZp_Pop(c, 0x130u);
}
