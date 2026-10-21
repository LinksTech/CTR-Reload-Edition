// THE ZONE PORT, PART E: the single precision math library of the source's
// executable (acos, cos, sin, their kernels), see include/platform/native_extzonephys.h
// for the conventions. Compiles alone too (offline tests).
//
// The constant tables (2/pi bits, n*pi/2 high words, pi/2 split in 11 parts, the
// per precision loop counts) are the public values of the usual freely
// distributed single precision library; every entry was compared with the bytes
// of the executable (see the offline test). A read outside a table goes to the
// port's memory at the original's address, as the original would read it.

#include <platform/native_extzonephys.h>
#include <platform/native_extzonephys_e.h>

#define MATH_G_VERSION 0x006A5E58u // global: the error handling mode (-1 = none, 2 = no errno)
#define MATH_G_IPIO2 0x006A57E0u   // table: bits of 2/pi (66 words)
#define MATH_G_NPIO2 0x006A5AF8u   // table: high words of n*pi/2 (32 words)
#define MATH_G_INITJK 0x006A5CE8u  // table: loop counts per precision
#define MATH_G_PIO2 0x006A5CF8u    // table: pi/2 in 11 parts of 24 bits (11 floats)
#define MATH_G_NAME 0x006A5F48u    // address of the function name record used by the error path

static const uint32_t s_ipio2[66] = {
    0xA2, 0xF9, 0x83, 0x6E, 0x4E, 0x44, 0x15, 0x29, 0xFC, 0x27, 0x57, 0xD1, 0xF5, 0x34, 0xDD, 0xC0, 0xDB, 0x62, 0x95, 0x99, 0x3C, 0x43, 0x90,
    0x41, 0xFE, 0x51, 0x63, 0xAB, 0xDE, 0xBB, 0xC5, 0x61, 0xB7, 0x24, 0x6E, 0x3A, 0x42, 0x4D, 0xD2, 0xE0, 0x06, 0x49, 0x2E, 0xEA, 0x09, 0xD1,
    0x92, 0x1C, 0xFE, 0x1D, 0xEB, 0x1C, 0xB1, 0x29, 0xA7, 0x3E, 0xE8, 0x82, 0x35, 0xF5, 0x2E, 0xBB, 0x44, 0x84, 0xE9, 0x9C};
static const uint32_t s_npio2[32] = {
    0x3FC90F00u, 0x40490F00u, 0x4096CB00u, 0x40C90F00u, 0x40FB5300u, 0x4116CB00u, 0x412FED00u, 0x41490F00u, 0x41623100u, 0x417B5300u, 0x418A3A00u,
    0x4196CB00u, 0x41A35C00u, 0x41AFED00u, 0x41BC7E00u, 0x41C90F00u, 0x41D5A000u, 0x41E23100u, 0x41EEC200u, 0x41FB5300u, 0x4203F200u, 0x420A3A00u,
    0x42108300u, 0x4216CB00u, 0x421D1400u, 0x42235C00u, 0x4229A500u, 0x422FED00u, 0x42363600u, 0x423C7E00u, 0x4242C700u, 0x42490F00u};
static const uint32_t s_initjk[4] = {4u, 7u, 9u, 0u};
static const uint32_t s_pio2[11] = {0x3FC90000u, 0x39F00000u, 0x37DA0000u, 0x33A20000u, 0x2E840000u, 0x2B500000u, 0x27C20000u, 0x22D00000u, 0x1FC40000u, 0x1BC60000u, 0x17440000u};

// A word of one of the original's constant tables (or of the port's memory).
static uint32_t MathTab(ExtZpCtx *c, uint32_t addr)
{
	if ((addr & 3u) == 0)
	{
		if (addr >= MATH_G_IPIO2 && addr < MATH_G_IPIO2 + 66u * 4u)
			return s_ipio2[(addr - MATH_G_IPIO2) >> 2];
		if (addr >= MATH_G_NPIO2 && addr < MATH_G_NPIO2 + 32u * 4u)
			return s_npio2[(addr - MATH_G_NPIO2) >> 2];
		if (addr >= MATH_G_INITJK && addr < MATH_G_INITJK + 4u * 4u)
			return s_initjk[(addr - MATH_G_INITJK) >> 2];
		if (addr >= MATH_G_PIO2 && addr < MATH_G_PIO2 + 11u * 4u)
			return s_pio2[(addr - MATH_G_PIO2) >> 2];
	}
	return ZmR32(c->m, addr);
}

// fabsf: clears the sign bit.
Ps2F ExtZp_Math_43b068(ExtZpCtx *c, Ps2F x)
{
	(void)c;
	return x & 0x7FFFFFFFu;
}

// isnanf: 1 when the magnitude pattern is above the infinity pattern.
uint32_t ExtZp_Math_43b198(ExtZpCtx *c, Ps2F x)
{
	(void)c;
	return (0x7F800000u - (x & 0x7FFFFFFFu)) >> 31;
}

// copysign(x, y): magnitude of x, sign of y.
Ps2F ExtZp_Math_43af50(ExtZpCtx *c, Ps2F x, Ps2F y)
{
	(void)c;
	return (x & 0x7FFFFFFFu) | (y & 0x80000000u);
}

// floorf.
Ps2F ExtZp_Math_43b0b0(ExtZpCtx *c, Ps2F x)
{
	int32_t a0 = (int32_t)x;
	int32_t a1 = ((a0 >> 23) & 0xFF) - 0x7F;
	int32_t a2, v0;
	Ps2F f0;
	(void)c;
	if (!(a1 < 0x17))
	{
		if (a1 != 0x80)
			return x;
		return Ps2F_Add(x, x);
	}
	if (a1 >= 0)
	{
		a2 = 0x007FFFFF >> (a1 & 31); // v0 = 0x7f0000 | 0xffff
		if ((a0 & a2) == 0)
			return x;
		f0 = Ps2F_Add(x, 0x7149F2CAu); // 1e30 (the original's constant)
		if (Ps2F_Lt(0u, f0))
		{
			if (a0 < 0)
				a0 += 0x00800000 >> (a1 & 31);
			a0 &= ~a2;
		}
		return (Ps2F)a0;
	}
	f0 = Ps2F_Add(x, 0x7149F2CAu); // 1e30 (the original's constant)
	if (Ps2F_Lt(0u, f0))
	{
		if (a0 >= 0)
			a0 = 0;
		else
		{
			v0 = a0 & 0x7FFFFFFF;
			if (v0 != 0)
				a0 = (int32_t)0xBF800000u; // -1
		}
	}
	return (Ps2F)a0;
}

// sqrtf: a bit-by-bit square root; zero, infinity and the negative case as the original.
Ps2F ExtZp_Math_437e60(ExtZpCtx *c, Ps2F x)
{
	int32_t a1 = (int32_t)x, a2, a0, t0, a3, v0;
	uint32_t v1, r;
	Ps2F f0;
	(void)c;
	if ((x & 0x7F800000u) == 0x7F800000u)
	{
		f0 = Ps2F_Mul(x, x);
		return Ps2F_Add(f0, x);
	}
	if (!(a1 > 0))
	{
		if ((a1 & 0x7FFFFFFF) == 0)
			return x;
		if (a1 >= 0) // not reachable (a1 > 0 or a1 <= 0 is decided above)
		{
		}
		else
		{
			f0 = Ps2F_Sub(x, x);
			return Ps2F_Div(f0, f0);
		}
	}
	a2 = a1 >> 23;
	if (a2 == 0)
	{
		a0 = 0;
		if ((a1 & 0x00800000) != 0)
			a2 = 1 - a0;
		else
		{
			do
			{
				a1 = (int32_t)((uint32_t)a1 << 1);
				a0++;
			} while ((a1 & 0x00800000) == 0);
			a2 = 1 - a0;
		}
	}
	a2 -= 0x7F;
	a0 = a2 & 1;
	a2 >>= 1;
	a1 = (a1 & 0x007FFFFF) | 0x00800000;
	a1 = (int32_t)((uint32_t)a1 << (a0 & 31));
	t0 = (int32_t)((uint32_t)a2 << 23);
	a1 = (int32_t)((uint32_t)a1 << 1);
	a3 = 0;
	a0 = 0;
	a2 = 0x01000000;
	do
	{
		v1 = (uint32_t)(a3 + a2);
		if (a1 < (int32_t)v1)
			a2 = (int32_t)((uint32_t)a2 >> 1);
		else
		{
			a1 -= (int32_t)v1;
			a3 = (int32_t)v1 + a2;
			a0 += a2;
			a2 = (int32_t)((uint32_t)a2 >> 1);
		}
		a1 = (int32_t)((uint32_t)a1 << 1);
	} while (a2 != 0);
	v0 = a0 & 1;
	if (a1 != 0)
		a0 += v0;
	a0 >>= 1;
	r = (uint32_t)(a0 + 0x3F000000) + (uint32_t)t0;
	return (Ps2F)r;
}

// scalbn(x, n): x * 2^n with the original's range handling (n in a0).
Ps2F ExtZp_Math_43b1c0(ExtZpCtx *c, uint32_t n, Ps2F x)
{
	int32_t a0 = (int32_t)n, a1 = (int32_t)x, a2 = a1, v1, v0;
	Ps2F f0, f1, f20;
	v1 = (a2 & 0x7F800000) >> 23;
	if (v1 == 0)
	{
		if ((a2 & 0x7FFFFFFF) == 0)
			return (Ps2F)a1;
		f1 = Ps2F_Mul((Ps2F)a1, 0x4C000000u); // 33554432 (the original's constant)
		a1 = (int32_t)f1;
		a2 = a1;
		v1 = (a1 & 0x7F800000) >> 23;
		v0 = (a0 < (int32_t)0xFFFF3CB0);
		v1 -= 0x19;
		if (v0)
			return Ps2F_Mul(f1, 0x0DA24260u); // 1e-30 (the original's constant)
	}
	{
		int32_t old = v1;
		v1 += a0;
		if (old == 255)
			return Ps2F_Add((Ps2F)a1, (Ps2F)a1);
	}
	if (v1 < 255)
	{
		if (v1 > 0)
		{
			a1 = (a2 & (int32_t)0x807FFFFF) | (int32_t)((uint32_t)v1 << 23);
			return (Ps2F)a1;
		}
		if (v1 < -24)
		{
			f20 = 0x0DA24260u; // 1e-30 (the original's constant)
			if (50000 < a0)
				f20 = 0x7149F2CAu; // 1e30 (the original's constant)
		}
		else
		{
			v1 += 0x19;
			a1 = (a2 & (int32_t)0x807FFFFF) | (int32_t)((uint32_t)v1 << 23);
			return Ps2F_Mul((Ps2F)a1, 0x33000000u); // 2.98023224e-08 (the original's constant)
		}
	}
	else
		f20 = 0x7149F2CAu; // 1e30 (the original's constant)
	f0 = ExtZp_Math_43af50(c, f20, (Ps2F)a1);
	return Ps2F_Mul(f0, f20);
}

// Kernel cos: cos(x + y) for |x| <= pi/4 (y = the tail of x).
Ps2F ExtZp_Math_438f28(ExtZpCtx *c, Ps2F x, Ps2F y)
{
	uint32_t ix = x & 0x7FFFFFFFu, v0;
	Ps2F f0, f1, f2, f3, f4, f6;
	(void)c;
	f6 = Ps2F_Mul(x, x);
	if (!(0x31FFFFFFu < (int32_t)ix))
	{
		if (Ps2F_ToInt(x) == 0)
			return 0x3F800000u; // 1
	}
	f0 = Ps2F_Mul(f6, 0xAD47D74Eu);                 // -1.13596476e-11 (the original's constant)
	f0 = Ps2F_Add(f0, 0x310F74F6u);                 // 2.08757234e-09
	f0 = Ps2F_Mul(f6, f0);
	f0 = Ps2F_Add(f0, 0xB493F27Cu);                 // -2.75573143e-07
	f0 = Ps2F_Mul(f6, f0);
	f0 = Ps2F_Add(f0, 0x37D00D01u);                 // 2.48015876e-05
	f0 = Ps2F_Mul(f6, f0);
	f0 = Ps2F_Add(f0, 0xBAB60B61u);                 // -0.00138888892
	f0 = Ps2F_Mul(f6, f0);
	f0 = Ps2F_Add(f0, 0x3D2AAAABu);                 // 0.0416666679
	f1 = Ps2F_Mul(f6, f0);
	if (!((int32_t)0x3E999999 < (int32_t)ix))       // 0x3e999999 (the original's constant)
	{
		f1 = Ps2F_Mul(f6, f1);
		f0 = Ps2F_Mul(f6, 0x3F000000u);             // 0.5
		f2 = Ps2F_Mul(x, y);
		f1 = Ps2F_Sub(f1, f2);
		f0 = Ps2F_Sub(f0, f1);
		return Ps2F_Sub(0x3F800000u, f0);           // 1
	}
	v0 = ((int32_t)0x3F480000 < (int32_t)ix) ? 0x3E900000u : ix + 0xFF000000u;
	f2 = Ps2F_Mul(f6, f1);
	f3 = Ps2F_Mul(x, y);
	f0 = Ps2F_Mul(f6, 0x3F000000u);                 // 0.5
	f4 = (Ps2F)v0;
	f2 = Ps2F_Sub(f2, f3);
	f0 = Ps2F_Sub(f0, f4);
	f1 = Ps2F_Sub(0x3F800000u, f4);                 // 1
	f0 = Ps2F_Sub(f0, f2);
	return Ps2F_Sub(f1, f0);
}

// Kernel sin: sin(x + y) for |x| <= pi/4 (iy = 0: y is zero and ignored).
Ps2F ExtZp_Math_4399d0(ExtZpCtx *c, uint32_t iy, Ps2F x, Ps2F y)
{
	uint32_t ix = x & 0x7FFFFFFFu;
	Ps2F f0, f1, f2, f5, f6;
	(void)c;
	f5 = Ps2F_Mul(x, x);
	if (!((int32_t)0x31FFFFFF < (int32_t)ix))
	{
		if (Ps2F_ToInt(x) == 0)
			return x;
	}
	f0 = Ps2F_Mul(f5, 0x2F2EC9D3u);                 // 1.58969102e-10 (the original's constant)
	f6 = Ps2F_Mul(f5, x);
	f0 = Ps2F_Add(f0, 0xB2D72F34u);                 // -2.50507597e-08
	f0 = Ps2F_Mul(f5, f0);
	f0 = Ps2F_Add(f0, 0x3638EF1Bu);                 // 2.75573143e-06
	f0 = Ps2F_Mul(f5, f0);
	f0 = Ps2F_Add(f0, 0xB9500D01u);                 // -0.000198412701
	f0 = Ps2F_Mul(f5, f0);
	f1 = Ps2F_Add(f0, 0x3C088889u);                 // 0.00833333377
	if (iy == 0)
	{
		f0 = Ps2F_Mul(f5, f1);
		f0 = Ps2F_Add(f0, 0xBE2AAAABu);             // -0.166666672
		f0 = Ps2F_Mul(f6, f0);
		return Ps2F_Add(x, f0);
	}
	f2 = Ps2F_Mul(f6, f1);
	f0 = Ps2F_Mul(y, 0x3F000000u);                  // 0.5
	f1 = Ps2F_Mul(f6, 0xBE2AAAABu);                 // -0.166666672
	f0 = Ps2F_Sub(f0, f2);
	f0 = Ps2F_Mul(f5, f0);
	f0 = Ps2F_Sub(f0, y);
	f0 = Ps2F_Sub(f0, f1);
	return Ps2F_Sub(x, f0);
}

// Reduction for large arguments (the kernel of the argument reduction): tx = the
// argument as a base 2^8 number (a0), y = the two result words (a1), e0 = the
// exponent of the first word (a2), nx = the word count (a3), prec = 2 for
// single precision (t0), ipio2 = the table of 2/pi bits (t1). Returns n & 7.
// The work arrays live on the stack: iq at 0, f at 0x50, fq at 0xA0, q at 0xF0.
uint32_t ExtZp_Math_439080(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1)
{
	enum { SZ = 0x210, OFF_F = 0x50, OFF_FQ = 0xA0, OFF_Q = 0xF0 };
	const Ps2F TWON8 = 0x3B800000u; // 0.00390625 (the original's constant)
	const Ps2F TWO8 = 0x43800000u;  // 256
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, SZ);
	int32_t e0 = (int32_t)a2, prec = (int32_t)t0;
	int32_t t4, v0, jv, q0, jx, j, jk, mm, i, jz, k, ih, n, s1, carry;
	int jkneg, prec3;
	Ps2F z, f0, f1, f2, f3, scale;

#define IQ(ix_) (sp + (uint32_t)(ix_)*4u)
#define FF(ix_) (sp + OFF_F + (uint32_t)(ix_)*4u)
#define FQ(ix_) (sp + OFF_FQ + (uint32_t)(ix_)*4u)
#define QQ(ix_) (sp + OFF_Q + (uint32_t)(ix_)*4u)

	t4 = e0 - 3;
	v0 = e0 + 4;
	if (-1 < t4)
		v0 = t4;
	jv = v0 >> 3;
	if (!(-1 < jv))
		jv = 0;
	q0 = e0 - ((jv + 1) << 3);
	jx = (int32_t)a3 - 1;
	j = jv - jx;
	i = 0;
	jk = (int32_t)MathTab(c, MATH_G_INITJK + (uint32_t)prec * 4u);
	mm = jx + jk;
	jkneg = jk < 0;
	prec3 = prec < 3;
	if (mm >= 0)
	{
		do
		{
			if (j >= 0)
				f0 = Ps2F_FromInt((int32_t)MathTab(c, t1 + (uint32_t)j * 4u));
			else
				f0 = 0;
			ZmWF(m, FF(i), f0);
			i++;
			j++;
		} while (!(mm < i));
	}
	if (!jkneg)
	{
		i = 0;
		do
		{
			f2 = 0;
			if (!(jx < 0))
			{
				j = 0;
				do
				{
					f1 = ZmRF(m, FF(jx + i - j));
					f0 = ZmRF(m, a0 + (uint32_t)j * 4u);
					f0 = Ps2F_Mul(f0, f1);
					j++;
					f2 = Ps2F_Add(f2, f0);
				} while (!(jx < j));
			}
			ZmWF(m, QQ(i), f2);
			i++;
		} while (!(jk < i));
	}
	jz = jk;
	for (;;)
	{
		// 0x43921c: split q[jz] into base 2^8 digits
		z = ZmRF(m, QQ(jz));
		if (jz > 0)
		{
			int32_t a1c = jz, idx = 0;
			uint32_t a0p = QQ(jz - 1);
			do
			{
				f0 = Ps2F_Mul(z, TWON8);
				f1 = ZmRF(m, a0p);
				a0p -= 4;
				a1c--;
				f2 = Ps2F_FromInt(Ps2F_ToInt(f0));
				f0 = Ps2F_Mul(f2, TWO8);
				f0 = Ps2F_Sub(z, f0);
				z = Ps2F_Add(f1, f2);
				ZmW32(m, IQ(idx), (uint32_t)Ps2F_ToInt(f0));
				idx++;
			} while (a1c > 0);
		}
		z = ExtZp_Math_43b1c0(c, (uint32_t)q0, z);
		ih = 0;
		f0 = ExtZp_Math_43b0b0(c, Ps2F_Mul(z, 0x3E000000u)); // 0.125
		f0 = Ps2F_Mul(f0, 0x41000000u);                         // 8
		z = Ps2F_Sub(z, f0);
		n = Ps2F_ToInt(z);
		f0 = Ps2F_FromInt(n);
		z = Ps2F_Sub(z, f0);
		if (q0 > 0)
		{
			int32_t w = (int32_t)ZmR32(m, IQ(jz - 1));
			int32_t sh = 8 - q0, ii;
			ii = w >> (sh & 31);
			n += ii;
			w -= (int32_t)((uint32_t)ii << (sh & 31));
			ih = w >> ((7 - q0) & 31);
			ZmW32(m, IQ(jz - 1), (uint32_t)w);
		}
		else if (q0 == 0)
			ih = (int32_t)ZmR32(m, IQ(jz - 1)) >> 8;
		else if (Ps2F_Le(0x3F000000u, z)) // 0.5
			ih = 2;
		carry = 0;
		if (ih > 0)
		{
			n++;
			for (i = 0; i < jz; i++)
			{
				int32_t w = (int32_t)ZmR32(m, IQ(i));
				if (carry == 0)
				{
					if (w != 0)
					{
						carry = 1;
						ZmW32(m, IQ(i), (uint32_t)(0x100 - w));
					}
				}
				else
					ZmW32(m, IQ(i), (uint32_t)(0xFF - w));
			}
			if (q0 > 0)
			{
				if (q0 == 1)
					ZmW32(m, IQ(jz - 1), ZmR32(m, IQ(jz - 1)) & 0x7Fu);
				else if (q0 == 2)
					ZmW32(m, IQ(jz - 1), ZmR32(m, IQ(jz - 1)) & 0x3Fu);
			}
			if (ih == 2)
			{
				z = Ps2F_Sub(0x3F800000u, z); // 1
				if (carry != 0)
					z = Ps2F_Sub(z, ExtZp_Math_43b1c0(c, (uint32_t)q0, 0x3F800000u));
			}
		}
		if (Ps2F_Eq(z, 0u))
		{
			int32_t a2c = jz - 1, orv = 0, a0n, a1n, t0c = 1;
			if (!(a2c < jk))
			{
				do
				{
					orv |= (int32_t)ZmR32(m, IQ(a2c));
					a2c--;
				} while (!(a2c < jk));
			}
			if (orv == 0)
			{
				if ((int32_t)ZmR32(m, IQ(jk - 1)) != 0)
				{
					a1n = jz + 1;
					a0n = jz + t0c;
				}
				else
				{
					a1n = jz + 1;
					do
					{
						t0c++;
					} while ((int32_t)ZmR32(m, IQ(jk - t0c)) == 0);
					a0n = jz + t0c;
				}
				a2c = a1n;
				if (!(a0n < a2c))
				{
					do
					{
						int32_t t2 = jx + a2c;
						f0 = Ps2F_FromInt((int32_t)MathTab(c, t1 + (uint32_t)(jv + a2c) * 4u));
						ZmWF(m, FF(t2), f0);
						f2 = 0;
						if (!(jx < 0))
						{
							j = 0;
							do
							{
								f1 = ZmRF(m, FF(t2 - j));
								f0 = ZmRF(m, a0 + (uint32_t)j * 4u);
								j++;
								f0 = Ps2F_Mul(f0, f1);
								f2 = Ps2F_Add(f2, f0);
							} while (!(jx < j));
						}
						ZmWF(m, QQ(a2c), f2);
						a2c++;
					} while (!(a0n < a2c));
				}
				jz = a0n;
				continue;
			}
			// 0x439578: z is zero and the low digits are not
			jz--;
			q0 -= 8;
			if ((int32_t)ZmR32(m, IQ(jz)) == 0)
			{
				do
				{
					jz--;
					q0 -= 8;
				} while ((int32_t)ZmR32(m, IQ(jz)) == 0);
			}
			s1 = n & 7;
		}
		else
		{
			// 0x4395cc: z is not zero: scale it back
			uint32_t oldOff = (uint32_t)jz * 4u;
			z = ExtZp_Math_43b1c0(c, (uint32_t)(0 - q0), z);
			if (Ps2F_Le(TWO8, z))
			{
				jz++;
				f0 = Ps2F_Mul(z, TWON8);
				s1 = n & 7;
				f2 = Ps2F_FromInt(Ps2F_ToInt(f0));
				f0 = Ps2F_Mul(f2, TWO8);
				f0 = Ps2F_Sub(z, f0);
				ZmW32(m, sp + oldOff, (uint32_t)Ps2F_ToInt(f0));
				ZmW32(m, IQ(jz), (uint32_t)Ps2F_ToInt(f2));
				q0 += 8;
			}
			else
			{
				ZmW32(m, sp + oldOff, (uint32_t)Ps2F_ToInt(z));
				s1 = n & 7;
			}
		}
		break;
	}
	// 0x439654: convert the digits back to floats
	f2 = ExtZp_Math_43b1c0(c, (uint32_t)q0, 0x3F800000u);
	scale = f2;
	k = jz;
	if (!(jz < 0))
	{
		for (i = jz; i >= 0; i--)
		{
			f0 = Ps2F_FromInt((int32_t)ZmR32(m, IQ(i)));
			f0 = Ps2F_Mul(scale, f0);
			scale = Ps2F_Mul(scale, TWON8);
			ZmWF(m, QQ(i), f0);
		}
	}
	(void)k;
	if (!(jz < 0))
	{
		for (i = jz; i >= 0; i--)
		{
			int32_t t1c = jz - i;
			f2 = 0;
			k = 0;
			if (!jkneg && t1c >= 0)
			{
				do
				{
					f0 = (Ps2F)MathTab(c, MATH_G_PIO2 + (uint32_t)k * 4u);
					f1 = ZmRF(m, QQ(i + k));
					k++;
					f0 = Ps2F_Mul(f0, f1);
					f2 = Ps2F_Add(f2, f0);
				} while (!(jk < k) && !(t1c < k));
			}
			ZmWF(m, FQ(t1c), f2);
		}
	}
	if (prec3)
	{
		if (prec > 0)
		{
			f2 = 0;
			f3 = ZmRF(m, FQ(0));
			if (!(jz < 0))
			{
				i = jz;
				do
				{
					f0 = ZmRF(m, FQ(i));
					i--;
					f2 = Ps2F_Add(f2, f0);
				} while (i >= 0);
			}
			ZmWF(m, a1, f2);
			if (ih != 0)
				ZmWF(m, a1, Ps2F_Neg(f2));
			f2 = Ps2F_Sub(f3, f2);
			if (!(jz < 1))
			{
				i = 1;
				do
				{
					f0 = ZmRF(m, FQ(i));
					i++;
					f2 = Ps2F_Add(f2, f0);
				} while (!(jz < i));
			}
			ZmWF(m, a1 + 4, f2);
			if (ih != 0)
				ZmWF(m, a1 + 4, Ps2F_Neg(f2));
			ExtZp_Pop(c, SZ);
			return (uint32_t)s1;
		}
		if (prec == 0)
		{
			f2 = 0;
			if (!(jz < 0))
			{
				i = jz;
				do
				{
					f0 = ZmRF(m, FQ(i));
					i--;
					f2 = Ps2F_Add(f2, f0);
				} while (i >= 0);
			}
			ZmWF(m, a1, f2);
			if (ih != 0)
				ZmWF(m, a1, Ps2F_Neg(f2));
		}
		ExtZp_Pop(c, SZ);
		return (uint32_t)s1;
	}
	if (prec == 3)
	{
		i = jz;
		if (i > 0)
		{
			do
			{
				int32_t a0c = i - 1;
				f1 = ZmRF(m, FQ(i));
				f0 = ZmRF(m, FQ(a0c));
				i = a0c;
				f2 = Ps2F_Add(f0, f1);
				f0 = Ps2F_Sub(f0, f2);
				f1 = Ps2F_Add(f1, f0);
				ZmWF(m, FQ(i + 1), f1);
				ZmWF(m, FQ(a0c), f2);
			} while (i > 0);
			i = jz;
		}
		if (!(jz < 2))
		{
			do
			{
				int32_t a0c = i - 1;
				f1 = ZmRF(m, FQ(i));
				f0 = ZmRF(m, FQ(a0c));
				i = a0c;
				f2 = Ps2F_Add(f0, f1);
				f0 = Ps2F_Sub(f0, f2);
				f1 = Ps2F_Add(f1, f0);
				ZmWF(m, FQ(i + 1), f1);
				ZmWF(m, FQ(a0c), f2);
			} while (!(i < 2));
		}
		i = jz;
		f2 = 0;
		f3 = ZmRF(m, FQ(0));
		if (!(i < 2))
		{
			f1 = ZmRF(m, FQ(1));
			do
			{
				f0 = ZmRF(m, FQ(i));
				i--;
				f2 = Ps2F_Add(f2, f0);
			} while (!(i < 2));
		}
		else
			f1 = ZmRF(m, FQ(1));
		if (ih == 0)
		{
			ZmWF(m, a1 + 8, f2);
			ZmWF(m, a1, f3);
			ZmWF(m, a1 + 4, f1);
		}
		else
		{
			f0 = Ps2F_Neg(f3);
			f1 = Ps2F_Neg(f1);
			f2 = Ps2F_Neg(f2);
			ZmWF(m, a1, f0);
			ZmWF(m, a1 + 4, f1);
			ZmWF(m, a1 + 8, f2);
		}
	}
	ExtZp_Pop(c, SZ);
	return (uint32_t)s1;
#undef IQ
#undef FF
#undef FQ
#undef QQ
}

// Argument reduction of cos/sin: y[0] + y[1] (at address y) = x - n * pi/2, n returned.
uint32_t ExtZp_Math_437a80(ExtZpCtx *c, uint32_t y, Ps2F x)
{
	ExtZpMem *m = c->m;
	int32_t hx = (int32_t)x;
	uint32_t ix = x & 0x7FFFFFFFu;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f12 = x;
	int32_t a1, a0, v1, i;
	if (!((int32_t)0x3F490FD8 < (int32_t)ix))
	{
		ZmWF(m, y, x);
		ZmW32(m, y + 4, 0);
		return 0;
	}
	if (!((int32_t)0x4016CBE3 < (int32_t)ix))
	{
		if (hx > 0)
		{
			f0 = 0x3FC90F80u; // 1.57078552 (the original's constant)
			f12 = Ps2F_Sub(f12, f0);
			if ((ix & 0xFFFFFFF0u) == 0x3FC90FD0u)
			{
				f0 = 0x37354400u; // 1.08042732e-05
				f2 = 0x2E85A308u; // 6.0770999e-11
				f12 = Ps2F_Sub(f12, f0);
				f1 = Ps2F_Sub(f12, f2);
			}
			else
			{
				f2 = 0x37354443u; // 1.08043341e-05
				f1 = Ps2F_Sub(f12, f2);
			}
			f0 = Ps2F_Sub(f12, f1);
			ZmWF(m, y, f1);
			f0 = Ps2F_Sub(f0, f2);
			ZmWF(m, y + 4, f0);
			return 1;
		}
		f0 = 0x3FC90F80u; // 1.57078552 (the original's constant)
		f12 = Ps2F_Add(f12, f0);
		if ((ix & 0xFFFFFFF0u) == 0x3FC90FD0u)
		{
			f0 = 0x37354400u;
			f2 = 0x2E85A308u;
			f12 = Ps2F_Add(f12, f0);
			f1 = Ps2F_Add(f12, f2);
		}
		else
		{
			f2 = 0x37354443u;
			f1 = Ps2F_Add(f12, f2);
		}
		f0 = Ps2F_Sub(f12, f1);
		ZmWF(m, y, f1);
		f0 = Ps2F_Add(f0, f2);
		ZmWF(m, y + 4, f0);
		return (uint32_t)-1;
	}
	if (!((int32_t)0x43490F80 < (int32_t)ix))
	{
		// medium: n = round(|x| * 2/pi)
		f0 = ExtZp_Math_43b068(c, x);
		f5 = f0;
		f1 = Ps2F_Mul(f5, 0x3F22F984u); // 0.636619806 (the original's constant)
		f0 = 0x3FC90F80u;               // 1.57078552
		f3 = 0x37354443u;               // 1.08043341e-05
		f1 = Ps2F_Add(f1, 0x3F000000u); // 0.5
		a1 = Ps2F_ToInt(f1);
		f6 = Ps2F_FromInt(a1);
		f0 = Ps2F_Mul(f6, f0);
		f3 = Ps2F_Mul(f6, f3);
		f4 = Ps2F_Sub(f5, f0);
		if (a1 < 0x20)
		{
			uint32_t tabv = MathTab(c, MATH_G_NPIO2 + (uint32_t)(a1 - 1) * 4u);
			f0 = Ps2F_Sub(f4, f3);
			if ((ix & 0xFFFFFF00u) != tabv)
			{
				ZmWF(m, y, f0);
				f1 = ZmRF(m, y);
				goto fin;
			}
		}
		else
			f0 = Ps2F_Sub(f4, f3);
		a0 = (int32_t)ix >> 23;
		ZmW32(m, y, f0);
		v1 = a0 - (int32_t)((f0 >> 23) & 0xFF);
		if (!(v1 < 9))
		{
			f0 = 0x37354400u; // 1.08042732e-05
			f5 = f4;
			f1 = 0x2E85A308u; // 6.0770999e-11
			f3 = Ps2F_Mul(f6, f0);
			f1 = Ps2F_Mul(f6, f1);
			f4 = Ps2F_Sub(f4, f3);
			f0 = Ps2F_Sub(f5, f4);
			f0 = Ps2F_Sub(f0, f3);
			f3 = Ps2F_Sub(f1, f0);
			f2 = Ps2F_Sub(f4, f3);
			ZmWF(m, y, f2);
			v1 = a0 - (int32_t)((f2 >> 23) & 0xFF);
			if (!(v1 < 0x1A))
			{
				f0 = 0x2E85A300u; // 6.07709438e-11
				f5 = f4;
				f2 = 0x248D3132u; // 6.12323426e-17
				f3 = Ps2F_Mul(f6, f0);
				f2 = Ps2F_Mul(f6, f2);
				f4 = Ps2F_Sub(f4, f3);
				f0 = Ps2F_Sub(f5, f4);
				f0 = Ps2F_Sub(f0, f3);
				f3 = Ps2F_Sub(f2, f0);
				f1 = Ps2F_Sub(f4, f3);
				ZmWF(m, y, f1);
			}
		}
		f1 = ZmRF(m, y);
		goto fin2;
	fin:
		f1 = ZmRF(m, y);
	fin2:
		f0 = Ps2F_Sub(f4, f1);
		f0 = Ps2F_Sub(f0, f3);
		ZmWF(m, y + 4, f0);
		if (hx >= 0)
			return (uint32_t)a1;
		f1 = Ps2F_Neg(f1);
		f0 = Ps2F_Neg(f0);
		ZmWF(m, y, f1);
		ZmWF(m, y + 4, f0);
		return (uint32_t)(0 - a1);
	}
	if (!((int32_t)0x7F7FFFFF < (int32_t)ix))
	{
		// large finite argument: split into base 2^8 words, run the kernel
		uint32_t tx = ExtZp_Push(c, 16);
		int32_t e0 = ((int32_t)ix >> 23) - 0x86;
		int32_t a3 = 3, q;
		uint32_t n;
		f12 = (Ps2F)((int32_t)ix - (int32_t)((uint32_t)e0 << 23));
		for (i = 0; i < 2; i++)
		{
			int32_t iv = Ps2F_ToInt(f12);
			f0 = Ps2F_FromInt(iv);
			f1 = Ps2F_Sub(f12, f0);
			ZmWF(m, tx + (uint32_t)i * 4u, f0);
			f12 = Ps2F_Mul(f1, 0x43800000u); // 256
		}
		ZmWF(m, tx + 8, f12);
		if (Ps2F_Eq(f12, 0u))
		{
			q = 2;
			a3 = q;
			for (;;)
			{
				int32_t nq = a3 - 1;
				f0 = ZmRF(m, tx + (uint32_t)nq * 4u);
				if (Ps2F_Eq(f0, 0u))
					a3 = nq;
				else
					break;
			}
		}
		n = ExtZp_Math_439080(c, tx, y, (uint32_t)e0, (uint32_t)a3, 2u, MATH_G_IPIO2);
		ExtZp_Pop(c, 16);
		if (hx >= 0)
			return n;
		f1 = ZmRF(m, y);
		f0 = ZmRF(m, y + 4);
		f1 = Ps2F_Neg(f1);
		f0 = Ps2F_Neg(f0);
		ZmWF(m, y, f1);
		ZmWF(m, y + 4, f0);
		return (uint32_t)(0 - (int32_t)n);
	}
	f0 = Ps2F_Sub(x, x);
	ZmWF(m, y, f0);
	ZmWF(m, y + 4, f0);
	return 0;
}

// cosf.
Ps2F ExtZp_Math_43af80(ExtZpCtx *c, Ps2F x)
{
	int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
	uint32_t y, n;
	Ps2F f0, y0, y1;
	if (!((int32_t)0x3F490FD8 < ix))
		return ExtZp_Math_438f28(c, x, 0u);
	if ((int32_t)0x7F7FFFFF < ix)
		return Ps2F_Sub(x, x);
	y = ExtZp_Push(c, 16);
	n = ExtZp_Math_437a80(c, y, x) & 3u;
	y0 = ZmRF(c->m, y);
	y1 = ZmRF(c->m, y + 4);
	switch (n)
	{
	case 0: f0 = ExtZp_Math_438f28(c, y0, y1); break;
	case 1: f0 = Ps2F_Neg(ExtZp_Math_4399d0(c, 1u, y0, y1)); break;
	case 2: f0 = Ps2F_Neg(ExtZp_Math_438f28(c, y0, y1)); break;
	default: f0 = ExtZp_Math_4399d0(c, 1u, y0, y1); break;
	}
	ExtZp_Pop(c, 16);
	return f0;
}

// sinf.
Ps2F ExtZp_Math_43b320(ExtZpCtx *c, Ps2F x)
{
	int32_t ix = (int32_t)(x & 0x7FFFFFFFu);
	uint32_t y, n;
	Ps2F f0, y0, y1;
	if (!((int32_t)0x3F490FD8 < ix))
		return ExtZp_Math_4399d0(c, 0u, x, 0u);
	if ((int32_t)0x7F7FFFFF < ix)
		return Ps2F_Sub(x, x);
	y = ExtZp_Push(c, 16);
	n = ExtZp_Math_437a80(c, y, x) & 3u;
	y0 = ZmRF(c->m, y);
	y1 = ZmRF(c->m, y + 4);
	switch (n)
	{
	case 0: f0 = ExtZp_Math_4399d0(c, 1u, y0, y1); break;
	case 1: f0 = ExtZp_Math_438f28(c, y0, y1); break;
	case 2: f0 = Ps2F_Neg(ExtZp_Math_4399d0(c, 1u, y0, y1)); break;
	default: f0 = Ps2F_Neg(ExtZp_Math_438f28(c, y0, y1)); break;
	}
	ExtZp_Pop(c, 16);
	return f0;
}

// acosf core (|x| <= 1 gives the angle; |x| > 1 gives 0/0).
Ps2F ExtZp_Math_436920(ExtZpCtx *c, Ps2F x)
{
	int32_t a0 = (int32_t)x;
	int32_t v1 = a0 & 0x7FFFFFFF;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f12, f20, f21, f22;
	if (v1 == 0x3F800000)
	{
		if (a0 > 0)
			return 0u;
		return 0x40490FDBu; // 3.14159274 (the original's constant)
	}
	if (0x3F800000 < v1)
	{
		f0 = Ps2F_Sub(x, x);
		return Ps2F_Div(f0, f0);
	}
	if (!(0x3EFFFFFF < v1))
	{
		if (!(0x23000000 < v1))
			return 0x3FC90FDBu; // 1.57079637
		f21 = Ps2F_Mul(x, x);
		f1 = 0x3811EF08u;
		f3 = 0x3A4F7F04u;
		f6 = 0xBD241146u;
		f1 = Ps2F_Mul(f21, f1);
		f2 = 0x3D9DC62Eu;
		f0 = 0xBF303361u;
		f2 = Ps2F_Mul(f21, f2);
		f7 = 0x3E4E0AA8u;
		f1 = Ps2F_Add(f1, f3);
		f4 = 0x4001572Du;
		f9 = 0xBEA6B090u;
		f2 = Ps2F_Add(f2, f0);
		f3 = 0xC019D139u;
		f1 = Ps2F_Mul(f21, f1);
		f8 = 0x3E2AAAABu;
		f5 = 0x3F800000u;
		f2 = Ps2F_Mul(f21, f2);
		f0 = 0x33A22168u; // 7.54978942e-08
		f1 = Ps2F_Add(f1, f6);
		f10 = 0x3FC90FDAu; // 1.57079625
		f2 = Ps2F_Add(f2, f4);
		f1 = Ps2F_Mul(f21, f1);
		f2 = Ps2F_Mul(f21, f2);
		f1 = Ps2F_Add(f1, f7);
		f2 = Ps2F_Add(f2, f3);
		f1 = Ps2F_Mul(f21, f1);
		f2 = Ps2F_Mul(f21, f2);
		f1 = Ps2F_Add(f1, f9);
		f22 = Ps2F_Add(f2, f5);
		f1 = Ps2F_Mul(f21, f1);
		f1 = Ps2F_Add(f1, f8);
		f20 = Ps2F_Mul(f21, f1);
		f12 = Ps2F_Div(f20, f22);
		f1 = Ps2F_Mul(x, f12);
		f0 = Ps2F_Sub(f0, f1);
		f0 = Ps2F_Sub(x, f0);
		return Ps2F_Sub(f10, f0);
	}
	if (a0 < 0)
	{
		f10 = 0x3F800000u;
		f3 = 0x3F000000u;
		f2 = Ps2F_Add(x, f10);
		f0 = 0x3811EF08u;
		f4 = 0x3A4F7F04u;
		f7 = 0xBD241146u;
		f21 = Ps2F_Mul(f2, f3);
		f1 = 0x3D9DC62Eu;
		f2 = 0xBF303361u;
		f8 = 0x3E4E0AA8u;
		f0 = Ps2F_Mul(f21, f0);
		f3 = 0x4001572Du;
		f1 = Ps2F_Mul(f21, f1);
		f9 = 0xBEA6B090u;
		f5 = 0xC019D139u;
		f12 = f21;
		f0 = Ps2F_Add(f0, f4);
		f6 = 0x3E2AAAABu;
		f1 = Ps2F_Add(f1, f2);
		f0 = Ps2F_Mul(f21, f0);
		f1 = Ps2F_Mul(f21, f1);
		f0 = Ps2F_Add(f0, f7);
		f1 = Ps2F_Add(f1, f3);
		f0 = Ps2F_Mul(f21, f0);
		f1 = Ps2F_Mul(f21, f1);
		f0 = Ps2F_Add(f0, f8);
		f1 = Ps2F_Add(f1, f5);
		f0 = Ps2F_Mul(f21, f0);
		f1 = Ps2F_Mul(f21, f1);
		f0 = Ps2F_Add(f0, f9);
		f22 = Ps2F_Add(f1, f10);
		f0 = Ps2F_Mul(f21, f0);
		f0 = Ps2F_Add(f0, f6);
		f20 = Ps2F_Mul(f21, f0); // the delay slot of the call
		f0 = ExtZp_Math_437e60(c, f12);
		f10 = f0;
		f1 = 0x33A22168u; // 7.54978942e-08
		f12 = Ps2F_Div(f20, f22);
		f2 = 0x40490FDAu; // 3.1415925
		f0 = Ps2F_Mul(f12, f10);
		f0 = Ps2F_Sub(f0, f1);
		f0 = Ps2F_Add(f10, f0);
		f0 = Ps2F_Add(f0, f0);
		return Ps2F_Sub(f2, f0);
	}
	{
		uint32_t b;
		f20 = 0x3F800000u;
		f1 = 0x3F000000u;
		f0 = Ps2F_Sub(f20, x);
		f21 = Ps2F_Mul(f0, f1);
		f12 = f21;
		f0 = ExtZp_Math_437e60(c, f12);
		b = f0;
		f10 = b;
		b &= 0xFFFFF000u;
		f0 = b;
		f2 = Ps2F_Mul(f0, f0);
		f0 = 0x3811EF08u;
		f4 = 0x3A4F7F04u;
		f0 = Ps2F_Mul(f21, f0);
		f1 = 0x3D9DC62Eu;
		f9 = 0xBD241146u;
		f2 = Ps2F_Sub(f21, f2);
		f1 = Ps2F_Mul(f21, f1);
		f5 = 0xBF303361u;
		f0 = Ps2F_Add(f0, f4);
		f8 = 0xBEA6B090u;
		f6 = b;
		f1 = Ps2F_Add(f1, f5);
		f4 = 0x4001572Du;
		f0 = Ps2F_Mul(f21, f0);
		f7 = 0x3E2AAAABu;
		f3 = Ps2F_Add(f10, f6);
		f6 = 0x3E4E0AA8u;
		f1 = Ps2F_Mul(f21, f1);
		f0 = Ps2F_Add(f0, f9);
		f2 = Ps2F_Div(f2, f3);
		f1 = Ps2F_Add(f1, f4);
		f3 = 0xC019D139u;
		f0 = Ps2F_Mul(f21, f0);
		f1 = Ps2F_Mul(f21, f1);
		f0 = Ps2F_Add(f0, f6);
		f1 = Ps2F_Add(f1, f3);
		f0 = Ps2F_Mul(f21, f0);
		f1 = Ps2F_Mul(f21, f1);
		f0 = Ps2F_Add(f0, f8);
		f22 = Ps2F_Add(f1, f20);
		f0 = Ps2F_Mul(f21, f0);
		f1 = b;
		f0 = Ps2F_Add(f0, f7);
		f20 = Ps2F_Mul(f21, f0);
		f12 = Ps2F_Div(f20, f22);
		f0 = Ps2F_Mul(f12, f10);
		f0 = Ps2F_Add(f0, f2);
		f0 = Ps2F_Add(f1, f0);
		return Ps2F_Add(f0, f0);
	}
}

// acosf with the error handling of the library: |x| > 1 queries the error
// hooks (EXT_ZP_Q_OTHER) and returns what the last of them gives.
Ps2F ExtZp_Math_43bc40(ExtZpCtx *c, Ps2F x)
{
	ExtZpMem *m = c->m;
	Ps2F f21 = ExtZp_Math_436920(c, x);
	int32_t s0 = (int32_t)ZmR32(m, MATH_G_VERSION);
	uint32_t sp, v0;
	uint64_t r64;
	ExtZpQueryArgs qa;
	ExtZpQueryResult qr;
	int i;
	if (s0 == -1)
		return f21;
	if (ExtZp_Math_43b198(c, x) != 0)
		return f21;
	if (!Ps2F_Lt(0x3F800000u, ExtZp_Math_43b068(c, x)))
		return f21;
	// error path: a record of 0x28 bytes on the stack: type, name, three words, errno slot at 0x20
	sp = ExtZp_Push(c, 0x30);
	ZmW32(m, sp, 1u);
	ZmW32(m, sp + 4, MATH_G_NAME);
	for (i = 0; i < 8; i++)
	{
		qa.a[i] = 0;
		qa.f[i] = 0;
	}
	qa.f[0] = x;
	qa.func = 0x003FAD18u;
	ZmW32(m, sp + 0x20, 0u);
	qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	r64 = (uint64_t)(int64_t)(int32_t)qr.v0;
	ZmW64(m, sp + 8, r64);
	ZmW64(m, sp + 0x18, 0u);
	ZmW64(m, sp + 0x10, r64);
	v0 = 1;
	if (s0 != 2)
	{
		for (i = 0; i < 8; i++)
		{
			qa.a[i] = 0;
			qa.f[i] = 0;
		}
		qa.a[0] = sp;
		qa.func = 0x0043A6F8u;
		qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
		v0 = qr.v0 != 0;
	}
	else
		v0 = 0;
	if (v0 == 0)
	{
		for (i = 0; i < 8; i++)
		{
			qa.a[i] = 0;
			qa.f[i] = 0;
		}
		qa.func = 0x00428410u;
		qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
		ZmW32(m, qr.v0, 0x21u);
	}
	v0 = ZmR32(m, sp + 0x20);
	if (v0 != 0)
	{
		for (i = 0; i < 8; i++)
		{
			qa.a[i] = 0;
			qa.f[i] = 0;
		}
		qa.func = 0x00428410u;
		qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
		ZmW32(m, qr.v0, ZmR32(m, sp + 0x20));
	}
	for (i = 0; i < 8; i++)
	{
		qa.a[i] = 0;
		qa.f[i] = 0;
	}
	qa.a[0] = (uint32_t)ZmR64(m, sp + 0x18);
	qa.func = 0x003FB8D0u;
	qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	ExtZp_Pop(c, 0x30);
	return qr.f0;
}
