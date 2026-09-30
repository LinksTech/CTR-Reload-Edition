/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gte/PsyX_GTE.cpp
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

// THE SECOND GTE PATH, AND WHY IT IS A COPY TODAY.
//
// platform/native_gte_core.c is the coprocessor this game has always
// computed with. This file is the same coprocessor once more - expression by
// expression, bracket by bracket, the same saturations in the same
// order. Today it is neither better nor faster. It is the place
// where something can be changed without touching the path the game
// runs on.
//
// WHY TWO AT ALL. Not only the picture hangs on the GTE. Collision, physics,
// camera, bot logic and the recordings read the same registers - a
// deviation of one bit there is not a pixel but a different ride. A
// second path that starts bit-identical makes every later change
// measurable: whatever differs, differs against a zero point
// that is proven and not claimed.
//
// WHAT IS NOT HERE. No optimisation, no simplification, no
// merged computation. The first step is equality; everything
// else comes after that and is measured one by one.
//
// HOW IT IS SWITCHED. g_cfg_gteAlt, read in doCOP2
// (platform/native_inline_c.c). Default 0 - the old path. --gte-alt switches
// over, the debug menu shows nothing of it, because this is not a feature but
// a measurement state.
//
// THE PROOF. --gte-selftest runs both paths over the same registers and
// compares all 64 registers plus the return value after every single
// operation. It hangs on the build as ctest 'gte_paths_identical'.
//
// ALL NAMES ARE RENAMED. The build is a unity build: both files land
// in one translation unit, and 'internal' is 'static'. Two helpers with the same
// name would be an error there, not a second path. What is NOT
// renamed is gteRegs - both paths compute on the same register set,
// and exactly that makes them comparable.

#include <stdio.h>
#include <stdlib.h>

#include <ctr_subpixel.h>
#include <macros.h>
#include <platform.h>
#include <psx/gtereg.h>
#include <psx/libgte.h>


#define ALT_SF(op)    ((op >> 19) & 1)
#define ALT_MX(op)    ((op >> 17) & 3)
#define ALT_V(op)     ((op >> 15) & 3)
#define ALT_CV(op)    ((op >> 13) & 3)
#define ALT_LM(op)    ((op >> 10) & 1)
#define ALT_FUNCT(op) (op & 63)

#define altop(code)   (code & 0x1ffffff)

#define ALT_VX(n)         (n < 3 ? gteRegs.CP2D.p[n << 1].sw.l : C2_IR1)
#define ALT_VY(n)         (n < 3 ? gteRegs.CP2D.p[n << 1].sw.h : C2_IR2)
#define ALT_VZ(n)         (n < 3 ? gteRegs.CP2D.p[(n << 1) + 1].sw.l : C2_IR3)
#define ALT_MX11(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3)].sw.l : -C2_R << 4)
#define ALT_MX12(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3)].sw.h : C2_R << 4)
#define ALT_MX13(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 1].sw.l : C2_IR0)
#define ALT_MX21(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 1].sw.h : C2_R13)
#define ALT_MX22(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 2].sw.l : C2_R13)
#define ALT_MX23(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 2].sw.h : C2_R13)
#define ALT_MX31(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 3].sw.l : C2_R22)
#define ALT_MX32(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 3].sw.h : C2_R22)
#define ALT_MX33(n)       (n < 3 ? gteRegs.CP2C.p[(n << 3) + 4].sw.l : C2_R22)
#define ALT_CV1(n)        (n < 3 ? gteRegs.CP2C.p[(n << 3) + 5].sd : 0)
#define ALT_CV2(n)        (n < 3 ? gteRegs.CP2C.p[(n << 3) + 6].sd : 0)
#define ALT_CV3(n)        (n < 3 ? gteRegs.CP2C.p[(n << 3) + 7].sd : 0)

// Which path computes. 0 is the core, and 0 is the default.
//
// A setting, not a derivation: nothing down here asks whether the
// other path might be faster. Whoever switches says so.
int g_cfg_gteAlt = 0;

// THE DIVISION PATH THAT DOES NOT SATURATE.
//
// WHAT THE OLD ONE DOES. GteAlt_Divide is the UNR reimplementation of the PS1 coprocessor:
// a lookup table, one Newton step, done. It is only built for
// numerator < 2*denominator; above that it gives up and returns 0xffffffff,
// and GteAlt_LmE turns that into 0x1ffff and sets flag bit 17. In the projection
// that means: as soon as the view-space depth SZ3 falls below H/2 - H is the
// projection distance in control register 26 - H/SZ3 is no longer H/SZ3
// but a constant. The point lands somewhere. Exactly at this edge
// sits the near clip threshold of the track renderer, (H>>1)+1, one above
// the limit - the renderer throws away what the coprocessor can no longer
// compute.
//
// WHAT THIS ONE DOES. For SZ3 below H/2, H/SZ3 is computed exactly, in 64 bits,
// and the screen coordinate may take the whole s16 range instead of only
// +-1024.
//
// WHY INTEGER AND EXACT, AND NOT FLOATING POINT. The UNR table is in the
// core because the PS1 had no divider. We have one. Above the edge
// its result is what the game has always computed with, and it is not
// touched here; below it there is nothing to preserve, so the exact
// division is the choice that has nothing new to defend. Floating point would have
// introduced a rounding mode as a second unknown, in a chain that
// is otherwise fixed point throughout. The rounding is the same as that of the
// UNR path: to the nearest integer, +0x8000 before the shift by 16.
//
// WHERE EXACTLY THE LIMIT LIES, AND WHY THAT IS A GUARANTEE. The wide path
// is ONLY entered when GteAlt_Divide returns 0xffffffff - i.e. exactly
// when the old path has given up. Every projection the core
// manages runs through the same table, the same LmE and the same
// +-1024 clamp as before, expression by expression. Not "measured, no
// difference", but: there is no branch in which one could arise.
//
// SZ3 == 0 STAYS CLAMPED. A point exactly on the camera plane has no
// finite projection, and the exact division would be a division by
// zero there. The old path with its 0x1ffff and its flag bit stays.
//
// SWITCH: --gte-near-div, default off, independent of --gte-alt. It only takes effect
// when the second path computes - it lives in that path's file, after all. If it is off,
// it costs one branch on a global zero per saturated division.
int g_cfg_gteNearDiv = 0;

// What the wide path did. It is here and not in native_gte_flags.c, because
// these are numbers about THIS path and not about the FLAG register.
long long g_gteNearDivSaturations = 0; // how often the old path would have given up
long long g_gteNearDivWide = 0;        // of those, computed exactly (SZ3 != 0)
long long g_gteNearDivZeroDepth = 0;   // of those, SZ3 == 0, so still clamped
long long g_gteNearDivClampedX = 0;    // wide result ran into the s16 limit
long long g_gteNearDivClampedY = 0;
int g_gteNearDivMaxAbsX = 0; // largest magnitude the wide path produced
int g_gteNearDivMaxAbsY = 0;

global_variable int s_altSf;
global_variable s64 s_altMac0;
global_variable s64 s_altMac3;

internal u32 GteAlt_LeadingZeroCount(u32 lzcs)
{
	u32 lzcr = ((s32)lzcs < 0) ? ~lzcs : lzcs;
	local_persist const char debruijn32[32] = {0, 31, 9, 30, 3, 8,  13, 29, 2,  5,  7,  21, 12, 24, 28, 19,
	                                           1, 10, 4, 14, 6, 22, 25, 20, 11, 15, 23, 26, 16, 27, 17, 18};

	if (!lzcr)
	{
		return 32;
	}

	lzcr |= lzcr >> 1;
	lzcr |= lzcr >> 2;
	lzcr |= lzcr >> 4;
	lzcr |= lzcr >> 8;
	lzcr |= lzcr >> 16;
	lzcr++;

	return debruijn32[lzcr * 0x076be629 >> 27];
}

internal int GteAlt_LIM(int value, int max, int min, unsigned int flag)
{
	if (value > max)
	{
		C2_FLAG |= flag;
		return max;
	}
	else if (value < min)
	{
		C2_FLAG |= flag;
		return min;
	}

	return value;
}

internal inline s64 GteAlt_Shift(s64 a, int sf)
{
	if (sf > 0)
	{
		return a >> 12;
	}
	else if (sf < 0)
	{
		return a << 12;
	}

	return a;
}

internal int GteAlt_Bounds(/*int44*/ s64 value, int max_flag, int min_flag)
{
	if (value /*.positive_overflow()*/ > (s64)0x7ffffffffff)
	{
		C2_FLAG |= max_flag;
	}

	if (value /*.negative_overflow()*/ < (s64)-0x8000000000)
	{
		C2_FLAG |= min_flag;
	}

	return (int)(GteAlt_Shift(value /*.value()*/, s_altSf));
}

internal u32 GteAlt_Divide(u16 numerator, u16 denominator)
{
	if (numerator < (denominator * 2))
	{
		local_persist const u8 table[] = {
		    0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1, 0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3, 0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5,
		    0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8, 0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb, 0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
		    0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4, 0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a, 0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90,
		    0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
		    0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d,
		    0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
		    0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b, 0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35,
		    0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f, 0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
		    0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a, 0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15,
		    0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11, 0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
		    0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00};

		int shift = GteAlt_LeadingZeroCount(denominator) - 16;

		int r1 = (denominator << shift) & 0x7fff;
		int r2 = table[((r1 + 0x40) >> 7)] + 0x101;
		int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;
		u32 reciprocal = ((r2 * r3) + 0x80) >> 8;

		return (u32)((((u64)reciprocal * (numerator << shift)) + 0x8000) >> 16);
	}

	return 0xffffffff;
}

/* Setting bits 12 & 19-22 in FLAG does not set bit 31 */

internal int GteAlt_A1(/*int44*/ s64 a)
{
	return GteAlt_Bounds(a, (1 << 31) | (1 << 30), (1 << 31) | (1 << 27));
}
internal int GteAlt_A2(/*int44*/ s64 a)
{
	return GteAlt_Bounds(a, (1 << 31) | (1 << 29), (1 << 31) | (1 << 26));
}
internal int GteAlt_A3(/*int44*/ s64 a)
{
	s_altMac3 = a;
	return GteAlt_Bounds(a, (1 << 31) | (1 << 28), (1 << 31) | (1 << 25));
}
internal int GteAlt_LmB1(int a, int lm)
{
	return GteAlt_LIM(a, 0x7fff, -0x8000 * !lm, (1 << 31) | (1 << 24));
}
internal int GteAlt_LmB2(int a, int lm)
{
	return GteAlt_LIM(a, 0x7fff, -0x8000 * !lm, (1 << 31) | (1 << 23));
}
internal int GteAlt_LmB3(int a, int lm)
{
	return GteAlt_LIM(a, 0x7fff, -0x8000 * !lm, (1 << 22));
}

internal int GteAlt_LmB3Sf(s64 value, int sf, int lm)
{
	int value_sf = (int)(GteAlt_Shift(value, sf));
	int value_12 = (int)(GteAlt_Shift(value, 1));
	int max = 0x7fff;
	int min = 0;
	if (lm == 0)
	{
		min = -0x8000;
	}

	if (value_12 < -0x8000 || value_12 > 0x7fff)
	{
		C2_FLAG |= (1 << 22);
	}

	if (value_sf > max)
	{
		return max;
	}
	else if (value_sf < min)
	{
		return min;
	}

	return value_sf;
}

internal int GteAlt_LmC1(int a)
{
	return GteAlt_LIM(a, 0x00ff, 0x0000, (1 << 21));
}
internal int GteAlt_LmC2(int a)
{
	return GteAlt_LIM(a, 0x00ff, 0x0000, (1 << 20));
}
internal int GteAlt_LmC3(int a)
{
	return GteAlt_LIM(a, 0x00ff, 0x0000, (1 << 19));
}
internal int GteAlt_LmD(s64 a, int sf)
{
	return GteAlt_LIM((int)(GteAlt_Shift(a, sf)), 0xffff, 0x0000, (1 << 31) | (1 << 18));
}

internal u32 GteAlt_LmE(u32 result)
{
	if (result == 0xffffffff)
	{
		C2_FLAG |= (1 << 31) | (1 << 17);
		return 0x1ffff;
	}

	if (result > 0x1ffff)
	{
		return 0x1ffff;
	}

	return result;
}

internal s64 GteAlt_F(s64 a)
{
	s_altMac0 = a;

	if (a > 0x7fffffffLL)
	{
		C2_FLAG |= (1 << 31) | (1 << 16);
	}

	if (a < -0x80000000LL)
	{
		C2_FLAG |= (1 << 31) | (1 << 15);
	}

	return a;
}

internal int GteAlt_LmG1(s64 a)
{
	if (a > 0x3ff)
	{
		C2_FLAG |= (1 << 31) | (1 << 14);
		return 0x3ff;
	}
	if (a < -0x400)
	{
		C2_FLAG |= (1 << 31) | (1 << 14);
		return -0x400;
	}

	return (int)(a);
}

internal int GteAlt_LmG2(s64 a)
{
	if (a > 0x3ff)
	{
		C2_FLAG |= (1 << 31) | (1 << 13);
		return 0x3ff;
	}

	if (a < -0x400)
	{
		C2_FLAG |= (1 << 31) | (1 << 13);
		return -0x400;
	}

	return (int)(a);
}

// The clamp of the wide path. +-0x7fff instead of +-1024, and that is the width of the
// register and not a chosen number: SXY2 packs SX and SY as two s16,
// and the track renderer reads them back out as s16. No result can reach further
// than the edge of the field in which the result is stored.
//
// The same flag bits as the narrow clamp. Whoever reads the FLAG register to
// learn "this coordinate ran into a limit" still gets exactly
// this information; only the limit is a different one.
internal int GteAlt_LmG1Wide(s64 a)
{
	if (a > 0x7fff)
	{
		C2_FLAG |= (1 << 31) | (1 << 14);
		g_gteNearDivClampedX++;
		return 0x7fff;
	}
	if (a < -0x8000)
	{
		C2_FLAG |= (1 << 31) | (1 << 14);
		g_gteNearDivClampedX++;
		return -0x8000;
	}

	return (int)(a);
}

internal int GteAlt_LmG2Wide(s64 a)
{
	if (a > 0x7fff)
	{
		C2_FLAG |= (1 << 31) | (1 << 13);
		g_gteNearDivClampedY++;
		return 0x7fff;
	}
	if (a < -0x8000)
	{
		C2_FLAG |= (1 << 31) | (1 << 13);
		g_gteNearDivClampedY++;
		return -0x8000;
	}

	return (int)(a);
}

// H/SZ3 in 16.16, exact, rounded to the nearest integer.
//
// H is a u16, SZ3 a non-zero u16. So the numerator is at most
// 0xffff << 17 = 0x1fffe0000, the quotient at most 0x1fffe0000 - both fit
// in 64 bits with room to spare. The +1 before the last shift is the rounding, the same
// as the +0x8000 before the >>16 in the UNR path, only applied one bit later.
internal s64 GteAlt_DivideExact(u32 numerator, u32 denominator)
{
	return (s64)(((((u64)numerator << 17) / (u64)denominator) + 1u) >> 1);
}

internal int GteAlt_LmH(s64 value, int sf)
{
	s64 value_sf = GteAlt_Shift(value, sf);
	int value_12 = (int)(GteAlt_Shift(value, 1));
	int max = 0x1000;
	int min = 0x0000;

	if (value_sf < min || value_sf > max)
	{
		C2_FLAG |= (1 << 12);
	}

	if (value_12 > max)
	{
		return max;
	}

	if (value_12 < min)
	{
		return min;
	}

	return value_12;
}

internal int GteAlt_RotTransPers(int idx, int lm)
{
	C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_TRX << 12) + (C2_R11 * ALT_VX(idx)) + (C2_R12 * ALT_VY(idx)) + (C2_R13 * ALT_VZ(idx)));
	C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_TRY << 12) + (C2_R21 * ALT_VX(idx)) + (C2_R22 * ALT_VY(idx)) + (C2_R23 * ALT_VZ(idx)));
	C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_TRZ << 12) + (C2_R31 * ALT_VX(idx)) + (C2_R32 * ALT_VY(idx)) + (C2_R33 * ALT_VZ(idx)));
	C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
	C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
	C2_IR3 = GteAlt_LmB3Sf(s_altMac3, s_altSf, lm);
	C2_SZ0 = C2_SZ1;
	C2_SZ1 = C2_SZ2;
	C2_SZ2 = C2_SZ3;
	C2_SZ3 = GteAlt_LmD(s_altMac3, 1);

	const u32 rawQuotient = GteAlt_Divide(C2_H, C2_SZ3);

	// The one branch where the two paths part. rawQuotient is
	// 0xffffffff exactly when the UNR table has given up, i.e. when
	// H >= 2*SZ3 - the edge at which the near clip threshold of the
	// track renderer sits. Everywhere else it goes on unchanged.
	if (g_cfg_gteNearDiv && (rawQuotient == 0xffffffffu))
	{
		g_gteNearDivSaturations++;

		if (C2_SZ3 != 0)
		{
			g_gteNearDivWide++;

			const s64 wide = GteAlt_DivideExact((u32)C2_H, (u32)C2_SZ3);
			const s64 sxWideFull = GteAlt_F((s64)C2_OFX + ((s64)C2_IR1 * wide));
			const int sx = GteAlt_LmG1Wide(sxWideFull >> 16);
			const s64 syWideFull = GteAlt_F((s64)C2_OFY + ((s64)C2_IR2 * wide));
			const int sy = GteAlt_LmG2Wide(syWideFull >> 16);
			const int absX = (sx < 0) ? -sx : sx;
			const int absY = (sy < 0) ? -sy : sy;

			C2_SXY0 = C2_SXY1;
			C2_SXY1 = C2_SXY2;
			C2_SX2 = sx;
			C2_SY2 = sy;

			NativeSubpixel_PushFrac(sxWideFull, syWideFull, (int)((sxWideFull >> 16) != (s64)sx), (int)((syWideFull >> 16) != (s64)sy));

			if (absX > g_gteNearDivMaxAbsX)
			{
				g_gteNearDivMaxAbsX = absX;
			}
			if (absY > g_gteNearDivMaxAbsY)
			{
				g_gteNearDivMaxAbsY = absY;
			}

			// The return value is the fog factor numerator: the caller
			// computes MAC0 = DQB + DQA*h and IR0 with it. Here h is larger than
			// anything the old path ever delivered, so IR0 runs into
			// its 0x1000 clamp - the fog is at the stop. That is
			// correct: nearer than the near plane there is no fog any more.
			// The value is returned as int as before; it cannot go beyond 2^31,
			// because H is at most 0xffff and SZ3 at least
			// one, so wide is at most 0xffff0000.
			return (int)wide;
		}

		// SZ3 == 0: no finite projection, no exact quotient. The old
		// path with its 0x1ffff and its flag bit 17 stays in place.
		g_gteNearDivZeroDepth++;
	}

	int h_over_sz3 = GteAlt_LmE(rawQuotient);
	C2_SXY0 = C2_SXY1;
	C2_SXY1 = C2_SXY2;
	// The same place as in platform/native_gte_core.c, and split up for the same
	// reason: the fractional part only exists between the sum
	// and the shift. Both paths must report it the same way, otherwise
	// --subpixel-report measures something else under --gte-alt.
	const s64 sxFull = GteAlt_F((s64)C2_OFX + ((s64)C2_IR1 * h_over_sz3));
	C2_SX2 = GteAlt_LmG1(sxFull >> 16);
	const s64 syFull = GteAlt_F((s64)C2_OFY + ((s64)C2_IR2 * h_over_sz3));
	C2_SY2 = GteAlt_LmG2(syFull >> 16);

	NativeSubpixel_PushFrac(sxFull, syFull, (int)((sxFull >> 16) != (s64)C2_SX2), (int)((syFull >> 16) != (s64)C2_SY2));

	return h_over_sz3;
}

int GTE_operatorAlt(int op)
{
	int v;
	int cv;
	int mx;
	int h_over_sz3 = 0;

	int lm = ALT_LM(altop(op));
	s_altSf = ALT_SF(altop(op));

	C2_FLAG = 0;

	switch (ALT_FUNCT(altop(op)))
	{
	case 0x00:
	case 0x01:
		h_over_sz3 = GteAlt_RotTransPers(0, lm);

		C2_MAC0 = (int)(GteAlt_F((s64)C2_DQB + ((s64)C2_DQA * h_over_sz3)));
		C2_IR0 = GteAlt_LmH(s_altMac0, 1);

		return 1;

	case 0x06:
		C2_MAC0 = (int)(GteAlt_F((s64)(C2_SX0 * C2_SY1) + (C2_SX1 * C2_SY2) + (C2_SX2 * C2_SY0) - (C2_SX0 * C2_SY2) - (C2_SX1 * C2_SY0) - (C2_SX2 * C2_SY1)));
		C2_FLAG = 0;
		return 1;

	case 0x0c:

		C2_MAC1 = GteAlt_A1((s64)(C2_R22 * C2_IR3) - (C2_R33 * C2_IR2));
		C2_MAC2 = GteAlt_A2((s64)(C2_R33 * C2_IR1) - (C2_R11 * C2_IR3));
		C2_MAC3 = GteAlt_A3((s64)(C2_R11 * C2_IR2) - (C2_R22 * C2_IR1));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		return 1;

	case 0x10:

		C2_MAC1 = GteAlt_A1((C2_R << 16) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - (C2_R << 16)), 0)));
		C2_MAC2 = GteAlt_A2((C2_G << 16) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - (C2_G << 16)), 0)));
		C2_MAC3 = GteAlt_A3((C2_B << 16) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - (C2_B << 16)), 0)));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x11:

		C2_MAC1 = GteAlt_A1((C2_IR1 << 12) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - (C2_IR1 << 12)), 0)));
		C2_MAC2 = GteAlt_A2((C2_IR2 << 12) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - (C2_IR2 << 12)), 0)));
		C2_MAC3 = GteAlt_A3((C2_IR3 << 12) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - (C2_IR3 << 12)), 0)));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x12:

		mx = ALT_MX(altop(op));
		v = ALT_V(altop(op));
		cv = ALT_CV(altop(op));

		switch (cv)
		{
		case 2:
			C2_MAC1 = GteAlt_A1((s64)(ALT_MX12(mx) * ALT_VY(v)) + (ALT_MX13(mx) * ALT_VZ(v)));
			C2_MAC2 = GteAlt_A2((s64)(ALT_MX22(mx) * ALT_VY(v)) + (ALT_MX23(mx) * ALT_VZ(v)));
			C2_MAC3 = GteAlt_A3((s64)(ALT_MX32(mx) * ALT_VY(v)) + (ALT_MX33(mx) * ALT_VZ(v)));
			GteAlt_LmB1(GteAlt_A1(((s64)ALT_CV1(cv) << 12) + (ALT_MX11(mx) * ALT_VX(v))), 0);
			GteAlt_LmB2(GteAlt_A2(((s64)ALT_CV2(cv) << 12) + (ALT_MX21(mx) * ALT_VX(v))), 0);
			GteAlt_LmB3(GteAlt_A3(((s64)ALT_CV3(cv) << 12) + (ALT_MX31(mx) * ALT_VX(v))), 0);
			break;

		default:
			C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)ALT_CV1(cv) << 12) + (ALT_MX11(mx) * ALT_VX(v)) + (ALT_MX12(mx) * ALT_VY(v)) + (ALT_MX13(mx) * ALT_VZ(v)));
			C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)ALT_CV2(cv) << 12) + (ALT_MX21(mx) * ALT_VX(v)) + (ALT_MX22(mx) * ALT_VY(v)) + (ALT_MX23(mx) * ALT_VZ(v)));
			C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)ALT_CV3(cv) << 12) + (ALT_MX31(mx) * ALT_VX(v)) + (ALT_MX32(mx) * ALT_VY(v)) + (ALT_MX33(mx) * ALT_VZ(v)));
			break;
		}

		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		return 1;

	case 0x13:

		C2_MAC1 = GteAlt_A1((s64)(C2_L11 * C2_VX0) + (C2_L12 * C2_VY0) + (C2_L13 * C2_VZ0));
		C2_MAC2 = GteAlt_A2((s64)(C2_L21 * C2_VX0) + (C2_L22 * C2_VY0) + (C2_L23 * C2_VZ0));
		C2_MAC3 = GteAlt_A3((s64)(C2_L31 * C2_VX0) + (C2_L32 * C2_VY0) + (C2_L33 * C2_VZ0));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
		C2_MAC2 = GteAlt_A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
		C2_MAC3 = GteAlt_A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x14:

		C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
		C2_MAC2 = GteAlt_A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
		C2_MAC3 = GteAlt_A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x16:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = GteAlt_A1((s64)(C2_L11 * ALT_VX(v)) + (C2_L12 * ALT_VY(v)) + (C2_L13 * ALT_VZ(v)));
			C2_MAC2 = GteAlt_A2((s64)(C2_L21 * ALT_VX(v)) + (C2_L22 * ALT_VY(v)) + (C2_L23 * ALT_VZ(v)));
			C2_MAC3 = GteAlt_A3((s64)(C2_L31 * ALT_VX(v)) + (C2_L32 * ALT_VY(v)) + (C2_L33 * ALT_VZ(v)));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
			C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
			C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_MAC1 = GteAlt_A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
			C2_MAC2 = GteAlt_A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
			C2_MAC3 = GteAlt_A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
			C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
			C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		}
		return 1;

	case 0x1b:

		C2_MAC1 = GteAlt_A1((s64)(C2_L11 * C2_VX0) + (C2_L12 * C2_VY0) + (C2_L13 * C2_VZ0));
		C2_MAC2 = GteAlt_A2((s64)(C2_L21 * C2_VX0) + (C2_L22 * C2_VY0) + (C2_L23 * C2_VZ0));
		C2_MAC3 = GteAlt_A3((s64)(C2_L31 * C2_VX0) + (C2_L32 * C2_VY0) + (C2_L33 * C2_VZ0));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1((C2_R << 4) * C2_IR1);
		C2_MAC2 = GteAlt_A2((C2_G << 4) * C2_IR2);
		C2_MAC3 = GteAlt_A3((C2_B << 4) * C2_IR3);
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x1c:

		C2_MAC1 = GteAlt_A1(/*int44*/ (s64)(((s64)C2_RBK) << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = GteAlt_A2(/*int44*/ (s64)(((s64)C2_GBK) << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = GteAlt_A3(/*int44*/ (s64)(((s64)C2_BBK) << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1((C2_R << 4) * C2_IR1);
		C2_MAC2 = GteAlt_A2((C2_G << 4) * C2_IR2);
		C2_MAC3 = GteAlt_A3((C2_B << 4) * C2_IR3);
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x1e:

		C2_MAC1 = GteAlt_A1((s64)(C2_L11 * C2_VX0) + (C2_L12 * C2_VY0) + (C2_L13 * C2_VZ0));
		C2_MAC2 = GteAlt_A2((s64)(C2_L21 * C2_VX0) + (C2_L22 * C2_VY0) + (C2_L23 * C2_VZ0));
		C2_MAC3 = GteAlt_A3((s64)(C2_L31 * C2_VX0) + (C2_L32 * C2_VY0) + (C2_L33 * C2_VZ0));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
		C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
		C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x20:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = GteAlt_A1((s64)(C2_L11 * ALT_VX(v)) + (C2_L12 * ALT_VY(v)) + (C2_L13 * ALT_VZ(v)));
			C2_MAC2 = GteAlt_A2((s64)(C2_L21 * ALT_VX(v)) + (C2_L22 * ALT_VY(v)) + (C2_L23 * ALT_VZ(v)));
			C2_MAC3 = GteAlt_A3((s64)(C2_L31 * ALT_VX(v)) + (C2_L32 * ALT_VY(v)) + (C2_L33 * ALT_VZ(v)));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
			C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
			C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
			C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
			C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		}
		return 1;

	case 0x28:

		C2_MAC1 = GteAlt_A1(C2_IR1 * C2_IR1);
		C2_MAC2 = GteAlt_A2(C2_IR2 * C2_IR2);
		C2_MAC3 = GteAlt_A3(C2_IR3 * C2_IR3);
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		return 1;

	case 0x29:

		C2_MAC1 = GteAlt_A1(((C2_R << 4) * C2_IR1) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - ((C2_R << 4) * C2_IR1)), 0)));
		C2_MAC2 = GteAlt_A2(((C2_G << 4) * C2_IR2) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - ((C2_G << 4) * C2_IR2)), 0)));
		C2_MAC3 = GteAlt_A3(((C2_B << 4) * C2_IR3) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - ((C2_B << 4) * C2_IR3)), 0)));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x2a:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = GteAlt_A1((C2_R0 << 16) + (C2_IR0 * GteAlt_LmB1(GteAlt_A1(((s64)C2_RFC << 12) - (C2_R0 << 16)), 0)));
			C2_MAC2 = GteAlt_A2((C2_G0 << 16) + (C2_IR0 * GteAlt_LmB2(GteAlt_A2(((s64)C2_GFC << 12) - (C2_G0 << 16)), 0)));
			C2_MAC3 = GteAlt_A3((C2_B0 << 16) + (C2_IR0 * GteAlt_LmB3(GteAlt_A3(((s64)C2_BFC << 12) - (C2_B0 << 16)), 0)));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
			C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
			C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		}
		return 1;

	case 0x2d:

		C2_MAC0 = (int)(GteAlt_F((s64)(C2_ZSF3 * C2_SZ1) + (C2_ZSF3 * C2_SZ2) + (C2_ZSF3 * C2_SZ3)));
		C2_OTZ = GteAlt_LmD(s_altMac0, 1);
		return 1;

	case 0x2e:

		C2_MAC0 = (int)(GteAlt_F((s64)(C2_ZSF4 * C2_SZ0) + (C2_ZSF4 * C2_SZ1) + (C2_ZSF4 * C2_SZ2) + (C2_ZSF4 * C2_SZ3)));
		C2_OTZ = GteAlt_LmD(s_altMac0, 1);
		return 1;

	case 0x30:

		for (v = 0; v < 3; v++)
		{
			h_over_sz3 = GteAlt_RotTransPers(v, lm);
		}

		C2_MAC0 = (int)(GteAlt_F((s64)C2_DQB + ((s64)C2_DQA * h_over_sz3)));
		C2_IR0 = GteAlt_LmH(s_altMac0, 1);
		return 1;

	case 0x3d:

		C2_MAC1 = GteAlt_A1(C2_IR0 * C2_IR1);
		C2_MAC2 = GteAlt_A2(C2_IR0 * C2_IR2);
		C2_MAC3 = GteAlt_A3(C2_IR0 * C2_IR3);
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x3e:

		C2_MAC1 = GteAlt_A1(GteAlt_Shift(C2_MAC1, -s_altSf) + (C2_IR0 * C2_IR1));
		C2_MAC2 = GteAlt_A2(GteAlt_Shift(C2_MAC2, -s_altSf) + (C2_IR0 * C2_IR2));
		C2_MAC3 = GteAlt_A3(GteAlt_Shift(C2_MAC3, -s_altSf) + (C2_IR0 * C2_IR3));
		C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
		C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
		C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
		C2_RGB0 = C2_RGB1;
		C2_RGB1 = C2_RGB2;
		C2_CD2 = C2_CODE;
		C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
		C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
		C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		return 1;

	case 0x3f:

		for (v = 0; v < 3; v++)
		{
			C2_MAC1 = GteAlt_A1((s64)(C2_L11 * ALT_VX(v)) + (C2_L12 * ALT_VY(v)) + (C2_L13 * ALT_VZ(v)));
			C2_MAC2 = GteAlt_A2((s64)(C2_L21 * ALT_VX(v)) + (C2_L22 * ALT_VY(v)) + (C2_L23 * ALT_VZ(v)));
			C2_MAC3 = GteAlt_A3((s64)(C2_L31 * ALT_VX(v)) + (C2_L32 * ALT_VY(v)) + (C2_L33 * ALT_VZ(v)));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_MAC1 = GteAlt_A1(/*int44*/ (s64)((s64)C2_RBK << 12) + (C2_LR1 * C2_IR1) + (C2_LR2 * C2_IR2) + (C2_LR3 * C2_IR3));
			C2_MAC2 = GteAlt_A2(/*int44*/ (s64)((s64)C2_GBK << 12) + (C2_LG1 * C2_IR1) + (C2_LG2 * C2_IR2) + (C2_LG3 * C2_IR3));
			C2_MAC3 = GteAlt_A3(/*int44*/ (s64)((s64)C2_BBK << 12) + (C2_LB1 * C2_IR1) + (C2_LB2 * C2_IR2) + (C2_LB3 * C2_IR3));
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_MAC1 = GteAlt_A1((C2_R << 4) * C2_IR1);
			C2_MAC2 = GteAlt_A2((C2_G << 4) * C2_IR2);
			C2_MAC3 = GteAlt_A3((C2_B << 4) * C2_IR3);
			C2_IR1 = GteAlt_LmB1(C2_MAC1, lm);
			C2_IR2 = GteAlt_LmB2(C2_MAC2, lm);
			C2_IR3 = GteAlt_LmB3(C2_MAC3, lm);
			C2_RGB0 = C2_RGB1;
			C2_RGB1 = C2_RGB2;
			C2_CD2 = C2_CODE;
			C2_R2 = GteAlt_LmC1(C2_MAC1 >> 4);
			C2_G2 = GteAlt_LmC2(C2_MAC2 >> 4);
			C2_B2 = GteAlt_LmC3(C2_MAC3 >> 4);
		}
		return 1;
	}

	return 0;
}

// THE REPORT OF THE WIDE PATH.
//
// It hangs on atexit and not on a frame counter, because the question it
// was built for is a question about sums: did the wide path fire at all,
// and did a coordinate arise that no screen can hold
// any more. It is registered when the switch is read; unlike the
// flag census in platform/native_gte_flags.c it does not need the log, it
// also writes to stdout when Platform_Shutdown is already done.
void NativeGteNearDiv_Report(void)
{
	if (!g_cfg_gteNearDiv)
	{
		return;
	}

	Platform_Log("[CTR GTE] near-div: %lld saturating divides - %lld computed exactly, %lld left clamped (SZ3 == 0)\n", g_gteNearDivSaturations,
	             g_gteNearDivWide, g_gteNearDivZeroDepth);
	Platform_Log("[CTR GTE] near-div: largest |SX| %d, largest |SY| %d; hit the s16 wall %lld times in X, %lld in Y\n", g_gteNearDivMaxAbsX,
	             g_gteNearDivMaxAbsY, g_gteNearDivClampedX, g_gteNearDivClampedY);
	Platform_LogFlush();
}

// From main, when --gte-near-div was read.
void NativeGteNearDiv_Arm(void)
{
	g_cfg_gteNearDiv = 1;
	Platform_AtExitReport(NativeGteNearDiv_Report);
	printf("[CTR GTE] near-plane divide on (--gte-near-div)\n");
}
