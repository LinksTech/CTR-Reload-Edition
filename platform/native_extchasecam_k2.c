// THE CHASE CAMERA PORT, PART K2: the camera's per-frame flow, timers, velocity
// mean, camera up vector, parameter block choice and small helpers (see
// include/platform/native_extchasecam_k2.h and native_extzonephys.h for the
// conventions). Compiles alone too (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extchasecam_k2.h>

#include <string.h>

// Globals of the original (absolute addresses of pointer / value words).
#define K2_G_MIN_SPEED 0x00612308u    // word holding 0.5 (the original's constant)
#define K2_G_UP_SCALE 0x006122F0u     // word holding a scale factor of the up smoothing
#define K2_G_TIMER_MGR 0x006F3B00u    // pointer: the frame timer holder ([+4] -> [+8] = frame time in ms)
#define K2_G_PLAYER_TABLE 0x006F49E0u // pointer: table of the drivers' camera records (0x14 bytes each, from +0x2C)
#define K2_G_INPUT_MGR 0x006F3650u    // pointer: the input manager
#define K2_G_PLAYER_STATE 0x006F53C8u // pointer: per player state bytes (zoom at +0x30 + player, dirty flag at +0x58)
#define K2_G_GAME_STATE 0x006F35B8u   // pointer: the game state (+0x4C = game mode value)
#define K2_G_CAM_MGR 0x006F3640u      // pointer: the camera manager

#define K2_C_0_001 0x3A83126Fu   // 0.001 (the original's constant)
#define K2_C_ONE 0x3F800000u     // 1.0
#define K2_C_HALF 0x3F000000u    // 0.5
#define K2_C_NEG_ONE 0xBF800000u // -1.0

// Table lookup: word a2 of the 32-byte row a1 of table a0; the first word of the row is the fallback; 0 when both are 0.
uint32_t ExtZp_Cam_11d8d0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t row = a0 + (a1 << 5);
	uint32_t v = ZmR32(m, row + (a2 << 2));
	if (v != 0)
		return v;
	v = ZmR32(m, row);
	if (v != 0)
		return v;
	return 0;
}

// The input device of player a1: word a1 of the manager's device table (+0xC4C).
uint32_t ExtZp_Cam_128060(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	return ZmR32(c->m, (a1 << 2) + a0 + 0xC4Cu);
}

// vec3 a0 -= vec3 a1 (the original loads a0.x and all of a1 first, stores x, then reads the next a0 lane); returns a0.
uint32_t ExtZp_Cam_11f1a0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	Ps2F f3 = ZmRF(m, a0 + 0x0);
	Ps2F f0 = ZmRF(m, a1 + 0x0);
	Ps2F f2 = ZmRF(m, a1 + 0x4);
	Ps2F f1 = ZmRF(m, a1 + 0x8);
	f0 = Ps2F_Sub(f3, f0);
	ZmWF(m, a0 + 0x0, f0);
	f0 = ZmRF(m, a0 + 0x4);
	f0 = Ps2F_Sub(f0, f2);
	ZmWF(m, a0 + 0x4, f0);
	f0 = ZmRF(m, a0 + 0x8);
	f0 = Ps2F_Sub(f0, f1);
	ZmWF(m, a0 + 0x8, f0);
	return a0;
}

// Word at +8 of a0 (a timer holder's current time in ms).
uint32_t ExtZp_Cam_11f1e0(ExtZpCtx *c, uint32_t a0)
{
	return ZmR32(c->m, a0 + 0x8);
}

// Word a1 of the list at a0 + 4.
uint32_t ExtZp_Cam_11f1f0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	return ZmR32(c->m, (a1 << 2) + a0 + 0x4);
}

// The timer manager pointer (global K2_G_TIMER_MGR).
uint32_t ExtZp_Cam_11f200(ExtZpCtx *c)
{
	return ZmR32(c->m, K2_G_TIMER_MGR);
}

// vec3 a0 = vec3 a1; returns a0.
uint32_t ExtZp_Cam_11f210(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	Ps2F f2 = ZmRF(m, a1 + 0x0);
	Ps2F f1 = ZmRF(m, a1 + 0x4);
	Ps2F f0 = ZmRF(m, a1 + 0x8);
	ZmWF(m, a0 + 0x0, f2);
	ZmWF(m, a0 + 0x4, f1);
	ZmWF(m, a0 + 0x8, f0);
	return a0;
}

// vec3 a0 += vec3 a1 (same load / store order as 11f1a0); returns a0.
uint32_t ExtZp_Cam_11f230(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	Ps2F f3 = ZmRF(m, a0 + 0x0);
	Ps2F f0 = ZmRF(m, a1 + 0x0);
	Ps2F f2 = ZmRF(m, a1 + 0x4);
	Ps2F f1 = ZmRF(m, a1 + 0x8);
	f0 = Ps2F_Add(f3, f0);
	ZmWF(m, a0 + 0x0, f0);
	f0 = ZmRF(m, a0 + 0x4);
	f0 = Ps2F_Add(f0, f2);
	ZmWF(m, a0 + 0x4, f0);
	f0 = ZmRF(m, a0 + 0x8);
	f0 = Ps2F_Add(f0, f1);
	ZmWF(m, a0 + 0x8, f0);
	return a0;
}

// vec3 a0 *= f12; returns a0.
uint32_t ExtZp_Cam_11f270(ExtZpCtx *c, uint32_t a0, Ps2F f12)
{
	ExtZpMem *m = c->m;
	Ps2F f0 = ZmRF(m, a0 + 0x0);
	f0 = Ps2F_Mul(f0, f12);
	ZmWF(m, a0 + 0x0, f0);
	f0 = ZmRF(m, a0 + 0x4);
	f0 = Ps2F_Mul(f0, f12);
	ZmWF(m, a0 + 0x4, f0);
	f0 = ZmRF(m, a0 + 0x8);
	f0 = Ps2F_Mul(f0, f12);
	ZmWF(m, a0 + 0x8, f0);
	return a0;
}

// vec4 a0 = vec4 a1 (four words); returns a0.
uint32_t ExtZp_Cam_1238a0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	Ps2F f3 = ZmRF(m, a1 + 0x0);
	Ps2F f2 = ZmRF(m, a1 + 0x4);
	Ps2F f1 = ZmRF(m, a1 + 0x8);
	Ps2F f0 = ZmRF(m, a1 + 0xC);
	ZmWF(m, a0 + 0x0, f3);
	ZmWF(m, a0 + 0x4, f2);
	ZmWF(m, a0 + 0x8, f1);
	ZmWF(m, a0 + 0xC, f0);
	return a0;
}

// As 1238a0; the original also leaves a copy of the first three words on its own stack (not modelled); returns a0.
uint32_t ExtZp_Cam_1238d0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	return ExtZp_Cam_1238a0(c, a0, a1);
}

// Returns its argument.
uint32_t ExtZp_Cam_123910(ExtZpCtx *c, uint32_t a0)
{
	(void)c;
	return a0;
}

// word [a0] = word [a1 + 0x10] (no return value).
void ExtZp_Cam_123920(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ZmW32(c->m, a0, ZmR32(c->m, a1 + 0x10));
}

// word [a0] = word [a1 + 8] (no return value).
void ExtZp_Cam_153f90(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ZmW32(c->m, a0, ZmR32(c->m, a1 + 0x8));
}

// Record a1 of the driver table a0 (0x14 bytes each): the word at +0x2C of the record.
uint32_t ExtZp_Cam_154e00(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	uint32_t v = (a1 << 2) + a1;
	v <<= 2;
	return ZmR32(c->m, v + a0 + 0x2Cu);
}

// Start of a list walk: tail jump into 1544b0 with a1 = [a1 + 4].
void ExtZp_Cam_1544a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	uint32_t a1n = ZmR32(c->m, a1 + 0x4);
	ExtZp_Cam_1544b0(c, a0, a1n, a2, a3);
}

// Iterator start: record a0 = { list, current, mask, flag }; steps to the first entry whose
// shape part does not match the mask and whose flag byte differs from a3.
void ExtZp_Cam_1544b0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x30);
	uint32_t slot2c = sp + 0x2C, slot28 = sp + 0x28;
	uint32_t v0 = ZmR32(m, a1 + 0x4);
	uint32_t cur, v1, a1n;
	ZmW32(m, a0 + 0x0, v0);
	ZmW32(m, a0 + 0x4, 0);
	ZmW32(m, a0 + 0x8, a2);
	ZmW8(m, a0 + 0xC, (uint8_t)a3);
	a1n = ZmR32(m, a0);
	ExtZp_Cam_153f90(c, slot2c, a1n);
	v0 = ZmR32(m, slot2c);
	ZmW32(m, a0 + 0x4, v0);
	a1n = ZmR32(m, a0);
	ExtZp_Cam_123920(c, slot28, a1n);
	cur = ZmR32(m, a0 + 0x4);
	v1 = ZmR32(m, slot28);
	if (cur != v1)
	{
		for (;;)
		{
			uint32_t mask = ZmR32(m, a0 + 0x8);
			uint32_t shape = ZmR32(m, cur + 0x8); // delay slot of the compare: read in any case
			if (mask == 0xFFFFFFFFu)
				break;
			v1 = ZmR32(m, shape + 0x10);
			if ((mask & v1) != 0)
				break;
			if (ZmR8(m, a0 + 0xC) == ZmR8(m, shape + 0xC))
				break;
			v0 = ZmR32(m, cur);
			ZmW32(m, a0 + 0x4, v0);
			a1n = ZmR32(m, a0);
			ExtZp_Cam_123920(c, slot28, a1n);
			cur = ZmR32(m, a0 + 0x4);
			v1 = ZmR32(m, slot28);
			if (cur == v1)
				break;
		}
	}
	ExtZp_Pop(c, 0x30);
}

// Quaternion (x y z w at a0) -> 4x4 rotation matrix at a1 (rows of four words, last row 0 0 0 1; translation 0).
void ExtZp_Cam_58d220(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	Ps2F f4 = ZmRF(m, a0 + 0x0);
	Ps2F f3 = ZmRF(m, a0 + 0x4);
	Ps2F f1 = ZmRF(m, a0 + 0x8);
	Ps2F f0 = 0x40000000u; // 2.0 (the original's constant)
	Ps2F f5 = ZmRF(m, a0 + 0xC);
	Ps2F f2, f6, f7, f8, f9, f10, f11, f12;
	f6 = Ps2F_Mul(f0, f4);
	f12 = Ps2F_Mul(f0, f3);
	f0 = Ps2F_Mul(f0, f1);
	f11 = Ps2F_Mul(f6, f5);
	f8 = Ps2F_Mul(f6, f4);
	f10 = Ps2F_Mul(f12, f5);
	f9 = Ps2F_Mul(f0, f5);
	f7 = Ps2F_Mul(f12, f4);
	f6 = Ps2F_Mul(f0, f4);
	f1 = Ps2F_Mul(f0, f1);
	f4 = Ps2F_Mul(f12, f3);
	f5 = Ps2F_Mul(f0, f3);
	f0 = Ps2F_Add(f4, f1);
	f2 = K2_C_ONE;
	f1 = Ps2F_Add(f8, f1);
	f0 = Ps2F_Sub(f2, f0);
	ZmWF(m, a1 + 0x0, f0);
	f0 = Ps2F_Add(f8, f4);
	f1 = Ps2F_Sub(f2, f1);
	f0 = Ps2F_Sub(f2, f0);
	f2 = Ps2F_Sub(f7, f9);
	ZmWF(m, a1 + 0x4, f2);
	f4 = Ps2F_Add(f6, f10);
	f3 = Ps2F_Add(f7, f9);
	ZmWF(m, a1 + 0x8, f4);
	ZmWF(m, a1 + 0x10, f3);
	ZmWF(m, a1 + 0x14, f1);
	f3 = Ps2F_Sub(f5, f11);
	f2 = Ps2F_Sub(f6, f10);
	ZmWF(m, a1 + 0x18, f3);
	f1 = Ps2F_Add(f5, f11);
	ZmWF(m, a1 + 0x20, f2);
	ZmWF(m, a1 + 0x24, f1);
	ZmWF(m, a1 + 0x28, f0);
	ZmW32(m, a1 + 0xC, 0);
	ZmW32(m, a1 + 0x1C, 0);
	ZmW32(m, a1 + 0x2C, 0);
	ZmW32(m, a1 + 0x30, 0);
	ZmW32(m, a1 + 0x34, 0);
	ZmW32(m, a1 + 0x38, 0);
	ZmW32(m, a1 + 0x3C, K2_C_ONE);
}

// Blend weight scalar: the float at t0 = [a1] * (1 - w) + [a2] * w with the transition progress w:
// a3 != 0: w = f12 / f15 (1 when f15 <= 0); a3 == 0: w = 1 - f13 / f14 (0 when f14 <= 0).
// w >= 1 gives [a2]; w <= 0 gives [a1]. a0 is unused.
void ExtZp_Cam_1239e0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                      Ps2F f12, Ps2F f13, Ps2F f14, Ps2F f15)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3;
	(void)a0;
	if (a3 != 0)
	{
		f0 = 0;
		if (Ps2F_Le(f15, f0))
			f3 = K2_C_ONE;
		else
			f3 = Ps2F_Div(f12, f15);
	}
	else
	{
		f3 = 0;
		if (!Ps2F_Le(f14, f3))
		{
			f1 = Ps2F_Div(f13, f14);
			f0 = K2_C_ONE;
			f3 = Ps2F_Sub(f0, f1);
		}
	}
	f1 = K2_C_ONE;
	if (!Ps2F_Lt(f3, f1))
	{
		ZmWF(m, t0, ZmRF(m, a2));
		return;
	}
	f0 = 0;
	if (Ps2F_Le(f3, f0))
	{
		ZmWF(m, t0, ZmRF(m, a1));
		return;
	}
	f0 = ZmRF(m, a2);
	f2 = ZmRF(m, a1);
	f1 = Ps2F_Sub(f1, f3);
	f0 = Ps2F_Mul(f0, f3);
	f1 = Ps2F_Mul(f2, f1);
	f0 = Ps2F_Add(f1, f0);
	ZmWF(m, t0, f0);
}

// The original's int -> float of a frame time in ms (unsigned trick for negative ints: halve, convert, double).
static Ps2F k2_FrameMsToFloat(uint32_t v)
{
	if ((int32_t)v >= 0)
		return Ps2F_FromInt((int32_t)v);
	{
		uint32_t h = (v >> 1) | (v & 1u);
		Ps2F f = Ps2F_FromInt((int32_t)h);
		return Ps2F_Add(f, f);
	}
}

// Maglev timer: a1 = maglev flag byte (stored at cam + 0x98), a2 = second condition. With the flag set the
// "on" timer (0x90) grows by dt and the "off" timer (0x94) is cleared, and the second timer pair
// (0x9C / 0xA0, flag 0xA4) tracks a2; with it clear the "on" timer is cleared and the "off" timer grows.
void ExtZp_Cam_121d80(ExtZpCtx *c, uint32_t cam, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t v1 = ZmR32(m, ZmR32(m, ZmR32(m, K2_G_TIMER_MGR) + 0x4) + 0x8);
	Ps2F f1 = k2_FrameMsToFloat(v1);
	Ps2F f0, f2;
	ZmW8(m, cam + 0x98, (uint8_t)a1);
	f0 = K2_C_0_001;
	v1 = ZmR8(m, cam + 0x98);
	f2 = Ps2F_Mul(f0, f1);
	if (v1 != 0)
	{
		f0 = ZmRF(m, cam + 0x90);
		f1 = 0;
		f0 = Ps2F_Add(f0, f2);
		ZmWF(m, cam + 0x90, f0);
		ZmW32(m, cam + 0x94, 0);
		if (a2 != 0 && ZmR8(m, cam + 0xA4) == 0)
		{
			ZmW8(m, cam + 0xA4, 0);
			f0 = ZmRF(m, cam + 0xA0);
			f0 = Ps2F_Add(f0, f2);
			ZmWF(m, cam + 0xA0, f0);
			ZmWF(m, cam + 0x9C, f1);
		}
		else
		{
			ZmW8(m, cam + 0xA4, 1);
			ZmW32(m, cam + 0xA0, 0);
			f0 = ZmRF(m, cam + 0x9C);
			f0 = Ps2F_Add(f0, f2);
			ZmWF(m, cam + 0x9C, f0);
		}
	}
	else
	{
		ZmW32(m, cam + 0x90, 0);
		f0 = ZmRF(m, cam + 0x94);
		f0 = Ps2F_Add(f0, f2);
		ZmWF(m, cam + 0x94, f0);
		ZmW8(m, cam + 0xA4, 0);
		f0 = ZmRF(m, cam + 0xA0);
		f0 = Ps2F_Add(f0, f2);
		ZmWF(m, cam + 0xA0, f0);
		ZmW32(m, cam + 0x9C, 0);
	}
}

// Velocity mean over the last P+0xDC frames (ring buffer at cam + 0xA8, 12 bytes each): outVec gets the
// mean velocity (z blended by 1239e0), outSpeed its length; below 0.5 m/s (or while `flag` holds the
// last value) the last vector (cam + 0x128) is used; with the kart in state 3 (backwards) the vector
// and the speed are mirrored. kart = K (velocity at +0x78); a3, t0 unused.
void ExtZp_Cam_121710(ExtZpCtx *c, uint32_t cam, uint32_t kart, uint32_t flag, uint32_t a3, uint32_t t0,
                      uint32_t outVec, uint32_t outSpeed)
{
	ExtZpMem *m = c->m;
	uint32_t v0, v1, p, cnt, e, sp;
	int hold;
	Ps2F sx, sy, sz, f0, f1, f2, f3;
	(void)a3;
	(void)t0;
	ZmWF(m, outVec + 0x0, ZmRF(m, kart + 0x78));
	ZmWF(m, outVec + 0x4, ZmRF(m, kart + 0x7C));
	ZmWF(m, outVec + 0x8, ZmRF(m, kart + 0x80));
	v1 = ZmR32(m, cam + 0x120);
	e = cam + (((v1 << 1) + v1) << 2);
	ZmWF(m, e + 0xA8, ZmRF(m, outVec + 0x0));
	ZmWF(m, e + 0xAC, ZmRF(m, outVec + 0x4));
	ZmWF(m, e + 0xB0, ZmRF(m, outVec + 0x8));
	v0 = ZmR32(m, cam + 0x120) + 1;
	ZmW32(m, cam + 0x120, v0);
	p = ZmR32(m, cam + 0xC);
	v1 = ZmR32(m, cam + 0x120);
	cnt = ZmR32(m, p + 0xDC);
	if (!((int32_t)v1 < (int32_t)cnt))
	{
		ZmW8(m, cam + 0x124, 1);
		ZmW32(m, cam + 0x120, 0);
	}
	if (ZmR8(m, cam + 0x124) != 0)
	{
		int32_t n, i = 0;
		sx = sy = sz = 0;
		n = (int32_t)ZmR32(m, ZmR32(m, cam + 0xC) + 0xDC);
		if (0 < n)
		{
			int unroll = 0;
			if (!(n < 9))
				unroll = (n >= 0 && n < 0x7FFFFFFF);
			if (unroll)
			{
				uint32_t a2p = cam;
				do
				{
					Ps2F f4, f5, f6, f7, f8;
					f6 = sx;
					i += 8;
					f5 = ZmRF(m, a2p + 0xA8);
					f4 = ZmRF(m, a2p + 0xAC);
					f3 = sy;
					f0 = ZmRF(m, a2p + 0xB4);
					f2 = ZmRF(m, a2p + 0xB0);
					f1 = sz;
					f5 = Ps2F_Add(f6, f5);
					f8 = Ps2F_Add(f3, f4);
					f6 = Ps2F_Add(f5, f0);
					f7 = Ps2F_Add(f1, f2);
					f5 = ZmRF(m, a2p + 0xB8);
					f3 = ZmRF(m, a2p + 0xC0);
					f4 = ZmRF(m, a2p + 0xBC);
					f2 = ZmRF(m, a2p + 0xC4);
					f0 = ZmRF(m, a2p + 0xCC);
					f1 = ZmRF(m, a2p + 0xC8);
					f5 = Ps2F_Add(f8, f5);
					f8 = Ps2F_Add(f5, f2);
					f3 = Ps2F_Add(f6, f3);
					f6 = Ps2F_Add(f3, f0);
					f4 = Ps2F_Add(f7, f4);
					f7 = Ps2F_Add(f4, f1);
					f5 = ZmRF(m, a2p + 0xD0);
					f3 = ZmRF(m, a2p + 0xD8);
					f4 = ZmRF(m, a2p + 0xD4);
					f2 = ZmRF(m, a2p + 0xDC);
					f1 = ZmRF(m, a2p + 0xE0);
					f0 = ZmRF(m, a2p + 0xE4);
					f5 = Ps2F_Add(f8, f5);
					f3 = Ps2F_Add(f6, f3);
					f4 = Ps2F_Add(f7, f4);
					f6 = Ps2F_Add(f5, f2);
					f8 = Ps2F_Add(f4, f1);
					f7 = Ps2F_Add(f3, f0);
					f5 = ZmRF(m, a2p + 0xE8);
					f4 = ZmRF(m, a2p + 0xEC);
					f3 = ZmRF(m, a2p + 0xF0);
					f2 = ZmRF(m, a2p + 0xF4);
					f0 = ZmRF(m, a2p + 0xFC);
					f1 = ZmRF(m, a2p + 0xF8);
					f6 = Ps2F_Add(f6, f5);
					f5 = Ps2F_Add(f8, f4);
					f4 = Ps2F_Add(f7, f3);
					f3 = Ps2F_Add(f6, f2);
					f0 = Ps2F_Add(f4, f0);
					f2 = Ps2F_Add(f5, f1);
					sx = f0;
					f1 = ZmRF(m, a2p + 0x100);
					f0 = ZmRF(m, a2p + 0x104);
					f1 = Ps2F_Add(f3, f1);
					a2p += 0x60;
					f0 = Ps2F_Add(f2, f0);
					sy = f1;
					sz = f0;
				} while (i < n - 8);
			}
			n = (int32_t)ZmR32(m, ZmR32(m, cam + 0xC) + 0xDC);
			{
				uint32_t ep = cam + (uint32_t)(((i << 1) + i) << 2);
				while (i < n)
				{
					Ps2F f4, f5;
					f5 = sx;
					i++;
					f4 = ZmRF(m, ep + 0xA8);
					f3 = ZmRF(m, ep + 0xAC);
					f2 = sy;
					f1 = ZmRF(m, ep + 0xB0);
					f0 = sz;
					f4 = Ps2F_Add(f5, f4);
					ep += 0xC;
					f2 = Ps2F_Add(f2, f3);
					f0 = Ps2F_Add(f0, f1);
					sx = f4;
					sy = f2;
					sz = f0;
				}
			}
		}
		f2 = Ps2F_Div(K2_C_ONE, Ps2F_FromInt(n));
		f0 = Ps2F_Mul(sy, f2);
		f1 = Ps2F_Mul(sx, f2);
		sy = f0;
		sx = f1;
		ZmWF(m, outVec + 0x0, f1);
		ZmWF(m, outVec + 0x4, sy);
		f0 = Ps2F_Mul(sz, f2);
		sz = f0;
		ZmWF(m, outVec + 0x8, sz);
	}
	sp = ExtZp_Push(c, 0x10);
	{
		uint32_t s68 = sp + 0x0, s6c = sp + 0x4;
		ZmWF(m, s6c, ZmRF(m, outVec + 0x8));
		ZmWF(m, s68, ZmRF(m, outVec + 0x8));
		if (ZmR8(m, ZmR32(m, cam + 0xC) + 0x5F) != 0)
			ZmW32(m, s6c, 0);
		p = ZmR32(m, cam + 0xC);
		ExtZp_Cam_1239e0(c, cam, s6c, s68, ZmR8(m, cam + 0xA4), outVec + 8, ZmRF(m, cam + 0x9C),
		                 ZmRF(m, cam + 0xA0), ZmRF(m, p + 0xD0), ZmRF(m, p + 0xCC));
	}
	ExtZp_Pop(c, 0x10);
	f1 = ZmRF(m, outVec + 0x4);
	f2 = ZmRF(m, outVec + 0x0);
	f3 = ZmRF(m, outVec + 0x8);
	f0 = ZmRF(m, K2_G_MIN_SPEED);
	f1 = Ps2F_Mul(f1, f1);
	f2 = Ps2F_Mul(f2, f2);
	{
		Ps2Acc acc = Ps2A_Adda(f2, f1);
		f1 = Ps2A_Madd(acc, f3, f3);
	}
	f1 = Ps2F_Sqrt(f1);
	ZmWF(m, outSpeed, f1);
	hold = 0;
	if (Ps2F_Le(f1, f0))
		hold = 1;
	else if (flag != 0 && ZmR8(m, cam + 0x88) != 0)
		hold = 1;
	if (hold)
	{
		ZmWF(m, outVec + 0x0, ZmRF(m, cam + 0x128));
		ZmWF(m, outVec + 0x4, ZmRF(m, cam + 0x12C));
		ZmWF(m, outVec + 0x8, ZmRF(m, cam + 0x130));
		f0 = ZmRF(m, outVec + 0x4);
		f1 = ZmRF(m, outVec + 0x0);
		f2 = ZmRF(m, outVec + 0x8);
		f0 = Ps2F_Mul(f0, f0);
		f1 = Ps2F_Mul(f1, f1);
		{
			Ps2Acc acc = Ps2A_Adda(f1, f0);
			f0 = Ps2A_Madd(acc, f2, f2);
		}
		f0 = Ps2F_Sqrt(f0);
		ZmWF(m, outSpeed, f0);
		if (flag != 0)
			ZmW8(m, cam + 0x88, 1);
	}
	if (flag == 0)
		ZmW8(m, cam + 0x88, 0);
	v0 = ExtZp_Cam_154e00(c, ZmR32(m, K2_G_PLAYER_TABLE), ZmR32(m, cam + 0x8));
	v1 = ZmR32(m, ZmR32(m, v0 + 0x4) + 0x3E0);
	if (v1 == 3)
	{
		f0 = ZmRF(m, outVec + 0x0);
		f1 = K2_C_NEG_ONE;
		ZmWF(m, outVec + 0x0, Ps2F_Neg(f0));
		ZmWF(m, outVec + 0x4, Ps2F_Neg(ZmRF(m, outVec + 0x4)));
		ZmWF(m, outVec + 0x8, Ps2F_Neg(ZmRF(m, outVec + 0x8)));
		f0 = ZmRF(m, outSpeed);
		f0 = Ps2F_Mul(f0, f1);
		ZmWF(m, outSpeed, f0);
	}
	ZmWF(m, cam + 0x128, ZmRF(m, outVec + 0x0));
	ZmWF(m, cam + 0x12C, ZmRF(m, outVec + 0x4));
	ZmWF(m, cam + 0x130, ZmRF(m, outVec + 0x8));
}

// Camera up vector: the up row (a1 + 0x20) blended towards (0, 0, 1) by P+0x70, smoothed through 123ac0
// with the timers (cam + 0x90 / 0x94, flag 0x98, P+0xD4 / 0xD8), then (if P+0x5C) blended with the
// previous up (cam + 0x3C) by P+0x54 scaled by the frame time; result in a3 and cam + 0x3C.
void ExtZp_Cam_121b60(ExtZpCtx *c, uint32_t cam, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0xE0);
	uint32_t p;
	Ps2F f0, f1, f2, f20;
	(void)a2;
	f2 = ZmRF(m, a1 + 0x24);
	f1 = ZmRF(m, a1 + 0x28);
	f0 = ZmRF(m, a1 + 0x20);
	ZmWF(m, sp + 0xA4, f2);
	ZmWF(m, sp + 0xA8, f1);
	ZmWF(m, sp + 0xD4, f2);
	ZmWF(m, sp + 0xD8, f1);
	ZmWF(m, sp + 0xD0, f0);
	ZmWF(m, sp + 0xA0, f0);
	f2 = ZmRF(m, a1 + 0x24);
	f1 = ZmRF(m, a1 + 0x28);
	f0 = ZmRF(m, a1 + 0x20);
	ZmWF(m, sp + 0x94, f2);
	ZmWF(m, sp + 0x98, f1);
	ZmWF(m, sp + 0xC4, f2);
	ZmWF(m, sp + 0xC8, f1);
	ZmWF(m, sp + 0xC0, f0);
	p = ZmR32(m, cam + 0xC);
	ZmWF(m, sp + 0x90, f0);
	f20 = ZmRF(m, p + 0x70);
	ZmW32(m, sp + 0xB8, K2_C_ONE);
	ZmW32(m, sp + 0xB0, 0);
	ZmW32(m, sp + 0xB4, 0);
	ExtZp_Cam_11f2a0(c, sp + 0x80, sp + 0xD0);
	f0 = K2_C_ONE;
	ExtZp_Cam_11f270(c, sp + 0x80, Ps2F_Sub(f0, f20));
	ExtZp_Cam_11f2a0(c, sp + 0x70, sp + 0xB0);
	ExtZp_Cam_11f270(c, sp + 0x70, f20);
	ExtZp_Cam_11f2a0(c, sp + 0x60, sp + 0x80);
	ExtZp_Cam_11f230(c, sp + 0x60, sp + 0x70);
	ExtZp_Cam_11f210(c, sp + 0xD0, sp + 0x60);
	p = ZmR32(m, cam + 0xC);
	ExtZp_Cam_123ac0(c, cam, sp + 0xD0, sp + 0xC0, ZmR8(m, cam + 0x98), a3, ZmRF(m, cam + 0x90),
	                 ZmRF(m, cam + 0x94), ZmRF(m, p + 0xD8), ZmRF(m, p + 0xD4));
	p = ZmR32(m, cam + 0xC);
	f20 = ZmRF(m, p + 0x54);
	if (ZmR8(m, p + 0x5C) != 0)
	{
		uint32_t v = ExtZp_Cam_11f200(c);
		v = ExtZp_Cam_11f1f0(c, v, 0);
		v = ExtZp_Cam_11f1e0(c, v);
		f2 = k2_FrameMsToFloat(v);
		f1 = K2_C_0_001;
		f0 = ZmRF(m, K2_G_UP_SCALE);
		f1 = Ps2F_Mul(f1, f2);
		f0 = Ps2F_Mul(f1, f0);
		f20 = Ps2F_Mul(f20, f0);
		f0 = K2_C_ONE;
		if (!Ps2F_Lt(f20, f0))
			f20 = f0;
	}
	ExtZp_Cam_11f2a0(c, sp + 0x50, a3);
	ExtZp_Cam_11f1a0(c, sp + 0x50, cam + 0x3C);
	ExtZp_Cam_11f270(c, sp + 0x50, f20);
	ExtZp_Cam_11f2a0(c, sp + 0x40, cam + 0x3C);
	ExtZp_Cam_11f230(c, sp + 0x40, sp + 0x50);
	ExtZp_Cam_11f210(c, a3, sp + 0x40);
	ZmWF(m, cam + 0x3C, ZmRF(m, a3 + 0x0));
	ZmWF(m, cam + 0x40, ZmRF(m, a3 + 0x4));
	ZmWF(m, cam + 0x44, ZmRF(m, a3 + 0x8));
	ExtZp_Pop(c, 0xE0);
}

// Parameter block choice: the zoom byte toggles on the input event 0x1D, the look-back axis 0x1E > 0.5
// sets cam + 0x134, cam + 0x135 = (game mode == 2); index = zoom + 2 * lookback + 4 * mode2; when the
// index changes (or P is 0) the new block is looked up (11d8d0) and the camera's set-parameters method
// (vtable +0x14) is called. The three indirect calls (two input device methods, the set-parameters
// method) are handed to Reload as "other" queries with the target read from the vtable as args.func.
void ExtZp_Cam_121e70(ExtZpCtx *c, uint32_t cam)
{
	ExtZpMem *m = c->m;
	uint32_t s3, s1, s0, s2, t0, a3, t1, a2, t2, v1, vt, dev;
	ExtZpQueryArgs qa;
	ExtZpQueryResult qr;
	uint32_t sp;
	dev = ExtZp_Cam_128060(c, ZmR32(m, K2_G_INPUT_MGR), ZmR32(m, cam + 0x8));
	s3 = dev;
	s1 = ZmR32(m, K2_G_PLAYER_STATE);
	t0 = 2;
	t2 = ZmR32(m, cam + 0x8);
	a3 = 4;
	t1 = ZmR8(m, cam + 0x134);
	a2 = ZmR8(m, cam + 0x135);
	v1 = ZmR32(m, K2_G_GAME_STATE);
	t2 = t2 + s1;
	s2 = ZmR8(m, t2 + 0x30);
	if (t1 == 0)
		t0 = 0;
	if (a2 == 0)
		a3 = 0;
	v1 = ZmR32(m, v1 + 0x4C);
	a2 = s2 + t0;
	v1 ^= 2u;
	v1 = (v1 < 1u) ? 1u : 0u;
	ZmW8(m, cam + 0x135, (uint8_t)v1);
	vt = ZmR32(m, dev + 0x0);
	vt = ZmR32(m, vt + 0x30);
	s0 = a3 + a2;
	memset(&qa, 0, sizeof(qa));
	qa.a[0] = s3;
	qa.a[1] = 0x1E;
	qa.a[2] = a2;
	qa.a[3] = a3;
	qa.a[4] = t0;
	qa.a[5] = t1;
	qa.a[6] = t2;
	qa.func = vt;
	qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	v1 = 1;
	if (Ps2F_Le(qr.f0, K2_C_HALF))
		v1 = 0;
	ZmW8(m, cam + 0x134, (uint8_t)v1);
	sp = ExtZp_Push(c, 0x10);
	vt = ZmR32(m, s3 + 0x0);
	vt = ZmR32(m, vt + 0x34);
	memset(&qa, 0, sizeof(qa));
	qa.a[0] = sp + 0x0;
	qa.a[1] = s3;
	qa.a[2] = 0x1D;
	qa.func = vt;
	qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	v1 = ZmR32(m, sp + 0x0);
	ExtZp_Pop(c, 0x10);
	if (!((int32_t)v1 <= 0))
	{
		uint32_t ad, old;
		v1 = ZmR32(m, cam + 0x8);
		s2 = (s2 != 0) ? 0u : 1u;
		v1 = v1 + s1;
		ad = v1 + 0x30;
		old = ZmR8(m, v1 + 0x30);
		if (s2 != old)
			ZmW8(m, s1 + 0x58, 1);
		ZmW8(m, ad, (uint8_t)s2);
	}
	a3 = ZmR8(m, cam + 0x134);
	a2 = 2;
	{
		uint32_t a1 = ZmR8(m, cam + 0x135);
		uint32_t a0 = 4;
		v1 = s2 & 0xFFu;
		if (a3 == 0)
			a2 = 0;
		v1 = v1 + a2;
		if (a1 == 0)
			a0 = 0;
		s1 = a0 + v1;
	}
	if (s0 == s1)
	{
		v1 = ZmR32(m, cam + 0xC);
		if (v1 != 0)
			return;
	}
	{
		uint32_t part = ExtZp_Cam_11b260(c, ZmR32(m, K2_G_CAM_MGR));
		uint32_t blk = ExtZp_Cam_11d8d0(c, part, 1, s1);
		vt = ZmR32(m, cam + 0x4);
		vt = ZmR32(m, vt + 0x14);
		memset(&qa, 0, sizeof(qa));
		qa.a[0] = cam;
		qa.a[1] = blk;
		qa.func = vt;
		qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	}
}

// ---- 0x122db0: the camera's per-frame flow -------------------------------------------------------------
#define K2_G_BLEND_350 0x00612350u // a scale constant of the special camera blend
#define K2_G_BLEND_358 0x00612358u // a scale constant of the special camera blend

// A call the port does not compute (boundary): handed to Reload as an "other" query for the function at `func`.
static uint32_t k2_other(ExtZpCtx *c, uint32_t func, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpQueryArgs qa;
	ExtZpQueryResult qr;
	memset(&qa, 0, sizeof(qa));
	qa.a[0] = a0;
	qa.a[1] = a1;
	qa.a[2] = a2;
	qa.func = func;
	qr = ExtZp_Query(c, EXT_ZP_Q_OTHER, &qa);
	return qr.v0;
}

// One frame of the chase camera: maglev timer -> velocity mean -> look direction / pivot -> camera up ->
// eye (local, then world) -> look point -> view matrix, with the special cases (teleport, special camera
// test, field of view, listener) handed to Reload. cam = C, vw = view matrix object (0 = none), a2 unused.
void ExtZp_Cam_122db0(ExtZpCtx *c, uint32_t cam, uint32_t vw, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x300);
	uint32_t s0, s1, s2, v0, v1, a1, a3, t1, t2, p;
	Ps2F f0, f1, f2, f20;
	int special = 0;
	(void)a2;
	s2 = ExtZp_Cam_154d30(c, ZmR32(m, K2_G_PLAYER_TABLE), ZmR32(m, cam + 0x8));
	if (s2 == 0)
		goto done;
	if ((ZmR32(m, s2 + 0x10) & 4u) == 0)
		goto done;
	s1 = ExtZp_Cam_154e00(c, ZmR32(m, K2_G_PLAYER_TABLE), ZmR32(m, cam + 0x8));
	v0 = ZmR32(m, ZmR32(m, ZmR32(m, K2_G_TIMER_MGR) + 0x4) + 0x8);
	f2 = k2_FrameMsToFloat(v0);
	f1 = K2_C_0_001;
	f0 = 0;
	f20 = Ps2F_Mul(f1, f2);
	if (!Ps2F_Le(f20, f0))
		ExtZp_Cam_121e70(c, cam);
	s0 = ZmR32(m, s2 + 0x8);
	ExtZp_Cam_58a7a0(c, sp + 0x180);
	ExtZp_Cam_160b90(c, s2, sp + 0x180);
	v1 = ZmR32(m, ZmR32(m, ZmR32(m, K2_G_TIMER_MGR) + 0x4) + 0x8);
	f2 = k2_FrameMsToFloat(v1);
	f1 = K2_C_0_001;
	f0 = ZmRF(m, cam + 0x150);
	f1 = Ps2F_Mul(f1, f2);
	f0 = Ps2F_Add(f0, f1);
	ZmWF(m, cam + 0x150, f0);
	a1 = ZmR32(m, cam + 0x14C);
	if (a1 == 3)
	{
		p = ZmR32(m, cam + 0xC);
		f0 = ZmRF(m, cam + 0x150);
		f1 = ZmRF(m, p + 0x1D0);
		if (!Ps2F_Le(f0, f1))
		{
			ZmWF(m, cam + 0x150, f1);
			ZmW32(m, cam + 0x14C, 0);
		}
	}
	else if (a1 == 1)
	{
		p = ZmR32(m, cam + 0xC);
		f0 = ZmRF(m, cam + 0x150);
		f1 = ZmRF(m, p + 0x1CC);
		if (!Ps2F_Le(f0, f1))
		{
			ZmWF(m, cam + 0x150, f1);
			ZmW32(m, cam + 0x14C, 2);
		}
	}
	v1 = ZmR8(m, s0 + 0xB1);
	if (v1 != 0)
		special = 1;
	else if (ZmR8(m, cam + 0x148) != 0)
		special = 1;
	if (!special)
	{
		if (ZmR8(m, s0 + 0x34) != 0)
			goto done;
		if (ZmR8(m, s0 + 0x48) != 0)
			ZmW32(m, cam + 0x8C, 0);
		else
		{
			f0 = ZmRF(m, cam + 0x8C);
			f0 = Ps2F_Add(f0, f20);
			ZmWF(m, cam + 0x8C, f0);
		}
		ExtZp_Cam_121d80(c, cam, ZmR8(m, s0 + 0x30), ZmR8(m, s0 + 0x40));
		a3 = 1;
		if (ZmR8(m, s0 + 0x48) == 0)
		{
			v0 = ZmR32(m, s1 + 0x4);
			f0 = 0;
			f1 = ZmRF(m, v0 + 0x480);
			if (Ps2F_Lt(f1, f0))
				f1 = Ps2F_Neg(f1);
			f0 = 0x3C23D70Au; // 0.01 (the original's constant)
			if (Ps2F_Le(f1, f0))
				a3 = 0;
		}
		ExtZp_Cam_121710(c, cam, s2, ZmR8(m, s0 + 0x31), a3, sp + 0x180, sp + 0x2A0, sp + 0x2FC);
		ExtZp_Cam_58a7a0(c, sp + 0xE0);
		v0 = ZmR32(m, s1 + 0x4);
		f0 = 0;
		f1 = ZmRF(m, v0 + 0x480);
		t2 = ZmR8(m, s0 + 0x30);
		if (Ps2F_Lt(f1, f0))
			f1 = Ps2F_Neg(f1);
		f0 = 0x3C23D70Au;
		t1 = 1;
		if (Ps2F_Le(f1, f0))
			t1 = 0;
		f2 = ZmRF(m, sp + 0x2FC);
		ExtZp_Cam_120c40(c, cam, sp + 0x180, sp + 0x2A0, ZmR8(m, s0 + 0x31), ZmR8(m, s0 + 0x48), t1, t2, sp + 0xE0, f2);
		ExtZp_Cam_58a7a0(c, sp + 0xA0);
		v0 = ExtZp_Cam_154e00(c, ZmR32(m, K2_G_PLAYER_TABLE), ZmR32(m, cam + 0x8));
		v0 = ZmR32(m, v0 + 0x4);
		v0 = ZmR32(m, v0 + 0x3E0);
		v0 ^= 0xDu;
		ExtZp_Cam_120b10(c, cam, (v0 < 1u) ? 1u : 0u, sp + 0xE0, sp + 0xA0);
		ExtZp_Cam_121b60(c, cam, sp + 0xA0, ZmR8(m, s0 + 0x30), sp + 0x290);
		v0 = ZmR32(m, s1 + 0x4);
		f0 = 0;
		f1 = ZmRF(m, v0 + 0x4C8);
		a3 = Ps2F_Le(f1, f0) ? 0u : 1u;
		f2 = ZmRF(m, sp + 0x2FC);
		ExtZp_Cam_11ed30(c, cam, s2, sp + 0xA0, a3, sp + 0x280, f2, f20); // f20: this frame's time (0.001 * ms), live in the original's f20
		f2 = ZmRF(m, sp + 0x2FC);
		ExtZp_Cam_11fcd0(c, cam, sp + 0xA0, sp + 0x290, sp + 0x280, sp + 0x270, sp + 0x2F8, sp + 0x2F4, f2);
		p = ZmR32(m, cam + 0xC);
		f1 = ZmRF(m, sp + 0x2F8);
		f2 = ZmRF(m, sp + 0x2F4);
		ZmWF(m, sp + 0x260, ZmRF(m, p + 0x28));
		ZmWF(m, sp + 0x264, ZmRF(m, p + 0x2C));
		ZmWF(m, sp + 0x268, ZmRF(m, p + 0x30));
		ExtZp_Cam_11fe60(c, cam, sp + 0xE0, sp + 0x260, sp + 0x250, f1, f2);
		v0 = k2_other(c, 0x00167490u, s2, 0, 0);
		if (v0 == 0)
			ZmW8(m, cam + 0x160, 0);
		else
		{
			Ps2F f3, f4, f5, f6, f7, f8;
			uint32_t t3 = 0xFFFFFFFFu, cnt = 0;
			v1 = t3;
			f8 = K2_C_NEG_ONE;
			t1 = 0;
			f7 = ZmRF(m, sp + 0xD4);
			p = ZmR32(m, cam + 0xC);
			f6 = ZmRF(m, sp + 0xD8);
			f5 = ZmRF(m, sp + 0xD0);
			for (;;)
			{
				int take;
				v0 = p + t1;
				f2 = ZmRF(m, v0 + 0x104);
				f1 = ZmRF(m, v0 + 0x108);
				f0 = ZmRF(m, v0 + 0x10C);
				ZmWF(m, sp + 0x210, f5);
				ZmWF(m, sp + 0x214, f7);
				ZmWF(m, sp + 0x218, f6);
				f4 = Ps2F_Sub(f5, f2);
				f3 = Ps2F_Sub(f7, f1);
				f2 = Ps2F_Sub(f6, f0);
				ZmWF(m, sp + 0x240, f4);
				ZmWF(m, sp + 0x244, f3);
				ZmWF(m, sp + 0x248, f2);
				take = (t3 == v1);
				if (!take)
				{
					Ps2Acc acc;
					f1 = Ps2F_Mul(f4, f4);
					f0 = Ps2F_Mul(f3, f3);
					acc = Ps2A_Adda(f1, f0);
					f0 = Ps2A_Madd(acc, f2, f2);
					f0 = Ps2F_Sqrt(f0);
					take = Ps2F_Lt(f0, f8) ? 1 : 0;
				}
				if (take)
				{
					Ps2Acc acc;
					t3 = cnt;
					f0 = ZmRF(m, sp + 0x240);
					f3 = ZmRF(m, sp + 0x244);
					f2 = ZmRF(m, sp + 0x248);
					f1 = Ps2F_Mul(f0, f0);
					f0 = Ps2F_Mul(f3, f3);
					acc = Ps2A_Adda(f1, f0);
					f0 = Ps2A_Madd(acc, f2, f2);
					f8 = Ps2F_Sqrt(f0);
				}
				cnt++;
				if (!((int32_t)cnt < 4))
					break;
				t1 += 0x24;
			}
			v0 = ZmR32(m, cam + 0xC);
			v1 = (((t3 << 3) + t3) << 2);
			v0 += v1;
			ZmWF(m, sp + 0x270, ZmRF(m, v0 + 0x104));
			ZmWF(m, sp + 0x274, ZmRF(m, v0 + 0x108));
			ZmWF(m, sp + 0x278, ZmRF(m, v0 + 0x10C));
			v0 = ZmR32(m, cam + 0xC);
			v0 += v1;
			ZmWF(m, sp + 0x290, ZmRF(m, v0 + 0x11C));
			ZmWF(m, sp + 0x294, ZmRF(m, v0 + 0x120));
			ZmWF(m, sp + 0x298, ZmRF(m, v0 + 0x124));
			if (ZmR8(m, cam + 0x160) != 0)
			{
				p = ZmR32(m, cam + 0xC);
				f20 = ZmRF(m, p + 0x194);
				if (ZmR8(m, p + 0x5C) != 0)
				{
					v0 = ExtZp_Cam_11f200(c);
					v0 = ExtZp_Cam_11f1f0(c, v0, 0);
					v0 = ExtZp_Cam_11f1e0(c, v0);
					f2 = k2_FrameMsToFloat(v0);
					f1 = K2_C_0_001;
					f0 = ZmRF(m, K2_G_UP_SCALE);
					f1 = Ps2F_Mul(f1, f2);
					f0 = Ps2F_Mul(f1, f0);
					f20 = Ps2F_Mul(f20, f0);
					f0 = K2_C_ONE;
					if (!Ps2F_Lt(f20, f0))
						f20 = f0;
				}
				ExtZp_Cam_11f2a0(c, sp + 0x1D0, sp + 0x250);
				ExtZp_Cam_11f1a0(c, sp + 0x1D0, cam + 0x154);
				ExtZp_Cam_11f270(c, sp + 0x1D0, f20);
				ExtZp_Cam_11f2a0(c, sp + 0x1C0, cam + 0x154);
				ExtZp_Cam_11f230(c, sp + 0x1C0, sp + 0x1D0);
				ExtZp_Cam_11f210(c, sp + 0x250, sp + 0x1C0);
			}
			ZmWF(m, cam + 0x154, ZmRF(m, sp + 0x250));
			ZmWF(m, cam + 0x158, ZmRF(m, sp + 0x254));
			ZmWF(m, cam + 0x15C, ZmRF(m, sp + 0x258));
			ZmW8(m, cam + 0x160, 1);
		}
		if (vw != 0)
		{
			Ps2F f3, f4;
			Ps2Acc acc;
			v0 = ZmR32(m, s1 + 0x4);
			f1 = ZmRF(m, v0 + 0x4C8);
			f0 = 0;
			a1 = Ps2F_Le(f1, f0) ? 0u : 1u;
			f0 = 0;
			t2 = Ps2F_Le(f1, f0) ? 0u : 1u;
			(void)k2_other(c, 0x00122190u, cam, a1, t2);
			f0 = ZmRF(m, sp + 0x250);
			f1 = K2_C_ONE;
			ZmWF(m, cam + 0x2BC, f0);
			ZmWF(m, cam + 0x2C0, ZmRF(m, sp + 0x254));
			ZmWF(m, cam + 0x2C4, ZmRF(m, sp + 0x258));
			f2 = ZmRF(m, cam + 0x2BC);
			f0 = ZmRF(m, sp + 0x270);
			f0 = Ps2F_Sub(f2, f0);
			ZmWF(m, cam + 0x2BC, f0);
			f2 = ZmRF(m, cam + 0x2C0);
			f0 = ZmRF(m, sp + 0x274);
			f0 = Ps2F_Sub(f2, f0);
			ZmWF(m, cam + 0x2C0, f0);
			f2 = ZmRF(m, cam + 0x2C4);
			f0 = ZmRF(m, sp + 0x278);
			f0 = Ps2F_Sub(f2, f0);
			ZmWF(m, cam + 0x2C4, f0);
			f2 = ZmRF(m, cam + 0x2C0);
			f3 = ZmRF(m, cam + 0x2BC);
			f4 = ZmRF(m, cam + 0x2C4);
			f0 = ZmRF(m, cam + 0x2BC);
			f2 = Ps2F_Mul(f2, f2);
			f3 = Ps2F_Mul(f3, f3);
			acc = Ps2A_Adda(f3, f2);
			f2 = Ps2A_Madd(acc, f4, f4);
			f2 = Ps2F_Sqrt(f2);
			f1 = Ps2F_Div(f1, f2);
			f0 = Ps2F_Mul(f0, f1);
			ZmWF(m, cam + 0x2BC, f0);
			f0 = ZmRF(m, cam + 0x2C0);
			f0 = Ps2F_Mul(f0, f1);
			ZmWF(m, cam + 0x2C0, f0);
			f0 = ZmRF(m, cam + 0x2C4);
			f0 = Ps2F_Mul(f0, f1);
			ZmWF(m, cam + 0x2C4, f0);
			ZmWF(m, cam + 0x2C8, ZmRF(m, sp + 0x270));
			ZmWF(m, cam + 0x2CC, ZmRF(m, sp + 0x274));
			ZmWF(m, cam + 0x2D0, ZmRF(m, sp + 0x278));
			ZmWF(m, cam + 0x2D4, ZmRF(m, sp + 0x290));
			ZmWF(m, cam + 0x2D8, ZmRF(m, sp + 0x294));
			f0 = ZmRF(m, sp + 0x298);
			ZmWF(m, cam + 0x2DC, f0);
			ExtZp_Cam_58bd70(c, vw, sp + 0x270, sp + 0x250, sp + 0x290);
		}
		(void)k2_other(c, 0x0011C260u, ZmR32(m, cam + 0x10), sp + 0x270, 0);
		goto done;
	}
	/* teleport / special state (1234fc) */
	{
		uint32_t s3 = 0, a0, s1p = sp + 0x174, s6p = sp + 0x178, s0p = sp + 0x17C;
		ExtZp_Cam_1071e0(c, cam + 0x48, sp + 0x180);
		(void)k2_other(c, 0x00122030u, cam, s2, 0);
		s2 = ZmR32(m, s1 + 0x3C);
		ExtZp_Cam_1544a0(c, sp + 0x170, ZmR32(m, K2_G_PLAYER_TABLE), 0x40, 0xFF);
		ExtZp_Cam_123920(c, sp + 0x2F0, ZmR32(m, sp + 0x170));
		a0 = ZmR32(m, sp + 0x174);
		v1 = ZmR32(m, sp + 0x2F0);
		if (a0 != v1)
		{
			for (;;)
			{
				a0 = ZmR32(m, s1p);
				s3 = ZmR32(m, a0 + 0x8);
				v1 = ZmR32(m, s3 + 0x8);
				v1 = ZmR32(m, v1 + 0x20);
				if (s2 == v1)
					break;
				v0 = ZmR32(m, a0);
				a1 = ZmR32(m, sp + 0x170);
				ZmW32(m, s1p, v0);
				ExtZp_Cam_123920(c, sp + 0x2EC, a1);
				a1 = ZmR32(m, s1p);
				v0 = ZmR32(m, sp + 0x2EC);
				if (a1 != v0)
				{
					for (;;)
					{
						v1 = ZmR32(m, s6p);
						a0 = ZmR32(m, a1 + 0x8);
						if (v1 == 0xFFFFFFFFu)
							break;
						v0 = ZmR32(m, a0 + 0x10);
						if ((v1 & v0) != 0)
							break;
						v0 = ZmR8(m, a0 + 0xC);
						v1 = ZmR8(m, s0p);
						if (v1 == v0)
							break;
						v0 = ZmR32(m, a1);
						a1 = ZmR32(m, sp + 0x170);
						ZmW32(m, s1p, v0);
						ExtZp_Cam_123920(c, sp + 0x2EC, a1);
						a1 = ZmR32(m, s1p);
						v0 = ZmR32(m, sp + 0x2EC);
						if (a1 == v0)
							break;
					}
				}
				ExtZp_Cam_123920(c, sp + 0x2F0, ZmR32(m, sp + 0x170));
				a0 = ZmR32(m, s1p);
				v1 = ZmR32(m, sp + 0x2F0);
				if (a0 == v1)
					break;
			}
		}
		if (s3 == 0)
			goto done;
		a0 = ZmR32(m, s3 + 0x8);
		if (ZmR8(m, a0 + 0x38) == 0)
			goto done;
		{
			Ps2F f3, f4, f5, f6, f7, f8, f9, f10, f11;
			f2 = ZmRF(m, a0 + 0xC);
			f1 = ZmRF(m, a0 + 0x10);
			f0 = ZmRF(m, a0 + 0x14);
			ZmWF(m, sp + 0x230, f2);
			ZmWF(m, sp + 0x234, f1);
			ZmWF(m, sp + 0x2E0, f2);
			ZmWF(m, sp + 0x2E4, f1);
			ZmWF(m, sp + 0x2E8, f0);
			v0 = ZmR32(m, s3 + 0x8);
			ZmWF(m, sp + 0x238, f0);
			f2 = ZmRF(m, v0 + 0xC);
			f1 = ZmRF(m, v0 + 0x10);
			f0 = ZmRF(m, v0 + 0x14);
			ZmWF(m, sp + 0x220, f2);
			ZmWF(m, sp + 0x224, f1);
			ZmWF(m, sp + 0x2D0, f2);
			ZmWF(m, sp + 0x2D4, f1);
			ZmWF(m, sp + 0x228, f0);
			ZmWF(m, sp + 0x2D8, f0);
			v0 = ExtZp_Cam_123910(c, sp + 0x90);
			v1 = ZmR32(m, s3 + 0x8);
			v0 = ExtZp_Cam_1238d0(c, v0, v1 + 0x28);
			ExtZp_Cam_1238a0(c, sp + 0x160, v0);
			ExtZp_Cam_58a7a0(c, sp + 0x120);
			ExtZp_Cam_58d220(c, sp + 0x160, sp + 0x120);
			f4 = ZmRF(m, sp + 0x130);
			f3 = ZmRF(m, sp + 0x134);
			f9 = ZmRF(m, sp + 0x138);
			f2 = ZmRF(m, sp + 0x2D0);
			f6 = ZmRF(m, sp + 0x140);
			f8 = ZmRF(m, sp + 0x144);
			f7 = ZmRF(m, sp + 0x148);
			f0 = ZmRF(m, sp + 0x2E0);
			f5 = 0x41000000u; // 8.0 (the original's constant)
			f3 = Ps2F_Mul(f3, f5);
			ZmWF(m, sp + 0x2C4, f3);
			f4 = Ps2F_Mul(f4, f5);
			f3 = Ps2F_Mul(f9, f5);
			f1 = ZmRF(m, K2_G_BLEND_350);
			f9 = Ps2F_Add(f2, f4);
			ZmWF(m, sp + 0x2C0, f4);
			f6 = Ps2F_Mul(f6, f1);
			f5 = Ps2F_Mul(f8, f1);
			ZmWF(m, sp + 0x2C8, f3);
			ZmWF(m, sp + 0x200, f4);
			f4 = Ps2F_Mul(f7, f1);
			f0 = Ps2F_Add(f0, f6);
			f11 = ZmRF(m, sp + 0x2C4);
			f8 = ZmRF(m, sp + 0x2D4);
			f1 = ZmRF(m, K2_G_BLEND_358);
			ZmWF(m, sp + 0x2E0, f0);
			f10 = ZmRF(m, sp + 0x2C8);
			f3 = ZmRF(m, sp + 0x2E4);
			f2 = ZmRF(m, sp + 0x2E8);
			f7 = ZmRF(m, sp + 0x2D8);
			f0 = Ps2F_Mul(f6, f1);
			ZmWF(m, sp + 0x1F0, f6);
			ZmWF(m, sp + 0x2B0, f0);
			ZmWF(m, sp + 0x1E0, f0);
			f0 = Ps2F_Add(f9, f0);
			ZmWF(m, sp + 0x2D0, f0);
			f0 = Ps2F_Add(f3, f5);
			ZmWF(m, sp + 0x2E4, f0);
			f0 = Ps2F_Add(f2, f4);
			ZmWF(m, sp + 0x2E8, f0);
			f0 = Ps2F_Mul(f5, f1);
			f1 = Ps2F_Mul(f4, f1);
			f8 = Ps2F_Add(f8, f11);
			ZmWF(m, sp + 0x1E4, f0);
			ZmWF(m, sp + 0x2B4, f0);
			f0 = Ps2F_Add(f8, f0);
			ZmWF(m, sp + 0x2D4, f0);
			f6 = Ps2F_Add(f7, f10);
			f0 = Ps2F_Add(f6, f1);
			ZmWF(m, sp + 0x204, f11);
			ZmWF(m, sp + 0x208, f10);
			ZmWF(m, sp + 0x1F4, f5);
			ZmWF(m, sp + 0x1F8, f4);
			ZmWF(m, sp + 0x1E8, f1);
			ZmWF(m, sp + 0x2B8, f1);
			ZmWF(m, sp + 0x2D8, f0);
			ExtZp_Cam_58bd70(c, vw, sp + 0x2D0, sp + 0x2E0, cam + 0x3C);
		}
	}
done:
	ExtZp_Pop(c, 0x300);
}
