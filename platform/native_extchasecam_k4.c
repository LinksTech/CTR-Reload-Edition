// THE CHASE CAMERA, PART K4: the camera's eye and look point (see
// include/platform/native_extchasecam_k4.h and native_extzonephys.h for the
// conventions). One C function per source function, in the source's order of
// operations; registers of the source are plain C variables named like them.

#include <platform/native_extzonephys.h>
#include <platform/native_extchasecam_k4.h>
#include <string.h>

#define CAM_ONE 0x3F800000u // 1.0

// Camera object C: fields used here.
#define CAM_C_PARAMS 0xCu       // ptr: the current parameter block P
#define CAM_C_EYE 0x24u         // vec3: eye, local (smoothed)
#define CAM_C_SMOOTH 0x30u      // vec3: smoothed correction of the eye (local)
#define CAM_C_HOLD 0x8Cu        // float: seconds since the hold ended
#define CAM_C_TRAINFLAG 0x136u  // u8: train-collision marker
#define CAM_C_TRANSSTATE 0x14Cu // s32: transition state 0..3
#define CAM_C_TRANSTIME 0x150u  // float: time in the transition state

// Constants of the source.
#define CAM_CONST_150 0x43160000u // 150.0 (the original's constant at 0x006122f0)
#define CAM_CONST_ONE 0x3F800000u // 1.0 (the original's constant at 0x006122f8)
#define CAM_MS_TO_S 0x3A83126Fu   // 0.001 (immediate in the source)

#define LF(a) ZmRF(m, (a))
#define SF(a, v) ZmWF(m, (a), (v))

// The 16 bytes at any address, as the source's lq / mtsab / qfsrv pair reads them.
static Ps2Vec4 CamRead16(ExtZpMem *m, uint32_t a)
{
	Ps2Vec4 v;
	if ((a & 0xFu) == 0u)
		return ZmRV(m, a);
	ExtZpMem_Read(m, a, v.v, 16);
	return v;
}

// The source's clamp of a ratio to 0..1 (compare / move sequence): "f1 = x < 1 ? x : 1;
// if f1 <= 0 result 0, else x < 1 ? x : 1.0".
static Ps2F CamClamp01(Ps2F x)
{
	Ps2F f1 = CAM_ONE;
	if (Ps2F_Lt(x, f1))
		f1 = x;
	if (Ps2F_Le(f1, 0u))
		return 0u;
	return Ps2F_Lt(x, CAM_ONE) ? x : CAM_ONE;
}

// The source's frame-time conversion: milliseconds (int) to float with the unsigned
// fix-up for negative values (cvt.s.w of (v >> 1 | v & 1), doubled).
static Ps2F CamMsToFloat(uint32_t v0)
{
	Ps2F f2;
	if ((int32_t)v0 < 0)
	{
		uint32_t v1 = (v0 >> 1) | (v0 & 1u);
		f2 = Ps2F_FromInt((int32_t)v1);
		f2 = Ps2F_Add(f2, f2);
	}
	else
		f2 = Ps2F_FromInt((int32_t)v0);
	return f2;
}

// The frame time of the game state in milliseconds (the source: 0x11f200 -> 0x11f1f0(., 0) -> 0x11f1e0).
static uint32_t CamFrameMs(ExtZpCtx *c)
{
	uint32_t r = ExtZp_Cam_11f200(c);
	r = ExtZp_Cam_11f1f0(c, r, 0u);
	return ExtZp_Cam_11f1e0(c, r);
}

// 0x58cc00: transforms `count` points (vec3, stride 12) by a 4x4 matrix at a0 (any
// alignment): sum of row_k * p_k plus row 3 (vector unit accumulator: mulax,
// madday, maddaz, maddw), then times 1 / w of the sum (vdiv, vmulq on xyz).
void ExtZp_Cam_58cc00(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 vf0 = {{0u, 0u, 0u, CAM_ONE}};
	Ps2Vec4 vf1, vf2, vf3, vf4, vf9, vf17, bc, bq;
	Ps2VAcc acc;
	uint32_t t0, src, dst;
	Ps2F q;
	int k;
	vf1 = CamRead16(m, a0);
	vf2 = CamRead16(m, a0 + 0x10u);
	vf3 = CamRead16(m, a0 + 0x20u);
	vf4 = CamRead16(m, a0 + 0x30u);
	for (t0 = 0u; t0 < a3; t0++)
	{
		src = a1 + 12u * t0;
		dst = a2 + 12u * t0;
		vf17 = CamRead16(m, src);
		bc = Ps2V_Bc(&vf17, 0);
		Ps2V_Mula(&acc, &vf1, &bc, 0xFu);
		bc = Ps2V_Bc(&vf17, 1);
		Ps2V_Madda(&acc, &vf2, &bc, 0xFu);
		bc = Ps2V_Bc(&vf17, 2);
		Ps2V_Madda(&acc, &vf3, &bc, 0xFu);
		bc = Ps2V_Bc(&vf0, 3);
		Ps2V_MaddAcc(&vf9, &acc, &vf4, &bc, 0xFu);
		q = Ps2F_Div(vf0.v[3], vf9.v[3]); // vdiv Q, vf0w, vf9w
		for (k = 0; k < 4; k++)
			bq.v[k] = q;
		Ps2V_Mul(&vf9, &vf9, &bq, 0xEu); // vmulq.xyz
		ZmW32(m, dst, vf9.v[0]);
		ZmW32(m, dst + 4u, vf9.v[1]);
		ZmW32(m, dst + 8u, vf9.v[2]);
	}
}

// 0x11fe60: the look point in the world. Local point = LookPos with z raised by
// wallShift * P.LookMoveUpWall + floorShift * P.LookMoveUpFloor, then through the
// pivot frame (0x58cc00).
void ExtZp_Cam_11fe60(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, Ps2F f12, Ps2F f13)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x40u);
	uint32_t v0;
	Ps2F f0, f1, f2, f3, f4;
	f4 = LF(a2 + 0u);
	f3 = LF(a2 + 4u);
	v0 = ZmR32(m, a0 + CAM_C_PARAMS);
	f2 = LF(a2 + 8u);
	SF(sp + 0x20u, f4);
	SF(sp + 0x24u, f3);
	f1 = LF(v0 + 0xBCu); // P.LookMoveUpByPercentOfWallCollision
	f0 = LF(v0 + 0xC0u); // P.LookMoveUpByPercentOfFloorCollision
	f1 = Ps2F_Mul(f12, f1);
	f0 = Ps2F_Mul(f13, f0);
	f1 = Ps2F_Add(f2, f1);
	f0 = Ps2F_Add(f1, f0);
	SF(sp + 0x28u, f0);
	f0 = LF(sp + 0x28u);
	SF(sp + 0x30u, f4);
	SF(sp + 0x34u, f3);
	SF(sp + 0x38u, f0);
	ExtZp_Cam_58cc00(c, a1, sp + 0x30u, sp + 0x30u, 1u);
	ZmW32(m, a3, ZmR32(m, sp + 0x30u));
	ZmW32(m, a3 + 4u, ZmR32(m, sp + 0x34u));
	ZmW32(m, a3 + 8u, ZmR32(m, sp + 0x38u));
	ExtZp_Pop(c, 0x40u);
}

// 0x11ed30: eye in the pivot frame (local). Target = blend of the near / far
// positions by speed (or the boost position), changed by the jump box (0x120810)
// and the train-collision position (P.CamRelPosDuringTrainCollision), blended with
// the transition position by the transition state; then smoothed:
// eye += rate * (target - eye), rate = P rate (times frame time when
// P.CompensateForDTime), at most 1. The result is also written to t0.
void ExtZp_Cam_11ed30(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12, Ps2F f20in)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x130u);
	uint32_t s5 = a0, s4 = a1, s3 = a2, s2 = a3, s1 = t0, s0 = 0u, v0, v1 = 0u;
	Ps2F f0, f1, f2, f20 = f20in, f21 = 0u;
	ExtZpQueryArgs q;

	if (s2 != 0u)
	{
		v0 = ZmR32(m, s5 + CAM_C_PARAMS);
		ZmW32(m, sp + 0x120u, ZmR32(m, v0 + 0x40u)); // P.PosBehindBoost
		ZmW32(m, sp + 0x124u, ZmR32(m, v0 + 0x44u));
		ZmW32(m, sp + 0x128u, ZmR32(m, v0 + 0x48u));
	}
	else
	{
		s0 = ZmR32(m, s5 + CAM_C_PARAMS);
		f21 = 0u;
		f2 = LF(s0 + 0x20u); // P.SpeedForNearPos
		f0 = LF(s0 + 0x24u); // P.SpeedForFarPos
		f1 = Ps2F_Sub(f0, f2);
		if (Ps2F_Le(f1, f21))
		{
			f21 = Ps2F_Lt(f12, f2) ? 0u : CAM_ONE;
		}
		else
		{
			f0 = Ps2F_Sub(f12, f2);
			f21 = CamClamp01(Ps2F_Div(f0, f1));
		}
		ExtZp_Cam_11f2a0(c, sp + 0xF0u, s0 + 8u); // P.PosBehindNear
		ExtZp_Cam_11f270(c, sp + 0xF0u, Ps2F_Sub(CAM_ONE, f21));
		ExtZp_Cam_11f2a0(c, sp + 0xE0u, s0 + 0x14u); // P.PosBehindFar
		ExtZp_Cam_11f270(c, sp + 0xE0u, f21);
		ExtZp_Cam_11f2a0(c, sp + 0xD0u, sp + 0xF0u);
		ExtZp_Cam_11f230(c, sp + 0xD0u, sp + 0xE0u);
		ExtZp_Cam_11f210(c, sp + 0x120u, sp + 0xD0u);
	}
	// Lee50
	{
		Ps2F g2 = LF(s3 + 0x34u), g1 = LF(s3 + 0x38u), g0 = LF(s3 + 0x30u);
		SF(sp + 0x104u, g2);
		SF(sp + 0x108u, g1);
		SF(sp + 0x114u, g2);
		SF(sp + 0x118u, g1);
		SF(sp + 0x100u, g0);
		SF(sp + 0x110u, g0);
	}
	// 0x120810 (the jump box, outside the path): C, buffer sp+0x110, eye sp+0x120 twice
	memset(&q, 0, sizeof(q));
	q.func = 0x00120810u;
	q.a[0] = s5;
	q.a[1] = sp + 0x110u;
	q.a[2] = sp + 0x120u;
	q.a[3] = sp + 0x120u;
	(void)ExtZp_Query(c, EXT_ZP_Q_OTHER, &q);
	v0 = ZmR32(m, s5 + CAM_C_PARAMS);
	f21 = (s2 == 0u) ? LF(v0 + 0x4Cu)   // P.BehindAdjustmentPercentPerNormalFrame
	                 : LF(v0 + 0x50u);  // P.BehindAdjustmentPercentPerBoostFrame
	// 0x1674c0 (train-collision test, outside the path): a0 = kart
	memset(&q, 0, sizeof(q));
	q.func = 0x001674C0u;
	q.a[0] = s4;
	v0 = ExtZp_Query(c, EXT_ZP_Q_OTHER, &q).v0;
	if (v0 != 0u)
	{
		// train collision: target = P.CamRelPosDuringTrainCollision, no smoothing rate
		v0 = ZmR32(m, s5 + CAM_C_PARAMS);
		f21 = 0u;
		ZmW32(m, sp + 0x120u, ZmR32(m, v0 + 0xE8u));
		ZmW32(m, sp + 0x124u, ZmR32(m, v0 + 0xECu));
		ZmW32(m, sp + 0x128u, ZmR32(m, v0 + 0xF0u));
		if (ZmR8(m, s5 + CAM_C_TRAINFLAG) == 0u)
		{
			ZmW32(m, s5 + CAM_C_EYE, ZmR32(m, sp + 0x120u));
			ZmW32(m, s5 + CAM_C_EYE + 4u, ZmR32(m, sp + 0x124u));
			ZmW32(m, s5 + CAM_C_EYE + 8u, ZmR32(m, sp + 0x128u));
		}
		ZmW8(m, s5 + CAM_C_TRAINFLAG, 1u);
	}
	else if (ZmR8(m, s5 + CAM_C_TRAINFLAG) != 0u)
	{
		// the marker was set: the eye jumps to the target once
		ZmW32(m, s5 + CAM_C_EYE, ZmR32(m, sp + 0x120u));
		ZmW32(m, s5 + CAM_C_EYE + 4u, ZmR32(m, sp + 0x124u));
		ZmW32(m, s5 + CAM_C_EYE + 8u, ZmR32(m, sp + 0x128u));
		ZmW8(m, s5 + CAM_C_TRAINFLAG, 0u);
	}
	else
		ZmW8(m, s5 + CAM_C_TRAINFLAG, 0u);
	// Leec4: transition blend
	v1 = ZmR32(m, s5 + CAM_C_TRANSSTATE);
	if (v1 != 0u)
	{
		if (v1 == 3u)
		{
			v1 = ZmR32(m, s5 + CAM_C_PARAMS);
			f2 = LF(s5 + CAM_C_TRANSTIME);
			f1 = LF(v1 + 0x1D0u); // P.duration of state 3
			f1 = Ps2F_Div(f2, f1);
			f20 = Ps2F_Sub(CAM_ONE, f1);
		}
		else if (v1 == 2u)
			f20 = CAM_ONE;
		else if (v1 == 1u)
		{
			v0 = ZmR32(m, s5 + CAM_C_PARAMS);
			f1 = LF(s5 + CAM_C_TRANSTIME);
			f0 = LF(v0 + 0x1CCu); // P.duration of state 1
			f20 = Ps2F_Div(f1, f0);
		}
		else
			; // the source keeps its caller's live f20 here (parameter f20in)
		{
			v0 = ZmR32(m, s5 + CAM_C_PARAMS);
			s0 = v0 + 0x1D4u; // P.transition position
			ExtZp_Cam_11f2a0(c, sp + 0xC0u, sp + 0x120u);
			ExtZp_Cam_11f270(c, sp + 0xC0u, Ps2F_Sub(CAM_ONE, f20));
			ExtZp_Cam_11f2a0(c, sp + 0xB0u, s0);
			ExtZp_Cam_11f270(c, sp + 0xB0u, f20);
			ExtZp_Cam_11f2a0(c, sp + 0xA0u, sp + 0xC0u);
			ExtZp_Cam_11f230(c, sp + 0xA0u, sp + 0xB0u);
			ExtZp_Cam_11f210(c, sp + 0x120u, sp + 0xA0u);
		}
	}
	// Lefb8: rate compensation by frame time
	v0 = ZmR32(m, s5 + CAM_C_PARAMS);
	if (ZmR8(m, v0 + 0x5Cu) != 0u) // P.CompensateForDTime
	{
		Ps2F ms = CamMsToFloat(CamFrameMs(c));
		f1 = Ps2F_Mul(CAM_MS_TO_S, ms);
		f0 = Ps2F_Mul(f1, CAM_CONST_150);
		f21 = Ps2F_Mul(f21, f0);
		if (!Ps2F_Lt(f21, CAM_ONE))
			f21 = CAM_ONE;
	}
	// L058: eye += rate * (target - eye)
	ExtZp_Cam_11f2a0(c, sp + 0x90u, sp + 0x120u);
	ExtZp_Cam_11f1a0(c, sp + 0x90u, s5 + CAM_C_EYE);
	ExtZp_Cam_11f270(c, sp + 0x90u, f21);
	ExtZp_Cam_11f2a0(c, sp + 0x80u, s5 + CAM_C_EYE);
	ExtZp_Cam_11f230(c, sp + 0x80u, sp + 0x90u);
	ExtZp_Cam_11f210(c, s1, sp + 0x80u);
	ZmW32(m, s5 + CAM_C_EYE, ZmR32(m, s1));
	ZmW32(m, s5 + CAM_C_EYE + 4u, ZmR32(m, s1 + 4u));
	ZmW32(m, s5 + CAM_C_EYE + 8u, ZmR32(m, s1 + 8u));
	ExtZp_Pop(c, 0x130u);
}

// Calls the camera sphere test 0x11b270 (a0 = start, a1 = end, a2 = mask, a3 = out
// record, f12 = radius); returns the low byte of its result.
static uint32_t CamSphere(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, Ps2F f12)
{
	ExtZpQueryArgs q;
	memset(&q, 0, sizeof(q));
	q.func = 0x0011B270u;
	q.a[0] = a0;
	q.a[1] = a1;
	q.a[2] = a2;
	q.a[3] = a3;
	q.f[0] = f12;
	return ExtZp_Query(c, EXT_ZP_Q_CAMERA_SPHERE, &q).v0 & 0xFFu;
}

// 0x11f2c0: floor correction of the eye. Takes the eye (a1, world), the camera up
// vector (a2, normalised in the plane steps below), tests a sphere against the
// floor when the speed is below P.FloorColNoCollisionAboveThisSpeed, and blends the
// eye towards the hit by the speed window (P.FloorColBelowThisSpeed ...). Writes the
// corrected eye to a3 and the z difference (corrected - input) to t0.
void ExtZp_Cam_11f2c0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x100u);
	uint32_t s5 = a0, s4 = a1, s3 = a3, s2 = t0, v1, v2;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12r, f13, f20, f21, f22, f23;
	Ps2Acc acc;
	uint32_t hit = 0u;
	int fcc;

	f9 = LF(a2 + 4u);
	f11 = LF(a2 + 0u);
	f6 = LF(a2 + 8u);
	f2 = LF(a1 + 0u);
	SF(sp + 0xD4u, f9);
	f3 = LF(sp + 0xD4u);
	SF(sp + 0xD8u, f6);
	f4 = LF(sp + 0xD8u);
	f8 = Ps2F_Mul(f11, f11);
	f3 = Ps2F_Mul(f3, f3);
	acc = Ps2A_Adda(f8, f3);
	f3 = Ps2A_Madd(acc, f4, f4);
	f3 = Ps2F_Sqrt(f3);
	f7 = CAM_ONE;
	f13 = Ps2F_Div(f7, f3);
	f1 = LF(a1 + 4u);
	f0 = LF(a1 + 8u);
	SF(sp + 0xF0u, f2);
	SF(sp + 0xE0u, f2);
	f4 = Ps2F_Mul(f11, f13);
	SF(sp + 0xF4u, f1);
	SF(sp + 0xF8u, f0);
	SF(sp + 0xE4u, f1);
	SF(sp + 0xE8u, f0);
	v1 = ZmR32(m, a0 + CAM_C_PARAMS);
	f5 = f2;
	f23 = f12;
	f22 = 0u;
	fcc = Ps2F_Lt(f23, f22);
	f10 = LF(v1 + 0x7Cu); // P.FloorCamMinHeightAboveFloor
	SF(sp + 0xC4u, f9);
	SF(sp + 0xC8u, f6);
	f6 = Ps2F_Mul(f4, f10);
	f3 = LF(sp + 0xC4u);
	f4 = LF(sp + 0xC8u);
	f5 = Ps2F_Sub(f5, f6);
	f3 = Ps2F_Mul(f3, f3);
	acc = Ps2A_Adda(f8, f3);
	f3 = Ps2A_Madd(acc, f4, f4);
	f3 = Ps2F_Sqrt(f3);
	SF(sp + 0xD0u, f6);
	f6 = Ps2F_Div(f7, f3);
	f3 = Ps2F_Mul(f11, f6);
	f11 = LF(sp + 0xD4u);
	SF(sp + 0xE0u, f5);
	f4 = LF(sp + 0xC4u);
	f9 = LF(sp + 0xD8u);
	SF(sp + 0xC0u, f3);
	f5 = f3;
	f12r = Ps2F_Mul(f11, f13);
	f11 = Ps2F_Mul(f9, f13);
	f9 = Ps2F_Mul(f12r, f10);
	SF(sp + 0xD4u, f9);
	f3 = LF(sp + 0xC8u);
	f9 = Ps2F_Mul(f11, f10);
	SF(sp + 0xD8u, f9);
	f3 = Ps2F_Mul(f3, f6);
	f4 = Ps2F_Mul(f4, f6);
	SF(sp + 0xC4u, f4);
	f8 = LF(sp + 0xE4u);
	f21 = LF(sp + 0xD4u);
	f7 = LF(sp + 0xE8u);
	f20 = LF(sp + 0xD8u);
	SF(sp + 0xC8u, f3);
	f4 = LF(sp + 0xC4u);
	f8 = Ps2F_Sub(f8, f21);
	f3 = Ps2F_Sub(f7, f20);
	SF(sp + 0xE8u, f3);
	SF(sp + 0xE4u, f8);
	v1 = ZmR32(m, a0 + CAM_C_PARAMS);
	f3 = LF(sp + 0xC8u);
	f6 = LF(v1 + 0x80u); // P.FloorColStartHeightAboveCamera
	f5 = Ps2F_Mul(f5, f6);
	f2 = Ps2F_Add(f5, f2);
	SF(sp + 0xC0u, f2);
	f4 = Ps2F_Mul(f4, f6);
	f2 = Ps2F_Mul(f3, f6);
	f1 = Ps2F_Add(f4, f1);
	f0 = Ps2F_Add(f2, f0);
	SF(sp + 0xC4u, f1);
	SF(sp + 0xC8u, f0); // delay slot of the branch below
	f1 = fcc ? Ps2F_Neg(f23) : f23;
	v1 = ZmR32(m, s5 + CAM_C_PARAMS);
	f0 = LF(v1 + 0x88u); // P.FloorColNoCollisionAboveThisSpeed
	fcc = Ps2F_Lt(f1, f0);
	if (fcc)
		hit = CamSphere(c, sp + 0xC0u, sp + 0xE0u, 0x003FFFFEu, sp + 0xB0u, 0u);
	v2 = hit;
	f22 = 0u; // the source's f22 (still 0.0 unless the hit path below changes it)
	if (v2 != 0u)
	{
		f3 = LF(sp + 0xB4u);
		f0 = 0u;
		f4 = LF(sp + 0xB0u);
		fcc = Ps2F_Lt(f23, f0);
		f1 = LF(sp + 0xD0u);
		f0 = Ps2F_Add(f3, f21);
		f2 = LF(sp + 0xB8u);
		SF(sp + 0xF4u, f0);
		f1 = Ps2F_Add(f4, f1);
		f0 = Ps2F_Add(f2, f20);
		SF(sp + 0xF0u, f1);
		SF(sp + 0xF8u, f0); // delay slot
		if (fcc)
			f23 = Ps2F_Neg(f23);
		v1 = ZmR32(m, s5 + CAM_C_PARAMS);
		f3 = 0u;
		f2 = LF(v1 + 0x84u); // P.FloorColBelowThisSpeed
		f0 = LF(v1 + 0x88u);
		f1 = Ps2F_Sub(f0, f2);
		f0 = Ps2F_Sub(f23, f2); // delay slot
		if (Ps2F_Le(f1, f3))
		{
			f3 = Ps2F_Lt(f23, f2) ? 0u : CAM_ONE;
		}
		else
		{
			f3 = Ps2F_Div(f0, f1);
			f3 = CamClamp01(f3);
		}
		f20 = Ps2F_Sub(CAM_ONE, f3);
		ExtZp_Cam_11f2a0(c, sp + 0xA0u, s4);
		ExtZp_Cam_11f270(c, sp + 0xA0u, Ps2F_Sub(CAM_ONE, f20));
		ExtZp_Cam_11f2a0(c, sp + 0x90u, sp + 0xF0u);
		ExtZp_Cam_11f270(c, sp + 0x90u, f20);
		ExtZp_Cam_11f2a0(c, sp + 0x80u, sp + 0xA0u);
		ExtZp_Cam_11f230(c, sp + 0x80u, sp + 0x90u);
		ExtZp_Cam_11f210(c, sp + 0xF0u, sp + 0x80u);
		f1 = LF(s4 + 8u);
		f0 = LF(sp + 0xF8u);
		f22 = Ps2F_Sub(f0, f1);
	}
	ZmW32(m, s3, ZmR32(m, sp + 0xF0u));
	ZmW32(m, s3 + 4u, ZmR32(m, sp + 0xF4u));
	ZmW32(m, s3 + 8u, ZmR32(m, sp + 0xF8u));
	ZmW32(m, s2, f22);
	ExtZp_Pop(c, 0x100u);
}

// 0x11f670: wall correction of the eye. The eye (local, a3) is put into the world
// (0x58cc00 with the frame a1); when the speed is below P.WallNoCollisionAboveThisSpeed
// a sphere is tested from the wall ray start; a hit pulls the eye along the up
// vector by P.WallPercentOfForwardDistanceToMoveUp of the shift. Writes the corrected
// eye to t0 and the shift length to t1.
void ExtZp_Cam_11f670(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0xF0u);
	uint32_t s3 = a0, s2 = a2, s1 = t0, s0 = t1, v0, v1 = 0u, hit = 0u;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f20;
	Ps2Acc acc;
	int fcc;

	f2 = LF(a3 + 0u);
	f1 = LF(a3 + 4u);
	f0 = LF(a3 + 8u);
	SF(sp + 0xE0u, f2);
	SF(sp + 0xE4u, f1);
	SF(sp + 0xE8u, f0);
	v0 = ZmR32(m, a0 + CAM_C_PARAMS);
	f20 = f12;
	f0 = LF(v0 + 0x90u); // P.WallCollisionRayStartRelPos
	SF(sp + 0xD0u, f0);
	f0 = LF(v0 + 0x94u);
	SF(sp + 0xD4u, f0);
	f0 = LF(v0 + 0x98u);
	SF(sp + 0xD8u, f0);
	ExtZp_Cam_58cc00(c, a1, sp + 0xD0u, sp + 0xD0u, 1u);
	f0 = 0u;
	fcc = Ps2F_Lt(f20, f0);
	f1 = fcc ? Ps2F_Neg(f20) : f20;
	v1 = ZmR32(m, s3 + CAM_C_PARAMS);
	f0 = LF(v1 + 0xA4u); // P.WallNoCollisionAboveThisSpeed
	fcc = Ps2F_Lt(f1, f0);
	if (fcc)
	{
		f0 = LF(v1 + 0x9Cu); // P.WallCollisionRayRadius
		hit = CamSphere(c, sp + 0xD0u, sp + 0xE0u, 0x08000000u, sp + 0xC0u, f0);
	}
	else
		hit = 0u;
	if (hit == 0u)
	{
		f1 = LF(sp + 0xE4u);
		f0 = LF(sp + 0xE8u);
		f8 = 0u;
		f5 = LF(sp + 0xE0u);
		SF(s1 + 0u, f5);
		SF(s1 + 4u, f1);
		SF(s1 + 8u, f0);
		SF(s0, f8);
		ExtZp_Pop(c, 0xF0u);
		return;
	}
	{
		f0 = 0u;
		fcc = Ps2F_Lt(f20, f0);
		if (fcc)
			f20 = Ps2F_Neg(f20);
		v0 = ZmR32(m, s3 + CAM_C_PARAMS);
		f3 = 0u;
		f2 = LF(v0 + 0xA0u); // P.WallCollisionBelowThisSpeed
		f0 = LF(v0 + 0xA4u);
		f1 = Ps2F_Sub(f0, f2);
		f0 = Ps2F_Sub(f20, f2); // delay slot
		if (Ps2F_Le(f1, f3))
		{
			f3 = Ps2F_Lt(f20, f2) ? 0u : CAM_ONE;
		}
		else
		{
			f3 = Ps2F_Div(f0, f1);
			f3 = CamClamp01(f3);
		}
		f20 = Ps2F_Sub(CAM_ONE, f3);
		ExtZp_Cam_11f2a0(c, sp + 0x80u, sp + 0xE0u);
		ExtZp_Cam_11f270(c, sp + 0x80u, Ps2F_Sub(CAM_ONE, f20));
		ExtZp_Cam_11f2a0(c, sp + 0x70u, sp + 0xC0u);
		ExtZp_Cam_11f270(c, sp + 0x70u, f20);
		ExtZp_Cam_11f2a0(c, sp + 0x60u, sp + 0x80u);
		ExtZp_Cam_11f230(c, sp + 0x60u, sp + 0x70u);
		ExtZp_Cam_11f210(c, sp + 0xB0u, sp + 0x60u);
		f4 = LF(s2 + 4u);
		f8 = LF(sp + 0xB0u);
		f5 = LF(sp + 0xE0u);
		v0 = ZmR32(m, s3 + CAM_C_PARAMS);
		f2 = CAM_ONE;
		f3 = LF(s2 + 8u);
		f0 = LF(sp + 0xB4u);
		SF(sp + 0xA4u, f4);
		f11 = LF(sp + 0xE4u);
		f4 = Ps2F_Sub(f8, f5);
		f8 = Ps2F_Mul(f4, f4);
		f4 = Ps2F_Sub(f0, f11);
		f4 = Ps2F_Mul(f4, f4);
		f10 = LF(sp + 0xB8u);
		f9 = LF(sp + 0xE8u);
		acc = Ps2A_Adda(f8, f4);
		SF(sp + 0xA8u, f3);
		f3 = LF(sp + 0xA4u);
		f7 = LF(v0 + 0xA8u); // P.WallPercentOfForwardDistanceToMoveUp
		f6 = LF(s2 + 0u);
		f4 = Ps2F_Sub(f10, f9);
		f4 = Ps2A_Madd(acc, f4, f4);
		f8 = Ps2F_Sqrt(f4);
		f5 = LF(sp + 0xA8u);
		f3 = Ps2F_Mul(f3, f3);
		f4 = Ps2F_Mul(f6, f6);
		acc = Ps2A_Adda(f4, f3);
		f3 = Ps2A_Madd(acc, f5, f5);
		f3 = Ps2F_Sqrt(f3);
		f4 = Ps2F_Div(f2, f3);
		f7 = Ps2F_Mul(f8, f7);
		f2 = Ps2F_Mul(f6, f4);
		f2 = Ps2F_Mul(f2, f7);
		f1 = LF(sp + 0xB0u);
		SF(sp + 0xA0u, f2);
		f5 = Ps2F_Add(f1, f2);
		f2 = LF(sp + 0xA4u);
		f1 = LF(sp + 0xA8u);
		SF(sp + 0x90u, f5);
		f3 = Ps2F_Mul(f2, f4);
		f2 = Ps2F_Mul(f1, f4);
		f1 = Ps2F_Mul(f3, f7);
		SF(sp + 0xA4u, f1);
		f1 = LF(sp + 0xA4u);
		f2 = Ps2F_Mul(f2, f7);
		f1 = Ps2F_Add(f0, f1);
		SF(sp + 0xA8u, f2);
		f0 = LF(sp + 0xA8u);
		SF(sp + 0x94u, f1);
		f1 = LF(sp + 0x94u);
		f0 = Ps2F_Add(f10, f0);
		SF(sp + 0x98u, f0);
		f0 = LF(sp + 0x98u);
		SF(s1 + 0u, f5);
		SF(s1 + 4u, f1);
		SF(s1 + 8u, f0);
		SF(s0, f8);
		ExtZp_Pop(c, 0xF0u);
		return;
	}
}

// 0x11f9a0: smoothing of the collision correction. The pivot frame (a1) is copied
// and inverted, the world eye (a2) is taken back to the frame (0x58cc00), the offset
// to the local eye Rl (a3) is the correction; its share kept per frame depends on
// the speed window (P.SmoothCollision...) and, with P.CompensateForDTime, on the
// frame time. C + 0x30 gets the smoothed correction, t0 the new world eye.
void ExtZp_Cam_11f9a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x120u);
	uint32_t s5 = a0, s4 = a1, s3 = a2, s2 = a3, s1 = t0, s0, s6, v0;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f20 = f12;
	int fcc;

	ExtZp_Cam_1071e0(c, sp + 0x90u, a1);
	ExtZp_Cam_58c3a0(c, sp + 0x11Cu, sp + 0x90u, sp + 0x90u);
	f2 = LF(s3 + 0u);
	f1 = LF(s3 + 4u);
	f0 = LF(s3 + 8u);
	s0 = sp + 0x104u;
	s6 = sp + 0x108u;
	SF(sp + 0x100u, f2);
	SF(s0, f1);
	SF(s6, f0);
	ExtZp_Cam_58cc00(c, sp + 0x90u, sp + 0x100u, sp + 0x100u, 1u);
	f5 = LF(sp + 0x104u);
	f2 = LF(s2 + 4u);
	f0 = 0u;
	f6 = LF(sp + 0x100u);
	fcc = Ps2F_Lt(f20, f0);
	f3 = LF(s2 + 0u);
	f0 = Ps2F_Sub(f5, f2);
	f4 = LF(sp + 0x108u);
	f3 = Ps2F_Sub(f6, f3);
	f1 = LF(s2 + 8u);
	SF(sp + 0xF4u, f0);
	SF(sp + 0xF0u, f3);
	f0 = Ps2F_Sub(f4, f1);
	SF(sp + 0xF8u, f0);
	if (fcc)
		f20 = Ps2F_Neg(f20);
	v0 = ZmR32(m, s5 + CAM_C_PARAMS);
	f3 = 0u;
	f2 = LF(v0 + 0xB4u); // P.SmoothCollisionZeroSmoothingBelowThisSpeed
	f0 = LF(v0 + 0xB8u); // P.SmoothCollisionFullSmoothingAboveThisSpeed
	f1 = Ps2F_Sub(f0, f2);
	fcc = Ps2F_Le(f1, f3);
	f0 = Ps2F_Sub(f20, f2); // delay slot
	if (fcc)
		f3 = Ps2F_Lt(f20, f2) ? 0u : CAM_ONE;
	else
		f3 = CamClamp01(Ps2F_Div(f0, f1));
	f2 = CAM_CONST_ONE;
	f0 = LF(v0 + 0xB0u); // P.SmoothCollisionFullAdjustmentPercentPerFrame
	f1 = Ps2F_Sub(CAM_ONE, f3);
	f1 = Ps2F_Mul(f2, f1);
	f0 = Ps2F_Mul(f0, f3);
	f20 = Ps2F_Add(f1, f0);
	if (ZmR8(m, v0 + 0x5Cu) != 0u) // P.CompensateForDTime
	{
		Ps2F ms = CamMsToFloat(CamFrameMs(c));
		f1 = Ps2F_Mul(CAM_MS_TO_S, ms);
		f0 = Ps2F_Mul(f1, CAM_CONST_150);
		f20 = Ps2F_Mul(f20, f0);
		if (!Ps2F_Lt(f20, CAM_ONE))
			f20 = CAM_ONE;
	}
	ExtZp_Cam_11f2a0(c, sp + 0xE0u, sp + 0xF0u);
	ExtZp_Cam_11f1a0(c, sp + 0xE0u, s5 + CAM_C_SMOOTH);
	ExtZp_Cam_11f270(c, sp + 0xE0u, f20);
	ExtZp_Cam_11f2a0(c, sp + 0xD0u, s5 + CAM_C_SMOOTH);
	ExtZp_Cam_11f230(c, sp + 0xD0u, sp + 0xE0u);
	ExtZp_Cam_11f210(c, sp + 0xF0u, sp + 0xD0u);
	f3 = LF(sp + 0xF0u);
	f2 = LF(s2 + 0u);
	f1 = LF(sp + 0xF4u);
	f5 = LF(s2 + 4u);
	f0 = LF(sp + 0xF8u);
	f4 = LF(s2 + 8u);
	f2 = Ps2F_Add(f2, f3);
	f1 = Ps2F_Add(f5, f1);
	f0 = Ps2F_Add(f4, f0);
	SF(sp + 0x100u, f2);
	SF(s0, f1);
	SF(s6, f0);
	SF(s5 + CAM_C_SMOOTH, f3);
	ZmW32(m, s5 + CAM_C_SMOOTH + 4u, ZmR32(m, sp + 0xF4u));
	ZmW32(m, s5 + CAM_C_SMOOTH + 8u, ZmR32(m, sp + 0xF8u));
	f2 = LF(sp + 0x100u);
	f1 = LF(sp + 0x104u);
	f0 = LF(sp + 0x108u);
	SF(sp + 0x110u, f2);
	SF(sp + 0x114u, f1);
	SF(sp + 0x118u, f0);
	ExtZp_Cam_58cc00(c, s4, sp + 0x110u, sp + 0x110u, 1u);
	ZmW32(m, s1, ZmR32(m, sp + 0x110u));
	ZmW32(m, s1 + 4u, ZmR32(m, sp + 0x114u));
	ZmW32(m, s1 + 8u, ZmR32(m, sp + 0x118u));
	ExtZp_Pop(c, 0x120u);
}

// 0x11fcd0: the eye in the world, with the corrections. Rl (a3, local) goes through the
// frame a1 to the world; then the floor correction (0x11f2c0, when P.FloorDoCollision),
// the wall correction (0x11f670, when P.WallDoCollision and not held off by
// P.WallColOffTime) and the smoothing (0x11f9a0, when P.SmoothCollisionBasedAdjustments).
// Outputs: t0 = eye (world), t1 = wall shift, t2 = floor shift.
void ExtZp_Cam_11fcd0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1, uint32_t t2, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0xB0u);
	uint32_t s5 = a0, s4 = a1, s3 = a2, s2 = a3, s1 = t0, s0 = t1, s6 = t2, v1, wall;
	Ps2F f0, f1, f2, f20 = f12;

	f2 = LF(a3 + 0u);
	f1 = LF(a3 + 4u);
	f0 = LF(a3 + 8u);
	SF(sp + 0x98u, f2);
	ZmW32(m, sp + 0xACu, 0u);
	SF(sp + 0x9Cu, f1);
	ZmW32(m, sp + 0xA8u, 0u);
	SF(sp + 0xA0u, f0);
	ExtZp_Cam_58cc00(c, s4, sp + 0x98u, sp + 0x98u, 1u);
	v1 = ZmR32(m, s5 + CAM_C_PARAMS);
	if (ZmR8(m, v1 + 0x78u) != 0u) // P.FloorDoCollision
		ExtZp_Cam_11f2c0(c, s5, sp + 0x98u, s3, sp + 0x98u, sp + 0xA8u, f20);
	v1 = ZmR32(m, s5 + CAM_C_PARAMS);
	wall = ZmR8(m, v1 + 0x8Cu); // P.WallDoCollision
	if (ZmR8(m, v1 + 0xE0u) != 0u) // P.WallColOffWhileHeld
	{
		f0 = LF(v1 + 0xE4u);
		f1 = LF(s5 + CAM_C_HOLD);
		if (Ps2F_Le(f1, f0))
			wall = 0u;
	}
	if (wall != 0u)
		ExtZp_Cam_11f670(c, s5, s4, s3, sp + 0x98u, sp + 0x98u, sp + 0xACu, f20);
	v1 = ZmR32(m, s5 + CAM_C_PARAMS);
	if (ZmR8(m, v1 + 0xACu) != 0u) // P.SmoothCollisionBasedAdjustments
		ExtZp_Cam_11f9a0(c, s5, s4, sp + 0x98u, s2, sp + 0x98u, f20);
	ZmW32(m, s1, ZmR32(m, sp + 0x98u));
	ZmW32(m, s1 + 4u, ZmR32(m, sp + 0x9Cu));
	ZmW32(m, s1 + 8u, ZmR32(m, sp + 0xA0u));
	ZmW32(m, s0, ZmR32(m, sp + 0xACu));
	ZmW32(m, s6, ZmR32(m, sp + 0xA8u));
	ExtZp_Pop(c, 0xB0u);
}
