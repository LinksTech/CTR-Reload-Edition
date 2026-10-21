// THE ZONE PORT, PART J: the kart's contact callback (see include/platform/native_extzonephys.h
// for the conventions). Compiles alone (offline tests).
//
// ============================================================ THE CONTACT RECORD (for the integration)
// The callback is called once per contact point of the kart body by the collision phase
// (kart virtual method, vtable +0x2C): ExtZp_Contact_197e30(c, kart, contact). Reload's collision
// has to produce one such record per contact point, in the port's memory, with these fields
// (offsets from `contact`; every field not listed is never read by the callback):
//   +0x00 s32   surface type of the contact. 1..23 normal road, 24..26 anti-gravity road (the
//               anti-gravity surface and the two blaster surfaces), 27 wall, 28 and 29 reset
//               surfaces. Other values do nothing.
//   +0x10 vec4  contact point in world space (x y z; w unused), metres, z up.
//   +0x20 vec4  contact normal in world space (unit length), used when the triangle index below is -1.
//   +0x58 u8    selects the side of the contact pair that belongs to the other object: 0 = first
//               side (record Q +0x08 / +0x68), nonzero = second side (Q +0x0C / +0x6C).
//   +0x5C ptr   the contact pair record `pair`. pair+0x0C -> a record `Q`:
//                 Q+0x68 (s32) / Q+0x6C (s32) = triangle index of the first / second side; -1 = none
//                 Q+0x08 (ptr) / Q+0x0C (ptr) = the shape of the first / second side
//               If the selected triangle index is not -1 the callback asks the material query
//               (original 0x390f40, query EXT_ZP_Q_MATERIAL, func 0x390f40): a[0] = address of a
//               16-byte output buffer (the callback's stack), a[1] = the shape of the selected side,
//               a[2] = the triangle index; the query writes the contact normal (vec4) into the buffer.
//               Otherwise the normal is contact+0x20.
// Kart (`kart`, K) fields read / written:
//   +0x2C ptr -> ptr R (the kart's rigid body wrapper): R+0x10 vec (direction used for the wall slide
//               test), R+0x20 vec (the chassis up vector), R+0x40 vec, R+0x50 vec (side vectors used
//               for the wall slowdown), R+0x94 ptr = body H: H+0x30 vec4 position, H+0x80 vec4 force
//               accumulator (xyz += wall slowdown force), H+0x1B0 float mass.
//   +0x38 ptr D (driver / owner object, may be 0): D+0x88 = effect set E (E+8 = bit mask of active
//               effects); D+4 -> vtable, vtable+0x34 = virtual method (D, 0) (boost end), not ported
//               (ExtZp_Leave at the call sites 0x198284 / 0x19841c / 0x198468, execution continues).
//   +0x3E0 s32 state (4 = in the air, 5/6 = sliding, 9 = reset)
//   +0x402 u8 ground contact flag (set to 1 here), +0x410 vec4 sum of contact normals (set to the
//   up constant on the first contact of a frame, then += normal), +0x420 s32 surface type of the contact,
//   +0x428 u8 anti-gravity (maglev) state, +0x429 u8 selects the wall slowdown force (0 = +0x2D0, else +0x2E0),
//   +0x42C float speed, +0x434 float (set to 0.1 on a wall contact), +0x438 float (largest wall slide),
//   +0x440 vec4 wall direction (xyz normalised), +0x450 u8 head-on flag, +0x451 u8 wall flag,
//   +0x490 s32 (zeroed when a wall contact ends the air state), +0x4C8 float (-1.0), +0x4D0 s32,
//   +0x4D4 float, +0x4D8 u8, +0x504 u8 reset-done flag; parameters +0x5C collision radius, +0x274 boost
//   tolerance time, +0x2C0 hit stop angle (cos), +0x2C4 hit stop speed, +0x2C8 hit up-slide tolerance (cos),
//   +0x2CC hit slowdown speed min, +0x2D0 / +0x2E0 hit slowdown force (forward / reverse).
// Globals: 0x006A2160 = a vec4 constant of the original (four zeros: the zero vector).
// Calls: 0x390f40 -> EXT_ZP_Q_MATERIAL; 0x196800 (reset, a[0] = kart, a[1] = 0 for type 28, 1 for type 29)
// -> EXT_ZP_Q_OTHER.
// ============================================================

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_j.h>
#include <string.h>

#define J_G_UPVEC 0x006A2160u   // vec4 constant of the original: the zero vector (0, 0, 0, 0)
#define J_G_STATE 0x006F3568u   // gp-relative word: pointer to the game state (state+0x124 = mode, 7 = special)
#define J_G_BOOT 0x006F3B00u    // pointer to the global boot record (boot+4 -> +0xC = a word copied into the slot)

#define J_F_ONE 0x3F800000u       // 1.0 (the original's constant)
#define J_F_TWO 0x40000000u       // 2.0 (the original's constant)
#define J_F_TENTH 0x3DCCCCCDu     // 0.1 (the original's constant)
#define J_F_MINUS_ONE 0xBF800000u // -1.0 (the original's constant)

// ---- small helpers: the instruction sequences the callback repeats ----
// (a.x, a.y, a.z) . (b.x, b.y, b.z): mula, madda, madd (fs = first vector, ft = second).
static Ps2F J_Dot(ExtZpMem *m, uint32_t a, uint32_t b)
{
	Ps2Acc acc = Ps2A_Mula(ZmRF(m, a), ZmRF(m, b));
	acc = Ps2A_Madda(acc, ZmRF(m, a + 4), ZmRF(m, b + 4));
	return Ps2A_Madd(acc, ZmRF(m, a + 8), ZmRF(m, b + 8));
}
// Squared length of the vector at v (mula, madda, madd).
static Ps2F J_Len2(ExtZpMem *m, uint32_t v)
{
	return J_Dot(m, v, v);
}
// xyz of the vector at v divided by its length, in place; nothing when the squared length equals 0.
static void J_Normalize(ExtZpMem *m, uint32_t v)
{
	Ps2F len = J_Len2(m, v);
	if (!Ps2F_Eq(len, 0u))
	{
		Ps2F x, y, z;
		len = Ps2F_Sqrt(len);
		x = ZmRF(m, v);
		y = ZmRF(m, v + 4);
		z = ZmRF(m, v + 8);
		x = Ps2F_Div(x, len);
		y = Ps2F_Div(y, len);
		z = Ps2F_Div(z, len);
		ZmWF(m, v, x);
		ZmWF(m, v + 4, y);
		ZmWF(m, v + 8, z);
	}
}
// vec4 at dst: xyz += xyz of the vec4 at src (w kept).
static void J_AddXyz(ExtZpMem *m, uint32_t dst, uint32_t src)
{
	Ps2Vec4 a = ZmRV(m, dst), b = ZmRV(m, src);
	Ps2V_Add(&a, &a, &b, 0xEu);
	ZmWV(m, dst, a);
}
// vec4 at dst: xyz -= xyz of the vec4 at src (w kept).
static void J_SubXyz(ExtZpMem *m, uint32_t dst, uint32_t src)
{
	Ps2Vec4 a = ZmRV(m, dst), b = ZmRV(m, src);
	Ps2V_Sub(&a, &a, &b, 0xEu);
	ZmWV(m, dst, a);
}
// vec4 at dst: xyz *= s (w kept) (vmulx with s in the x lane).
static void J_ScaleXyz(ExtZpMem *m, uint32_t dst, Ps2F s)
{
	Ps2Vec4 a = ZmRV(m, dst);
	Ps2Vec4 sv = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&sv, 0);
	Ps2V_Mul(&a, &a, &bc, 0xEu);
	ZmWV(m, dst, a);
}

// Sets (value != 0) or clears (value == 0) the value slot of an effect object `ov`:
// ov+0x08 = value (cleared first), ov+0x0C = kind (0 cleared, 3 when value-15 < 6, else 1),
// ov+0x10 = a word of the boot record. Not set when the game state mode is 7.
void ExtZp_Contact_1b1380(ExtZpCtx *c, uint32_t ov, uint32_t value)
{
	ExtZpMem *m = c->m;
	uint32_t go;
	ZmW32(m, ov + 0x8u, 0u); // the branch delay slot: written in every case
	if (value == 0u)
	{
		ZmW32(m, ov + 0xCu, 0u);
		return;
	}
	if (ZmR32(m, ZmR32(m, J_G_STATE) + 0x124u) == 7u)
		go = 0u;
	else
	{
		go = (ZmR32(m, ov + 0x8u) == 0u) ? 1u : 0u;
		if (go != 0u)
			go = (value != 0u) ? 1u : 0u;
	}
	if (go == 0u)
		return;
	{
		uint32_t boot = ZmR32(m, J_G_BOOT);
		uint32_t w = ZmR32(m, ZmR32(m, boot + 0x4u) + 0xCu);
		ZmW32(m, ov + 0x10u, w);
		ZmW32(m, ov + 0x8u, value);
		ZmW32(m, ov + 0xCu, ((value - 0xFu) < 6u) ? 3u : 1u);
	}
}

// Clears effect bit n in the effect set e (e+8 and e+0xC) when it is set; bit 9 also empties the
// object's slot (via 0x1b1380) and e+0x80/0x84/0x6C and obj+0x168, bit 0 with flag == 0 sets obj+0x9C = 1000.
void ExtZp_Contact_1c2040(ExtZpCtx *c, uint32_t e, uint32_t n, uint32_t flag)
{
	ExtZpMem *m = c->m;
	uint32_t mask = 1u << (n & 31u);
	uint32_t v, nm;
	if ((ZmR32(m, e + 0x8u) & mask) == 0u)
		return;
	if (n == 2u)
		goto L_20e0;
	if (n == 0u)
	{
		if (flag != 0u)
			goto L_20e0;
		ZmW32(m, ZmR32(m, e + 0x4u) + 0x9Cu, 0x3E8u);
		goto L_20e0;
	}
	if (n == 9u)
	{
		uint32_t obj;
		ZmW32(m, e + 0x6Cu, 0u);
		obj = ZmR32(m, e + 0x4u);
		(void)ZmR32(m, e + 0x84u); // a2 of the call (unused by the callee)
		ExtZp_Contact_1b1380(c, ZmR32(m, obj + 0x80u), ZmR32(m, e + 0x80u));
		ZmW32(m, e + 0x80u, 0u);
		ZmW32(m, e + 0x84u, 0u);
		ZmW32(m, ZmR32(m, e + 0x4u) + 0x168u, 0u);
		goto L_20e0;
	}
L_20e0:
	v = ZmR32(m, e + 0x8u);
	nm = ~mask;
	v &= nm;
	ZmW32(m, e + 0x8u, v);
	v = ZmR32(m, e + 0xCu);
	v &= nm;
	ZmW32(m, e + 0xCu, v);
}

// The call through the driver's virtual method (D, 0) at the three sites of the callback: its target
// is not on the port's path, so it is recorded and the callback continues.
static void J_DriverCall(ExtZpCtx *c, uint32_t d, uint32_t site)
{
	uint32_t vt = ZmR32(c->m, d + 0x4u);
	(void)ZmR32(c->m, vt + 0x34u);
	ExtZp_Leave(c, site);
}

// The kart's contact callback (vtable +0x2C): kart = a0, contact = a1.
void ExtZp_Contact_197e30(ExtZpCtx *c, uint32_t kart, uint32_t contact)
{
	ExtZpMem *m = c->m;
	const uint32_t K = kart, CP = contact;
	const uint32_t sp = ExtZp_Push(c, 0x140u);
	const uint32_t N = sp + 0x130u; // the contact normal (vec4, w = 1.0 when not overwritten)
	int32_t type;
	uint32_t pair, Q, tri, R, H, D;
	uint8_t side;
	Ps2F f20 = 0u, f0, f1;

	ZmW32(m, sp + 0x138u, 0u);
	ZmW32(m, sp + 0x13Cu, J_F_ONE);
	ZmW32(m, sp + 0x134u, 0u);
	ZmW32(m, sp + 0x130u, 0u);
	side = ZmR8(m, CP + 0x58u);
	pair = ZmR32(m, CP + 0x5Cu);
	Q = ZmR32(m, pair + 0xCu);
	tri = side ? ZmR32(m, Q + 0x6Cu) : ZmR32(m, Q + 0x68u);
	if (tri != 0xFFFFFFFFu)
	{
		ExtZpQueryArgs qa;
		uint32_t shape = side ? ZmR32(m, Q + 0xCu) : ZmR32(m, Q + 0x8u);
		memset(&qa, 0, sizeof(qa));
		qa.func = 0x390F40u;
		qa.a[0] = sp + 0xA0u; // output buffer
		qa.a[1] = shape;
		qa.a[2] = tri;
		(void)ExtZp_Query(c, EXT_ZP_Q_MATERIAL, &qa);
		ZmWV(m, sp + 0xB0u, ZmRV(m, sp + 0xA0u));
		ZmWV(m, N, ZmRV(m, sp + 0xB0u));
	}
	else
		ZmWV(m, N, ZmRV(m, CP + 0x20u));
	type = (int32_t)ZmR32(m, CP);

	if (type == 0x1B)
	{
		f1 = ZmRF(m, K + 0x42Cu); // speed
		f20 = J_F_TWO;
		if (Ps2F_Le(f1, f20))
			goto L_198478;
		// ---- wall contact (type 27) at speed > 2.0
		ZmWV(m, sp + 0x120u, ZmRV(m, CP + 0x10u)); // contact point
		R = ZmR32(m, ZmR32(m, K + 0x2Cu));
		H = ZmR32(m, R + 0x94u);
		ZmWV(m, sp + 0x90u, ZmRV(m, H + 0x30u)); // body position
		J_SubXyz(m, sp + 0x120u, sp + 0x90u);
		R = ZmR32(m, ZmR32(m, K + 0x2Cu));
		f1 = J_Dot(m, sp + 0x120u, R + 0x20u);
		f0 = Ps2F_Neg(ZmRF(m, K + 0x5Cu));
		if (Ps2F_Lt(f1, f0))
			goto L_end; // the contact lies too far below the chassis plane
		D = ZmR32(m, K + 0x38u);
		if (D != 0u)
		{
			uint32_t e = ZmR32(m, D + 0x88u);
			if (ExtZp_Kart_1c1ff0(c, e, 0xDu) != 0u)
				ExtZp_Contact_1c2040(c, ZmR32(m, ZmR32(m, K + 0x38u) + 0x88u), 0xDu, 0u);
		}
		ZmW8(m, K + 0x451u, 1u);
		ZmW32(m, K + 0x434u, J_F_TENTH);
		ZmWV(m, K + 0x440u, ZmRV(m, sp + 0x120u));
		J_Normalize(m, K + 0x440u);
		R = ZmR32(m, ZmR32(m, K + 0x2Cu));
		f0 = J_Dot(m, R + 0x10u, N);
		f1 = Ps2F_Neg(f0);
		f0 = ZmRF(m, K + 0x438u);
		if (Ps2F_Le(f0, f1))
			f0 = f1;
		ZmWF(m, K + 0x438u, f0);
		if (ZmR32(m, K + 0x3E0u) == 4u)
		{
			uint32_t v0, s0;
			ZmWV(m, sp + 0x110u, ZmRV(m, N));
			H = ZmR32(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x94u);
			J_ScaleXyz(m, sp + 0x110u, ZmRF(m, H + 0x1B0u)); // impulse = normal * mass
			s0 = ZmR32(m, ZmR32(m, K + 0x2Cu));
			v0 = ExtZp_Kart_136160(c, sp + 0x110u);
			ExtZp_Kart_136110(c, ZmR32(m, s0 + 0x94u), v0);
			ExtZp_Glue_136050(c, s0);
			ZmW32(m, K + 0x490u, 0u); // delay slot of the next call
			ExtZp_Kart_1911b0(c, K, 4u, 0u);
		}
		// head-on test
		f1 = ZmRF(m, K + 0x42Cu);
		f0 = ZmRF(m, K + 0x2C4u);
		if (!Ps2F_Le(f1, f0))
		{
			uint32_t A = sp + 0x100u, B = sp + 0xF0u;
			ZmWV(m, A, ZmRV(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x40u));
			ZmWV(m, B, ZmRV(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x20u));
			R = ZmR32(m, ZmR32(m, K + 0x2Cu));
			f0 = J_Dot(m, A, R + 0x20u);
			J_ScaleXyz(m, B, f0);
			J_SubXyz(m, A, B);
			f1 = Ps2F_Sqrt(J_Len2(m, A));
			if (!Ps2F_Le(f1, 0u))
				J_Normalize(m, A);
			f0 = J_Dot(m, A, N);
			f1 = Ps2F_Neg(f0);
			f0 = ZmRF(m, K + 0x2C0u);
			if (!Ps2F_Le(f1, f0))
			{
				ZmW8(m, K + 0x450u, 1u);
				ZmW32(m, K + 0x4C8u, J_F_MINUS_ONE);
				ZmWF(m, K + 0x4D4u, Ps2F_Mul(f20, ZmRF(m, K + 0x274u)));
				ZmW8(m, K + 0x4D8u, 0u);
				D = ZmR32(m, K + 0x38u);
				if (D != 0u)
					J_DriverCall(c, D, 0x198284u);
				ZmW32(m, K + 0x4D0u, 0u); // delay slot of the next call
				ExtZp_Kart_1911b0(c, K, 2u, 0u);
				{
					uint32_t s0 = ZmR32(m, ZmR32(m, K + 0x2Cu)), v0;
					ZmWV(m, sp + 0x80u, ZmRV(m, J_G_UPVEC));
					v0 = ExtZp_Kart_136160(c, sp + 0x80u);
					ExtZp_Kart_13dd40(c, ZmR32(m, s0 + 0x94u), v0);
					ExtZp_Glue_136050(c, s0);
				}
				goto L_end;
			}
		}
		// slowdown along the wall
		f1 = ZmRF(m, K + 0x42Cu);
		f0 = ZmRF(m, K + 0x2CCu);
		if (!Ps2F_Le(f1, f0))
		{
			f1 = ZmR8(m, K + 0x429u) ? ZmRF(m, K + 0x2E0u) : ZmRF(m, K + 0x2D0u);
			if (!Ps2F_Le(f1, 0u))
			{
				uint32_t force;
				ZmWV(m, sp + 0xE0u, ZmRV(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x50u));
				f0 = Ps2F_Neg(f1);
				H = ZmR32(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x94u);
				f0 = Ps2F_Mul(f0, ZmRF(m, H + 0x1B0u));
				J_ScaleXyz(m, sp + 0xE0u, f0);
				force = ZmR32(m, ZmR32(m, ZmR32(m, K + 0x2Cu)) + 0x94u) + 0x80u;
				J_AddXyz(m, force, sp + 0xE0u);
			}
		}
		// sliding / state tests
		{
			uint32_t st = ZmR32(m, K + 0x3E0u);
			if (st == 6u || st == 5u)
			{
				R = ZmR32(m, ZmR32(m, K + 0x2Cu));
				f1 = J_Dot(m, N, R + 0x20u);
				f0 = ZmRF(m, K + 0x2C8u);
				if (!Ps2F_Lt(f1, f0))
					goto L_end;
				ZmW32(m, K + 0x4C8u, J_F_MINUS_ONE);
				ZmWF(m, K + 0x4D4u, Ps2F_Mul(f20, ZmRF(m, K + 0x274u)));
				ZmW8(m, K + 0x4D8u, 0u);
				D = ZmR32(m, K + 0x38u);
				if (D != 0u)
					J_DriverCall(c, D, 0x19841Cu);
				ZmW32(m, K + 0x4D0u, 0u);
				ExtZp_Kart_1911b0(c, K, 2u, 0u);
				goto L_end;
			}
			ZmW32(m, K + 0x4C8u, J_F_MINUS_ONE);
			ZmWF(m, K + 0x4D4u, Ps2F_Mul(f20, ZmRF(m, K + 0x274u)));
			ZmW8(m, K + 0x4D8u, 0u);
			D = ZmR32(m, K + 0x38u);
			if (D != 0u)
				J_DriverCall(c, D, 0x198468u);
			ZmW32(m, K + 0x4D0u, 0u);
			goto L_end;
		}
	}

L_198478:
	if (type < 0x18)
		goto L_198580;
	if (!(type < 0x1B))
		goto L_198580;
	// ---- types 24..26 (anti-gravity road and the blaster roads)
	if (ZmR8(m, K + 0x402u) == 0u)
	{
		ZmW8(m, K + 0x402u, 1u);
		ZmWV(m, sp + 0x70u, ZmRV(m, J_G_UPVEC));
		ZmWV(m, K + 0x410u, ZmRV(m, sp + 0x70u));
	}
	J_AddXyz(m, K + 0x410u, N);
	ZmW32(m, K + 0x420u, ZmR32(m, CP));
	if (ZmR8(m, K + 0x428u) != 0u)
		goto L_end;
	ZmWV(m, sp + 0xD0u, ZmRV(m, CP + 0x10u));
	R = ZmR32(m, ZmR32(m, K + 0x2Cu));
	H = ZmR32(m, R + 0x94u);
	ZmWV(m, sp + 0x60u, ZmRV(m, H + 0x30u));
	J_SubXyz(m, sp + 0xD0u, sp + 0x60u);
	R = ZmR32(m, ZmR32(m, K + 0x2Cu));
	f1 = J_Dot(m, sp + 0xD0u, R + 0x20u);
	if (!Ps2F_Lt(f1, 0u))
		goto L_end;
	if (ZmR8(m, K + 0x428u) == 1u)
		goto L_end;
	ZmW8(m, K + 0x428u, 1u);
	goto L_end;

L_198580:
	if (type <= 0 || !(type < 0x19))
		goto L_1986a0;
	// ---- types 1..23 (normal road)
	if (ZmR8(m, K + 0x402u) == 0u)
	{
		ZmW8(m, K + 0x402u, 1u);
		ZmWV(m, sp + 0x50u, ZmRV(m, J_G_UPVEC));
		ZmWV(m, K + 0x410u, ZmRV(m, sp + 0x50u));
		ZmW32(m, K + 0x420u, ZmR32(m, CP));
	}
	else
	{
		int32_t old = (int32_t)ZmR32(m, K + 0x420u);
		if (!(old < 0xF) && (old < 0x16))
			ZmW32(m, K + 0x420u, (uint32_t)type);
	}
	J_AddXyz(m, K + 0x410u, N);
	if (ZmR8(m, K + 0x428u) == 0u)
		goto L_end;
	ZmWV(m, sp + 0xC0u, ZmRV(m, CP + 0x10u));
	R = ZmR32(m, ZmR32(m, K + 0x2Cu));
	H = ZmR32(m, R + 0x94u);
	ZmWV(m, sp + 0x40u, ZmRV(m, H + 0x30u));
	J_SubXyz(m, sp + 0xC0u, sp + 0x40u);
	R = ZmR32(m, ZmR32(m, K + 0x2Cu));
	f1 = J_Dot(m, sp + 0xC0u, R + 0x20u);
	if (!Ps2F_Lt(f1, 0u))
		goto L_end;
	if (ZmR8(m, K + 0x428u) == 0u)
		goto L_end;
	ZmW8(m, K + 0x428u, 0u);
	goto L_end;

L_1986a0:
	{
		ExtZpQueryArgs qa;
		uint32_t arg;
		if (type == 0x1C)
			arg = 0u;
		else if (type == 0x1D)
			arg = 1u;
		else
			goto L_end;
		if (ZmR8(m, K + 0x504u) != 0u)
			goto L_end;
		if (ZmR32(m, K + 0x3E0u) == 9u)
			goto L_end;
		memset(&qa, 0, sizeof(qa));
		qa.func = 0x196800u; // reset the kart
		qa.a[0] = K;
		qa.a[1] = arg;
		(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	}

L_end:
	ExtZp_Pop(c, 0x140u);
}
