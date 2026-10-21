#ifndef NATIVE_EXTCHASECAM_K1_H
#define NATIVE_EXTCHASECAM_K1_H

#include <platform/native_extzonephys.h>

// Part K1 (platform/native_extchasecam_k1.c): the chase camera's per-frame update frame, the kart
// world matrix and the view matrix, with their small helpers. Arguments are 32-bit addresses in the
// port's memory (native_extzonemem.h) as the original passes pointers.

// 0x122430: chase camera update (vtable +0xC). cam = the camera object, vw = the output view matrix
// (4x4), a2 = passed through to the camera flow.
void ExtZp_Cam_122430(ExtZpCtx *c, uint32_t cam, uint32_t vw, uint32_t a2);
// 0x11b260: address of the sub-block at +0xF8 of the camera manager.
uint32_t ExtZp_Cam_11b260(ExtZpCtx *c, uint32_t mgr);
// 0x11e790: sets the default smoothing values of a camera parameter block (all rates 1.0 ...).
void ExtZp_Cam_11e790(ExtZpCtx *c, uint32_t p);
// 0x11f2a0: copies a vec3 (12 bytes); returns the destination.
uint32_t ExtZp_Cam_11f2a0(ExtZpCtx *c, uint32_t dst, uint32_t src);
// 0x122d40: copies 36 bytes (a fixed camera slot: position, a vec3, up); returns the destination.
uint32_t ExtZp_Cam_122d40(ExtZpCtx *c, uint32_t dst, uint32_t src);
// 0x122d90: copies the head word of a parameter block and sets the class pointer; returns dst.
uint32_t ExtZp_Cam_122d90(ExtZpCtx *c, uint32_t dst, uint32_t src);
// 0x154d30 / 0x154de0: fields of entry n (20 bytes each) of the driver table: +0x20 (the kart) / +0x28.
uint32_t ExtZp_Cam_154d30(ExtZpCtx *c, uint32_t table, uint32_t n);
uint32_t ExtZp_Cam_154de0(ExtZpCtx *c, uint32_t table, uint32_t n);
// 0x160b90: the kart's world matrix (4x4 at m) built from kart k.
void ExtZp_Cam_160b90(ExtZpCtx *c, uint32_t k, uint32_t m);
// 0x160ca0: the kart's model matrix address ([[k+0x3c]] + 0x64); 0x171ce0 is its tail (a = [k+0x3c]).
uint32_t ExtZp_Cam_160ca0(ExtZpCtx *c, uint32_t k);
uint32_t ExtZp_Cam_171ce0(ExtZpCtx *c, uint32_t a);
// 0x1071e0: copies a 4x4 matrix (64 bytes) from src to dst.
void ExtZp_Cam_1071e0(ExtZpCtx *c, uint32_t dst, uint32_t src);
// 0x58a7a0: sets a 4x4 matrix to identity.
void ExtZp_Cam_58a7a0(ExtZpCtx *c, uint32_t m);
// 0x58b090: stores the vec3 at src into the translation row (+0x30) of matrix m.
void ExtZp_Cam_58b090(ExtZpCtx *c, uint32_t m, uint32_t src);
// 0x58b9d0: rotation part of matrix m from the quaternion (x y z w) at q (normalised first).
void ExtZp_Cam_58b9d0(ExtZpCtx *c, uint32_t m, uint32_t q);
// 0x58bd70: view matrix vw from eye, look point and up vector (vec3 addresses).
void ExtZp_Cam_58bd70(ExtZpCtx *c, uint32_t vw, uint32_t eye, uint32_t look, uint32_t up);
// 0x58d690: out = a x b (vec3 addresses).
void ExtZp_Cam_58d690(ExtZpCtx *c, uint32_t out, uint32_t a, uint32_t b);

// ---- needed from other parts (signature derived from the disassembly)
// worker K2: the camera flow per frame (0x122db0): a0 = camera, a1 = output view matrix, a2 unused there; no result.
void ExtZp_Cam_122db0(ExtZpCtx *c, uint32_t cam, uint32_t vw, uint32_t a2);

#endif
