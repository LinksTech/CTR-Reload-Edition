/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/psx/INLINE_C.C
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#include <macros.h>
#include <ctr_subpixel.h>
#include <psx/gtereg.h>
#include <psx/inline_c.h>
#include <psx/libgte.h>

extern u32 gte_leadingzerocount(u32 lzcs);

u32 MFC2(s32 reg)
{
	switch (reg)
	{
	case 1:
	case 3:
	case 5:
	case 8:
	case 9:
	case 10:
	case 11:
		gteRegs.CP2D.p[reg].d = (s32)gteRegs.CP2D.p[reg].sw.l;
		break;

	case 7:
	case 16:
	case 17:
	case 18:
	case 19:
		gteRegs.CP2D.p[reg].d = (u32)gteRegs.CP2D.p[reg].w.l;
		break;

	case 15:
		gteRegs.CP2D.p[reg].d = C2_SXY2;
		break;

	case 28:
	case 29:
		gteRegs.CP2D.p[reg].d = LIM(C2_IR1 >> 7, 0x1f, 0, 0) | (LIM(C2_IR2 >> 7, 0x1f, 0, 0) << 5) | (LIM(C2_IR3 >> 7, 0x1f, 0, 0) << 10);
		break;
	}

	return gteRegs.CP2D.p[reg].d;
}

s32 MFC2_S(s32 reg)
{
	// TODO(aalhendi): Confirm whether modifier reads should also use signed semantics.
	switch (reg)
	{
	case 1:
	case 3:
	case 5:
	case 8:
	case 9:
	case 10:
	case 11:
		gteRegs.CP2D.p[reg].d = (s32)gteRegs.CP2D.p[reg].sw.l;
		break;

	case 7:
	case 16:
	case 17:
	case 18:
	case 19:
		gteRegs.CP2D.p[reg].d = (u32)gteRegs.CP2D.p[reg].w.l;
		break;

	case 15:
		gteRegs.CP2D.p[reg].d = C2_SXY2;
		break;

	case 28:
	case 29:
		gteRegs.CP2D.p[reg].d = LIM(C2_IR1 >> 7, 0x1f, 0, 0) | (LIM(C2_IR2 >> 7, 0x1f, 0, 0) << 5) | (LIM(C2_IR3 >> 7, 0x1f, 0, 0) << 10);
		break;
	}

	return gteRegs.CP2D.p[reg].sd;
}

void MTC2(u32 value, s32 reg)
{
	switch (reg)
	{
	case 15:
		C2_SXY0 = C2_SXY1;
		C2_SXY1 = C2_SXY2;
		C2_SXY2 = value;
		NativeSubpixel_PushExternal();
		break;

	case 28:
		C2_IR1 = (value & 0x1f) << 7;
		C2_IR2 = (value & 0x3e0) << 2;
		C2_IR3 = (value & 0x7c00) >> 3;
		break;

	case 30:
		C2_LZCR = gte_leadingzerocount(value);
		break;

	case 31:
		return;
	}

	// Whoever writes an SXY register from outside has no fractional part
	// for it - the shadow stack of the subpixel counting becomes invalid at this
	// place. Without that a later store would report the fraction
	// of the PREVIOUS projection and show coverage that does not exist.
	NativeSubpixel_InvalidateReg(reg);

	gteRegs.CP2D.p[reg].d = value;
}

void MTC2_S(s32 value, s32 reg)
{
	switch (reg)
	{
	case 15:
		C2_SXY0 = C2_SXY1;
		C2_SXY1 = C2_SXY2;
		C2_SXY2 = value;
		NativeSubpixel_PushExternal();
		break;

	case 28:
		C2_IR1 = (value & 0x1f) << 7;
		C2_IR2 = (value & 0x3e0) << 2;
		C2_IR3 = (value & 0x7c00) >> 3;
		break;

	case 30:
		C2_LZCR = gte_leadingzerocount(value);
		break;

	case 31:
		return;
	}

	// Whoever writes an SXY register from outside has no fractional part
	// for it - the shadow stack of the subpixel counting becomes invalid at this
	// place. Without that a later store would report the fraction
	// of the PREVIOUS projection and show coverage that does not exist.
	NativeSubpixel_InvalidateReg(reg);

	gteRegs.CP2D.p[reg].sd = value;
}

void CTC2(u32 value, s32 reg)
{
	switch (reg)
	{
	case 4:
	case 12:
	case 20:
	case 26:
	case 27:
	case 29:
	case 30:
		value = (u32)(s32)(s16)value;
		break;

	case 31:
		value = value & 0x7ffff000;
		if ((value & 0x7f87e000) != 0)
		{
			value |= 0x80000000;
		}
		break;
	}

	gteRegs.CP2C.p[reg].d = value;
}

void CTC2_S(s32 value, s32 reg)
{
	switch (reg)
	{
	case 4:
	case 12:
	case 20:
	case 26:
	case 27:
	case 29:
	case 30:
		value = (s32)(s16)value;
		break;

	case 31:
		value = value & 0x7ffff000;
		if ((value & 0x7f87e000) != 0)
		{
			value |= 0x80000000;
		}
		break;
	}

	gteRegs.CP2C.p[reg].sd = value;
}

u32 CFC2(s32 reg)
{
	// TODO(aalhendi): Verify CFC2 control-register edge cases against PS1 GTE behavior.

	return gteRegs.CP2C.p[reg].d;
}

s32 CFC2_S(s32 reg)
{
	// TODO(aalhendi): Verify CFC2 control-register edge cases against PS1 GTE behavior.

	return gteRegs.CP2C.p[reg].sd;
}

// THE SWITCH BETWEEN THE TWO GTE PATHS.
//
// Here and only here, because every single GTE operation of the game
// passes through here: all gte_* macros in include/psx/inline_c.h end in doCOP2.
//
// At runtime and not via #ifdef, and that is the whole purpose. Two
// builds of which only one ever exists cannot be measured
// side by side - one can only start them one after the other and hope
// that nothing else was different. A branch allows computing both in the same
// run, on the same register set, with the same inputs, and
// comparing register by register. That is exactly what --gte-selftest does.
//
// The branch costs one predictable jump per operation. That is the price
// for equality being checkable at all, and it is paid.
s32 doCOP2(s32 op)
{
	const s32 result = g_cfg_gteAlt ? GTE_operatorAlt(op) : GTE_operator(op);

	// The saturation counting, and HERE, because the FLAG register is still
	// intact now: the operator sets it to zero at the start and ORs into it during the
	// computation. So "did this operation clamp" can be answered
	// without changing a character in the GTE core. Off as long as nobody asks.
	//
	// op & 63 is the function number. The operator masks beforehand with
	// 0x1ffffff, which does not touch the lower six bits - the same number, without
	// this module having to know the core's macro.
	if (g_cfg_gteReport)
	{
		NativeGteFlags_Note(C2_FLAG, op & 63);
	}

	return result;
}
