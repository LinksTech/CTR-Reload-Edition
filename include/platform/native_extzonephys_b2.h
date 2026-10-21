#ifndef NATIVE_EXTZONEPHYS_B2_H
#define NATIVE_EXTZONEPHYS_B2_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, PART B2 (platform/native_extzonephys_b2.c): the integration
// helpers of the physics step (vector and matrix helpers, the direct
// integration of a body, a small vector container and a fill routine).
// Conventions as in native_extzonephys.h. Addresses are 32-bit addresses in the
// port's memory; "vec4" = a 16-byte vector (x y z w) at a 16-byte aligned address.

// ---- vector helpers
uint32_t ExtZp_Integ_1206c0(ExtZpCtx *c, uint32_t dst, uint32_t src);                 // dst = src (vec4); returns dst
void ExtZp_Integ_1206f0(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b);           // dst.xyz = a.xyz + b.xyz, dst.w = a.w
void ExtZp_Integ_120750(ExtZpCtx *c, uint32_t dst, uint32_t a, Ps2F s);               // dst.xyz = a.xyz * s, dst.w = a.w
Ps2F ExtZp_Integ_120780(ExtZpCtx *c, uint32_t a, uint32_t b);                         // dot product of the xyz parts (returns f0)
uint32_t ExtZp_Integ_1207b0(ExtZpCtx *c, uint32_t dst, uint32_t src);                 // dst = src (vec4); returns dst
void ExtZp_Integ_1207c0(ExtZpCtx *c, uint32_t dst, uint32_t a, Ps2F s);               // same as 120750
uint32_t ExtZp_Integ_34bb20(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b);       // dst.xyz = a.xyz - b.xyz, dst.w = a.w; returns dst
uint32_t ExtZp_Integ_35bc40(ExtZpCtx *c, uint32_t dst, uint32_t m4, uint32_t v);      // dst.xyz = v.x*row0 + v.y*row1 + v.z*row2 + row3 (4 vec4 rows at m4), dst.w = v.w; returns dst
uint32_t ExtZp_Integ_35c3f0(ExtZpCtx *c, uint32_t dst, uint32_t m3, uint32_t v);      // dst.xyz = v.x*row0 + v.y*row1 + v.z*row2 (3 vec4 rows at m3), dst.w = row2.w; returns dst
uint32_t ExtZp_Integ_360df0(ExtZpCtx *c, uint32_t v3, Ps2F d);                        // three floats at v3 divided by d; returns v3
void ExtZp_Integ_3f6780(ExtZpCtx *c, uint32_t m3, uint32_t q);                        // rotation matrix (3 rows of 4 floats, w slots untouched) from a quaternion (w at q+0, x y z at q+0x10)
uint32_t ExtZp_Integ_3f6b20(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b);       // dst = a * b (3x3 matrices stored as 3 vec4 rows); returns dst

// ---- body steps
void ExtZp_Integ_395bc0(ExtZpCtx *c, uint32_t obj);                                   // moves the two edge vectors of an object by its transform (skipped for state 0x10 at +0x14)
void ExtZp_Integ_398930(ExtZpCtx *c, uint32_t body, Ps2F dt);                         // direct integration of a body for a time step dt

// ---- allocation wrappers and the vector container
uint32_t ExtZp_Integ_33c120(ExtZpCtx *c, uint32_t size, uint32_t tag);                // allocate size bytes with a 16-byte header; returns the block + 0x10
void ExtZp_Integ_33c170(ExtZpCtx *c, uint32_t block);                                 // free a block of 33c120 (nothing meaningful is returned)
void ExtZp_Integ_33f800(ExtZpCtx *c, uint32_t vec, uint32_t pos, uint32_t n, uint32_t valp); // insert n copies of the word at valp at pos into the word vector {cap +0, size +4, data +8}
uint32_t ExtZp_Integ_33eb00(ExtZpCtx *c, uint32_t dst, uint32_t value, uint32_t n);   // jumps to 428998
uint32_t ExtZp_Integ_428998(ExtZpCtx *c, uint32_t dst, uint32_t value, uint32_t n);   // fills n bytes at dst with the low byte of value; returns dst

// Not part of B2: the allocator 0x33c1b0 and its free 0x33c3b0 and the two output routines
// 0x429f38 / 0x42bd70 are handed to Reload as EXT_ZP_Q_OTHER queries (a0.. as the original).

#endif
