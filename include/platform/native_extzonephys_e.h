#ifndef NATIVE_EXTZONEPHYS_E_H
#define NATIVE_EXTZONEPHYS_E_H

#include <platform/native_extzonephys.h>

// PART E: the single precision math library of the source's executable
// (platform/native_extzonephys_e.c): acos, cos, sin and their kernels. Float
// arguments in f12, f13; the result in f0 (Ps2F). The integer arguments come
// first (a0, a1, ...), then the floats.

// acosf: the wrapper with the error path (|x| > 1 -> queries EXT_ZP_Q_OTHER).
Ps2F ExtZp_Math_43bc40(ExtZpCtx *c, Ps2F x);
// acosf core.
Ps2F ExtZp_Math_436920(ExtZpCtx *c, Ps2F x);
// sqrtf (bit loop).
Ps2F ExtZp_Math_437e60(ExtZpCtx *c, Ps2F x);
// fabsf.
Ps2F ExtZp_Math_43b068(ExtZpCtx *c, Ps2F x);
// isnanf: 1 when the pattern is above the infinity pattern.
uint32_t ExtZp_Math_43b198(ExtZpCtx *c, Ps2F x);
// cosf / sinf.
Ps2F ExtZp_Math_43af80(ExtZpCtx *c, Ps2F x);
Ps2F ExtZp_Math_43b320(ExtZpCtx *c, Ps2F x);
// Argument reduction: y[0], y[1] (at address y) = x reduced to +-pi/4; returns n.
uint32_t ExtZp_Math_437a80(ExtZpCtx *c, uint32_t y, Ps2F x);
// Reduction kernel for large arguments (a0 = x array, a1 = y, a2 = exponent,
// a3 = word count, t0 = precision, t1 = the 2/pi table); returns n & 7.
uint32_t ExtZp_Math_439080(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1);
// Kernels: cos(x + y) and sin(x + y) for |x| <= pi/4 (iy = 0: y is zero).
Ps2F ExtZp_Math_438f28(ExtZpCtx *c, Ps2F x, Ps2F y);
Ps2F ExtZp_Math_4399d0(ExtZpCtx *c, uint32_t iy, Ps2F x, Ps2F y);
// scalbn(x, n).
Ps2F ExtZp_Math_43b1c0(ExtZpCtx *c, uint32_t n, Ps2F x);
// copysign(x, y).
Ps2F ExtZp_Math_43af50(ExtZpCtx *c, Ps2F x, Ps2F y);
// floorf (called by the reduction kernel; not in part E's list, ported here because it is needed).
Ps2F ExtZp_Math_43b0b0(ExtZpCtx *c, Ps2F x);

#endif
