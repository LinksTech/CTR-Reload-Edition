#ifndef NATIVE_EXTCHASECAM_K3_H
#define NATIVE_EXTCHASECAM_K3_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, CAMERA PART K3 (platform/native_extchasecam_k3.c): the chase
// camera's look direction and pivot frame, the 4x4 matrix inverse, the vector
// helpers and the drop helper. Conventions: include/platform/native_extzonephys.h.
// C = the camera object, P = its parameter block (C + 0xC), F / F2 = 4x4 frames
// (rows right, forward, up, position), M = the kart's world matrix.

// Look direction + pivot frame: F = frame built from M and the smoothed look
// direction (C + 0x18), Vm = the velocity direction, f12 = the speed.
// a3 = reverse/inverse mode flag, t0 = slide flag, t1 = slide-weight flag, t2 unused.
void ExtZp_Cam_120c40(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, Ps2F f12);
// Drop helper: F2 = F lowered along the frame's up row while a1 != 0.
void ExtZp_Cam_120b10(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
// Blend weight vector: *t0 = blend of *a1 and *a2 by a weight from f12..f15 (a3 selects the mode).
void ExtZp_Cam_123ac0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                      Ps2F f12, Ps2F f13, Ps2F f14, Ps2F f15);
// Row-copy / cross-product helpers (4 floats per row; the cross product keeps w = 1.0).
uint32_t ExtZp_Cam_1215f0(ExtZpCtx *c, uint32_t a0, uint32_t a1); // returns a0
void ExtZp_Cam_121620(ExtZpCtx *c, uint32_t a0, uint32_t a1);     // row 1 of a1 -> a0
uint32_t ExtZp_Cam_121650(ExtZpCtx *c, uint32_t a0, uint32_t a1); // row 0 of a1 -> a0, returns a0
void ExtZp_Cam_121680(ExtZpCtx *c, uint32_t a0, uint32_t a1);     // row 2 of a1 -> a0
void ExtZp_Cam_1216b0(ExtZpCtx *c, uint32_t a0, uint32_t a1);     // row 0 of a1 -> a0
Ps2F ExtZp_Cam_1216e0(ExtZpCtx *c, uint32_t a0);                  // returns [a0 + 8]
Ps2F ExtZp_Cam_1216f0(ExtZpCtx *c, uint32_t a0);                  // returns [a0 + 4]
Ps2F ExtZp_Cam_121700(ExtZpCtx *c, uint32_t a0);                  // returns [a0]
void ExtZp_Cam_58dac0(ExtZpCtx *c, uint32_t a0, uint32_t a1);     // a0.xyz = a0 x a1, a0.w = 1.0
// Matrix inverse: a1 = result (4x4), a2 = input; *a0 = a status word (from two globals).
void ExtZp_Cam_58c3a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
void ExtZp_Cam_58c4a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2); // adjugate of a1 -> a2
Ps2F ExtZp_Cam_58c700(ExtZpCtx *c, uint32_t a0);                  // determinant of a0 (returned in f0)
// a1[0..a3) vec3 (12-byte stride, any alignment) times the 3x3 of matrix a0 -> a2.
void ExtZp_Cam_58cb30(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);

// needed from other parts (signatures derived from the disassembly)
void ExtZp_Cam_1071e0(ExtZpCtx *c, uint32_t a0, uint32_t a1);     // copy 4x4 (64 bytes) a1 -> a0
uint32_t ExtZp_Cam_11f1a0(ExtZpCtx *c, uint32_t a0, uint32_t a1); // vec3 a0 -= a1, returns a0
uint32_t ExtZp_Cam_11f1e0(ExtZpCtx *c, uint32_t a0);              // returns u32 [a0 + 8]
uint32_t ExtZp_Cam_11f1f0(ExtZpCtx *c, uint32_t a0, uint32_t a1); // returns u32 [a0 + a1 * 4 + 4]
uint32_t ExtZp_Cam_11f200(ExtZpCtx *c);                           // returns u32 [0x6F3B00]
uint32_t ExtZp_Cam_11f210(ExtZpCtx *c, uint32_t a0, uint32_t a1); // vec3 copy a1 -> a0, returns a0
uint32_t ExtZp_Cam_11f230(ExtZpCtx *c, uint32_t a0, uint32_t a1); // vec3 a0 += a1, returns a0
uint32_t ExtZp_Cam_11f270(ExtZpCtx *c, uint32_t a0, Ps2F f12);    // vec3 a0 *= f12, returns a0
uint32_t ExtZp_Cam_11f2a0(ExtZpCtx *c, uint32_t a0, uint32_t a1); // vec3 copy a1 -> a0, returns a0
uint32_t ExtZp_Cam_154e00(ExtZpCtx *c, uint32_t a0, uint32_t a1); // returns u32 [a0 + a1 * 20 + 0x2C]

#endif
