// THE SECOND CONSOLE'S SINGLE PRECISION ARITHMETIC (see include/platform/native_ps2float.h).
//
// Integer code on the raw patterns; the host's floating point is used nowhere
// except in Ps2F_ToDouble / Ps2F_FromHostFloat (bit casts, no arithmetic).
//
// SOURCES of the rules (public): the console's CPU manuals (no Inf/NaN,
// +-Fmax on overflow, zero for denormals, only round toward zero), and the
// emulator community's soft-float research on the real hardware (PCSX2 pull
// request 12001, "Initial Interpreter Implementation of Ps2's floating point
// unit specification": the adder's pre-alignment cut, the Booth multiplier's
// dropped carry-save bits, the SRT divider and square root of 25 steps without
// remainder correction, the multiply-add overflow rules). This file is written
// from those descriptions; it contains no data of any game.

#include <platform/native_ps2float.h>

#include <stdio.h>
#include <string.h>

static unsigned Ps2F_Exp(Ps2F a) { return (a >> 23) & 0xFFu; }
static uint32_t Ps2F_Man(Ps2F a) { return a & 0x7FFFFFu; }
static unsigned Ps2F_SignBit(Ps2F a) { return a >> 31; }
static Ps2F Ps2F_Make(unsigned sign, unsigned exp, uint32_t man)
{
	return ((Ps2F)(sign & 1u) << 31) | ((Ps2F)(exp & 0xFFu) << 23) | (man & 0x7FFFFFu);
}
static void Ps2F_Raise(unsigned *flags, unsigned f)
{
	if (flags)
		*flags |= f;
}

// ---------------------------------------------------------------- add / sub

// The adder proper: x has the larger (or equal) exponent. Both operands as
// signed 24-bit mantissas with 6 bits below, the smaller one shifted right by
// the exponent difference; the exact sum is normalised and cut to 24 bits.
static Ps2F Ps2F_AddAligned(Ps2F x, Ps2F y, unsigned *flags)
{
	int ex = (int)Ps2F_Exp(x);
	int diff = ex - (int)Ps2F_Exp(y);
	if (diff < 0)
	{
		Ps2F t = x;
		x = y;
		y = t;
		ex = (int)Ps2F_Exp(x);
		diff = -diff;
	}
	if (diff >= 25)
		return x;

	int32_t mx = (int32_t)(Ps2F_Man(x) | 0x800000u);
	int32_t my = (int32_t)(Ps2F_Man(y) | 0x800000u);
	if (Ps2F_SignBit(x))
		mx = -mx;
	if (Ps2F_SignBit(y))
		my = -my;
	// (my * 64) >> diff: arithmetic shift; the bits it drops are zero after
	// the pre-alignment cut in Ps2F_AddEx (at most diff - 6 bits fall out,
	// all of them below the kept guard bit).
	int32_t sum = mx * 64 + ((my * 64) >> diff);
	if (sum == 0)
		return 0u;
	unsigned sign = sum < 0 ? 1u : 0u;
	uint32_t mag = (uint32_t)(sum < 0 ? -sum : sum);

	int top = 31;
	while (!(mag & (1u << top)))
		top--;
	int exp = ex - 6 + (top - 23);
	uint32_t man = top >= 23 ? (mag >> (top - 23)) : (mag << (23 - top));

	if (exp > 255)
	{
		Ps2F_Raise(flags, PS2F_FLAG_OVERFLOW);
		return sign ? PS2F_FMAX_NEG : PS2F_FMAX;
	}
	if (exp < 1)
	{
		Ps2F_Raise(flags, PS2F_FLAG_UNDERFLOW);
		return Ps2F_Make(sign, 0, 0);
	}
	return Ps2F_Make(sign, (unsigned)exp, man);
}

static Ps2F Ps2F_AddSub(Ps2F a, Ps2F b, int sub, unsigned *flags)
{
	unsigned ea = Ps2F_Exp(a), eb = Ps2F_Exp(b);
	if (ea == 0 || eb == 0)
	{
		// A zero/denormal operand: the other one passes unchanged, with the
		// sign of the larger magnitude (b negated for a subtraction).
		uint32_t absA = a & PS2F_FMAX, absB = b & PS2F_FMAX;
		unsigned signB = sub ? (Ps2F_SignBit(b) ^ 1u) : Ps2F_SignBit(b);
		unsigned sign = absA >= absB ? Ps2F_SignBit(a) : signB;
		if (ea == 0 && eb != 0)
			return Ps2F_Make(sign, eb, Ps2F_Man(b));
		if (ea != 0 && eb == 0)
			return Ps2F_Make(sign, ea, Ps2F_Man(a));
		// both zero: -0 only for (-0) + (-0) and (-0) - (+0)
		return (Ps2F_SignBit(a) && signB) ? PS2F_SIGN : 0u;
	}
	// The pre-alignment cut: the operand with the smaller exponent keeps no
	// bit below the one that ends up just under the larger operand's last bit.
	int diff = (int)ea - (int)eb;
	if (diff > 0 && diff < 25)
		b &= 0xFFFFFFFFu << (diff - 1);
	else if (diff < 0 && diff > -25)
		a &= 0xFFFFFFFFu << (-diff - 1);
	if (sub)
		b ^= PS2F_SIGN;
	return Ps2F_AddAligned(a, b, flags);
}

Ps2F Ps2F_AddEx(Ps2F a, Ps2F b, unsigned *flags) { return Ps2F_AddSub(a, b, 0, flags); }
Ps2F Ps2F_SubEx(Ps2F a, Ps2F b, unsigned *flags) { return Ps2F_AddSub(a, b, 1, flags); }
Ps2F Ps2F_Add(Ps2F a, Ps2F b) { return Ps2F_AddSub(a, b, 0, 0); }
Ps2F Ps2F_Sub(Ps2F a, Ps2F b) { return Ps2F_AddSub(a, b, 1, 0); }

// ---------------------------------------------------------------- multiply

// One radix-4 Booth partial product of the multiplicand m for digit group
// `group` of the multiplier r (bits 2g+1, 2g, 2g-1): 0, +-m, +-2m, shifted by
// 2g; a negative one is the one's complement above the shift, its +1 is
// returned in *carryIn (bit 2g).
static uint32_t Ps2F_BoothTerm(uint32_t m, uint32_t r, unsigned group, uint32_t *carryIn)
{
	uint32_t window = (group ? (r >> (group * 2 - 1)) : (r << 1)) & 7u;
	uint32_t term = m << (group * 2);
	uint32_t at = 1u << (group * 2);
	if (window == 3 || window == 4)
		term += term;
	int negative = window >= 4 && window <= 6;
	if (negative)
		term ^= (uint32_t)(0u - at); // invert bit 2g and up
	if (window == 0 || window == 7)
		term = 0;
	*carryIn = negative ? at : 0u;
	return term;
}

// Carry-save step: three words in, sum and carry (shifted) out.
static void Ps2F_Csa3(uint32_t a, uint32_t b, uint32_t c, uint32_t *sum, uint32_t *carry)
{
	uint32_t half = a ^ b;
	*sum = half ^ c;
	*carry = ((half & c) | (a & b)) << 1;
}

// The 48-bit product of two 24-bit mantissas as the hardware forms it: the true
// product, except bit 15, which comes from the low half of the carry-save tree
// with some low bits dropped; where that bit differs, 2^15 is taken away.
static uint64_t Ps2F_MulMantissa(uint32_t m, uint32_t r)
{
	uint64_t exact = (uint64_t)m * (uint64_t)r;
	uint32_t t[8], c[8];
	for (unsigned g = 0; g < 8; g++)
		t[g] = Ps2F_BoothTerm(m, r, g, &c[g]);

	uint32_t s0, k0, s1, k1, s2, k2, s3, k3, s4, k4, s5, k5;
	// cycle 1
	Ps2F_Csa3(t[1], t[2], t[3], &s0, &k0);
	Ps2F_Csa3(t[4] & ~0x7FFu, t[5] & ~0xFFFu, t[6], &s1, &k1);
	k1 |= c[6] | (t[5] & 0x800u); // carry-ins squeezed into free bits
	uint32_t t7 = t[7] | ((t[5] & 0x400u) + c[5]);
	// cycle 2
	Ps2F_Csa3(t[0], s0, k0, &s2, &k2);
	Ps2F_Csa3(t7, s1, k1, &s3, &k3);
	// cycle 3
	Ps2F_Csa3(k2, s3, k3, &s4, &k4);
	// cycle 4
	Ps2F_Csa3(s2, s4, k4, &s5, &k5);
	k5 += c[7];
	s5 &= ~0x7FFFu;
	k5 &= ~0x7FFFu;
	uint32_t low = s5 + k5;
	return exact - (uint64_t)((low ^ (uint32_t)exact) & 0x8000u);
}

Ps2F Ps2F_MulEx(Ps2F a, Ps2F b, unsigned *flags)
{
	unsigned sign = Ps2F_SignBit(a) ^ Ps2F_SignBit(b);
	if (Ps2F_Exp(a) == 0 || Ps2F_Exp(b) == 0)
		return Ps2F_Make(sign, 0, 0);
	int exp = (int)Ps2F_Exp(a) + (int)Ps2F_Exp(b) - 127;
	uint32_t man = (uint32_t)(Ps2F_MulMantissa(Ps2F_Man(a) | 0x800000u, Ps2F_Man(b) | 0x800000u) >> 23);
	if (man > 0xFFFFFFu)
	{
		man >>= 1;
		exp++;
	}
	if (exp > 255)
	{
		Ps2F_Raise(flags, PS2F_FLAG_OVERFLOW);
		return sign ? PS2F_FMAX_NEG : PS2F_FMAX;
	}
	if (exp < 1)
	{
		Ps2F_Raise(flags, PS2F_FLAG_UNDERFLOW);
		return Ps2F_Make(sign, 0, 0);
	}
	return Ps2F_Make(sign, (unsigned)exp, man);
}
Ps2F Ps2F_Mul(Ps2F a, Ps2F b) { return Ps2F_MulEx(a, b, 0); }

// ---------------------------------------------------------------- divide, square root

// The SRT digit selection on the carry-save remainder (point between bits 24
// and 25): +1 from 1/4 up, -1 below -1/2, else 0.
static int Ps2F_SrtDigit(uint32_t sum, uint32_t carry)
{
	const uint32_t low = (1u << 24) - 1u;
	int32_t est = (int32_t)(((sum & ~low) + carry) | (sum & low));
	if (est >= (1 << 23))
		return 1;
	if (est < (int32_t)(0xFFFFFFFFu << 24))
		return -1;
	return 0;
}

Ps2F Ps2F_DivEx(Ps2F a, Ps2F b, unsigned *flags)
{
	unsigned sign = Ps2F_SignBit(a) ^ Ps2F_SignBit(b);
	unsigned ea = Ps2F_Exp(a), eb = Ps2F_Exp(b);
	if (ea == 0 && eb != 0)
		return Ps2F_Make(sign, 0, 0);
	if (eb == 0)
	{
		Ps2F_Raise(flags, ea == 0 ? PS2F_FLAG_INVALID : PS2F_FLAG_DIVZERO);
		return sign ? PS2F_FMAX_NEG : PS2F_FMAX;
	}
	uint32_t divisor = (Ps2F_Man(b) | 0x800000u) << 2;
	uint32_t sum = (Ps2F_Man(a) | 0x800000u) << 2, carry = 0;
	uint32_t quot = 0;
	int digit = 1;
	for (int i = 0; i < 25; i++)
	{
		quot = (quot << 1) + (uint32_t)digit;
		uint32_t addend = digit > 0 ? ~divisor : (digit < 0 ? divisor : 0u);
		if (digit > 0)
			carry += 1u;
		uint32_t ns, nc;
		Ps2F_Csa3(sum, carry, addend, &ns, &nc);
		digit = digit ? Ps2F_SrtDigit(ns, nc) : Ps2F_SrtDigit(sum, carry);
		sum = ns << 1;
		carry = nc << 1;
	}
	int exp = (int)ea - (int)eb + 126;
	if (quot >= (1u << 24))
	{
		exp++;
		quot >>= 1;
	}
	if (exp > 255)
	{
		Ps2F_Raise(flags, PS2F_FLAG_OVERFLOW);
		return sign ? PS2F_FMAX_NEG : PS2F_FMAX;
	}
	if (exp < 1)
	{
		Ps2F_Raise(flags, PS2F_FLAG_UNDERFLOW);
		return Ps2F_Make(sign, 0, 0);
	}
	return Ps2F_Make(sign, (unsigned)exp, quot);
}
Ps2F Ps2F_Div(Ps2F a, Ps2F b) { return Ps2F_DivEx(a, b, 0); }

Ps2F Ps2F_SqrtEx(Ps2F a, unsigned *flags)
{
	if (Ps2F_Exp(a) == 0)
	{
		if (Ps2F_SignBit(a))
			Ps2F_Raise(flags, PS2F_FLAG_INVALID);
		return 0u;
	}
	if (Ps2F_SignBit(a))
		Ps2F_Raise(flags, PS2F_FLAG_INVALID);
	uint32_t sum = (Ps2F_Man(a) | 0x800000u) << 1, carry = 0;
	if (!(a & 0x800000u)) // even biased exponent = odd true exponent
		sum <<= 1;
	uint32_t root = 0;
	int digit = 1;
	for (int i = 0; i < 25; i++)
	{
		// adding d*2^k to the root adds d*2^k*(2*root + d*2^k) to its square
		uint32_t adjust = root + (uint32_t)((int32_t)digit * (int32_t)(1u << (24 - i)));
		root += (uint32_t)((int32_t)digit * (int32_t)(1u << (25 - i)));
		uint32_t addend = digit > 0 ? ~adjust : (digit < 0 ? adjust : 0u);
		if (digit > 0)
			carry += 1u;
		uint32_t ns, nc;
		Ps2F_Csa3(sum, carry, addend, &ns, &nc);
		digit = digit ? Ps2F_SrtDigit(ns, nc) : Ps2F_SrtDigit(sum, carry);
		sum = ns << 1;
		carry = nc << 1;
	}
	unsigned exp = (Ps2F_Exp(a) + 127u) >> 1;
	return Ps2F_Make(0, exp, root >> 2);
}
Ps2F Ps2F_Sqrt(Ps2F a) { return Ps2F_SqrtEx(a, 0); }

Ps2F Ps2F_RsqrtEx(Ps2F a, Ps2F b, unsigned *flags)
{
	unsigned f = 0;
	Ps2F root = Ps2F_SqrtEx(b & PS2F_FMAX, &f);
	Ps2F out = Ps2F_DivEx(a, root, &f);
	if (Ps2F_SignBit(b) && Ps2F_Exp(b) != 0)
		f |= PS2F_FLAG_INVALID;
	Ps2F_Raise(flags, f);
	return out;
}
Ps2F Ps2F_Rsqrt(Ps2F a, Ps2F b) { return Ps2F_RsqrtEx(a, b, 0); }

// ---------------------------------------------------------------- multiply-add

Ps2F Ps2F_MulAddEx(Ps2F acc, int accOverflow, Ps2F a, Ps2F b, int sub, unsigned *flags)
{
	unsigned mf = 0, af = 0;
	Ps2F prod = Ps2F_MulEx(a, b, &mf);
	Ps2F out = Ps2F_AddSub(acc, prod, sub, &af);
	int prodOverflow = (mf & PS2F_FLAG_OVERFLOW) != 0;
	// toward +Fmax: a positive product added, or a negative one subtracted
	int toMax = sub ? (Ps2F_SignBit(prod) != 0) : (Ps2F_SignBit(prod) == 0);
	if (!accOverflow)
	{
		if (prodOverflow)
		{
			out = toMax ? PS2F_FMAX : PS2F_FMAX_NEG;
			af = PS2F_FLAG_OVERFLOW;
		}
	}
	else if (!prodOverflow)
	{
		out = acc;
		af = PS2F_FLAG_OVERFLOW;
	}
	else
	{
		out = toMax ? PS2F_FMAX : PS2F_FMAX_NEG;
		af = PS2F_FLAG_OVERFLOW;
	}
	Ps2F_Raise(flags, af | (mf & PS2F_FLAG_UNDERFLOW));
	return out;
}
Ps2F Ps2F_MulAdd(Ps2F acc, Ps2F a, Ps2F b) { return Ps2F_MulAddEx(acc, 0, a, b, 0, 0); }
Ps2F Ps2F_MulSub(Ps2F acc, Ps2F a, Ps2F b) { return Ps2F_MulAddEx(acc, 0, a, b, 1, 0); }

// ---------------------------------------------------------------- sign, compare

Ps2F Ps2F_Abs(Ps2F a) { return a & PS2F_FMAX; }
Ps2F Ps2F_Neg(Ps2F a) { return a ^ PS2F_SIGN; }

int Ps2F_Compare(Ps2F a, Ps2F b)
{
	int32_t va = (int32_t)(a & PS2F_FMAX), vb = (int32_t)(b & PS2F_FMAX);
	if (Ps2F_SignBit(a))
		va = -va;
	if (Ps2F_SignBit(b))
		vb = -vb;
	return va < vb ? -1 : (va > vb ? 1 : 0);
}
int Ps2F_Eq(Ps2F a, Ps2F b) { return Ps2F_Compare(a, b) == 0; }
int Ps2F_Lt(Ps2F a, Ps2F b) { return Ps2F_Compare(a, b) < 0; }
int Ps2F_Le(Ps2F a, Ps2F b) { return Ps2F_Compare(a, b) <= 0; }
Ps2F Ps2F_Min(Ps2F a, Ps2F b) { return Ps2F_Compare(a, b) <= 0 ? a : b; }
Ps2F Ps2F_Max(Ps2F a, Ps2F b) { return Ps2F_Compare(a, b) >= 0 ? a : b; }

// ---------------------------------------------------------------- conversions

Ps2F Ps2F_FromInt(int32_t i)
{
	if (i == 0)
		return 0u;
	unsigned sign = i < 0 ? 1u : 0u;
	uint32_t mag = sign ? (uint32_t)0u - (uint32_t)i : (uint32_t)i;
	int top = 31;
	while (!(mag & (1u << top)))
		top--;
	uint32_t man = top >= 23 ? (mag >> (top - 23)) : (mag << (23 - top));
	return Ps2F_Make(sign, (unsigned)(127 + top), man);
}

// value * 2^fracBits cut toward zero; saturates from 2^31 up (an exponent
// field of 158 or more after the scale).
int32_t Ps2F_ToFixed(Ps2F a, int fracBits)
{
	int exp = (int)Ps2F_Exp(a);
	if (exp == 0)
		return 0;
	int e = exp - 127 + fracBits; // value = 1.m * 2^e
	if (e >= 31)
		return Ps2F_SignBit(a) ? (int32_t)0x80000000u : (int32_t)0x7FFFFFFF;
	if (e < 0)
		return 0;
	uint32_t man = Ps2F_Man(a) | 0x800000u;
	uint32_t mag = e >= 23 ? (man << (e - 23)) : (man >> (23 - e));
	return Ps2F_SignBit(a) ? (int32_t)(0u - mag) : (int32_t)mag;
}
int32_t Ps2F_ToInt(Ps2F a) { return Ps2F_ToFixed(a, 0); }

Ps2F Ps2F_FromFixed(int32_t i, int fracBits)
{
	Ps2F f = Ps2F_FromInt(i);
	if (f == 0u || fracBits == 0)
		return f;
	int exp = (int)Ps2F_Exp(f) - fracBits;
	if (exp < 1)
		return f & PS2F_SIGN;
	return (f & ~(0xFFu << 23)) | ((Ps2F)exp << 23);
}

double Ps2F_ToDouble(Ps2F a)
{
	unsigned exp = Ps2F_Exp(a);
	if (exp == 0)
		return Ps2F_SignBit(a) ? -0.0 : 0.0;
	uint64_t bits = ((uint64_t)Ps2F_SignBit(a) << 63) | ((uint64_t)(exp - 127u + 1023u) << 52) | ((uint64_t)Ps2F_Man(a) << 29);
	double d;
	memcpy(&d, &bits, sizeof d);
	return d;
}

Ps2F Ps2F_FromHostFloat(float f)
{
	Ps2F a;
	memcpy(&a, &f, sizeof a);
	if (Ps2F_Exp(a) == 255)
		return (a & PS2F_SIGN) | PS2F_FMAX;
	if (Ps2F_Exp(a) == 0)
		return a & PS2F_SIGN;
	return a;
}

// ---------------------------------------------------------------- vector unit (macro mode)

#define PS2V_LANE(mask, i) ((mask) & (8u >> (i)))

void Ps2V_Add(Ps2Vec4 *d, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask)
{
	Ps2Vec4 r = *d;
	for (int i = 0; i < 4; i++)
		if (PS2V_LANE(mask, i))
			r.v[i] = Ps2F_Add(a->v[i], b->v[i]);
	*d = r;
}
void Ps2V_Sub(Ps2Vec4 *d, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask)
{
	Ps2Vec4 r = *d;
	for (int i = 0; i < 4; i++)
		if (PS2V_LANE(mask, i))
			r.v[i] = Ps2F_Sub(a->v[i], b->v[i]);
	*d = r;
}
void Ps2V_Mul(Ps2Vec4 *d, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask)
{
	Ps2Vec4 r = *d;
	for (int i = 0; i < 4; i++)
		if (PS2V_LANE(mask, i))
			r.v[i] = Ps2F_Mul(a->v[i], b->v[i]);
	*d = r;
}
void Ps2V_MulAdd(Ps2Vec4 *d, const Ps2Vec4 *acc, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask)
{
	Ps2Vec4 r = *d;
	for (int i = 0; i < 4; i++)
		if (PS2V_LANE(mask, i))
			r.v[i] = Ps2F_MulAdd(acc->v[i], a->v[i], b->v[i]);
	*d = r;
}
void Ps2V_MulSub(Ps2Vec4 *d, const Ps2Vec4 *acc, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask)
{
	Ps2Vec4 r = *d;
	for (int i = 0; i < 4; i++)
		if (PS2V_LANE(mask, i))
			r.v[i] = Ps2F_MulSub(acc->v[i], a->v[i], b->v[i]);
	*d = r;
}
void Ps2V_OpMula(Ps2Vec4 *acc, const Ps2Vec4 *fs, const Ps2Vec4 *ft)
{
	Ps2Vec4 r = *acc;
	r.v[0] = Ps2F_Mul(fs->v[1], ft->v[2]);
	r.v[1] = Ps2F_Mul(fs->v[2], ft->v[0]);
	r.v[2] = Ps2F_Mul(fs->v[0], ft->v[1]);
	*acc = r;
}
void Ps2V_OpMsub(Ps2Vec4 *d, const Ps2Vec4 *acc, const Ps2Vec4 *fs, const Ps2Vec4 *ft)
{
	Ps2Vec4 r = *d;
	r.v[0] = Ps2F_MulSub(acc->v[0], fs->v[1], ft->v[2]);
	r.v[1] = Ps2F_MulSub(acc->v[1], fs->v[2], ft->v[0]);
	r.v[2] = Ps2F_MulSub(acc->v[2], fs->v[0], ft->v[1]);
	*d = r;
}

// ---------------------------------------------------------------- selftest

static void Ps2F_Check(int ok, const char *what, uint32_t got, uint32_t want, int *checks, int *failures, void (*log)(const char *line))
{
	(*checks)++;
	if (!ok)
	{
		(*failures)++;
		if (log)
		{
			char line[160];
			snprintf(line, sizeof line, "ps2float FAIL %s: got %08x want %08x", what, (unsigned)got, (unsigned)want);
			log(line);
		}
	}
}
#define PS2F_EXPECT(what, got, want) do { uint32_t g_ = (uint32_t)(got), w_ = (uint32_t)(want); Ps2F_Check(g_ == w_, what, g_, w_, checks, failures, log); } while (0)

// Reference vectors: a, b, c, then a+b, a-b, a*b, a/b, sqrt(a), c+a*b, c-a*b,
// computed offline from the published description of the hardware (not by
// this file), mixed exponents and signs, edge patterns among them.
static const uint32_t s_ps2fVectors[][10] = {
#include "native_ps2float_vectors.inc"
};

void Ps2F_Selftest(int *checks, int *failures, void (*log)(const char *line))
{
	// Hand-computed cases.
	PS2F_EXPECT("1+1", Ps2F_Add(0x3F800000u, 0x3F800000u), 0x40000000u);
	PS2F_EXPECT("1.5*1.5", Ps2F_Mul(0x3FC00000u, 0x3FC00000u), 0x40100000u);
	PS2F_EXPECT("sqrt 4", Ps2F_Sqrt(0x40800000u), 0x40000000u);
	PS2F_EXPECT("6/2", Ps2F_Div(0x40C00000u, 0x40000000u), 0x40400000u);
	// Pre-alignment cut: 1 - 2^-24*(1+2^-23): b's exponent is 24 below, its
	// low 23 bits go, 1 - 2^-24 = 0x3F7FFFFF is exact (IEEE toward zero:
	// 0x3F7FFFFE).
	PS2F_EXPECT("align cut", Ps2F_Add(0x3F800000u, 0xB3800001u), 0x3F7FFFFFu);
	// Toward zero: 1 + 3*2^-24 = 1 + 2^-23 + 2^-24 -> 1 + 2^-23.
	PS2F_EXPECT("cut toward zero", Ps2F_Add(0x3F800000u, 0x34400000u), 0x3F800001u);
	// Exponent difference 25 and more: the larger operand unchanged.
	PS2F_EXPECT("far apart", Ps2F_Add(0x3F800000u, 0x33000000u), 0x3F800000u);
	// Exact cancellation is +0, also for negative operands.
	PS2F_EXPECT("cancel", Ps2F_Sub(0xBF800000u, 0xBF800000u), 0x00000000u);
	// No infinity: 0x7F800000 is 2^128, an ordinary number.
	PS2F_EXPECT("exp 255 is a number", Ps2F_Mul(0x7F800000u, 0x3F800000u), 0x7F800000u);
	PS2F_EXPECT("exp 255 half", Ps2F_Mul(0x7F800000u, 0x3F000000u), 0x7F000000u);
	// Overflow -> +-Fmax.
	PS2F_EXPECT("add overflow", Ps2F_Add(PS2F_FMAX, PS2F_FMAX), PS2F_FMAX);
	PS2F_EXPECT("mul overflow neg", Ps2F_Mul(0x7F000000u, 0xC0800000u), PS2F_FMAX_NEG);
	{
		unsigned f = 0;
		Ps2F_MulEx(0x7F000000u, 0x7F000000u, &f);
		PS2F_EXPECT("overflow flag", f & PS2F_FLAG_OVERFLOW, PS2F_FLAG_OVERFLOW);
	}
	// Denormals are zero; underflow -> signed zero.
	PS2F_EXPECT("denormal operand", Ps2F_Mul(0x00000001u, 0x3F800000u), 0x00000000u);
	PS2F_EXPECT("denormal operand sign", Ps2F_Mul(0x80400000u, 0x3F800000u), 0x80000000u);
	PS2F_EXPECT("denormal + x", Ps2F_Add(0x00400000u, 0xBF800000u), 0xBF800000u);
	PS2F_EXPECT("mul underflow", Ps2F_Mul(0x00800000u, 0x3F000000u), 0x00000000u);
	PS2F_EXPECT("-0 + -0", Ps2F_Add(0x80000000u, 0x80000000u), 0x80000000u);
	PS2F_EXPECT("-0 - +0", Ps2F_Sub(0x80000000u, 0x00000000u), 0x80000000u);
	PS2F_EXPECT("+0 + -0", Ps2F_Add(0x00000000u, 0x80000000u), 0x00000000u);
	// Division by zero -> +-Fmax.
	PS2F_EXPECT("x/0", Ps2F_Div(0xBF800000u, 0x00000000u), PS2F_FMAX_NEG);
	PS2F_EXPECT("0/x", Ps2F_Div(0x00000000u, 0xBF800000u), 0x80000000u);
	PS2F_EXPECT("0/0", Ps2F_Div(0x00000000u, 0x00000000u), PS2F_FMAX);
	// The divider without remainder correction: 1/3 ends one above the cut
	// quotient 0x3EAAAAAA.
	PS2F_EXPECT("1/3", Ps2F_Div(0x3F800000u, 0x40400000u), 0x3EAAAAABu);
	PS2F_EXPECT("sqrt -4", Ps2F_Sqrt(0xC0800000u), 0x40000000u);
	// Multiply-add: product overflow forces +-Fmax.
	PS2F_EXPECT("madd overflow", Ps2F_MulAdd(0x3F800000u, 0x7F000000u, 0x7F000000u), PS2F_FMAX);
	PS2F_EXPECT("msub overflow", Ps2F_MulSub(0x3F800000u, 0x7F000000u, 0x7F000000u), PS2F_FMAX_NEG);
	PS2F_EXPECT("madd 1+2*3", Ps2F_MulAdd(0x3F800000u, 0x40000000u, 0x40400000u), 0x40E00000u);
	// Compare, min/max: sign-magnitude, +0 == -0.
	PS2F_EXPECT("cmp +0 -0", Ps2F_Eq(0x00000000u, 0x80000000u), 1);
	PS2F_EXPECT("cmp -1 < 0", Ps2F_Lt(0xBF800000u, 0x00000000u), 1);
	PS2F_EXPECT("cmp Fmax > 2^128", Ps2F_Lt(0x7F800000u, PS2F_FMAX), 1);
	PS2F_EXPECT("max", Ps2F_Max(0xC0000000u, 0x3F800000u), 0x3F800000u);
	PS2F_EXPECT("min", Ps2F_Min(0xC0000000u, 0x3F800000u), 0xC0000000u);
	// Conversions.
	PS2F_EXPECT("cvt.s.w 16777217", Ps2F_FromInt(16777217), 0x4B800000u);
	PS2F_EXPECT("cvt.s.w -3", Ps2F_FromInt(-3), 0xC0400000u);
	PS2F_EXPECT("cvt.w.s -2.75", (uint32_t)Ps2F_ToInt(0xC0300000u), (uint32_t)-2);
	PS2F_EXPECT("cvt.w.s 2^31", (uint32_t)Ps2F_ToInt(0x4F000000u), 0x7FFFFFFFu);
	PS2F_EXPECT("cvt.w.s -2^31", (uint32_t)Ps2F_ToInt(0xCF000000u), 0x80000000u);
	PS2F_EXPECT("cvt.w.s exp 255", (uint32_t)Ps2F_ToInt(0x7F800000u), 0x7FFFFFFFu);
	PS2F_EXPECT("ftoi4 1.5", (uint32_t)Ps2F_ToFixed(0x3FC00000u, 4), 24u);
	PS2F_EXPECT("itof12 4096", Ps2F_FromFixed(4096, 12), 0x3F800000u);
	{
		uint32_t negInf = 0xFF800000u;
		float f;
		memcpy(&f, &negInf, sizeof f);
		PS2F_EXPECT("host inf", Ps2F_FromHostFloat(f), PS2F_FMAX_NEG);
	}
	PS2F_EXPECT("to double", Ps2F_ToDouble(0x7FFFFFFFu) > 6.8e38 ? 1u : 0u, 1u);
	// Vector unit: cross product (1,0,0) x (0,1,0) = (0,0,1), w kept.
	{
		Ps2Vec4 x = {{0x3F800000u, 0, 0, 0}}, y = {{0, 0x3F800000u, 0, 0}};
		Ps2Vec4 acc = {{0, 0, 0, 0}}, d = {{1u, 2u, 3u, 0x12345678u}};
		Ps2V_OpMula(&acc, &x, &y);
		Ps2V_OpMsub(&d, &acc, &y, &x);
		PS2F_EXPECT("cross x", d.v[0], 0u);
		PS2F_EXPECT("cross y", d.v[1], 0u);
		PS2F_EXPECT("cross z", d.v[2], 0x3F800000u);
		PS2F_EXPECT("cross w kept", d.v[3], 0x12345678u);
		Ps2Vec4 m = {{0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u}};
		Ps2V_Mul(&d, &m, &m, 0xAu); // x and z only
		PS2F_EXPECT("vmul mask x", d.v[0], 0x40800000u);
		PS2F_EXPECT("vmul mask y kept", d.v[1], 0u);
		PS2F_EXPECT("vmul mask z", d.v[2], 0x40800000u);
	}

	// Reference vectors.
	int bad = 0;
	for (size_t i = 0; i < sizeof s_ps2fVectors / sizeof s_ps2fVectors[0]; i++)
	{
		const uint32_t *v = s_ps2fVectors[i];
		uint32_t got[7] = {Ps2F_Add(v[0], v[1]), Ps2F_Sub(v[0], v[1]), Ps2F_Mul(v[0], v[1]), Ps2F_Div(v[0], v[1]),
		                   Ps2F_Sqrt(v[0]), Ps2F_MulAdd(v[2], v[0], v[1]), Ps2F_MulSub(v[2], v[0], v[1])};
		static const char *const names[7] = {"add", "sub", "mul", "div", "sqrt", "madd", "msub"};
		for (int k = 0; k < 7; k++)
		{
			char what[64];
			snprintf(what, sizeof what, "vector %u %s", (unsigned)i, names[k]);
			if (got[k] != v[3 + k])
				bad++;
			if (bad <= 8 || got[k] == v[3 + k])
				Ps2F_Check(got[k] == v[3 + k], what, got[k], v[3 + k], checks, failures, log);
			else
			{
				(*checks)++;
				(*failures)++;
			}
		}
	}
}
