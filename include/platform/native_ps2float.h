#ifndef NATIVE_PS2FLOAT_H
#define NATIVE_PS2FLOAT_H

#include <stdint.h>

// THE SECOND CONSOLE'S SINGLE PRECISION ARITHMETIC (platform/native_ps2float.c).
//
// WHAT. The floating point unit of that console's main CPU (coprocessor 1) and
// its vector unit 0 in macro mode do not follow IEEE 754. A port of code from
// that console that must give the same bits as the original computes with
// these functions instead of the host's float. Every value is the raw 32-bit
// pattern (Ps2F); nothing here uses the host's floating point.
//
// THE RULES (public descriptions of the hardware; the emulator community's
// soft-float research, see the notes in the .c file):
//   - no infinity, no NaN: exponent 255 is an ordinary exponent, the largest
//     magnitude is 0x7FFFFFFF (about 6.8e38);
//   - exponent 0 is zero: a denormal operand counts as a (signed) zero, a
//     result below the smallest normal becomes a signed zero (underflow);
//   - a result above the largest magnitude becomes +-0x7FFFFFFF (overflow);
//   - add/sub: the operand with the smaller exponent loses its low bits before
//     the add (one guard bit, no sticky bit), the sum is cut toward zero;
//     an exact cancellation gives +0;
//   - mul: the operand order matters (a = fs is the multiplicand, b = ft is
//     recoded): a radix-4 Booth multiplier whose carry-save tree drops low bits; the
//     product is the true product cut toward zero except that bit 15 of the
//     48-bit mantissa product may differ (rarely one unit in the last place
//     below the cut result);
//   - div, sqrt: a radix-2 SRT digit recurrence of 25 steps without a final
//     remainder correction (the last bit may be one above the cut result);
//   - multiply-add: the product is rounded first, then added; overflow of the
//     product or of the accumulator decides the result (see Ps2F_MulAddEx).
// The flags (Ps2F_FLAG_*) are reported, the arithmetic never traps.
//
// Standalone C: the module includes only <stdint.h>, so that an offline test
// can link it without the game.

typedef uint32_t Ps2F;

#define PS2F_ONE      0x3F800000u
#define PS2F_FMAX     0x7FFFFFFFu // the largest magnitude (+Fmax)
#define PS2F_FMAX_NEG 0xFFFFFFFFu // -Fmax
#define PS2F_SIGN     0x80000000u

#define PS2F_FLAG_OVERFLOW  1u
#define PS2F_FLAG_UNDERFLOW 2u
#define PS2F_FLAG_DIVZERO   4u
#define PS2F_FLAG_INVALID   8u

// The basic operations (flags may be 0).
Ps2F Ps2F_AddEx(Ps2F a, Ps2F b, unsigned *flags);
Ps2F Ps2F_SubEx(Ps2F a, Ps2F b, unsigned *flags);
Ps2F Ps2F_MulEx(Ps2F a, Ps2F b, unsigned *flags);
Ps2F Ps2F_DivEx(Ps2F a, Ps2F b, unsigned *flags);
Ps2F Ps2F_SqrtEx(Ps2F a, unsigned *flags);        // sqrt(|a|); a < 0 sets INVALID
Ps2F Ps2F_RsqrtEx(Ps2F a, Ps2F b, unsigned *flags); // a / sqrt(|b|)
Ps2F Ps2F_Add(Ps2F a, Ps2F b);
Ps2F Ps2F_Sub(Ps2F a, Ps2F b);
Ps2F Ps2F_Mul(Ps2F a, Ps2F b);
Ps2F Ps2F_Div(Ps2F a, Ps2F b);
Ps2F Ps2F_Sqrt(Ps2F a);
Ps2F Ps2F_Rsqrt(Ps2F a, Ps2F b);

// acc + a*b (sub = 0) or acc - a*b (sub = 1): the main CPU's MADD/MSUB and the
// vector unit's MADD/MSUB. accOverflow = the accumulator came from an operation
// that overflowed (its OVERFLOW flag); *flags gets OVERFLOW when the result is
// forced to +-Fmax or kept at the overflowed accumulator.
Ps2F Ps2F_MulAddEx(Ps2F acc, int accOverflow, Ps2F a, Ps2F b, int sub, unsigned *flags);
Ps2F Ps2F_MulAdd(Ps2F acc, Ps2F a, Ps2F b); // accumulator without overflow
Ps2F Ps2F_MulSub(Ps2F acc, Ps2F a, Ps2F b);

// Sign handling, compares (sign-magnitude, +0 == -0; denormals keep their
// pattern as in the hardware compare), MIN/MAX.
Ps2F Ps2F_Abs(Ps2F a);
Ps2F Ps2F_Neg(Ps2F a);
int Ps2F_Compare(Ps2F a, Ps2F b);   // -1, 0, 1
int Ps2F_Eq(Ps2F a, Ps2F b);        // C.EQ.S
int Ps2F_Lt(Ps2F a, Ps2F b);        // C.LT.S
int Ps2F_Le(Ps2F a, Ps2F b);        // C.LE.S
Ps2F Ps2F_Min(Ps2F a, Ps2F b);      // MIN.S / vector MINI
Ps2F Ps2F_Max(Ps2F a, Ps2F b);      // MAX.S / vector MAX

// Conversions. FromInt = CVT.S.W (cut toward zero above 24 bits); ToInt =
// CVT.W.S (cut toward zero, saturates to 0x7FFFFFFF / 0x80000000 from 2^31 up).
// FromFixed/ToFixed = the vector unit's ITOF0/4/12/15 and FTOI0/4/12/15
// (fracBits 0, 4, 12 or 15: the integer is a fixed point number).
Ps2F Ps2F_FromInt(int32_t i);
int32_t Ps2F_ToInt(Ps2F a);
Ps2F Ps2F_FromFixed(int32_t i, int fracBits);
int32_t Ps2F_ToFixed(Ps2F a, int fracBits);

// Host values, for logs and tests only (exact: every pattern fits a double).
double Ps2F_ToDouble(Ps2F a);
Ps2F Ps2F_FromHostFloat(float f); // the pattern of an IEEE float, Inf/NaN -> +-Fmax, denormal -> +-0

// The 4-wide vector unit in macro mode: each lane as above; mask = the
// destination lanes (bit 3 = x .. bit 0 = w, as the instruction's xyzw field);
// lanes outside the mask keep d's value.
typedef struct Ps2Vec4 { Ps2F v[4]; } Ps2Vec4; // x, y, z, w
void Ps2V_Add(Ps2Vec4 *d, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask);
void Ps2V_Sub(Ps2Vec4 *d, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask);
void Ps2V_Mul(Ps2Vec4 *d, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask);
void Ps2V_MulAdd(Ps2Vec4 *d, const Ps2Vec4 *acc, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask);
void Ps2V_MulSub(Ps2Vec4 *d, const Ps2Vec4 *acc, const Ps2Vec4 *a, const Ps2Vec4 *b, unsigned mask);
// Outer product (OPMULA / OPMSUB, xyz; w kept): OpMula: acc = (fs.y*ft.z,
// fs.z*ft.x, fs.x*ft.y); OpMsub: d = acc - (fs.y*ft.z, fs.z*ft.x, fs.x*ft.y).
// The pair OpMula(acc, a, b), OpMsub(d, acc, b, a) gives the cross product a x b.
void Ps2V_OpMula(Ps2Vec4 *acc, const Ps2Vec4 *fs, const Ps2Vec4 *ft);
void Ps2V_OpMsub(Ps2Vec4 *d, const Ps2Vec4 *acc, const Ps2Vec4 *fs, const Ps2Vec4 *ft);

// The module's own checks (hand-computed cases and reference vectors); adds
// to *checks and *failures, logs each failure through the callback (may be 0).
void Ps2F_Selftest(int *checks, int *failures, void (*log)(const char *line));

#endif
