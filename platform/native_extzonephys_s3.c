// THE ZONE PORT, PART S3: the contact solver frame, island building (part 2) and the
// contact list copy (see include/platform/native_extzonephys.h for the conventions).
// Compiles alone too (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_s3.h>

#define S3_G_FILL_WORD 0x00645E48u     // a word the island table is reset with (global of the original)
#define S3_G_FILL_PTR 0x00645E50u      // fill value pointer handed to 35bc90
#define S3_G_ERRCTX 0x00657754u        // pointer to the object whose +0xC is the output stream of the error routine
#define S3_TXT_VEC_INSERT 0x00645BD0u  // error text address (vector insert too large)
#define S3_TXT_VEC_RESERVE 0x00643BB0u // error text address (reserve too large)
#define S3_VT_BASE 0x006CC390u         // contact table (base class form)
#define S3_VT_CONTACT 0x006CC3A0u      // contact table (slot +8 = destructor 0x13e130)

// A call the port does not compute: the original's function address and its a0..a3.
static uint32_t Solver_Other(ExtZpCtx *c, uint32_t func, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
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
	args.func = func;
	r = ExtZp_Query(c, EXT_ZP_Q_OTHER, &args);
	return r.v0;
}

// The original's 0x345a10: returns a0 + 8 (the data pointer slot of a vector head).
static uint32_t Solver_345a10(uint32_t a0)
{
	return a0 + 8u;
}

// The virtual call "element->table[+8](element, -1)": the destructor of a contact.
static void Solver_DestroyElem(ExtZpCtx *c, uint32_t elem)
{
	ExtZpMem *m = c->m;
	uint32_t t9 = ZmR32(m, elem + 0u); // table
	t9 = ZmR32(m, t9 + 8u);            // slot +8
	if (t9 == 0x0013E130u)
	{
		ExtZp_Solver_13e130(c, elem, 0xFFFFFFFFu);
	}
	else
	{
		ExtZp_Leave(c, t9);
	}
}

// ------------------------------------------------------------------ tiny functions
uint32_t ExtZp_Solver_3a9920(ExtZpCtx *c, uint32_t a0) { (void)c; return a0; }
uint32_t ExtZp_Solver_3a9930(ExtZpCtx *c, uint32_t a0) { (void)c; return a0; }
uint32_t ExtZp_Solver_37b400(ExtZpCtx *c, uint32_t a0) { (void)c; return a0; }
uint32_t ExtZp_Solver_37b430(ExtZpCtx *c, uint32_t a0) { (void)c; return a0; }

uint32_t ExtZp_Solver_348e00(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	return ZmR32(c->m, a0 + 8u) + (a1 << 2);
}
uint32_t ExtZp_Solver_3af420(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	return ZmR32(c->m, a0 + 8u) + (a1 << 2);
}

// 0x345640 (clears a vector head) and 0x345660 (returns a0) are in part s2.

void ExtZp_Solver_37b410(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	uint32_t a2 = ZmR32(c->m, a0);
	uint32_t v1 = ZmR32(c->m, a1);
	ZmW32(c->m, a0, v1);
	ZmW32(c->m, a1, a2);
}

// A contact's data block: 4 vectors, a byte at +0x40, 5 words +0x44..+0x54, words +0x58, +0x5C.
uint32_t ExtZp_Solver_35bfa0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	ZmWV(m, a0 + 0x00u, ZmRV(m, a1 + 0x00u));
	ZmWV(m, a0 + 0x10u, ZmRV(m, a1 + 0x10u));
	ZmWV(m, a0 + 0x20u, ZmRV(m, a1 + 0x20u));
	ZmWV(m, a0 + 0x30u, ZmRV(m, a1 + 0x30u));
	ZmW8(m, a0 + 0x40u, ZmR8(m, a1 + 0x40u));
	ZmWF(m, a0 + 0x44u, ZmRF(m, a1 + 0x44u));
	ZmWF(m, a0 + 0x48u, ZmRF(m, a1 + 0x48u));
	ZmWF(m, a0 + 0x4Cu, ZmRF(m, a1 + 0x4Cu));
	ZmWF(m, a0 + 0x50u, ZmRF(m, a1 + 0x50u));
	ZmWF(m, a0 + 0x54u, ZmRF(m, a1 + 0x54u));
	ZmW32(m, a0 + 0x58u, ZmR32(m, a1 + 0x58u));
	ZmW32(m, a0 + 0x5Cu, ZmR32(m, a1 + 0x5Cu));
	return a0;
}
// Same body as 35bfa0 (a second copy in the original).
uint32_t ExtZp_Solver_3723e0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	ZmWV(m, a0 + 0x00u, ZmRV(m, a1 + 0x00u));
	ZmWV(m, a0 + 0x10u, ZmRV(m, a1 + 0x10u));
	ZmWV(m, a0 + 0x20u, ZmRV(m, a1 + 0x20u));
	ZmWV(m, a0 + 0x30u, ZmRV(m, a1 + 0x30u));
	ZmW8(m, a0 + 0x40u, ZmR8(m, a1 + 0x40u));
	ZmWF(m, a0 + 0x44u, ZmRF(m, a1 + 0x44u));
	ZmWF(m, a0 + 0x48u, ZmRF(m, a1 + 0x48u));
	ZmWF(m, a0 + 0x4Cu, ZmRF(m, a1 + 0x4Cu));
	ZmWF(m, a0 + 0x50u, ZmRF(m, a1 + 0x50u));
	ZmWF(m, a0 + 0x54u, ZmRF(m, a1 + 0x54u));
	ZmW32(m, a0 + 0x58u, ZmR32(m, a1 + 0x58u));
	ZmW32(m, a0 + 0x5Cu, ZmR32(m, a1 + 0x5Cu));
	return a0;
}

// ------------------------------------------------------------------ object release
// Frees the data block of a vector head (when it has one) and, when (int16)a1 > 0, the object itself (0x4ba240).
uint32_t ExtZp_Solver_3455e0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	if (a0 != 0u)
	{
		uint32_t a = ZmR32(m, a0 + 8u);
		if (a != 0u)
		{
			ExtZp_Integ_33c170(c, a);
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			Solver_Other(c, 0x004BA240u, a0, 0, 0, 0);
		}
	}
	return a0;
}

// Releases a tree holder: the tree at +4 (3a9760), a1 > 0 frees the holder (0x4ba240).
uint32_t ExtZp_Solver_3a9040(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	if (a0 != 0u)
	{
		uint32_t node = ZmR32(m, a0 + 4u);
		if (node != 0u)
		{
			ExtZp_Solver_3a9760(c, a0, node);
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			Solver_Other(c, 0x004BA240u, a0, 0, 0, 0);
		}
	}
	return a0;
}

// Releases an island solver object: its list at +0x14 (flag 1), the holder at +4 (flag -1); a1 > 0: frees the object (0x33c3b0, size 0x18, tag 0xD).
uint32_t ExtZp_Solver_3a90a0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	if (a0 != 0u)
	{
		uint32_t n;
		ExtZp_Solver_3af250(c, ZmR32(m, a0 + 0x14u), 1u);
		n = a0 + 4u;
		if (n != 0u)
		{
			if (n != 0u)
			{
				ExtZp_Solver_3a9040(c, n, 0xFFFFFFFFu);
			}
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			Solver_Other(c, 0x0033C3B0u, a0, 0x18u, 0x0Du, 0);
		}
	}
	return a0;
}

// Releases an object with two vector heads (+0xC and +0, in this order); a1 > 0: frees the object (0x33c3b0, size 0x1C, tag 0xD).
uint32_t ExtZp_Solver_3af250(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	if (a0 != 0u)
	{
		uint32_t h = a0 + 0xCu;
		if (h != 0u)
		{
			ExtZp_Solver_3455e0(c, h, 0u);
		}
		if (a0 != 0u)
		{
			ExtZp_Solver_3455e0(c, a0, 0u);
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			Solver_Other(c, 0x0033C3B0u, a0, 0x1Cu, 0x0Du, 0);
		}
	}
	return a0;
}

// Releases a two-level tree: a0 = owner, a1 = root node {first +0, second +4}; every inner node
// {left +0, right +4} with its two sub nodes is released, bottom up, and each node block freed (0x33c170).
static void Solver_ReleaseInner(ExtZpCtx *c, uint32_t owner, uint32_t s3)
{
	ExtZpMem *m = c->m;
	uint32_t a1 = ZmR32(m, s3 + 0u);
	if (a1 != 0u)
	{
		ExtZp_Solver_3a9760(c, owner, a1);
	}
	a1 = ZmR32(m, s3 + 4u);
	if (a1 != 0u)
	{
		ExtZp_Solver_3a9760(c, owner, a1);
	}
	ExtZp_Solver_3a9930(c, owner);
	ExtZp_Solver_3a9920(c, owner + 4u);
	ExtZp_Integ_33c170(c, s3);
}
static void Solver_ReleaseSide(ExtZpCtx *c, uint32_t owner, uint32_t s2)
{
	ExtZpMem *m = c->m;
	uint32_t s3 = ZmR32(m, s2 + 0u);
	if (s3 != 0u)
	{
		Solver_ReleaseInner(c, owner, s3);
	}
	s3 = ZmR32(m, s2 + 4u);
	if (s3 != 0u)
	{
		Solver_ReleaseInner(c, owner, s3);
	}
	ExtZp_Integ_33c170(c, s2);
}
void ExtZp_Solver_3a9760(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s2 = ZmR32(m, a1 + 0u);
	if (s2 != 0u)
	{
		Solver_ReleaseSide(c, a0, s2);
	}
	s2 = ZmR32(m, a1 + 4u);
	if (s2 != 0u)
	{
		Solver_ReleaseSide(c, a0, s2);
	}
	ExtZp_Integ_33c170(c, a1);
}

// ------------------------------------------------------------------ contact destruction
// The contact's destructor: restores its tables, a1 > 0: frees the block (0x33c3b0, size = [+4], tag 0x13). Returns a0.
uint32_t ExtZp_Solver_13e130(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	if (a0 != 0u)
	{
		ZmW32(m, a0 + 0u, S3_VT_CONTACT);
		if (a0 != 0u)
		{
			ZmW32(m, a0 + 0u, S3_VT_BASE);
		}
		if ((int16_t)(uint16_t)a1 > 0)
		{
			uint32_t sz = ZmR32(m, a0 + 4u);
			Solver_Other(c, 0x0033C3B0u, a0, sz, 0x13u, 0);
		}
	}
	return a0;
}

void ExtZp_Solver_13e0b0(ExtZpCtx *c, uint32_t a0)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, a0 + 4u);
	uint32_t s0 = ZmR32(m, a0 + 8u);
	uint32_t s1;
	v1 <<= 7;
	s1 = s0 + v1;
	if (s0 < s1)
	{
		do
		{
			s1 -= 0x80u;
			Solver_DestroyElem(c, s1);
		} while (s0 < s1);
	}
	ZmW32(m, a0 + 4u, 0u);
}

// ------------------------------------------------------------------ contact vector insert
// Copy constructor of a contact (d is raw memory).
static void Solver_CopyConstruct(ExtZpCtx *c, uint32_t d, uint32_t s)
{
	ExtZpMem *m = c->m;
	uint32_t v1, v0;
	ZmW32(m, d + 0u, S3_VT_BASE);
	v1 = ZmR32(m, s + 4u);
	ZmW32(m, d + 4u, v1);
	ZmW32(m, d + 0u, S3_VT_CONTACT);
	v0 = ZmR32(m, s + 8u);
	ZmW32(m, d + 8u, v0);
	v0 = ZmR32(m, s + 0xCu);
	ZmW32(m, d + 0xCu, v0);
	ExtZp_Solver_35bfa0(c, d + 0x10u, s + 0x10u);
	v1 = ZmR32(m, s + 0x70u);
	ZmW32(m, d + 0x70u, v1);
}
// Copy assignment of a contact (the table pointer and +0x74.. stay).
static void Solver_CopyAssign(ExtZpCtx *c, uint32_t d, uint32_t s)
{
	ExtZpMem *m = c->m;
	uint32_t v0, f1, f0, v1;
	v0 = ZmR32(m, s + 4u);
	ZmW32(m, d + 4u, v0);
	f1 = ZmR32(m, s + 8u);
	f0 = ZmR32(m, s + 0xCu);
	ZmW32(m, d + 8u, f1);
	ZmW32(m, d + 0xCu, f0);
	ExtZp_Solver_3723e0(c, d + 0x10u, s + 0x10u);
	v1 = ZmR32(m, s + 0x70u);
	ZmW32(m, d + 0x70u, v1);
}

void ExtZp_Solver_37ae50(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	uint32_t s5 = a0, s4 = a1, s3 = a2, s2 = a3;
	uint32_t s0, s1, s6, s7, fp, v1, v0, at, t;
	int32_t d;
	if (s3 == 0u)
		return;
	v1 = 0x01FFFFFFu;
	if (v1 < s3)
		goto err;
	t = ZmR32(m, s5 + 4u);
	v1 -= s3;
	if (!(v1 < t))
		goto ok;
err:
	v0 = ZmR32(m, S3_G_ERRCTX);
	t = ZmR32(m, v0 + 0xCu);
	Solver_Other(c, 0x00429F38u, t, S3_TXT_VEC_INSERT, 0, 0);
	Solver_Other(c, 0x0042BD70u, 0, 0, 0, 0);
ok:
	a1 = ZmR32(m, s5 + 4u);
	v1 = ZmR32(m, s5 + 0u);
	t = a1 + s3;
	if (v1 < t)
		goto grow;
	// in place: enough capacity
	v1 = ZmR32(m, s5 + 8u);
	t = a1 << 7;
	s6 = v1 + t;
	d = (int32_t)(s6 - s4);
	if (d >= 0)
		s7 = (uint32_t)(d >> 7);
	else
		s7 = (uint32_t)((int32_t)((uint32_t)d + 0x7Fu) >> 7);
	fp = s3 << 7;
	if (!(s7 < s3))
		goto L37b00c;
	s0 = s6;
	do
	{
		if (s0 != 0u)
			Solver_CopyConstruct(c, s0, s2);
		v1 = ZmR32(m, s5 + 4u);
		s3 -= 1u;
		s0 += 0x80u;
		v1 += 1u;
		ZmW32(m, s5 + 4u, v1);
	} while (s7 < s3);
	if (!(s4 < s6))
		goto L37b0fc;
	s1 = s4;
	do
	{
		if (s0 != 0u)
			Solver_CopyConstruct(c, s0, s1);
		t = ZmR32(m, s5 + 4u);
		s1 += 0x80u;
		s0 += 0x80u;
		at = (s1 < s6);
		t += 1u;
		ZmW32(m, s5 + 4u, t);
	} while (at);
	goto L37b0fc;

L37b00c:
	s0 = s6 - fp;
	if (s0 < s6)
	{
		s1 = s6;
		do
		{
			if (s1 != 0u)
				Solver_CopyConstruct(c, s1, s0);
			t = ZmR32(m, s5 + 4u);
			s0 += 0x80u;
			s1 += 0x80u;
			at = (s0 < s6);
			t += 1u;
			ZmW32(m, s5 + 4u, t);
		} while (at);
	}
	v1 = s7 - s3;
	t = v1 << 7;
	v1 = s6 - t;
	s0 = s4 + t;
	if (!(s2 < v1))
	{
		if (s2 < s6)
			s2 += fp;
		s0 = s4 + t;
	}
	if (s4 < s0)
	{
		do
		{
			s0 -= 0x80u;
			s6 -= 0x80u;
			Solver_CopyAssign(c, s6, s0);
			v1 = ZmR32(m, s0 + 0x70u);
			at = (s4 < s0);
			ZmW32(m, s6 + 0x70u, v1);
		} while (at);
	}
L37b0fc:
	if (s3 == 0u)
		return;
	do
	{
		v0 = ZmR32(m, s2 + 4u);
		ZmW32(m, s4 + 4u, v0);
		a1 = ZmR32(m, s2 + 8u);   // f1
		a0 = ZmR32(m, s2 + 0xCu); // f0
		ZmW32(m, s4 + 8u, a1);
		ZmW32(m, s4 + 0xCu, a0);
		ExtZp_Solver_3723e0(c, s4 + 0x10u, s2 + 0x10u);
		v1 = ZmR32(m, s2 + 0x70u);
		s3 -= 1u;
		ZmW32(m, s4 + 0x70u, v1);
		s4 += 0x80u;
	} while (s3 != 0u);
	return;

grow:
	{
		// a new block: the elements before pos, n copies, the elements after pos; then swapped in, old ones destroyed
		uint32_t tmp = ExtZp_Push(c, 0x10u); // temporary vector head: capacity +0, size +4 (s6), data +8 (fp)
		uint32_t sizeNew;
		ExtZp_Solver_37b430(c, tmp);
		ZmW32(m, tmp + 0u, 0u);
		s6 = tmp + 4u;
		ZmW32(m, s6, 0u);
		fp = tmp + 8u;
		ZmW32(m, fp, 0u);
		v0 = ZmR32(m, s5 + 4u);
		s0 = ZmR32(m, s5 + 0u);
		if (s0 == 0u)
			s0 = 1u;
		v1 = v0 + s3;
		at = (s0 < v1);
		if (at)
		{
			do
			{
				if (s0 < 0x00FFFFFFu)
					s0 <<= 1;
				else
					s0 = 0x01FFFFFFu;
			} while (s0 < v1);
		}
		v0 = ExtZp_Integ_33c120(c, s0 << 7, 0x0Eu);
		ZmW32(m, fp, v0);
		ZmW32(m, tmp + 0u, s0);
		s1 = ZmR32(m, s5 + 8u);
		v1 = ZmR32(m, s5 + 4u);
		s0 = ZmR32(m, fp);
		at = (s1 < s4);
		v1 <<= 7;
		s7 = s1 + v1;
		if (at)
		{
			do
			{
				if (s0 != 0u)
					Solver_CopyConstruct(c, s0, s1);
				sizeNew = ZmR32(m, s6) + 1u;
				s1 += 0x80u;
				s0 += 0x80u;
				at = (s1 < s4);
				ZmW32(m, s6, sizeNew);
			} while (at);
		}
		if (s3 != 0u)
		{
			do
			{
				if (s0 != 0u)
					Solver_CopyConstruct(c, s0, s2);
				v1 = ZmR32(m, s6);
				s3 -= 1u;
				s0 += 0x80u;
				v1 += 1u;
				ZmW32(m, s6, v1);
			} while (s3 != 0u);
		}
		if (s1 < s7)
		{
			do
			{
				if (s0 != 0u)
					Solver_CopyConstruct(c, s0, s1);
				sizeNew = ZmR32(m, s6) + 1u;
				s1 += 0x80u;
				s0 += 0x80u;
				at = (s1 < s7);
				ZmW32(m, s6, sizeNew);
			} while (at);
		}
		if (tmp != s5)
		{
			uint32_t x;
			ExtZp_Solver_37b410(c, tmp, s5);
			x = ZmR32(m, fp);
			v1 = ZmR32(m, s5 + 8u);
			ZmW32(m, fp, v1);
			ZmW32(m, s5 + 8u, x);
			x = ZmR32(m, s6);
			v1 = ZmR32(m, s5 + 4u);
			ZmW32(m, s6, v1);
			ZmW32(m, s5 + 4u, x);
		}
		v1 = ZmR32(m, s6);
		s0 = ZmR32(m, fp);
		v1 <<= 7;
		s1 = s0 + v1;
		if (s0 < s1)
		{
			do
			{
				s1 -= 0x80u;
				ExtZp_Solver_37b400(c, tmp);
				Solver_DestroyElem(c, s1);
			} while (s0 < s1);
		}
		ZmW32(m, s6, 0u);
		t = ZmR32(m, fp);
		if (t != 0u)
			ExtZp_Integ_33c170(c, t);
		ExtZp_Pop(c, 0x10u);
	}
}

// ------------------------------------------------------------------ island building
// Union-find "find" over the label table (entries at +8, count +4): entry a1; a path of links is
// flattened (every entry on it is set to the root); a negative entry is a root. Returns the root.
uint32_t ExtZp_Solver_3af2f0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s2 = a0;
	uint32_t s0 = ZmR32(m, s2 + 8u) + (a1 << 2);
	int32_t s1 = (int32_t)ZmR32(m, s0);
	uint32_t v0, s1p, s3, s3p, s4, p, r;
	if (s1 < 0)
		return a1;
	v0 = ZmR32(m, Solver_345a10(s2));
	v0 += (uint32_t)s1 << 2;
	if ((int32_t)ZmR32(m, v0) < 0)
	{
		ZmW32(m, s0, (uint32_t)s1);
		return (uint32_t)s1;
	}
	s1p = ZmR32(m, s2 + 8u) + ((uint32_t)s1 << 2);
	s3 = ZmR32(m, s1p);
	v0 = ExtZp_Solver_348e00(c, s2, s3);
	if ((int32_t)ZmR32(m, v0) < 0)
	{
		ZmW32(m, s1p, s3);
		ZmW32(m, s0, s3);
		return s3;
	}
	s4 = s3 << 2;
	s3p = ZmR32(m, Solver_345a10(s2)) + s4;
	v0 = ZmR32(m, Solver_345a10(s2)) + s4;
	s4 = ZmR32(m, v0);
	v0 = ExtZp_Solver_3af420(c, s2, s4);
	if ((int32_t)ZmR32(m, v0) < 0)
	{
		ZmW32(m, s3p, s4);
		ZmW32(m, s1p, s4);
		ZmW32(m, s0, s4);
		return s4;
	}
	(void)ExtZp_Solver_3af420(c, s2, s4);
	p = ExtZp_Solver_3af420(c, s2, s4);
	r = ExtZp_Solver_3af2f0(c, s2, ZmR32(m, p));
	ZmW32(m, p, r);
	ZmW32(m, s3p, r);
	ZmW32(m, s1p, r);
	ZmW32(m, s0, r);
	return r;
}

// Joins the labels a1 and a2 (negative table values are the set sizes): the larger set's label stays.
void ExtZp_Solver_3af6d0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, a0 + 8u);
	uint32_t t3 = a1 << 2, t1 = a2 << 2;
	uint32_t t2 = v1 + t1, t0 = v1 + t3;
	int32_t x = (int32_t)ZmR32(m, t0);
	int32_t a3 = (int32_t)ZmR32(m, t2);
	if (a3 < x)
	{
		ZmW32(m, t2, (uint32_t)(a3 + x));
		v1 = ZmR32(m, a0 + 8u) + t3;
		ZmW32(m, v1, a2);
	}
	else
	{
		ZmW32(m, t0, (uint32_t)(x + a3));
		v1 = ZmR32(m, a0 + 8u) + t1;
		ZmW32(m, v1, a1);
	}
}

// Makes the capacity of a word vector at least a1: copies into a new block (33c120, tag 0xE).
void ExtZp_Solver_34a640(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s1 = a0, s0 = a1, s2, v0, v1, p, q;
	v1 = ZmR32(m, s1 + 0u);
	if (!(v1 < s0))
		return;
	if (!(s0 < 0x40000000u))
	{
		v0 = ZmR32(m, S3_G_ERRCTX);
		v0 = ZmR32(m, v0 + 0xCu);
		Solver_Other(c, 0x00429F38u, v0, S3_TXT_VEC_RESERVE, 0, 0);
		Solver_Other(c, 0x0042BD70u, 0, 0, 0, 0);
	}
	s2 = ZmR32(m, s1 + 8u);
	v0 = ExtZp_Integ_33c120(c, s0 << 2, 0x0Eu);
	ZmW32(m, s1 + 8u, v0);
	if (s2 != 0u)
	{
		v0 = ZmR32(m, s1 + 4u);
		q = ZmR32(m, s1 + 8u);
		v0 <<= 2;
		v1 = s2 + v0;
		if (s2 < v1)
		{
			p = s2;
			do
			{
				v0 = ZmR32(m, p);
				ZmW32(m, q, v0);
				p += 4u;
				q += 4u;
			} while (p < v1);
		}
		ExtZp_Integ_33c170(c, s2);
	}
	ZmW32(m, s1 + 0u, s0);
}

// Collects the first island that is not taken yet: a0 = the solver object (+0x10 entries, labels at +0x14,
// +0x18 = first entry not yet looked at); a1 = the vector (cap/size/data) that receives the entry numbers. Returns 1 if an island was found.
uint32_t ExtZp_Solver_3af730(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s3 = a0, s2 = a1;
	uint32_t s0 = ZmR32(m, a0 + 0x10u);
	uint32_t loc = ExtZp_Push(c, 0x10u); // +0 i (sp+0x58), +4 j (sp+0x5c)
	uint32_t rv = 0u;
	int32_t s1;
	uint32_t v0, v1;
	ZmW32(m, loc + 0u, ZmR32(m, a0 + 0x18u));
	for (;;)
	{
		v1 = ZmR32(m, loc + 0u);
		if (!((int32_t)v1 < (int32_t)s0))
			break;
		v0 = ZmR32(m, s3 + 0x14u) + (v1 << 2);
		s1 = (int32_t)ZmR32(m, v0);
		if (s1 < 0)
		{
			ZmW32(m, loc + 0u, ZmR32(m, loc + 0u) + 1u);
			continue;
		}
		ZmW32(m, s2 + 4u, 0u);
		v0 = ZmR32(m, Solver_345a10(s2));
		v1 = ZmR32(m, s2 + 4u) << 2;
		ExtZp_Integ_33f800(c, s2, v0 + v1, 1u, loc + 0u);
		v0 = ZmR32(m, loc + 0u) + 1u;
		ZmW32(m, s3 + 0x18u, v0);
		v1 = ZmR32(m, loc + 0u) << 2;
		v0 = ZmR32(m, s3 + 0x14u) + v1;
		ZmW32(m, v0, 0xFFFFFFFFu);
		v0 = ZmR32(m, loc + 0u) + 1u;
		ZmW32(m, loc + 4u, v0);
		while ((int32_t)ZmR32(m, loc + 4u) < (int32_t)s0)
		{
			v1 = ZmR32(m, loc + 4u) << 2;
			v0 = ZmR32(m, s3 + 0x14u) + v1;
			v0 = ZmR32(m, v0);
			if ((uint32_t)s1 == v0)
			{
				v0 = ZmR32(m, Solver_345a10(s2));
				v1 = ZmR32(m, s2 + 4u) << 2;
				ExtZp_Integ_33f800(c, s2, v0 + v1, 1u, loc + 4u);
				v1 = ZmR32(m, loc + 4u) << 2;
				v0 = ZmR32(m, s3 + 0x14u) + v1;
				ZmW32(m, v0, 0xFFFFFFFFu);
			}
			ZmW32(m, loc + 4u, ZmR32(m, loc + 4u) + 1u);
		}
		rv = 1u;
		break;
	}
	ExtZp_Pop(c, 0x10u);
	return rv;
}

// Flattens the label chains of the table (count +4, data +8) and of the list (count +0x10, data +0x14).
void ExtZp_Solver_3af430(ExtZpCtx *c, uint32_t a0)
{
	ExtZpMem *m = c->m;
	uint32_t a3 = ZmR32(m, a0 + 8u);
	uint32_t a2 = a3 + (ZmR32(m, a0 + 4u) << 2);
	uint32_t a1, v1;
	if (a3 != a2)
	{
		do
		{
			v1 = ZmR32(m, a3);
			if ((int32_t)v1 >= 0)
			{
				for (;;)
				{
					a1 = ZmR32(m, a3);
					a1 = ZmR32(m, a0 + 8u) + (a1 << 2);
					v1 = ZmR32(m, a1);
					if ((int32_t)v1 < 0)
						break;
					v1 = ZmR32(m, a1);
					ZmW32(m, a3, v1);
				}
			}
			a3 += 4u;
		} while (a3 != a2);
	}
	a3 = ZmR32(m, a0 + 0x14u);
	a2 = a3 + (ZmR32(m, a0 + 0x10u) << 2);
	if (a3 != a2)
	{
		do
		{
			for (;;)
			{
				a1 = ZmR32(m, a3);
				a1 = ZmR32(m, a0 + 8u) + (a1 << 2);
				v1 = ZmR32(m, a1);
				if ((int32_t)v1 < 0)
					break;
				v1 = ZmR32(m, a1);
				ZmW32(m, a3, v1);
			}
			a3 += 4u;
		} while (a3 != a2);
	}
}

// Builds the islands of a pair list: a0 = the solver object; a1 = the pair list (count +4, pairs of two entry
// numbers at +8); a2 = the table size. Resets the table, joins the labels of every pair, flattens the chains.
void ExtZp_Solver_3af4f0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t s4 = a0, s0, s1, s2, s3, v0, v1, p, loc;
	ExtZp_Integ_35bc90(c, s4, a2, S3_G_FILL_PTR);
	v0 = ZmR32(m, Solver_345a10(s4));
	v1 = ZmR32(m, s4 + 4u) << 2;
	s1 = v0 + v1;
	v1 = ZmR32(m, Solver_345a10(s4));
	if (v1 != s1)
	{
		do
		{
			v0 = ZmR32(m, S3_G_FILL_WORD);
			ZmW32(m, v1, v0);
			v1 += 4u;
		} while (v1 != s1);
	}
	ZmW32(m, s4 + 0x10u, 0u);
	ExtZp_Solver_34a640(c, s4 + 0xCu, ZmR32(m, a1 + 4u));
	v0 = ZmR32(m, a1 + 4u);
	s1 = ZmR32(m, a1 + 8u);
	s0 = s1 + (v0 << 3);
	loc = ExtZp_Push(c, 0x10u);
	if (s1 != s0)
	{
		do
		{
			s2 = ZmR32(m, s1 + 0u);
			s3 = ZmR32(m, s1 + 4u);
			p = ExtZp_Solver_3af420(c, s4, s2);
			if ((int32_t)ZmR32(m, p) >= 0)
			{
				(void)ExtZp_Solver_3af420(c, s4, s2);
				p = ExtZp_Solver_3af420(c, s4, s2);
				v0 = ExtZp_Solver_3af2f0(c, s4, ZmR32(m, p));
				ZmW32(m, p, v0);
				s2 = v0;
			}
			p = ExtZp_Solver_3af420(c, s4, s3);
			if ((int32_t)ZmR32(m, p) >= 0)
			{
				(void)ExtZp_Solver_3af420(c, s4, s3);
				p = ExtZp_Solver_3af420(c, s4, s3);
				v0 = ExtZp_Solver_3af2f0(c, s4, ZmR32(m, p));
				ZmW32(m, p, v0);
				s3 = v0;
			}
			if (s2 != s3)
			{
				ExtZp_Solver_3af6d0(c, s4, s2, s3);
			}
			if (!((int32_t)s2 < (int32_t)s3))
				s2 = s3;
			ZmW32(m, loc + 0u, s2);
			v0 = ZmR32(m, Solver_345a10(s4 + 0xCu));
			v1 = ZmR32(m, s4 + 0x10u) << 2;
			ExtZp_Integ_33f800(c, s4 + 0xCu, v0 + v1, 1u, loc + 0u);
			s1 += 8u;
		} while (s1 != s0);
	}
	ExtZp_Pop(c, 0x10u);
	ExtZp_Solver_3af430(c, s4);
	ZmW32(m, s4 + 0x18u, 0u);
}

// Builds the contacts of one island: a0 = the solver (+0 = the owner of the source contact vector, +0x14 = the island object),
// a1 = the target contact vector. Returns 1 if an island was found (its contacts are appended to a1), else 0.
uint32_t ExtZp_Solver_3a9120(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s3 = a0, s2 = a1, s0, s1, v0, v1, a3, a1v;
	uint32_t hd = ExtZp_Push(c, 0x20u); // hd +0 .. +8: the vector head (sp+0x50), hd + 0xC the word at sp+0x5c
	uint32_t rv;
	ExtZp_Solver_345660(c, hd + 0xCu);
	ExtZp_Solver_345640(c, hd, hd + 0xCu);
	v0 = ExtZp_Solver_3af730(c, ZmR32(m, s3 + 0x14u), hd);
	if (v0 == 0u)
	{
		ExtZp_Solver_3455e0(c, hd, 0u);
		rv = 0u;
	}
	else
	{
		ExtZp_Solver_13e0b0(c, s2);
		s0 = 0u;
		s1 = 0u;
		while (s0 < ZmR32(m, hd + 4u))
		{
			v1 = ZmR32(m, hd + 8u);
			v0 = ZmR32(m, s3);
			a3 = ZmR32(m, s2 + 4u);
			a1v = ZmR32(m, s2 + 8u);
			v1 = v1 + s1;
			v1 = ZmR32(m, v1);
			v0 = ZmR32(m, v0 + 8u);
			a3 <<= 7;
			a1v += a3;
			v1 <<= 7;
			ExtZp_Solver_37ae50(c, s2, a1v, 1u, v0 + v1);
			s1 += 4u;
			s0 += 1u;
		}
		ExtZp_Solver_3455e0(c, hd, 0u);
		rv = 1u;
	}
	ExtZp_Pop(c, 0x20u);
	return rv;
}
