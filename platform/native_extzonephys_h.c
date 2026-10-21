// THE ZONE PORT, PART H: downforce / gravity, drive sub function, jump (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_h.h>

// Globals of the original (absolute addresses, read from the port's memory).
#define H_G_WORLD 0x006FA190u        // pointer to the physics world (gravity vector at +0x30)
#define H_G_SPLINE_CLASS 0x006F63E0u // class record of a gravity spline (for isKindOf)
#define H_G_LEVEL_A 0x006F3568u      // pointer to a level object (flag index at +0x124), may be 0
#define H_G_LEVEL_B 0x006F35B8u      // pointer to another level object (flag index at +0x24c)
#define H_G_FLAGS 0x0068B650u        // table of bytes: one flag per level index

// Constants of the original (loaded as immediates).
#define H_ONE 0x3F800000u       // 1.0 (the original's constant)
#define H_MINUS_ONE 0xBF800000u // -1.0 (the original's constant)
#define H_TWO 0x40000000u       // 2.0 (the original's constant)
#define H_HALF 0x3F000000u      // 0.5 (the original's constant)
#define H_TENTH 0x3DCCCCCDu     // 0.1 (the original's constant)
#define H_HUNDREDTH 0x3C23D70Au // 0.01 (the original's constant)

// Kart fields used here (kart = K, its frame R = **(K + 0x2c), the body H = *(R + 0x94)).
enum
{
	H_K_FRAME = 0x2C,   // pointer to a pointer to the kart frame R
	H_K_DRIVER = 0x38,  // driver object D (may be 0); D+4 = vtable, D+0x88 = effect bits
	H_K_LOWSPEED = 0x8C,
	H_K_GRAV_AIR = 0x90,
	H_K_GRAV_GROUND = 0x94,
	H_K_DOWN_MAGLEV = 0x98,
	H_K_DOWN_MAGLEV_AIR = 0x9C,
	H_K_DOWN_GROUND = 0xA0,
	H_K_DOWN_AIRTIME = 0xA4,
	H_K_SPLINE_LIST = 0xA8, // pointer: object whose +8 -> list of gravity splines
	H_K_STATE = 0x3E0,
	H_K_GROUND = 0x402,     // u8: touching the ground
	H_K_NORMALS = 0x410,    // vec4: sum of the contact normals
	H_K_MAGLEV = 0x428,
	H_K_SPEED = 0x42C,
	H_K_GRAV_FACTOR = 0x460,
	H_K_SPLINE_INDEX = 0x464,
	H_K_SPLINE_NODE = 0x468,
	H_K_JUMP_TIMER = 0x490,
	H_K_AIR_TIME = 0x498,
	H_R_UP = 0x20,          // frame: up row (vec4)
	H_R_BODY = 0x94,        // frame: pointer to the body
	H_B_FORCE = 0x80,       // body: force accumulator (vec4)
	H_B_MASS = 0x1B0,       // body: mass
	H_B_POS = 0x30,         // body: position (vec4)
	H_W_GRAVITY = 0x30      // world: gravity vector
};

// ------------------------------------------------------------------ small helpers
// v.xyz *= s (the vector unit: qmtc2 to vf2.x, vmulx.xyz); w kept.
static void h_scale_xyz(Ps2Vec4 *v, Ps2F s)
{
	Ps2Vec4 sv = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&sv, 0);
	Ps2V_Mul(v, v, &bc, 0xEu);
}

// The frame R = **(kart + 0x2c).
static uint32_t h_frame(ExtZpMem *m, uint32_t kart)
{
	return ZmR32(m, ZmR32(m, kart + H_K_FRAME));
}

// The driver's virtual method (vtable + 0x34) called with (driver, arg): not a function of the
// path, handed out as a query (func = the call target, which the original keeps in t9).
static void h_driver_call(ExtZpCtx *c, uint32_t driver, uint32_t arg)
{
	ExtZpMem *m = c->m;
	ExtZpQueryArgs args;
	int i;

	for (i = 0; i < 8; i++)
	{
		args.a[i] = 0;
		args.f[i] = 0;
	}
	args.func = ZmR32(m, ZmR32(m, driver + 4) + 0x34);
	args.a[0] = driver;
	args.a[1] = arg;
	(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &args);
}

// body force accumulator += v.xyz (vadd.xyz); the body is read again, as the original does.
static void h_add_force(ExtZpCtx *c, uint32_t kart, Ps2Vec4 v)
{
	ExtZpMem *m = c->m;
	uint32_t body = ZmR32(m, h_frame(m, kart) + H_R_BODY);
	Ps2Vec4 f = ZmRV(m, body + H_B_FORCE);
	Ps2V_Add(&f, &f, &v, 0xEu);
	ZmWV(m, body + H_B_FORCE, f);
}

// |gravity of the world| : sqrt(g.x*g.x + g.y*g.y + g.z*g.z) with the accumulator sequence.
static Ps2F h_world_gravity_len(ExtZpCtx *c)
{
	ExtZpMem *m = c->m;
	uint32_t w = ZmR32(m, H_G_WORLD);
	Ps2F gx = ZmRF(m, w + H_W_GRAVITY), gy = ZmRF(m, w + H_W_GRAVITY + 4), gz = ZmRF(m, w + H_W_GRAVITY + 8);
	Ps2Acc acc = Ps2A_Mula(gx, gx);
	acc = Ps2A_Madda(acc, gy, gy);
	return Ps2F_Sqrt(Ps2A_Madd(acc, gz, gz));
}

// ------------------------------------------------------------------ 0x195e50
// Downforce and gravity. Maglev: strength s = parameter 0x98 (0x9c in the air after a jump and
// within the air time), times the gravity factor when off the ground; force = direction * s *
// mass * |world gravity|, direction = gravity spline direction (when the kart has one, speed
// >= low-speed or state 4) or minus the sum of the contact normals. Not maglev: gravity (air:
// parameter 0x90 * factor, ground: 0x94) along the world gravity vector, and downforce along
// minus the frame's up row.
void ExtZp_State_195e50(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	Ps2F f4, f5;

	if (ZmR8(m, kart + H_K_MAGLEV) != 0)
	{
		uint32_t state = ZmR32(m, kart + H_K_STATE);
		uint32_t idx;
		int useSpline = 1;
		Ps2Vec4 dir;
		Ps2F mass, glen, f20;
		uint32_t body;

		if (state == 4u)
		{
			int jumped = !Ps2F_Le(ZmRF(m, kart + H_K_JUMP_TIMER), 0u);
			if (!jumped)
				f20 = ZmRF(m, kart + H_K_DOWN_MAGLEV_AIR);
			else if (Ps2F_Le(ZmRF(m, kart + H_K_AIR_TIME), ZmRF(m, kart + H_K_DOWN_AIRTIME)))
				f20 = ZmRF(m, kart + H_K_DOWN_MAGLEV_AIR);
			else
				f20 = ZmRF(m, kart + H_K_DOWN_MAGLEV);
		}
		else
		{
			f20 = ZmRF(m, kart + H_K_DOWN_MAGLEV);
		}
		if (ZmR8(m, kart + H_K_GROUND) == 0)
			f20 = Ps2F_Mul(f20, ZmRF(m, kart + H_K_GRAV_FACTOR));

		if (Ps2F_Lt(ZmRF(m, kart + H_K_SPEED), ZmRF(m, kart + H_K_LOWSPEED)) && state != 4u)
			useSpline = 0;
		idx = ZmR32(m, kart + H_K_SPLINE_INDEX);
		if (useSpline && idx == 0xFFFFFFFFu)
			useSpline = 0;

		if (useSpline)
		{
			uint32_t s0, sp;
			Ps2Vec4 pos;

			s0 = ZmR32(m, ZmR32(m, ZmR32(m, ZmR32(m, kart + H_K_SPLINE_LIST) + 8) + 0x10) + (idx << 2));
			if (s0 != 0)
			{
				if (ExtZp_Kart_512ee0(c, s0, ZmR32(m, H_G_SPLINE_CLASS)) == 0)
					s0 = 0;
			}
			body = ZmR32(m, h_frame(m, kart) + H_R_BODY);
			pos = ZmRV(m, body + H_B_POS);
			sp = ExtZp_Push(c, 0x20); // +0: position (3 floats), +0x10: direction (3 floats)
			ZmWF(m, sp + 0x0, pos.v[0]);
			ZmWF(m, sp + 0x4, pos.v[1]);
			ZmWF(m, sp + 0x8, pos.v[2]);
			ExtZp_Kart_28b600(c, s0, ZmR32(m, kart + H_K_SPLINE_NODE), sp + 0x0, sp + 0x10);
			dir.v[0] = ZmRF(m, sp + 0x10);
			dir.v[1] = ZmRF(m, sp + 0x14);
			dir.v[2] = ZmRF(m, sp + 0x18);
			dir.v[3] = H_ONE;
			ExtZp_Pop(c, 0x20);

			body = ZmR32(m, h_frame(m, kart) + H_R_BODY);
			mass = ZmRF(m, body + H_B_MASS);
			glen = h_world_gravity_len(c);
			h_scale_xyz(&dir, Ps2F_Mul(Ps2F_Mul(f20, mass), glen));
			h_add_force(c, kart, dir);
			return;
		}
		dir = ZmRV(m, kart + H_K_NORMALS);
		body = ZmR32(m, h_frame(m, kart) + H_R_BODY);
		mass = ZmRF(m, body + H_B_MASS);
		glen = h_world_gravity_len(c);
		h_scale_xyz(&dir, Ps2F_Mul(Ps2F_Mul(Ps2F_Mul(H_MINUS_ONE, f20), mass), glen));
		h_add_force(c, kart, dir);
		return;
	}

	if (ZmR8(m, kart + H_K_GROUND) != 0)
	{
		f4 = ZmRF(m, kart + H_K_GRAV_GROUND);
		f5 = ZmRF(m, kart + H_K_DOWN_GROUND);
	}
	else
	{
		f5 = 0u;
		f4 = Ps2F_Mul(ZmRF(m, kart + H_K_GRAV_AIR), ZmRF(m, kart + H_K_GRAV_FACTOR));
	}
	if (!Ps2F_Le(f5, 0u))
	{
		uint32_t frame = h_frame(m, kart);
		Ps2Vec4 up = ZmRV(m, frame + H_R_UP);
		uint32_t body = ZmR32(m, frame + H_R_BODY);
		Ps2F mass = ZmRF(m, body + H_B_MASS);
		Ps2F glen = h_world_gravity_len(c);
		h_scale_xyz(&up, Ps2F_Mul(Ps2F_Mul(Ps2F_Mul(H_MINUS_ONE, f5), mass), glen));
		h_add_force(c, kart, up);
	}
	if (!Ps2F_Le(f4, 0u))
	{
		uint32_t w = ZmR32(m, H_G_WORLD);
		Ps2Vec4 g = ZmRV(m, w + H_W_GRAVITY);
		uint32_t body = ZmR32(m, h_frame(m, kart) + H_R_BODY);
		h_scale_xyz(&g, Ps2F_Mul(f4, ZmRF(m, body + H_B_MASS)));
		h_add_force(c, kart, g);
	}
}

// ------------------------------------------------------------------ 0x1972a0
// Cancels a boost: boost time = -1.0, in-a-row timer = 2 * tolerance (kart + 0x274), flag
// (kart + 0x4d8) = 0, the driver's virtual method (vtable + 0x34, with 0) is handed out as a query,
// in-a-row counter (kart + 0x4d0) = 0.
void ExtZp_State_1972a0(ExtZpCtx *c, uint32_t kart)
{
	ExtZpMem *m = c->m;
	uint32_t d;

	ZmW32(m, kart + 0x4C8, H_MINUS_ONE); // boost time left
	ZmWF(m, kart + 0x4D4, Ps2F_Mul(H_TWO, ZmRF(m, kart + 0x274)));
	ZmW8(m, kart + 0x4D8, 0);
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0)
		h_driver_call(c, d, 0u); // virtual call (driver, 0)
	ZmW32(m, kart + 0x4D0, 0);
}

// ------------------------------------------------------------------ 0x197300
// Jump. Timer (kart + 0x490) counts down by dt while above zero. When it is over and the jump
// is asked (+0x3fb or +0x3f9): maglev = impulse (up row * parameter 0x114 * mass); otherwise
// an upward impulse built from the speed-dependent parameters 0x110..0x124; then the timer is
// set to parameter 0x128 and the frame's velocity copy refreshed.
void ExtZp_State_197300(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	ExtZpMem *m = c->m;
	uint32_t state = ZmR32(m, kart + H_K_STATE);

	if (state == 4u)
	{
		if (ZmR8(m, kart + 0x494) != 0)
			return;
		if (!Ps2F_Le(ZmRF(m, kart + H_K_JUMP_TIMER), 0u))
			return;
		if (!Ps2F_Le(ZmRF(m, kart + H_K_AIR_TIME), ZmRF(m, kart + 0x10C)))
			return;
	}
	{
		Ps2F timer = ZmRF(m, kart + H_K_JUMP_TIMER);
		if (!Ps2F_Le(timer, 0u))
		{
			ZmWF(m, kart + H_K_JUMP_TIMER, Ps2F_Sub(timer, dt));
			return;
		}
	}
	if (ZmR8(m, kart + 0x3FB) == 0)
	{
		if (ZmR8(m, kart + 0x3F9) == 0)
			return;
	}

	if (ZmR8(m, kart + H_K_MAGLEV) != 0)
	{
		uint32_t frame = h_frame(m, kart);
		Ps2Vec4 j = ZmRV(m, frame + H_R_UP);
		uint32_t body = ZmR32(m, frame + H_R_BODY);
		uint32_t jv, v;
		Ps2F f0 = Ps2F_Mul(ZmRF(m, kart + 0x114), ZmRF(m, body + H_B_MASS));

		h_scale_xyz(&j, f0);
		jv = ExtZp_Push(c, 0x10);
		ZmWV(m, jv, j);
		frame = h_frame(m, kart);
		v = ExtZp_Kart_136160(c, jv);
		ExtZp_Kart_136110(c, ZmR32(m, frame + H_R_BODY), v);
		ExtZp_Glue_136050(c, frame);
		ExtZp_Pop(c, 0x10);
		ZmWF(m, kart + H_K_JUMP_TIMER, ZmRF(m, kart + 0x128));
		return;
	}
	{
		uint32_t frame = h_frame(m, kart);
		Ps2F f4 = ZmRF(m, frame + 0x48);
		Ps2F f2 = ZmRF(m, kart + 0x11C);
		Ps2F f0, f1, f3;

		f0 = f2; // mov.s in the branch delay slot
		if (Ps2F_Lt(f4, f2))
			f0 = f4;
		f1 = ZmRF(m, kart + 0x118);
		if (Ps2F_Le(f0, f1))
		{
			f2 = f1;
			f3 = Ps2F_Sub(f2, f1);
		}
		else
		{
			if (Ps2F_Lt(f4, f2))
				f2 = f4;
			f3 = Ps2F_Sub(f2, f1);
		}
		f2 = ZmRF(m, kart + 0x120);
		f1 = ZmRF(m, kart + 0x110);
		f0 = ZmRF(m, kart + 0x124);
		f2 = Ps2F_Mul(f3, f2);
		f2 = Ps2F_Add(f2, f1);
		f1 = Ps2F_Add(f2, f4);
		if (!Ps2F_Le(f1, f0))
		{
			f0 = Ps2F_Sub(f1, f0);
			f2 = Ps2F_Sub(f2, f0);
		}
		f1 = 0u;
		if (!Ps2F_Le(f2, f1))
		{
			uint32_t body = ZmR32(m, frame + H_R_BODY);
			uint32_t jv, v;
			Ps2Vec4 j;

			j.v[0] = f1;
			j.v[1] = f1;
			j.v[2] = Ps2F_Mul(f2, ZmRF(m, body + H_B_MASS));
			j.v[3] = H_ONE;
			jv = ExtZp_Push(c, 0x10);
			ZmWV(m, jv, j);
			frame = h_frame(m, kart);
			v = ExtZp_Kart_136160(c, jv);
			ExtZp_Kart_136110(c, ZmR32(m, frame + H_R_BODY), v);
			ExtZp_Glue_136050(c, frame);
			ExtZp_Pop(c, 0x10);
		}
		ZmWF(m, kart + H_K_JUMP_TIMER, ZmRF(m, kart + 0x128));
	}
}

// ------------------------------------------------------------------ 0x197870
// Adds the vector `vec` minus its component along the frame's up row, normalised, times a
// factor (parameter 0x314, times the length when below parameter 0x318, else times 0x318),
// to kart + 0x3b0; only when the level's flag is set.
void ExtZp_State_197870(ExtZpCtx *c, uint32_t kart, uint32_t vec)
{
	ExtZpMem *m = c->m;
	uint32_t lv = ZmR32(m, H_G_LEVEL_A);
	uint32_t flag;
	Ps2Vec4 a, b, k;
	Ps2F f3, f0, dot;
	Ps2Acc acc;

	if (lv != 0)
		flag = ZmR8(m, H_G_FLAGS + ZmR32(m, lv + 0x124));
	else
		flag = ZmR8(m, H_G_FLAGS + ZmR32(m, ZmR32(m, H_G_LEVEL_B) + 0x24C));
	if (flag == 0)
		return;

	a = ZmRV(m, vec);
	b = ZmRV(m, h_frame(m, kart) + H_R_UP);
	acc = Ps2A_Mula(a.v[0], b.v[0]);
	acc = Ps2A_Madda(acc, a.v[1], b.v[1]);
	dot = Ps2A_Madd(acc, a.v[2], b.v[2]);
	h_scale_xyz(&b, dot);
	{
		Ps2Vec4 d = a;
		Ps2V_Sub(&d, &a, &b, 0xEu);
		a = d;
	}
	acc = Ps2A_Mula(a.v[0], a.v[0]);
	acc = Ps2A_Madda(acc, a.v[1], a.v[1]);
	f3 = Ps2A_Madd(acc, a.v[2], a.v[2]);
	if (!Ps2F_Eq(f3, 0u))
	{
		f3 = Ps2F_Sqrt(f3);
		a.v[0] = Ps2F_Div(a.v[0], f3);
		a.v[1] = Ps2F_Div(a.v[1], f3);
		a.v[2] = Ps2F_Div(a.v[2], f3);
	}
	f0 = ZmRF(m, kart + 0x318);
	if (Ps2F_Lt(f3, f0))
	{
		f0 = ZmRF(m, kart + 0x314);
	}
	else
	{
		f3 = f0;
		f0 = ZmRF(m, kart + 0x314);
	}
	f0 = Ps2F_Mul(f0, f3);
	h_scale_xyz(&a, f0);
	k = ZmRV(m, kart + 0x3B0);
	Ps2V_Add(&k, &k, &a, 0xEu);
	ZmWV(m, kart + 0x3B0, k);
}

// ------------------------------------------------------------------ 0x197d50
// A factor for the input f12: 1.0 when maglev, state 4 or f12 <= 0; otherwise with
// x = acos(1 - f12): 1.0 for x <= p0 (kart + 0xb0), 1.0 + p2 * (x - p0) / (p1 - p0) for
// p0 < x < p1 (p1 = kart + 0xb4), 1.0 + p2 above (p2 = kart + 0xb8).
Ps2F ExtZp_State_197d50(ExtZpCtx *c, uint32_t kart, Ps2F f12)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2;

	if (ZmR8(m, kart + H_K_MAGLEV) != 0)
		return H_ONE;
	if (ZmR32(m, kart + H_K_STATE) == 4u)
		return H_ONE;
	if (Ps2F_Le(f12, 0u))
		return H_ONE;
	f12 = Ps2F_Sub(H_ONE, f12);
	f0 = ExtZp_Math_43bc40(c, f12);
	f2 = ZmRF(m, kart + 0xB0);
	if (Ps2F_Le(f0, f2))
		return H_ONE;
	f1 = ZmRF(m, kart + 0xB4);
	if (!Ps2F_Lt(f0, f1))
	{
		f1 = ZmRF(m, kart + 0xB8);
		return Ps2F_Add(H_ONE, f1);
	}
	f1 = Ps2F_Sub(f1, f2);
	f0 = Ps2F_Sub(f0, f2);
	f2 = Ps2F_Div(f0, f1);
	f1 = ZmRF(m, kart + 0xB8);
	f1 = Ps2F_Mul(f1, f2);
	return Ps2F_Add(H_ONE, f1);
}

// ------------------------------------------------------------------ 0x196b40
static void h_drive_body(ExtZpCtx *c, uint32_t kart, Ps2F f21, uint32_t sp);

// Drive sub function (f12 = step time dt). Runs the boost-in-a-row timer, ends or applies the
// boost state (parameters 0x268..0x270), counts the slide timer (kart + 0x4bc) and turns the
// kart's velocity by a quaternion rotation about the up row while sliding, then multiplies the
// target speed (kart + 0x4c4) by the factors of the surface and driver effects and, when the
// kart is slower than that target, adds a force along the drive direction (limited by
// parameter 0x1cc) to the body's force accumulator.
void ExtZp_State_196b40(ExtZpCtx *c, uint32_t kart, Ps2F dt)
{
	uint32_t sp = ExtZp_Push(c, 0x110);
	h_drive_body(c, kart, dt, sp);
	ExtZp_Pop(c, 0x110);
}

static void h_drive_body(ExtZpCtx *c, uint32_t kart, Ps2F f21, uint32_t sp)
{
	ExtZpMem *m = c->m;
	Ps2F f20 = 0u, f0, f1, f2;
	uint32_t d, state;

	// boost-in-a-row timer
	f1 = ZmRF(m, kart + 0x4D4);
	f0 = ZmRF(m, kart + 0x274);
	if (Ps2F_Lt(f1, f0))
	{
		f0 = Ps2F_Add(f1, f21);
		ZmWF(m, kart + 0x4D4, f0);
		f1 = ZmRF(m, kart + 0x274);
		f0 = ZmRF(m, kart + 0x4D4);
		if (!Ps2F_Lt(f0, f1))
		{
			if (ZmR32(m, kart + H_K_STATE) == 4u)
			{
				ZmWF(m, kart + 0x4D4, Ps2F_Sub(f1, f21));
			}
			else
			{
				ZmW32(m, kart + 0x4D0, 0);
				d = ZmR32(m, kart + H_K_DRIVER);
				if (d != 0)
					h_driver_call(c, d, 0u); // virtual call (driver, 0)
			}
		}
	}

	// boost step (driver effect bit 5)
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0 && ExtZp_Kart_1c1ff0(c, ZmR32(m, d + 0x88), 5) != 0)
	{
		f0 = ZmRF(m, kart + 0x3F4);
		f1 = H_TENTH;
		if (Ps2F_Lt(f0, f1))
		{
			f0 = ZmRF(m, kart + 0x3EC);
			if (!Ps2F_Le(f0, f1))
			{
				f1 = ZmRF(m, kart + 0x4C8);
				f0 = 0u;
				if (Ps2F_Le(f1, f0))
				{
					ExtZp_Kart_196890(c, kart, 0xB, H_ONE);
				}
				else
				{
					f1 = ZmRF(m, kart + 0x268);
					f0 = ZmRF(m, kart + 0x4C4);
					if (Ps2F_Le(f0, f1))
						f0 = f1;
					ZmWF(m, kart + 0x4C4, f0);
					f1 = ZmRF(m, kart + 0x26C);
					f0 = ZmRF(m, kart + 0x4C8);
					if (Ps2F_Le(f0, f1))
						f0 = f1;
					ZmWF(m, kart + 0x4C8, f0);
					f1 = ZmRF(m, kart + 0x270);
					ZmWF(m, kart + 0x4CC, f1);
					f0 = ZmRF(m, kart + 0x550);
					if (Ps2F_Le(f0, f1))
						f0 = f1;
					ZmWF(m, kart + 0x550, f0);
					f0 = Ps2F_Neg(ZmRF(m, kart + 0x4C8));
					ZmWF(m, kart + 0x4D4, f0);
				}
			}
		}
	}

	// boost time left
	f0 = ZmRF(m, kart + 0x4C8);
	f1 = 0u;
	if (Ps2F_Le(f0, f1))
		return;
	f0 = Ps2F_Sub(f0, f21);
	ZmWF(m, kart + 0x4C8, f0);
	f0 = ZmRF(m, kart + 0x3F4);
	if (!Ps2F_Le(f0, f1))
	{
		ExtZp_State_1972a0(c, kart);
		return;
	}

	// slide timer and slide turn
	state = ZmR32(m, kart + H_K_STATE);
	if ((state == 6u || state == 5u) && !Ps2F_Le(ZmRF(m, kart + 0x4BC), f21))
	{
		uint32_t frame, s2, v0, ptr;
		Ps2F cosA, sinB, dotv;
		Ps2Vec4 q;
		Ps2Acc acc;

		f0 = Ps2F_Sub(ZmRF(m, kart + 0x4BC), f21);
		ZmWF(m, kart + 0x4BC, f0);
		f2 = ZmRF(m, kart + 0x4A8);
		f0 = 0u;
		if (!Ps2F_Le(f2, f0))
		{
			f1 = ZmRF(m, kart + 0x1DC);
			f0 = ZmRF(m, kart + 0x1E0);
			f0 = Ps2F_Sub(f0, f1);
			f0 = Ps2F_Mul(f2, f0);
			f20 = Ps2F_Add(f1, f0);
		}
		else
		{
			f0 = Ps2F_Neg(f2);
			f2 = ZmRF(m, kart + 0x1DC);
			f1 = ZmRF(m, kart + 0x1D8);
			f1 = Ps2F_Sub(f1, f2);
			f0 = Ps2F_Mul(f0, f1);
			f20 = Ps2F_Add(f2, f0);
		}
		frame = h_frame(m, kart);
		acc = Ps2A_Mula(ZmRF(m, frame + 0x30), ZmRF(m, frame + 0x50));
		acc = Ps2A_Madda(acc, ZmRF(m, frame + 0x34), ZmRF(m, frame + 0x54));
		f1 = Ps2A_Madd(acc, ZmRF(m, frame + 0x38), ZmRF(m, frame + 0x58));
		f0 = Ps2F_Neg(f21);
		if (Ps2F_Le(f1, 0u))
			f20 = Ps2F_Mul(f20, f0);
		else
			f20 = Ps2F_Mul(f20, f21);

		f1 = ZmRF(m, kart + 0x4BC);
		f0 = ZmRF(m, kart + 0x1D4);
		ZmW32(m, sp + 0x10C, H_ONE);
		ZmW32(m, sp + 0x108, 0);
		ZmW32(m, sp + 0x104, 0);
		ZmW32(m, sp + 0x100, 0);
		f0 = Ps2F_Div(f1, f0);
		f20 = Ps2F_Mul(f20, f0);
		cosA = ExtZp_Math_43af80(c, Ps2F_Mul(H_HALF, f20));
		ZmWF(m, sp + 0xF0, cosA);
		q = ZmRV(m, frame + H_R_UP);
		ZmWV(m, sp + 0x100, q);
		sinB = ExtZp_Math_43b320(c, Ps2F_Mul(H_HALF, f20));
		q = ZmRV(m, sp + 0x100);
		h_scale_xyz(&q, sinB);
		ZmWV(m, sp + 0x100, q);
		frame = h_frame(m, kart);
		s2 = frame + 0x40;
		ExtZp_Kart_1207f0(c, sp + 0xB0);
		f1 = ZmRF(m, sp + 0xF0);
		f0 = Ps2F_Mul(H_TWO, f1);
		f0 = Ps2F_Mul(f0, f1);
		ExtZp_Integ_1207c0(c, sp + 0xA0, s2, Ps2F_Sub(f0, H_ONE));
		ExtZp_Integ_1207b0(c, sp + 0xB0, sp + 0xA0);
		dotv = ExtZp_Integ_120780(c, sp + 0x100, s2);
		ExtZp_Integ_120750(c, sp + 0x70, sp + 0x100, dotv);
		ExtZp_Kart_120720(c, sp + 0x50, sp + 0x100, s2);
		ExtZp_Integ_120750(c, sp + 0x60, sp + 0x50, ZmRF(m, sp + 0xF0));
		ExtZp_Integ_1206f0(c, sp + 0x80, sp + 0x70, sp + 0x60);
		ExtZp_Integ_120750(c, sp + 0x90, sp + 0x80, H_TWO);
		ExtZp_Kart_1206d0(c, sp + 0xB0, sp + 0x90);
		ExtZp_Integ_1206c0(c, sp + 0xC0, sp + 0xB0);
		ZmWV(m, sp + 0xE0, ZmRV(m, sp + 0xC0));
		ptr = ExtZp_Kart_136160(c, sp + 0xE0);
		v0 = h_frame(m, kart);
		ExtZp_Kart_13dd40(c, ZmR32(m, v0 + H_R_BODY), ptr);
		ExtZp_Glue_136050(c, v0);
	}
	else
	{
		ZmW32(m, kart + 0x4BC, 0);
	}

	// target speed factors
	d = ZmR32(m, kart + H_K_DRIVER);
	f20 = ZmRF(m, kart + 0x4C4);
	if (d == 0 || ExtZp_Kart_1c1ff0(c, ZmR32(m, d + 0x88), 2) == 0)
	{
		uint32_t t = ZmR32(m, kart + 0x424);
		f20 = Ps2F_Mul(f20, ZmRF(m, t + 0xC));
	}
	{
		uint32_t t = ZmR32(m, kart + 0x424);
		f20 = Ps2F_Mul(f20, ZmRF(m, t + 0x10));
	}
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0 && (int32_t)ZmR32(m, d + 0xA0) > 0)
		f20 = Ps2F_Mul(f20, ZmRF(m, kart + 0x328));
	if (d != 0 && ExtZp_Kart_15d3b0(c, d) != 0)
		f20 = Ps2F_Mul(f20, ZmRF(m, kart + 0x334));
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0 && ExtZp_Kart_1c1ff0(c, ZmR32(m, d + 0x88), 0xB) != 0)
		f20 = Ps2F_Mul(f20, ZmRF(m, kart + 0x340));
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0 && ExtZp_Kart_1c1ff0(c, ZmR32(m, d + 0x88), 0xE) != 0)
		f20 = Ps2F_Mul(f20, ZmRF(m, kart + 0x364));
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0 && ExtZp_Kart_1c1ff0(c, ZmR32(m, d + 0x88), 0xC) != 0)
		f20 = Ps2F_Mul(f20, ZmRF(m, kart + 0x370));
	d = ZmR32(m, kart + H_K_DRIVER);
	if (d != 0 && ExtZp_Kart_1c1ff0(c, ZmR32(m, d + 0x88), 3) != 0)
	{
		uint32_t e = ZmR32(m, ZmR32(m, kart + H_K_DRIVER) + 0x88);
		if (ZmR8(m, e + 0x68) == 0)
			f20 = Ps2F_Mul(f20, ZmRF(m, kart + 0x31C));
	}

	// drive force
	f0 = ZmRF(m, kart + H_K_SPEED);
	if (!Ps2F_Lt(f0, f20))
		return;
	{
		Ps2Vec4 v;
		uint32_t frame;
		int slide;
		Ps2F f3;

		state = ZmR32(m, kart + H_K_STATE);
		v.v[0] = 0u;
		v.v[1] = 0u;
		v.v[2] = 0u;
		v.v[3] = H_ONE;
		slide = (state == 6u || state == 5u);
		if (slide)
		{
			v = ZmRV(m, h_frame(m, kart) + 0x50);
		}
		else
		{
			f1 = ZmRF(m, kart + 0x480);
			if (Ps2F_Lt(f1, 0u))
				f1 = Ps2F_Neg(f1);
			if (!Ps2F_Le(f1, H_HUNDREDTH) && ZmR8(m, kart + 0x48C) != 0)
				v = ZmRV(m, h_frame(m, kart) + 0x50);
			else
				v = ZmRV(m, h_frame(m, kart) + 0x10);
		}
		if (ZmR8(m, kart + H_K_MAGLEV) == 0)
		{
			state = ZmR32(m, kart + H_K_STATE);
			slide = (state == 6u || state == 5u);
			if (slide || ZmR8(m, kart + H_K_GROUND) == 0)
			{
				Ps2Acc acc;
				v.v[2] = 0u;
				acc = Ps2A_Mula(v.v[0], v.v[0]);
				acc = Ps2A_Madda(acc, v.v[1], v.v[1]);
				f3 = Ps2A_Madd(acc, v.v[2], v.v[2]);
				if (!Ps2F_Eq(f3, 0u))
				{
					f3 = Ps2F_Sqrt(f3);
					v.v[0] = Ps2F_Div(v.v[0], f3);
					v.v[1] = Ps2F_Div(v.v[1], f3);
					v.v[2] = Ps2F_Div(v.v[2], f3);
				}
			}
		}
		f1 = ZmRF(m, kart + H_K_SPEED);
		f0 = ZmRF(m, kart + 0x1CC);
		f1 = Ps2F_Sub(f20, f1);
		f1 = Ps2F_Div(f1, f21);
		if (!Ps2F_Lt(f1, f0))
			f1 = f0;
		frame = h_frame(m, kart);
		f0 = Ps2F_Mul(f1, ZmRF(m, ZmR32(m, frame + H_R_BODY) + H_B_MASS));
		h_scale_xyz(&v, f0);
		h_add_force(c, kart, v);
	}
}
