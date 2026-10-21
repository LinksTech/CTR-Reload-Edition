// THE CHASE CAMERA PORT, PART K1: the per-frame update frame of the chase camera (vtable +0xC), the kart
// world matrix and the view matrix, with their small helpers (see include/platform/native_extzonephys.h
// for the conventions). Compiles alone (offline tests).
//
// Layout facts used here (offsets from the camera object C, the kart K, the parameter block P):
//   C+0x08 s32 driver index, C+0x0C ptr parameter block P, C+0x14 u8 teleport flag, C+0x78 vec3 kart position
//   of the previous frame, C+0x120 s32 / C+0x124 u8 velocity ring buffer index / full flag, C+0x128 vec3 last
//   velocity direction, C+0x144 float horizontal field of view, C+0x168 u8 / C+0x16C float jump box zone
//   state / time, C+0x2E0 / C+0x2E4 floats distance and height of the special (spectator) camera.
//   K+0x10 flags (bit 2 = chase camera follows normally, bit 3 with K+0x14 == 0xE = special camera).
// Globals: 0x006F49E0 = the table of drivers (20 bytes per entry, kart at +0x20, driver record at +0x28),
//   0x006F3B00 = the global clock record ([+4] -> time block, time block +8 = frame time in milliseconds),
//   0x006F3640 = the camera manager.
// The scratch stack words of the original that are written but never read again are not reproduced
// (they lie in the stack range, which is never compared).

#include <platform/native_extzonephys.h>
#include <platform/native_extchasecam_k1.h>

#define CAM_G_DRIVERS 0x006F49E0u // table of drivers (gp-relative word)
#define CAM_G_CLOCK 0x006F3B00u   // global clock record
#define CAM_G_MANAGER 0x006F3640u // camera manager

#define CAM_F_ONE 0x3F800000u     // 1.0 (the original's constant)
#define CAM_F_TWO 0x40000000u     // 2.0 (the original's constant)
#define CAM_F_HALF 0x3F000000u    // 0.5 (the original's constant)
#define CAM_F_MS 0x3A83126Fu      // 0.001 (the original's constant: milliseconds to seconds)
#define CAM_F_TEN 0x41200000u     // 10.0 (the original's constant)
#define CAM_F_FIFTY 0x42480000u   // 50.0 (the original's constant: teleport speed limit, m/s)
#define CAM_F_THREE 0x40400000u   // 3.0 (the original's constant)
#define CAM_F_THREEQ 0x3F400000u  // 0.75 (the original's constant)
#define CAM_F_TENTH 0x3DCCCCCDu   // 0.1 (the original's constant)
#define CAM_F_NINE99 0x4479C000u  // 999.0 (the original's constant)
#define CAM_F_MINNORM 0x00800000u // 1.17549435e-38 (smallest normal number)

// ------------------------------------------------------------------ small helpers

// 0x11b260: the sub-block at +0xF8 of the camera manager.
uint32_t ExtZp_Cam_11b260(ExtZpCtx *c, uint32_t mgr)
{
	(void)c;
	return mgr + 0xF8u;
}

// 0x11e790: the default smoothing values of a parameter block (every rate 1.0, no delays).
void ExtZp_Cam_11e790(ExtZpCtx *c, uint32_t p)
{
	ExtZpMem *m = c->m;
	ZmW32(m, p + 0x3C, CAM_F_ONE);
	ZmW32(m, p + 0x4C, CAM_F_ONE);
	ZmW32(m, p + 0x50, CAM_F_ONE);
	ZmW32(m, p + 0x54, CAM_F_ONE);
	ZmW32(m, p + 0x74, CAM_F_ONE);
	ZmW8(m, p + 0xAC, 0);
	ZmW32(m, p + 0xB0, CAM_F_ONE);
	ZmW32(m, p + 0xB4, 0x42C80000u); // 100.0 (the original's constant)
	ZmW32(m, p + 0xB8, 0x42CA0000u); // 101.0 (the original's constant)
	ZmW32(m, p + 0xC4, 0);
	ZmW32(m, p + 0xC8, 0);
	ZmW32(m, p + 0xCC, 0);
	ZmW32(m, p + 0xD0, 0);
	ZmW32(m, p + 0xD4, 0);
	ZmW32(m, p + 0xD8, 0);
	ZmW8(m, p + 0x5C, 0);
	ZmW8(m, p + 0xE0, 0);
	ZmW32(m, p + 0xE4, 0);
	ZmW32(m, p + 0x100, CAM_F_ONE);
}

// 0x11f2a0: copies a vec3 (three words); returns the destination.
uint32_t ExtZp_Cam_11f2a0(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	ExtZpMem *m = c->m;
	Ps2F x = ZmRF(m, src), y = ZmRF(m, src + 4), z = ZmRF(m, src + 8);
	ZmWF(m, dst, x);
	ZmWF(m, dst + 4, y);
	ZmWF(m, dst + 8, z);
	return dst;
}

// 0x122d40: copies 36 bytes (nine words) of a fixed camera slot; returns the destination.
uint32_t ExtZp_Cam_122d40(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	ExtZpMem *m = c->m;
	Ps2F w[9];
	for (uint32_t i = 0; i < 9; i++)
		w[i] = ZmRF(m, src + 4u * i);
	for (uint32_t i = 0; i < 9; i++)
		ZmWF(m, dst + 4u * i, w[i]);
	return dst;
}

// 0x122d90: the head word of a parameter block is copied, the class pointer (0x6CBC10) is set; returns dst.
uint32_t ExtZp_Cam_122d90(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	ExtZpMem *m = c->m;
	uint32_t head = ZmR32(m, src);
	ZmW32(m, dst, head);
	ZmW32(m, dst + 4, 0x006CBC10u); // class pointer (the original's constant)
	return dst;
}

// 0x154d30: the kart of entry n of the driver table (entries of 20 bytes, kart at +0x20).
uint32_t ExtZp_Cam_154d30(ExtZpCtx *c, uint32_t table, uint32_t n)
{
	uint32_t o = (((n << 2) + n) << 2) + table;
	return ZmR32(c->m, o + 0x20u);
}

// 0x154de0: the driver record of entry n of the driver table (+0x28).
uint32_t ExtZp_Cam_154de0(ExtZpCtx *c, uint32_t table, uint32_t n)
{
	uint32_t o = (((n << 2) + n) << 2) + table;
	return ZmR32(c->m, o + 0x28u);
}

// 0x171ce0: the model matrix address inside a model object: [a] + 0x64.
uint32_t ExtZp_Cam_171ce0(ExtZpCtx *c, uint32_t a)
{
	return ZmR32(c->m, a) + 0x64u;
}

// 0x160ca0: the kart's model matrix address: tail jump to 0x171ce0 with a = [k + 0x3C].
uint32_t ExtZp_Cam_160ca0(ExtZpCtx *c, uint32_t k)
{
	uint32_t a = ZmR32(c->m, k + 0x3Cu);
	return ExtZp_Cam_171ce0(c, a);
}

// 0x1071e0: copies a 4x4 matrix (16 words). Both addresses 16-byte aligned: four 128-bit moves, else word by word.
void ExtZp_Cam_1071e0(ExtZpCtx *c, uint32_t dst, uint32_t src)
{
	ExtZpMem *m = c->m;
	if (((dst | src) & 0xFu) == 0)
	{
		Ps2Vec4 r0 = ZmRV(m, src), r1 = ZmRV(m, src + 0x10), r2 = ZmRV(m, src + 0x20), r3 = ZmRV(m, src + 0x30);
		ZmWV(m, dst, r0);
		ZmWV(m, dst + 0x10, r1);
		ZmWV(m, dst + 0x20, r2);
		ZmWV(m, dst + 0x30, r3);
	}
	else
	{
		for (uint32_t i = 0; i < 16; i++)
		{
			Ps2F v = ZmRF(m, src + 4u * i);
			ZmWF(m, dst + 4u * i, v);
		}
	}
}

// 0x58a7a0: identity matrix.
void ExtZp_Cam_58a7a0(ExtZpCtx *c, uint32_t mat)
{
	ExtZpMem *m = c->m;
	for (uint32_t i = 0; i < 16; i++)
		ZmW32(m, mat + 4u * i, (i % 5u) == 0 ? CAM_F_ONE : 0u);
}

// 0x58b090: the translation row (+0x30) of matrix mat = the vec3 at src.
void ExtZp_Cam_58b090(ExtZpCtx *c, uint32_t mat, uint32_t src)
{
	ExtZpMem *m = c->m;
	ZmWF(m, mat + 0x30, ZmRF(m, src));
	ZmWF(m, mat + 0x34, ZmRF(m, src + 4));
	ZmWF(m, mat + 0x38, ZmRF(m, src + 8));
}

// 0x58d690: out = a x b, component by component with the accumulator pair (mula, msub).
void ExtZp_Cam_58d690(ExtZpCtx *c, uint32_t out, uint32_t a, uint32_t b)
{
	ExtZpMem *m = c->m;
	Ps2F bz = ZmRF(m, b + 8);
	Ps2F ay = ZmRF(m, a + 4);
	Ps2F by = ZmRF(m, b + 4);
	Ps2F az = ZmRF(m, a + 8);
	Ps2F ax = ZmRF(m, a);
	Ps2F bx = ZmRF(m, b);
	Ps2Acc acc = Ps2A_Mula(ay, bz);
	ZmWF(m, out, Ps2A_Msub(acc, az, by));
	acc = Ps2A_Mula(az, bx);
	ZmWF(m, out + 4, Ps2A_Msub(acc, ax, bz));
	acc = Ps2A_Mula(ax, by);
	ZmWF(m, out + 8, Ps2A_Msub(acc, ay, bx));
}

// 0x58b9d0: rotation part (3x3 in the first three rows of the 4x4) of matrix mat from the quaternion
// (x, y, z, w) at q; the quaternion is normalised first (times 1 / sqrt(x*x + y*y + z*z + w*w)).
void ExtZp_Cam_58b9d0(ExtZpCtx *c, uint32_t mat, uint32_t q)
{
	ExtZpMem *m = c->m;
	Ps2F f8 = ZmRF(m, q + 8);
	Ps2F f12 = ZmRF(m, q);
	Ps2F f11 = ZmRF(m, q + 4);
	Ps2F f6 = ZmRF(m, q + 0xC);
	Ps2F one = CAM_F_ONE, two = CAM_F_TWO;
	Ps2F f0, f1, f2, f3, f4, f7, f9, f10;
	Ps2Acc acc;
	f0 = Ps2F_Mul(f8, f8);
	acc = Ps2A_Mula(f12, f12);
	f1 = Ps2A_Madd(acc, f11, f11);
	acc = Ps2A_Adda(f0, f1);
	f0 = Ps2A_Madd(acc, f6, f6);
	f0 = Ps2F_Sqrt(f0);
	f0 = Ps2F_Div(one, f0);
	f11 = Ps2F_Mul(f11, f0);
	f8 = Ps2F_Mul(f8, f0);
	f6 = Ps2F_Mul(f6, f0);
	f12 = Ps2F_Mul(f12, f0);
	f10 = Ps2F_Mul(f8, f8);
	f3 = Ps2F_Mul(f11, f11);
	f0 = Ps2F_Add(f3, f10);
	f4 = Ps2F_Mul(f8, f6);
	f0 = Ps2F_Mul(two, f0);
	f0 = Ps2F_Sub(one, f0);
	ZmWF(m, mat, f0);
	f0 = Ps2F_Mul(f12, f11);
	f1 = Ps2F_Sub(f0, f4);
	f0 = Ps2F_Add(f0, f4);
	f4 = Ps2F_Mul(two, f0);
	f1 = Ps2F_Mul(two, f1);
	f0 = Ps2F_Mul(f12, f12);
	ZmWF(m, mat + 4, f1);
	f1 = Ps2F_Add(f10, f0);
	f0 = Ps2F_Add(f3, f0);
	f1 = Ps2F_Mul(two, f1);
	f0 = Ps2F_Mul(two, f0);
	f9 = Ps2F_Mul(f11, f6);
	f7 = Ps2F_Mul(f12, f6);
	f6 = Ps2F_Mul(f11, f8);
	f8 = Ps2F_Mul(f8, f12);
	f3 = Ps2F_Sub(one, f1);
	f0 = Ps2F_Sub(one, f0);
	f2 = Ps2F_Add(f8, f9);
	f2 = Ps2F_Mul(two, f2);
	ZmWF(m, mat + 8, f2);
	f1 = Ps2F_Sub(f8, f9);
	f2 = Ps2F_Mul(two, f1);
	ZmWF(m, mat + 0x10, f4);
	ZmWF(m, mat + 0x14, f3);
	f3 = Ps2F_Sub(f6, f7);
	f1 = Ps2F_Add(f6, f7);
	f3 = Ps2F_Mul(two, f3);
	ZmWF(m, mat + 0x18, f3);
	f1 = Ps2F_Mul(two, f1);
	ZmWF(m, mat + 0x20, f2);
	ZmWF(m, mat + 0x24, f1);
	ZmWF(m, mat + 0x28, f0);
}

// 0x58bd70 helper: squared length of the vec3 (x, y, z): y*y, x*x, adda x*x + y*y, madd z*z.
static Ps2F Cam_Len2(Ps2F x, Ps2F y, Ps2F z)
{
	Ps2F yy = Ps2F_Mul(y, y);
	Ps2F xx = Ps2F_Mul(x, x);
	Ps2Acc acc = Ps2A_Adda(xx, yy);
	return Ps2A_Madd(acc, z, z);
}

// 0x58bd70 helper: the vec3 at v times 1 / sqrt(len2) when its squared length is above the smallest normal number.
static void Cam_Normalize(ExtZpMem *m, uint32_t v)
{
	Ps2F x = ZmRF(m, v), y = ZmRF(m, v + 4), z = ZmRF(m, v + 8);
	Ps2F len2 = Cam_Len2(x, y, z);
	if (Ps2F_Lt(CAM_F_MINNORM, len2))
	{
		Ps2F r = Ps2F_Div(CAM_F_ONE, Ps2F_Sqrt(len2));
		ZmWF(m, v, Ps2F_Mul(x, r));
		ZmWF(m, v + 4, Ps2F_Mul(y, r));
		ZmWF(m, v + 8, Ps2F_Mul(z, r));
	}
}

// 0x58bd70: view matrix vw (row vector convention, translation in row 3) from the eye, the look point and
// the up vector. dir = unit(eye - look), upn = unit(up), side = unit(upn x dir), top = dir x side. The columns
// of the 3x3 part are side, top, dir; the translation is -(eye . column); element 15 is 1 - (0 . eye).
void ExtZp_Cam_58bd70(ExtZpCtx *c, uint32_t vw, uint32_t eye, uint32_t look, uint32_t up)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x90);
	uint32_t col;
	// locals of the original's frame: dir 0x80, up 0x70, side 0x60, top 0x50
	ZmWF(m, sp + 0x80, Ps2F_Sub(ZmRF(m, eye), ZmRF(m, look)));
	ZmWF(m, sp + 0x84, Ps2F_Sub(ZmRF(m, eye + 4), ZmRF(m, look + 4)));
	ZmWF(m, sp + 0x88, Ps2F_Sub(ZmRF(m, eye + 8), ZmRF(m, look + 8)));
	Cam_Normalize(m, sp + 0x80);
	ZmWF(m, sp + 0x70, ZmRF(m, up));
	ZmWF(m, sp + 0x74, ZmRF(m, up + 4));
	ZmWF(m, sp + 0x78, ZmRF(m, up + 8));
	Cam_Normalize(m, sp + 0x70);
	ExtZp_Cam_58d690(c, sp + 0x60, sp + 0x70, sp + 0x80);
	Cam_Normalize(m, sp + 0x60);
	ExtZp_Cam_58d690(c, sp + 0x50, sp + 0x80, sp + 0x60);
	ZmWF(m, vw, ZmRF(m, sp + 0x60));
	ZmWF(m, vw + 4, ZmRF(m, sp + 0x50));
	ZmWF(m, vw + 8, ZmRF(m, sp + 0x80));
	ZmW32(m, vw + 0xC, 0);
	ZmWF(m, vw + 0x10, ZmRF(m, sp + 0x64));
	ZmWF(m, vw + 0x14, ZmRF(m, sp + 0x54));
	ZmWF(m, vw + 0x18, ZmRF(m, sp + 0x84));
	ZmW32(m, vw + 0x1C, 0);
	ZmWF(m, vw + 0x20, ZmRF(m, sp + 0x68));
	ZmWF(m, vw + 0x24, ZmRF(m, sp + 0x58));
	ZmWF(m, vw + 0x28, ZmRF(m, sp + 0x88));
	ZmW32(m, vw + 0x2C, 0);
	for (col = 0; col < 4; col++)
	{
		Ps2F ex = ZmRF(m, eye), ey = ZmRF(m, eye + 4), ez = ZmRF(m, eye + 8);
		Ps2F a = ZmRF(m, vw + 4 * col);
		Ps2F b = ZmRF(m, vw + 0x10 + 4 * col);
		Ps2F d = ZmRF(m, vw + 0x20 + 4 * col);
		Ps2F p0, p1, r;
		Ps2Acc acc;
		if (col == 0)
		{
			// the first column multiplies the middle row first (m[0x10] * ey, then m[0] * ex)
			p1 = Ps2F_Mul(b, ey);
			p0 = Ps2F_Mul(a, ex);
			acc = Ps2A_Adda(p0, p1);
		}
		else
		{
			p0 = Ps2F_Mul(a, ex);
			p1 = Ps2F_Mul(b, ey);
			acc = Ps2A_Adda(p0, p1);
		}
		r = Ps2A_Madd(acc, d, ez);
		if (col < 3)
			ZmWF(m, vw + 0x30 + 4 * col, Ps2F_Neg(r));
		else
			ZmWF(m, vw + 0x3C, Ps2F_Sub(CAM_F_ONE, r));
	}
	ExtZp_Pop(c, 0x90);
}

// 0x160b90: the kart's world matrix at mat (the caller set it to identity). Three sources, in this order:
// the instance record K+0xF38 (position +0x1C, quaternion +0x28), else the model matrix when K+0xF2C != 0, else
// the model matrix with the translation = model offset (row 2) * K+0x8E4 + K+0x5C and the rotation from the
// quaternion at K+0x68.
void ExtZp_Cam_160b90(ExtZpCtx *c, uint32_t k, uint32_t mat)
{
	ExtZpMem *m = c->m;
	uint32_t inst = ZmR32(m, k + 0xF38);
	if (inst != 0)
	{
		ExtZp_Cam_58b090(c, mat, inst + 0x1C);
		inst = ZmR32(m, k + 0xF38);
		ExtZp_Cam_58b9d0(c, mat, inst + 0x28);
		return;
	}
	if (ZmR32(m, k + 0xF2C) != 0)
	{
		uint32_t mm = ExtZp_Cam_160ca0(c, k);
		ExtZp_Cam_1071e0(c, mat, mm);
		return;
	}
	{
		uint32_t mm = ExtZp_Cam_160ca0(c, k);
		uint32_t sp, mm2;
		Ps2F f0, f1, f2, f3, f4, f5, f6;
		ExtZp_Cam_1071e0(c, mat, mm);
		mm2 = ExtZp_Cam_160ca0(c, k);
		sp = ExtZp_Push(c, 0x50);
		f3 = ZmRF(m, mm2 + 0x20);
		f4 = ZmRF(m, k + 0x8E4);
		f6 = ZmRF(m, mm2 + 0x24);
		f5 = ZmRF(m, mm2 + 0x28);
		f2 = ZmRF(m, k + 0x5C);
		f1 = ZmRF(m, k + 0x60);
		f3 = Ps2F_Mul(f3, f4);
		f2 = Ps2F_Add(f3, f2);
		ZmWF(m, sp + 0x40, f2);
		f3 = Ps2F_Mul(f6, f4);
		f1 = Ps2F_Add(f3, f1);
		f0 = ZmRF(m, k + 0x64);
		f2 = Ps2F_Mul(f5, f4);
		f0 = Ps2F_Add(f2, f0);
		ZmWF(m, sp + 0x44, f1);
		ZmWF(m, sp + 0x48, f0);
		ExtZp_Cam_58b090(c, mat, sp + 0x40);
		ExtZp_Cam_58b9d0(c, mat, k + 0x68);
		ExtZp_Pop(c, 0x50);
	}
}

// ------------------------------------------------------------------ the update frame

// The frame time in seconds-source: the signed millisecond count as a float (an unsigned-style conversion for
// negative values: halve with the low bit kept, convert, double).
static Ps2F Cam_MsToFloat(uint32_t ms)
{
	if ((int32_t)ms >= 0)
		return Ps2F_FromInt((int32_t)ms);
	{
		uint32_t h = (ms >> 1) | (ms & 1u);
		Ps2F f = Ps2F_FromInt((int32_t)h);
		return Ps2F_Add(f, f);
	}
}

// One glide step of a value v towards a target t (rate r per second, dt seconds in d): up with +, down with -.
// Returns the new value; the caller stores it. Up: v + d*r (v < t) clamped to t; down: v - d*r (v > t) clamped to t.
static Ps2F Cam_Glide(Ps2F v, Ps2F t, Ps2F d, Ps2F rate)
{
	Ps2F f0;
	if (Ps2F_Lt(v, t))
	{
		f0 = Ps2F_Mul(d, rate);
		f0 = Ps2F_Add(v, f0);
		return Ps2F_Lt(f0, t) ? f0 : t;
	}
	if (Ps2F_Le(v, t))
		return v; // equal: untouched (the original does not even store)
	f0 = Ps2F_Mul(d, rate);
	f0 = Ps2F_Sub(v, f0);
	return Ps2F_Le(f0, t) ? t : f0;
}

// Field copies of the parameter block P into the temporary block T (the original copies these words / bytes
// one by one, the rest of the block only through the helpers below).
static const uint16_t s_copyWords[] = {
    0x20, 0x24, 0x34, 0x38, 0x3C, 0x4C, 0x50, 0x54, 0x58, 0x60, 0x70, 0x74, 0x7C, 0x80, 0x84, 0x88, 0x9C, 0xA0, 0xA4, 0xA8,
    0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE4, 0xF4, 0xF8, 0xFC, 0x100, 0x194, 0x19C,
    0x1A4, 0x1A8, 0x1AC, 0x1B0, 0x1B4, 0x1B8, 0x1BC, 0x1C0, 0x1C4, 0x1C8, 0x1CC, 0x1D0, 0x1E0, 0x1E4, 0x1E8, 0x1EC, 0x1FC, 0x200};
static const uint16_t s_copyBytes[] = {0x5C, 0x5D, 0x5E, 0x5F, 0x78, 0x8C, 0xAC, 0xE0, 0x198, 0x1A0, 0x1A1};
static const uint16_t s_copyVec3[] = {0x8, 0x14, 0x28, 0x40, 0x64, 0x90, 0xE8, 0x1D4, 0x1F0, 0x204};
static const uint16_t s_copySlots[] = {0x104, 0x128, 0x14C, 0x170};

// 0x122430: the chase camera's update per frame (vtable +0xC). cam = camera object C, vw = the output view
// matrix, a2 = passed to the camera flow 0x122db0. K = the kart of the camera's driver (driver table).
//  - K+0x10 bit 2: the normal chase camera. The kart's world matrix gives this frame's position; when the
//    horizontal move since C+0x78 divided by the frame time is above 50 m/s (or the flag C+0x14 was set) the
//    camera is reset (teleport): the ring buffer and the jump zone state are cleared, the field of view is set
//    to the base value and the camera flow runs once with a temporary copy of the parameter block with the
//    default smoothing values; otherwise the camera flow runs directly.
//  - K+0x10 bit 3 and K+0x14 == 0xE: the special camera, a view matrix from the kart's position and a
//    distance / height that glide towards targets scaled by the driver record's value at +0x8C0.
void ExtZp_Cam_122430(ExtZpCtx *c, uint32_t cam, uint32_t vw, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x420);
	uint32_t table = ZmR32(m, CAM_G_DRIVERS);
	uint32_t k = ExtZp_Cam_154d30(c, table, ZmR32(m, cam + 8));

	if (k != 0 && (ZmR32(m, k + 0x10) & 4u) != 0)
	{
		Ps2F f22, f21, f20, dx, dy, dz, dtf, dt, f1, f2, f3, f4;
		Ps2Acc acc;
		uint32_t clk, gt, dtms, i;
		ZmW32(m, cam + 0x2E0, CAM_F_THREE);
		ZmW32(m, cam + 0x2E4, CAM_F_THREEQ);
		f22 = ZmRF(m, cam + 0x7C);
		f21 = ZmRF(m, cam + 0x80);
		f20 = ZmRF(m, cam + 0x78);
		ExtZp_Cam_58a7a0(c, sp + 0x2E0);
		ExtZp_Cam_160b90(c, k, sp + 0x2E0);
		// kart position of this frame (row 3 of the world matrix) minus the remembered one
		f3 = ZmRF(m, sp + 0x314);
		f1 = ZmRF(m, sp + 0x310);
		f2 = ZmRF(m, sp + 0x318);
		clk = ZmR32(m, CAM_G_CLOCK);
		gt = ZmR32(m, clk + 4);
		dtms = ZmR32(m, gt + 8);
		dy = Ps2F_Sub(f3, f22);
		dx = Ps2F_Sub(f1, f20);
		dz = Ps2F_Sub(f2, f21);
		(void)dz;
		dtf = Cam_MsToFloat(dtms);
		dt = Ps2F_Mul(CAM_F_MS, dtf);
		if (Ps2F_Eq(0u, dt))
			dt = Ps2F_Div(CAM_F_ONE, CAM_F_TEN); // no time passed: one tenth of a second
		// the vertical part is dropped: the original stores 0 over it before reading it back
		f1 = Ps2F_Mul(dy, dy);
		f2 = Ps2F_Mul(dx, dx);
		acc = Ps2A_Adda(f2, f1);
		f1 = Ps2A_Madd(acc, 0u, 0u);
		f1 = Ps2F_Sqrt(f1);
		f1 = Ps2F_Div(f1, dt);
		f4 = CAM_F_FIFTY;
		if (!Ps2F_Le(f1, f4))
			ZmW8(m, cam + 0x14, 1);
		if (ZmR8(m, cam + 0x14) == 0)
		{
			ExtZp_Cam_122db0(c, cam, vw, a2);
		}
		else
		{
			// teleport frame: reset the camera state from the fresh world matrix
			uint32_t t = sp + 0x90, saved, p, cm, cf;
			Ps2F f0;
			ZmW8(m, cam + 0x14, 0);
			ExtZp_Cam_58a7a0(c, sp + 0x2A0);
			ExtZp_Cam_160b90(c, k, sp + 0x2A0);
			f3 = ZmRF(m, sp + 0x2B0);
			f0 = ZmRF(m, sp + 0x2BC);
			f2 = ZmRF(m, sp + 0x2B4);
			f1 = ZmRF(m, sp + 0x2B8);
			ZmWF(m, cam + 0x128, f3);
			ZmWF(m, cam + 0x12C, f2);
			ZmWF(m, cam + 0x130, f1);
			ZmW32(m, cam + 0x120, 0);
			ZmW8(m, cam + 0x124, 0);
			ZmW8(m, cam + 0x168, 0);
			ZmW32(m, cam + 0x16C, CAM_F_NINE99);
			cm = ZmR32(m, CAM_G_MANAGER);
			cm = ExtZp_Cam_11b260(c, cm);
			cf = ZmR32(m, cm + 0x140);
			ZmWF(m, cam + 0x144, ZmRF(m, cf + 8));
			p = ZmR32(m, cam + 0xC);
			ExtZp_Cam_122d90(c, t, p);
			ZmW32(m, t + 4, 0x006CBC90u); // class pointer of the temporary block (the original's constant)
			for (i = 0; i < sizeof(s_copyVec3) / sizeof(s_copyVec3[0]); i++)
				ExtZp_Cam_11f2a0(c, t + s_copyVec3[i], p + s_copyVec3[i]);
			for (i = 0; i < sizeof(s_copyWords) / sizeof(s_copyWords[0]); i++)
				ZmW32(m, t + s_copyWords[i], ZmR32(m, p + s_copyWords[i]));
			for (i = 0; i < sizeof(s_copyBytes) / sizeof(s_copyBytes[0]); i++)
				ZmW8(m, t + s_copyBytes[i], ZmR8(m, p + s_copyBytes[i]));
			for (i = 0; i < sizeof(s_copySlots) / sizeof(s_copySlots[0]); i++)
				ExtZp_Cam_122d40(c, t + s_copySlots[i], p + s_copySlots[i]);
			ExtZp_Cam_11e790(c, t); // the default smoothing values over the copy
			saved = ZmR32(m, cam + 0xC);
			ZmW32(m, cam + 0xC, t);
			ExtZp_Cam_122db0(c, cam, vw, a2);
			if (ZmR32(m, cam + 0xC) == t)
				ZmW32(m, cam + 0xC, saved);
			ZmW32(m, t + 4, 0x006CBC10u);
		}
	}
	else if (k != 0 && (ZmR32(m, k + 0x10) & 8u) != 0 && ZmR32(m, k + 0x14) == 0xEu)
	{
		uint32_t q, clk, gt, dtms, l;
		Ps2F f0, f1, f2, f3, f4, f5, dtf;
		Ps2F dist, height, eyeX, eyeY, eyeZ, r1x, r1y, r1z, r2x, r2y, r2z, nd, h, f9, f10, f11;
		ExtZp_Cam_58a7a0(c, sp + 0x320);
		ExtZp_Cam_58b090(c, sp + 0x320, k + 0x1C);
		ExtZp_Cam_58b9d0(c, sp + 0x320, k + 0x28);
		q = ExtZp_Cam_154de0(c, ZmR32(m, CAM_G_DRIVERS), ZmR32(m, cam + 8));
		f4 = ZmRF(m, q + 0x8C0);
		f3 = CAM_F_THREE;
		f2 = CAM_F_THREEQ;
		f1 = CAM_F_THREEQ;
		f2 = Ps2F_Mul(f2, f4);
		f0 = CAM_F_TENTH;
		f5 = Ps2F_Add(f3, f2);   // target distance
		clk = ZmR32(m, CAM_G_CLOCK);
		f0 = Ps2F_Mul(f0, f4);
		gt = ZmR32(m, clk + 4);
		dtms = ZmR32(m, gt + 8);
		f3 = Ps2F_Add(f1, f0);   // target height
		dtf = Cam_MsToFloat(dtms);
		f1 = ZmRF(m, cam + 0x2E0);
		f2 = Ps2F_Mul(CAM_F_MS, dtf); // dt in seconds
		// distance glides at 2.0 per second towards f5, height at 0.5 per second towards f3
		if (Ps2F_Lt(f1, f5))
		{
			f0 = Ps2F_Mul(f2, CAM_F_TWO);
			f0 = Ps2F_Add(f1, f0);
			ZmWF(m, cam + 0x2E0, Ps2F_Lt(f0, f5) ? f0 : f5);
		}
		else if (!Ps2F_Le(f1, f5))
		{
			f0 = Ps2F_Mul(f2, CAM_F_TWO);
			f1 = ZmRF(m, cam + 0x2E0);
			f0 = Ps2F_Sub(f1, f0);
			ZmWF(m, cam + 0x2E0, Ps2F_Le(f0, f5) ? f5 : f0);
		}
		f1 = ZmRF(m, cam + 0x2E4);
		if (Ps2F_Lt(f1, f3))
		{
			f0 = Ps2F_Mul(f2, CAM_F_HALF);
			f0 = Ps2F_Add(f1, f0);
			ZmWF(m, cam + 0x2E4, Ps2F_Lt(f0, f3) ? f0 : f3);
		}
		else if (!Ps2F_Le(f1, f3))
		{
			f0 = Ps2F_Mul(f2, CAM_F_HALF);
			f1 = ZmRF(m, cam + 0x2E4);
			f0 = Ps2F_Sub(f1, f0);
			ZmWF(m, cam + 0x2E4, Ps2F_Le(f0, f3) ? f3 : f0);
		}
		// eye = K.pos - row1 * distance + row2 * height; look = K.pos + row2 * height; up = row2
		r1x = ZmRF(m, sp + 0x330);
		r1y = ZmRF(m, sp + 0x334);
		r1z = ZmRF(m, sp + 0x338);
		r2x = ZmRF(m, sp + 0x340);
		r2y = ZmRF(m, sp + 0x344);
		r2z = ZmRF(m, sp + 0x348);
		dist = ZmRF(m, cam + 0x2E0);
		nd = Ps2F_Neg(dist);
		eyeY = Ps2F_Mul(r1y, nd);
		eyeZ = Ps2F_Mul(r1z, nd);
		eyeX = Ps2F_Mul(r1x, nd);
		height = ZmRF(m, cam + 0x2E4);
		h = height;
		f11 = Ps2F_Mul(r2x, h);
		f10 = Ps2F_Mul(r2y, h);
		f9 = Ps2F_Mul(r2z, h);
		eyeX = Ps2F_Add(eyeX, f11);
		eyeY = Ps2F_Add(eyeY, f10);
		eyeZ = Ps2F_Add(eyeZ, f9);
		ZmWF(m, sp + 0x410, Ps2F_Add(eyeX, ZmRF(m, k + 0x1C)));
		ZmWF(m, sp + 0x414, Ps2F_Add(eyeY, ZmRF(m, k + 0x20)));
		ZmWF(m, sp + 0x418, Ps2F_Add(eyeZ, ZmRF(m, k + 0x24)));
		ZmWF(m, sp + 0x3F0, Ps2F_Add(ZmRF(m, k + 0x1C), f11));
		ZmWF(m, sp + 0x3F4, Ps2F_Add(ZmRF(m, k + 0x20), f10));
		ZmWF(m, sp + 0x3F8, Ps2F_Add(ZmRF(m, k + 0x24), f9));
		ZmWF(m, sp + 0x3D0, r2x);
		ZmWF(m, sp + 0x3D4, r2y);
		ZmWF(m, sp + 0x3D8, r2z);
		l = 0;
		(void)l;
		ExtZp_Cam_58bd70(c, vw, sp + 0x410, sp + 0x3F0, sp + 0x3D0);
	}
	ExtZp_Pop(c, 0x420);
}
