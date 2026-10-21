// THE SOURCE GAME'S ZONE PHYSICS, PORTED STEP BY STEP (see include/platform/native_extzonephys.h).
//
// Conventions of the port: one C function per function of the original; the
// operations in the original's order and with its operand order (the
// multiplier is not symmetric: Ps2F_Mul(fs, ft)); a sum the original forms in
// the accumulator (ADDA then MADD) is written with Ps2A_*; a compare is the
// console's (Ps2F_Lt / Ps2F_Le). Every function names the original's
// behaviour, not its data: no data of the original lives here.

#include <platform/native_extzonephys.h>

#define EXT_ZP_ONE 0x3F800000u
#define EXT_ZP_MINUS_ONE 0xBF800000u
#define EXT_ZP_FLOAT_MAX 0x7F7FFFFFu // the original's "no distance yet"

// (a.x * b.x + a.y * b.y) + a.z * b.z in the accumulator, the products as
// the original forms them: fs = a, ft = b.
static Ps2F ExtZp_Dot(const Ps2F a[3], const Ps2F b[3])
{
	Ps2Acc acc = Ps2A_Adda(Ps2F_Mul(a[0], b[0]), Ps2F_Mul(a[1], b[1]));
	return Ps2A_Madd(acc, a[2], b[2]);
}

// x*x + y*y + z*z the same way.
static Ps2F ExtZp_Len2(const Ps2F v[3])
{
	return ExtZp_Dot(v, v);
}

// 1 / sqrt(len2) as the original divides one by the root.
static Ps2F ExtZp_InvLen(const Ps2F v[3])
{
	return Ps2F_Div(EXT_ZP_ONE, Ps2F_Sqrt(ExtZp_Len2(v)));
}

int ExtZp_BoxContains(const ExtZpBox *box, const Ps2F p[3])
{
	for (int k = 0; k < 3; k++)
	{
		const Ps2F d = ExtZp_Dot(box->axis[k], p);
		if (Ps2F_Lt(d, box->range[k][0]))
		{
			return 0;
		}
		if (!Ps2F_Le(d, box->range[k][1]))
		{
			return 0;
		}
	}
	return 1;
}

int ExtZp_BoxListFirst(const ExtZpBox *const *list, int count, const Ps2F p[3], int *hit)
{
	for (int i = 0; i < count; i++)
	{
		if (ExtZp_BoxContains(list[i], p))
		{
			if (hit)
			{
				*hit = i;
			}
			return 1;
		}
	}
	return 0;
}

// One axis of the box: axis = unit(pk - p0), range = [axis . p0, axis . pk].
static void ExtZp_BoxAxis(ExtZpBox *box, int k, const Ps2F p0[3], const Ps2F pk[3])
{
	Ps2F *a = box->axis[k];
	a[0] = Ps2F_Sub(pk[0], p0[0]);
	a[1] = Ps2F_Sub(pk[1], p0[1]);
	a[2] = Ps2F_Sub(pk[2], p0[2]);
	const Ps2F inv = ExtZp_InvLen(a);
	a[0] = Ps2F_Mul(a[0], inv);
	a[1] = Ps2F_Mul(a[1], inv);
	a[2] = Ps2F_Mul(a[2], inv);
	box->range[k][0] = ExtZp_Dot(a, p0);
	box->range[k][1] = ExtZp_Dot(a, pk);
}

void ExtZp_BoxBuild(ExtZpBox *box, const Ps2F p0[3], const Ps2F p1[3], const Ps2F p2[3], const Ps2F p3[3])
{
	// the original reads every corner before it writes the box
	Ps2F c0[3] = {p0[0], p0[1], p0[2]}, c1[3] = {p1[0], p1[1], p1[2]}, c2[3] = {p2[0], p2[1], p2[2]}, c3[3] = {p3[0], p3[1], p3[2]};
	ExtZp_BoxAxis(box, 0, c0, c1);
	ExtZp_BoxAxis(box, 1, c0, c2);
	ExtZp_BoxAxis(box, 2, c0, c3);
}

// The squared distance of p to segment a..b of the search: the segment
// parameter u = ((b - a) . (p - a)) / |b - a|^2; u <= 0: |a - p|^2; u >= 1:
// |b - p|^2; else |a + (b - a) u - p|^2.
static Ps2F ExtZp_SegmentDist2(const Ps2F a[3], const Ps2F b[3], const Ps2F p[3])
{
	const Ps2F pa[3] = {Ps2F_Sub(p[0], a[0]), Ps2F_Sub(p[1], a[1]), Ps2F_Sub(p[2], a[2])};
	const Ps2F d[3] = {Ps2F_Sub(b[0], a[0]), Ps2F_Sub(b[1], a[1]), Ps2F_Sub(b[2], a[2])};
	const Ps2F num = ExtZp_Dot(d, pa);
	const Ps2F den = ExtZp_Len2(d);
	const Ps2F u = Ps2F_Div(num, den);
	Ps2F e[3];

	if (Ps2F_Le(u, 0u))
	{
		e[0] = Ps2F_Sub(a[0], p[0]);
		e[1] = Ps2F_Sub(a[1], p[1]);
		e[2] = Ps2F_Sub(a[2], p[2]);
	}
	else if (!Ps2F_Lt(u, EXT_ZP_ONE))
	{
		e[0] = Ps2F_Sub(b[0], p[0]);
		e[1] = Ps2F_Sub(b[1], p[1]);
		e[2] = Ps2F_Sub(b[2], p[2]);
	}
	else
	{
		e[2] = Ps2F_Sub(Ps2F_Add(a[2], Ps2F_Mul(d[2], u)), p[2]);
		const Ps2F y = Ps2F_Add(a[1], Ps2F_Mul(d[1], u));
		const Ps2F x = Ps2F_Add(a[0], Ps2F_Mul(d[0], u));
		e[1] = Ps2F_Sub(y, p[1]);
		e[0] = Ps2F_Sub(x, p[0]);
	}
	return ExtZp_Len2(e);
}

int ExtZp_SplineLocal(const ExtZpSpline *s, int node, const Ps2F p[3])
{
	const int last = s->nodeCount - 2;
	const int hi = (node + 1 < last) ? node + 1 : last;
	int lo = node - 1;
	int best = -1;
	Ps2F bestD = EXT_ZP_FLOAT_MAX;

	if (lo <= 0)
	{
		lo = 0;
	}
	for (int j = lo; j <= hi; j++)
	{
		const Ps2F d = ExtZp_SegmentDist2(s->nodes[j], s->nodes[j + 1], p);
		if (Ps2F_Lt(d, bestD))
		{
			bestD = d;
			best = j;
		}
	}
	return best;
}

int ExtZp_SplineNearest(const ExtZpSpline *s, const Ps2F p[3])
{
	int best = -1;
	Ps2F bestD = EXT_ZP_FLOAT_MAX;

	for (int k = 0; k < s->nodeCount; k++)
	{
		const Ps2F *n = s->nodes[k];
		const Ps2F e[3] = {Ps2F_Sub(n[0], p[0]), Ps2F_Sub(n[1], p[1]), Ps2F_Sub(n[2], p[2])};
		const Ps2F d = ExtZp_Len2(e);
		if (Ps2F_Lt(d, bestD))
		{
			bestD = d;
			best = k;
		}
	}
	return ExtZp_SplineLocal(s, best, p);
}

// out = unit(v) as the original: v stored, x*x... in the accumulator, root,
// one divided by it, each part times that.
static void ExtZp_Unit(Ps2F v[3])
{
	const Ps2F inv = ExtZp_InvLen(v);
	v[0] = Ps2F_Mul(v[0], inv);
	v[1] = Ps2F_Mul(v[1], inv);
	v[2] = Ps2F_Mul(v[2], inv);
}

void ExtZp_SplineDir(const ExtZpSpline *s, int node, const Ps2F p[3], Ps2F out[3])
{
	const Ps2F *a, *b;
	Ps2F c[3];

	if (node == s->nodeCount)
	{
		a = s->nodes[node - 1];
		b = s->nodes[node];
	}
	else
	{
		a = s->nodes[node];
		b = s->nodes[node + 1];
	}
	{
		const Ps2F d[3] = {Ps2F_Sub(b[0], a[0]), Ps2F_Sub(b[1], a[1]), Ps2F_Sub(b[2], a[2])};
		const Ps2F pa[3] = {Ps2F_Sub(p[0], a[0]), Ps2F_Sub(p[1], a[1]), Ps2F_Sub(p[2], a[2])};
		const Ps2F u = Ps2F_Div(ExtZp_Dot(d, pa), ExtZp_Len2(d));

		c[0] = a[0];
		c[1] = a[1];
		c[2] = a[2];
		if (!Ps2F_Le(u, 0u))
		{
			if (Ps2F_Lt(u, EXT_ZP_ONE))
			{
				c[0] = Ps2F_Add(a[0], Ps2F_Mul(d[0], u));
				c[1] = Ps2F_Add(a[1], Ps2F_Mul(d[1], u));
				c[2] = Ps2F_Add(a[2], Ps2F_Mul(d[2], u));
			}
			else
			{
				c[0] = b[0];
				c[1] = b[1];
				c[2] = b[2];
			}
		}
		if (s->type == EXT_ZP_SPLINE_INVPOINT || s->type == EXT_ZP_SPLINE_POINT)
		{
			out[0] = Ps2F_Sub(c[0], p[0]);
			out[1] = Ps2F_Sub(c[1], p[1]);
			out[2] = Ps2F_Sub(c[2], p[2]);
			ExtZp_Unit(out);
			if (s->type == EXT_ZP_SPLINE_INVPOINT)
			{
				out[0] = Ps2F_Mul(out[0], EXT_ZP_MINUS_ONE);
				out[1] = Ps2F_Mul(out[1], EXT_ZP_MINUS_ONE);
				out[2] = Ps2F_Mul(out[2], EXT_ZP_MINUS_ONE);
			}
		}
		else if (s->type == EXT_ZP_SPLINE_PLANE)
		{
			// the directions blended by the unclamped u: (b - a) * u + a
			out[0] = Ps2F_Add(Ps2F_Mul(Ps2F_Sub(b[3], a[3]), u), a[3]);
			out[2] = Ps2F_Add(Ps2F_Mul(Ps2F_Sub(b[5], a[5]), u), a[5]);
			out[1] = Ps2F_Add(Ps2F_Mul(Ps2F_Sub(b[4], a[4]), u), a[4]);
			ExtZp_Unit(out);
		}
		else
		{
			out[0] = 0u;
			out[1] = 0u;
			out[2] = EXT_ZP_ONE;
		}
	}
}

void ExtZp_Cross(Ps2F out[4], const Ps2F a[4], const Ps2F b[4])
{
	Ps2Vec4 va, vb, r;
	Ps2VAcc acc;
	for (int i = 0; i < 4; i++)
	{
		va.v[i] = a[i];
		vb.v[i] = b[i];
	}
	r = va;
	Ps2V_OpMulaAcc(&acc, &va, &vb);
	Ps2V_OpMsubAcc(&r, &acc, &vb, &va);
	for (int i = 0; i < 4; i++)
	{
		out[i] = r.v[i];
	}
}

// ------------------------------------------------------------------ the memory form, by part
#include "native_extzonephys_k0.c"
