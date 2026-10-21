#ifndef NATIVE_EXTCHASECAM_K4_H
#define NATIVE_EXTCHASECAM_K4_H

#include <platform/native_extzonephys.h>

// THE CHASE CAMERA, PART K4 (platform/native_extchasecam_k4.c): the camera's eye
// (local position, world position with floor / wall correction and smoothing)
// and the look point, plus the point-times-matrix helper they share. Conventions
// as in native_extzonephys.h (memory form). C = the camera object, P = its
// current parameter block (C + 0xC), F2 = the pivot frame (a 4x4 matrix).

// 0x58cc00: out[i] = (in[i] . matrix rows 0..2 + row 3) / w for i < count
// (vec3 arrays, stride 12; matrix at a0, any alignment; in place allowed).
void ExtZp_Cam_58cc00(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
// 0x11fe60: look point. a0 = C, a1 = pivot frame F, a2 = P.LookPos (vec3), a3 = out
// vec3 (world); f12 = wall shift, f13 = floor shift.
void ExtZp_Cam_11fe60(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, Ps2F f12, Ps2F f13);
// 0x11ed30: eye in the pivot frame (local). a0 = C, a1 = kart, a2 = frame F2,
// a3 = boost flag, t0 = out vec3; f12 = speed; f20 = the caller's live f20 (the
// source reads it for a transition state outside 0..3; callers pass their frame time).
void ExtZp_Cam_11ed30(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12, Ps2F f20);
// 0x11fcd0: eye in the world with the corrections. a0 = C, a1 = F2, a2 = U (camera up),
// a3 = Rl (eye, local), t0 = out eye vec3, t1 = out wall shift (float), t2 = out
// floor shift (float); f12 = speed.
void ExtZp_Cam_11fcd0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1, uint32_t t2, Ps2F f12);
// 0x11f2c0: floor correction (sphere test 0x11b270). a0 = C, a1 = eye (world vec3),
// a2 = U, a3 = out vec3, t0 = out float (z difference); f12 = speed.
void ExtZp_Cam_11f2c0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12);
// 0x11f670: wall correction (sphere test 0x11b270). a0 = C, a1 = F2, a2 = U, a3 = eye
// local Rl (vec3), t0 = out eye vec3, t1 = out float (shift length); f12 = speed.
void ExtZp_Cam_11f670(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1, Ps2F f12);
// 0x11f9a0: smoothing of the correction. a0 = C, a1 = F2, a2 = corrected eye (world),
// a3 = Rl, t0 = out eye vec3; f12 = speed.
void ExtZp_Cam_11f9a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12);

// ---- needed from other parts (signatures derived from the disassembly) ----
uint32_t ExtZp_Cam_11f1a0(ExtZpCtx *c, uint32_t a0, uint32_t a1);  // a0 -= a1 (vec3), returns a0
uint32_t ExtZp_Cam_11f1e0(ExtZpCtx *c, uint32_t a0);               // returns [a0 + 8]
uint32_t ExtZp_Cam_11f1f0(ExtZpCtx *c, uint32_t a0, uint32_t a1);  // returns [a0 + a1 * 4 + 4]
uint32_t ExtZp_Cam_11f200(ExtZpCtx *c);                            // returns the word at 0x006f3b00
uint32_t ExtZp_Cam_11f210(ExtZpCtx *c, uint32_t a0, uint32_t a1);  // a0 = a1 (vec3), returns a0
uint32_t ExtZp_Cam_11f230(ExtZpCtx *c, uint32_t a0, uint32_t a1);  // a0 += a1 (vec3), returns a0
uint32_t ExtZp_Cam_11f270(ExtZpCtx *c, uint32_t a0, Ps2F f12);     // a0 *= f12 (vec3), returns a0
uint32_t ExtZp_Cam_11f2a0(ExtZpCtx *c, uint32_t a0, uint32_t a1);  // a0 = a1 (vec3), returns a0
void ExtZp_Cam_1071e0(ExtZpCtx *c, uint32_t a0, uint32_t a1);      // copies a 4x4 matrix (0x40 bytes) a1 -> a0
void ExtZp_Cam_58c3a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2); // matrix inverse a1 -> a2 (a0 = status word out)

#endif
