// THE ZONE PORT, PENETRATION HANDLING, PART PB: the time search of one pair, the body set back to a time,
// and the two record vector helpers (see include/platform/native_extzonecontact.h and
// include/platform/native_extzonephys.h for the conventions; prefix Pen).
// Compiles alone too (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonecontact.h>
#include <platform/native_extzonephys_s3.h>


#define PEN_ONE 0x3F800000u          // 1.0 (the original's constant)
#define PEN_HALF 0x3F000000u         // 0.5 (the original's constant)
#define PEN_EPS 0x38D1B717u          // 9.99999975e-05 (the original's constant)
#define PEN_XYZ 0xEu                 // vector lane mask x y z
#define PEN_VT_BASE 0x006CC390u      // record class table (base form)
#define PEN_VT_RECORD 0x006CC3A0u    // record class table (slot +8 = destructor 0x13e130)
#define PEN_DTOR_RECORD 0x0013E130u  // the record destructor
#define PEN_BODY_SLOT_SET_TIME 0x398C80u // body table slot +0x2C: set the body to a time
#define PEN_SUB_SLOT_TOL 0x3B2600u   // subspace table slot +0x20: the detector's tolerance

// A call to the collision detector's boundary: the original's address and its a0..a3, t0.
static void Pen_Collide(ExtZpCtx *c, uint32_t func, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0)
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
	args.a[2] = a2;
	args.a[3] = a3;
	args.a[4] = t0;
	args.func = func;
	(void)ExtZp_Query(c, EXT_ZP_Q_COLLIDE, &args);
}

// The virtual call "body->table[+0x2C](body, t)": sets the body to the time t.
static void Pen_SetTime(ExtZpCtx *c, uint32_t body, Ps2F t)
{
	uint32_t t9 = ZmR32(c->m, ZmR32(c->m, body + 0u) + 0x2Cu);
	if (t9 == PEN_BODY_SLOT_SET_TIME)
		ExtZp_Pen_398c80(c, body, t);
	else
		ExtZp_Leave(c, t9);
}

// The virtual call "subspace->table[+0x20](subspace)": the detector's tolerance (f0).
static Ps2F Pen_Tolerance(ExtZpCtx *c, uint32_t sub)
{
	uint32_t t9 = ZmR32(c->m, ZmR32(c->m, sub + 0u) + 0x20u);
	if (t9 == PEN_SUB_SLOT_TOL)
		return ExtZp_Step_3b2600(c, sub);
	ExtZp_Leave(c, t9);
	return 0u;
}

// The original's division by 0x80 of a pointer difference (shift with the sign correction).
static uint32_t Pen_Div128(uint32_t v)
{
	if ((int32_t)v >= 0)
		return (uint32_t)((int32_t)v >> 7);
	return (uint32_t)((int32_t)(v + 0x7Fu) >> 7);
}

// ------------------------------------------------------------------ 0x398c80
// A rigid body set back to the time fraction t of its last step (body table slot +0x2C). The body keeps a
// saved state from before the step (position +0x150, orientation scalar +0x160, orientation axis +0x170,
// velocity +0x180, angular rate +0x190, step length +0x1A0). Nothing happens when the body is flagged
// (bits 0 or 2 of +0x18) or when t is already the body's time (+0x14). For t == 0 the saved position,
// scalar and axis are copied back. Otherwise one Euler step from the saved state with t * step length:
// position = saved + velocity * t', orientation = saved + 0.5 * (angular rate * saved orientation) * t',
// renormalised. Then the rotation matrix (+0xE0) and its translation row (+0x110), the world inertia
// (+0x120 .. +0x14C), the step counter (+0x294) and the derived data (0x399ca0) are renewed; +0x14 = t.
void ExtZp_Pen_398c80(ExtZpCtx *c, uint32_t H, Ps2F t)
{
	ExtZpMem *m = c->m;
	uint32_t sp;
	uint32_t v1, a0;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f20, f21;
	Ps2Acc acc;
	Ps2Vec4 va, vb;
	uint32_t v;

	f21 = t;
	v1 = (uint32_t)(int32_t)(int8_t)ZmR8(m, H + 0x18u);
	a0 = (v1 & 4u) ? 0xFFFFFFFFu : 0u; // bit 2 of +0x18 (dsll32 / dsra32)
	v1 = (v1 & 1u) ? 0xFFFFFFFFu : 0u; // bit 0 of +0x18
	v1 = a0 | v1;
	if (v1 != 0u)
		return;
	f0 = ZmRF(m, H + 0x14u); // the body's current time
	if (Ps2F_Eq(f21, f0))
		return;
	f0 = 0u;
	if (Ps2F_Eq(f0, f21))
	{
		// t == 0: copy the saved state back
		ZmWV(m, H + 0x30u, ZmRV(m, H + 0x150u));
		f0 = ZmRF(m, H + 0x160u);
		ZmWF(m, H + 0x40u, f0);
		ZmWV(m, H + 0x50u, ZmRV(m, H + 0x170u));
		sp = ExtZp_Push(c, 0x140);
		goto L_398f44;
	}
	sp = ExtZp_Push(c, 0x140);
	f0 = ZmRF(m, H + 0x1A0u); // length of the last step
	f20 = Ps2F_Mul(f21, f0);  // t' = t * step length
	ExtZp_Integ_1207c0(c, sp + 0x70u, H + 0x180u, f20);
	va = ZmRV(m, H + 0x150u);
	vb = ZmRV(m, sp + 0x70u);
	Ps2V_Add(&va, &va, &vb, PEN_XYZ);
	ZmWV(m, sp + 0x120u, va);
	ZmWV(m, sp + 0x60u, ZmRV(m, sp + 0x120u));
	ZmWV(m, H + 0x30u, ZmRV(m, sp + 0x60u));
	ZmW32(m, sp + 0x48u, 0u);
	ZmW32(m, sp + 0x4Cu, PEN_ONE);
	ZmW32(m, sp + 0x44u, 0u);
	ZmW32(m, sp + 0x40u, 0u);
	ZmW32(m, sp + 0x30u, 0u);
	ZmWV(m, sp + 0x40u, ZmRV(m, H + 0x190u)); // the saved angular rate (x y z, w)
	f5 = ZmRF(m, H + 0x178u);
	f6 = ZmRF(m, H + 0x174u);
	f11 = ZmRF(m, sp + 0x44u);
	f7 = ZmRF(m, sp + 0x48u);
	f14 = ZmRF(m, sp + 0x40u);
	f13 = ZmRF(m, H + 0x170u);
	f4 = ZmRF(m, H + 0x160u);
	f0 = Ps2F_Sub(f6, f5);
	f1 = Ps2F_Sub(f7, f11);
	f10 = Ps2F_Mul(f1, f0);
	f3 = ZmRF(m, sp + 0x30u);
	f1 = Ps2F_Add(f4, f13);
	f2 = Ps2F_Sub(f7, f14);
	f0 = Ps2F_Add(f3, f14);
	f9 = Ps2F_Mul(f0, f1);
	f1 = Ps2F_Sub(f3, f14);
	f0 = Ps2F_Add(f6, f5);
	f8 = Ps2F_Mul(f1, f0);
	f1 = Ps2F_Sub(f4, f13);
	f0 = Ps2F_Add(f7, f11);
	acc = Ps2A_Mula(f1, f0);
	f1 = Ps2F_Add(f7, f14);
	f0 = Ps2F_Sub(f13, f6);
	f7 = Ps2F_Mul(f2, f0);
	f0 = Ps2F_Add(f13, f6);
	f6 = Ps2F_Mul(f1, f0);
	f2 = Ps2F_Sub(f4, f5);
	f1 = Ps2F_Add(f4, f5);
	f0 = Ps2F_Add(f3, f11);
	f5 = Ps2F_Mul(f0, f2);
	f0 = Ps2F_Sub(f3, f11);
	f4 = Ps2F_Mul(f0, f1);
	f0 = Ps2F_Add(f6, f5);
	f1 = Ps2F_Add(f4, f0);
	f2 = Ps2F_Add(f7, f1);
	f12 = PEN_HALF;
	f3 = Ps2F_Mul(f12, f2);
	f0 = Ps2F_Add(f10, f3);
	f0 = Ps2F_Sub(f0, f6);
	ZmWF(m, sp + 0x30u, f0);
	f0 = Ps2F_Add(f9, f3);
	f1 = Ps2F_Sub(f0, f1);
	f0 = Ps2F_Add(f8, f3);
	f0 = Ps2F_Sub(f0, f4);
	ZmWF(m, sp + 0x44u, f0);
	f0 = Ps2A_Madd(acc, f12, f2);
	f0 = Ps2F_Sub(f0, f5);
	ZmWF(m, sp + 0x40u, f1);
	ZmWF(m, sp + 0x48u, f0);
	ZmWV(m, sp + 0x90u, ZmRV(m, sp + 0x40u));
	ExtZp_Integ_120750(c, sp + 0x80u, sp + 0x90u, f12);
	ZmWV(m, sp + 0x50u, ZmRV(m, sp + 0x80u));
	ExtZp_Integ_1207c0(c, sp + 0xC0u, sp + 0x50u, f20);
	ExtZp_Integ_1206c0(c, sp + 0xB0u, H + 0x170u);
	va = ZmRV(m, sp + 0xB0u);
	vb = ZmRV(m, sp + 0xC0u);
	Ps2V_Add(&va, &va, &vb, PEN_XYZ);
	ZmWV(m, sp + 0x130u, va);
	ZmWV(m, sp + 0xA0u, ZmRV(m, sp + 0x130u));
	ZmWV(m, H + 0x50u, ZmRV(m, sp + 0xA0u));
	f1 = ZmRF(m, sp + 0x30u);
	f0 = PEN_HALF;
	f2 = ZmRF(m, H + 0x160u);
	f0 = Ps2F_Mul(f0, f1);
	f0 = Ps2F_Mul(f20, f0);
	f0 = Ps2F_Add(f2, f0);
	ZmWF(m, H + 0x40u, f0); // the delay slot of the call below: stored before it
	f0 = ExtZp_Integ_120780(c, H + 0x50u, H + 0x50u);
	f2 = ZmRF(m, H + 0x40u);
	f1 = Ps2F_Mul(f2, f2);
	f12 = Ps2F_Add(f1, f0);
	f12 = Ps2F_Sqrt(f12);
	f0 = Ps2F_Div(f2, f12);
	ZmWF(m, H + 0x40u, f0); // the delay slot of the call below: stored before it
	ExtZp_Integ_360df0(c, H + 0x50u, f12);
	ZmW32(m, sp + 0xE8u, 0u); // dead copies of the orientation on the original's stack
	ZmW32(m, sp + 0xECu, PEN_ONE);
	ZmW32(m, sp + 0xE4u, 0u);
	ZmW32(m, sp + 0xE0u, 0u);
	f0 = ZmRF(m, H + 0x40u);
	ZmWF(m, sp + 0xD0u, f0);
	ZmWV(m, sp + 0xE0u, ZmRV(m, H + 0x50u));
L_398f44:
	ExtZp_Integ_3f6780(c, H + 0xE0u, H + 0x40u);
	ZmWV(m, H + 0xE0u + 0x30u, ZmRV(m, H + 0x30u));
	ExtZp_Integ_3f6b20(c, sp + 0xF0u, H + 0xE0u, H + 0xB0u);
	for (v = 0; v < 0x30u; v += 0x10u)
	{
		f3 = ZmRF(m, sp + 0xF0u + v + 0x0u);
		f2 = ZmRF(m, sp + 0xF0u + v + 0x4u);
		f1 = ZmRF(m, sp + 0xF0u + v + 0x8u);
		f0 = ZmRF(m, sp + 0xF0u + v + 0xCu);
		ZmWF(m, H + 0x120u + v + 0x0u, f3);
		ZmWF(m, H + 0x120u + v + 0x4u, f2);
		ZmWF(m, H + 0x120u + v + 0x8u, f1);
		ZmWF(m, H + 0x120u + v + 0xCu, f0);
	}
	v = ZmR32(m, H + 0x294u);
	ZmW32(m, H + 0x294u, v + 1u); // the delay slot of the call below: counted before it
	ExtZp_Integ_399ca0(c, H);
	ZmWF(m, H + 0x14u, f21);
	ExtZp_Pop(c, 0x140);
}

// ------------------------------------------------------------------ 0x3b0c90
// Removes the records [a1, a2) of the record vector a0 (count at +4, data at +8, records of 0x80 bytes):
// the records behind a2 are copied down field by field (not the table pointer, not +0x74 ..), the
// surplus records at the end are destroyed through their table (slot +8, no free), the count shrinks.
// Returns a1.
uint32_t ExtZp_Pen_3b0c90(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t s6 = a0, s5 = a1, s4 = a2;
	uint32_t s0, s1, s2, s3, v0, v1, t9, f1, f0;

	if (s5 == s4)
		return s5;
	v1 = ZmR32(m, s6 + 4u);
	v0 = ZmR32(m, s6 + 8u);
	v1 = v1 << 7;
	s3 = v0 + v1; // end of the data
	v0 = s3 - s4;
	s0 = Pen_Div128(v0); // number of records behind a2
	if (s0 != 0u && s4 < s3)
	{
		s2 = s5;
		s1 = s4;
		do
		{
			v0 = ZmR32(m, s1 + 4u);
			ZmW32(m, s2 + 4u, v0);
			f1 = ZmRF(m, s1 + 8u);
			f0 = ZmRF(m, s1 + 0xCu);
			ZmWF(m, s2 + 8u, f1);
			ZmWF(m, s2 + 0xCu, f0);
			ExtZp_Solver_3723e0(c, s2 + 0x10u, s1 + 0x10u);
			v0 = ZmR32(m, s1 + 0x70u);
			ZmW32(m, s2 + 0x70u, v0);
			s1 = s1 + 0x80u;
			s2 = s2 + 0x80u;
		} while (s1 < s3);
	}
	s0 = s5 + (s0 << 7);
	if (s0 < s3)
	{
		do
		{
			t9 = ZmR32(m, s0 + 0u);
			t9 = ZmR32(m, t9 + 8u); // table slot +8: the destructor
			if (t9 == PEN_DTOR_RECORD)
				ExtZp_Solver_13e130(c, s0, 0xFFFFFFFFu);
			else
				ExtZp_Leave(c, t9);
			s0 = s0 + 0x80u;
		} while (s0 < s3);
	}
	v0 = s4 - s5;
	v0 = Pen_Div128(v0);
	v1 = ZmR32(m, s6 + 4u);
	v1 = v1 - v0;
	ZmW32(m, s6 + 4u, v1);
	return s5;
}

// ------------------------------------------------------------------ 0x3b0bb0
// Resizes the record vector a0 to a1 records: more records are appended as copies of a default record built
// on the stack (class table 0x6CC3A0, identity-like vectors with w = 1, key parts -1; the other fields keep
// what the stack held, here zero), fewer are removed from the end (0x3b0c90). Nothing is returned.
void ExtZp_Pen_3b0bb0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x90);
	uint32_t E = sp + 0x10u; // the default record
	uint32_t v1 = ZmR32(m, a0 + 4u);
	uint32_t grow = (v1 < a1);
	uint32_t shrink = (a1 < v1);

	if (grow)
	{
		uint32_t a2, dataPtr, cnt;
		ZmW32(m, sp + 0x18u, 0u);
		ZmW32(m, sp + 0x10u, PEN_VT_RECORD);
		ZmW32(m, sp + 0x1Cu, 0u);
		ZmW32(m, sp + 0x28u, 0u);
		ZmW32(m, sp + 0x24u, 0u);
		ZmW32(m, sp + 0x20u, 0u);
		ZmW32(m, sp + 0x2Cu, PEN_ONE);
		ZmW32(m, sp + 0x38u, 0u);
		ZmW32(m, sp + 0x34u, 0u);
		ZmW32(m, sp + 0x30u, 0u);
		ZmW32(m, sp + 0x3Cu, PEN_ONE);
		ZmW32(m, sp + 0x48u, 0u);
		ZmW32(m, sp + 0x44u, 0u);
		ZmW32(m, sp + 0x40u, 0u);
		ZmW32(m, sp + 0x4Cu, PEN_ONE);
		ZmW32(m, sp + 0x5Cu, PEN_ONE);
		ZmW32(m, sp + 0x58u, 0u);
		ZmW32(m, sp + 0x54u, 0u);
		ZmW32(m, sp + 0x50u, 0u);
		ZmW8(m, sp + 0x60u, 0u);
		ZmW32(m, sp + 0x78u, 0xFFFFFFFFu);
		ZmW32(m, sp + 0x7Cu, 0xFFFFFFFFu);
		cnt = ZmR32(m, a0 + 4u);
		dataPtr = ZmR32(m, a0 + 8u);
		a2 = a1 - cnt;
		ExtZp_Solver_37ae50(c, a0, dataPtr + (cnt << 7), a2, E);
		ZmW32(m, sp + 0x10u, PEN_VT_BASE); // the default record's table is set back; no destructor runs
	}
	else if (shrink)
	{
		uint32_t d = ZmR32(m, a0 + 8u);
		(void)ExtZp_Pen_3b0c90(c, a0, d + (a1 << 7), d + (v1 << 7));
	}
	ExtZp_Pop(c, 0x90);
}

// ------------------------------------------------------------------ 0x3b06d0
// The time search of one pair (a1 = 8-byte pair entry: shell A, shell B; body = *(shell + 0x18)) of the
// subspace a0. The two bodies have their own times (+0x14). Unequal times: the body with the later time is
// first set to the earlier time tmin; the detector's pair test (0x380fb0, flag byte + contact vector) is
// asked at the start state and at tmin. If it overlaps at tmin the search goes on as for equal times. Else
// the step w = 0.5 * |tA - tB| is halved from t = tmin + w: t is moved back by w when the pair overlaps at
// t, forward when it does not, until w <= 1e-4; a first contact within the detector's tolerance (subspace
// table slot +0x20) ends the search at once. A pair still overlapping at the end is searched as for equal
// times; one that does not leaves the later body at tmin. Equal times: both bodies are set to t, starting at
// 0.5 * (the smaller time), the same halving; a pair that still overlaps is entered into the detector's
// list P + 0xB0 (0x3770e0) and both bodies are set back to their original times. The pair is passed to the
// list as the 64-bit word the original loads (shell A in the low half = a[1], shell B in the high half =
// a[2] here, a convention of this port; the original's a2 is not meant to carry it).
void ExtZp_Pen_3b06d0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0xD0);
	uint32_t s5 = a0, s6 = a1;
	uint32_t s0, s1, s2, s3, v0, v1, det, P;
	Ps2F f0, f1, f20, f21, f22, f23, f24;

	v1 = ZmR32(m, s6 + 0u);
	v0 = ZmR32(m, s6 + 4u);
	s2 = ZmR32(m, v1 + 0x18u); // body A
	s3 = ZmR32(m, v0 + 0x18u); // body B
	f20 = ZmRF(m, s2 + 0x14u); // time of A
	f21 = ZmRF(m, s3 + 0x14u); // time of B
	s0 = s2;
	if (!Ps2F_Lt(f20, f21))
		f24 = f21;
	else
		f24 = f20; // tmin
	if (Ps2F_Eq(f20, f21))
		goto L_3b0968;
	if (Ps2F_Le(f20, f21))
		s2 = s3; // s2 = the body with the later time
	ExtZp_Pen_36e250(c, sp + 0xA8u, sp + 0xC8u);
	ZmW8(m, sp + 0xCEu, 1u);
	det = ZmR32(m, s5 + 0x70u);
	Pen_Collide(c, 0x00380FB0u, det, s0, s3, sp + 0xCEu, sp + 0xA8u);
	v0 = ZmR8(m, sp + 0xCEu);
	if (v0 != 0u)
		goto L_3b07bc;
	ExtZp_Pen_13dfd0(c, sp + 0xA8u, 0u);
	goto L_epilog_b;
L_3b07bc:
	ExtZp_Solver_13e0b0(c, sp + 0xA8u);
	Pen_SetTime(c, s2, f24);
	det = ZmR32(m, s5 + 0x70u);
	Pen_Collide(c, 0x00380FB0u, det, s0, s3, sp + 0xCEu, sp + 0xA8u);
	v0 = ZmR8(m, sp + 0xCEu);
	if (v0 != 0u)
		goto L_3b0960;
	v0 = ZmR32(m, sp + 0xACu); // contact count
	f0 = Ps2F_Sub(f20, f21);
	if (v0 == 0u)
		goto L_3b0848;
	s1 = ZmR32(m, sp + 0xB0u); // contact data
	f0 = Pen_Tolerance(c, s5);
	f1 = ZmRF(m, s1 + 0x54u);
	if (!Ps2F_Lt(f1, f0))
		goto L_3b0844;
	ExtZp_Pen_13dfd0(c, sp + 0xA8u, 0u);
	goto L_epilog_a;
L_3b0844:
	f0 = Ps2F_Sub(f20, f21);
L_3b0848:
	f1 = PEN_HALF;
	f0 = Ps2F_Abs(f0);
	f22 = Ps2F_Mul(f1, f0);
	f23 = Ps2F_Add(f24, f22);
	goto L_3b0908;
L_3b0864:
	Pen_SetTime(c, s2, f23);
	det = ZmR32(m, s5 + 0x70u);
	Pen_Collide(c, 0x00380FB0u, det, s0, s3, sp + 0xCEu, sp + 0xA8u);
	v0 = ZmR8(m, sp + 0xCEu);
	if (v0 == 0u)
		goto L_3b08b0;
	f0 = PEN_HALF;
	f22 = Ps2F_Mul(f22, f0);
	f23 = Ps2F_Sub(f23, f22);
	goto L_3b0908;
L_3b08b0:
	v0 = ZmR32(m, sp + 0xACu);
	if (v0 == 0u)
		goto L_3b08f8;
	s1 = ZmR32(m, sp + 0xB0u);
	f0 = Pen_Tolerance(c, s5);
	f1 = ZmRF(m, s1 + 0x54u);
	if (!Ps2F_Lt(f1, f0))
		goto L_3b08f8;
	ExtZp_Pen_13dfd0(c, sp + 0xA8u, 0u);
	goto L_epilog_a;
L_3b08f8:
	f0 = PEN_HALF;
	f22 = Ps2F_Mul(f22, f0);
	f23 = Ps2F_Add(f23, f22);
L_3b0908:
	f0 = PEN_EPS;
	if (!Ps2F_Le(f22, f0))
		goto L_3b0864;
	v0 = ZmR8(m, sp + 0xCEu);
	if (v0 != 0u)
		goto L_3b0960;
	Pen_SetTime(c, s2, f24);
	ExtZp_Pen_13dfd0(c, sp + 0xA8u, 0u);
	goto L_epilog_a;
L_3b0960:
	ExtZp_Pen_13dfd0(c, sp + 0xA8u, 0u);
L_3b0968:
	f23 = ZmRF(m, s0 + 0x14u);
	f0 = ZmRF(m, s3 + 0x14u);
	if (!Ps2F_Lt(f23, f0))
		f23 = f0; // the smaller of the two times
	f0 = PEN_HALF;
	f23 = Ps2F_Mul(f23, f0);
	ExtZp_Pen_36e250(c, sp + 0xB8u, sp + 0xCCu);
	f22 = f23;
	ZmW8(m, sp + 0xCFu, 1u);
	goto L_3b0a70;
L_3b09b4:
	Pen_SetTime(c, s0, f23);
	Pen_SetTime(c, s3, f23);
	det = ZmR32(m, s5 + 0x70u);
	Pen_Collide(c, 0x00380FB0u, det, s0, s3, sp + 0xCFu, sp + 0xB8u);
	v0 = ZmR8(m, sp + 0xCFu);
	if (v0 == 0u)
		goto L_3b0a14;
	f0 = PEN_HALF;
	f22 = Ps2F_Mul(f22, f0);
	f23 = Ps2F_Sub(f23, f22);
	goto L_3b0a70;
L_3b0a14:
	v0 = ZmR32(m, sp + 0xBCu);
	if (v0 == 0u)
		goto L_3b0a60;
	s1 = ZmR32(m, sp + 0xC0u);
	f0 = Pen_Tolerance(c, s5);
	f1 = ZmRF(m, s1 + 0x54u);
	if (!Ps2F_Lt(f1, f0))
		goto L_3b0a60;
	ExtZp_Pen_13dfd0(c, sp + 0xB8u, 0u);
	goto L_epilog_a;
L_3b0a60:
	f0 = PEN_HALF;
	f22 = Ps2F_Mul(f22, f0);
	f23 = Ps2F_Add(f23, f22);
L_3b0a70:
	f0 = PEN_EPS;
	if (!Ps2F_Le(f22, f0))
		goto L_3b09b4;
	v0 = ZmR8(m, sp + 0xCFu);
	if (v0 == 0u)
		goto L_3b0aec;
	v0 = ZmR32(m, s5 + 0x70u);
	P = ZmR32(m, v0 + 0x14u);
	Pen_Collide(c, 0x003770E0u, P, ZmR32(m, s6 + 0u), ZmR32(m, s6 + 4u), 0u, 0u); // a1 = the 64-bit pair
	v0 = ZmR32(m, s6 + 0u);
	Pen_SetTime(c, ZmR32(m, v0 + 0x18u), f20);
	v0 = ZmR32(m, s6 + 4u);
	Pen_SetTime(c, ZmR32(m, v0 + 0x18u), f21);
L_3b0aec:
	ExtZp_Pen_13dfd0(c, sp + 0xB8u, 0u);
L_epilog_a:
L_epilog_b:
	ExtZp_Pop(c, 0xD0);
}
