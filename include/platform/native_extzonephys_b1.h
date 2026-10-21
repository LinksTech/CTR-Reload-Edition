#ifndef NATIVE_EXTZONEPHYS_B1_H
#define NATIVE_EXTZONEPHYS_B1_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, PART B1 (platform/native_extzonephys_b1.c): the core of the
// rigid body integration (the system's step from time t to t+h, the state
// vector of a body, its derivative, the Euler step, the buffers around them).
// Conventions as in native_extzonephys.h.
//
// Time values: the original passes a time as one 64-bit register (low word =
// whole seconds as a signed integer, high word = the fraction as a float).
// The port takes it as one uint64_t with the same layout (a "time pair").
//
// Layout facts used here (offsets in bytes):
//   system S: +0x14 embedded interface (vtable pointer), +0x24 body count, +0x28 body pointers,
//             +0x2C active vector {cap +0, size +4, data +8} (as +0x2C, data pointer at +0x34, count at +0x30),
//             +0x40 time field (pairs of {int, float}), +0x4C / +0x58 state / derivative buffers P1,
//             +0x5C/+0x60 current time, +0x68 buffer index, +0x6C no-gravity byte, +0x70 subspace,
//             +0x78/+0x7C controller count / list (8 bytes per entry), +0x80 ODE solver, +0x88 fixed step,
//             +0x94 / +0xA0 state / derivative buffers P2, +0xA5 direct-integration flag.
//   buffer record (16 bytes): +0 cached data pointer, +4 array {cap +4, size +8, data +0xC}.
//   body H: +0x18 flags (bit 0 and 2: not integrated, bit 1: active), +0x30 position, +0x40 / +0x50 quaternion w / xyz,
//           +0x60 velocity, +0x70 angular velocity, +0x80 force, +0x90 moment, +0xA4 inverse mass,
//           +0xE0 rotation rows, +0x120 world inertia rows, +0x150.. saved state, +0x1B0 mass.

// ---- ported here
void ExtZp_Integ_39adb0(ExtZpCtx *c, uint32_t sys, uint64_t t);
// Euler step: out[i] = y[i] + h * dy[i] for n floats; a2 / a3 / t1 = records (data pointer at +0). a1 = time pair (unused by the step).
void ExtZp_Integ_3f58e0(ExtZpCtx *c, uint64_t a1, uint32_t a2, uint32_t a3, uint32_t t1, uint32_t t2, Ps2F h);
void ExtZp_Integ_397bf0(ExtZpCtx *c, uint32_t body, uint32_t out);
void ExtZp_Integ_397ca0(ExtZpCtx *c, uint32_t body, uint32_t in);
void ExtZp_Integ_397ea0(ExtZpCtx *c, uint32_t body, uint32_t out);
void ExtZp_Integ_39a750(ExtZpCtx *c, uint32_t sys, uint32_t g);
uint32_t ExtZp_Integ_39a7f0(ExtZpCtx *c, uint32_t sys, uint32_t index);   // returns 13 (floats per body state)
void ExtZp_Integ_39a800(ExtZpCtx *c, uint32_t sys);
void ExtZp_Integ_39a8f0(ExtZpCtx *c, uint32_t sys);
void ExtZp_Integ_39a9a0(ExtZpCtx *c, uint32_t sys, uint32_t rec);
void ExtZp_Integ_39aa60(ExtZpCtx *c, uint32_t sys, uint32_t rec);
void ExtZp_Integ_39ab30(ExtZpCtx *c, uint32_t sys, uint32_t rec, uint32_t countp);
void ExtZp_Integ_39ad50(ExtZpCtx *c, uint32_t sys);
void ExtZp_Integ_38e3e0(ExtZpCtx *c, uint32_t sys);
void ExtZp_Integ_38e7c0(ExtZpCtx *c, uint32_t sys, uint64_t t);
void ExtZp_Integ_38ea50(ExtZpCtx *c, uint32_t sys);
void ExtZp_Integ_399000(ExtZpCtx *c, uint32_t body);
void ExtZp_Integ_399ca0(ExtZpCtx *c, uint32_t body);
uint32_t ExtZp_Integ_3908a0(ExtZpCtx *c, uint32_t dst, uint32_t src);
void ExtZp_Integ_3908f0(ExtZpCtx *c, uint32_t arr, uint32_t first, uint32_t last);
uint32_t ExtZp_Integ_36c8a0(ExtZpCtx *c, uint32_t a0);                   // returns a0
uint32_t ExtZp_Integ_39b540(ExtZpCtx *c, uint32_t a0);                   // returns a0 + 8
uint32_t ExtZp_Integ_3b15c0(ExtZpCtx *c, uint32_t a0);                   // returns a0 + 0x20
uint32_t ExtZp_Integ_3660d0(ExtZpCtx *c, uint32_t a0);                   // returns [a0 + 4]
void ExtZp_Integ_35bc90(ExtZpCtx *c, uint32_t arr, uint32_t newSize, uint32_t fillp);
uint32_t ExtZp_Integ_35bd00(ExtZpCtx *c, uint32_t arr, uint32_t first, uint32_t last);
void ExtZp_Integ_38e8b0(ExtZpCtx *c, uint32_t sys);
void ExtZp_Integ_38e9a0(ExtZpCtx *c, uint32_t sys);

// ---- needed from other parts
// (B2: native_extzonephys_b2.h) 1206c0.. 33c120, 33c170, 33eb00, 33f800, 398930, 35c3f0, 360df0, 3f6780, 3f6b20, 120750, 120780, 1207c0.
// Kart action 0x33afa0 (worker A): called through the controller's vtable +0x1C with (controller, subspace, time pair).
void ExtZp_Glue_33afa0(ExtZpCtx *c, uint32_t ctrl, uint32_t sub, uint64_t t);
// Floor / ceil (libm, one float in f12, result f0), owned by part a: 0x43b0b0 (floor) and 0x43ae78 (ceil).
Ps2F ExtZp_Step_43b0b0(ExtZpCtx *c, Ps2F x);
Ps2F ExtZp_Step_43ae78(ExtZpCtx *c, Ps2F x);

#endif
