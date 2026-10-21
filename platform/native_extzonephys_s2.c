// THE ZONE PORT, PART S2: the contact solver frame and the island builder's
// container helpers (see include/platform/native_extzonephys.h for the
// conventions). Compiles alone too (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_b2.h>
#include <platform/native_extzonephys_s2.h>

// A 32-bit register holds its value sign-extended in a 64-bit register; the
// original's sltu compares those 64-bit values.
#define S2_LTU(a, b) ((uint64_t)(int64_t)(int32_t)(a) < (uint64_t)(int64_t)(int32_t)(b))
#define S2_LT(a, b) ((int32_t)(a) < (int32_t)(b))

#define S2_G_DEFAULT_VALUE 0x00645D58u // the map node's default value word (a global of the original)
#define S2_G_OUT_OBJECT 0x00657754u    // the output object (a global of the original)
#define S2_TEXT_MAP 0x00645D80u        // message text (map too long)
#define S2_TEXT_VEC 0x00645D60u        // message text (vector too long)

// Clears a query record and names the original callee it stands for.
static void s2_qinit(ExtZpQueryArgs *qa, uint32_t func)
{
	uint32_t i;
	for (i = 0; i < 8; i++)
	{
		qa->a[i] = 0u;
		qa->f[i] = 0u;
	}
	qa->func = func;
}

// The two output routines of the original (the "too long" message and the abort).
static void s2_fail(ExtZpCtx *c, uint32_t text)
{
	ExtZpMem *m = c->m;
	ExtZpQueryArgs qa;
	uint32_t v0 = ZmR32(m, S2_G_OUT_OBJECT);
	uint32_t a0 = ZmR32(m, v0 + 0xCu);
	s2_qinit(&qa, 0x00429F38u);
	qa.a[0] = a0;
	qa.a[1] = text;
	(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	s2_qinit(&qa, 0x0042BD70u);
	(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
}

// ------------------------------------------------------------------ tiny helpers

// The contact list of a contact holder: *(holder + 0x14) + 0xF8.
uint32_t ExtZp_Solver_381000(ExtZpCtx *c, uint32_t holder)
{
	return ZmR32(c->m, holder + 0x14u) + 0xF8u;
}

// The map's head node address (builder + 4).
uint32_t ExtZp_Solver_3a8d80(ExtZpCtx *c, uint32_t b)
{
	(void)c;
	return b + 4u;
}

// The address of the map's leftmost-node pointer (map + 0xC).
uint32_t ExtZp_Solver_3a8d90(ExtZpCtx *c, uint32_t m)
{
	(void)c;
	return m + 0xCu;
}

// Empty vector: capacity, count and data cleared; returns v.
uint32_t ExtZp_Solver_3a9010(ExtZpCtx *c, uint32_t v)
{
	ZmW32(c->m, v + 0x0u, 0u);
	ZmW32(c->m, v + 0x4u, 0u);
	ZmW32(c->m, v + 0x8u, 0u);
	return v;
}

// Iterator start: *it = node; returns it.
uint32_t ExtZp_Solver_3a9030(ExtZpCtx *c, uint32_t it, uint32_t node)
{
	ZmW32(c->m, it, node);
	return it;
}

// Same as 3a9010 for the allocator-based vector constructor (the second argument is unused).
uint32_t ExtZp_Solver_345640(ExtZpCtx *c, uint32_t v, uint32_t unused)
{
	(void)unused;
	ZmW32(c->m, v + 0x0u, 0u);
	ZmW32(c->m, v + 0x4u, 0u);
	ZmW32(c->m, v + 0x8u, 0u);
	return v;
}

// Allocator constructor: returns its argument.
uint32_t ExtZp_Solver_345660(ExtZpCtx *c, uint32_t p)
{
	(void)c;
	return p;
}

// Swaps the words at p and q.
void ExtZp_Solver_3a96c0(ExtZpCtx *c, uint32_t p, uint32_t q)
{
	ExtZpMem *m = c->m;
	uint32_t a2 = ZmR32(m, p + 0x0u);
	uint32_t v1 = ZmR32(m, q + 0x0u);
	ZmW32(m, p + 0x0u, v1);
	ZmW32(m, q + 0x0u, a2);
}

// Allocator copy constructor: returns its first argument.
uint32_t ExtZp_Solver_3a96e0(ExtZpCtx *c, uint32_t p, uint32_t q)
{
	(void)c;
	(void)q;
	return p;
}

// Empty map: count 0, root 0, the flag byte copied from *p, leftmost = the head node (m + 4); returns m.
uint32_t ExtZp_Solver_3a9940(ExtZpCtx *c, uint32_t m_, uint32_t p)
{
	ExtZpMem *m = c->m;
	uint32_t v1, a1;
	ZmW32(m, m_ + 0x0u, 0u);
	v1 = m_ + 4u;
	ZmW32(m, m_ + 0x4u, 0u);
	a1 = ZmR8(m, p + 0x0u);
	ZmW8(m, m_ + 0x8u, (uint8_t)a1);
	ZmW32(m, m_ + 0xCu, v1);
	return m_;
}

// Clear a pair vector: the (trivial) element destructors run over the elements, the count becomes 0.
void ExtZp_Solver_3a9960(ExtZpCtx *c, uint32_t v)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, v + 0x4u);
	uint32_t a1 = ZmR32(m, v + 0x8u);
	v1 = v1 << 3;
	v1 = a1 + v1;
	if (S2_LTU(a1, v1))
	{
		do
		{
			v1 = v1 - 8u;
		} while (S2_LTU(a1, v1));
	}
	ZmW32(m, v + 0x4u, 0u);
}

// Destroys a pair vector: clears it, frees its data; for a positive 16-bit flag the object itself is deleted too. Returns v.
uint32_t ExtZp_Solver_3a96f0(ExtZpCtx *c, uint32_t v, uint32_t flag)
{
	ExtZpMem *m = c->m;
	uint32_t a0;
	if (v == 0u)
		return v;
	ExtZp_Solver_3a9960(c, v);
	a0 = ZmR32(m, v + 0x8u);
	if (a0 != 0u)
		ExtZp_Integ_33c170(c, a0);
	if ((int16_t)(flag & 0xFFFFu) > 0)
	{
		ExtZpQueryArgs qa;
		s2_qinit(&qa, 0x004BA240u); // operator delete
		qa.a[0] = v;
		(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	}
	return v;
}

// Union-find object: two empty vectors, at u and u + 0xC; returns u.
uint32_t ExtZp_Solver_3af1f0(ExtZpCtx *c, uint32_t u)
{
	uint32_t sp = ExtZp_Push(c, 0x30u);
	uint32_t s0;
	ZmW32(c->m, sp + 0x24u, u);
	s0 = ZmR32(c->m, sp + 0x24u);
	(void)ExtZp_Solver_345660(c, sp + 0x28u);
	(void)ExtZp_Solver_345640(c, s0, sp + 0x28u);
	s0 = ZmR32(c->m, sp + 0x24u) + 0xCu;
	(void)ExtZp_Solver_345660(c, sp + 0x2Cu);
	(void)ExtZp_Solver_345640(c, s0, sp + 0x2Cu);
	s0 = ZmR32(c->m, sp + 0x24u);
	ExtZp_Pop(c, 0x30u);
	return s0;
}

// In-order successor in the tree: *itp becomes the next node (the head node after the last).
// Node: left +0, right +4, parent | colour bit 0 at +8.
void ExtZp_Solver_33ffd0(ExtZpCtx *c, uint32_t itp)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, itp + 0x0u);
	uint32_t a1 = ZmR32(m, v1 + 0x4u);
	uint32_t a2;
	if (a1 != 0u)
	{
		v1 = ZmR32(m, a1 + 0x0u);
		if (v1 != 0u)
		{
			do
			{
				a1 = v1;
				v1 = ZmR32(m, v1 + 0x0u);
			} while (v1 != 0u);
		}
		ZmW32(m, itp + 0x0u, a1);
		return;
	}
	for (;;)
	{
		a2 = ZmR32(m, itp + 0x0u);
		v1 = 0xFFFFFFFEu;
		a1 = ZmR32(m, a2 + 0x8u);
		a1 = a1 & v1;
		v1 = ZmR32(m, a1 + 0x0u);
		if (a2 != v1)
		{
			ZmW32(m, itp + 0x0u, a1);
			continue;
		}
		ZmW32(m, itp + 0x0u, a1);
		return;
	}
}

// ------------------------------------------------------------------ red-black tree

// Rotate left around x; rootp points at the root variable.
void ExtZp_Solver_340e30(ExtZpCtx *c, uint32_t x, uint32_t rootp)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, rootp + 0x0u);
	uint32_t a3 = ZmR32(m, x + 0x4u); // y
	uint32_t a1, a2;
	if (v1 == x)
		ZmW32(m, rootp + 0x0u, a3);
	v1 = ZmR32(m, a3 + 0x0u);
	ZmW32(m, x + 0x4u, v1);
	a1 = ZmR32(m, a3 + 0x0u);
	if (a1 != 0u)
	{
		v1 = ZmR32(m, a1 + 0x8u);
		v1 = v1 & 1u;
		v1 = x | v1;
		ZmW32(m, a1 + 0x8u, v1);
	}
	v1 = ZmR32(m, a3 + 0x8u);
	a2 = 0xFFFFFFFEu;
	a1 = ZmR32(m, x + 0x8u);
	v1 = v1 & 1u;
	a1 = a1 & a2;
	v1 = a1 | v1;
	ZmW32(m, a3 + 0x8u, v1);
	v1 = ZmR32(m, x + 0x8u);
	a1 = v1 & a2;
	v1 = ZmR32(m, a1 + 0x0u);
	if (x == v1)
		ZmW32(m, a1 + 0x0u, a3);
	else
		ZmW32(m, a1 + 0x4u, a3);
	ZmW32(m, a3 + 0x0u, x);
	v1 = ZmR32(m, x + 0x8u);
	v1 = v1 & 1u;
	v1 = a3 | v1;
	ZmW32(m, x + 0x8u, v1);
}

// Rotate right around x (mirror of 340e30).
void ExtZp_Solver_340ed0(ExtZpCtx *c, uint32_t x, uint32_t rootp)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, rootp + 0x0u);
	uint32_t a3 = ZmR32(m, x + 0x0u); // y
	uint32_t a1, a2;
	if (v1 == x)
		ZmW32(m, rootp + 0x0u, a3);
	v1 = ZmR32(m, a3 + 0x4u);
	ZmW32(m, x + 0x0u, v1);
	a1 = ZmR32(m, a3 + 0x4u);
	if (a1 != 0u)
	{
		v1 = ZmR32(m, a1 + 0x8u);
		v1 = v1 & 1u;
		v1 = x | v1;
		ZmW32(m, a1 + 0x8u, v1);
	}
	v1 = ZmR32(m, a3 + 0x8u);
	a2 = 0xFFFFFFFEu;
	a1 = ZmR32(m, x + 0x8u);
	v1 = v1 & 1u;
	a1 = a1 & a2;
	v1 = a1 | v1;
	ZmW32(m, a3 + 0x8u, v1);
	v1 = ZmR32(m, x + 0x8u);
	a1 = v1 & a2;
	v1 = ZmR32(m, a1 + 0x0u);
	if (x == v1)
		ZmW32(m, a1 + 0x0u, a3);
	else
		ZmW32(m, a1 + 0x4u, a3);
	ZmW32(m, a3 + 0x4u, x);
	v1 = ZmR32(m, x + 0x8u);
	v1 = v1 & 1u;
	v1 = a3 | v1;
	ZmW32(m, x + 0x8u, v1);
}

// Balancing after an insert of node x (made red); root = the root at the time of the call.
// The root variable lives on the stack (its address goes to the rotations).
void ExtZp_Solver_340bf0(ExtZpCtx *c, uint32_t x, uint32_t root)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x30u);
	uint32_t s0 = x, t0, a0, a1, a2, v0, v1, red;
	ZmW32(m, sp + 0x2Cu, root);
	v1 = ZmR32(m, x + 0x8u);
	v1 = v1 | 1u;
	ZmW32(m, x + 0x8u, v1);
	for (;;)
	{
		t0 = ZmR32(m, sp + 0x2Cu);
		if (s0 == t0)
			break;
		a0 = ZmR32(m, s0 + 0x8u);
		v1 = 0xFFFFFFFEu;
		a0 = a0 & v1; // p
		a2 = ZmR32(m, a0 + 0x8u);
		red = ((a2 & 1u) != 0u) ? 1u : 0u;
		a1 = a2 & v1; // g
		if (red != 1u)
			break;
		a2 = ZmR32(m, a1 + 0x0u);
		if (a0 == a2)
		{
			// p is the left child of g: uncle = g.right
			a2 = ZmR32(m, a1 + 0x4u);
			if (a2 != 0u)
			{
				a1 = ZmR32(m, a2 + 0x8u);
				a1 = a1 & 1u;
				red = (a1 != 0u) ? 1u : 0u;
				if (red == 1u)
				{
					a1 = ZmR32(m, a0 + 0x8u);
					a1 = a1 & v1;
					ZmW32(m, a0 + 0x8u, a1);
					a0 = ZmR32(m, a2 + 0x8u);
					a0 = a0 & v1;
					ZmW32(m, a2 + 0x8u, a0);
					a0 = ZmR32(m, s0 + 0x8u);
					a0 = a0 & v1;
					a0 = ZmR32(m, a0 + 0x8u);
					s0 = a0 & v1;
					v1 = ZmR32(m, s0 + 0x8u);
					v1 = v1 | 1u;
					ZmW32(m, s0 + 0x8u, v1);
					continue;
				}
			}
			v0 = ZmR32(m, a0 + 0x4u);
			if (s0 == v0)
			{
				s0 = a0;
				ExtZp_Solver_340e30(c, a0, sp + 0x2Cu);
			}
			v0 = ZmR32(m, s0 + 0x8u);
			a0 = 0xFFFFFFFEu;
			v1 = v0 & a0; // p
			v0 = ZmR32(m, v1 + 0x8u);
			v0 = v0 & a0;
			ZmW32(m, v1 + 0x8u, v0);
			v0 = ZmR32(m, s0 + 0x8u);
			v0 = v0 & a0;
			v0 = ZmR32(m, v0 + 0x8u);
			v1 = v0 & a0; // g
			v0 = ZmR32(m, v1 + 0x8u);
			v0 = v0 | 1u;
			ZmW32(m, v1 + 0x8u, v0);
			v0 = ZmR32(m, s0 + 0x8u);
			v0 = v0 & a0;
			v0 = ZmR32(m, v0 + 0x8u);
			a0 = v0 & a0;
			ExtZp_Solver_340ed0(c, a0, sp + 0x2Cu);
			continue;
		}
		// p is the right child of g: uncle = g.left (a2)
		if (a2 != 0u)
		{
			a1 = ZmR32(m, a2 + 0x8u);
			a1 = a1 & 1u;
			red = (a1 != 0u) ? 1u : 0u;
			if (red == 1u)
			{
				a1 = ZmR32(m, a0 + 0x8u);
				a1 = a1 & v1;
				ZmW32(m, a0 + 0x8u, a1);
				a0 = ZmR32(m, a2 + 0x8u);
				a0 = a0 & v1;
				ZmW32(m, a2 + 0x8u, a0);
				a0 = ZmR32(m, s0 + 0x8u);
				a0 = a0 & v1;
				a0 = ZmR32(m, a0 + 0x8u);
				s0 = a0 & v1;
				v1 = ZmR32(m, s0 + 0x8u);
				v1 = v1 | 1u;
				ZmW32(m, s0 + 0x8u, v1);
				continue;
			}
		}
		v0 = ZmR32(m, a0 + 0x0u);
		if (s0 == v0)
		{
			s0 = a0;
			ExtZp_Solver_340ed0(c, a0, sp + 0x2Cu);
		}
		v0 = ZmR32(m, s0 + 0x8u);
		a0 = 0xFFFFFFFEu;
		v1 = v0 & a0;
		v0 = ZmR32(m, v1 + 0x8u);
		v0 = v0 & a0;
		ZmW32(m, v1 + 0x8u, v0);
		v0 = ZmR32(m, s0 + 0x8u);
		v0 = v0 & a0;
		v0 = ZmR32(m, v0 + 0x8u);
		v1 = v0 & a0;
		v0 = ZmR32(m, v1 + 0x8u);
		v0 = v0 | 1u;
		ZmW32(m, v1 + 0x8u, v0);
		v0 = ZmR32(m, s0 + 0x8u);
		v0 = v0 & a0;
		v0 = ZmR32(m, v0 + 0x8u);
		a0 = v0 & a0;
		ExtZp_Solver_340e30(c, a0, sp + 0x2Cu);
	}
	// the root (reloaded) becomes black
	a0 = ZmR32(m, t0 + 0x8u);
	v1 = 0xFFFFFFFEu;
	v1 = a0 & v1;
	ZmW32(m, t0 + 0x8u, v1);
	ExtZp_Pop(c, 0x30u);
}

// Inserts a node (20 bytes: left, right, parent|colour, key, value) holding *pair under `parent`
// (left = 1: as its left child), counts it, balances; leftmost = 1: it becomes the map's leftmost node.
uint32_t ExtZp_Solver_3a99a0(ExtZpCtx *c, uint32_t m_, uint32_t parent, uint32_t left, uint32_t leftmost, uint32_t pair)
{
	ExtZpMem *m = c->m;
	uint32_t s0, v0, v1, a1;
	v0 = ZmR32(m, m_ + 0x0u);
	if (!((uint64_t)(int64_t)(int32_t)v0 < 0xFFFFFFFFull))
		s2_fail(c, S2_TEXT_MAP);
	s0 = ExtZp_Integ_33c120(c, 0x14u, 0xEu);
	v1 = s0 + 0xCu;
	if (v1 != 0u)
	{
		v0 = ZmR32(m, pair + 0x0u);
		ZmW32(m, v1 + 0x0u, v0); // key
		v0 = ZmR32(m, pair + 0x4u);
		ZmW32(m, v1 + 0x4u, v0); // value
	}
	ZmW32(m, s0 + 0x4u, 0u);
	ZmW32(m, s0 + 0x0u, 0u);
	v0 = ZmR32(m, s0 + 0x8u);
	v0 = v0 & 1u;
	v0 = parent | v0;
	ZmW32(m, s0 + 0x8u, v0);
	if (left != 0u)
		ZmW32(m, parent + 0x0u, s0);
	else
		ZmW32(m, parent + 0x4u, s0);
	v0 = ZmR32(m, m_ + 0x0u);
	v0 = v0 + 1u;
	ZmW32(m, m_ + 0x0u, v0);
	a1 = ZmR32(m, m_ + 0x4u);
	ExtZp_Solver_340bf0(c, s0, a1);
	if (leftmost != 0u)
		ZmW32(m, m_ + 0xCu, s0);
	return s0;
}

// Finds the node of the key *keyp (unsigned order) or inserts one (value from the global default);
// returns the address of the node's key field (the value lies at +4).
uint32_t ExtZp_Solver_3a9210(ExtZpCtx *c, uint32_t m_, uint32_t keyp)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x20u);
	uint32_t a2 = 1u, a3, t0 = 0u, t1, t2, v0, v1, r;
	t2 = ZmR32(m, m_ + 0x4u);
	t1 = m_ + 4u;
	a3 = a2;
	if (t2 != 0u)
	{
		v1 = ZmR32(m, keyp + 0x0u);
		do
		{
			v0 = ZmR32(m, t2 + 0xCu);
			t1 = t2;
			if (S2_LTU(v1, v0))
			{
				t2 = ZmR32(m, t2 + 0x0u);
				a2 = 1u;
			}
			else
			{
				t0 = t2;
				t2 = ZmR32(m, t2 + 0x4u);
				a2 = 0u;
				a3 = 0u;
			}
		} while (t2 != 0u);
	}
	if (t0 != 0u)
	{
		v1 = ZmR32(m, t0 + 0xCu);
		v0 = ZmR32(m, keyp + 0x0u);
		if (!S2_LTU(v1, v0))
		{
			ExtZp_Pop(c, 0x20u);
			return t0 + 0xCu;
		}
	}
	v1 = ZmR32(m, keyp + 0x0u);
	v0 = ZmR32(m, S2_G_DEFAULT_VALUE);
	ZmW32(m, sp + 0x18u, v1);
	ZmW32(m, sp + 0x1Cu, v0);
	r = ExtZp_Solver_3a99a0(c, m_, t1, a2, a3, sp + 0x18u);
	ExtZp_Pop(c, 0x20u);
	return r + 0xCu;
}

// ------------------------------------------------------------------ pair vector insert

// Inserts n copies of the 8-byte element *src before pos into the vector v {cap +0, count +4, data +8}
// (doubling the capacity when it does not fit).
void ExtZp_Solver_3a92c0(ExtZpCtx *c, uint32_t v, uint32_t pos, uint32_t n, uint32_t src)
{
	ExtZpMem *m = c->m;
	uint32_t sp, s0, s1, s6, s3 = n, s4 = pos, s2 = src, s5 = v;
	uint32_t v0, v1, a0, a1, a2, a3, t0, t1;
	int needFail = 0;
	if (s3 == 0u)
		return;
	v1 = 0x1FFFFFFFu;
	if (S2_LTU(v1, s3))
		needFail = 1;
	else
	{
		a0 = ZmR32(m, s5 + 0x4u);
		v1 = v1 - s3;
		if (S2_LTU(v1, a0))
			needFail = 1;
	}
	if (needFail)
		s2_fail(c, S2_TEXT_VEC);
	a1 = ZmR32(m, s5 + 0x4u);
	v1 = ZmR32(m, s5 + 0x0u);
	a0 = a1 + s3;
	if (S2_LTU(v1, a0))
		goto L_realloc;
	// ---- insert without reallocation
	v1 = ZmR32(m, s5 + 0x8u);
	a0 = a1 << 3;
	v1 = v1 + a0; // end
	a0 = v1 - s4;
	if ((int32_t)a0 < 0)
		a0 = a0 + 7u;
	a2 = (uint32_t)((int32_t)a0 >> 3);
	t1 = s3 << 3;
	if (S2_LTU(a2, s3))
	{
		a3 = v1;
		// (the second compare a2 < s3 is true again)
		do
		{
			if (a3 != 0u)
			{
				a0 = ZmR32(m, s2 + 0x0u);
				ZmW32(m, a3 + 0x0u, a0);
				a0 = ZmR32(m, s2 + 0x4u);
				ZmW32(m, a3 + 0x4u, a0);
			}
			a0 = ZmR32(m, s5 + 0x4u);
			s3 = s3 - 1u;
			a3 = a3 + 8u;
			a0 = a0 + 1u;
			ZmW32(m, s5 + 0x4u, a0);
		} while (S2_LTU(a2, s3));
		a2 = s4;
		if (S2_LTU(s4, v1))
		{
			do
			{
				if (a3 != 0u)
				{
					a0 = ZmR32(m, a2 + 0x0u);
					ZmW32(m, a3 + 0x0u, a0);
					a0 = ZmR32(m, a2 + 0x4u);
					ZmW32(m, a3 + 0x4u, a0);
				}
				a1 = ZmR32(m, s5 + 0x4u);
				a2 = a2 + 8u;
				a3 = a3 + 8u;
				a1 = a1 + 1u;
				ZmW32(m, s5 + 0x4u, a1);
			} while (S2_LTU(a2, v1));
		}
		goto L_tail;
	}
	// at least n elements behind pos
	a3 = v1 - t1;
	t0 = v1;
	if (S2_LTU(a3, v1))
	{
		do
		{
			if (t0 != 0u)
			{
				a0 = ZmR32(m, a3 + 0x0u);
				ZmW32(m, t0 + 0x0u, a0);
				a0 = ZmR32(m, a3 + 0x4u);
				ZmW32(m, t0 + 0x4u, a0);
			}
			a1 = ZmR32(m, s5 + 0x4u);
			a3 = a3 + 8u;
			t0 = t0 + 8u;
			a1 = a1 + 1u;
			ZmW32(m, s5 + 0x4u, a1);
		} while (S2_LTU(a3, v1));
	}
	a0 = a2 - s3;
	a1 = a0 << 3;
	a0 = v1 - a1;
	if (!S2_LTU(s2, a0))
	{
		if (S2_LTU(s2, v1))
			s2 = s2 + t1;
	}
	a1 = s4 + a1;
	if (S2_LTU(s4, a1))
	{
		int more;
		do
		{
			a1 = a1 - 8u;
			v1 = v1 - 8u;
			a0 = ZmR32(m, a1 + 0x0u);
			more = S2_LTU(s4, a1);
			ZmW32(m, v1 + 0x0u, a0);
			a0 = ZmR32(m, a1 + 0x4u);
			ZmW32(m, v1 + 0x4u, a0);
		} while (more);
	}
L_tail:
	if (s3 == 0u)
		return;
	do
	{
		v1 = ZmR32(m, s2 + 0x0u);
		s3 = s3 - 1u;
		ZmW32(m, s4 + 0x0u, v1);
		v1 = ZmR32(m, s2 + 0x4u);
		ZmW32(m, s4 + 0x4u, v1);
		s4 = s4 + 8u;
	} while (s3 != 0u);
	return;

L_realloc:
	sp = ExtZp_Push(c, 0x90u);
	s1 = sp + 0x84u;
	s6 = sp + 0x88u;
	(void)ExtZp_Solver_3a96e0(c, sp + 0x80u, s5);
	ZmW32(m, sp + 0x80u, 0u);
	ZmW32(m, s1, 0u);
	ZmW32(m, s6, 0u);
	v0 = ZmR32(m, s5 + 0x4u);
	s0 = ZmR32(m, s5 + 0x0u);
	v1 = v0 + s3;
	if (s0 == 0u)
		s0 = 1u;
	if (S2_LTU(s0, v1))
	{
		do
		{
			if (S2_LTU(s0, 0x0FFFFFFFu))
				s0 = s0 << 1;
			else
				s0 = 0x1FFFFFFFu;
		} while (S2_LTU(s0, v1));
	}
	v0 = ExtZp_Integ_33c120(c, s0 << 3, 0xEu);
	ZmW32(m, s6 + 0x0u, v0);
	ZmW32(m, sp + 0x80u, s0);
	v1 = ZmR32(m, s5 + 0x8u);
	a0 = ZmR32(m, s5 + 0x4u);
	v0 = ZmR32(m, s6 + 0x0u);
	a0 = a0 << 3;
	a2 = v1 + a0;
	if (S2_LTU(v1, s4))
	{
		do
		{
			if (v0 != 0u)
			{
				a0 = ZmR32(m, v1 + 0x0u);
				ZmW32(m, v0 + 0x0u, a0);
				a0 = ZmR32(m, v1 + 0x4u);
				ZmW32(m, v0 + 0x4u, a0);
			}
			a1 = ZmR32(m, s1 + 0x0u);
			v1 = v1 + 8u;
			v0 = v0 + 8u;
			a1 = a1 + 1u;
			ZmW32(m, s1 + 0x0u, a1);
		} while (S2_LTU(v1, s4));
	}
	if (s3 != 0u)
	{
		do
		{
			if (v0 != 0u)
			{
				a0 = ZmR32(m, s2 + 0x0u);
				ZmW32(m, v0 + 0x0u, a0);
				a0 = ZmR32(m, s2 + 0x4u);
				ZmW32(m, v0 + 0x4u, a0);
			}
			a0 = ZmR32(m, s1 + 0x0u);
			s3 = s3 - 1u;
			v0 = v0 + 8u;
			a0 = a0 + 1u;
			ZmW32(m, s1 + 0x0u, a0);
		} while (s3 != 0u);
	}
	if (S2_LTU(v1, a2))
	{
		do
		{
			if (v0 != 0u)
			{
				a0 = ZmR32(m, v1 + 0x0u);
				ZmW32(m, v0 + 0x0u, a0);
				a0 = ZmR32(m, v1 + 0x4u);
				ZmW32(m, v0 + 0x4u, a0);
			}
			a1 = ZmR32(m, s1 + 0x0u);
			v1 = v1 + 8u;
			v0 = v0 + 8u;
			a1 = a1 + 1u;
			ZmW32(m, s1 + 0x0u, a1);
		} while (S2_LTU(v1, a2));
	}
	a0 = sp + 0x80u;
	if (a0 != s5)
	{
		ExtZp_Solver_3a96c0(c, a0, s5);
		v1 = ZmR32(m, s6 + 0x0u);
		v0 = ZmR32(m, s5 + 0x8u);
		ZmW32(m, s6 + 0x0u, v0);
		ZmW32(m, s5 + 0x8u, v1);
		v1 = ZmR32(m, s1 + 0x0u);
		v0 = ZmR32(m, s5 + 0x4u);
		ZmW32(m, s1 + 0x0u, v0);
		ZmW32(m, s5 + 0x4u, v1);
	}
	ExtZp_Solver_3a9960(c, sp + 0x80u);
	a0 = ZmR32(m, s6 + 0x0u);
	if (a0 != 0u)
		ExtZp_Integ_33c170(c, a0);
	ExtZp_Pop(c, 0x90u);
}

// ------------------------------------------------------------------ island builder

// Island builder b (0x18 bytes: list +0, map +4..+0x13, union-find +0x14): every body of every contact gets an
// id (in key order), the contacts become edges between ids (a static body is replaced by the other body), the
// union-find over the edges gives every edge its island. Returns b.
uint32_t ExtZp_Solver_3a8da0(ExtZpCtx *c, uint32_t b, uint32_t list)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x90u);
	uint32_t s0, s1, s2, v0, v1, a0, a1, r;
	ZmW32(m, b + 0x0u, list);
	(void)ExtZp_Solver_3a9940(c, b + 0x4u, sp + 0x8Cu);
	{
		ExtZpQueryArgs qa;
		s2_qinit(&qa, 0x0033C1B0u); // the allocator
		qa.a[0] = 0x1Cu;
		qa.a[1] = 13u;
		v0 = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa).v0;
	}
	if (v0 != 0u)
		v0 = ExtZp_Solver_3af1f0(c, v0);
	ZmW32(m, b + 0x14u, v0);
	// pass 1: every body gets id -1
	v0 = ZmR32(m, b + 0x0u);
	s1 = ZmR32(m, v0 + 0x8u);
	v0 = ZmR32(m, v0 + 0x4u);
	v0 = v0 << 7;
	s0 = s1 + v0;
	if (s1 != s0)
	{
		do
		{
			v1 = ZmR32(m, s1 + 0x8u);
			v1 = ZmR32(m, v1 + 0x18u);
			ZmW32(m, sp + 0x80u, v1);
			v0 = ExtZp_Solver_3a9210(c, b + 0x4u, sp + 0x80u);
			ZmW32(m, v0 + 0x4u, 0xFFFFFFFFu);
			v1 = ZmR32(m, s1 + 0xCu);
			v1 = ZmR32(m, v1 + 0x18u);
			ZmW32(m, sp + 0x7Cu, v1);
			v0 = ExtZp_Solver_3a9210(c, b + 0x4u, sp + 0x7Cu);
			s1 = s1 + 0x80u;
			ZmW32(m, v0 + 0x4u, 0xFFFFFFFFu);
		} while (s1 != s0);
	}
	// pass 2: ids 0.. in key order
	v0 = ExtZp_Solver_3a8d90(c, b + 0x4u);
	a1 = ZmR32(m, v0 + 0x0u);
	(void)ExtZp_Solver_3a9030(c, sp + 0x78u, a1);
	v1 = ZmR32(m, sp + 0x78u);
	ZmW32(m, sp + 0x84u, v1);
	v0 = ExtZp_Solver_3a8d80(c, b + 0x4u);
	a1 = v0;
	(void)ExtZp_Solver_3a9030(c, sp + 0x74u, a1);
	s1 = ZmR32(m, sp + 0x74u);
	s0 = 0u;
	v0 = ZmR32(m, sp + 0x84u);
	while (v0 != s1)
	{
		ZmW32(m, v0 + 0x10u, s0);
		ExtZp_Solver_33ffd0(c, sp + 0x84u);
		s0 = s0 + 1u;
		v0 = ZmR32(m, sp + 0x84u);
	}
	// the edge vector at sp + 0x50
	(void)ExtZp_Solver_3a9010(c, sp + 0x50u);
	v0 = ZmR32(m, b + 0x0u);
	s0 = ZmR32(m, v0 + 0x8u);
	v0 = ZmR32(m, v0 + 0x4u);
	v0 = v0 << 7;
	s1 = s0 + v0;
	if (s0 != s1)
	{
		do
		{
			uint64_t flag;
			v0 = ZmR32(m, s0 + 0x8u);
			a0 = ZmR32(m, v0 + 0x18u);
			ZmW32(m, sp + 0x68u, a0);
			v0 = ZmR32(m, s0 + 0xCu);
			a1 = ZmR32(m, v0 + 0x18u);
			ZmW32(m, sp + 0x6Cu, a1);
			// a body is static when bit 0 or bit 2 of its flag byte (body + 0x18) is set
			flag = (uint64_t)(int64_t)(int8_t)ZmR8(m, a0 + 0x18u);
			if (!(((flag >> 2) & 1u) != 0u || (flag & 1u) != 0u))
			{
				flag = (uint64_t)(int64_t)(int8_t)ZmR8(m, a1 + 0x18u);
				if (((flag >> 2) & 1u) != 0u || (flag & 1u) != 0u)
					ZmW32(m, sp + 0x6Cu, a0);
			}
			else
				ZmW32(m, sp + 0x68u, a1);
			r = ExtZp_Solver_3a9210(c, b + 0x4u, sp + 0x68u);
			s2 = ZmR32(m, r + 0x4u);
			r = ExtZp_Solver_3a9210(c, b + 0x4u, sp + 0x6Cu);
			a1 = ZmR32(m, r + 0x4u);
			v1 = ZmR32(m, sp + 0x54u);
			a0 = ZmR32(m, sp + 0x58u);
			ZmW32(m, sp + 0x64u, a1);
			v1 = v1 << 3;
			ZmW32(m, sp + 0x60u, s2);
			ExtZp_Solver_3a92c0(c, sp + 0x50u, a0 + v1, 1u, sp + 0x60u);
			s0 = s0 + 0x80u;
		} while (s0 != s1);
	}
	v0 = b;
	a1 = ZmR32(m, v0 + 0x4u); // the map's count
	a0 = ZmR32(m, v0 + 0x14u);
	ExtZp_Solver_3af4f0(c, a0, sp + 0x50u, a1);
	(void)ExtZp_Solver_3a96f0(c, sp + 0x50u, 0u);
	ExtZp_Pop(c, 0x90u);
	return b;
}

// ------------------------------------------------------------------ the frame

// Calls the resolver's virtual function at slot +0x1C (a0 = resolver, a1 = vector); type 1 is 0x3AB780.
static void s2_resolve(ExtZpCtx *c, uint32_t res, uint32_t vec)
{
	ExtZpMem *m = c->m;
	uint32_t t9 = ZmR32(m, res + 0x0u);
	t9 = ZmR32(m, t9 + 0x1Cu);
	if (t9 == 0x003AB780u)
		ExtZp_Solver_3ab780(c, res, vec);
	else
		ExtZp_Leave(c, t9);
}

// Numbers the contacts 0..n-1 (field +0x70 of the 0x80-byte contact), the data pointer read from dataWord each
// time (as the original reloads it): eight contacts per pass while more than eight remain, then one by one.
static void s2_number(ExtZpCtx *c, uint32_t dataWord, uint32_t n)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = 0u, a0, a1, a2, a3, k;
	if (!S2_LT(0u, n))
		return;
	if (!S2_LT(n, 9u))
	{
		a0 = n - 8u;
		do
		{
			a2 = ZmR32(m, dataWord);
			a1 = v1 << 7;
			a2 = a2 + a1;
			ZmW32(m, a2 + 0x70u, v1);
			a3 = ZmR32(m, dataWord);
			v1 = v1 + 8u;
			a2 = S2_LT(v1, a0) ? 1u : 0u;
			a3 = a3 + a1;
			ZmW32(m, a3 + 0xF0u, v1 - 8u + 1u);
			for (k = 2; k <= 6; k++)
			{
				a3 = ZmR32(m, dataWord);
				a3 = a3 + a1;
				ZmW32(m, a3 + 0x70u + 0x80u * k, v1 - 8u + k);
			}
			a3 = ZmR32(m, dataWord);
			a1 = a3 + a1;
			ZmW32(m, a1 + 0x3F0u, v1 - 8u + 7u);
		} while (a2 != 0u);
	}
	if (S2_LT(v1, n))
	{
		do
		{
			a0 = ZmR32(m, dataWord);
			a1 = v1 << 7;
			a0 = a0 + a1;
			ZmW32(m, a0 + 0x70u, v1);
			v1 = v1 + 1u;
		} while (S2_LT(v1, n));
	}
}

// The solver frame for the contact list `list` (vector: count +4, data +8) and the step/tolerance h:
// stores h in the resolver (+0x14); flat mode (byte +0x18 == 0) numbers the contacts and calls the resolver
// once; island mode builds the islands and calls the resolver per island with the island's contact copies
// (vector at resolver + 0x1C), then cleans the builder up.
void ExtZp_Solver_3ac5a0(ExtZpCtx *c, uint32_t res, uint32_t list, Ps2F h)
{
	ExtZpMem *m = c->m;
	uint32_t sp, b, v0;
	if (ZmR32(m, list + 0x4u) == 0u)
		return;
	ZmWF(m, res + 0x14u, h);
	v0 = ZmR8(m, res + 0x18u);
	if (v0 == 0u)
	{
		v0 = ZmR32(m, list + 0x4u);
		s2_number(c, list + 0x8u, v0);
		s2_resolve(c, res, list);
		return;
	}
	sp = ExtZp_Push(c, 0x40u);
	b = sp + 0x20u;
	(void)ExtZp_Solver_3a8da0(c, b, list);
	v0 = ExtZp_Solver_3a9120(c, b, res + 0x1Cu);
	while (v0 != 0u)
	{
		v0 = ZmR32(m, res + 0x20u);
		s2_number(c, res + 0x24u, v0);
		s2_resolve(c, res, res + 0x1Cu);
		v0 = ExtZp_Solver_3a9120(c, b, res + 0x1Cu);
	}
	(void)ExtZp_Solver_3a90a0(c, b, 0xFFFFFFFFu);
	ExtZp_Pop(c, 0x40u);
}

// Subspace hook (vtable +0x2C): with a resolver (+0x7C) and a contact holder (+0x70), takes the detector's
// contact list and the tolerance h (the subspace's virtual function at +0x20, 0x3B2600) and runs the frame.
void ExtZp_Solver_3b1960(ExtZpCtx *c, uint32_t sub)
{
	ExtZpMem *m = c->m;
	uint32_t v1, a0, s0, t9;
	Ps2F h = 0u;
	uint32_t sp = 0u;
	v1 = ZmR32(m, sub + 0x7Cu);
	if (v1 == 0u)
		return;
	a0 = ZmR32(m, sub + 0x70u);
	if (a0 == 0u)
		return;
	sp = ExtZp_Push(c, 0x30u); // the original's frame (the island builder below it lives on the stack)
	s0 = ExtZp_Solver_381000(c, a0);
	t9 = ZmR32(m, sub + 0x0u);
	t9 = ZmR32(m, t9 + 0x20u);
	{
		ExtZpQueryArgs qa;
		s2_qinit(&qa, t9); // the subspace's virtual function +0x20 (the detector tolerance): an outside call
		qa.a[0] = sub;
		h = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa).f0;
	}
	a0 = ZmR32(m, sub + 0x7Cu);
	ExtZp_Solver_3ac5a0(c, a0, s0, h);
	ExtZp_Pop(c, 0x30u);
	(void)sp;
}
