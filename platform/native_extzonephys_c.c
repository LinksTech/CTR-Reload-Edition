// THE ZONE PORT, PART C: the kart update frame and its small helpers (see
// include/platform/native_extzonephys.h for the conventions and
// native_extzonephys_c.h for the kart fields). Compiles alone too.

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_c.h>

#include <string.h>

#define KC_G_WORLD 0x006FA190u  // global: pointer to the physics world (its +0x74 is the ray test object)
#define KC_G_CLASS 0x006F63E0u  // global: the type object a controller node must be of
#define KC_G_STATES 0x006F53F0u // the state table (36 bytes per state), known to the port as a switch

// The original's vector at 0x6A2160 (read from memory: (0, 0, 0, 0) in the loaded exe,
// (0, 0, 0, 1.0) once the running game has set it up).
#define KC_G_VEC0 0x006A2160u
static Ps2Vec4 kc_zero4(ExtZpMem *m)
{
	return ZmRV(m, KC_G_VEC0);
}

// dst.xyz = a.xyz op b.xyz with w from a (lqc2, lqc2, vadd/vsub.xyz, sqc2).
static void kc_vadd(ExtZpMem *m, uint32_t dst, uint32_t a, uint32_t b)
{
	Ps2Vec4 x = ZmRV(m, a), y = ZmRV(m, b);
	Ps2V_Add(&x, &x, &y, 0xEu);
	ZmWV(m, dst, x);
}
static void kc_vsub(ExtZpMem *m, uint32_t dst, uint32_t a, uint32_t b)
{
	Ps2Vec4 x = ZmRV(m, a), y = ZmRV(m, b);
	Ps2V_Sub(&x, &x, &y, 0xEu);
	ZmWV(m, dst, x);
}
// dst.xyz = a.xyz * s (qmtc2 into vf2.x, vmulx.xyz), w from a.
static void kc_vscale(ExtZpMem *m, uint32_t dst, uint32_t a, Ps2F s)
{
	Ps2Vec4 x = ZmRV(m, a);
	Ps2Vec4 sv = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&sv, 0);
	Ps2V_Mul(&x, &x, &bc, 0xEu);
	ZmWV(m, dst, x);
}
// Dot product of two float triples: mula, madda, madd.
static Ps2F kc_dot3(Ps2F ax, Ps2F ay, Ps2F az, Ps2F bx, Ps2F by, Ps2F bz)
{
	Ps2Acc acc = Ps2A_Mula(ax, bx);
	acc = Ps2A_Madda(acc, ay, by);
	return Ps2A_Madd(acc, az, bz);
}

// ------------------------------------------------------------------ small helpers
// a.xyz += b.xyz.
uint32_t ExtZp_Kart_1206d0(ExtZpCtx *c, uint32_t a, uint32_t b)
{
	kc_vadd(c->m, a, a, b);
	return a;
}

// v = (0, 0, 0, 1).
uint32_t ExtZp_Kart_1207f0(ExtZpCtx *c, uint32_t v)
{
	ZmW32(c->m, v + 8u, 0u);
	ZmW32(c->m, v + 4u, 0u);
	ZmW32(c->m, v + 0u, 0u);
	ZmW32(c->m, v + 0xCu, PS2F_ONE);
	return v;
}

uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0;
}

uint32_t ExtZp_Kart_15d3b0(ExtZpCtx *c, uint32_t obj)
{
	return ZmR32(c->m, obj + 0x154u) != 0u;
}

uint32_t ExtZp_Kart_1a37b0(ExtZpCtx *c, uint32_t obj, uint32_t i)
{
	if ((int32_t)i < 4)
		return ZmR8(c->m, i + obj + 0x1Cu);
	return 1u;
}

uint32_t ExtZp_Kart_1c1ff0(ExtZpCtx *c, uint32_t obj, uint32_t n)
{
	return (ZmR32(c->m, obj + 8u) & (1u << (n & 31u))) != 0u;
}

uint32_t ExtZp_Kart_1c2010(ExtZpCtx *c, uint32_t obj, uint32_t n)
{
	return (ZmR32(c->m, obj + 0xCu) & (1u << (n & 31u))) != 0u;
}

// The driver object's method at its table slot +0x34, called with (driver, value): for a
// computer driver (0x15DB60) it stores the value at driver + 0xCC; the player's (0x16D760)
// keeps the screen's boost counters and is handed to Reload as a query.
void ExtZp_Kart_DriverSlot34(ExtZpCtx *c, uint32_t driver, uint32_t value)
{
	ExtZpMem *m = c->m;
	const uint32_t target = ZmR32(m, ZmR32(m, driver + 4u) + 0x34u);
	if (target == 0x0015DB60u)
	{
		ZmW32(m, driver + 0xCCu, value);
	}
	else
	{
		ExtZpQueryArgs args;
		memset(&args, 0, sizeof(args));
		args.func = target;
		args.a[0] = driver;
		args.a[1] = value;
		(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &args);
	}
}

uint32_t ExtZp_Kart_512ee0(ExtZpCtx *c, uint32_t obj, uint32_t cls)
{
	ExtZpMem *m = c->m;
	ExtZpQueryArgs args;
	ExtZpQueryResult r;
	uint32_t v0;
	uint32_t i;
	// the virtual call: the object's table (+0) at slot +0x4C
	for (i = 0; i < 8; i++)
	{
		args.a[i] = 0;
		args.f[i] = 0;
	}
	args.a[0] = obj;
	args.a[1] = cls;
	args.func = ZmR32(m, ZmR32(m, obj) + 0x4Cu);
	if (args.func == 0x00136160u)
	{
		// the known target: the object answers with itself
		v0 = ExtZp_Kart_136160(c, obj);
	}
	else if (args.func == 0x0028AA30u)
	{
		// the gravity spline's type method (0x28AA30): its type node, the word at 0x6F63E0
		v0 = ZmR32(m, 0x006F63E0u);
	}
	else
	{
		// any other target: not on the path, answered by the query
		r = ExtZp_Query(c, EXT_ZP_Q_OTHER, &args);
		v0 = r.v0;
	}
	while (v0 != 0u)
	{
		if (v0 == cls)
			return 1u;
		v0 = ZmR32(m, v0 + 0x38u);
	}
	return 0u;
}

// ------------------------------------------------------------------ the state table
// Slot kinds: 0 enter, 1 update, 2 exit. Whether the original's table holds
// something there (states 1..14 as set up by its start-up code).
static int kc_slot_used(uint32_t state, uint32_t slot)
{
	switch (slot)
	{
	case 0: // enter
		return state == 1 || state == 4 || state == 5 || state == 6 || state == 7 || state == 9 || state == 13 || state == 14;
	case 1: // update
		return state >= 1 && state <= 13;
	case 2: // exit
		return state == 4 || state == 5 || state == 6 || state == 9 || state == 14;
	}
	return 0;
}
// mp -> state and slot kind; returns 0 when mp is outside the table (states 0..14).
static int kc_decode(uint32_t mp, uint32_t *state, uint32_t *slot)
{
	uint32_t o;
	if (mp < KC_G_STATES)
		return 0;
	o = mp - KC_G_STATES;
	if (o >= 36u * 15u || (o % 12u) != 0u)
		return 0;
	*state = o / 36u;
	*slot = (o % 36u) / 12u;
	return 1;
}

uint32_t ExtZp_Kart_4ba960(ExtZpCtx *c, uint32_t mp)
{
	uint32_t state, slot;
	(void)c;
	if (!kc_decode(mp, &state, &slot))
		return 0u;
	return kc_slot_used(state, slot) ? 1u : 0u;
}

// The handler address of a slot (for the record of a branch the port does not follow).
static uint32_t kc_target(uint32_t state, uint32_t slot)
{
	static const uint32_t enter[15] = {0, 0x192840u, 0, 0, 0x192860u, 0x192890u, 0x192890u, 0x192960u, 0, 0x192c80u, 0, 0, 0, 0x192d40u, 0x192d70u};
	static const uint32_t update[15] = {0, 0x192e80u, 0x192fc0u, 0x192fc0u, 0x1936b0u, 0x1938d0u, 0x1938d0u, 0x194280u, 0x1944c0u, 0x194720u, 0x194850u, 0x1948d0u, 0x1949b0u, 0x194bc0u, 0};
	static const uint32_t exitt[15] = {0, 0, 0, 0, 0x194d20u, 0x194dd0u, 0x194dd0u, 0, 0, 0x194f70u, 0, 0, 0, 0, 0x194f90u};
	return slot == 0 ? enter[state] : (slot == 1 ? update[state] : exitt[state]);
}

uint32_t ExtZp_Kart_4ba920(ExtZpCtx *c, uint32_t mp, uint32_t a0, uint32_t a1, uint32_t a2, Ps2F f12)
{
	uint32_t state, slot;
	if (!kc_decode(mp, &state, &slot) || !kc_slot_used(state, slot))
	{
		ExtZp_Leave(c, mp);
		return 0u;
	}
	if (slot == 0)
	{
		switch (state)
		{
		case 4: return ExtZp_State_192860(c, a0);
		case 5:
		case 6: return ExtZp_State_192890(c, a0);
		}
		// the other enter handlers (states 1, 7, 9, 13, 14) lie outside the anti-gravity path
		ExtZp_Leave(c, kc_target(state, slot));
		return 0u;
	}
	else if (slot == 1)
	{
		switch (state)
		{
		case 2:
		case 3: ExtZp_State_192fc0(c, a0, f12); break;
		case 4: ExtZp_State_1936b0(c, a0, f12); break;
		case 5:
		case 6: ExtZp_State_1938d0(c, a0, f12); break;
		default: ExtZp_Leave(c, kc_target(state, slot)); break; // states 1, 7..13: outside the path
		}
		return 0u;
	}
	else
	{
		switch (state)
		{
		case 4: return ExtZp_State_194d20(c, a0);
		case 5:
		case 6: return ExtZp_State_194dd0(c, a0);
		}
		// the exit handlers of states 9 and 14 lie outside the path
		ExtZp_Leave(c, kc_target(state, slot));
		return 0u;
	}
	return 0u;
}

#define KC_MP(state, slotOff) (KC_G_STATES + 36u * (state) + (slotOff))

void ExtZp_Kart_190f60(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t arg)
{
	ExtZpMem *m = c->m;
	uint32_t s1 = ZmR32(m, kart + 0x3E0u); // old state
	uint32_t s2 = arg;
	uint32_t mp;
	uint32_t v0;
	ZmW32(m, kart + 0x3E0u, state);
	// exit handler of the old state
	mp = KC_MP(s1, 0x18u);
	if (ExtZp_Kart_4ba960(c, mp))
	{
		v0 = ExtZp_Kart_4ba920(c, mp, kart, state, s2, 0u);
		if ((v0 & 0xFFu) == 0u)
		{
			ZmW32(m, kart + 0x3E0u, s1); // change taken back
			return;
		}
	}
	// enter handler of the new state
	mp = KC_MP(state, 0u);
	if (!ExtZp_Kart_4ba960(c, mp))
		return;
	s2 = ExtZp_Kart_4ba920(c, mp, kart, s1, s2, 0u);
	if (s2 == 0u)
		return;
	// redirect: the new state is the handler's result
	ZmW32(m, kart + 0x3E0u, 0u);
	s1 = ZmR32(m, kart + 0x3E0u);
	ZmW32(m, kart + 0x3E0u, s2);
	mp = KC_MP(s1, 0x18u);
	if (ExtZp_Kart_4ba960(c, mp))
	{
		// (state 0 holds no exit handler; the original would pass a stale a2 here)
		ExtZp_Leave(c, 0x00190F60u);
		return;
	}
	mp = KC_MP(s2, 0u);
	if (!ExtZp_Kart_4ba960(c, mp))
		return;
	s2 = ExtZp_Kart_4ba920(c, mp, kart, s1, 0u, 0u);
	if (s2 == 0u)
		return;
	ZmW32(m, kart + 0x3E0u, 0u);
	s1 = ZmR32(m, kart + 0x3E0u);
	ZmW32(m, kart + 0x3E0u, s2);
	mp = KC_MP(s1, 0x18u);
	if (ExtZp_Kart_4ba960(c, mp))
	{
		ExtZp_Leave(c, 0x00190F60u);
		return;
	}
	mp = KC_MP(s2, 0u);
	if (!ExtZp_Kart_4ba960(c, mp))
		return;
	v0 = ExtZp_Kart_4ba920(c, mp, kart, s1, 0u, 0u);
	if (v0 == 0u)
		return;
	ZmW32(m, kart + 0x3E0u, 0u);
	ExtZp_Kart_190f60(c, kart, v0, 0u);
}

void ExtZp_Kart_1911b0(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t arg)
{
	if (ZmR32(c->m, kart + 0x38u) != 0u || (state - 2u) < 5u)
		ExtZp_Kart_190f60(c, kart, state, arg);
}

// ------------------------------------------------------------------ the kart update
void ExtZp_Kart_190670(ExtZpCtx *c, uint32_t K, Ps2F dt)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x1E0u);
	uint32_t E, H, s0;
	Ps2F f0, f1, f2, f3;
	Ps2Acc acc;
	uint32_t v0;

	if (ZmR32(m, K + 0x38u) == 0u && ZmR32(m, K + 0x3Cu) == 0u)
		goto done;

	// 1. is the force accumulator (K+0x3B0) non-empty?
	f1 = kc_dot3(ZmRF(m, K + 0x3B0u), ZmRF(m, K + 0x3B4u), ZmRF(m, K + 0x3B8u), ZmRF(m, K + 0x3B0u), ZmRF(m, K + 0x3B4u), ZmRF(m, K + 0x3B8u));
	f0 = 0u;
	if (!Ps2F_Le(f1, f0))
	{
		// n = unit(force) (left unnormalised when its squared length is zero)
		ZmWV(m, sp + 0x1A0u, ZmRV(m, K + 0x3B0u));
		f3 = kc_dot3(ZmRF(m, sp + 0x1A0u), ZmRF(m, sp + 0x1A4u), ZmRF(m, sp + 0x1A8u), ZmRF(m, sp + 0x1A0u), ZmRF(m, sp + 0x1A4u), ZmRF(m, sp + 0x1A8u));
		if (!Ps2F_Eq(f3, f0))
		{
			f3 = Ps2F_Sqrt(f3);
			f2 = ZmRF(m, sp + 0x1A0u);
			f1 = ZmRF(m, sp + 0x1A4u);
			f0 = ZmRF(m, sp + 0x1A8u);
			f2 = Ps2F_Div(f2, f3);
			f1 = Ps2F_Div(f1, f3);
			f0 = Ps2F_Div(f0, f3);
			ZmWF(m, sp + 0x1A0u, f2);
			ZmWF(m, sp + 0x1A4u, f1);
			ZmWF(m, sp + 0x1A8u, f0);
		}
		// relative velocity: (E + 0x40) - (K + 0x3D0)
		E = ZmR32(m, ZmR32(m, K + 0x2Cu));
		ZmWV(m, sp + 0x190u, ZmRV(m, E + 0x40u));
		kc_vsub(m, sp + 0x190u, sp + 0x190u, K + 0x3D0u);
		f1 = kc_dot3(ZmRF(m, sp + 0x1A0u), ZmRF(m, sp + 0x1A4u), ZmRF(m, sp + 0x1A8u), ZmRF(m, sp + 0x190u), ZmRF(m, sp + 0x194u), ZmRF(m, sp + 0x198u));
		f0 = 0u;
		if (!Ps2F_Le(f1, f0))
		{
			// the part along the force, at most its length, is given back as an impulse
			if (!Ps2F_Lt(f1, f3))
				f1 = f3;
			ZmWV(m, sp + 0x180u, ZmRV(m, K + 0x3B0u));
			f0 = Ps2F_Div(f1, f3);
			H = ZmR32(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x94u);
			f2 = ZmRF(m, H + 0x1B0u);
			f1 = 0xBF800000u; // -1.0 (the original's constant)
			f1 = Ps2F_Mul(f1, f2);
			f0 = Ps2F_Mul(f0, f1);
			kc_vscale(m, sp + 0x180u, sp + 0x180u, f0);
			s0 = ZmR32(m, ZmR32(m, K + 0x2Cu));
			v0 = ExtZp_Kart_136160(c, sp + 0x180u);
			ExtZp_Kart_136110(c, ZmR32(m, s0 + 0x94u), v0);
			ExtZp_Glue_136050(c, s0);
		}
		// the force accumulator and the last velocity are cleared
		ZmWV(m, K + 0x3B0u, kc_zero4(m));
		ZmWV(m, K + 0x3D0u, kc_zero4(m));
	}

	// 2. the force of this call joins the accumulator, the call's force is cleared
	kc_vadd(m, K + 0x3B0u, K + 0x3B0u, K + 0x3C0u);
	ZmWV(m, K + 0x3C0u, kc_zero4(m));

	// 3. state 14: the body is stopped, nothing else runs
	if (ZmR32(m, K + 0x3E0u) == 0xEu)
	{
		s0 = ZmR32(m, ZmR32(m, K + 0x2Cu));
		ZmWV(m, sp + 0x110u, kc_zero4(m));
		v0 = ExtZp_Kart_136160(c, sp + 0x110u);
		ExtZp_Kart_13dd40(c, ZmR32(m, s0 + 0x94u), v0);
		ExtZp_Glue_136050(c, s0);
		s0 = ZmR32(m, ZmR32(m, K + 0x2Cu));
		ZmWV(m, sp + 0x100u, kc_zero4(m));
		v0 = ExtZp_Kart_136160(c, sp + 0x100u);
		ExtZp_Kart_190350(c, ZmR32(m, s0 + 0x94u), v0);
		ExtZp_Glue_190280(c, s0);
		goto done;
	}

	// 4. zones, ground probe
	ExtZp_Kart_1911f0(c, K);
	ZmW8(m, K + 0x504u, 0u);
	ExtZp_Kart_191bc0(c, K);

	// 5. air time and the reset test (a ray 50 m down the kart's axis)
	if (ZmR8(m, K + 0x402u) != 0u)
	{
		ZmW32(m, K + 0x528u, 0u);
	}
	else if (ZmR32(m, K + 0x3E0u) != 9u && ZmR32(m, K + 0x3E0u) != 1u)
	{
		f1 = ZmRF(m, K + 0x528u);
		f0 = 0x41000000u; // 8.0 (the original's constant)
		f1 = Ps2F_Add(f1, dt);
		ZmWF(m, K + 0x528u, f1);
		if (!Ps2F_Le(f1, f0))
		{
			ExtZpQueryArgs qa;
			ExtZpQueryResult qr;
			uint32_t i;
			ZmW32(m, sp + 0x178u, 0u);
			ZmW32(m, sp + 0x17Cu, PS2F_ONE);
			ZmW32(m, sp + 0x174u, 0u);
			ZmW32(m, sp + 0x170u, 0u);
			E = ZmR32(m, ZmR32(m, K + 0x2Cu));
			ZmWV(m, sp + 0x170u, ZmRV(m, E + 0x20u));
			kc_vscale(m, sp + 0x170u, sp + 0x170u, 0xC2480000u); // -50.0 (the original's constant)
			H = ZmR32(m, E + 0x94u);
			ZmWV(m, sp + 0xF0u, ZmRV(m, H + 0x30u));
			kc_vadd(m, sp + 0x170u, sp + 0x170u, sp + 0xF0u);
			ZmWV(m, sp + 0xE0u, ZmRV(m, H + 0x30u));
			for (i = 0; i < 8; i++)
			{
				qa.a[i] = 0;
				qa.f[i] = 0;
			}
			qa.a[0] = ZmR32(m, ZmR32(m, KC_G_WORLD) + 0x74u);
			qa.a[1] = sp + 0xE0u;
			qa.a[2] = sp + 0x170u;
			qa.func = 0x00338700u;
			qr = ExtZp_Query(c, EXT_ZP_Q_RAY, &qa);
			if (qr.v0 == 0u)
			{
				ZmW32(m, K + 0x528u, 0u);
				ZmW8(m, K + 0x505u, 0u);
				ExtZp_Kart_1911b0(c, K, 9u, 0u);
			}
		}
	}

	// 6. the steps
	ExtZp_Kart_191f60(c, K);
	ExtZp_Kart_192220(c, K);
	ExtZp_Kart_192650(c, K);
	ExtZp_Kart_1927a0(c, K, dt);

	// 7. the angular velocity of the body is cleared
	s0 = ZmR32(m, ZmR32(m, K + 0x2Cu));
	ZmWV(m, sp + 0xD0u, kc_zero4(m));
	v0 = ExtZp_Kart_136160(c, sp + 0xD0u);
	ExtZp_Kart_190350(c, ZmR32(m, s0 + 0x94u), v0);
	ExtZp_Glue_190280(c, s0);

	// 8. the target up vector: K+0x410, or (maglev with spline nodes) minus the spline's direction
	ZmW32(m, sp + 0x168u, 0u);
	ZmW32(m, sp + 0x164u, 0u);
	ZmW32(m, sp + 0x16Cu, PS2F_ONE);
	ZmW32(m, sp + 0x160u, 0u);
	{
		int viaSpline = 0;
		if (ZmR8(m, K + 0x428u) != 0u)
		{
			uint32_t n464 = ZmR32(m, K + 0x464u);
			if (n464 != 0xFFFFFFFFu && ZmR32(m, K + 0x46Cu) != 0xFFFFFFFFu)
			{
				uint32_t spl = ZmR32(m, ZmR32(m, ZmR32(m, ZmR32(m, K + 0xACu) + 8u) + 0x10u) + (n464 << 2));
				if (spl != 0u && ExtZp_Kart_512ee0(c, spl, ZmR32(m, KC_G_CLASS)) != 0u)
					s0 = spl;
				else
					s0 = 0u;
				viaSpline = 1;
				// the body position as the start, copied through two slots
				E = ZmR32(m, ZmR32(m, K + 0x2Cu));
				ZmWV(m, sp + 0xC0u, ZmRV(m, ZmR32(m, E + 0x94u) + 0x30u));
				f1 = ZmRF(m, sp + 0xC4u);
				f2 = ZmRF(m, sp + 0xC0u);
				f0 = ZmRF(m, sp + 0xC8u);
				ZmWF(m, sp + 0x1B4u, f1);
				f1 = ZmRF(m, sp + 0x1B4u);
				ZmWF(m, sp + 0x1C0u, f2);
				ZmWF(m, sp + 0x1B8u, f0);
				f0 = ZmRF(m, sp + 0x1B8u);
				ZmWF(m, sp + 0x1C4u, f1);
				ZmWF(m, sp + 0x1C8u, f0);
				ZmWF(m, sp + 0x1B0u, f2);
				ExtZp_Kart_28b600(c, s0, ZmR32(m, K + 0x46Cu), sp + 0x1C0u, sp + 0x1D0u);
				f1 = ZmRF(m, sp + 0x1D4u);
				f0 = ZmRF(m, sp + 0x1D8u);
				f3 = 0xBF800000u; // -1.0
				f2 = ZmRF(m, sp + 0x1D0u);
				ZmW32(m, sp + 0x5Cu, PS2F_ONE);
				f1 = Ps2F_Mul(f1, f3);
				f0 = Ps2F_Mul(f0, f3);
				f2 = Ps2F_Mul(f2, f3);
				ZmWF(m, sp + 0x1D4u, f1);
				ZmWF(m, sp + 0x1D8u, f0);
				f1 = ZmRF(m, sp + 0x1D4u);
				f0 = ZmRF(m, sp + 0x1D8u);
				ZmWF(m, sp + 0x1D0u, f2);
				ZmWF(m, sp + 0x50u, f2);
				ZmWF(m, sp + 0x54u, f1);
				ZmWF(m, sp + 0x58u, f0);
				ZmWV(m, sp + 0xB0u, ZmRV(m, sp + 0x50u));
				ZmWV(m, sp + 0x160u, ZmRV(m, sp + 0xB0u));
			}
		}
		if (!viaSpline)
			ZmWV(m, sp + 0x160u, ZmRV(m, K + 0x410u));
	}
	ExtZp_Kart_197510(c, K, sp + 0x160u, dt, 0x3DCCCCCDu); // 0.1 (the original's constant)

	// 9. speed: the body's velocity (E + 0x40) in the kart frame (maglev) or its horizontal part
	if (ZmR8(m, K + 0x428u) != 0u)
	{
		E = ZmR32(m, ZmR32(m, K + 0x2Cu));
		f2 = ZmRF(m, E + 0x40u);
		f0 = ZmRF(m, E + 0x30u);
		f1 = ZmRF(m, E + 0x44u);
		acc = Ps2A_Mula(f2, f0);
		f0 = ZmRF(m, E + 0x34u);
		f2 = ZmRF(m, E + 0x48u);
		acc = Ps2A_Madda(acc, f1, f0);
		f0 = ZmRF(m, E + 0x38u);
		f3 = Ps2A_Madd(acc, f2, f0);
		f2 = ZmRF(m, E + 0x40u);
		f0 = ZmRF(m, E + 0x10u);
		f1 = ZmRF(m, E + 0x44u);
		acc = Ps2A_Mula(f2, f0);
		f0 = ZmRF(m, E + 0x14u);
		f2 = ZmRF(m, E + 0x48u);
		acc = Ps2A_Madda(acc, f1, f0);
		f0 = ZmRF(m, E + 0x18u);
		f0 = Ps2A_Madd(acc, f2, f0);
		acc = Ps2A_Mula(f3, f3);
		f0 = Ps2A_Madd(acc, f0, f0);
		f0 = Ps2F_Sqrt(f0);
		ZmWF(m, K + 0x42Cu, f0);
	}
	else
	{
		E = ZmR32(m, ZmR32(m, K + 0x2Cu));
		f1 = ZmRF(m, E + 0x44u);
		f0 = ZmRF(m, E + 0x40u);
		acc = Ps2A_Mula(f1, f1);
		f0 = Ps2A_Madd(acc, f0, f0);
		f0 = Ps2F_Sqrt(f0);
		ZmWF(m, K + 0x42Cu, f0);
	}

	// 10. standstill time
	f1 = ZmRF(m, K + 0x42Cu);
	f0 = 0x3DCCCCCDu; // 0.1 (the original's constant)
	if (Ps2F_Le(f1, f0))
	{
		f0 = ZmRF(m, K + 0x54Cu);
		f0 = Ps2F_Add(f0, dt);
		ZmWF(m, K + 0x54Cu, f0);
	}
	else
	{
		f1 = ZmRF(m, K + 0x3ECu);
		f0 = 0u;
		if (!Ps2F_Le(f1, f0))
			ZmWF(m, K + 0x54Cu, f0);
	}

	// 11. the state's update handler (table slot +0xC)
	{
		uint32_t st = ZmR32(m, K + 0x3E0u);
		if (ExtZp_Kart_4ba960(c, KC_MP(st, 0xCu)))
		{
			uint32_t st2 = ZmR32(m, K + 0x3E0u);
			ExtZp_Kart_4ba920(c, KC_MP(st2, 0xCu), K, st2, 0u, dt);
		}
	}

	// 12. grip and the climb cap
	ExtZp_Kart_1979d0(c, K);
	ExtZp_Kart_1963d0(c, K, dt);

	// 13. leftover force (K+0x3B0): given to the body as velocity change
	f1 = kc_dot3(ZmRF(m, K + 0x3B0u), ZmRF(m, K + 0x3B4u), ZmRF(m, K + 0x3B8u), ZmRF(m, K + 0x3B0u), ZmRF(m, K + 0x3B4u), ZmRF(m, K + 0x3B8u));
	f0 = 0u;
	if (!Ps2F_Le(f1, f0))
	{
		E = ZmR32(m, ZmR32(m, K + 0x2Cu));
		ZmWV(m, K + 0x3D0u, ZmRV(m, E + 0x40u));
		H = ZmR32(m, E + 0x94u);
		ZmWV(m, sp + 0x70u, ZmRV(m, H + 0x80u));
		f0 = ZmRF(m, H + 0x1B0u);
		f3 = ZmRF(m, sp + 0x70u);
		f2 = ZmRF(m, sp + 0x74u);
		f1 = ZmRF(m, sp + 0x78u);
		f3 = Ps2F_Div(f3, f0);
		f2 = Ps2F_Div(f2, f0);
		f1 = Ps2F_Div(f1, f0);
		ZmWF(m, sp + 0x80u, f3);
		ZmWF(m, sp + 0x84u, f2);
		ZmWF(m, sp + 0x88u, f1);
		ZmWF(m, sp + 0x8Cu, PS2F_ONE);
		kc_vscale(m, sp + 0x40u, sp + 0x80u, dt);
		ZmWV(m, sp + 0x90u, ZmRV(m, sp + 0x40u));
		ZmWV(m, sp + 0xA0u, ZmRV(m, sp + 0x90u));
		kc_vadd(m, K + 0x3D0u, K + 0x3D0u, sp + 0xA0u);
		ZmWV(m, sp + 0x150u, ZmRV(m, K + 0x3B0u));
		f0 = ZmRF(m, H + 0x1B0u);
		kc_vscale(m, sp + 0x150u, sp + 0x150u, f0);
		s0 = ZmR32(m, ZmR32(m, K + 0x2Cu));
		v0 = ExtZp_Kart_136160(c, sp + 0x150u);
		ExtZp_Kart_136110(c, ZmR32(m, s0 + 0x94u), v0);
		ExtZp_Glue_136050(c, s0);
	}

	// 14. timers
	ZmW8(m, K + 0x402u, 0u);
	f0 = 0u;
	f1 = ZmRF(m, K + 0x434u);
	f1 = Ps2F_Sub(f1, dt);
	ZmWF(m, K + 0x434u, f1);
	if (Ps2F_Le(f1, f0))
	{
		ZmWF(m, K + 0x438u, f0);
		ZmWV(m, K + 0x440u, kc_zero4(m));
		ZmW8(m, K + 0x450u, 0u);
		ZmW8(m, K + 0x451u, 0u);
		ZmWF(m, K + 0x434u, f0);
	}
	f1 = ZmRF(m, K + 0x454u);
	f0 = 0u;
	f1 = Ps2F_Sub(f1, dt);
	ZmWF(m, K + 0x454u, f1);
	if (Ps2F_Le(f1, f0))
		ZmW8(m, K + 0x458u, 0u);
	f0 = ZmRF(m, K + 0x45Cu);
	f0 = Ps2F_Add(f0, dt);
	ZmWF(m, K + 0x45Cu, f0);

done:
	ExtZp_Pop(c, 0x1E0u);
}
