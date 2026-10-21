#ifndef NATIVE_EXTCHASECAM_K2_H
#define NATIVE_EXTCHASECAM_K2_H

#include <platform/native_extzonephys.h>

// THE CHASE CAMERA PORT, PART K2 (platform/native_extchasecam_k2.c): the camera's
// per-frame flow, its timers, the velocity mean over several frames, the camera
// up vector, the parameter block choice and the small vector/table helpers they
// use. One C function per function of the source game's executable (see
// native_extzonephys.h for the conventions). Addresses are the original's.
// C = the camera object, K = the kart object, P = the camera's parameter block.

// Fields of the camera object C.
enum
{
	EXT_ZP_CAM_VTABLE = 0x04,
	EXT_ZP_CAM_TARGET = 0x08,     // s32: the player / driver index the camera follows
	EXT_ZP_CAM_PARAMS = 0x0C,     // ptr: the current parameter block P
	EXT_ZP_CAM_UP = 0x3C,         // vec3: the camera up vector (smoothed)
	EXT_ZP_CAM_HOLD = 0x88,       // u8: velocity hold flag
	EXT_ZP_CAM_T_ON = 0x90,       // f32: timer
	EXT_ZP_CAM_T_OFF = 0x94,      // f32: timer
	EXT_ZP_CAM_T2D_ON = 0x9C,     // f32: time since flag 0xA4 is set
	EXT_ZP_CAM_T2D_OFF = 0xA0,    // f32: time since flag 0xA4 is clear
	EXT_ZP_CAM_FLAG2 = 0xA4,      // u8: second flag (2D velocity transition)
	EXT_ZP_CAM_VRING = 0xA8,      // vec3[10]: velocity ring buffer
	EXT_ZP_CAM_VRING_IDX = 0x120, // s32: ring write index
	EXT_ZP_CAM_VRING_FULL = 0x124,// u8: ring filled once
	EXT_ZP_CAM_VLAST = 0x128,     // vec3: last velocity vector
};

// ---- ported here
uint32_t ExtZp_Cam_11d8d0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t ExtZp_Cam_128060(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Cam_11f1a0(ExtZpCtx *c, uint32_t a0, uint32_t a1);          // returns a0
uint32_t ExtZp_Cam_11f1e0(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Cam_11f1f0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Cam_11f200(ExtZpCtx *c);
uint32_t ExtZp_Cam_11f210(ExtZpCtx *c, uint32_t a0, uint32_t a1);          // returns a0
uint32_t ExtZp_Cam_11f230(ExtZpCtx *c, uint32_t a0, uint32_t a1);          // returns a0
uint32_t ExtZp_Cam_11f270(ExtZpCtx *c, uint32_t a0, Ps2F f12);             // returns a0
uint32_t ExtZp_Cam_1238a0(ExtZpCtx *c, uint32_t a0, uint32_t a1);          // returns a0
uint32_t ExtZp_Cam_1238d0(ExtZpCtx *c, uint32_t a0, uint32_t a1);          // returns a0
uint32_t ExtZp_Cam_123910(ExtZpCtx *c, uint32_t a0);                       // returns a0
void ExtZp_Cam_123920(ExtZpCtx *c, uint32_t a0, uint32_t a1);
void ExtZp_Cam_153f90(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Cam_154e00(ExtZpCtx *c, uint32_t a0, uint32_t a1);
void ExtZp_Cam_1544a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
void ExtZp_Cam_1544b0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
void ExtZp_Cam_58d220(ExtZpCtx *c, uint32_t a0, uint32_t a1);              // quaternion (a0) -> 4x4 matrix (a1)
void ExtZp_Cam_1239e0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                      Ps2F f12, Ps2F f13, Ps2F f14, Ps2F f15);
void ExtZp_Cam_121d80(ExtZpCtx *c, uint32_t cam, uint32_t a1, uint32_t a2);
void ExtZp_Cam_121710(ExtZpCtx *c, uint32_t cam, uint32_t kart, uint32_t flag, uint32_t a3, uint32_t t0,
                      uint32_t outVec, uint32_t outSpeed);
void ExtZp_Cam_121b60(ExtZpCtx *c, uint32_t cam, uint32_t a1, uint32_t a2, uint32_t a3);
void ExtZp_Cam_121e70(ExtZpCtx *c, uint32_t cam);
// The camera's per-frame flow (called by 122430). cam = C, vw = the view matrix object the result goes to (0 = none), a2 unused. No return value.
void ExtZp_Cam_122db0(ExtZpCtx *c, uint32_t cam, uint32_t vw, uint32_t a2);

// ---- needed from other parts (signatures from the disassembly)
uint32_t ExtZp_Cam_154d30(ExtZpCtx *c, uint32_t table, uint32_t n);        // driver record n of the table (returns pointer)
void ExtZp_Cam_58a7a0(ExtZpCtx *c, uint32_t m);                            // 4x4 identity into m
void ExtZp_Cam_160b90(ExtZpCtx *c, uint32_t k, uint32_t m);                // kart frame matrix into m
void ExtZp_Cam_1071e0(ExtZpCtx *c, uint32_t dst, uint32_t src);            // 4x4 copy
void ExtZp_Cam_58bd70(ExtZpCtx *c, uint32_t vw, uint32_t eye, uint32_t look, uint32_t up); // look-at matrix
void ExtZp_Cam_120c40(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, Ps2F f12);
void ExtZp_Cam_120b10(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
void ExtZp_Cam_11ed30(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12, Ps2F f20);
void ExtZp_Cam_11fcd0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1, uint32_t t2, Ps2F f12);
void ExtZp_Cam_11fe60(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, Ps2F f12, Ps2F f13);
// boundary (EXT_ZP_Q_OTHER, args.func set): 0x122190 field of view, 0x11C260 listener, 0x122030 teleport camera,
// 0x167490 special camera test (returns v0).
uint32_t ExtZp_Cam_11f2a0(ExtZpCtx *c, uint32_t a0, uint32_t a1);          // copies a vec3 (a1 -> a0), returns a0
uint32_t ExtZp_Cam_11b260(ExtZpCtx *c, uint32_t a0);                       // returns a0 + 0xF8
void ExtZp_Cam_123ac0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                      Ps2F f12, Ps2F f13, Ps2F f14, Ps2F f15);             // vec3 blend a1 -> a2 into t0

#endif
