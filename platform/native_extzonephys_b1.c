// THE ZONE PORT, PART B1: the core of the rigid body integration (see
// include/platform/native_extzonephys_b1.h and native_extzonephys.h for the
// conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_b1.h>
#include <platform/native_extzonephys_b2.h>

#define INTEG_G_FILL_P1 0x00645C68u    // address of the fill word used for the first buffer pair (the original's constant)
#define INTEG_G_FILL_P2 0x00645CD8u    // address of the fill word used for the second buffer pair (the original's constant)
#define INTEG_G_ERRSTR_PTR 0x00657754u // global: pointer to the output channel record (+0xC = stream)
#define INTEG_G_ERRSTR_MSG 0x00645C90u // address of the message text of the size error
#define INTEG_ONE 0x3F800000u          // 1.0 (the original's constant)
#define INTEG_HALF 0x3F000000u         // 0.5 (the original's constant)
#define INTEG_EPS 0x358637BDu          // 9.99999997e-07 (the original's constant)

// ------------------------------------------------------------------ small helpers
// The address of an entry of a vtable: [[obj] + slot].
static uint32_t IntegVSlot(ExtZpCtx *c, uint32_t obj, uint32_t slot)
{
	return ZmR32(c->m, ZmR32(c->m, obj) + slot);
}

// 1 = the body is not integrated (flag byte bit 0 or bit 2 set).
static int IntegBodyFixed(ExtZpMem *m, uint32_t body)
{
	return (ZmR8(m, body + 0x18u) & 5u) != 0;
}

// 0x345a10 (a helper of the step part): returns a0 + 8.
static uint32_t Integ345a10(uint32_t a0)
{
	return a0 + 8u;
}

// Indirect calls through the system's vtable (targets M1 lists; others are left).
static uint32_t IntegCallStateSize(ExtZpCtx *c, uint32_t sys, uint32_t idx)
{
	uint32_t t = IntegVSlot(c, sys, 0x54u);
	if (t == 0x0039A7F0u)
		return ExtZp_Integ_39a7f0(c, sys, idx);
	ExtZp_Leave(c, t);
	return 0;
}
static void IntegCallGet(ExtZpCtx *c, uint32_t sys, uint32_t rec) // vtable +0x60
{
	uint32_t t = IntegVSlot(c, sys, 0x60u);
	if (t == 0x0039AA60u)
		ExtZp_Integ_39aa60(c, sys, rec);
	else
		ExtZp_Leave(c, t);
}
static void IntegCallSet(ExtZpCtx *c, uint32_t sys, uint32_t rec) // vtable +0x64
{
	uint32_t t = IntegVSlot(c, sys, 0x64u);
	if (t == 0x0039A9A0u)
		ExtZp_Integ_39a9a0(c, sys, rec);
	else
		ExtZp_Leave(c, t);
}
static void IntegCallDeriv(ExtZpCtx *c, uint32_t sys, uint32_t rec, uint32_t countp) // vtable +0x58
{
	uint32_t t = IntegVSlot(c, sys, 0x58u);
	if (t == 0x0039AB30u)
		ExtZp_Integ_39ab30(c, sys, rec, countp);
	else
		ExtZp_Leave(c, t);
}
static void IntegCallForces(ExtZpCtx *c, uint32_t sys, uint64_t tp) // vtable +0x4C
{
	uint32_t t = IntegVSlot(c, sys, 0x4Cu);
	if (t == 0x0038E7C0u)
		ExtZp_Integ_38e7c0(c, sys, tp);
	else
		ExtZp_Leave(c, t);
}
static void IntegCallGravity(ExtZpCtx *c, uint32_t sys, uint32_t g) // vtable +0x50
{
	uint32_t t = IntegVSlot(c, sys, 0x50u);
	if (t == 0x0039A750u)
		ExtZp_Integ_39a750(c, sys, g);
	else
		ExtZp_Leave(c, t);
}
// The ODE solver's step (solver vtable +0xC): Euler only.
static void IntegCallOde(ExtZpCtx *c, uint32_t sys, Ps2F h, uint64_t tp, uint32_t a2, uint32_t a3, uint32_t t1, uint32_t t2)
{
	uint32_t solver = ZmR32(c->m, sys + 0x80u);
	uint32_t t = IntegVSlot(c, solver, 0xCu);
	if (t == 0x003F58E0u)
		ExtZp_Integ_3f58e0(c, tp, a2, a3, t1, t2, h);
	else
		ExtZp_Leave(c, t);
}

// ------------------------------------------------------------------ trivial helpers
// Returns its argument.
uint32_t ExtZp_Integ_36c8a0(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0;
}
// Returns a0 + 8 (the address of a field).
uint32_t ExtZp_Integ_39b540(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0 + 8u;
}
// Returns a0 + 0x20 (the gravity vector of a subspace).
uint32_t ExtZp_Integ_3b15c0(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0 + 0x20u;
}
// Returns the size of an array: [a0 + 4].
uint32_t ExtZp_Integ_3660d0(ExtZpCtx *c, uint32_t a0)
{
	return ZmR32(c->m, a0 + 4u);
}
// The state size per body (floats): always 13 (position 3, quaternion 4, momentum 3, angular velocity 3).
uint32_t ExtZp_Integ_39a7f0(ExtZpCtx *c, uint32_t sys, uint32_t index)
{
	(void)c;
	(void)sys;
	(void)index;
	return 13u;
}

// ------------------------------------------------------------------ word arrays {cap +0, size +4, elems +8}
static void IntegClearArgs(ExtZpQueryArgs *a, uint32_t func)
{
	uint32_t i;
	for (i = 0; i < 8; i++)
	{
		a->a[i] = 0;
		a->f[i] = 0;
	}
	a->func = func;
}

// Assigns the words [first, last) to an array: grows it (new block, old one freed) when the capacity is too small.
void ExtZp_Integ_3908f0(ExtZpCtx *c, uint32_t arr, uint32_t first, uint32_t last)
{
	ExtZpMem *m = c->m;
	int32_t d = (int32_t)(last - first);
	uint32_t n = (uint32_t)(d >= 0 ? (d >> 2) : ((d + 3) >> 2));
	if (ZmR32(m, arr) < n)
	{
		if (!(n < 0x40000000u))
		{
			ExtZpQueryArgs a;
			IntegClearArgs(&a, 0x00429F38u);
			a.a[0] = ZmR32(m, ZmR32(m, INTEG_G_ERRSTR_PTR) + 0xCu);
			a.a[1] = INTEG_G_ERRSTR_MSG;
			ExtZp_Query(c, EXT_ZP_Q_OTHER, &a);
			IntegClearArgs(&a, 0x0042BD70u);
			ExtZp_Query(c, EXT_ZP_Q_OTHER, &a);
		}
		ZmW32(m, arr + 4u, 0);
		if (ZmR32(m, arr + 8u) != 0)
		{
			ExtZp_Integ_33c170(c, ZmR32(m, arr + 8u));
			ZmW32(m, arr + 8u, 0);
			ZmW32(m, arr, 0);
		}
		ZmW32(m, arr + 8u, ExtZp_Integ_33c120(c, n << 2, 0xEu));
		ZmW32(m, arr, n);
	}
	if (first < last)
	{
		uint32_t dst = ZmR32(m, arr + 8u);
		do
		{
			ZmW32(m, dst, ZmR32(m, first));
			first += 4u;
			dst += 4u;
		} while (first < last);
	}
	ZmW32(m, arr + 4u, n);
}

// Array assignment dst = src (nothing when both are the same array); returns dst.
// (The original passes a stray stack byte as a fourth argument that the callee never reads.)
uint32_t ExtZp_Integ_3908a0(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	if (dst != src)
	{
		uint32_t size = ZmR32(c->m, src + 4u);
		uint32_t elems = ZmR32(c->m, src + 8u);
		ExtZp_Integ_3908f0(c, dst, elems, elems + (size << 2));
	}
	return dst;
}

// Erases [first, last) of a word array, moving the tail down; returns first.
uint32_t ExtZp_Integ_35bd00(ExtZpCtx *c, uint32_t arr, uint32_t first, uint32_t last)
{
	ExtZpMem *m = c->m;
	uint32_t end, p, q;
	int32_t d;
	uint32_t n;
	if (first == last)
		return first;
	end = ZmR32(m, arr + 8u) + (ZmR32(m, arr + 4u) << 2);
	d = (int32_t)(end - last);
	n = (uint32_t)(d >= 0 ? (d >> 2) : ((d + 3) >> 2));
	if (n != 0 && last < end)
	{
		p = last;
		q = first;
		do
		{
			ZmW32(m, q, ZmR32(m, p));
			p += 4u;
			q += 4u;
		} while (p < end);
	}
	d = (int32_t)(last - first);
	n = (uint32_t)(d >= 0 ? (d >> 2) : ((d + 3) >> 2));
	ZmW32(m, arr + 4u, ZmR32(m, arr + 4u) - n);
	return first;
}

// Resizes a word array to newSize: new words are copies of the word at fillp, a smaller size erases the tail.
void ExtZp_Integ_35bc90(ExtZpCtx *c, uint32_t arr, uint32_t newSize, uint32_t fillp)
{
	ExtZpMem *m = c->m;
	uint32_t size = ZmR32(m, arr + 4u);
	if (size < newSize)
	{
		ExtZp_Integ_33f800(c, arr, ZmR32(m, arr + 8u) + (size << 2), newSize - size, fillp);
	}
	else if (newSize < size)
	{
		uint32_t elems = ZmR32(m, arr + 8u);
		ExtZp_Integ_35bd00(c, arr, elems + (newSize << 2), elems + (size << 2));
	}
}

// ------------------------------------------------------------------ body state
// Body -> state vector (13 floats at out): position, quaternion xyz and w, momentum (mass * velocity), angular velocity.
void ExtZp_Integ_397bf0(ExtZpCtx *c, uint32_t H, uint32_t out)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 q, w;
	ZmWF(m, out + 0x00u, ZmRF(m, H + 0x30u));
	ZmWF(m, out + 0x04u, ZmRF(m, H + 0x34u));
	ZmWF(m, out + 0x08u, ZmRF(m, H + 0x38u));
	q = ZmRV(m, H + 0x50u);
	ZmWF(m, out + 0x0Cu, q.v[0]);
	ZmWF(m, out + 0x10u, q.v[1]);
	ZmWF(m, out + 0x14u, q.v[2]);
	ZmWF(m, out + 0x18u, ZmRF(m, H + 0x40u));
	ZmWF(m, out + 0x1Cu, Ps2F_Mul(ZmRF(m, H + 0x1B0u), ZmRF(m, H + 0x60u)));
	ZmWF(m, out + 0x20u, Ps2F_Mul(ZmRF(m, H + 0x1B0u), ZmRF(m, H + 0x64u)));
	ZmWF(m, out + 0x24u, Ps2F_Mul(ZmRF(m, H + 0x1B0u), ZmRF(m, H + 0x68u)));
	w = ZmRV(m, H + 0x70u);
	ZmWF(m, out + 0x28u, w.v[0]);
	ZmWF(m, out + 0x2Cu, w.v[1]);
	ZmWF(m, out + 0x30u, w.v[2]);
}

// State vector (13 floats at in) -> body: position, quaternion (normalised), velocity = momentum * inverse mass,
// angular velocity, rotation rows and world inertia rows recomputed, force and moment cleared to (0, 0, 0, 1).
void ExtZp_Integ_397ca0(ExtZpCtx *c, uint32_t H, uint32_t in)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0xC0u); // stack locals whose address is passed on
	Ps2Vec4 v, vf0 = {{0u, 0u, 0u, INTEG_ONE}};
	Ps2F n2, norm, f2;
	ZmWF(m, H + 0x30u, ZmRF(m, in + 0x00u));
	ZmWF(m, H + 0x34u, ZmRF(m, in + 0x04u));
	ZmWF(m, H + 0x38u, ZmRF(m, in + 0x08u));
	v.v[0] = ZmRF(m, in + 0x0Cu);
	v.v[1] = ZmRF(m, in + 0x10u);
	v.v[2] = ZmRF(m, in + 0x14u);
	v.v[3] = INTEG_ONE;
	ZmWV(m, H + 0x50u, v);
	ZmWF(m, H + 0x40u, ZmRF(m, in + 0x18u));
	n2 = ExtZp_Integ_120780(c, H + 0x50u, H + 0x50u); // |q.xyz|^2
	f2 = ZmRF(m, H + 0x40u);
	norm = Ps2F_Sqrt(Ps2F_Add(Ps2F_Mul(f2, f2), n2));
	ZmWF(m, H + 0x40u, Ps2F_Div(f2, norm));
	ExtZp_Integ_360df0(c, H + 0x50u, norm); // q.xyz / norm
	// velocity = momentum * inverse mass
	v.v[0] = ZmRF(m, in + 0x1Cu);
	v.v[1] = ZmRF(m, in + 0x20u);
	v.v[2] = ZmRF(m, in + 0x24u);
	v.v[3] = INTEG_ONE;
	ZmWV(m, sp + 0x70u, v);
	ExtZp_Integ_120750(c, sp + 0x60u, sp + 0x70u, ZmRF(m, H + 0xA4u));
	ZmWV(m, H + 0x60u, ZmRV(m, sp + 0x60u));
	v.v[0] = ZmRF(m, in + 0x28u);
	v.v[1] = ZmRF(m, in + 0x2Cu);
	v.v[2] = ZmRF(m, in + 0x30u);
	v.v[3] = INTEG_ONE;
	ZmWV(m, H + 0x70u, v);
	ExtZp_Integ_3f6780(c, H + 0xE0u, H + 0x40u);
	ZmWV(m, H + 0x110u, ZmRV(m, H + 0x30u));
	ExtZp_Integ_3f6b20(c, sp + 0x90u, H + 0xE0u, H + 0xB0u);
	ZmWF(m, H + 0x120u, ZmRF(m, sp + 0x90u));
	ZmWF(m, H + 0x124u, ZmRF(m, sp + 0x94u));
	ZmWF(m, H + 0x128u, ZmRF(m, sp + 0x98u));
	ZmWF(m, H + 0x12Cu, ZmRF(m, sp + 0x9Cu));
	ZmWF(m, H + 0x130u, ZmRF(m, sp + 0xA0u));
	ZmWF(m, H + 0x134u, ZmRF(m, sp + 0xA4u));
	ZmWF(m, H + 0x138u, ZmRF(m, sp + 0xA8u));
	ZmWF(m, H + 0x13Cu, ZmRF(m, sp + 0xACu));
	ZmWF(m, H + 0x140u, ZmRF(m, sp + 0xB0u));
	ZmWF(m, H + 0x144u, ZmRF(m, sp + 0xB4u));
	ZmWF(m, H + 0x148u, ZmRF(m, sp + 0xB8u));
	ZmWF(m, H + 0x14Cu, ZmRF(m, sp + 0xBCu));
	ZmW32(m, H + 0x294u, ZmR32(m, H + 0x294u) + 1u); // recompute counter
	ZmWV(m, H + 0x80u, vf0);
	ZmWV(m, H + 0x90u, vf0);
	ExtZp_Pop(c, 0xC0u);
}

// Body -> derivative of the state (13 floats at out): velocity, 1/2 (omega * q) (quaternion product with the angular
// velocity as a pure quaternion), force, moment * world inertia.
void ExtZp_Integ_397ea0(ExtZpCtx *c, uint32_t H, uint32_t out)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0xC0u);
	Ps2Vec4 om = ZmRV(m, H + 0x70u);
	Ps2F f0, f1, f2, f3 = 0u, f4, f5, f6, f7, f8, f9, f10, f11, f12 = INTEG_HALF, f13, f14;
	Ps2Acc acc;
	Ps2Vec4 v;
	f5 = ZmRF(m, H + 0x58u); // q z
	f6 = ZmRF(m, H + 0x54u); // q y
	f11 = om.v[1];
	f7 = om.v[2];
	f14 = om.v[0];
	f13 = ZmRF(m, H + 0x50u); // q x
	f0 = Ps2F_Sub(f6, f5);
	f1 = Ps2F_Sub(f7, f11);
	f4 = ZmRF(m, H + 0x40u); // q w
	f10 = Ps2F_Mul(f1, f0);
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
	f3 = Ps2F_Mul(f12, f2);
	f0 = Ps2F_Add(f10, f3);
	f0 = Ps2F_Sub(f0, f6);
	ZmWF(m, sp + 0x30u, f0); // scalar part of the product
	f0 = Ps2F_Add(f9, f3);
	f1 = Ps2F_Sub(f0, f1);
	f0 = Ps2F_Add(f8, f3);
	f0 = Ps2F_Sub(f0, f4);
	ZmWF(m, sp + 0x44u, f0);
	f0 = Ps2A_Madd(acc, f12, f2);
	f0 = Ps2F_Sub(f0, f5);
	ZmWF(m, sp + 0x40u, f1);
	ZmWF(m, sp + 0x48u, f0);
	ZmWF(m, sp + 0x4Cu, INTEG_ONE);
	// velocity
	v = ZmRV(m, H + 0x60u);
	ZmWF(m, out + 0x00u, v.v[0]);
	ZmWF(m, out + 0x04u, v.v[1]);
	ZmWF(m, out + 0x08u, v.v[2]);
	// 0.5 * (x y z) of the product
	ZmWV(m, sp + 0x90u, ZmRV(m, sp + 0x40u));
	ExtZp_Integ_120750(c, sp + 0x80u, sp + 0x90u, f12);
	ZmWV(m, sp + 0x60u, ZmRV(m, sp + 0x80u));
	ZmWF(m, out + 0x0Cu, ZmRF(m, sp + 0x60u));
	ZmWF(m, out + 0x10u, ZmRF(m, sp + 0x64u));
	ZmWF(m, out + 0x14u, ZmRF(m, sp + 0x68u));
	ZmWF(m, out + 0x18u, Ps2F_Mul(INTEG_HALF, ZmRF(m, sp + 0x30u)));
	// force
	ZmWF(m, out + 0x1Cu, ZmRF(m, H + 0x80u));
	ZmWF(m, out + 0x20u, ZmRF(m, H + 0x84u));
	ZmWF(m, out + 0x24u, ZmRF(m, H + 0x88u));
	// moment * world inertia
	ZmWV(m, sp + 0x70u, ZmRV(m, H + 0x90u));
	ExtZp_Integ_35c3f0(c, sp + 0xA0u, H + 0x120u, sp + 0x70u);
	ZmWV(m, sp + 0x70u, ZmRV(m, sp + 0xA0u));
	ZmWF(m, out + 0x28u, ZmRF(m, sp + 0x70u));
	ZmWF(m, out + 0x2Cu, ZmRF(m, sp + 0x74u));
	ZmWF(m, out + 0x30u, ZmRF(m, sp + 0x78u));
	ExtZp_Pop(c, 0xC0u);
}

// Saves the body's position, quaternion w, quaternion xyz, velocity and angular velocity for a step back.
void ExtZp_Integ_399000(ExtZpCtx *c, uint32_t H)
{
	ExtZpMem *m = c->m;
	ZmWV(m, H + 0x150u, ZmRV(m, H + 0x30u));
	ZmWF(m, H + 0x160u, ZmRF(m, H + 0x40u));
	ZmWV(m, H + 0x170u, ZmRV(m, H + 0x50u));
	ZmWV(m, H + 0x180u, ZmRV(m, H + 0x60u));
	ZmWV(m, H + 0x190u, ZmRV(m, H + 0x70u));
}

// Updates the derived data of a body: the list of its attached objects (count +0x28C, pointers +0x290) moves with it.
void ExtZp_Integ_399ca0(ExtZpCtx *c, uint32_t H)
{
	ExtZpMem *m = c->m;
	uint32_t p = ZmR32(m, H + 0x290u);
	uint32_t end = p + (ZmR32(m, H + 0x28Cu) << 2);
	while (p != end)
	{
		ExtZp_Integ_395bc0(c, ZmR32(m, p));
		p += 4u;
	}
}

// ------------------------------------------------------------------ the Euler step
// y_out[i] = y[i] + h * dy[i] for n floats. a2, a3 and t1 hold records whose first word is the data pointer (read again
// for every float, as the original does). a1 (time pair) is not used.
void ExtZp_Integ_3f58e0(ExtZpCtx *c, uint64_t a1, uint32_t a2, uint32_t a3, uint32_t t1, uint32_t t2, Ps2F h)
{
	ExtZpMem *m = c->m;
	int32_t n = (int32_t)t2, i;
	(void)a1;
	for (i = 0; i < n; i++)
	{
		uint32_t off = (uint32_t)i << 2;
		Ps2F dy = ZmRF(m, ZmR32(m, a3) + off);
		Ps2F y = ZmRF(m, ZmR32(m, a2) + off);
		uint32_t o = ZmR32(m, t1) + off;
		ZmWF(m, o, Ps2F_Add(y, Ps2F_Mul(h, dy)));
	}
}

// ------------------------------------------------------------------ system loops
// Active bodies: force += mass * g for every active body (flag bit 1).
void ExtZp_Integ_39a750(ExtZpCtx *c, uint32_t sys, uint32_t g)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x60u);
	uint32_t p = ZmR32(m, sys + 0x34u);
	uint32_t end = p + (ZmR32(m, sys + 0x30u) << 2);
	while (p != end)
	{
		uint32_t b = ZmR32(m, p);
		if ((ZmR8(m, b + 0x18u) & 2u) != 0)
		{
			Ps2Vec4 f, mg;
			ExtZp_Integ_1207c0(c, sp + 0x50u, g, ZmRF(m, b + 0x1B0u));
			f = ZmRV(m, b + 0x80u);
			mg = ZmRV(m, sp + 0x50u);
			Ps2V_Add(&f, &f, &mg, 0xEu);
			ZmWV(m, b + 0x80u, f);
		}
		p += 4u;
	}
	ExtZp_Pop(c, 0x60u);
}

// Sizes the first buffer pair (state, derivative; two records each) to the sum of the bodies' state sizes.
void ExtZp_Integ_38e8b0(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t s0;
	for (s0 = 0; s0 < 2; s0++)
	{
		int32_t n = (int32_t)ZmR32(m, sys + 0x24u);
		uint32_t sum = 0, i, rec;
		for (i = 0; (int32_t)i < n; i++)
			sum += IntegCallStateSize(c, sys, i);
		rec = ZmR32(m, sys + 0x4Cu) + (s0 << 4);
		ExtZp_Integ_35bc90(c, rec + 4u, sum, INTEG_G_FILL_P1);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
		rec = ZmR32(m, sys + 0x58u) + (s0 << 4);
		ExtZp_Integ_35bc90(c, rec + 4u, sum, INTEG_G_FILL_P1);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
	}
}

// Empties the first buffer pair.
void ExtZp_Integ_38e9a0(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t s0;
	for (s0 = 0; s0 < 2; s0++)
	{
		uint32_t rec = ZmR32(m, sys + 0x4Cu) + (s0 << 4);
		ExtZp_Integ_35bc90(c, rec + 4u, 0, INTEG_G_FILL_P1);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
		rec = ZmR32(m, sys + 0x58u) + (s0 << 4);
		ExtZp_Integ_35bc90(c, rec + 4u, 0, INTEG_G_FILL_P1);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
	}
}

// Prepares the buffers: first pair sized by 38e8b0, then (with a fixed step h > 0) the second pair sized like the first.
void ExtZp_Integ_39a800(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t s0;
	ExtZp_Integ_38e8b0(c, sys);
	if (Ps2F_Le(ZmRF(m, sys + 0x88u), 0u))
		return;
	for (s0 = 0; s0 < 2; s0++)
	{
		uint32_t o = s0 << 4, rec, size;
		size = ExtZp_Integ_3660d0(c, ZmR32(m, sys + 0x4Cu) + o + 4u);
		rec = ZmR32(m, sys + 0x94u) + o;
		ExtZp_Integ_35bc90(c, rec + 4u, size, INTEG_G_FILL_P2);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
		size = ExtZp_Integ_3660d0(c, ZmR32(m, sys + 0x58u) + o + 4u);
		rec = ZmR32(m, sys + 0xA0u) + o;
		ExtZp_Integ_35bc90(c, rec + 4u, size, INTEG_G_FILL_P2);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
	}
}

// Empties the first pair, then the second pair.
void ExtZp_Integ_39a8f0(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t s0;
	ExtZp_Integ_38e9a0(c, sys);
	for (s0 = 0; s0 < 2; s0++)
	{
		uint32_t o = s0 << 4, rec;
		rec = ZmR32(m, sys + 0x94u) + o;
		ExtZp_Integ_35bc90(c, rec + 4u, 0, INTEG_G_FILL_P2);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
		rec = ZmR32(m, sys + 0xA0u) + o;
		ExtZp_Integ_35bc90(c, rec + 4u, 0, INTEG_G_FILL_P2);
		ZmW32(m, rec, ZmR32(m, Integ345a10(rec + 4u)));
	}
}

// Sets the state of every active body from a buffer record (13 floats per body, in list order); fixed bodies are skipped.
void ExtZp_Integ_39a9a0(ExtZpCtx *c, uint32_t sys, uint32_t rec)
{
	ExtZpMem *m = c->m;
	uint32_t stride = IntegCallStateSize(c, sys, 0);
	uint32_t p = ZmR32(m, sys + 0x34u);
	uint32_t end = p + (ZmR32(m, sys + 0x30u) << 2);
	uint32_t i = 0;
	while (p != end)
	{
		uint32_t b = ZmR32(m, p);
		uint32_t a1 = ZmR32(m, rec) + ((i * stride) << 2);
		if (!IntegBodyFixed(m, b))
			ExtZp_Integ_397ca0(c, b, a1);
		p += 4u;
		i++;
	}
}

// Gets the state of every active body into a buffer record; a fixed body's slot is zero-filled.
void ExtZp_Integ_39aa60(ExtZpCtx *c, uint32_t sys, uint32_t rec)
{
	ExtZpMem *m = c->m;
	uint32_t stride = IntegCallStateSize(c, sys, 0);
	uint32_t p = ZmR32(m, sys + 0x34u);
	uint32_t end = p + (ZmR32(m, sys + 0x30u) << 2);
	uint32_t i = 0;
	while (p != end)
	{
		uint32_t b = ZmR32(m, p);
		uint32_t a1 = ZmR32(m, rec) + ((i * stride) << 2);
		if (!IntegBodyFixed(m, b))
			ExtZp_Integ_397bf0(c, b, a1);
		else
			ExtZp_Integ_33eb00(c, a1, 0, stride << 2);
		p += 4u;
		i++;
	}
}

// Derivative of every active body into a buffer record (a fixed body's slot is zero-filled); *countp += stride per body.
void ExtZp_Integ_39ab30(ExtZpCtx *c, uint32_t sys, uint32_t rec, uint32_t countp)
{
	ExtZpMem *m = c->m;
	uint32_t stride = IntegCallStateSize(c, sys, 0);
	uint32_t p = ZmR32(m, sys + 0x34u);
	uint32_t end = p + (ZmR32(m, sys + 0x30u) << 2);
	uint32_t i = 0;
	while (p != end)
	{
		uint32_t b = ZmR32(m, p);
		uint32_t a1 = ZmR32(m, rec) + ((i * stride) << 2);
		if (!IntegBodyFixed(m, b))
			ExtZp_Integ_397ea0(c, b, a1);
		else
			ExtZp_Integ_33eb00(c, a1, 0, stride << 2);
		ZmW32(m, countp, ZmR32(m, countp) + stride);
		p += 4u;
		i++;
	}
}

// Derived data of every active body.
void ExtZp_Integ_39ad50(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t p = ZmR32(m, sys + 0x34u);
	uint32_t end = p + (ZmR32(m, sys + 0x30u) << 2);
	while (p != end)
	{
		ExtZp_Integ_399ca0(c, ZmR32(m, p));
		p += 4u;
	}
}

// Step back: flips the buffer index, sets the bodies from the first state buffer of the new index (through the
// embedded interface, vtable +0x10 = a thunk to 39a9a0) and takes the current time from the time field.
void ExtZp_Integ_38e3e0(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t idx, tf, t, rec;
	ZmW32(m, sys + 0x68u, (ZmR32(m, sys + 0x68u) != 0) ? 0u : 1u);
	t = IntegVSlot(c, sys + 0x14u, 0x10u);
	rec = ZmR32(m, sys + 0x4Cu) + (ZmR32(m, sys + 0x68u) << 4);
	if (t == 0x0039B570u) // thunk: this - 0x14, then 39a9a0
		ExtZp_Integ_39a9a0(c, sys, rec);
	else
		ExtZp_Leave(c, t);
	idx = ZmR32(m, sys + 0x68u);
	tf = ZmR32(m, sys + 0x40u) + (idx << 3);
	ZmW32(m, sys + 0x5Cu, ZmR32(m, tf));
	ZmW32(m, sys + 0x60u, ZmR32(m, tf + 4u));
}

// Gravity (unless the no-gravity byte is set: through vtable +0x50) and then every controller (vtable +0x1C of the
// controller, with the subspace and the time pair); a controller's own time (+0x14, +0x18) is set afterwards.
void ExtZp_Integ_38e7c0(ExtZpCtx *c, uint32_t sys, uint64_t t)
{
	ExtZpMem *m = c->m;
	uint32_t sub = ZmR32(m, sys + 0x70u);
	uint32_t s0, s3 = 0;
	if (ZmR8(m, sys + 0x6Cu) == 0)
		IntegCallGravity(c, sys, ExtZp_Integ_3b15c0(c, sub));
	for (s0 = 0; s0 < ZmR32(m, sys + 0x78u); s0++)
	{
		uint32_t ctrl = ZmR32(m, ZmR32(m, sys + 0x7Cu) + s3);
		uint32_t tg = IntegVSlot(c, ctrl, 0x1Cu);
		if (tg == 0x0033AFA0u)
			ExtZp_Glue_33afa0(c, ctrl, sub, t);
		else
			ExtZp_Leave(c, tg);
		s3 += 8u;
		ZmW32(m, ctrl + 0x14u, (uint32_t)t);
		ZmW32(m, ctrl + 0x18u, (uint32_t)(t >> 32));
	}
}

// Rebuilds the list of active bodies (flag bit 1) in the vector at sys + 0x2C.
void ExtZp_Integ_38ea50(ExtZpCtx *c, uint32_t sys)
{
	ExtZpMem *m = c->m;
	uint32_t p, end;
	ZmW32(m, sys + 0x30u, 0);
	p = ZmR32(m, sys + 0x28u);
	end = p + (ZmR32(m, sys + 0x24u) << 2);
	while (p != end)
	{
		if ((ZmR8(m, ZmR32(m, p) + 0x18u) & 2u) != 0)
		{
			uint32_t v = Integ345a10(sys + 0x2Cu);
			uint32_t elems = ZmR32(m, v);
			uint32_t cnt = ZmR32(m, sys + 0x30u);
			ExtZp_Integ_33f800(c, sys + 0x2Cu, elems + (cnt << 2), 1u, p);
		}
		p += 4u;
	}
}

// ------------------------------------------------------------------ the system step
static int IntegTimeDiffers(ExtZpMem *m, uint32_t sys, uint32_t curInt, Ps2F curFrac)
{
	uint32_t tf = ZmR32(m, sys + 0x40u) + (ZmR32(m, sys + 0x68u) << 3);
	if (curInt != ZmR32(m, tf))
		return 1;
	return !Ps2F_Eq(curFrac, ZmRF(m, tf + 4u));
}

// Integration from the system's time to t: the bodies' states are taken, the derivative evaluated, the ODE solver
// (Euler) advances, the states are set again, the time moves. With a fixed step h (+0x88 > 0) and a longer interval the
// interval is cut into whole steps of h plus a remainder, forces and controllers running before each step. Without a
// fixed step (or without solver or bodies) one step, or only the time, is done.
void ExtZp_Integ_39adb0(ExtZpCtx *c, uint32_t sys, uint64_t t)
{
	ExtZpMem *m = c->m;
	uint32_t tlo = (uint32_t)t;
	Ps2F f22 = (Ps2F)(t >> 32), f20 = 0u, f21 = 0u;
	uint32_t cnt = ExtZp_Push(c, 16u); // the count the derivative reports (a stack local passed by address)
	uint32_t listStart, s1end, p, idx, tf;
	ExtZp_Integ_38ea50(c, sys);
	if (ZmR8(m, sys + 0xA5u) != 0)
	{
		// direct integration of every active body, no solver
		Ps2F d, oldFrac;
		uint32_t oldInt, a1;
		idx = ZmR32(m, sys + 0x68u);
		tf = ZmR32(m, sys + 0x40u) + (idx << 3);
		oldFrac = ZmRF(m, tf + 4u);
		oldInt = ZmR32(m, tf);
		d = Ps2F_Sub(f22, oldFrac);
		a1 = tlo - oldInt;
		ZmW32(m, sys + 0x5Cu, tlo);
		ZmWF(m, sys + 0x60u, f22);
		f20 = Ps2F_Add(Ps2F_FromInt((int32_t)a1), d);
		idx = ZmR32(m, sys + 0x68u);
		tf = ZmR32(m, ExtZp_Integ_39b540(c, sys + 0x38u)) + (idx << 3);
		ZmW32(m, tf, ZmR32(m, sys + 0x5Cu));
		ZmW32(m, tf + 4u, ZmR32(m, sys + 0x60u));
		p = ZmR32(m, Integ345a10(sys + 0x2Cu));
		s1end = ZmR32(m, Integ345a10(sys + 0x2Cu)) + (ZmR32(m, sys + 0x30u) << 2);
		while (p != s1end)
		{
			uint32_t b = ZmR32(m, p);
			ExtZp_Integ_399000(c, b);
			if (!IntegBodyFixed(m, b))
				ExtZp_Integ_398930(c, b, f20);
			ZmW32(m, b + 0x14u, INTEG_ONE);
			p += 4u;
			ZmWF(m, b + 0x1A0u, f20);
		}
		ExtZp_Pop(c, 16u);
		return;
	}
	listStart = ZmR32(m, sys + 0x34u);
	s1end = listStart + (ZmR32(m, sys + 0x30u) << 2);
	for (p = listStart; p != s1end; p += 4u)
		ExtZp_Integ_399000(c, ZmR32(m, p));
	idx = ZmR32(m, sys + 0x68u);
	tf = ZmR32(m, sys + 0x40u) + (idx << 3);
	{
		Ps2F f1 = Ps2F_Sub(f22, ZmRF(m, tf + 4u));
		uint32_t a0 = tlo - ZmR32(m, tf);
		ZmW32(m, cnt, 0);
		f21 = Ps2F_Add(Ps2F_FromInt((int32_t)a0), f1);
	}
	if (ZmR32(m, sys + 0x80u) == 0 || (int32_t)ZmR32(m, sys + 0x30u) <= 0)
	{
		// no solver or no bodies: only the time moves
		ZmW32(m, sys + 0x5Cu, tlo);
		ZmWF(m, sys + 0x60u, f22);
		idx = ZmR32(m, sys + 0x68u);
		tf = ZmR32(m, sys + 0x40u) + (idx << 3);
		ZmW32(m, tf, ZmR32(m, sys + 0x5Cu));
		ZmW32(m, tf + 4u, ZmR32(m, sys + 0x60u));
	}
	else
	{
		Ps2F h = ZmRF(m, sys + 0x88u);
		if (Ps2F_Le(h, 0u) || Ps2F_Le(f21, h))
		{
			// one step over the whole interval
			uint32_t oi, v1, t1, a0, a2, a3;
			Ps2F f12;
			idx = ZmR32(m, sys + 0x68u);
			IntegCallGet(c, sys, ZmR32(m, sys + 0x4Cu) + (idx << 4));
			idx = ZmR32(m, sys + 0x68u);
			IntegCallDeriv(c, sys, ZmR32(m, sys + 0x58u) + (idx << 4), cnt);
			a0 = ZmR32(m, sys + 0x68u);
			v1 = ZmR32(m, sys + 0x4Cu);
			t1 = v1 + ((a0 != 0 ? 0u : 1u) << 4);
			tf = ZmR32(m, sys + 0x40u) + (a0 << 3);
			oi = ZmR32(m, tf);
			f12 = Ps2F_Add(Ps2F_FromInt((int32_t)(tlo - oi)), Ps2F_Sub(f22, ZmRF(m, tf + 4u)));
			a2 = v1 + (a0 << 4);
			a3 = ZmR32(m, sys + 0x58u) + (a0 << 4);
			IntegCallOde(c, sys, f12, ZmR64(m, tf), a2, a3, t1, ZmR32(m, cnt));
			IntegCallSet(c, sys, ZmR32(m, sys + 0x4Cu) + ((ZmR32(m, sys + 0x68u) != 0 ? 0u : 1u) << 4));
		}
		else
		{
			// whole steps of h, then the remainder
			uint32_t s2 = 0, s3, s5, curInt;
			Ps2F curFrac, f1, f0;
			int32_t s6;
			IntegCallGet(c, sys, ZmR32(m, sys + 0x4Cu) + (ZmR32(m, sys + 0x68u) << 4));
			idx = ZmR32(m, sys + 0x68u);
			tf = ZmR32(m, sys + 0x40u) + (idx << 3);
			f1 = Ps2F_Div(f21, h);
			curInt = ZmR32(m, tf);
			curFrac = ZmRF(m, tf + 4u);
			s6 = Ps2F_ToInt(f1);
			f0 = Ps2F_FromInt(s6);
			f0 = Ps2F_Mul(f0, h);
			f20 = Ps2F_Sub(f21, f0);
			if (0 < s6)
			{
				for (s3 = 0; (int32_t)s3 < s6; s3++)
				{
					Ps2F f12;
					ZmW32(m, cnt, 0);
					if (IntegTimeDiffers(m, sys, curInt, curFrac))
						IntegCallForces(c, sys, ((uint64_t)curFrac << 32) | curInt);
					s5 = s2 << 4;
					IntegCallDeriv(c, sys, ZmR32(m, sys + 0xA0u) + s5, cnt);
					IntegCallGet(c, sys, ZmR32(m, sys + 0x94u) + s5);
					s2 = (s2 != 0) ? 0u : 1u;
					IntegCallOde(c, sys, ZmRF(m, sys + 0x88u), ((uint64_t)curFrac << 32) | curInt, ZmR32(m, sys + 0x94u) + s5,
					             ZmR32(m, sys + 0xA0u) + s5, ZmR32(m, sys + 0x94u) + (s2 << 4), ZmR32(m, cnt));
					IntegCallSet(c, sys, ZmR32(m, sys + 0x94u) + (s2 << 4));
					f12 = Ps2F_Add(curFrac, ZmRF(m, sys + 0x88u));
					curFrac = f12;
					if (!Ps2F_Lt(Ps2F_Abs(f12), INTEG_ONE))
					{
						Ps2F r;
						int32_t iv;
						if (Ps2F_Lt(0u, f12))
							r = ExtZp_Step_43b0b0(c, f12);
						else
							r = ExtZp_Step_43ae78(c, f12);
						iv = Ps2F_ToInt(r);
						curFrac = Ps2F_Sub(curFrac, r);
						curInt += (uint32_t)iv;
					}
				}
			}
			if (!Ps2F_Le(f20, INTEG_EPS))
			{
				uint32_t o;
				ZmW32(m, cnt, 0);
				if (IntegTimeDiffers(m, sys, curInt, curFrac))
					IntegCallForces(c, sys, ((uint64_t)curFrac << 32) | curInt);
				s2 <<= 4;
				IntegCallDeriv(c, sys, ZmR32(m, sys + 0xA0u) + s2, cnt);
				IntegCallGet(c, sys, ZmR32(m, sys + 0x94u) + s2);
				o = ZmR32(m, sys + 0x4Cu) + ((ZmR32(m, sys + 0x68u) != 0 ? 0u : 1u) << 4);
				IntegCallOde(c, sys, f20, ((uint64_t)curFrac << 32) | curInt, ZmR32(m, sys + 0x94u) + s2, ZmR32(m, sys + 0xA0u) + s2, o,
				             ZmR32(m, cnt));
				IntegCallSet(c, sys, ZmR32(m, sys + 0x4Cu) + ((ZmR32(m, sys + 0x68u) != 0 ? 0u : 1u) << 4));
			}
			else
			{
				uint32_t s2rec = ZmR32(m, sys + 0x94u) + (s2 << 4);
				uint32_t s3rec = ZmR32(m, sys + 0x4Cu) + ((ZmR32(m, sys + 0x68u) != 0 ? 0u : 1u) << 4);
				ExtZp_Integ_3908a0(c, s3rec + 4u, s2rec + 4u);
				ExtZp_Integ_36c8a0(c, s3rec + 0x10u);
				ZmW32(m, s3rec, ZmR32(m, Integ345a10(s3rec + 4u)));
			}
		}
		// common tail of the solver paths: current time, time field of the other index, flip the index
		{
			uint32_t o;
			ZmW32(m, sys + 0x5Cu, tlo);
			ZmWF(m, sys + 0x60u, f22);
			o = ZmR32(m, sys + 0x40u) + ((ZmR32(m, sys + 0x68u) != 0 ? 0u : 1u) << 3);
			ZmW32(m, o, ZmR32(m, sys + 0x5Cu));
			ZmW32(m, o + 4u, ZmR32(m, sys + 0x60u));
			ZmW32(m, sys + 0x68u, (ZmR32(m, sys + 0x68u) != 0 ? 0u : 1u));
		}
	}
	// every active body: time scale word := 1.0, last step := the interval
	{
		uint32_t a1 = ZmR32(m, sys + 0x34u);
		if (a1 != s1end)
		{
			do
			{
				uint32_t b = ZmR32(m, a1);
				ZmW32(m, b + 0x14u, INTEG_ONE);
				a1 += 4u;
				ZmWF(m, b + 0x1A0u, f21);
			} while (a1 != s1end);
		}
	}
	ExtZp_Pop(c, 16u);
}
