// THE ZONE PORT, PART Z9B: removing a rigid body from the physics system (see
// include/platform/native_extzonephys.h for the conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_z9b.h>

// Layout of the physics system (a0), every list a vector {capacity +0, size +4, data +8}; the vector
// heads sit at these offsets:
#define Z9B_SYS_LIST_STATIC 0x34u // words: bodies with the "static" bit of their definition
#define Z9B_SYS_LIST_DYN_A 0x28u  // words: bodies without that bit
#define Z9B_SYS_LIST_DYN_B 0x1Cu  // words: bodies without that bit
#define Z9B_SYS_LIST_W80 0x80u    // words
#define Z9B_SYS_LIST_W8C 0x8Cu    // words
#define Z9B_SYS_LIST_DEF_A 0x74u  // words: body definitions
#define Z9B_SYS_LIST_DEF_B 0x68u  // words: body definitions
#define Z9B_SYS_LIST_P5C 0x5Cu    // pairs (8 bytes)
#define Z9B_SYS_LIST_P98 0x98u    // pairs
#define Z9B_SYS_LIST_PB0 0xB0u    // pairs
#define Z9B_SYS_LIST_PBC 0xBCu    // pairs
#define Z9B_SYS_LIST_R1B8 0x1B8u  // records of 12 bytes, the body at +8 (static bodies)
#define Z9B_SYS_LIST_R1A0 0x1A0u  // records of 12 bytes (other bodies)
#define Z9B_SYS_LIST_R1AC 0x1ACu  // records of 12 bytes (other bodies)
#define Z9B_SYS_LIST_C128 0xF8u   // contact records of 0x80 bytes, the two bodies at +8 / +0xC
#define Z9B_SYS_LIST_PEC 0xECu    // pairs (cleaned by 0x36f5a0)
#define Z9B_SYS_LIST_PC8 0xC8u    // pairs (cleaned by 0x36f5a0)
#define Z9B_SYS_LIST_PE0 0xE0u    // pairs (cleaned by 0x36f5a0)
#define Z9B_SYS_TABLE_D4 0xD4u    // pairs cleared in place (size +0xD8, data +0xDC)
#define Z9B_SYS_DIRTY 0x184u      // byte cleared at the start

// 0x35f070: erases the word at a1 from a word vector (a0): the following words move down by one, size - 1. Returns a1.
static uint32_t Z9b_Erase4(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t a3 = ZmR32(m, a0 + 8u) + (ZmR32(m, a0 + 4u) << 2);
	int32_t v1 = (int32_t)(a3 - a1);
	int32_t v0 = (v1 >= 0) ? (v1 >> 2) : ((v1 + 3) >> 2);
	v0 -= 1;
	if (v0 != 0)
	{
		uint32_t src = a1 + 4u;
		uint32_t dst = a1;
		if (src < a3)
		{
			do
			{
				ZmW32(m, dst, ZmR32(m, src));
				src += 4u;
				dst += 4u;
			} while (src < a3);
		}
	}
	ZmW32(m, a0 + 4u, ZmR32(m, a0 + 4u) - 1u);
	return a1;
}

// 0x37ab50: erases the pair (two words) at a1 from a pair vector (a0). Returns a1.
// 0x11c620 is the same function with the copy unrolled by 16 words; the copy is still strictly forward,
// word by word, so the bytes written are the same and it shares this port.
static uint32_t Z9b_Erase8(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t a3 = ZmR32(m, a0 + 8u) + (ZmR32(m, a0 + 4u) << 3);
	int32_t v1 = (int32_t)(a3 - a1);
	int32_t v0 = (v1 >= 0) ? (v1 >> 3) : ((v1 + 7) >> 3);
	v0 -= 1;
	if (v0 != 0)
	{
		uint32_t src = a1 + 8u;
		uint32_t dst = a1;
		if (src < a3)
		{
			do
			{
				ZmW32(m, dst, ZmR32(m, src));
				ZmW32(m, dst + 4u, ZmR32(m, src + 4u));
				src += 8u;
				dst += 8u;
			} while (src < a3);
		}
	}
	ZmW32(m, a0 + 4u, ZmR32(m, a0 + 4u) - 1u);
	return a1;
}

// 0x37b440: erases the 12-byte record at a1 from a record vector (a0). Returns a1.
static uint32_t Z9b_Erase12(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t a3 = ZmR32(m, a0 + 8u) + (ZmR32(m, a0 + 4u) * 12u);
	int32_t v1 = (int32_t)(a3 - a1);
	// the original divides by 12 with the multiply-high trick: hi(0x2AAAAAAB * v1) >> 1, plus the sign bit of v1
	int32_t hi = (int32_t)(((int64_t)(int32_t)0x2AAAAAABu * (int64_t)v1) >> 32);
	int32_t v0 = (hi >> 1) + (int32_t)((uint32_t)v1 >> 31);
	v0 -= 1;
	if (v0 != 0)
	{
		uint32_t src = a1 + 0xCu;
		uint32_t dst = a1;
		if (src < a3)
		{
			do
			{
				ZmW32(m, dst, ZmR32(m, src));
				ZmW32(m, dst + 4u, ZmR32(m, src + 4u));
				ZmW32(m, dst + 8u, ZmR32(m, src + 8u));
				src += 0xCu;
				dst += 0xCu;
			} while (src < a3);
		}
	}
	ZmW32(m, a0 + 4u, ZmR32(m, a0 + 4u) - 1u);
	return a1;
}

// The virtual call "element->table[+8](element, -1)": the destructor of a contact record.
static void Z9b_DestroyElem(ExtZpCtx *c, uint32_t elem)
{
	ExtZpMem *m = c->m;
	uint32_t t9 = ZmR32(m, elem + 0u); // table
	t9 = ZmR32(m, t9 + 8u);            // slot +8
	if (t9 == 0x0013E130u)
	{
		(void)ExtZp_Solver_13e130(c, elem, 0xFFFFFFFFu);
	}
	else
	{
		ExtZp_Leave(c, t9);
	}
}

// 0x37ad60: erases the contact record (0x80 bytes) at a1 from the contact vector (a0), then destroys the
// record that was the last one. The table pointer at +0 of each record stays. Returns a1.
static uint32_t Z9b_EraseContact(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s2 = ZmR32(m, a0 + 8u) + (ZmR32(m, a0 + 4u) << 7);
	int32_t v1 = (int32_t)(s2 - a1);
	int32_t v0 = (v1 >= 0) ? (v1 >> 7) : ((v1 + 0x7F) >> 7);
	uint32_t s0, s1, cnt, last;
	v0 -= 1;
	s0 = a1 + 0x80u;
	s1 = a1;
	if (v0 != 0 && s0 < s2)
	{
		do
		{
			uint32_t w4 = ZmR32(m, s0 + 4u);
			Ps2F f1, f0;
			ZmW32(m, s1 + 4u, w4);
			f1 = ZmRF(m, s0 + 8u);
			f0 = ZmRF(m, s0 + 0xCu);
			ZmWF(m, s1 + 8u, f1);
			ZmWF(m, s1 + 0xCu, f0);
			(void)ExtZp_Solver_3723e0(c, s1 + 0x10u, s0 + 0x10u);
			ZmW32(m, s1 + 0x70u, ZmR32(m, s0 + 0x70u));
			s0 += 0x80u;
			s1 += 0x80u;
		} while (s0 < s2);
	}
	cnt = ZmR32(m, a0 + 4u) - 1u;
	ZmW32(m, a0 + 4u, cnt);
	last = ZmR32(m, a0 + 8u) + (cnt << 7);
	Z9b_DestroyElem(c, last);
	return a1;
}

// Finds the first word equal to key in the word vector at vec: the address of the match, or the end.
static uint32_t Z9b_FindWord(ExtZpMem *m, uint32_t vec, uint32_t key)
{
	uint32_t p = ZmR32(m, vec + 8u);
	uint32_t end = p + (ZmR32(m, vec + 4u) << 2);
	while (p != end)
	{
		if (ZmR32(m, p) == key)
		{
			break;
		}
		p += 4u;
	}
	return p;
}

// Removes every word equal to key from the word vector at vec (the search starts again from the front after each erase).
static void Z9b_RemoveWords(ExtZpCtx *c, uint32_t vec, uint32_t key)
{
	ExtZpMem *m = c->m;
	uint32_t a1 = Z9b_FindWord(m, vec, key);
	while (a1 != ZmR32(m, vec + 8u) + (ZmR32(m, vec + 4u) << 2))
	{
		(void)Z9b_Erase4(c, vec, a1);
		a1 = Z9b_FindWord(m, vec, key);
	}
}

// 0x36f5a0: removes every pair of the pair vector that holds key in either word (the original's a0 is not used).
static void Z9b_RemovePairsHolding(ExtZpCtx *c, uint32_t vec, uint32_t key)
{
	ExtZpMem *m = c->m;
	uint32_t a1 = ZmR32(m, vec + 8u);
	while (a1 != ZmR32(m, vec + 8u) + (ZmR32(m, vec + 4u) << 3))
	{
		if (ZmR32(m, a1) == key || ZmR32(m, a1 + 4u) == key)
		{
			a1 = Z9b_Erase8(c, vec, a1);
		}
		else
		{
			a1 += 8u;
		}
	}
}

// The inline pair loops of 0x36edc0 (a pair is removed when either word is key; the loop ends at a1 >= end).
static void Z9b_RemovePairsInline(ExtZpCtx *c, uint32_t vec, uint32_t key)
{
	ExtZpMem *m = c->m;
	uint32_t a1 = ZmR32(m, vec + 8u);
	while (a1 < ZmR32(m, vec + 8u) + (ZmR32(m, vec + 4u) << 3))
	{
		if (ZmR32(m, a1) == key || ZmR32(m, a1 + 4u) == key)
		{
			a1 = Z9b_Erase8(c, vec, a1);
		}
		else
		{
			a1 += 8u;
		}
	}
}

// Removes every 12-byte record whose body (+8) is key.
static void Z9b_RemoveRecords(ExtZpCtx *c, uint32_t vec, uint32_t key)
{
	ExtZpMem *m = c->m;
	uint32_t a1 = ZmR32(m, vec + 8u);
	while (a1 < ZmR32(m, vec + 8u) + ZmR32(m, vec + 4u) * 12u)
	{
		if (ZmR32(m, a1 + 8u) == key)
		{
			a1 = Z9b_Erase12(c, vec, a1);
		}
		else
		{
			a1 += 0xCu;
		}
	}
}

// Bit 0 of the flag byte of the body's definition (body + 0x18 -> definition, byte +0x18).
static int Z9b_StaticBit(ExtZpMem *m, uint32_t body)
{
	uint32_t def = ZmR32(m, body + 0x18u);
	return (int)(ZmR8(m, def + 0x18u) & 1u);
}

// Takes the body a1 out of the physics system a0: its entries in the system's body lists, pair lists,
// definition lists, contact and joint records go, and the system's entry in the body's own list goes.
void ExtZp_Integ_36edc0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	uint32_t s2 = a0, s1 = a1, s0, p, end;

	ZmW8(m, s2 + Z9B_SYS_DIRTY, 0u);
	if (Z9b_StaticBit(m, s1))
	{
		Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_STATIC, s1);
	}
	else
	{
		Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_DYN_A, s1);
		Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_DYN_B, s1);
	}
	Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_W80, s1);
	Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_W8C, s1);
	Z9b_RemovePairsInline(c, s2 + Z9B_SYS_LIST_P5C, s1);
	Z9b_RemovePairsInline(c, s2 + Z9B_SYS_LIST_P98, s1);
	Z9b_RemovePairsInline(c, s2 + Z9B_SYS_LIST_PB0, s1);
	Z9b_RemovePairsInline(c, s2 + Z9B_SYS_LIST_PBC, s1);

	// the body's definition out of the definition lists
	s0 = ZmR32(m, s1 + 0x18u);
	Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_DEF_A, s0);
	Z9b_RemoveWords(c, s2 + Z9B_SYS_LIST_DEF_B, s0);

	if (Z9b_StaticBit(m, s1))
	{
		Z9b_RemoveRecords(c, s2 + Z9B_SYS_LIST_R1B8, s1);
	}
	else
	{
		Z9b_RemoveRecords(c, s2 + Z9B_SYS_LIST_R1A0, s1);
		Z9b_RemoveRecords(c, s2 + Z9B_SYS_LIST_R1AC, s1);
	}

	// contact records (0x80 bytes): the body is one of the two bodies at +8 / +0xC
	p = ZmR32(m, s2 + Z9B_SYS_LIST_C128 + 8u);
	while (p != ZmR32(m, s2 + Z9B_SYS_LIST_C128 + 8u) + (ZmR32(m, s2 + Z9B_SYS_LIST_C128 + 4u) << 7))
	{
		if (ZmR32(m, p + 8u) == s1 || ZmR32(m, p + 0xCu) == s1)
		{
			p = Z9b_EraseContact(c, s2 + Z9B_SYS_LIST_C128, p);
		}
		else
		{
			p += 0x80u;
		}
	}

	Z9b_RemovePairsHolding(c, s2 + Z9B_SYS_LIST_PEC, s1);
	Z9b_RemovePairsHolding(c, s2 + Z9B_SYS_LIST_PC8, s1);
	Z9b_RemovePairsHolding(c, s2 + Z9B_SYS_LIST_PE0, s1);

	// the pairs of the table at +0xD4 are cleared in place (the size stays)
	p = ZmR32(m, s2 + Z9B_SYS_TABLE_D4 + 8u);
	end = p + (ZmR32(m, s2 + Z9B_SYS_TABLE_D4 + 4u) << 3);
	if (p != end)
	{
		do
		{
			if (ZmR32(m, p) == s1)
			{
				ZmW32(m, p, 0u);
			}
			else if (ZmR32(m, p + 4u) == s1)
			{
				ZmW32(m, p + 4u, 0u);
			}
			p += 8u;
		} while (p != end);
	}

	// the system out of the body's own list (pairs at body + 8, the system in the first word)
	p = ZmR32(m, s1 + 0x10u);
	while (p < ZmR32(m, s1 + 0x10u) + (ZmR32(m, s1 + 0xCu) << 3))
	{
		if (ZmR32(m, p) == s2)
		{
			p = Z9b_Erase8(c, s1 + 8u, p);
		}
		else
		{
			p += 8u;
		}
	}
}
