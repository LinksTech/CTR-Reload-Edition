// THE ZONE PORT, PENETRATION HANDLING, PART PA: the subspace's handling of a penetration (0x3AFE10) and
// the small list / vector helpers it uses (prefix Pen; conventions: include/platform/native_extzonephys.h).
// Compiles alone too (offline tests); the time search (ExtZp_Pen_3b06d0) and the record vector resize
// (ExtZp_Pen_3b0bb0) are in part pb.

#include <platform/native_extzonephys.h>
#include <platform/native_extzonecontact.h>


// Ports of other parts this one calls (declared here, so that the file compiles alone).
uint32_t ExtZp_Step_345a10(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_345640(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Solver_345660(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_3455e0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Solver_3723e0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Solver_37b400(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_381000(ExtZpCtx *c, uint32_t holder);
uint32_t ExtZp_Solver_13e130(ExtZpCtx *c, uint32_t a0, uint32_t a1);
void ExtZp_Solver_13e0b0(ExtZpCtx *c, uint32_t a0);
void ExtZp_Solver_37ae50(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t ExtZp_Integ_33c120(ExtZpCtx *c, uint32_t size, uint32_t tag);
void ExtZp_Integ_33c170(ExtZpCtx *c, uint32_t block);
void ExtZp_Integ_33f800(ExtZpCtx *c, uint32_t vec, uint32_t pos, uint32_t n, uint32_t valp);
// Part pb.
void ExtZp_Pen_3b06d0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
void ExtZp_Pen_3b0bb0(ExtZpCtx *c, uint32_t a0, uint32_t a1);

#define PEN_G_ERRCTX 0x00657754u      // pointer to the object whose +0xC is the output stream of the error routine (global of the original)
#define PEN_TXT_PAIR_INSERT 0x00645950u // error text address (pair vector insert too large)
#define PEN_TXT_PAIR_ASSIGN 0x00645630u // error text address (pair vector assign too large)
#define PEN_TXT_WORD_COPY 0x00645BF0u   // error text address (word vector copy too large)
#define PEN_SLOT_DESTRUCTOR 8u          // slot +8 of a record's table: the destructor
#define PEN_DESTRUCTOR_13E130 0x0013E130u

// A call the port does not compute: the original's function address and its t0 / a0..a3.
static uint32_t Pen_Call(ExtZpCtx *c, int kind, uint32_t func, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0)
{
	ExtZpQueryArgs args;
	ExtZpQueryResult r;
	int i;
	for (i = 0; i < 8; i++)
	{
		args.a[i] = 0;
		args.f[i] = 0;
	}
	args.a[0] = a0;
	args.a[1] = a1;
	args.a[2] = a2;
	args.a[3] = a3;
	args.a[4] = t0;
	args.func = func;
	r = ExtZp_Query(c, kind, &args);
	return r.v0;
}
static uint32_t Pen_Other(ExtZpCtx *c, uint32_t func, uint32_t a0, uint32_t a1)
{
	return Pen_Call(c, EXT_ZP_Q_OTHER, func, a0, a1, 0, 0, 0);
}

// The output routine pair the vector code calls for a size that is too large: print text, then the follow-up.
static void Pen_TooLarge(ExtZpCtx *c, uint32_t text)
{
	uint32_t ctx = ZmR32(c->m, PEN_G_ERRCTX);
	uint32_t stream = ZmR32(c->m, ctx + 0xCu);
	Pen_Other(c, 0x00429F38u, stream, text);
	Pen_Other(c, 0x0042BD70u, 0, 0);
}

// The virtual call "element->table[+8](element, -1)": the destructor of a record.
static void Pen_DestroyElem(ExtZpCtx *c, uint32_t elem)
{
	ExtZpMem *m = c->m;
	uint32_t t9 = ZmR32(m, elem + 0u);   // table
	t9 = ZmR32(m, t9 + PEN_SLOT_DESTRUCTOR); // slot +8
	if (t9 == PEN_DESTRUCTOR_13E130)
	{
		(void)ExtZp_Solver_13e130(c, elem, 0xFFFFFFFFu);
	}
	else
	{
		ExtZp_Leave(c, t9);
	}
}

// ------------------------------------------------------------------ tiny helpers

// Destroys a record vector (all elements from the back, then the data block is freed); a1 > 0 (low 16 bits, signed) frees the object itself.
// Returns a0 (the object).
uint32_t ExtZp_Pen_13dfd0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	if (a0 != 0u)
	{
		uint32_t dataPtr;
		ExtZp_Solver_13e0b0(c, a0);
		dataPtr = ZmR32(m, a0 + 8u);
		if (dataPtr != 0u)
		{
			ExtZp_Integ_33c170(c, dataPtr);
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			Pen_Other(c, 0x004BA240u, a0, 0);
		}
	}
	return a0;
}

// The same for a vector of plain words: the size is set to 0 (13e1a0), the data block freed. Returns a0.
uint32_t ExtZp_Pen_13e040(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	if (a0 != 0u)
	{
		uint32_t dataPtr;
		ExtZp_Pen_13e1a0(c, a0);
		dataPtr = ZmR32(m, a0 + 8u);
		if (dataPtr != 0u)
		{
			ExtZp_Integ_33c170(c, dataPtr);
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			Pen_Other(c, 0x004BA240u, a0, 0);
		}
	}
	return a0;
}

// Sets a vector's size (+4) to 0.
void ExtZp_Pen_13e1a0(ExtZpCtx *c, uint32_t a0)
{
	ZmW32(c->m, a0 + 4u, 0u);
}

// The end of a word vector: data + 4 * size.
uint32_t ExtZp_Pen_345cc0(ExtZpCtx *c, uint32_t a0)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, a0 + 4u);
	uint32_t v0 = ZmR32(m, a0 + 8u);
	return v0 + (v1 << 2);
}

// The address of a vector head's data slot: a0 + 8.
uint32_t ExtZp_Pen_359210(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0 + 8u;
}

// Empties a vector head (capacity, size, data = 0); returns a0 (a1 unused).
uint32_t ExtZp_Pen_359220(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	(void)a1;
	ZmW32(m, a0 + 0u, 0u);
	ZmW32(m, a0 + 4u, 0u);
	ZmW32(m, a0 + 8u, 0u);
	return a0;
}

// Swaps the words at a0 and a1.
void ExtZp_Pen_3678b0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t a2 = ZmR32(m, a0 + 0u);
	uint32_t v1 = ZmR32(m, a1 + 0u);
	ZmW32(m, a0 + 0u, v1);
	ZmW32(m, a1 + 0u, a2);
}

// Returns a0 (an allocator constructor).
uint32_t ExtZp_Pen_3678d0(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0;
}

// Empties a vector head (same as 359220); returns a0 (a1 unused).
uint32_t ExtZp_Pen_36e250(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	(void)a1;
	ZmW32(m, a0 + 0u, 0u);
	ZmW32(m, a0 + 4u, 0u);
	ZmW32(m, a0 + 8u, 0u);
	return a0;
}

// Returns a0 (a hook that runs after a vector shrank).
uint32_t ExtZp_Pen_370820(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0;
}

// The detector's pair list: *(a0 + 0x14) + 0xEC.
uint32_t ExtZp_Pen_381010(ExtZpCtx *c, uint32_t a0)
{
	return ZmR32(c->m, a0 + 0x14u) + 0xECu;
}

// The detector's list of pairs in contact: *(a0 + 0x14) + 0xC8.
uint32_t ExtZp_Pen_381020(ExtZpCtx *c, uint32_t a0)
{
	return ZmR32(c->m, a0 + 0x14u) + 0xC8u;
}

// ------------------------------------------------------------------ vectors of 8-byte pairs

// Assigns the range [a1, a2) of 8-byte pairs to the pair vector a0 (a0 = {capacity +0, size +4, data +8}): when the
// capacity suffices the data is overwritten and the size set, otherwise the data is replaced by a new block.
void ExtZp_Pen_359260(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t s2 = a0, s1 = a1, s0 = a2;
	uint32_t d, n, v1, at, cnt, p, q, e, sz;
	d = s0 - s1;
	if ((int32_t)d >= 0)
		n = (uint32_t)((int32_t)d >> 3);
	else
		n = (uint32_t)((int32_t)(d + 7u) >> 3);
	// n is kept in the stack slot sp + 0x4c in the original (not passed to a callee)
	v1 = ZmR32(m, s2 + 0u); // capacity
	if (v1 < n)
		goto grow;
	v1 = ZmR32(m, s2 + 4u); // size
	if (v1 < n)
		cnt = v1; // copies min(size, n) pairs over the old ones
	else
		cnt = n;
	p = ZmR32(m, s2 + 8u);
	e = p + (cnt << 3);
	if (p < e)
	{
		do
		{
			ZmW32(m, p + 0u, ZmR32(m, s1 + 0u));
			ZmW32(m, p + 4u, ZmR32(m, s1 + 4u));
			p += 8u;
			s1 += 8u;
		} while (p < e);
	}
	sz = ZmR32(m, s2 + 4u);
	if (n < sz)
	{
		// shrinks: the surplus pairs are dropped (nothing to destroy); the size becomes n
		q = ZmR32(m, s2 + 8u) + (sz << 3);
		if (p < q)
		{
			do
			{
				p += 8u;
			} while (p < q);
		}
		ZmW32(m, s2 + 4u, n);
		return;
	}
	if (sz < n)
	{
		if (s1 == s0)
			return;
		do
		{
			if (p != 0u)
			{
				ZmW32(m, p + 0u, ZmR32(m, s1 + 0u));
				ZmW32(m, p + 4u, ZmR32(m, s1 + 4u));
			}
			v1 = ZmR32(m, s2 + 4u);
			s1 += 8u;
			p += 8u;
			v1 += 1u;
			ZmW32(m, s2 + 4u, v1);
		} while (s1 != s0);
	}
	return;

grow:
	at = (n < 0x20000000u);
	if (!at)
		Pen_TooLarge(c, PEN_TXT_PAIR_ASSIGN);
	ExtZp_Pen_13e1a0(c, s2);
	p = ZmR32(m, s2 + 8u);
	if (p != 0u)
	{
		ExtZp_Integ_33c170(c, p);
		ZmW32(m, s2 + 8u, 0u);
		ZmW32(m, s2 + 0u, 0u);
	}
	p = ExtZp_Integ_33c120(c, n << 3, 14u);
	ZmW32(m, s2 + 8u, p);
	ZmW32(m, s2 + 0u, n);
	if (s1 == s0)
		return;
	p = ZmR32(m, s2 + 8u);
	do
	{
		if (p != 0u)
		{
			ZmW32(m, p + 0u, ZmR32(m, s1 + 0u));
			ZmW32(m, p + 4u, ZmR32(m, s1 + 4u));
		}
		v1 = ZmR32(m, s2 + 4u);
		s1 += 8u;
		p += 8u;
		v1 += 1u;
		ZmW32(m, s2 + 4u, v1);
	} while (s1 != s0);
}

// Inserts a2 copies of the pair at a3 at the place a1 (an address inside the data) of the pair vector a0. When the capacity
// is too small the elements move to a new block (capacity doubled until it holds the new size). Nothing is returned.
void ExtZp_Pen_3674c0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	uint32_t s5 = a0, s4 = a1, s3 = a2, s2 = a3;
	uint32_t s0, v0, v1, at, t0, t1, d, cnt, size, cap;
	uint32_t p, q, e;
	if (s3 == 0u)
		return;
	v1 = 0x1FFFFFFFu;
	if (v1 < s3)
		goto err;
	size = ZmR32(m, s5 + 4u);
	v1 -= s3;
	if (!(v1 < size))
		goto ok;
err:
	Pen_TooLarge(c, PEN_TXT_PAIR_INSERT);
ok:
	size = ZmR32(m, s5 + 4u);
	cap = ZmR32(m, s5 + 0u);
	if (cap < size + s3)
		goto grow;
	// in place: enough capacity
	v1 = ZmR32(m, s5 + 8u) + (size << 3); // end of the data
	d = v1 - s4;
	if ((int32_t)d >= 0)
		cnt = (uint32_t)((int32_t)d >> 3);
	else
		cnt = (uint32_t)((int32_t)(d + 7u) >> 3);
	t1 = s3 << 3;
	if (!(cnt < s3))
		goto tail;
	// fewer elements behind the place than copies: the surplus copies of the value go to the end first
	p = v1;
	do
	{
		if (p != 0u)
		{
			ZmW32(m, p + 0u, ZmR32(m, s2 + 0u));
			ZmW32(m, p + 4u, ZmR32(m, s2 + 4u));
		}
		size = ZmR32(m, s5 + 4u);
		s3 -= 1u;
		p += 8u;
		ZmW32(m, s5 + 4u, size + 1u);
	} while (cnt < s3);
	if (s4 < v1)
	{
		// then the elements behind the place move behind the copies
		q = s4;
		do
		{
			if (p != 0u)
			{
				ZmW32(m, p + 0u, ZmR32(m, q + 0u));
				ZmW32(m, p + 4u, ZmR32(m, q + 4u));
			}
			size = ZmR32(m, s5 + 4u);
			q += 8u;
			p += 8u;
			ZmW32(m, s5 + 4u, size + 1u);
		} while (q < v1);
	}
	goto fill;

tail:
	// at least as many elements behind the place as copies: the last copies-worth of elements move to the end
	q = v1 - t1;
	if (q < v1)
	{
		t0 = v1;
		do
		{
			if (t0 != 0u)
			{
				ZmW32(m, t0 + 0u, ZmR32(m, q + 0u));
				ZmW32(m, t0 + 4u, ZmR32(m, q + 4u));
			}
			size = ZmR32(m, s5 + 4u);
			q += 8u;
			t0 += 8u;
			ZmW32(m, s5 + 4u, size + 1u);
		} while (q < v1);
	}
	t0 = (cnt - s3) << 3; // a1 of the original
	e = v1 - t0;          // a0 of the original
	if (!(s2 < e))
	{
		if (s2 < v1)
			s2 += t1; // the value lies in the moved part: follow it
	}
	q = s4 + t0; // a1: the end of the part that moves up
	if (s4 < q)
	{
		do
		{
			q -= 8u;
			v1 -= 8u;
			ZmW32(m, v1 + 0u, ZmR32(m, q + 0u));
			at = (s4 < q);
			ZmW32(m, v1 + 4u, ZmR32(m, q + 4u));
		} while (at);
	}
fill:
	if (s3 == 0u)
		return;
	do
	{
		ZmW32(m, s4 + 0u, ZmR32(m, s2 + 0u));
		s3 -= 1u;
		ZmW32(m, s4 + 4u, ZmR32(m, s2 + 4u));
		s4 += 8u;
	} while (s3 != 0u);
	return;

grow:
	{
		// a new block: the elements before the place, n copies, the elements after it; then swapped in, the old data freed
		uint32_t tmp = ExtZp_Push(c, 0x10u); // temporary vector head: capacity +0, size +4, data +8
		uint32_t newSize, end, tdata;
		(void)ExtZp_Pen_3678d0(c, tmp);
		ZmW32(m, tmp + 0u, 0u);
		ZmW32(m, tmp + 4u, 0u);
		ZmW32(m, tmp + 8u, 0u);
		size = ZmR32(m, s5 + 4u);
		s0 = ZmR32(m, s5 + 0u);
		newSize = size + s3;
		if (s0 == 0u)
			s0 = 1u;
		while (s0 < newSize)
		{
			if (s0 < 0x0FFFFFFFu)
				s0 <<= 1;
			else
				s0 = 0x1FFFFFFFu;
		}
		v0 = ExtZp_Integ_33c120(c, s0 << 3, 0xEu);
		ZmW32(m, tmp + 8u, v0);
		ZmW32(m, tmp + 0u, s0);
		v1 = ZmR32(m, s5 + 8u);
		size = ZmR32(m, s5 + 4u);
		v0 = ZmR32(m, tmp + 8u);
		end = v1 + (size << 3);
		if (v1 < s4)
		{
			do
			{
				if (v0 != 0u)
				{
					ZmW32(m, v0 + 0u, ZmR32(m, v1 + 0u));
					ZmW32(m, v0 + 4u, ZmR32(m, v1 + 4u));
				}
				ZmW32(m, tmp + 4u, ZmR32(m, tmp + 4u) + 1u);
				v1 += 8u;
				v0 += 8u;
			} while (v1 < s4);
		}
		if (s3 != 0u)
		{
			do
			{
				if (v0 != 0u)
				{
					ZmW32(m, v0 + 0u, ZmR32(m, s2 + 0u));
					ZmW32(m, v0 + 4u, ZmR32(m, s2 + 4u));
				}
				ZmW32(m, tmp + 4u, ZmR32(m, tmp + 4u) + 1u);
				s3 -= 1u;
				v0 += 8u;
			} while (s3 != 0u);
		}
		if (v1 < end)
		{
			do
			{
				if (v0 != 0u)
				{
					ZmW32(m, v0 + 0u, ZmR32(m, v1 + 0u));
					ZmW32(m, v0 + 4u, ZmR32(m, v1 + 4u));
				}
				ZmW32(m, tmp + 4u, ZmR32(m, tmp + 4u) + 1u);
				v1 += 8u;
				v0 += 8u;
			} while (v1 < end);
		}
		if (tmp != s5)
		{
			ExtZp_Pen_3678b0(c, tmp, s5); // capacity
			tdata = ZmR32(m, tmp + 8u);
			v1 = ZmR32(m, s5 + 8u);
			ZmW32(m, tmp + 8u, v1);
			ZmW32(m, s5 + 8u, tdata);
			newSize = ZmR32(m, tmp + 4u);
			v1 = ZmR32(m, s5 + 4u);
			ZmW32(m, tmp + 4u, v1);
			ZmW32(m, s5 + 4u, newSize);
		}
		(void)ExtZp_Pen_13e040(c, tmp, 0xFFFFFFFFu);
		ExtZp_Pop(c, 0x10u);
	}
}

// Removes the pairs [a1, a2) of the pair vector a0 (the pairs behind move down); returns a1.
uint32_t ExtZp_Pen_37aa80(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t v0, v1, t0, t1, a3, d;
	if (a1 == a2)
		return a1;
	v1 = ZmR32(m, a0 + 4u);
	v0 = ZmR32(m, a0 + 8u);
	t1 = v0 + (v1 << 3); // end of the data
	d = t1 - a2;
	if ((int32_t)d >= 0)
		v1 = (uint32_t)((int32_t)d >> 3);
	else
		v1 = (uint32_t)((int32_t)(d + 7u) >> 3);
	if (v1 != 0u && a2 < t1)
	{
		t0 = a1;
		a3 = a2;
		do
		{
			ZmW32(m, t0 + 0u, ZmR32(m, a3 + 0u));
			ZmW32(m, t0 + 4u, ZmR32(m, a3 + 4u));
			a3 += 8u;
			t0 += 8u;
		} while (a3 < t1);
	}
	// (a loop over the removed tail that destroys nothing, no memory effect)
	d = a2 - a1;
	if ((int32_t)d >= 0)
		a2 = (uint32_t)((int32_t)d >> 3);
	else
		a2 = (uint32_t)((int32_t)(d + 7u) >> 3);
	v1 = ZmR32(m, a0 + 4u);
	v1 -= a2;
	ZmW32(m, a0 + 4u, v1);
	return a1;
}

// Sets the size of the pair vector a0 to a1: larger = appends copies of a pair (0, 0), smaller = removes the pairs behind a1.
void ExtZp_Pen_379bc0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, a0 + 4u);
	if (v1 < a1)
	{
		uint32_t tmp = ExtZp_Push(c, 0x10u); // the pair (0, 0) the copies are made of (sp + 0x18 / 0x1c)
		uint32_t a2, v0;
		ZmW32(m, tmp + 0u, 0u);
		ZmW32(m, tmp + 4u, 0u);
		a2 = ZmR32(m, a0 + 4u);
		v0 = ZmR32(m, a0 + 8u);
		v1 = a2 << 3;
		a2 = a1 - a2;
		ExtZp_Pen_3674c0(c, a0, v0 + v1, a2, tmp);
		ExtZp_Pop(c, 0x10u);
	}
	else if (a1 < v1)
	{
		uint32_t a2 = ZmR32(m, a0 + 8u);
		uint32_t v0 = v1 << 3;
		v1 = a1 << 3;
		(void)ExtZp_Pen_37aa80(c, a0, a2 + v1, a2 + v0);
	}
}

// ------------------------------------------------------------------ vectors of words

// Builds the word vector a0 as a copy of the word vector a1: zero head, then the range copy 37f0f0. Returns a0.
uint32_t ExtZp_Pen_37e3f0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t v0, v1;
	ZmW32(m, a0 + 0u, 0u);
	ZmW32(m, a0 + 4u, 0u);
	ZmW32(m, a0 + 8u, 0u);
	v0 = ZmR32(m, a1 + 4u);
	v1 = ZmR32(m, a1 + 8u);
	ExtZp_Pen_37f0f0(c, a0, v1, v1 + (v0 << 2));
	return a0;
}

// Fills the empty word vector a0 with the words [a1, a2): capacity = size = the count, a new block, copies.
void ExtZp_Pen_37f0f0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t d = a2 - a1, v1, blk, p;
	if ((int32_t)d >= 0)
		v1 = (uint32_t)((int32_t)d >> 2);
	else
		v1 = (uint32_t)((int32_t)(d + 3u) >> 2);
	ZmW32(m, a0 + 0u, v1);
	ZmW32(m, a0 + 4u, v1);
	v1 = ZmR32(m, a0 + 4u);
	if (!(v1 < 0x40000000u))
		Pen_TooLarge(c, PEN_TXT_WORD_COPY);
	v1 = ZmR32(m, a0 + 4u);
	if (v1 == 0u)
		return;
	blk = ExtZp_Integ_33c120(c, v1 << 2, 14u);
	ZmW32(m, a0 + 8u, blk);
	if (!(a1 < a2))
		return;
	p = ZmR32(m, a0 + 8u);
	do
	{
		v1 = ZmR32(m, a1 + 0u);
		ZmW32(m, p + 0u, v1);
		a1 += 4u;
		p += 4u;
	} while (a1 < a2);
}

// ------------------------------------------------------------------ the penetration handling

// Whether the body (a rigid body address) is static for the handling: flag byte +0x18, bit 2 or bit 0.
static int Pen_IsStatic(ExtZpCtx *c, uint32_t body)
{
	int32_t f = (int8_t)ZmR8(c->m, body + 0x18u);
	return ((f & 4) != 0) || ((f & 1) != 0);
}

// A word is in the word vector v (linear search).
static int Pen_Contains(ExtZpCtx *c, uint32_t v, uint32_t word)
{
	ExtZpMem *m = c->m;
	uint32_t p = ZmR32(m, v + 8u);
	uint32_t e = p + (ZmR32(m, v + 4u) << 2);
	for (; p != e; p += 4u)
	{
		if (ZmR32(m, p) == word)
			return 1;
	}
	return 0;
}

// Adds the body of the shell at *shellSlot (shell + 0x18) to the body set V1 unless static or already in it.
// tmp = the stack word the original keeps the body in (its address is handed to the vector insert).
static void Pen_AddBody(ExtZpCtx *c, uint32_t v1set, uint32_t shell, uint32_t tmp)
{
	ExtZpMem *m = c->m;
	uint32_t body = ZmR32(m, shell + 0x18u);
	uint32_t end;
	ZmW32(m, tmp, body);
	if (Pen_IsStatic(c, body))
		return;
	if (Pen_Contains(c, v1set, body))
		return;
	end = ExtZp_Pen_345cc0(c, v1set);
	ExtZp_Integ_33f800(c, v1set, end, 1u, tmp);
}

// The pair a (shell A, shell B) names a body of the set (a word vector at set): either of the two bodies.
static int Pen_PairTouches(ExtZpCtx *c, uint32_t set, uint32_t wrapA, uint32_t wrapB)
{
	ExtZpMem *m = c->m;
	if (Pen_Contains(c, set, ZmR32(m, wrapA + 0x18u)))
		return 1;
	return Pen_Contains(c, set, ZmR32(m, wrapB + 0x18u)) != 0;
}

// The subspace's handling of a penetration (called after the detection found overlapping pairs): every pair goes to the
// time search, then the detection runs again for the bodies it moved, until no new pair is found; then the contacts and
// pairs of all affected bodies are replaced by the new ones. a0 = subspace. The locals of the original's frame (V1 = bodies of
// the current round, V2 = all affected bodies, C1 = new records, C2 = pairs, C3 = new pairs) are at the same offsets in the
// port's stack frame.
void ExtZp_Pen_3afe10(ExtZpCtx *c, uint32_t a0)
{
	ExtZpMem *m = c->m;
	const uint32_t SUB = a0;
	uint32_t sp = ExtZp_Push(c, 0xF0u);
	const uint32_t V1 = sp + 0x80u, V2 = sp + 0x90u, C1 = sp + 0xA0u, C2 = sp + 0xB0u, C3 = sp + 0xC0u;
	uint32_t DET, s0, s1, s2, s3, s6, v0, v1, a1, a2, t0, e;
	DET = ZmR32(m, SUB + 0x70u);
	s0 = ExtZp_Pen_381010(c, DET); // the pair list P + 0xEC
	(void)ExtZp_Solver_345660(c, sp + 0xE8u);
	(void)ExtZp_Solver_345640(c, V1, sp + 0xE8u); // V1 = an empty body set

	// ---- phase 1: V1 = the non-static bodies of the pairs, each once
	v0 = ZmR32(m, s0 + 4u);
	s2 = ZmR32(m, s0 + 8u);
	s1 = s2 + (v0 << 3);
	if (s2 != s1)
	{
		do
		{
			Pen_AddBody(c, V1, ZmR32(m, s2 + 0u), sp + 0xDCu);
			Pen_AddBody(c, V1, ZmR32(m, s2 + 4u), sp + 0xD8u);
			s2 += 8u;
		} while (s2 != s1);
	}
	(void)ExtZp_Pen_37e3f0(c, V2, V1); // V2 = a copy of V1
	(void)ExtZp_Pen_36e250(c, C1, sp + 0xCCu);
	s6 = 0u; // the round counter
	(void)ExtZp_Pen_359220(c, C2, sp + 0xD0u);
	goto L_3b0394;

	// ---- phase 2: as long as the pair list is not empty
L_3affd0:
	v0 = ZmR32(m, s0 + 4u);
	s2 = ZmR32(m, s0 + 8u);
	s1 = s2 + (v0 << 3);
	s6 += 1u;
	if (s2 != s1)
	{
		do
		{
			ExtZp_Pen_3b06d0(c, SUB, s2); // the time search of the pair
			s2 += 8u;
		} while (s2 != s1);
	}
	(void)ExtZp_Pen_359220(c, C3, sp + 0xD4u);
	ExtZp_Pen_3b0bb0(c, C1, 0u); // C1 emptied
	ExtZp_Pen_379bc0(c, C2, 0u); // C2 emptied
	DET = ZmR32(m, SUB + 0x70u);
	(void)Pen_Call(c, EXT_ZP_Q_COLLIDE, 0x00380FA0u, DET, V1, C3, C1, C2); // the detection for the bodies of V1
	DET = ZmR32(m, SUB + 0x70u);
	v0 = ZmR32(m, DET + 0x14u); // P
	s3 = sp + 0xC4u;            // &C3.count
	s1 = ZmR32(m, sp + 0xC8u);  // C3.data
	{
		const uint32_t known = v0 + 0xB0u; // the list of known pairs
		goto L_3b0114;
		// ---- C3 filtered: pairs that already are in P + 0xB0 (in either order) are removed
	L_3b0054:
		{
			uint32_t kc = ZmR32(m, known + 4u);
			uint32_t kd = ZmR32(m, known + 8u);
			uint32_t kend = kd + (kc << 3);
			uint32_t K0, K1, N0, N1;
			for (;;)
			{
				if (kd == kend)
					break;
				K0 = ZmR32(m, kd + 0u);
				N0 = ZmR32(m, s1 + 0u);
				K1 = ZmR32(m, kd + 4u);
				N1 = ZmR32(m, s1 + 4u);
				if ((K0 == N0 && K1 == N1) || (K1 == N0 && K0 == N1))
					break;
				kd += 8u;
			}
			if (kd == kend)
				goto L_3b010c; // not found: keep
			// found: the last pair overwrites this one, C3.count - 1
			v0 = ZmR32(m, t0 - 8u);
			ZmW32(m, s1 + 0u, v0);
			v0 = ZmR32(m, t0 - 4u);
			ZmW32(m, s1 + 4u, v0);
			v0 = ZmR32(m, s3 + 0u);
			v0 -= 1u;
			ZmW32(m, s3 + 0u, v0); // (the delay slot of the hook call stores it)
			(void)ExtZp_Pen_370820(c, C3);
			goto L_3b0114;
		}
	L_3b010c:
		s1 += 8u;
	L_3b0114:
		a1 = ZmR32(m, s3 + 0u);       // C3.count
		v0 = ZmR32(m, sp + 0xC8u);    // C3.data
		t0 = v0 + (a1 << 3);          // end
		if (s1 != t0)
			goto L_3b0054;
	}
	if (a1 == 0u)
		goto L_3b0374; // no new pairs: the pair list is emptied

	// ---- pair list P + 0xEC := C3
	v0 = ExtZp_Pen_359210(c, C3);
	v1 = ZmR32(m, s3);
	v0 = ZmR32(m, v0);
	s1 = v0 + (v1 << 3);
	v0 = ExtZp_Pen_359210(c, C3);
	a1 = ZmR32(m, v0);
	ExtZp_Pen_359260(c, s0, a1, s1);

	// ---- V1 rebuilt over the new pair list
	ZmW32(m, sp + 0x84u, 0u);
	v0 = ZmR32(m, s0 + 4u);
	s3 = ZmR32(m, s0 + 8u);
	s2 = s3 + (v0 << 3);
	if (s3 != s2)
	{
		do
		{
			Pen_AddBody(c, V1, ZmR32(m, s3 + 0u), sp + 0xE4u);
			Pen_AddBody(c, V1, ZmR32(m, s3 + 4u), sp + 0xE0u);
			s3 += 8u;
		} while (s3 != s2);
	}

	// ---- V2 := V2 united with V1
	v0 = ExtZp_Step_345a10(c, V1);
	s2 = ZmR32(m, v0);
	v0 = ExtZp_Step_345a10(c, V1);
	v1 = ZmR32(m, sp + 0x84u);
	v0 = ZmR32(m, v0);
	s1 = v0 + (v1 << 2);
	if (s2 != s1)
	{
		do
		{
			if (!Pen_Contains(c, V2, ZmR32(m, s2)))
			{
				e = ExtZp_Pen_345cc0(c, V2);
				ExtZp_Integ_33f800(c, V2, e, 1u, s2);
			}
			s2 += 4u;
		} while (s2 != s1);
	}
	goto L_3b0384;

L_3b0374:
	ExtZp_Pen_379bc0(c, s0, 0u); // pair list emptied
L_3b0384:
	(void)ExtZp_Pen_13e040(c, C3, 0u);
L_3b0394:
	v0 = ZmR32(m, s0 + 4u);
	if (v0 != 0u)
		goto L_3affd0;
	if (s6 >= 2u)
	{
		// ---- the closing detection over all affected bodies (V2) when there were at least two rounds
		ExtZp_Pen_3b0bb0(c, C1, 0u);
		DET = ZmR32(m, SUB + 0x70u);
		(void)Pen_Call(c, EXT_ZP_Q_COLLIDE, 0x00380FA0u, DET, V2, s0, C1, C2);
	}

	// ---- records that touch a body of V2 are removed from the record list P + 0xF8
	DET = ZmR32(m, SUB + 0x70u);
	v0 = ExtZp_Solver_381000(c, DET);
	s1 = ZmR32(m, v0 + 8u);
	s0 = v0;
	for (;;)
	{
		uint32_t last;
		v1 = ZmR32(m, s0 + 4u);
		v0 = ZmR32(m, s0 + 8u);
		a2 = v0 + (v1 << 7); // end
		if (s1 == a2)
			break;
		if (!Pen_PairTouches(c, V2, ZmR32(m, s1 + 8u), ZmR32(m, s1 + 0xCu)))
		{
			s1 += 0x80u;
			continue;
		}
		// removed: replaced by the LAST record, which is then destroyed (the order changes)
		last = a2 - 0x80u;
		v0 = ZmR32(m, a2 - 0x7Cu);
		ZmW32(m, s1 + 0x4u, v0);
		a1 = ZmR32(m, a2 - 0x78u);
		t0 = ZmR32(m, a2 - 0x74u);
		ZmW32(m, s1 + 0x8u, a1);
		ZmW32(m, s1 + 0xCu, t0);
		(void)ExtZp_Solver_3723e0(c, s1 + 0x10u, last + 0x10u);
		v0 = ZmR32(m, last + 0x70u);
		ZmW32(m, s1 + 0x70u, v0);
		v0 = ZmR32(m, s0 + 4u);
		v0 -= 1u;
		ZmW32(m, s0 + 4u, v0);
		v1 = v0 << 7;
		v0 = ZmR32(m, s0 + 8u);
		s2 = v0 + v1;
		(void)ExtZp_Solver_37b400(c, s0);
		Pen_DestroyElem(c, s2);
	}

	// ---- the new records C1 are appended
	v0 = ZmR32(m, SUB + 0x70u);
	a0 = ZmR32(m, v0 + 0x14u); // P
	(void)Pen_Call(c, EXT_ZP_Q_COLLIDE, 0x00376B90u, a0, C1, 0, 0, 0);
	v0 = ZmR32(m, sp + 0xA4u);
	s2 = ZmR32(m, sp + 0xA8u);
	s1 = s2 + (v0 << 7);
	if (s2 != s1)
	{
		do
		{
			v1 = ZmR32(m, s0 + 4u);
			v0 = ZmR32(m, s0 + 8u);
			ExtZp_Solver_37ae50(c, s0, v0 + (v1 << 7), 1u, s2);
			s2 += 0x80u;
		} while (s2 != s1);
	}

	// ---- pairs of P + 0xC8 that touch a body of V2 are removed
	DET = ZmR32(m, SUB + 0x70u);
	v0 = ExtZp_Pen_381020(c, DET);
	s1 = ZmR32(m, v0 + 8u);
	s0 = v0;
	for (;;)
	{
		v1 = ZmR32(m, s0 + 4u);
		v0 = ZmR32(m, s0 + 8u);
		a2 = v0 + (v1 << 3); // end
		if (s1 == a2)
			break;
		if (!Pen_PairTouches(c, V2, ZmR32(m, s1 + 0u), ZmR32(m, s1 + 4u)))
		{
			s1 += 8u;
			continue;
		}
		// removed: replaced by the last pair
		v0 = ZmR32(m, a2 - 8u);
		ZmW32(m, s1 + 0u, v0);
		v0 = ZmR32(m, a2 - 4u);
		ZmW32(m, s1 + 4u, v0);
		v0 = ZmR32(m, s0 + 4u);
		v0 -= 1u;
		(void)ExtZp_Pen_370820(c, s0);
		ZmW32(m, s0 + 4u, v0);
	}

	// ---- the new pairs C2 are appended to P + 0xC8
	v0 = ZmR32(m, sp + 0xB4u);
	s1 = ZmR32(m, sp + 0xB8u);
	s2 = s1 + (v0 << 3);
	if (s1 != s2)
	{
		do
		{
			v1 = ZmR32(m, s0 + 4u);
			v0 = ZmR32(m, s0 + 8u);
			ExtZp_Pen_3674c0(c, s0, v0 + (v1 << 3), 1u, s1);
			s1 += 8u;
		} while (s1 != s2);
	}
	(void)ExtZp_Pen_13e040(c, C2, 0u);
	(void)ExtZp_Pen_13dfd0(c, C1, 0u);
	(void)ExtZp_Solver_3455e0(c, V2, 0u);
	(void)ExtZp_Solver_3455e0(c, V1, 0u);
	ExtZp_Pop(c, 0xF0u);
}
