// THE ZONE PORT, CAMERA PART K3: the chase camera's look direction and pivot
// frame, the matrix inverse, vector helpers and the drop helper (see
// include/platform/native_extchasecam_k3.h and native_extzonephys.h for the
// conventions). Compiles alone (offline tests).

#include <platform/native_extzonephys.h>
#include <platform/native_extchasecam_k3.h>

// Globals of the original (read from the port's memory at the same address).
#define K3_G_CLOCK 0x006F3B00u         // pointer to the global clock; clock + 4 -> time block, time block + 8 = frame time in ms
#define K3_G_TARGETS 0x006F49E0u       // table of camera targets (entry 0x14 bytes, field at +0x2C)
#define K3_G_INV_FAIL 0x006B6498u      // status word the matrix inverse reports when the determinant is too small
#define K3_G_INV_OK 0x006B6490u        // status word the matrix inverse reports otherwise

#define K3_CONST_MS 0x3A83126Fu        // 0.001 (the original's constant): milliseconds -> seconds
#define K3_CONST_150 0x43160000u       // 150.0 (the original's constant): reference rate of the frame time compensation
#define K3_CONST_PITCH 0x3F7FBE77u     // 0.999 (the original's constant)
#define K3_CONST_PITCH_NEG 0xBF7FBE77u // -0.999 (the original's constant)
#define K3_CONST_MINDET 0x00800000u    // 1.17549435e-38 (the original's constant): smallest accepted |determinant|

// An unsigned frame time (ms) as a float: the original converts through a
// signed convert and doubles a halved value when the top bit is set.
static Ps2F K3_U32ToF(uint32_t v)
{
	if ((int32_t)v >= 0)
	{
		return Ps2F_FromInt((int32_t)v);
	}
	{
		uint32_t h = (v >> 1) | (v & 1u);
		Ps2F f = Ps2F_FromInt((int32_t)h);
		return Ps2F_Add(f, f);
	}
}

// The unit vector of (x, y, z): the original's sequence x*x, y*y, acc = sum,
// + z*z, square root, 1 / length, each component times that.
static void K3_Unit3(Ps2F x, Ps2F y, Ps2F z, Ps2F o[3])
{
	Ps2F f4 = Ps2F_Mul(x, x);
	Ps2F f3 = Ps2F_Mul(y, y);
	Ps2Acc acc = Ps2A_Adda(f4, f3);
	f3 = Ps2A_Madd(acc, z, z);
	f3 = Ps2F_Sqrt(f3);
	f3 = Ps2F_Div(PS2F_ONE, f3);
	o[0] = Ps2F_Mul(x, f3);
	o[1] = Ps2F_Mul(y, f3);
	o[2] = Ps2F_Mul(z, f3);
}

// Copies of a frame's rows (4 floats each).
static void K3_Copy4(ExtZpMem *m, uint32_t dst, uint32_t src)
{
	Ps2F f3 = ZmRF(m, src + 0x0u);
	Ps2F f2 = ZmRF(m, src + 0x4u);
	Ps2F f1 = ZmRF(m, src + 0x8u);
	Ps2F f0 = ZmRF(m, src + 0xCu);
	ZmWF(m, dst + 0x0u, f3);
	ZmWF(m, dst + 0x4u, f2);
	ZmWF(m, dst + 0x8u, f1);
	ZmWF(m, dst + 0xCu, f0);
}

// Cross product of two rows in place: a0.xyz = a0 x a1, a0.w = 1.0 (the vector
// product as three accumulator pairs; the rows are read first).
void ExtZp_Cam_58dac0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	Ps2F f2 = ZmRF(m, a1 + 0x8u);
	Ps2F f1 = ZmRF(m, a0 + 0x4u);
	Ps2F f4 = ZmRF(m, a1 + 0x4u);
	Ps2F f3 = ZmRF(m, a0 + 0x8u);
	Ps2F f5 = ZmRF(m, a0 + 0x0u);
	Ps2F f6 = ZmRF(m, a1 + 0x0u);
	Ps2F f0;
	Ps2Acc acc;
	acc = Ps2A_Mula(f1, f2);
	f0 = Ps2A_Msub(acc, f3, f4);
	ZmWF(m, a0 + 0x0u, f0);
	acc = Ps2A_Mula(f3, f6);
	f0 = Ps2A_Msub(acc, f5, f2);
	acc = Ps2A_Mula(f5, f4);
	ZmWF(m, a0 + 0x4u, f0);
	f0 = Ps2A_Msub(acc, f1, f6);
	ZmWF(m, a0 + 0x8u, f0);
	ZmW32(m, a0 + 0xCu, PS2F_ONE); // 1.0
}

// Cross product of two rows, returns the first row's address.
uint32_t ExtZp_Cam_1215f0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZp_Cam_58dac0(c, a0, a1);
	return a0;
}

// Copy of row 1 (offset 0x10) of a frame.
void ExtZp_Cam_121620(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	ZmWF(m, a0 + 0x0u, ZmRF(m, a1 + 0x10u));
	ZmWF(m, a0 + 0x4u, ZmRF(m, a1 + 0x14u));
	ZmWF(m, a0 + 0x8u, ZmRF(m, a1 + 0x18u));
	ZmWF(m, a0 + 0xCu, ZmRF(m, a1 + 0x1Cu));
}

// Copy of a row, returns the destination.
uint32_t ExtZp_Cam_121650(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	K3_Copy4(c->m, a0, a1);
	return a0;
}

// Copy of row 2 (offset 0x20) of a frame.
void ExtZp_Cam_121680(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	ExtZpMem *m = c->m;
	ZmWF(m, a0 + 0x0u, ZmRF(m, a1 + 0x20u));
	ZmWF(m, a0 + 0x4u, ZmRF(m, a1 + 0x24u));
	ZmWF(m, a0 + 0x8u, ZmRF(m, a1 + 0x28u));
	ZmWF(m, a0 + 0xCu, ZmRF(m, a1 + 0x2Cu));
}

// Copy of row 0 of a frame.
void ExtZp_Cam_1216b0(ExtZpCtx *c, uint32_t a0, uint32_t a1)
{
	K3_Copy4(c->m, a0, a1);
}

// Component z / y / x of a vector.
Ps2F ExtZp_Cam_1216e0(ExtZpCtx *c, uint32_t a0) { return ZmRF(c->m, a0 + 0x8u); }
Ps2F ExtZp_Cam_1216f0(ExtZpCtx *c, uint32_t a0) { return ZmRF(c->m, a0 + 0x4u); }
Ps2F ExtZp_Cam_121700(ExtZpCtx *c, uint32_t a0) { return ZmRF(c->m, a0 + 0x0u); }

// The adjugate (transposed cofactors) of the 4x4 matrix at a1 -> a2 (a0 unused).
void ExtZp_Cam_58c4a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23;
	Ps2Acc acc;
	(void)a0;
	f15 = ZmRF(m, a1 + 0x24u); // 0058c4b4
	f6 = ZmRF(m, a1 + 0x3cu); // 0058c4b8
	f7 = ZmRF(m, a1 + 0x2cu); // 0058c4bc
	f14 = ZmRF(m, a1 + 0x34u); // 0058c4c0
	f11 = ZmRF(m, a1 + 0x28u); // 0058c4c4
	f10 = ZmRF(m, a1 + 0x38u); // 0058c4c8
	f16 = ZmRF(m, a1 + 0x14u); // 0058c4cc
	acc = Ps2A_Mula(f15, f6); // 0058c4d0
	f12 = ZmRF(m, a1 + 0x18u); // 0058c4d4
	f3 = Ps2A_Msub(acc, f14, f7); // 0058c4d8
	acc = Ps2A_Mula(f11, f6); // 0058c4dc
	f5 = Ps2A_Msub(acc, f10, f7); // 0058c4e0
	f20 = ZmRF(m, a1 + 0x10u); // 0058c4e4
	acc = Ps2A_Mula(f15, f10); // 0058c4e8
	f8 = ZmRF(m, a1 + 0x1cu); // 0058c4ec
	f2 = Ps2F_Mul(f16, f5); // 0058c4f0
	f0 = Ps2F_Mul(f12, f3); // 0058c4f4
	f1 = Ps2A_Msub(acc, f14, f11); // 0058c4f8
	acc = Ps2A_Suba(f2, f0); // 0058c4fc
	f19 = ZmRF(m, a1 + 0x20u); // 0058c500
	f0 = Ps2A_Madd(acc, f8, f1); // 0058c504
	f21 = ZmRF(m, a1 + 0u); // 0058c508
	f17 = ZmRF(m, a1 + 4u); // 0058c50c
	f13 = ZmRF(m, a1 + 8u); // 0058c510
	f9 = ZmRF(m, a1 + 0xcu); // 0058c514
	f18 = ZmRF(m, a1 + 0x30u); // 0058c518
	acc = Ps2A_Mula(f19, f6); // 0058c51c
	f22 = Ps2F_Mul(f20, f5); // 0058c520
	f4 = Ps2A_Msub(acc, f18, f7); // 0058c524
	ZmWF(m, a2 + 0u, f0); // 0058c528
	acc = Ps2A_Mula(f19, f10); // 0058c52c
	f0 = Ps2F_Mul(f12, f4); // 0058c530
	f2 = Ps2A_Msub(acc, f18, f11); // 0058c534
	acc = Ps2A_Suba(f22, f0); // 0058c538
	f0 = Ps2A_Madd(acc, f8, f2); // 0058c53c
	f0 = Ps2F_Neg(f0); // 0058c540
	f23 = Ps2F_Mul(f20, f3); // 0058c544
	f22 = Ps2F_Mul(f16, f4); // 0058c548
	ZmWF(m, a2 + 0x10u, f0); // 0058c54c
	acc = Ps2A_Mula(f19, f14); // 0058c550
	f0 = Ps2A_Msub(acc, f18, f15); // 0058c554
	acc = Ps2A_Suba(f23, f22); // 0058c558
	f22 = Ps2A_Madd(acc, f8, f0); // 0058c55c
	ZmWF(m, a2 + 0x20u, f22); // 0058c560
	f23 = Ps2F_Mul(f20, f1); // 0058c564
	f22 = Ps2F_Mul(f16, f2); // 0058c568
	acc = Ps2A_Suba(f23, f22); // 0058c56c
	f22 = Ps2A_Madd(acc, f12, f0); // 0058c570
	f22 = Ps2F_Neg(f22); // 0058c574
	ZmWF(m, a2 + 0x30u, f22); // 0058c578
	f23 = Ps2F_Mul(f17, f5); // 0058c57c
	f22 = Ps2F_Mul(f21, f5); // 0058c580
	f5 = Ps2F_Mul(f13, f3); // 0058c584
	acc = Ps2A_Suba(f23, f5); // 0058c588
	f5 = Ps2F_Mul(f21, f3); // 0058c58c
	f3 = Ps2A_Madd(acc, f9, f1); // 0058c590
	f3 = Ps2F_Neg(f3); // 0058c594
	ZmWF(m, a2 + 4u, f3); // 0058c598
	f3 = Ps2F_Mul(f21, f1); // 0058c59c
	f1 = Ps2F_Mul(f13, f4); // 0058c5a0
	acc = Ps2A_Suba(f22, f1); // 0058c5a4
	f1 = Ps2F_Mul(f17, f4); // 0058c5a8
	f4 = Ps2A_Madd(acc, f9, f2); // 0058c5ac
	acc = Ps2A_Suba(f5, f1); // 0058c5b0
	f1 = Ps2F_Mul(f17, f2); // 0058c5b4
	f2 = Ps2A_Madd(acc, f9, f0); // 0058c5b8
	acc = Ps2A_Suba(f3, f1); // 0058c5bc
	f0 = Ps2A_Madd(acc, f13, f0); // 0058c5c0
	ZmWF(m, a2 + 0x14u, f4); // 0058c5c4
	f1 = Ps2F_Neg(f2); // 0058c5c8
	acc = Ps2A_Mula(f16, f6); // 0058c5cc
	ZmWF(m, a2 + 0x24u, f1); // 0058c5d0
	f2 = Ps2A_Msub(acc, f14, f8); // 0058c5d4
	ZmWF(m, a2 + 0x34u, f0); // 0058c5d8
	acc = Ps2A_Mula(f12, f6); // 0058c5dc
	f0 = Ps2A_Msub(acc, f10, f8); // 0058c5e0
	acc = Ps2A_Mula(f16, f10); // 0058c5e4
	f1 = Ps2F_Mul(f17, f0); // 0058c5e8
	f3 = Ps2F_Mul(f21, f0); // 0058c5ec
	f0 = Ps2F_Mul(f13, f2); // 0058c5f0
	f4 = Ps2A_Msub(acc, f14, f12); // 0058c5f4
	acc = Ps2A_Suba(f1, f0); // 0058c5f8
	f0 = Ps2A_Madd(acc, f9, f4); // 0058c5fc
	acc = Ps2A_Mula(f20, f6); // 0058c600
	f5 = Ps2A_Msub(acc, f18, f8); // 0058c604
	ZmWF(m, a2 + 8u, f0); // 0058c608
	f1 = Ps2F_Mul(f21, f4); // 0058c60c
	acc = Ps2A_Mula(f20, f10); // 0058c610
	f0 = Ps2F_Mul(f13, f5); // 0058c614
	f4 = Ps2A_Msub(acc, f18, f12); // 0058c618
	acc = Ps2A_Suba(f3, f0); // 0058c61c
	f3 = Ps2A_Madd(acc, f9, f4); // 0058c620
	f3 = Ps2F_Neg(f3); // 0058c624
	f2 = Ps2F_Mul(f21, f2); // 0058c628
	f0 = Ps2F_Mul(f17, f5); // 0058c62c
	acc = Ps2A_Mula(f20, f14); // 0058c630
	ZmWF(m, a2 + 0x18u, f3); // 0058c634
	f3 = Ps2A_Msub(acc, f18, f16); // 0058c638
	acc = Ps2A_Suba(f2, f0); // 0058c63c
	f2 = Ps2A_Madd(acc, f9, f3); // 0058c640
	f0 = Ps2F_Mul(f17, f4); // 0058c644
	acc = Ps2A_Suba(f1, f0); // 0058c648
	f0 = Ps2A_Madd(acc, f13, f3); // 0058c64c
	acc = Ps2A_Mula(f16, f7); // 0058c650
	f3 = Ps2A_Msub(acc, f15, f8); // 0058c654
	ZmWF(m, a2 + 0x28u, f2); // 0058c658
	f0 = Ps2F_Neg(f0); // 0058c65c
	ZmWF(m, a2 + 0x38u, f0); // 0058c660
	acc = Ps2A_Mula(f12, f7); // 0058c664
	f0 = Ps2A_Msub(acc, f11, f8); // 0058c668
	acc = Ps2A_Mula(f16, f11); // 0058c66c
	f1 = Ps2F_Mul(f17, f0); // 0058c670
	f2 = Ps2F_Mul(f21, f0); // 0058c674
	f0 = Ps2F_Mul(f13, f3); // 0058c678
	f4 = Ps2A_Msub(acc, f15, f12); // 0058c67c
	acc = Ps2A_Suba(f1, f0); // 0058c680
	f0 = Ps2A_Madd(acc, f9, f4); // 0058c684
	acc = Ps2A_Mula(f20, f7); // 0058c688
	f0 = Ps2F_Neg(f0); // 0058c68c
	f5 = Ps2A_Msub(acc, f19, f8); // 0058c690
	ZmWF(m, a2 + 0xcu, f0); // 0058c694
	f1 = Ps2F_Mul(f21, f4); // 0058c698
	acc = Ps2A_Mula(f20, f11); // 0058c69c
	f0 = Ps2F_Mul(f13, f5); // 0058c6a0
	f4 = Ps2A_Msub(acc, f19, f12); // 0058c6a4
	acc = Ps2A_Suba(f2, f0); // 0058c6a8
	f0 = Ps2F_Mul(f17, f4); // 0058c6ac
	f4 = Ps2A_Madd(acc, f9, f4); // 0058c6b0
	f3 = Ps2F_Mul(f21, f3); // 0058c6b4
	f2 = Ps2F_Mul(f17, f5); // 0058c6b8
	acc = Ps2A_Mula(f20, f15); // 0058c6bc
	ZmWF(m, a2 + 0x1cu, f4); // 0058c6c0
	f4 = Ps2A_Msub(acc, f19, f16); // 0058c6c4
	acc = Ps2A_Suba(f3, f2); // 0058c6c8
	f2 = Ps2A_Madd(acc, f9, f4); // 0058c6cc
	acc = Ps2A_Suba(f1, f0); // 0058c6d0
	f1 = Ps2F_Neg(f2); // 0058c6d4
	f0 = Ps2A_Madd(acc, f13, f4); // 0058c6d8
	ZmWF(m, a2 + 0x2cu, f1); // 0058c6dc
	ZmWF(m, a2 + 0x3cu, f0); // 0058c6e0
}

// The determinant of the 4x4 matrix at a0 (returned in f0).
Ps2F ExtZp_Cam_58c700(ExtZpCtx *c, uint32_t a0)
{
	ExtZpMem *m = c->m;
	Ps2F f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19;
	Ps2Acc acc;
	f18 = ZmRF(m, a0 + 0x20u); // 0058c700
	f4 = ZmRF(m, a0 + 0x3cu); // 0058c704
	f5 = ZmRF(m, a0 + 0x2cu); // 0058c708
	f17 = ZmRF(m, a0 + 0x30u); // 0058c70c
	f8 = ZmRF(m, a0 + 0x28u); // 0058c710
	f7 = ZmRF(m, a0 + 0x38u); // 0058c714
	f15 = ZmRF(m, a0 + 0x24u); // 0058c718
	acc = Ps2A_Mula(f18, f4); // 0058c71c
	f14 = ZmRF(m, a0 + 0x34u); // 0058c720
	f11 = Ps2A_Msub(acc, f17, f5); // 0058c724
	acc = Ps2A_Mula(f8, f4); // 0058c728
	f2 = Ps2A_Msub(acc, f7, f5); // 0058c72c
	acc = Ps2A_Mula(f18, f7); // 0058c730
	f9 = Ps2A_Msub(acc, f17, f8); // 0058c734
	acc = Ps2A_Mula(f15, f4); // 0058c738
	f19 = ZmRF(m, a0 + 0x10u); // 0058c73c
	f10 = Ps2A_Msub(acc, f14, f5); // 0058c740
	f16 = ZmRF(m, a0 + 0x14u); // 0058c744
	f13 = ZmRF(m, a0 + 0x18u); // 0058c748
	acc = Ps2A_Mula(f15, f7); // 0058c74c
	f7 = Ps2A_Msub(acc, f14, f8); // 0058c750
	f4 = Ps2F_Mul(f19, f2); // 0058c754
	f5 = Ps2F_Mul(f16, f2); // 0058c758
	acc = Ps2A_Mula(f18, f14); // 0058c75c
	f2 = Ps2F_Mul(f13, f10); // 0058c760
	f8 = Ps2A_Msub(acc, f17, f15); // 0058c764
	acc = Ps2A_Suba(f5, f2); // 0058c768
	f12 = ZmRF(m, a0 + 0x1cu); // 0058c76c
	f2 = Ps2F_Mul(f19, f7); // 0058c770
	f1 = ZmRF(m, a0 + 0u); // 0058c774
	f7 = Ps2A_Madd(acc, f12, f7); // 0058c778
	f6 = ZmRF(m, a0 + 4u); // 0058c77c
	f5 = Ps2F_Mul(f19, f10); // 0058c780
	acc = Ps2A_Mula(f1, f7); // 0058c784
	f1 = Ps2F_Mul(f13, f11); // 0058c788
	f7 = Ps2F_Sub(f4, f1); // 0058c78c
	f1 = Ps2F_Mul(f16, f9); // 0058c790
	f2 = Ps2F_Sub(f2, f1); // 0058c794
	f1 = Ps2F_Mul(f12, f9); // 0058c798
	f1 = Ps2F_Add(f1, f7); // 0058c79c
	f4 = Ps2F_Mul(f16, f11); // 0058c7a0
	f6 = Ps2A_Msub(acc, f6, f1); // 0058c7a4
	acc = Ps2A_Suba(f5, f4); // 0058c7a8
	f3 = ZmRF(m, a0 + 8u); // 0058c7ac
	f1 = Ps2A_Madd(acc, f12, f8); // 0058c7b0
	f0 = ZmRF(m, a0 + 0xcu); // 0058c7b4
	f1 = Ps2F_Mul(f3, f1); // 0058c7b8
	acc = Ps2A_Adda(f1, f6); // 0058c7bc
	f1 = Ps2F_Mul(f13, f8); // 0058c7c0
	f1 = Ps2F_Add(f1, f2); // 0058c7c4
	f0 = Ps2A_Msub(acc, f0, f1); // 0058c7cc
	return f0;
}

// Matrix inverse: a1 = a2^-1 (adjugate / determinant); a0 gets a status word.
// The guard uses |determinant| (the original clears the sign bit through a
// stack byte), the scale uses the signed determinant. Too small: a1 is left alone.
void ExtZp_Cam_58c3a0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x90u);
	Ps2F f0, f1, f2;
	ExtZp_Cam_58c4a0(c, a1, a2, sp + 0x40u);
	f0 = ExtZp_Cam_58c700(c, a2);
	ZmWF(m, sp + 0x8Cu, f0);
	ZmW8(m, sp + 0x8Fu, (uint8_t)(ZmR8(m, sp + 0x8Fu) & 0x7Fu)); // sign bit cleared
	f1 = K3_CONST_MINDET;
	f2 = ZmRF(m, sp + 0x8Cu);
	if (Ps2F_Lt(f2, f1))
	{
		ZmW32(m, a0, ZmR32(m, K3_G_INV_FAIL));
	}
	else
	{
		uint32_t i, j;
		f1 = Ps2F_Div(PS2F_ONE, f0);
		for (i = 0; i < 4; i++)
		{
			for (j = 0; j < 4; j++)
			{
				f0 = ZmRF(m, sp + 0x40u + i * 16u + j * 4u);
				f0 = Ps2F_Mul(f1, f0);
				ZmWF(m, a1 + i * 16u + j * 4u, f0);
			}
		}
		ZmW32(m, a0, ZmR32(m, K3_G_INV_OK));
	}
	ExtZp_Pop(c, 0x90u);
}

// The 128 bits at a (any alignment) as the main CPU's mtsab / lq / lq / qfsrv
// sequence builds them: the 256-bit pair (quad(a+16) : quad(a)) shifted right by
// 8 * (a & 15) bits, low 128 bits kept.
static Ps2Vec4 K3_LoadFunnel(ExtZpMem *m, uint32_t a)
{
	Ps2Vec4 lo = ZmRV(m, a);
	Ps2Vec4 hi = ZmRV(m, a + 0x10u);
	uint8_t bytes[32];
	Ps2Vec4 r;
	uint32_t sh = a & 0xFu, i, k;
	for (i = 0; i < 4; i++)
	{
		for (k = 0; k < 4; k++)
		{
			bytes[i * 4 + k] = (uint8_t)(lo.v[i] >> (8 * k));
			bytes[16 + i * 4 + k] = (uint8_t)(hi.v[i] >> (8 * k));
		}
	}
	for (i = 0; i < 4; i++)
	{
		r.v[i] = (uint32_t)bytes[sh + i * 4] | ((uint32_t)bytes[sh + i * 4 + 1] << 8) |
		         ((uint32_t)bytes[sh + i * 4 + 2] << 16) | ((uint32_t)bytes[sh + i * 4 + 3] << 24);
	}
	return r;
}

// a3 vec3 values (12-byte stride, any alignment) at a1, each times the upper 3x3
// of the matrix a0 (rows loaded into vf1..vf3), result to a2: vmulax / vmadday /
// vmaddz on the vector unit, the words stored one by one (prot3w rotates the
// three low words).
void ExtZp_Cam_58cb30(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	Ps2Vec4 vf1, vf2, vf3, vf9, vf17;
	uint32_t t0;
	if ((a0 & 0xFu) == 0)
	{
		vf1 = ZmRV(m, a0 + 0x00u);
		vf2 = ZmRV(m, a0 + 0x10u);
		vf3 = ZmRV(m, a0 + 0x20u);
		(void)ZmRV(m, a0 + 0x30u); // vf4, not used below
	}
	else
	{
		vf1 = K3_LoadFunnel(m, a0 + 0x00u);
		vf2 = K3_LoadFunnel(m, a0 + 0x10u);
		vf3 = K3_LoadFunnel(m, a0 + 0x20u);
		(void)K3_LoadFunnel(m, a0 + 0x30u); // vf4, not used below
	}
	vf9.v[0] = vf9.v[1] = vf9.v[2] = vf9.v[3] = 0u;
	for (t0 = 0; t0 < a3; t0++)
	{
		uint32_t src = a1 + t0 * 12u;
		uint32_t dst = a2 + t0 * 12u;
		vf17 = K3_LoadFunnel(m, src);
		// Measured against the original: the overflow of vmulax's product is carried
		// into vmadday's add (Ps2V_Madda starts from the value alone), vmadday's own
		// overflow is not carried into vmaddz. So the lanes use Ps2F_MulAddEx.
		{
			uint32_t l;
			for (l = 0; l < 4; l++)
			{
				Ps2Acc acc = Ps2A_Mula(vf1.v[l], vf17.v[0]); // vmulax
				unsigned fl = 0;
				acc.v = Ps2F_MulAddEx(acc.v, acc.overflow, vf2.v[l], vf17.v[1], 0, &fl); // vmadday
				acc.overflow = (fl & PS2F_FLAG_OVERFLOW) != 0;
				vf9.v[l] = Ps2F_MulAddEx(acc.v, 0, vf3.v[l], vf17.v[2], 0, 0); // vmaddz
			}
		}
		ZmW32(m, dst + 0x0u, vf9.v[0]);
		ZmW32(m, dst + 0x4u, vf9.v[1]);
		ZmW32(m, dst + 0x8u, vf9.v[2]);
	}
}

// Drop helper: while a1 != 0 the drop time (C + 0x164) grows by the frame time
// in seconds and F2 = F with its position moved along the frame's up row by
// (P + 0x1E4 + P + 0x1E0 * drop time); otherwise the time is reset and F2 = F.
void ExtZp_Cam_120b10(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x60u);
	uint32_t s0 = a3, s2 = sp + 0x54u, s1 = sp + 0x58u;
	if (a1 == 0)
	{
		ZmW32(m, a0 + 0x164u, 0u);
		ExtZp_Cam_1071e0(c, s0, a2);
	}
	else
	{
		uint32_t v0 = ZmR32(m, K3_G_CLOCK);
		Ps2F f0, f1, f2, f3, f4, f5;
		v0 = ZmR32(m, v0 + 4u);
		v0 = ZmR32(m, v0 + 8u);
		f2 = K3_U32ToF(v0);
		f1 = K3_CONST_MS;
		f0 = ZmRF(m, a0 + 0x164u);
		f1 = Ps2F_Mul(f1, f2);
		f0 = Ps2F_Add(f0, f1);
		ZmWF(m, a0 + 0x164u, f0);
		v0 = ZmR32(m, a0 + 0xCu); // P
		f1 = ZmRF(m, a0 + 0x164u);
		f3 = ZmRF(m, a2 + 0x20u);
		f5 = ZmRF(m, a2 + 0x24u);
		f4 = ZmRF(m, a2 + 0x28u);
		f2 = ZmRF(m, v0 + 0x1E0u);
		f0 = ZmRF(m, v0 + 0x1E4u);
		ZmWF(m, sp + 0x44u, f5);
		ZmWF(m, sp + 0x48u, f4);
		ZmWF(m, sp + 0x40u, f3);
		f1 = Ps2F_Mul(f2, f1);
		f2 = Ps2F_Add(f0, f1);
		f0 = Ps2F_Mul(f3, f2);
		ZmWF(m, sp + 0x50u, f0);
		f1 = Ps2F_Mul(f5, f2);
		f0 = Ps2F_Mul(f4, f2);
		ZmWF(m, s2, f1);
		ZmWF(m, s1, f0);
		ExtZp_Cam_1071e0(c, s0, a2);
		f2 = ZmRF(m, sp + 0x50u);
		f3 = ZmRF(m, s0 + 0x30u);
		f1 = ZmRF(m, s2);
		f0 = ZmRF(m, s1);
		f2 = Ps2F_Add(f3, f2);
		ZmWF(m, s0 + 0x30u, f2);
		f2 = ZmRF(m, s0 + 0x34u);
		f1 = Ps2F_Add(f2, f1);
		ZmWF(m, s0 + 0x34u, f1);
		f1 = ZmRF(m, s0 + 0x38u);
		f0 = Ps2F_Add(f1, f0);
		ZmWF(m, s0 + 0x38u, f0);
	}
	ExtZp_Pop(c, 0x60u);
}

// Blend of two vectors a1 (weight 0) and a2 (weight 1) into t0. The weight
// f20: a3 != 0: f12 / f15 (1 when f15 <= 0); a3 == 0: 1 - f13 / f14 (0 when
// f14 <= 0); weight >= 1 gives a2, weight <= 0 gives a1, else (1-w)*a1 + w*a2.
void ExtZp_Cam_123ac0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                      Ps2F f12, Ps2F f13, Ps2F f14, Ps2F f15)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x70u);
	uint32_t s1 = a2, s0 = t0;
	Ps2F f0, f1, f20;
	(void)a0;
	if (a3 != 0)
	{
		f0 = 0u;
		if (Ps2F_Le(f15, f0))
		{
			f20 = PS2F_ONE;
		}
		else
		{
			f20 = Ps2F_Div(f12, f15);
		}
	}
	else
	{
		f20 = 0u;
		if (!Ps2F_Le(f14, f20))
		{
			f1 = Ps2F_Div(f13, f14);
			f0 = PS2F_ONE;
			f20 = Ps2F_Sub(f0, f1);
		}
	}
	f0 = PS2F_ONE;
	if (!Ps2F_Lt(f20, f0))
	{
		ZmWF(m, s0 + 0x0u, ZmRF(m, s1 + 0x0u));
		ZmWF(m, s0 + 0x4u, ZmRF(m, s1 + 0x4u));
		ZmWF(m, s0 + 0x8u, ZmRF(m, s1 + 0x8u));
	}
	else
	{
		f0 = 0u;
		if (Ps2F_Le(f20, f0))
		{
			ZmWF(m, s0 + 0x0u, ZmRF(m, a1 + 0x0u));
			ZmWF(m, s0 + 0x4u, ZmRF(m, a1 + 0x4u));
			ZmWF(m, s0 + 0x8u, ZmRF(m, a1 + 0x8u));
		}
		else
		{
			ExtZp_Cam_11f2a0(c, sp + 0x60u, a1);
			f0 = PS2F_ONE;
			ExtZp_Cam_11f270(c, sp + 0x60u, Ps2F_Sub(f0, f20));
			ExtZp_Cam_11f2a0(c, sp + 0x50u, s1);
			ExtZp_Cam_11f270(c, sp + 0x50u, f20);
			ExtZp_Cam_11f2a0(c, sp + 0x40u, sp + 0x60u);
			ExtZp_Cam_11f230(c, sp + 0x40u, sp + 0x50u);
			ExtZp_Cam_11f210(c, s0, sp + 0x40u);
		}
	}
	ExtZp_Pop(c, 0x70u);
}

// Look direction and pivot frame of the chase camera. F (t3) = frame of the kart
// matrix M (a1) with the pivot adjustment P + 0x64 added in the frame; the
// look direction at (C + 0x18) is smoothed towards a blend of the kart's
// forward row and the velocity direction Vm (a2) by weights from the speed f12
// and the slide value, its pitch limited and blended with the maglev times by
// 123ac0; the frame's forward row F + 0x10 gets it. a3 != 0: the reverse mode
// (the velocity direction goes through the inverse of F first); t0 != 0: slide
// state; t1 != 0: use the slide weight. Stack temporaries as in the original.
void ExtZp_Cam_120c40(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                      uint32_t t0, uint32_t t1, uint32_t t2, uint32_t t3, Ps2F f12)
{
	ExtZpMem *m = c->m;
	uint32_t sp = ExtZp_Push(c, 0x2D0u);
	uint32_t s0 = t3, s1 = t0, s2 = a3, s3 = a2, s4 = a1, s5 = a0, s6 = t1;
	uint32_t P, v0, v1, p274, p278;
	Ps2F f0, f1, f2, f3, f5, f6, f20, f21;
	Ps2F o[3];
	(void)t2;
	f21 = f12;
	ExtZp_Cam_1071e0(c, s0, a1);
	// the velocity direction, normalised, to sp + 0x2C0
	f6 = ZmRF(m, s3 + 0x0u);
	f1 = ZmRF(m, s3 + 0x4u);
	f0 = ZmRF(m, s3 + 0x8u);
	ZmWF(m, sp + 0x2C4u, f1);
	ZmWF(m, sp + 0x2C8u, f0);
	K3_Unit3(f6, ZmRF(m, sp + 0x2C4u), ZmRF(m, sp + 0x2C8u), o);
	ZmWF(m, sp + 0x2C0u, o[0]);
	ZmWF(m, sp + 0x2C4u, o[1]);
	ZmWF(m, sp + 0x2C8u, o[2]);
	// the pivot adjustment
	P = ZmR32(m, s5 + 0xCu);
	ZmWF(m, sp + 0x2B0u, ZmRF(m, P + 0x64u));
	ZmWF(m, sp + 0x2B4u, ZmRF(m, P + 0x68u));
	ZmWF(m, sp + 0x2B8u, ZmRF(m, P + 0x6Cu));
	if (s2 != 0)
	{
		goto L_13b0;
	}
	ExtZp_Cam_58cb30(c, s0, sp + 0x2B0u, sp + 0x2B0u, 1u);
	ExtZp_Cam_1071e0(c, s5 + 0x48u, s4);
L_120d48:
	f1 = ZmRF(m, s0 + 0x30u);
	f0 = ZmRF(m, sp + 0x2B0u);
	f20 = 0u;
	f0 = Ps2F_Add(f1, f0);
	ZmWF(m, s0 + 0x30u, f0);
	f0 = ZmRF(m, sp + 0x2B4u);
	f1 = ZmRF(m, s0 + 0x34u);
	f0 = Ps2F_Add(f1, f0);
	ZmWF(m, s0 + 0x34u, f0);
	f0 = ZmRF(m, sp + 0x2B8u);
	f1 = ZmRF(m, s0 + 0x38u);
	f0 = Ps2F_Add(f1, f0);
	ZmWF(m, s0 + 0x38u, f0);
	f2 = ZmRF(m, s0 + 0x14u);
	f1 = ZmRF(m, s0 + 0x18u);
	f0 = ZmRF(m, s0 + 0x10u);
	ZmWF(m, sp + 0x244u, f2);
	ZmWF(m, sp + 0x248u, f1);
	ZmWF(m, sp + 0x284u, f2);
	ZmWF(m, sp + 0x288u, f1);
	ZmWF(m, sp + 0x280u, f0);
	v1 = ZmR32(m, s5 + 0xCu); // P
	ZmWF(m, sp + 0x240u, f0);
	f2 = ZmRF(m, v1 + 0x34u); // m_SpeedForKartAt
	f0 = ZmRF(m, v1 + 0x38u); // m_SpeedForVelocityAt
	f1 = Ps2F_Sub(f0, f2);
	if (Ps2F_Le(f1, f20))
	{
		// speed range empty: the weight is 0 below the lower speed, 1 otherwise
		if (!Ps2F_Lt(f21, f2))
		{
			f20 = PS2F_ONE;
		}
		goto L_120e24;
	}
	f0 = Ps2F_Sub(f21, f2);
	f20 = Ps2F_Div(f0, f1);
	f1 = PS2F_ONE;
	if (Ps2F_Lt(f20, f1))
	{
		f1 = f20;
	}
	f0 = 0u;
	if (Ps2F_Le(f1, f0))
	{
		f20 = f0;
	}
	else
	{
		f0 = PS2F_ONE;
		if (!Ps2F_Lt(f20, f0))
		{
			f20 = f0;
		}
	}
L_120e24:
	if (s1 == 0 && s6 == 0)
	{
		f0 = ZmRF(m, v1 + 0x60u); // m_MaxNonSlideVelocityPercent
		if (!Ps2F_Lt(f20, f0))
		{
			f20 = f0;
		}
		ZmW32(m, s5 + 0x13Cu, 0u);
	}
	if (s2 != 0)
	{
		f20 = PS2F_ONE;
	}
	if (s1 != 0)
	{
		uint32_t a5;
		v1 = ZmR32(m, s5 + 0xCu);
		a5 = ZmR32(m, s5 + 0x8u);
		f20 = ZmRF(m, v1 + 0x58u); // m_SlideVelocityPercent
		v0 = ExtZp_Cam_154e00(c, ZmR32(m, K3_G_TARGETS), a5);
		{
			uint32_t a4 = ZmR32(m, v0 + 4u);
			uint32_t st = ZmR32(m, a4 + 0x3E0u);
			if (st == 6 || st == 5)
			{
				f0 = ZmRF(m, a4 + 0x4A8u);
			}
			else
			{
				f0 = 0u;
			}
			ZmWF(m, s5 + 0x13Cu, f0);
		}
	}
	if (s6 != 0)
	{
		v0 = ZmR32(m, s5 + 0xCu);
		f1 = ZmRF(m, s5 + 0x13Cu);
		f0 = 0u;
		f20 = ZmRF(m, v0 + 0xF4u);
		if (Ps2F_Le(f1, f0))
		{
			f0 = ZmRF(m, v0 + 0xFCu);
		}
		else
		{
			f0 = ZmRF(m, v0 + 0xF8u);
		}
		f0 = Ps2F_Mul(f1, f0);
		f20 = Ps2F_Add(f20, f0);
	}
	v0 = ZmR32(m, s5 + 0xCu);
	f3 = ZmRF(m, v0 + 0x100u); // m_PercentVelocityAdjustmentSpeed
	v0 = ZmR8(m, v0 + 0x5Cu);  // m_bCompensateForDTime
	if (v0 != 0)
	{
		v0 = ZmR32(m, K3_G_CLOCK);
		v0 = ZmR32(m, v0 + 4u);
		v0 = ZmR32(m, v0 + 8u);
		f2 = K3_U32ToF(v0);
		f1 = K3_CONST_MS;
		f0 = K3_CONST_150;
		f1 = Ps2F_Mul(f1, f2);
		f0 = Ps2F_Mul(f1, f0);
		f3 = Ps2F_Mul(f3, f0);
		f0 = PS2F_ONE;
		if (!Ps2F_Lt(f3, f0))
		{
			f3 = f0;
		}
	}
	f1 = ZmRF(m, s5 + 0x138u);
	f0 = Ps2F_Sub(f20, f1);
	f0 = Ps2F_Mul(f0, f3);
	f20 = Ps2F_Add(f1, f0);
	ZmWF(m, s5 + 0x138u, f20);
	ExtZp_Cam_11f2a0(c, sp + 0x230u, sp + 0x280u);
	f0 = PS2F_ONE;
	ExtZp_Cam_11f270(c, sp + 0x230u, Ps2F_Sub(f0, f20));
	ExtZp_Cam_11f2a0(c, sp + 0x220u, sp + 0x2C0u);
	ExtZp_Cam_11f270(c, sp + 0x220u, f20);
	ExtZp_Cam_11f2a0(c, sp + 0x210u, sp + 0x230u);
	ExtZp_Cam_11f230(c, sp + 0x210u, sp + 0x220u);
	ExtZp_Cam_11f210(c, sp + 0x270u, sp + 0x210u);
	// smoothing of the look direction C + 0x18 towards sp + 0x270
	v0 = ZmR32(m, s5 + 0xCu);
	f20 = ZmRF(m, v0 + 0x3Cu); // m_YawAdjustmentPercentPerFrame
	v0 = ZmR8(m, v0 + 0x5Cu);
	if (v0 != 0)
	{
		v0 = ExtZp_Cam_11f200(c);
		v0 = ExtZp_Cam_11f1f0(c, v0, 0u);
		v0 = ExtZp_Cam_11f1e0(c, v0);
		f2 = K3_U32ToF(v0);
		f1 = K3_CONST_MS;
		f0 = K3_CONST_150;
		f1 = Ps2F_Mul(f1, f2);
		f0 = Ps2F_Mul(f1, f0);
		f20 = Ps2F_Mul(f20, f0);
		f0 = PS2F_ONE;
		if (!Ps2F_Lt(f20, f0))
		{
			f20 = f0;
		}
	}
	ExtZp_Cam_11f2a0(c, sp + 0x200u, sp + 0x270u);
	ExtZp_Cam_11f1a0(c, sp + 0x200u, s5 + 0x18u);
	ExtZp_Cam_11f270(c, sp + 0x200u, f20);
	ExtZp_Cam_11f2a0(c, sp + 0x1F0u, s5 + 0x18u);
	ExtZp_Cam_11f230(c, sp + 0x1F0u, sp + 0x200u);
	ExtZp_Cam_11f210(c, sp + 0x270u, sp + 0x1F0u);
	p274 = sp + 0x274u;
	p278 = sp + 0x278u;
	f3 = ZmRF(m, p274);
	f5 = ZmRF(m, sp + 0x270u);
	f6 = ZmRF(m, p278);
	f1 = ZmRF(m, p274);
	f0 = ZmRF(m, p278);
	K3_Unit3(f5, f3, f6, o);
	ZmWF(m, sp + 0x270u, o[0]);
	ZmWF(m, p274, o[1]);
	ZmWF(m, p278, o[2]);
	ZmWF(m, s5 + 0x18u, o[0]);
	ZmWF(m, s5 + 0x1Cu, ZmRF(m, p274));
	ZmWF(m, s5 + 0x20u, ZmRF(m, p278));
	f1 = ZmRF(m, sp + 0x274u);
	f0 = ZmRF(m, sp + 0x278u);
	f3 = ZmRF(m, sp + 0x270u);
	f2 = ZmRF(m, sp + 0x270u);
	ZmWF(m, sp + 0x264u, f1);
	ZmWF(m, sp + 0x254u, f1);
	ZmWF(m, sp + 0x250u, f3);
	ZmWF(m, sp + 0x260u, f2);
	ZmWF(m, sp + 0x268u, f0);
	ZmWF(m, sp + 0x258u, f0);
	// pitch limits
	v0 = ZmR32(m, s5 + 0xCu);
	if (ZmR8(m, v0 + 0x5Du) != 0) // m_bPreventCameraPitchUp
	{
		f1 = ZmRF(m, sp + 0x268u);
		f0 = 0u;
		if (!Ps2F_Lt(f1, f0))
		{
			f1 = f0;
		}
		f3 = ZmRF(m, sp + 0x264u);
		f5 = ZmRF(m, sp + 0x260u);
		ZmWF(m, sp + 0x268u, f1);
		f6 = ZmRF(m, sp + 0x268u);
		f1 = ZmRF(m, sp + 0x264u);
		K3_Unit3(f5, f3, f6, o);
		f0 = ZmRF(m, sp + 0x268u);
		ZmWF(m, sp + 0x260u, o[0]);
		ZmWF(m, sp + 0x264u, o[1]);
		ZmWF(m, sp + 0x268u, o[2]);
	}
	v0 = ZmR32(m, s5 + 0xCu);
	if (ZmR8(m, v0 + 0x5Eu) != 0) // m_bPreventCameraPitchDown
	{
		f1 = ZmRF(m, sp + 0x268u);
		f0 = 0u;
		if (Ps2F_Le(f1, f0))
		{
			f1 = f0;
		}
		f3 = ZmRF(m, sp + 0x264u);
		f5 = ZmRF(m, sp + 0x260u);
		ZmWF(m, sp + 0x268u, f1);
		f6 = ZmRF(m, sp + 0x268u);
		f1 = ZmRF(m, sp + 0x264u);
		K3_Unit3(f5, f3, f6, o);
		ZmWF(m, sp + 0x260u, o[0]);
		ZmWF(m, sp + 0x264u, o[1]);
		ZmWF(m, sp + 0x268u, o[2]);
	}
	// blend with the maglev times
	v0 = ZmR32(m, s5 + 0xCu);
	{
		Ps2F g12 = ZmRF(m, s5 + 0x90u);
		uint32_t g7 = ZmR8(m, s5 + 0x98u);
		Ps2F g13 = ZmRF(m, s5 + 0x94u);
		Ps2F g14 = ZmRF(m, v0 + 0xC8u);
		Ps2F g15 = ZmRF(m, v0 + 0xC4u);
		ExtZp_Cam_123ac0(c, s5, sp + 0x260u, sp + 0x250u, g7, sp + 0x270u, g12, g13, g14, g15);
	}
	f3 = ZmRF(m, p274);
	f5 = ZmRF(m, sp + 0x270u);
	f6 = ZmRF(m, p278);
	f1 = ZmRF(m, p274);
	f0 = ZmRF(m, p278);
	K3_Unit3(f5, f3, f6, o);
	ZmWF(m, sp + 0x270u, o[0]);
	ZmWF(m, p274, o[1]);
	ZmWF(m, p278, o[2]);
	f0 = ExtZp_Cam_121700(c, sp + 0x270u);
	ZmWF(m, s0 + 0x10u, f0);
	f0 = ExtZp_Cam_1216f0(c, sp + 0x270u);
	ZmWF(m, s0 + 0x14u, f0);
	f0 = ExtZp_Cam_1216e0(c, sp + 0x270u);
	ZmWF(m, s0 + 0x18u, f0);
	ExtZp_Cam_1216b0(c, sp + 0x120u, s0);
	ExtZp_Cam_121680(c, sp + 0x110u, s0);
	ExtZp_Cam_121650(c, sp + 0x120u, sp + 0x110u);
	ExtZp_Cam_1216b0(c, sp + 0x100u, s0);
	ExtZp_Cam_121620(c, sp + 0xF0u, s0);
	ExtZp_Cam_1215f0(c, sp + 0x100u, sp + 0xF0u);
	ExtZp_Cam_121680(c, sp + 0xE0u, s0);
	ExtZp_Cam_1216b0(c, sp + 0xD0u, s0);
	ExtZp_Cam_121650(c, sp + 0xE0u, sp + 0xD0u);
	ExtZp_Cam_121680(c, sp + 0xC0u, s0);
	ExtZp_Cam_121620(c, sp + 0xB0u, s0);
	ExtZp_Cam_1215f0(c, sp + 0xC0u, sp + 0xB0u);
	ExtZp_Pop(c, 0x2D0u);
	return;

L_13b0:
	// reverse mode: the velocity direction in the frame of F's inverse
	{
		uint32_t s7 = sp + 0x2A4u, s3b = sp + 0x2A8u;
		ExtZp_Cam_1071e0(c, sp + 0x1B0u, s0);
		ExtZp_Cam_58c3a0(c, sp + 0x2CCu, sp + 0x1B0u, sp + 0x1B0u);
		f2 = ZmRF(m, sp + 0x2C0u);
		f1 = ZmRF(m, sp + 0x2C4u);
		f0 = ZmRF(m, sp + 0x2C8u);
		ZmWF(m, sp + 0x2A0u, f2);
		ZmWF(m, s7, f1);
		ZmWF(m, s3b, f0);
		ExtZp_Cam_58cb30(c, sp + 0x1B0u, sp + 0x2A0u, sp + 0x2A0u, 1u);
		f1 = ZmRF(m, s3b);
		f0 = K3_CONST_PITCH;
		if (Ps2F_Lt(f1, f0))
		{
			f0 = K3_CONST_PITCH_NEG;
			if (!Ps2F_Le(f1, f0))
			{
				f3 = ZmRF(m, sp + 0x2A4u);
				ZmW32(m, s3b, 0u);
				f5 = ZmRF(m, sp + 0x2A0u);
				f6 = ZmRF(m, sp + 0x2A8u);
				f1 = ZmRF(m, s7);
				f0 = ZmRF(m, s3b);
				K3_Unit3(f5, f3, f6, o);
				ZmWF(m, sp + 0x2A0u, o[0]);
				ZmWF(m, s7, o[1]);
				ZmWF(m, s3b, o[2]);
			}
		}
		f2 = ZmRF(m, sp + 0x2A0u);
		f1 = ZmRF(m, sp + 0x2A4u);
		f0 = ZmRF(m, sp + 0x2A8u);
		ZmWF(m, sp + 0x290u, f2);
		ZmWF(m, sp + 0x294u, f1);
		ZmWF(m, sp + 0x298u, f0);
		ExtZp_Cam_58cb30(c, s0, sp + 0x290u, sp + 0x290u, 1u);
		ExtZp_Cam_1071e0(c, s5 + 0x48u, s4);
		f0 = ExtZp_Cam_121700(c, sp + 0x290u);
		ZmWF(m, s5 + 0x58u, f0);
		f0 = ExtZp_Cam_1216f0(c, sp + 0x290u);
		ZmWF(m, s5 + 0x5Cu, f0);
		f0 = ExtZp_Cam_1216e0(c, sp + 0x290u);
		ZmWF(m, s5 + 0x60u, f0);
		ExtZp_Cam_1216b0(c, sp + 0x1A0u, s5 + 0x48u);
		ExtZp_Cam_121680(c, sp + 0x190u, s5 + 0x48u);
		ExtZp_Cam_121650(c, sp + 0x1A0u, sp + 0x190u);
		ExtZp_Cam_1216b0(c, sp + 0x180u, s5 + 0x48u);
		ExtZp_Cam_121620(c, sp + 0x170u, s5 + 0x48u);
		ExtZp_Cam_1215f0(c, sp + 0x180u, sp + 0x170u);
		ExtZp_Cam_121680(c, sp + 0x160u, s5 + 0x48u);
		ExtZp_Cam_1216b0(c, sp + 0x150u, s5 + 0x48u);
		ExtZp_Cam_121650(c, sp + 0x160u, sp + 0x150u);
		ExtZp_Cam_121680(c, sp + 0x140u, s5 + 0x48u);
		ExtZp_Cam_121620(c, sp + 0x130u, s5 + 0x48u);
		ExtZp_Cam_1215f0(c, sp + 0x140u, sp + 0x130u);
		ExtZp_Cam_58cb30(c, s5 + 0x48u, sp + 0x2B0u, sp + 0x2B0u, 1u);
	}
	goto L_120d48;
}

