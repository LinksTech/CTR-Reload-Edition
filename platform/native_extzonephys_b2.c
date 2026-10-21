// THE ZONE PORT, PART B2: the integration helpers of the physics step (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone
// too (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_b2.h>

#define B2_ONE 0x3F800000u  // 1.0 (the original's constant)
#define B2_HALF 0x3F000000u // 0.5 (the original's constant)
#define B2_TWO 0x40000000u  // 2.0 (the original's constant)

// Arithmetic shift right by two of a 32-bit value, as the original writes it
// ("sra", with the add of 3 for a negative value done by the callers).
#define B2_SRA2(x) ((uint32_t)(((int32_t)(x)) >> 2))

// The vector unit's xyz lane mask.
#define B2_XYZ 0xEu

// The vector unit's register vf0: (0, 0, 0, 1.0).
static Ps2Vec4 b2_vf0(void)
{
	Ps2Vec4 v = {{0u, 0u, 0u, B2_ONE}};
	return v;
}

// Clears a query record and names the original callee it stands for.
static void b2_qinit(ExtZpQueryArgs *qa, uint32_t func)
{
	uint32_t i;
	for (i = 0; i < 8; i++)
	{
		qa->a[i] = 0u;
		qa->f[i] = 0u;
	}
	qa->func = func;
}

// ------------------------------------------------------------------ small vector helpers

// dst = src (one 16-byte copy); returns dst.
uint32_t ExtZp_Integ_1206c0(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	ZmWV(c->m, dst, ZmRV(c->m, src));
	return dst;
}

// dst.xyz = a.xyz + b.xyz, dst.w = a.w (vadd.xyz).
void ExtZp_Integ_1206f0(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b)
{
	Ps2Vec4 va = ZmRV(c->m, a);
	Ps2Vec4 vb = ZmRV(c->m, b);
	Ps2V_Add(&va, &va, &vb, B2_XYZ);
	ZmWV(c->m, dst, va);
}

// dst.xyz = a.xyz * s, dst.w = a.w (vmulx.xyz with the scalar in the x lane).
void ExtZp_Integ_120750(ExtZpCtx *c, uint32_t dst, uint32_t a, Ps2F s)
{
	Ps2Vec4 va = ZmRV(c->m, a);
	Ps2Vec4 vs = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&vs, 0);
	Ps2V_Mul(&va, &va, &bc, B2_XYZ);
	ZmWV(c->m, dst, va);
}

// Dot product of the xyz parts of two vectors: (a.x*b.x + a.y*b.y) + a.z*b.z with
// the accumulator (mula, madda, madd); returned in f0.
Ps2F ExtZp_Integ_120780(ExtZpCtx *c, uint32_t a, uint32_t b)
{
	ExtZpMem *m = c->m;
	Ps2F f2 = ZmRF(m, a + 0x0);
	Ps2F f0 = ZmRF(m, b + 0x0);
	Ps2F f1 = ZmRF(m, a + 0x4);
	Ps2Acc acc = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, b + 0x4);
	f2 = ZmRF(m, a + 0x8);
	acc = Ps2A_Madda(acc, f1, f0);
	f0 = ZmRF(m, b + 0x8);
	f0 = Ps2A_Madd(acc, f2, f0);
	return f0;
}

// dst = src (one 16-byte copy); returns dst.
uint32_t ExtZp_Integ_1207b0(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	ZmWV(c->m, dst, ZmRV(c->m, src));
	return dst;
}

// dst.xyz = a.xyz * s, dst.w = a.w (same as 120750).
void ExtZp_Integ_1207c0(ExtZpCtx *c, uint32_t dst, uint32_t a, Ps2F s)
{
	Ps2Vec4 va = ZmRV(c->m, a);
	Ps2Vec4 vs = {{s, 0u, 0u, 0u}};
	Ps2Vec4 bc = Ps2V_Bc(&vs, 0);
	Ps2V_Mul(&va, &va, &bc, B2_XYZ);
	ZmWV(c->m, dst, va);
}

// dst.xyz = a.xyz - b.xyz, dst.w = a.w (vsub.xyz); returns dst.
uint32_t ExtZp_Integ_34bb20(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b)
{
	Ps2Vec4 va = ZmRV(c->m, a);
	Ps2Vec4 vb = ZmRV(c->m, b);
	Ps2V_Sub(&va, &va, &vb, B2_XYZ);
	ZmWV(c->m, dst, va);
	return dst;
}

// Transforms a point by a 4x4 matrix stored as four vec4 rows at m4: dst.xyz =
// row0*v.x + row1*v.y + row2*v.z + row3*1.0 (accumulator sequence), dst.w = v.w;
// returns dst.
uint32_t ExtZp_Integ_35bc40(ExtZpCtx *c, uint32_t dst, uint32_t m4, uint32_t v)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 vf0 = b2_vf0();
	Ps2Vec4 vf20 = ZmRV(m, v);
	Ps2Vec4 vf21 = ZmRV(m, m4 + 0x0);
	Ps2Vec4 vf22 = ZmRV(m, m4 + 0x10);
	Ps2Vec4 vf23 = ZmRV(m, m4 + 0x20);
	Ps2Vec4 vf24 = ZmRV(m, m4 + 0x30);
	Ps2VAcc acc;
	Ps2Vec4 bc;
	bc = Ps2V_Bc(&vf20, 0);
	Ps2V_Mula(&acc, &vf21, &bc, B2_XYZ);
	bc = Ps2V_Bc(&vf20, 1);
	Ps2V_Madda(&acc, &vf22, &bc, B2_XYZ);
	bc = Ps2V_Bc(&vf20, 2);
	Ps2V_Madda(&acc, &vf23, &bc, B2_XYZ);
	bc = Ps2V_Bc(&vf0, 3);
	Ps2V_MaddAcc(&vf20, &acc, &vf24, &bc, B2_XYZ);
	ZmWV(m, dst, vf20);
	return dst;
}

// Rotates a vector by a 3x3 matrix stored as three vec4 rows at m3: dst.xyz =
// row0*v.x + row1*v.y + row2*v.z (accumulator sequence), dst.w = row2.w (the
// result lands in the register that held row 2); returns dst.
uint32_t ExtZp_Integ_35c3f0(ExtZpCtx *c, uint32_t dst, uint32_t m3, uint32_t v)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 vf20 = ZmRV(m, v);
	Ps2Vec4 vf21 = ZmRV(m, m3 + 0x0);
	Ps2Vec4 vf22 = ZmRV(m, m3 + 0x10);
	Ps2Vec4 vf23 = ZmRV(m, m3 + 0x20);
	Ps2Vec4 r23 = vf23;
	Ps2VAcc acc;
	Ps2Vec4 bc;
	bc = Ps2V_Bc(&vf20, 0);
	Ps2V_Mula(&acc, &vf21, &bc, B2_XYZ);
	bc = Ps2V_Bc(&vf20, 1);
	Ps2V_Madda(&acc, &vf22, &bc, B2_XYZ);
	bc = Ps2V_Bc(&vf20, 2);
	Ps2V_MaddAcc(&r23, &acc, &vf23, &bc, B2_XYZ);
	ZmWV(m, dst, r23);
	return dst;
}

// Divides the three floats at v3 by d (div.s each, in place); returns v3.
uint32_t ExtZp_Integ_360df0(ExtZpCtx *c, uint32_t v3, Ps2F d)
{
	ExtZpMem *m = c->m;
	Ps2F f0 = ZmRF(m, v3 + 0x0);
	Ps2F f1 = ZmRF(m, v3 + 0x4);
	Ps2F f2 = ZmRF(m, v3 + 0x8);
	f0 = Ps2F_Div(f0, d);
	f1 = Ps2F_Div(f1, d);
	f2 = Ps2F_Div(f2, d);
	ZmWF(m, v3 + 0x0, f0);
	ZmWF(m, v3 + 0x4, f1);
	ZmWF(m, v3 + 0x8, f2);
	return v3;
}

// Builds a rotation matrix (three rows of four floats; the fourth float of each
// row is not written) from a quaternion: x y z are the vec4 at q+0x10, w is the
// float at q+0. Nothing is returned.
void ExtZp_Integ_3f6780(ExtZpCtx *c, uint32_t m3, uint32_t q)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 qv = ZmRV(m, q + 0x10);
	Ps2F f7 = qv.v[0];
	Ps2F f9 = qv.v[1];
	Ps2F f1 = qv.v[2];
	Ps2F f0 = B2_TWO;
	Ps2F f11 = ZmRF(m, q + 0x0);
	Ps2F f2, f3, f4, f5, f6, f8, f10;
	f3 = Ps2F_Mul(f0, f7);
	f6 = Ps2F_Mul(f0, f9);
	f0 = Ps2F_Mul(f0, f1);
	f4 = Ps2F_Mul(f3, f7);
	f5 = Ps2F_Mul(f3, f11);
	f8 = Ps2F_Mul(f6, f7);
	f10 = Ps2F_Mul(f0, f7);
	f7 = Ps2F_Mul(f6, f9);
	f1 = Ps2F_Mul(f0, f1);
	f9 = Ps2F_Mul(f0, f9);
	f3 = Ps2F_Mul(f0, f11);
	f0 = Ps2F_Add(f7, f1);
	f2 = B2_ONE;
	f1 = Ps2F_Add(f4, f1);
	f0 = Ps2F_Sub(f2, f0);
	ZmWF(m, m3 + 0x0, f0);
	f0 = Ps2F_Add(f4, f7);
	f6 = Ps2F_Mul(f6, f11);
	f1 = Ps2F_Sub(f2, f1);
	f0 = Ps2F_Sub(f2, f0);
	f2 = Ps2F_Sub(f8, f3);
	ZmWF(m, m3 + 0x10, f2);
	f4 = Ps2F_Add(f10, f6);
	f3 = Ps2F_Add(f8, f3);
	ZmWF(m, m3 + 0x20, f4);
	ZmWF(m, m3 + 0x4, f3);
	ZmWF(m, m3 + 0x14, f1);
	f3 = Ps2F_Sub(f9, f5);
	f2 = Ps2F_Sub(f10, f6);
	ZmWF(m, m3 + 0x24, f3);
	f1 = Ps2F_Add(f9, f5);
	ZmWF(m, m3 + 0x8, f2);
	ZmWF(m, m3 + 0x18, f1);
	ZmWF(m, m3 + 0x28, f0);
}

// dst = a * b for 3x3 matrices stored as three vec4 rows (rows of b scaled by the
// elements of a's rows, accumulator sequences; the w of each result row is the w
// of the matching row of b); returns dst.
uint32_t ExtZp_Integ_3f6b20(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 vf21 = ZmRV(m, a + 0x0);
	Ps2Vec4 vf22 = ZmRV(m, a + 0x10);
	Ps2Vec4 vf23 = ZmRV(m, a + 0x20);
	Ps2Vec4 vf24 = ZmRV(m, b + 0x0);
	Ps2Vec4 vf25 = ZmRV(m, b + 0x10);
	Ps2Vec4 vf26 = ZmRV(m, b + 0x20);
	// vmove.xyz vf27..vf29, vf0 : xyz = 0 (the w lanes are never read)
	Ps2Vec4 vf27 = {{0u, 0u, 0u, 0u}};
	Ps2Vec4 vf28 = {{0u, 0u, 0u, 0u}};
	Ps2Vec4 vf29 = {{0u, 0u, 0u, 0u}};
	Ps2VAcc acc;
	Ps2Vec4 bc;
	int k;
	// vf27 = b0*a0.x + b1*a1.x + b2*a2.x ; vf28 with .y ; vf29 with .z
	for (k = 0; k < 3; k++)
	{
		Ps2Vec4 *d = (k == 0) ? &vf27 : (k == 1) ? &vf28 : &vf29;
		bc = Ps2V_Bc(&vf21, k);
		Ps2V_Mula(&acc, &vf24, &bc, B2_XYZ);
		bc = Ps2V_Bc(&vf22, k);
		Ps2V_Madda(&acc, &vf25, &bc, B2_XYZ);
		bc = Ps2V_Bc(&vf23, k);
		Ps2V_MaddAcc(d, &acc, &vf26, &bc, B2_XYZ);
	}
	// vf24 = a0*vf27.x + a1*vf27.y + a2*vf27.z ; vf25 with vf28 ; vf26 with vf29
	for (k = 0; k < 3; k++)
	{
		Ps2Vec4 *src = (k == 0) ? &vf27 : (k == 1) ? &vf28 : &vf29;
		Ps2Vec4 *d = (k == 0) ? &vf24 : (k == 1) ? &vf25 : &vf26;
		bc = Ps2V_Bc(src, 0);
		Ps2V_Mula(&acc, &vf21, &bc, B2_XYZ);
		bc = Ps2V_Bc(src, 1);
		Ps2V_Madda(&acc, &vf22, &bc, B2_XYZ);
		bc = Ps2V_Bc(src, 2);
		Ps2V_MaddAcc(d, &acc, &vf23, &bc, B2_XYZ);
	}
	ZmWV(m, dst + 0x0, vf24);
	ZmWV(m, dst + 0x10, vf25);
	ZmWV(m, dst + 0x20, vf26);
	return dst;
}

// ------------------------------------------------------------------ body steps

// Moves the two vectors at obj+0x20 and obj+0x30 (the ends of an edge) with the
// object's transform: unless the state word at obj+0x14 is 0x10, the point at
// obj+0x40 is transformed by the matrix whose address is stored at obj+0xe0, and
// the pair is rebuilt around that point (mid point kept, half extent kept).
// Nothing is returned.
void ExtZp_Integ_395bc0(ExtZpCtx *c, uint32_t obj)
{
	ExtZpMem *m = c->m;
	uint32_t sp, a0, a1, v0, v1;
	a0 = ZmR32(m, obj + 0x14); // state
	if (a0 == 0x10u)
		return;
	sp = ExtZp_Push(c, 0xB0);
	a1 = ZmR32(m, obj + 0xE0); // address of the transform
	ExtZp_Integ_35bc40(c, sp + 0x20, a1, obj + 0x40);
	ZmWV(m, sp + 0x50, ZmRV(m, sp + 0x20));
	v1 = obj + 0x20;
	ZmWV(m, sp + 0x30, ZmRV(m, v1));
	v0 = v1 + 0x10;
	ZmWV(m, sp + 0x40, ZmRV(m, v0));
	ExtZp_Integ_34bb20(c, sp + 0x80, sp + 0x40, sp + 0x30);
	ExtZp_Integ_1207c0(c, sp + 0x90, sp + 0x80, B2_HALF);
	ZmWV(m, sp + 0xA0, ZmRV(m, sp + 0x90));
	ExtZp_Integ_34bb20(c, sp + 0x70, sp + 0x50, sp + 0xA0);
	ZmWV(m, sp + 0x30, ZmRV(m, sp + 0x70));
	ExtZp_Integ_1206f0(c, sp + 0x60, sp + 0x50, sp + 0xA0);
	ZmWV(m, sp + 0x40, ZmRV(m, sp + 0x60));
	ExtZp_Integ_1207b0(c, obj + 0x20, sp + 0x30);
	ExtZp_Integ_1207b0(c, obj + 0x30, sp + 0x40);
	ExtZp_Pop(c, 0xB0);
}

// Direct integration of a body for the time step dt (no solver): moves the
// position (+0x30) by velocity (+0x60) * dt, advances the orientation scalar
// (+0x40) and the axis (+0x50) from the angular rate (+0x70) and renormalises
// them, adds the force (+0x80, scaled by the inverse mass at +0xa4) to the
// velocity and the torque (+0x90 turned by the matrix at +0x120) to the angular
// rate, clears force and torque, rebuilds the rotation matrix (+0xe0) from the
// quaternion (+0x40), multiplies it with the matrix at +0xb0 into the matrix at
// +0x120 and counts the step (+0x294). Nothing is returned.
void ExtZp_Integ_398930(ExtZpCtx *c, uint32_t b, Ps2F dt)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x160);
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14;
	Ps2Acc acc;
	Ps2Vec4 va, vb;
	uint32_t v;

	ExtZp_Integ_1207c0(c, sp + 0x60, b + 0x60, dt);
	va = ZmRV(m, b + 0x30);
	vb = ZmRV(m, sp + 0x60);
	Ps2V_Add(&va, &va, &vb, B2_XYZ);
	ZmWV(m, b + 0x30, va);
	ZmW32(m, sp + 0x48, 0u);
	ZmW32(m, sp + 0x4C, B2_ONE);
	ZmW32(m, sp + 0x44, 0u);
	ZmW32(m, sp + 0x40, 0u);
	ZmW32(m, sp + 0x30, 0u);
	ZmWV(m, sp + 0x40, ZmRV(m, b + 0x70)); // the angular rate (x y z, w)
	f5 = ZmRF(m, b + 0x58);
	f6 = ZmRF(m, b + 0x54);
	f11 = ZmRF(m, sp + 0x44);
	f7 = ZmRF(m, sp + 0x48);
	f14 = ZmRF(m, sp + 0x40);
	f13 = ZmRF(m, b + 0x50);
	f4 = ZmRF(m, b + 0x40);
	f0 = Ps2F_Sub(f6, f5);
	f1 = Ps2F_Sub(f7, f11);
	f10 = Ps2F_Mul(f1, f0);
	f3 = ZmRF(m, sp + 0x30);
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
	f12 = B2_HALF;
	f3 = Ps2F_Mul(f12, f2);
	f0 = Ps2F_Add(f10, f3);
	f0 = Ps2F_Sub(f0, f6);
	ZmWF(m, sp + 0x30, f0);
	f0 = Ps2F_Add(f9, f3);
	f1 = Ps2F_Sub(f0, f1);
	f0 = Ps2F_Add(f8, f3);
	f0 = Ps2F_Sub(f0, f4);
	ZmWF(m, sp + 0x44, f0);
	f0 = Ps2A_Madd(acc, f12, f2);
	f0 = Ps2F_Sub(f0, f5);
	ZmWF(m, sp + 0x40, f1);
	ZmWF(m, sp + 0x48, f0);
	ZmWV(m, sp + 0x80, ZmRV(m, sp + 0x40));
	ExtZp_Integ_120750(c, sp + 0x70, sp + 0x80, f12);
	ZmWV(m, sp + 0x50, ZmRV(m, sp + 0x70));
	ExtZp_Integ_1207c0(c, sp + 0xB0, sp + 0x50, dt);
	ExtZp_Integ_1206c0(c, sp + 0xA0, b + 0x50);
	// vf1 = [sp+0xa0] + [sp+0xb0] (xyz) goes through sp+0x150 and sp+0x90 to b+0x50
	va = ZmRV(m, sp + 0xA0);
	vb = ZmRV(m, sp + 0xB0);
	Ps2V_Add(&va, &va, &vb, B2_XYZ);
	ZmWV(m, sp + 0x150, va);
	ZmWV(m, sp + 0x90, ZmRV(m, sp + 0x150));
	ZmWV(m, b + 0x50, ZmRV(m, sp + 0x90));
	f1 = ZmRF(m, sp + 0x30);
	f0 = B2_HALF;
	f2 = ZmRF(m, b + 0x40);
	f0 = Ps2F_Mul(f0, f1);
	f0 = Ps2F_Mul(dt, f0);
	f0 = Ps2F_Add(f2, f0);
	ZmWF(m, b + 0x40, f0); // delay slot of the call below
	f0 = ExtZp_Integ_120780(c, b + 0x50, b + 0x50);
	f2 = ZmRF(m, b + 0x40);
	f1 = Ps2F_Mul(f2, f2);
	f12 = Ps2F_Add(f1, f0);
	f12 = Ps2F_Sqrt(f12);
	f0 = Ps2F_Div(f2, f12);
	ZmWF(m, b + 0x40, f0); // delay slot of the call below
	ExtZp_Integ_360df0(c, b + 0x50, f12);
	ZmW32(m, sp + 0xD8, 0u);
	ZmW32(m, sp + 0xDC, B2_ONE);
	ZmW32(m, sp + 0xD4, 0u);
	ZmW32(m, sp + 0xD0, 0u);
	f0 = ZmRF(m, b + 0x40);
	ZmWF(m, sp + 0xC0, f0);
	ZmWV(m, sp + 0xD0, ZmRV(m, b + 0x50));
	ExtZp_Integ_1207c0(c, sp + 0xF0, b + 0x80, dt);
	f12 = ZmRF(m, b + 0xA4); // inverse mass
	ExtZp_Integ_1207c0(c, sp + 0xE0, sp + 0xF0, f12);
	va = ZmRV(m, b + 0x60);
	vb = ZmRV(m, sp + 0xE0);
	Ps2V_Add(&va, &va, &vb, B2_XYZ);
	ZmWV(m, b + 0x60, va);
	ExtZp_Integ_35c3f0(c, sp + 0x110, b + 0x120, b + 0x90);
	ExtZp_Integ_1207c0(c, sp + 0x100, sp + 0x110, dt);
	va = ZmRV(m, b + 0x70);
	vb = ZmRV(m, sp + 0x100);
	Ps2V_Add(&va, &va, &vb, B2_XYZ);
	ZmWV(m, b + 0x70, va);
	va = b2_vf0();
	ZmWV(m, b + 0x80, va);
	ZmWV(m, b + 0x90, va);
	ExtZp_Integ_3f6780(c, b + 0xE0, b + 0x40);
	ZmWV(m, b + 0xE0 + 0x30, ZmRV(m, b + 0x30));
	ExtZp_Integ_3f6b20(c, sp + 0x120, b + 0xE0, b + 0xB0);
	for (v = 0; v < 0x30; v += 0x10)
	{
		f3 = ZmRF(m, sp + 0x120 + v + 0x0);
		f2 = ZmRF(m, sp + 0x120 + v + 0x4);
		f1 = ZmRF(m, sp + 0x120 + v + 0x8);
		f0 = ZmRF(m, sp + 0x120 + v + 0xC);
		ZmWF(m, b + 0x120 + v + 0x0, f3);
		ZmWF(m, b + 0x120 + v + 0x4, f2);
		ZmWF(m, b + 0x120 + v + 0x8, f1);
		ZmWF(m, b + 0x120 + v + 0xC, f0);
	}
	v = ZmR32(m, b + 0x294);
	ZmW32(m, b + 0x294, v + 1u);
	ExtZp_Pop(c, 0x160);
}

// ------------------------------------------------------------------ allocation wrappers

// Allocates size bytes plus a 16-byte header through the allocator (0x33c1b0,
// handed to Reload as a query): the block gets the tag word 0x02345656, the size
// and the tag argument; returns the address behind the header.
uint32_t ExtZp_Integ_33c120(ExtZpCtx *c, uint32_t size, uint32_t tag)
{
	ExtZpMem *m = c->m;
	ExtZpQueryArgs qa;
	ExtZpQueryResult r;
	uint32_t v0;
	b2_qinit(&qa, 0x0033C1B0u);
	qa.a[0] = size + 0x10u;
	qa.a[1] = tag;
	r = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa); // the allocator
	v0 = r.v0;
	ZmW32(m, v0 + 0x0, 0x02345656u); // header tag
	ZmW32(m, v0 + 0x4, size);
	ZmW32(m, v0 + 0x8, tag);
	return v0 + 0x10u;
}

// Frees a block of 33c120: unless it is null, the header tag becomes 0xdeadbeef
// and the block (header at block-0x10, the size at -0xc, the tag at -8) goes back
// to the allocator (0x33c3b0, handed to Reload as a query). Returns nothing.
void ExtZp_Integ_33c170(ExtZpCtx *c, uint32_t block)
{
	ExtZpMem *m = c->m;
	ExtZpQueryArgs qa;
	uint32_t v0, a2;
	if (block == 0u)
		return;
	ZmW32(m, block - 0x10u, 0xDEADBEEFu);
	v0 = ZmR32(m, block - 0xCu);
	a2 = ZmR32(m, block - 0x8u);
	b2_qinit(&qa, 0x0033C3B0u);
	qa.a[0] = block - 0x10u;
	qa.a[1] = v0 + 0x10u;
	qa.a[2] = a2;
	(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
}

// ------------------------------------------------------------------ fill

// Fills n bytes at dst with the low byte of value (memset): sixteen bytes at a
// time when dst is 16-byte aligned and n >= 8, then eight, then single bytes.
// Returns dst.
uint32_t ExtZp_Integ_428998(ExtZpCtx *c, uint32_t dst, uint32_t value, uint32_t n)
{
	ExtZpMem *m = c->m;
	uint32_t a0 = dst, a1 = value, a2 = n, a3, v1;
	uint32_t pat32;
	Ps2Vec4 q;
	int v0;
	v1 = a0;
	if (a2 < 8u)
		goto L_a18;
	if ((a0 & 0xFu) != 0u)
		goto L_a18;
	a3 = a0;
	pat32 = (a1 & 0xFFu) * 0x01010101u; // the byte in every lane (the pattern's doublewords)
	q.v[0] = q.v[1] = q.v[2] = q.v[3] = pat32;
	if (a2 < 0x20u)
		goto L_a04;
	do
	{
		ZmWV(m, a3, q);
		a2 -= 0x20u;
		a3 += 0x10u;
		v0 = (a2 < 0x20u);
		ZmWV(m, a3, q);
		a3 += 0x10u;
	} while (!v0);
L_a04:
	v0 = (a2 < 8u);
	while (!v0)
	{
		ZmW64(m, a3, ((uint64_t)pat32 << 32) | pat32);
		a2 -= 8u;
		a3 += 8u;
		v0 = (a2 < 8u);
	}
	v1 = a3;
L_a18:
	a2 -= 1u;
	if (a2 == 0xFFFFFFFFu)
		return a0;
	do
	{
		ZmW8(m, v1, (uint8_t)a1);
		a2 -= 1u;
		v1 += 1u;
	} while (a2 != 0xFFFFFFFFu);
	return a0;
}

// A plain jump to 428998 (the same fill).
uint32_t ExtZp_Integ_33eb00(ExtZpCtx *c, uint32_t dst, uint32_t value, uint32_t n)
{
	return ExtZp_Integ_428998(c, dst, value, n);
}

// ------------------------------------------------------------------ the word vector

// Inserts n copies of the word read from valp at the place pos (an address inside
// the data) into the word vector vec = {capacity +0, size +4, data +8}. When the
// capacity is too small the data is moved to a new block (capacity doubled until
// it holds the new size, through 33c120 / 33c170); a value pointer that lies in
// the moved part is followed. Sizes of 0x40000000 words and more (or a total over
// 0x3fffffff) go through the output routines 0x429f38 / 0x42bd70 (queries) first
// and then the insert goes on as for any size. Nothing is returned.
void ExtZp_Integ_33f800(ExtZpCtx *c, uint32_t vec, uint32_t pos, uint32_t n, uint32_t valp)
{
	ExtZpMem *m = c->m;
	uint32_t a0, a1, a2, a3, v0, v1, at, t0, t1;
	uint32_t s0, s1, s2, s3, s4, s5, s6;
	s5 = vec;
	s4 = n;
	s3 = valp;
	s2 = pos;
	s1 = 0u;
	s6 = 0u;
	a1 = a2 = a3 = t0 = t1 = 0u;
	if (s4 == 0u)
		return;
	at = (s4 < 0x40000000u);
	if (!at)
		goto L_err;
	a0 = ZmR32(m, s5 + 4);
	v1 = 0x3FFFFFFFu - s4;
	at = (v1 < a0);
	if (at)
		goto L_err;
	a0 = ZmR32(m, s5 + 4);
L_f868:
	v1 = ZmR32(m, s5 + 0); // capacity
	s0 = a0 + s4;          // new size
	at = (v1 < s0);
	if (at)
		goto L_grow;
	v1 = ZmR32(m, s5 + 8);
	a0 <<= 2;
	a1 = v1 + a0; // end of the data
	v1 = a1 - s2;
	if ((int32_t)v1 >= 0)
		a0 = B2_SRA2(v1);
	else
	{
		v1 += 3u;
		a0 = B2_SRA2(v1);
	}
	v1 = a0 - s4;
	v1 <<= 2;
	v1 = a1 - v1;
	at = (s3 < v1);
	if (at)
		goto L_fa0c;
	at = (s3 < a1);
	if (!at)
		goto L_fa0c;
	v1 = s4 << 2;
	s3 += v1;
	goto L_fa0c;
L_f8d0:
	at = (s2 < a1);
	a0 = a1 + v1;
	if (!at)
		goto L_f8f8;
	do
	{
		a1 -= 4u;
		a0 -= 4u;
		v1 = ZmR32(m, a1);
		at = (s2 < a1);
		ZmW32(m, a0, v1);
	} while (at);
L_f8f8:
	a0 = s4;
	if (s4 == 0u)
		goto L_fa1c;
	v1 = ZmR32(m, s3); // the value is read once
	do
	{
		ZmW32(m, s2, v1);
		a0 -= 1u;
		s2 += 4u;
	} while (a0 != 0u);
	v1 = ZmR32(m, s5 + 4);
	goto L_fa20;
L_grow:
	s1 = ZmR32(m, s5 + 8); // old data
	v0 = s2 - s1;
	if ((int32_t)v0 >= 0)
		s6 = B2_SRA2(v0);
	else
	{
		v0 += 3u;
		s6 = B2_SRA2(v0);
	}
	s2 = ZmR32(m, s5 + 0);
	if (s2 == 0u)
		s2 = 1u;
	at = (s2 < s0);
	if (!at)
		goto L_f998;
	v0 = 0x3FFFFFFFu;
L_f970:
	at = (s2 < 0x1FFFFFFFu);
	if (!at)
	{
		s2 = v0;
		goto L_f988;
	}
	s2 <<= 1;
L_f988:
	at = (s2 < s0);
	if (at)
		goto L_f970;
L_f998:
	a0 = s2 << 2;
	v0 = ExtZp_Integ_33c120(c, a0, 0xEu);
	ZmW32(m, s5 + 8, v0);
	v1 = s6 << 2;
	a0 = ZmR32(m, s5 + 8);
	a1 = s4;
	if (s4 == 0u)
		goto L_fa38;
	a2 = a0 + v1;
	a0 = ZmR32(m, s3); // the value is read once
	do
	{
		ZmW32(m, a2, a0);
		a1 -= 1u;
		a2 += 4u;
	} while (a1 != 0u);
L_fa38:
	if (s1 == 0u)
		goto L_fbf0;
	v0 = ZmR32(m, s5 + 8);
	a0 = s1 + v1;
	at = (s1 < a0);
	a1 = s1;
	if (!at)
		goto L_fb08;
	a3 = v1 + 3u;
	t1 = a0 - 0x20u;
	if ((int32_t)a3 >= 0)
		a2 = B2_SRA2(a3);
	else
	{
		a2 = a3 + 3u;
		a2 = B2_SRA2(a2);
	}
	if ((int32_t)a2 < 9)
		goto L_fae0;
	at = (a0 < s1);
	if (at)
		goto L_fae0;
	do
	{
		uint32_t k;
		t0 = ZmR32(m, a1);
		a2 = v0 + 8u;
		a2 += 8u;
		a3 = a2 + 8u;
		ZmW32(m, v0, t0);
		for (k = 4; k < 0x20; k += 4)
		{
			a2 = ZmR32(m, a1 + k);
			ZmW32(m, v0 + k, a2);
		}
		a1 += 0x20u;
		a2 = (a1 < t1);
		v0 = a3 + 8u;
	} while (a2);
L_fae0:
	a3 = s1 + v1;
	goto L_faf8;
L_fae8:
	a2 = ZmR32(m, a1);
	ZmW32(m, v0, a2);
	a1 += 4u;
	v0 += 4u;
L_faf8:
	a2 = (a1 < a3);
	if (a2)
		goto L_fae8;
L_fb08:
	a2 = ZmR32(m, s5 + 4);
	v0 = ZmR32(m, s5 + 8);
	a1 = s4 << 2;
	a2 <<= 2;
	v1 = v0 + v1;
	v0 = s1 + a2; // end of the old data
	at = (a0 < v0);
	v1 = v1 + a1;
	if (!at)
		goto L_fbe8;
	a1 = v0 - a0;
	t0 = v0 - 0x20u;
	a2 = a1 + 3u;
	if ((int32_t)a2 >= 0)
		a1 = B2_SRA2(a2);
	else
	{
		a1 = a2 + 3u;
		a1 = B2_SRA2(a1);
	}
	if ((int32_t)a1 < 9)
		goto L_fbd8;
	at = (v0 < a0);
	if (at)
		goto L_fbd8;
	do
	{
		uint32_t k;
		a3 = ZmR32(m, a0);
		a1 = v1 + 8u;
		a1 += 8u;
		a2 = a1 + 8u;
		ZmW32(m, v1, a3);
		for (k = 4; k < 0x20; k += 4)
		{
			a1 = ZmR32(m, a0 + k);
			ZmW32(m, v1 + k, a1);
		}
		a0 += 0x20u;
		a1 = (a0 < t0);
		v1 = a2 + 8u;
	} while (a1);
	goto L_fbd8;
L_fbc4:
	a1 = ZmR32(m, a0);
	ZmW32(m, v1, a1);
	a0 += 4u;
	v1 += 4u;
L_fbd8:
	a1 = (a0 < v0);
	if (a1)
		goto L_fbc4;
L_fbe8:
	ExtZp_Integ_33c170(c, s1);
L_fbf0:
	ZmW32(m, s5 + 4, s0);
	ZmW32(m, s5 + 0, s2);
	return;
L_fa0c:
	if (a0 == 0u)
		goto L_f8f8;
	v1 = s4 << 2;
	goto L_f8d0;
L_fa1c:
	v1 = ZmR32(m, s5 + 4);
L_fa20:
	v1 += s4;
	ZmW32(m, s5 + 4, v1);
	return;
L_err:
	{
		ExtZpQueryArgs qa;
		v0 = ZmR32(m, 0x00657754u); // the output object (a global of the original)
		a0 = ZmR32(m, v0 + 0xC);
		b2_qinit(&qa, 0x00429F38u);
		qa.a[0] = a0;
		qa.a[1] = 0x00643070u;
		(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
		b2_qinit(&qa, 0x0042BD70u);
		(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
		a0 = ZmR32(m, s5 + 4);
		goto L_f868;
	}
}
