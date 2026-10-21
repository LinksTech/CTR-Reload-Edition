// THE ZONE PORT, PART S1: the runtime contact solver "type 1", core (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone
// too (offline tests; link the parts b1, b2 and d).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_s1.h>
#include <platform/native_extzonephys_b1.h>
#include <platform/native_extzonephys_b2.h>
#include <platform/native_extzonephys_d.h>

#define S1_XYZ 0xEu
#define S1_ONE 0x3F800000u         // 1.0 (the original's constant)
#define S1_NEG_ONE 0xBF800000u     // -1.0 (the original's constant)
#define S1_THR 0xBA83126Fu         // -0.001 (the original's constant)
#define S1_0P9 0x3F666666u         // 0.9 (the original's constant)
#define S1_0P1 0x3DCCCCCDu         // 0.1 (the original's constant)
#define S1_0P01 0x3C23D70Au        // 0.01 (the original's constant)
#define S1_G_ZERO_VEC 0x006A2160u  // global vector (16 bytes; (0,0,0,1) at run time, zero in the exe) - always read from memory
#define S1_G_FILL_P 0x00645DC0u    // global: the word the contact array is filled with
#define S1_VT_RESOLVE 0x0003AB9D0u // the solver's vtable entry +0x20 (this part's 3ab9d0)

// Body flag byte (body + 0x18): bit 0 = fixed, bit 2 = fixed too (no response).
static uint32_t s1_bit(uint32_t flags, uint32_t bit) { return (flags >> bit) & 1u; }

// Point velocity of a body: out = H.v + H.w x (p - H.pos); returns out.
uint32_t ExtZp_Solver_396fc0(ExtZpCtx *c, uint32_t out, uint32_t H, uint32_t p)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x40u);
	Ps2Vec4 a = ZmRV(m, p), b = ZmRV(m, H + 0x30u), v1, v2;
	Ps2V_Sub(&a, &a, &b, S1_XYZ);
	ZmWV(m, sp + 0x20u, a);           // sp+0x60
	ZmWV(m, sp + 0x00u, ZmRV(m, sp + 0x20u)); // sp+0x40
	ExtZp_Kart_120720(c, sp + 0x10u, H + 0x70u, sp + 0x00u); // sp+0x30 = w x r
	v1 = ZmRV(m, H + 0x60u);
	v2 = ZmRV(m, sp + 0x10u);
	Ps2V_Add(&v1, &v1, &v2, S1_XYZ);
	ZmWV(m, sp + 0x30u, v1);          // sp+0x50
	ZmWV(m, out, ZmRV(m, sp + 0x30u));
	ExtZp_Pop(c, 0x40u);
	return out;
}

// Round-robin contact search.
uint32_t ExtZp_Solver_3ac1d0(ExtZpCtx *c, uint32_t obj, uint32_t list, uint32_t pc, uint32_t prel, uint32_t pidx, Ps2F thr)
{
	ExtZpMem *m = c->m;
	uint32_t i = 0;
	uint32_t sp = ExtZp_Push(c, 0x20u);
	uint32_t res = 0;
	ZmW32(m, prel, obj + 0x10u);
	while ((int32_t)i < (int32_t)ZmR32(m, obj + 8u))
	{
		uint32_t cur, ct, h1, h2;
		ZmW32(m, pidx, ZmR32(m, obj + 4u));
		ZmW32(m, pc, ZmR32(m, list + 8u) + (ZmR32(m, obj + 4u) << 7));
		ct = ZmR32(m, pc);
		h1 = ZmR32(m, ZmR32(m, ct + 8u) + 0x18u);
		h2 = ZmR32(m, ZmR32(m, ct + 0xCu) + 0x18u);
		ZmWV(m, obj + 0x10u, ZmRV(m, S1_G_ZERO_VEC));
		if (s1_bit(ZmR8(m, h1 + 0x18u), 0) == 0u)
		{
			ExtZp_Solver_396fc0(c, sp + 0x10u, h1, ct + 0x20u);
			ZmWV(m, obj + 0x10u, ZmRV(m, sp + 0x10u));
		}
		if (s1_bit(ZmR8(m, h2 + 0x18u), 0) == 0u)
		{
			Ps2Vec4 a, b;
			ExtZp_Solver_396fc0(c, sp + 0x00u, h2, ct + 0x20u);
			a = ZmRV(m, obj + 0x10u);
			b = ZmRV(m, sp + 0x00u);
			Ps2V_Sub(&a, &a, &b, S1_XYZ);
			ZmWV(m, obj + 0x10u, a);
		}
		ZmWF(m, obj + 0x20u, ExtZp_Integ_120780(c, obj + 0x10u, ct + 0x10u));
		cur = ZmR32(m, obj + 4u) + 1u;
		ZmW32(m, obj + 4u, (uint32_t)((int32_t)cur % (int32_t)ZmR32(m, obj + 8u)));
		if (Ps2F_Lt(ZmRF(m, obj + 0x20u), thr))
		{
			res = 1u;
			break;
		}
		i++;
	}
	ExtZp_Pop(c, 0x20u);
	return res;
}

// Adds the impulse P (stack vector at p) to one body: velocity += P * inverse mass, angular velocity +=
// worldInertia * ((point - pos) x P). Shared shape of the original's inline blocks; every call in the
// original's order. 'scr' = scratch (0x40 bytes) for the results.
static void s1_apply_dyn(ExtZpCtx *c, uint32_t H, uint32_t contact, uint32_t p, uint32_t scr)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 a, b;
	ExtZp_Integ_120750(c, scr + 0x00u, p, ZmRF(m, H + 0xA4u));
	a = ZmRV(m, H + 0x60u);
	b = ZmRV(m, scr + 0x00u);
	Ps2V_Add(&a, &a, &b, S1_XYZ);
	ZmWV(m, H + 0x60u, a);
	(void)ExtZp_Integ_34bb20(c, scr + 0x10u, contact + 0x20u, H + 0x30u);
	ExtZp_Kart_120720(c, scr + 0x20u, scr + 0x10u, p);
	(void)ExtZp_Integ_35c3f0(c, scr + 0x30u, H + 0x120u, scr + 0x20u);
	a = ZmRV(m, H + 0x70u);
	b = ZmRV(m, scr + 0x30u);
	Ps2V_Add(&a, &a, &b, S1_XYZ);
	ZmWV(m, H + 0x70u, a);
}

// Impulse application: P (a0) goes to body 1 as is and to body 2 negated (only to bodies that respond).
void ExtZp_Solver_3ac800(ExtZpCtx *c, uint32_t imp, uint32_t contact)
{
	ExtZpMem *m = c->m;
	uint32_t h1 = ZmR32(m, ZmR32(m, contact + 8u) + 0x18u);
	uint32_t h2 = ZmR32(m, ZmR32(m, contact + 0xCu) + 0x18u);
	uint32_t f1 = ZmR8(m, h1 + 0x18u), f2 = ZmR8(m, h2 + 0x18u);
	uint32_t dyn1 = (s1_bit(f1, 2) | s1_bit(f1, 0)) == 0u;
	uint32_t dyn2 = (s1_bit(f2, 2) | s1_bit(f2, 0)) == 0u;
	uint32_t sp = ExtZp_Push(c, 0x80u);
	if (dyn1)
		s1_apply_dyn(c, h1, contact, imp, sp + 0x00u);
	if (dyn2)
	{
		Ps2Vec4 vf0 = {{0u, 0u, 0u, S1_ONE}};
		Ps2Vec4 a = ZmRV(m, imp);
		Ps2V_Sub(&a, &vf0, &a, S1_XYZ);
		ZmWV(m, sp + 0x40u, a);
		ZmWV(m, sp + 0x50u, ZmRV(m, sp + 0x40u)); // negated impulse (the original's sp+0x70)
		s1_apply_dyn(c, h2, contact, sp + 0x50u, sp + 0x00u);
	}
	ExtZp_Pop(c, 0x80u);
}

// Position-style correction of one contact: penetration share f20 = (0.9 * f12 - depth) / (0.9 * f12), only
// for 0 < f20 < 1 and an approaching relative velocity; the impulse P = n * f20 * f20 * (f12 / 0.1) is scaled
// by each responding body's mass and applied as in 3ac800 (body 2 with the negated impulse).
void ExtZp_Solver_3abdc0(ExtZpCtx *c, uint32_t contact, Ps2F scale)
{
	ExtZpMem *m = c->m;
	Ps2F f2 = Ps2F_Mul(S1_0P9, scale);
	Ps2F f20 = Ps2F_Div(Ps2F_Sub(f2, ZmRF(m, contact + 0x54u)), f2);
	Ps2F f21, f0;
	uint32_t sp, h1, h2, fl;
	if (Ps2F_Le(f20, 0u))
		return;
	if (!Ps2F_Lt(f20, S1_ONE))
		return;
	f21 = Ps2F_Div(scale, S1_0P1);
	sp = ExtZp_Push(c, 0x100u);
	// +0x00 relative velocity, +0x10 scratch, +0x20 impulse P, +0x30 negated P, +0x40 q, +0x80 block scratch
	h1 = ZmR32(m, ZmR32(m, contact + 8u) + 0x18u);
	h2 = ZmR32(m, ZmR32(m, contact + 0xCu) + 0x18u);
	ZmWV(m, sp + 0x00u, ZmRV(m, S1_G_ZERO_VEC));
	if (s1_bit(ZmR8(m, h1 + 0x18u), 0) == 0u)
	{
		(void)ExtZp_Solver_396fc0(c, sp + 0x10u, h1, contact + 0x20u);
		ZmWV(m, sp + 0x00u, ZmRV(m, sp + 0x10u));
	}
	if (s1_bit(ZmR8(m, h2 + 0x18u), 0) == 0u)
	{
		Ps2Vec4 a, b;
		(void)ExtZp_Solver_396fc0(c, sp + 0x10u, h2, contact + 0x20u);
		a = ZmRV(m, sp + 0x00u);
		b = ZmRV(m, sp + 0x10u);
		Ps2V_Sub(&a, &a, &b, S1_XYZ);
		ZmWV(m, sp + 0x00u, a);
	}
	f0 = ExtZp_Integ_120780(c, sp + 0x00u, contact + 0x10u);
	if (Ps2F_Le(f0, 0u))
	{
		ExtZp_Pop(c, 0x100u);
		return;
	}
	f0 = Ps2F_Mul(f21, f20);
	ExtZp_Integ_120750(c, sp + 0x10u, contact + 0x10u, Ps2F_Mul(f20, f0));
	ZmWV(m, sp + 0x20u, ZmRV(m, sp + 0x10u));
	h1 = ZmR32(m, ZmR32(m, contact + 8u) + 0x18u);
	h2 = ZmR32(m, ZmR32(m, contact + 0xCu) + 0x18u);
	fl = ZmR8(m, h1 + 0x18u);
	if ((s1_bit(fl, 2) | s1_bit(fl, 0)) == 0u)
	{
		ExtZp_Integ_120750(c, sp + 0x40u, sp + 0x20u, ZmRF(m, h1 + 0x1B0u));
		s1_apply_dyn(c, h1, contact, sp + 0x40u, sp + 0x80u);
	}
	fl = ZmR8(m, h2 + 0x18u);
	if ((s1_bit(fl, 2) | s1_bit(fl, 0)) == 0u)
	{
		ExtZp_Integ_120750(c, sp + 0x40u, sp + 0x20u, Ps2F_Mul(S1_NEG_ONE, ZmRF(m, h2 + 0x1B0u)));
		s1_apply_dyn(c, h2, contact, sp + 0x40u, sp + 0x80u);
	}
	ExtZp_Pop(c, 0x100u);
}

// Resolver +0x20: the impulse vector of a contact (a2) for the solver (a1); a3 = the relative velocity record
// (+0x10 = relative normal velocity, +0x14 byte = resting flag). j = -(1 + e) * vn / table[contact + 0x70],
// e = 0.01 when the flag is set, else contact[+0x60] * contact[+0x64]; the table is the word array at
// solver + 0x34. out = normal.xyz * j, w of the normal kept; returns out.
uint32_t ExtZp_Solver_3ab9d0(ExtZpCtx *c, uint32_t out, uint32_t solver, uint32_t contact, uint32_t rel)
{
	ExtZpMem *m = c->m;
	Ps2F e, f2, f1, f0;
	uint32_t idx = ZmR32(m, contact + 0x70u);
	Ps2Vec4 vn, vs, r;
	if (ZmR8(m, rel + 0x14u) != 0u)
		e = S1_0P01;
	else
		e = Ps2F_Mul(ZmRF(m, contact + 0x60u), ZmRF(m, contact + 0x64u));
	f2 = Ps2F_Add(S1_ONE, e);
	f2 = Ps2F_Neg(f2);
	f1 = ZmRF(m, rel + 0x10u);
	f1 = Ps2F_Mul(f2, f1);
	f0 = ZmRF(m, ZmR32(m, solver + 0x34u) + (idx << 2));
	f0 = Ps2F_Div(f1, f0);
	vn = ZmRV(m, contact + 0x10u);
	vs.v[0] = f0;
	vs.v[1] = vs.v[2] = vs.v[3] = 0u;
	vs = Ps2V_Bc(&vs, 0);
	r = vn;
	Ps2V_Mul(&r, &vn, &vs, S1_XYZ);
	ZmWV(m, out, r);
	return out;
}

// Iteration loop: for every contact found by 3ac1d0 (relative normal velocity below -0.001), computes its
// impulse (the solver's vtable +0x20 = 3ab9d0), applies it (3ac800) and the correction (3abdc0), until the
// iteration budget (4 * solver[0x28][0]) is used up.
void ExtZp_Solver_3ac0c0(ExtZpCtx *c, uint32_t solver, uint32_t list)
{
	ExtZpMem *m = c->m;
	uint32_t s0, s1, obj, sp;
	if (ZmR32(m, list + 8u) == ZmR32(m, list + 8u) + (ZmR32(m, list + 4u) << 7))
		return;
	obj = ZmR32(m, solver + 0x28u);
	ZmW32(m, obj + 4u, 0u);
	ZmW32(m, obj + 8u, ZmR32(m, list + 4u));
	sp = ExtZp_Push(c, 0x60u); // +0x00 out, +0x10 copy, +0x24 idx, +0x28 prel, +0x2C pc
	ZmW32(m, sp + 0x24u, 0xFFFFFFFFu);
	s0 = ZmR32(m, ZmR32(m, solver + 0x28u));
	s1 = 0u;
	while ((int32_t)s1 < (int32_t)(s0 << 2))
	{
		uint32_t hit, rel, ct, target;
		hit = ExtZp_Solver_3ac1d0(c, ZmR32(m, solver + 0x28u), list, sp + 0x2Cu, sp + 0x28u, sp + 0x24u, S1_THR);
		if (hit == 0u)
			break;
		rel = ZmR32(m, sp + 0x28u);
		ct = ZmR32(m, sp + 0x2Cu);
		ZmW8(m, rel + 0x14u, (uint8_t)((int32_t)s0 < (int32_t)s1));
		target = ZmR32(m, ZmR32(m, solver) + 0x20u);
		s1++;
		if (target == S1_VT_RESOLVE)
			(void)ExtZp_Solver_3ab9d0(c, sp + 0x00u, solver, ct, rel);
		else
		{
			ExtZp_Leave(c, target);
			break;
		}
		ZmWV(m, sp + 0x10u, ZmRV(m, sp + 0x00u));
		ExtZp_Solver_3ac800(c, sp + 0x10u, ZmR32(m, sp + 0x2Cu));
		ExtZp_Solver_3abdc0(c, ZmR32(m, sp + 0x2Cu), ZmRF(m, solver + 0x14u));
	}
	ExtZp_Pop(c, 0x60u);
}

// A matrix-times-cross term of the contact preparation: out vector u = I * (r x n), then (u x r) . n
// (the outer product pair of the vector unit, then the FPU dot with the accumulator chain).
static Ps2F s1_prep_term(ExtZpCtx *c, uint32_t H, uint32_t r, uint32_t n, uint32_t scr)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 vu, vr, res;
	Ps2VAcc acc;
	Ps2Acc a;
	Ps2F f2, f0, f1;
	ExtZp_Kart_120720(c, scr + 0x00u, r, n);                          // r x n
	(void)ExtZp_Integ_35c3f0(c, scr + 0x10u, H + 0x120u, scr + 0x00u); // I * (r x n)
	ZmWV(m, scr + 0x20u, ZmRV(m, scr + 0x10u));
	vu = ZmRV(m, scr + 0x20u);
	vr = ZmRV(m, r);
	res = vu;
	Ps2V_OpMulaAcc(&acc, &vu, &vr);
	Ps2V_OpMsubAcc(&res, &acc, &vr, &vu);
	ZmWV(m, scr + 0x30u, res);
	ZmWV(m, scr + 0x40u, ZmRV(m, scr + 0x30u));
	f2 = ZmRF(m, n + 0u);
	f0 = ZmRF(m, scr + 0x40u);
	f1 = ZmRF(m, n + 4u);
	a = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, scr + 0x44u);
	f2 = ZmRF(m, n + 8u);
	a = Ps2A_Madda(a, f1, f0);
	f0 = ZmRF(m, scr + 0x48u);
	return Ps2A_Madd(a, f2, f0);
}

// Resolver +0x1C: sizes the contact array (solver + 0x2C) like the list, then for every contact stores its
// effective inverse mass (sum of both bodies' inverse masses and both rotational terms) at
// table[solver + 0x34][index], and runs the iteration loop.
void ExtZp_Solver_3ab780(ExtZpCtx *c, uint32_t solver, uint32_t list)
{
	ExtZpMem *m = c->m;
	uint32_t ct, end, idx = 0u, sp;
	ExtZp_Integ_35bc90(c, solver + 0x2Cu, ZmR32(m, list + 4u), S1_G_FILL_P);
	ct = ZmR32(m, list + 8u);
	end = ct + (ZmR32(m, list + 4u) << 7);
	sp = ExtZp_Push(c, 0x100u);
	while (ct != end)
	{
		uint32_t n = ct + 0x10u, p = ct + 0x20u;
		uint32_t h1 = ZmR32(m, ZmR32(m, ct + 8u) + 0x18u);
		uint32_t h2 = ZmR32(m, ZmR32(m, ct + 0xCu) + 0x18u);
		Ps2Vec4 a, b;
		Ps2F f20, f1, f0, f2;
		a = ZmRV(m, p);
		b = ZmRV(m, h1 + 0x30u);
		Ps2V_Sub(&a, &a, &b, S1_XYZ);
		ZmWV(m, sp + 0x00u, a); // r1
		a = ZmRV(m, p);
		b = ZmRV(m, h2 + 0x30u);
		Ps2V_Sub(&a, &a, &b, S1_XYZ);
		ZmWV(m, sp + 0x10u, a); // r2
		f20 = s1_prep_term(c, h1, sp + 0x00u, n, sp + 0x20u);
		f1 = s1_prep_term(c, h2, sp + 0x10u, n, sp + 0x80u);
		f0 = ZmRF(m, h2 + 0xA4u);
		f2 = ZmRF(m, h1 + 0xA4u);
		f0 = Ps2F_Add(f2, f0);
		f0 = Ps2F_Add(f20, f0);
		f0 = Ps2F_Add(f1, f0);
		ZmWF(m, ZmR32(m, solver + 0x34u) + (idx << 2), f0);
		ct += 0x80u;
		idx++;
	}
	ExtZp_Pop(c, 0x100u);
	ExtZp_Solver_3ac0c0(c, solver, list);
}
