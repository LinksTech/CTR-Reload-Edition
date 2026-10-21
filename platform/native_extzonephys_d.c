// THE ZONE PORT, PART D: the kart's per-frame zone test, the gravity spline
// choice and the boost stage (see include/platform/native_extzonephys.h for the
// conventions, native_extzonephys_d.h for the signatures). Compiles alone too
// (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_d.h>
#include <platform/native_extzonephys_a.h>
#include <platform/native_extzonephys_b1.h>
#include <platform/native_extzonephys_b2.h>
#include <platform/native_extzonephys_c.h>
#include <platform/native_extzonephys_e.h>

#define KD_ONE 0x3F800000u         // 1.0 (the original's constant)
#define KD_MINUS_ONE 0xBF800000u   // -1.0 (the original's constant at 0x638c78)
#define KD_FLOAT_MAX 0x7F7FFFFFu   // 3.4028235e38 (the original's "no distance yet")
#define KD_TENTH 0x3DCCCCCDu       // 0.1 (the original's constant)
#define KD_G_CLASS_OBJ 0x006F63E0u // global: the class object the spline owners are checked against

// Spline object fields.
enum
{
	KD_SPL_KIND = 0x08,   // 0 plane, 1 point, 2 inverse point
	KD_SPL_LIST = 0x10,   // -> list
	KD_LIST_COUNT = 0x08, // node count
	KD_LIST_NODES = 0x10, // -> nodes (24 bytes each: position xyz, direction xyz)
};

static void KdQueryInit(ExtZpQueryArgs *args, uint32_t func)
{
	for (int i = 0; i < 8; i++)
	{
		args->a[i] = 0;
		args->f[i] = 0;
	}
	args->func = func;
}

// (a0 * b0 + a1 * b1) + a2 * b2 in the accumulator, the products as the
// original forms them (fs = a, ft = b).
static Ps2F KdDot(Ps2F a0, Ps2F a1, Ps2F a2, Ps2F b0, Ps2F b1, Ps2F b2)
{
	Ps2Acc acc = Ps2A_Adda(Ps2F_Mul(a0, b0), Ps2F_Mul(a1, b1));
	return Ps2A_Madd(acc, a2, b2);
}

// Unit vector in memory: x*x + y*y + z*z, root, one divided by it, each part times that.
static void KdUnitMem(ExtZpMem *m, uint32_t v)
{
	const Ps2F x = ZmRF(m, v + 0), y = ZmRF(m, v + 4), z = ZmRF(m, v + 8);
	const Ps2F inv = Ps2F_Div(KD_ONE, Ps2F_Sqrt(KdDot(x, y, z, x, y, z)));
	ZmWF(m, v + 0, Ps2F_Mul(ZmRF(m, v + 0), inv));
	ZmWF(m, v + 4, Ps2F_Mul(ZmRF(m, v + 4), inv));
	ZmWF(m, v + 8, Ps2F_Mul(ZmRF(m, v + 8), inv));
}

// Box test: is the point inside the box (every axis: lowest <= p . axis <= highest)?
uint32_t ExtZp_Kart_23cf10(ExtZpCtx *c, uint32_t box, uint32_t p)
{
	ExtZpMem *m = c->m;
	const Ps2F p0 = ZmRF(m, p + 0), p1 = ZmRF(m, p + 4), p2 = ZmRF(m, p + 8);
	for (uint32_t k = 0; k < 3; k++)
	{
		const uint32_t axis = box + 0x04u + 0xCu * k; // axis k
		const uint32_t range = box + 0x28u + 8u * k;  // range k: lowest, highest
		const Ps2F d = KdDot(ZmRF(m, axis + 0), ZmRF(m, axis + 4), ZmRF(m, axis + 8), p0, p1, p2);
		if (Ps2F_Lt(d, ZmRF(m, range + 0)))
		{
			return 0;
		}
		if (!Ps2F_Le(d, ZmRF(m, range + 4)))
		{
			return 0;
		}
	}
	return 1;
}

// First box of a list that holds the point; reports the entry through out.
uint32_t ExtZp_Kart_23d110(ExtZpCtx *c, uint32_t list, uint32_t out, uint32_t p)
{
	ExtZpMem *m = c->m;
	uint32_t entry = ZmR32(m, list + 4); // first entry
	while (entry != 0)
	{
		if (ExtZp_Kart_23cf10(c, ZmR32(m, entry + 0), p) != 0)
		{
			if (out != 0)
			{
				ZmW32(m, out + 0, entry);
				ZmW32(m, out + 4, 0);
			}
			return 1;
		}
		entry = ZmR32(m, entry + 4); // next
	}
	return 0;
}

// Local spline search: the nearest segment among node - 1 .. node + 1 (clamped to 0 .. count - 2).
uint32_t ExtZp_Kart_28b3f0(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t p)
{
	ExtZpMem *m = c->m;
	int32_t hi = (int32_t)(node + 1u);
	int32_t lo = (int32_t)(node - 1u);
	uint32_t result = 0xFFFFFFFFu;
	Ps2F best = KD_FLOAT_MAX;
	uint32_t list;
	{
		const int32_t last = (int32_t)(ZmR32(m, ZmR32(m, spline + KD_SPL_LIST) + KD_LIST_COUNT) - 2u);
		if (!(hi < last))
		{
			hi = last;
		}
	}
	if (lo <= 0)
	{
		lo = 0;
	}
	if (hi < lo)
	{
		return 0xFFFFFFFFu;
	}
	list = ZmR32(m, spline + KD_SPL_LIST);
	{
		const Ps2F px = ZmRF(m, p + 0), py = ZmRF(m, p + 4), pz = ZmRF(m, p + 8);
		for (int32_t j = lo; j <= hi; j++)
		{
			const uint32_t nodes = ZmR32(m, list + KD_LIST_NODES);
			const uint32_t a = nodes + (uint32_t)j * 24u;
			const uint32_t b = a + 0x18u;
			const Ps2F ax = ZmRF(m, a + 0), ay = ZmRF(m, a + 4), az = ZmRF(m, a + 8);
			const Ps2F bx = ZmRF(m, b + 0), by = ZmRF(m, b + 4), bz = ZmRF(m, b + 8);
			const Ps2F pay = Ps2F_Sub(py, ay), pax = Ps2F_Sub(px, ax);
			const Ps2F dy = Ps2F_Sub(by, ay), dx = Ps2F_Sub(bx, ax);
			const Ps2F paz = Ps2F_Sub(pz, az), dz = Ps2F_Sub(bz, az);
			Ps2F ex, ey, ez, d;
			// u = ((b - a) . (p - a)) / |b - a|^2
			const Ps2F num = KdDot(dx, dy, dz, pax, pay, paz);
			const Ps2F den = KdDot(dx, dy, dz, dx, dy, dz);
			const Ps2F u = Ps2F_Div(num, den);
			if (Ps2F_Le(u, 0u))
			{
				ex = Ps2F_Sub(ax, px);
				ey = Ps2F_Sub(ay, py);
				ez = Ps2F_Sub(az, pz);
			}
			else if (!Ps2F_Lt(u, KD_ONE))
			{
				ex = Ps2F_Sub(bx, px);
				ey = Ps2F_Sub(by, py);
				ez = Ps2F_Sub(bz, pz);
			}
			else
			{
				ez = Ps2F_Sub(Ps2F_Add(az, Ps2F_Mul(dz, u)), pz);
				ey = Ps2F_Sub(Ps2F_Add(ay, Ps2F_Mul(dy, u)), py);
				ex = Ps2F_Sub(Ps2F_Add(ax, Ps2F_Mul(dx, u)), px);
			}
			d = KdDot(ex, ey, ez, ex, ey, ez);
			if (Ps2F_Lt(d, best))
			{
				best = d;
				result = (uint32_t)j;
			}
		}
	}
	return result;
}

// Global spline search: the node nearest to the point, then the local search from it.
uint32_t ExtZp_Kart_28b320(ExtZpCtx *c, uint32_t spline, uint32_t p)
{
	ExtZpMem *m = c->m;
	Ps2F best = KD_FLOAT_MAX;
	uint32_t node = 0xFFFFFFFFu;
	const uint32_t list = ZmR32(m, spline + KD_SPL_LIST);
	if (0 < (int32_t)ZmR32(m, list + KD_LIST_COUNT))
	{
		const Ps2F px = ZmRF(m, p + 0), py = ZmRF(m, p + 4), pz = ZmRF(m, p + 8);
		const uint32_t nodes = ZmR32(m, list + KD_LIST_NODES);
		uint32_t i = 0;
		do
		{
			const uint32_t n = nodes + i * 24u;
			const Ps2F ex = Ps2F_Sub(ZmRF(m, n + 0), px);
			const Ps2F ez = Ps2F_Sub(ZmRF(m, n + 8), pz);
			const Ps2F ey = Ps2F_Sub(ZmRF(m, n + 4), py);
			const Ps2F d = KdDot(ex, ey, ez, ex, ey, ez);
			if (Ps2F_Lt(d, best))
			{
				best = d;
				node = i;
			}
			i++;
		} while ((int32_t)i < (int32_t)ZmR32(m, ZmR32(m, spline + KD_SPL_LIST) + KD_LIST_COUNT));
	}
	return ExtZp_Kart_28b3f0(c, spline, node, p);
}

// Force direction of a spline at a point on segment `node` (node == count: the last
// segment is taken from node - 1 and node, which reads past the nodes as the original does).
void ExtZp_Kart_28b600(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t p, uint32_t out)
{
	ExtZpMem *m = c->m;
	const uint32_t list = ZmR32(m, spline + KD_SPL_LIST);
	const uint32_t nodes = ZmR32(m, list + KD_LIST_NODES);
	const uint32_t count = ZmR32(m, list + KD_LIST_COUNT);
	uint32_t a = nodes + node * 24u, b;
	if (node != count)
	{
		b = nodes + (node + 1u) * 24u;
	}
	else
	{
		b = a;
		a = nodes + (node - 1u) * 24u;
	}
	{
		Ps2F cx = ZmRF(m, a + 0), cy = ZmRF(m, a + 4), cz = ZmRF(m, a + 8);
		const Ps2F bx = ZmRF(m, b + 0), by = ZmRF(m, b + 4), bz = ZmRF(m, b + 8);
		const Ps2F px = ZmRF(m, p + 0), py = ZmRF(m, p + 4), pz = ZmRF(m, p + 8);
		const Ps2F dy = Ps2F_Sub(by, cy), dx = Ps2F_Sub(bx, cx), dz = Ps2F_Sub(bz, cz);
		const Ps2F pay = Ps2F_Sub(py, cy), pax = Ps2F_Sub(px, cx), paz = Ps2F_Sub(pz, cz);
		const Ps2F u = Ps2F_Div(KdDot(dx, dy, dz, pax, pay, paz), KdDot(dx, dy, dz, dx, dy, dz));
		const uint32_t kind = ZmR32(m, spline + KD_SPL_KIND);

		// c = the nearest point of the segment (a, inside, or b)
		if (!Ps2F_Le(u, 0u))
		{
			if (Ps2F_Lt(u, KD_ONE))
			{
				cx = Ps2F_Add(cx, Ps2F_Mul(dx, u));
				cy = Ps2F_Add(cy, Ps2F_Mul(dy, u));
				cz = Ps2F_Add(cz, Ps2F_Mul(dz, u));
			}
			else
			{
				cx = bx;
				cy = by;
				cz = bz;
			}
		}
		if (kind == 2u)
		{
			// inverse point: unit(c - p) times the original's constant (-1)
			ZmWF(m, out + 0, Ps2F_Sub(cx, px));
			ZmWF(m, out + 4, Ps2F_Sub(cy, py));
			ZmWF(m, out + 8, Ps2F_Sub(cz, pz));
			KdUnitMem(m, out);
			ZmWF(m, out + 0, Ps2F_Mul(ZmRF(m, out + 0), KD_MINUS_ONE));
			ZmWF(m, out + 4, Ps2F_Mul(ZmRF(m, out + 4), KD_MINUS_ONE));
			ZmWF(m, out + 8, Ps2F_Mul(ZmRF(m, out + 8), KD_MINUS_ONE));
		}
		else if (kind == 1u)
		{
			// point: unit(c - p)
			ZmWF(m, out + 0, Ps2F_Sub(cx, px));
			ZmWF(m, out + 4, Ps2F_Sub(cy, py));
			ZmWF(m, out + 8, Ps2F_Sub(cz, pz));
			KdUnitMem(m, out);
		}
		else if (kind == 0u)
		{
			// plane: the directions blended by the unclamped u, then unit
			const Ps2F a3 = ZmRF(m, a + 0xC), b3 = ZmRF(m, b + 0xC);
			const Ps2F a5 = ZmRF(m, a + 0x14), b5 = ZmRF(m, b + 0x14);
			const Ps2F a4 = ZmRF(m, a + 0x10), b4 = ZmRF(m, b + 0x10);
			const Ps2F x = Ps2F_Add(Ps2F_Mul(Ps2F_Sub(b3, a3), u), a3);
			ZmWF(m, out + 0, x);
			{
				const Ps2F z = Ps2F_Add(Ps2F_Mul(Ps2F_Sub(b5, a5), u), a5);
				const Ps2F y = Ps2F_Add(Ps2F_Mul(Ps2F_Sub(b4, a4), u), a4);
				ZmWF(m, out + 4, y);
				ZmWF(m, out + 8, z);
			}
			KdUnitMem(m, out);
		}
		else
		{
			ZmW32(m, out + 0, 0);
			ZmW32(m, out + 4, 0);
			ZmW32(m, out + 8, KD_ONE);
		}
	}
}

// out = a x b (the vector unit's outer product pair), w = a.w.
void ExtZp_Kart_120720(ExtZpCtx *c, uint32_t out, uint32_t a, uint32_t b)
{
	Ps2Vec4 va = ZmRV(c->m, a), vb = ZmRV(c->m, b), r;
	Ps2VAcc acc;
	r = va;
	Ps2V_OpMulaAcc(&acc, &va, &vb);
	Ps2V_OpMsubAcc(&r, &acc, &vb, &va);
	ZmWV(c->m, out, r);
}

// Boost start: the stage's values from the kart's table (kart + 0x1E4 + 12 * stage:
// speed, time, wheelie) scaled by the intensity (f12, at least 0.1).
// State 7 (kart + 0x3E0): no effect; state 3: first changed to 2.
void ExtZp_Kart_196890(ExtZpCtx *c, uint32_t kart, uint32_t stage, Ps2F intensity)
{
	ExtZpMem *m = c->m;
	const uint32_t state = ZmR32(m, kart + 0x3E0);
	Ps2F f20 = intensity;
	uint32_t row;
	Ps2F f1, f0;

	if (state == 7u)
	{
		return;
	}
	if (state == 3u)
	{
		ExtZp_Kart_1911b0(c, kart, 2u, 0u);
	}
	if (Ps2F_Le(f20, KD_TENTH))
	{
		f20 = KD_TENTH;
	}
	ZmW32(m, kart + 0x4C0, stage); // boost kind
	row = stage * 12u + kart;
	f1 = ZmRF(m, kart + 0x4C8);
	if (Ps2F_Le(f1, 0u))
	{
		// no boost running: take the table values
		ZmW32(m, kart + 0x4C4, ZmR32(m, row + 0x1E4));
		ZmWF(m, kart + 0x4C8, Ps2F_Mul(ZmRF(m, row + 0x1E8), f20));
		ZmWF(m, kart + 0x4CC, Ps2F_Mul(ZmRF(m, row + 0x1EC), f20));
	}
	else
	{
		// running: speed = max(old, table); time += table time * intensity, capped
		f1 = ZmRF(m, row + 0x1E4);
		f0 = ZmRF(m, kart + 0x4C4);
		if (Ps2F_Le(f0, f1))
		{
			f0 = f1;
		}
		ZmWF(m, kart + 0x4C4, f0);
		f1 = ZmRF(m, row + 0x1E8);
		f0 = ZmRF(m, kart + 0x4C8);
		f1 = Ps2F_Mul(f1, f20);
		f0 = Ps2F_Add(f0, f1);
		ZmWF(m, kart + 0x4C8, f0);
		f1 = ZmRF(m, kart + 0x1D0);
		f0 = ZmRF(m, kart + 0x4C8);
		if (!Ps2F_Lt(f0, f1))
		{
			f0 = f1;
		}
		ZmWF(m, kart + 0x4C8, f0);
		ZmWF(m, kart + 0x4CC, Ps2F_Mul(ZmRF(m, row + 0x1EC), f20));
	}
	f1 = ZmRF(m, kart + 0x4CC);
	if (!Ps2F_Le(f1, 0u))
	{
		if (!Ps2F_Le(f20, ZmRF(m, kart + 0x38C)))
		{
			f0 = ZmRF(m, kart + 0x550);
			if (Ps2F_Le(f0, f1))
			{
				f0 = f1;
			}
			ZmWF(m, kart + 0x550, f0);
			f1 = ZmRF(m, kart + 0x388);
			f0 = ZmRF(m, kart + 0x550);
			if (Ps2F_Le(f0, f1))
			{
				f0 = f1;
			}
			ZmWF(m, kart + 0x550, f0);
		}
	}
	if (stage != 0xBu)
	{
		uint32_t driver;
		ZmW32(m, kart + 0x4D0, ZmR32(m, kart + 0x4D0) + 1u); // boosts in a row
		ZmWF(m, kart + 0x4D4, Ps2F_Neg(ZmRF(m, kart + 0x4C8)));
		driver = ZmR32(m, kart + 0x38);
		if (driver != 0u)
		{
			// the driver's method at its table slot +0x34 (driver, count): part c
			ExtZp_Kart_DriverSlot34(c, driver, ZmR32(m, kart + 0x4D0));
		}
	}
	ZmW32(m, kart + 0x4BC, ZmR32(m, kart + 0x1D4)); // push timer
}

// Boost values from the stage's table row, without scaling: speed and time as max,
// wheelie copied then max-ed into the wheelie time; the in-a-row timer = -time.
void ExtZp_Kart_196aa0(ExtZpCtx *c, uint32_t kart, uint32_t stage)
{
	ExtZpMem *m = c->m;
	const uint32_t row = kart + stage * 12u;
	Ps2F f1, f0;

	f1 = ZmRF(m, row + 0x1E4);
	f0 = ZmRF(m, kart + 0x4C4);
	if (Ps2F_Le(f0, f1))
	{
		f0 = f1;
	}
	ZmWF(m, kart + 0x4C4, f0);
	f1 = ZmRF(m, row + 0x1E8);
	f0 = ZmRF(m, kart + 0x4C8);
	if (Ps2F_Le(f0, f1))
	{
		f0 = f1;
	}
	ZmWF(m, kart + 0x4C8, f0);
	f1 = ZmRF(m, row + 0x1EC);
	ZmWF(m, kart + 0x4CC, f1);
	f0 = ZmRF(m, kart + 0x550);
	if (Ps2F_Le(f0, f1))
	{
		f0 = f1;
	}
	ZmWF(m, kart + 0x550, f0);
	ZmWF(m, kart + 0x4D4, Ps2F_Neg(ZmRF(m, kart + 0x4C8)));
}

// The owner object a spline list entry belongs to, if it is of the expected class (else 0).
static uint32_t KdOwner(ExtZpCtx *c, uint32_t list, uint32_t index)
{
	ExtZpMem *m = c->m;
	uint32_t s1 = ZmR32(m, ZmR32(m, list + 0x10) + index * 4u);
	if (s1 != 0u)
	{
		if (ExtZp_Kart_512ee0(c, s1, ZmR32(m, KD_G_CLASS_OBJ)) == 0u)
		{
			s1 = 0u;
		}
	}
	return s1;
}

// Spline choice: remembers the index and the nearest segment of the spline of that
// index in the kart's two spline lists (kart + 0xA8 and kart + 0xAC).
void ExtZp_Kart_198bf0(ExtZpCtx *c, uint32_t kart, uint32_t index)
{
	ExtZpMem *m = c->m;
	uint32_t s1, point, list;

	if (ZmR32(m, kart + 0xA8) == 0u)
	{
		return;
	}
	if (index == 0xFFFFFFFFu)
	{
		ZmW32(m, kart + 0x464, 0xFFFFFFFFu);
		ZmW32(m, kart + 0x468, 0xFFFFFFFFu);
		ZmW32(m, kart + 0x46C, 0xFFFFFFFFu);
		return;
	}
	ZmW32(m, kart + 0x464, index);
	// the body's position (vector at body + 0x30; body = *(**(kart + 0x2C) + 0x94)), copied to a
	// local the callee gets by address
	{
		const Ps2Vec4 pos = ZmRV(m, ZmR32(m, ZmR32(m, ZmR32(m, kart + 0x2C)) + 0x94u) + 0x30u);
		point = ExtZp_Push(c, 16);
		ZmWF(m, point + 0, pos.v[0]);
		ZmWF(m, point + 4, pos.v[1]);
		ZmWF(m, point + 8, pos.v[2]);
	}
	list = ZmR32(m, ZmR32(m, kart + 0xA8) + 8u);
	s1 = KdOwner(c, list, ZmR32(m, kart + 0x464));
	ZmW32(m, kart + 0x468, ExtZp_Kart_28b320(c, s1, point));
	ZmW32(m, kart + 0x46C, 0xFFFFFFFFu);
	if (ZmR32(m, kart + 0xAC) != 0u)
	{
		list = ZmR32(m, ZmR32(m, kart + 0xAC) + 8u);
		if ((int32_t)ZmR32(m, kart + 0x464) < (int32_t)ZmR32(m, list + 8u))
		{
			s1 = KdOwner(c, list, ZmR32(m, kart + 0x464));
			if (s1 != 0u)
			{
				ZmW32(m, kart + 0x46C, ExtZp_Kart_28b320(c, s1, point));
			}
		}
	}
	ExtZp_Pop(c, 16);
}

// ------------------------------------------------------------------ 0x396f00

// Refreshes a body's rotation rows from its quaternion (see the header).
void ExtZp_Integ_396f00(ExtZpCtx *c, uint32_t body)
{
	ExtZpMem *m = c->m;
	const uint32_t tmp = ExtZp_Push(c, 0x30); // 3 rows of 4 floats the product is built in

	ExtZp_Integ_3f6780(c, body + 0xE0u, body + 0x40u);
	ZmWV(m, body + 0xE0u + 0x30u, ZmRV(m, body + 0x30u)); // position next to the rows
	ExtZp_Integ_3f6b20(c, tmp, body + 0xE0u, body + 0xB0u);
	for (uint32_t row = 0; row < 3; row++)
	{
		for (uint32_t w = 0; w < 4; w++)
		{
			ZmW32(m, body + 0x120u + 0x10u * row + 4u * w, ZmR32(m, tmp + 0x10u * row + 4u * w));
		}
	}
	ZmW32(m, body + 0x294u, ZmR32(m, body + 0x294u) + 1u);
	ExtZp_Pop(c, 0x30);
}

// ------------------------------------------------------------------ 0x1911f0

#define KZ_G_TABLE 0x006F3B20u    // global: the zone table (lists of boxes) of the loaded track
#define KZ_G_SURFACE 0x006F5620u  // global: the object the surface-type boost test asks
#define KZ_HALF 0x3F000000u       // 0.5 (the original's constant)
#define KZ_TWO 0x40000000u        // 2.0 (the original's constant)

// Frame offsets of the original's stack locals (the frame is 0x2A0 bytes).
enum
{
	KZ_HIT = 0x288,   // out record of the list test: {entry, 0}
	KZ_P = 0x290,     // the point: the body's position xyz
	KZ_TMP = 0x29C,   // scratch word
};

static void KzCopy(ExtZpMem *m, uint32_t dst, uint32_t src)
{
	ZmWV(m, dst, ZmRV(m, src));
}

// The body's position vector: **(kart + 0x2C) + 0x94 -> body, body + 0x30.
static Ps2Vec4 KzBodyPos(ExtZpMem *m, uint32_t kart)
{
	return ZmRV(m, ZmR32(m, ZmR32(m, ZmR32(m, kart + 0x2C)) + 0x94u) + 0x30u);
}

// The test point = the body's position (three floats).
static void KzLoadPoint(ExtZpMem *m, uint32_t kart, uint32_t point)
{
	const Ps2Vec4 v = KzBodyPos(m, kart);
	ZmWF(m, point + 0, v.v[0]);
	ZmWF(m, point + 4, v.v[1]);
	ZmWF(m, point + 8, v.v[2]);
}

// The box of the entry the last list test found (hit -> entry -> box).
static uint32_t KzHitBox(ExtZpMem *m, uint32_t hit)
{
	return ZmR32(m, ZmR32(m, hit));
}

static void KzQuery(ExtZpCtx *c, int kind, uint32_t func, uint32_t a0, uint32_t a1)
{
	ExtZpQueryArgs args;
	KdQueryInit(&args, func);
	args.a[0] = a0;
	args.a[1] = a1;
	(void)ExtZp_Query(c, kind, &args);
}

// The wall-like box (kart + 0x568) the body is inside: pushes the body out along the
// box's normal (a plane, or a rotated one from a quaternion-like entry) and removes the
// velocity component into it. sp = the frame.
static void KzPushOut(ExtZpCtx *c, uint32_t K, uint32_t sp)
{
	ExtZpMem *m = c->m;
	const uint32_t s2 = sp + 0x240;
	const uint32_t box = ZmR32(m, K + 0x568);
	const uint32_t s1 = ZmR32(m, box + 0x40);
	Ps2F f3, f0, f1, f2;
	Ps2Acc acc;

	ZmW32(m, sp + 0x248, 0);
	ZmW32(m, sp + 0x244, 0);
	ZmW32(m, s2, 0);
	ZmW32(m, sp + 0x24C, KD_ONE);
	ZmW32(m, sp + 0x22C, KD_ONE);
	ZmW32(m, sp + 0x21C, KD_ONE);
	ZmW32(m, sp + 0x228, 0);
	ZmW32(m, sp + 0x224, 0);
	ZmW32(m, sp + 0x220, 0);
	ZmW32(m, sp + 0x218, 0);
	ZmW32(m, sp + 0x214, 0);
	ZmW32(m, sp + 0x210, 0);
	if ((s1 & 1u) != 0u)
	{
		// a plane entry: normal (0, 0, 1), point from the entry, offset from the entry
		const uint32_t e = s1 & 0xFFFFFFFEu;
		ZmW32(m, sp + 0x1B0, 0);
		ZmW32(m, sp + 0x1B4, 0);
		ZmW32(m, sp + 0x1B8, KD_ONE);
		ZmW32(m, sp + 0x1BC, KD_ONE);
		KzCopy(m, sp + 0x220, sp + 0x1B0);
		ZmW32(m, sp + 0x1AC, KD_ONE);
		ZmW32(m, sp + 0x1A8, ZmR32(m, e + 0x14));
		ZmW32(m, sp + 0x1A4, ZmR32(m, e + 0x10));
		ZmW32(m, sp + 0x1A0, ZmR32(m, e + 0xC));
		KzCopy(m, sp + 0x210, sp + 0x1A0);
		f0 = Ps2F_Neg(ZmRF(m, e + 0x20));
		f3 = Ps2F_Mul(KZ_HALF, f0);
	}
	else
	{
		// a turned entry: the normal is (0, 0, 2cos^2 - 1) plus twice the cross part of the
		// quaternion (axis * sin(half angle), cos(half angle))
		const uint32_t s3 = sp + 0x190;
		const Ps2F f20 = KZ_HALF;
		const Ps2F f21 = ZmRF(m, s1 + 0x18);
		Ps2F cosv, sinv, f12;
		Ps2Vec4 v, inv;

		f3 = 0u;
		ZmWF(m, sp + 0x198, f3);
		ZmWF(m, sp + 0x194, f3);
		ZmWF(m, s3, f3);
		ZmW32(m, sp + 0x178, ZmR32(m, s1 + 0x14));
		ZmW32(m, sp + 0x174, ZmR32(m, s1 + 0x10));
		ZmW32(m, sp + 0x170, ZmR32(m, s1 + 0xC));
		ZmW32(m, sp + 0x17C, KD_ONE);
		f12 = Ps2F_Mul(f20, f21);
		ZmW32(m, sp + 0x19C, KD_ONE);
		cosv = ExtZp_Math_43af80(c, f12);
		ZmWF(m, sp + 0x180, cosv);
		KzCopy(m, s3, sp + 0x170);
		sinv = ExtZp_Math_43b320(c, Ps2F_Mul(f20, f21));
		v = ZmRV(m, s3);
		inv.v[0] = sinv;
		inv.v[1] = inv.v[2] = inv.v[3] = 0u;
		inv = Ps2V_Bc(&inv, 0);
		Ps2V_Mul(&v, &v, &inv, 0xEu);
		ZmWV(m, s3, v);
		ZmWF(m, sp + 0x230, ZmRF(m, sp + 0x180));
		KzCopy(m, s2, s3);
		ZmW32(m, sp + 0x140, 0);
		ZmW32(m, sp + 0x144, 0);
		ZmW32(m, sp + 0x148, KD_ONE);
		ZmW32(m, sp + 0x14C, KD_ONE);
		(void)ExtZp_Kart_1207f0(c, sp + 0xD0);
		f1 = ZmRF(m, sp + 0x230);
		f0 = Ps2F_Mul(KZ_TWO, f1);
		f0 = Ps2F_Mul(f0, f1);
		ExtZp_Integ_1207c0(c, sp + 0xC0, sp + 0x140, Ps2F_Sub(f0, KD_ONE));
		(void)ExtZp_Integ_1207b0(c, sp + 0xD0, sp + 0xC0);
		f12 = ExtZp_Integ_120780(c, s2, sp + 0x140);
		ExtZp_Integ_120750(c, sp + 0x90, s2, f12);
		ExtZp_Kart_120720(c, sp + 0x70, s2, sp + 0x140);
		ExtZp_Integ_120750(c, sp + 0x80, sp + 0x70, ZmRF(m, sp + 0x230));
		ExtZp_Integ_1206f0(c, sp + 0xA0, sp + 0x90, sp + 0x80);
		ExtZp_Integ_120750(c, sp + 0xB0, sp + 0xA0, KZ_TWO);
		(void)ExtZp_Kart_1206d0(c, sp + 0xD0, sp + 0xB0);
		(void)ExtZp_Integ_1206c0(c, sp + 0x150, sp + 0xD0);
		KzCopy(m, sp + 0x160, sp + 0x150);
		KzCopy(m, sp + 0x220, sp + 0x160);
		ZmW32(m, sp + 0x13C, KD_ONE);
		ZmW32(m, sp + 0x138, ZmR32(m, s1 + 0x24));
		ZmW32(m, sp + 0x134, ZmR32(m, s1 + 0x20));
		ZmW32(m, sp + 0x130, ZmR32(m, s1 + 0x1C));
		KzCopy(m, sp + 0x210, sp + 0x130);
		f0 = Ps2F_Neg(ZmRF(m, s1 + 0x30));
		f3 = Ps2F_Mul(f20, f0);
	}
	// the body's position relative to the entry's point, along the normal
	{
		Ps2Vec4 pos = KzBodyPos(m, K), rel, pt;
		ZmWV(m, sp + 0x120, pos);
		KzCopy(m, sp + 0x200, sp + 0x120);
		rel = ZmRV(m, sp + 0x200);
		pt = ZmRV(m, sp + 0x210);
		Ps2V_Sub(&rel, &rel, &pt, 0xEu);
		ZmWV(m, sp + 0x200, rel);
	}
	f2 = ZmRF(m, sp + 0x200);
	f0 = ZmRF(m, sp + 0x220);
	f1 = ZmRF(m, sp + 0x204);
	acc = Ps2A_Mula(f2, f0);
	f0 = ZmRF(m, sp + 0x224);
	f2 = ZmRF(m, sp + 0x208);
	acc = Ps2A_Madda(acc, f1, f0);
	f0 = ZmRF(m, sp + 0x228);
	f1 = Ps2A_Madd(acc, f2, f0);
	f0 = Ps2F_Sub(f1, ZmRF(m, K + 0x5C));
	if (!Ps2F_Lt(f0, f3))
	{
		return; // not through the plane
	}
	// depth = f3 - f0: the body moves out along the normal by that much
	f0 = Ps2F_Sub(f3, f0);
	{
		Ps2Vec4 n = ZmRV(m, sp + 0x220), sc, out, pos, body;
		sc.v[0] = f0;
		sc.v[1] = sc.v[2] = sc.v[3] = 0u;
		sc = Ps2V_Bc(&sc, 0);
		out = n;
		Ps2V_Mul(&out, &n, &sc, 0xEu);
		ZmWV(m, sp + 0xE0, out);
		KzCopy(m, sp + 0x1F0, sp + 0xE0);
		ZmWV(m, sp + 0x110, KzBodyPos(m, K));
		out = ZmRV(m, sp + 0x1F0);
		pos = ZmRV(m, sp + 0x110);
		Ps2V_Add(&out, &out, &pos, 0xEu);
		ZmWV(m, sp + 0x1F0, out);
		KzCopy(m, sp + 0x100, sp + 0x1F0);
		(void)body;
	}
	{
		// the body takes the new position; its saved copies follow; the rows are refreshed
		const uint32_t h = ZmR32(m, ZmR32(m, ZmR32(m, K + 0x2C)) + 0x94u);
		Ps2Vec4 vn, vsc, vv;
		uint32_t p1;
		Ps2F d;

		KzCopy(m, h + 0x30u, sp + 0x100);
		KzCopy(m, h + 0x150u, h + 0x30u);
		ZmWV(m, h + 0xE0u + 0x30u, ZmRV(m, h + 0x30u));
		ExtZp_Integ_396f00(c, h);
		ExtZp_Integ_399ca0(c, h);
		// velocity: v - n * (n . v)
		KzCopy(m, sp + 0x1E0, sp + 0x220);
		p1 = ZmR32(m, ZmR32(m, K + 0x2C)) + 0x40u;
		f2 = ZmRF(m, sp + 0x1E0);
		f0 = ZmRF(m, p1 + 0);
		f1 = ZmRF(m, sp + 0x1E4);
		acc = Ps2A_Mula(f2, f0);
		f0 = ZmRF(m, p1 + 4);
		f2 = ZmRF(m, sp + 0x1E8);
		acc = Ps2A_Madda(acc, f1, f0);
		f0 = ZmRF(m, p1 + 8);
		f0 = Ps2A_Madd(acc, f2, f0);
		d = Ps2F_Neg(f0);
		vn = ZmRV(m, sp + 0x1E0);
		vsc.v[0] = d;
		vsc.v[1] = vsc.v[2] = vsc.v[3] = 0u;
		vsc = Ps2V_Bc(&vsc, 0);
		Ps2V_Mul(&vn, &vn, &vsc, 0xEu);
		ZmWV(m, sp + 0x1E0, vn);
		p1 = ZmR32(m, ZmR32(m, K + 0x2C)) + 0x40u;
		vn = ZmRV(m, sp + 0x1E0);
		vv = ZmRV(m, p1);
		Ps2V_Add(&vn, &vn, &vv, 0xEu);
		ZmWV(m, sp + 0x1E0, vn);
		{
			const uint32_t s0 = ZmR32(m, ZmR32(m, K + 0x2C));
			KzCopy(m, sp + 0xF0, sp + 0x1E0);
			const uint32_t v = ExtZp_Kart_136160(c, sp + 0xF0);
			ExtZp_Kart_13dd40(c, ZmR32(m, s0 + 0x94u), v);
			ExtZp_Glue_136050(c, s0);
		}
	}
}

// The zone test of a kart (see the header). Frame offsets as in the original.
void ExtZp_Kart_1911f0(ExtZpCtx *c, uint32_t K)
{
	ExtZpMem *m = c->m;
	const uint32_t D = ZmR32(m, K + 0x38); // the driver object
	uint32_t sp, T, a0, v0, entry;
	const uint32_t hitAt = KZ_HIT, pAt = KZ_P;
	Ps2F f1;

	if (D == 0u)
	{
		return;
	}
	sp = ExtZp_Push(c, 0x2A0);
	T = ZmR32(m, KZ_G_TABLE);
	{
		const uint32_t hit = sp + hitAt, P = sp + pAt;

		ZmW32(m, hit, 0);
		KzLoadPoint(m, K, P);
		// portal boxes
		if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x30), hit, P) != 0u)
		{
			KzQuery(c, EXT_ZP_Q_PORTAL, 0x199220u, K, ZmR32(m, KzHitBox(m, hit) + 0x40u));
			KzLoadPoint(m, K, P);
		}
		// surface-type boost zone
		if (ZmR32(m, K + 0x38) == 0u)
		{
			goto L191448;
		}
		{
			const uint32_t surf = ZmR32(m, K + 0x420);
			const uint32_t obj = ZmR32(m, KZ_G_SURFACE);
			uint32_t which = 0xFFFFFFFFu;
			if (surf == 0x1Au)
			{
				which = 3;
			}
			else if (surf == 0x19u)
			{
				which = 2;
			}
			else if (surf == 0xEu)
			{
				which = 1;
			}
			else if (surf == 0xDu)
			{
				which = 0;
			}
			v0 = (which == 0xFFFFFFFFu) ? 0u : (ExtZp_Kart_1a37b0(c, obj, which) & 0xFFu);
		}
		if (v0 == 0u)
		{
			if (ZmR8(m, K + 0x402) != 0u)
			{
				ZmW8(m, K + 0x4D8, 0);
			}
			goto L191448;
		}
		f1 = ZmRF(m, K + 0x4C8);
		if (!Ps2F_Le(f1, 0u) && ZmR8(m, K + 0x4D8) != 0u)
		{
			ExtZp_Kart_196aa0(c, K, 6u);
			a0 = ZmR32(m, K + 0x554);
			goto L19144c;
		}
		if (ZmR32(m, K + 0x3E0) == 4u || !Ps2F_Lt(ZmRF(m, K + 0x3F4), KD_TENTH))
		{
			ZmW8(m, K + 0x4D8, 0);
		}
		else
		{
			ExtZp_Kart_196890(c, K, 6u, KD_ONE);
			ZmW8(m, K + 0x4D8, 1);
		}
	L191448:
		a0 = ZmR32(m, K + 0x554);
	L19144c:
		// the current zone box (kart + 0x554): still inside, or look for one
		if (a0 == 0u)
		{
			if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x10), hit, P) != 0u)
			{
				ZmW32(m, K + 0x554, KzHitBox(m, hit));
				if (ZmR32(m, K + 0x3E0) != 4u)
				{
					ExtZp_Kart_196890(c, K, 6u, KD_ONE);
				}
			}
		}
		else if (ExtZp_Kart_23cf10(c, a0, P) == 0u)
		{
			ZmW32(m, K + 0x554, 0);
		}
		else
		{
			f1 = ZmRF(m, K + 0x4C8);
			if (!Ps2F_Le(f1, 0u))
			{
				ExtZp_Kart_196aa0(c, K, 6u);
			}
			else if (ZmR32(m, K + 0x3E0) != 4u && Ps2F_Lt(ZmRF(m, K + 0x3F4), KD_TENTH))
			{
				ExtZp_Kart_196890(c, K, 6u, KD_ONE);
			}
		}
		// maglev on / off lists
		if (ZmR8(m, K + 0x428) != 0u)
		{
			if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x24), hit, P) != 0u)
			{
				ExtZp_Kart_1967e0(c, K, 0u);
			}
		}
		else if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x20), hit, P) != 0u)
		{
			ExtZp_Kart_1967e0(c, K, 1u);
		}
		// gravity volume (kart + 0x558) and its factor (kart + 0x460)
		a0 = ZmR32(m, K + 0x558);
		if (a0 != 0u)
		{
			if (ExtZp_Kart_23cf10(c, a0, P) == 0u)
			{
				ZmW32(m, K + 0x558, 0);
				ZmW32(m, K + 0x460, KD_ONE);
			}
		}
		else if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x28), hit, P) != 0u)
		{
			ZmW32(m, K + 0x558, KzHitBox(m, hit));
			ZmW32(m, sp + KZ_TMP, ZmR32(m, KzHitBox(m, hit) + 0x40u));
			ZmW32(m, K + 0x460, ZmR32(m, sp + KZ_TMP));
		}
		// magnetic gravity volume (kart + 0x560) and its spline choice
		a0 = ZmR32(m, K + 0x560);
		if (a0 != 0u)
		{
			if (ExtZp_Kart_23cf10(c, a0, P) == 0u)
			{
				ZmW32(m, K + 0x560, 0);
				ExtZp_Kart_198bf0(c, K, 0xFFFFFFFFu);
			}
		}
		else if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x2C), hit, P) != 0u)
		{
			ZmW32(m, K + 0x560, KzHitBox(m, hit));
			ExtZp_Kart_198bf0(c, K, ZmR32(m, KzHitBox(m, hit) + 0x40u));
		}
		// driver zones: flags (+0x204 / +0x205 / +0x206) and the zone events
		if ((ZmR32(m, D + 0x38) & 2u) == 0u)
		{
			if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x14), hit, P) != 0u)
			{
				ZmW8(m, D + 0x204, 1);
				ZmW8(m, D + 0x206, 0);
			}
			a0 = ZmR32(m, K + 0x55C);
			if (a0 != 0u)
			{
				if (ExtZp_Kart_23cf10(c, a0, P) == 0u)
				{
					ZmW32(m, K + 0x55C, 0);
					if (ZmR32(m, D + 0x170) != 0u)
					{
						ZmW8(m, D + 0x205, 0);
						KzQuery(c, EXT_ZP_Q_OTHER, 0x16AB70u, D, 0u);
					}
				}
			}
			else if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x18), hit, P) != 0u)
			{
				v0 = 0u;
				if (ZmR32(m, D + 0x170) != 0u)
				{
					ZmW8(m, D + 0x205, 1);
					KzQuery(c, EXT_ZP_Q_OTHER, 0x16AB70u, D, 0u);
					v0 = 1u;
				}
				if (v0 != 0u)
				{
					entry = ZmR32(m, hit);
					ZmW32(m, K + 0x55C, ZmR32(m, entry));
				}
			}
			if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x1C), hit, P) != 0u)
			{
				KzQuery(c, EXT_ZP_Q_OTHER, 0x169F00u, D, 0u);
			}
		}
		// the wall-like box (kart + 0x568)
		a0 = ZmR32(m, K + 0x568);
		if (a0 == 0u)
		{
			if (ExtZp_Kart_23d110(c, ZmR32(m, T + 0x38), hit, P) != 0u)
			{
				ZmW32(m, K + 0x568, KzHitBox(m, hit));
			}
		}
		else if (ExtZp_Kart_23cf10(c, a0, P) == 0u)
		{
			ZmW32(m, K + 0x568, 0);
		}
		else
		{
			KzPushOut(c, K, sp);
		}
	}
	ExtZp_Pop(c, 0x2A0);
}
