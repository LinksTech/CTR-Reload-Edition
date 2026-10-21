#ifndef NATIVE_EXTZONEPHYS_H_PART_H
#define NATIVE_EXTZONEPHYS_H_PART_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, PART H (platform/native_extzonephys_h.c): downforce and gravity of the kart, the
// drive sub function (boost timers, slide turn, boost-in-a-row), the jump and two small helpers
// of the kart state. See native_extzonephys.h for the conventions (memory form, Ps2F, ctx).

// 0x195e50 downforce / gravity: adds the downforce (maglev: along the contact normal sum or the
// gravity spline direction; otherwise gravity of the world) to the body's force accumulator.
void ExtZp_State_195e50(ExtZpCtx *c, uint32_t kart);
// 0x196b40 drive sub function (f12 = step time): boost-in-a-row timer, boost end, slide turn of
// the velocity, speed target and the drive force added to the body's force accumulator.
void ExtZp_State_196b40(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// 0x1972a0 cancels a boost: remaining time -1, in-a-row timer 2 * tolerance, flag 0, driver
// callback (virtual, left) with 0, counter 0.
void ExtZp_State_1972a0(ExtZpCtx *c, uint32_t kart);
// 0x197300 jump (f12 = step time): counts the jump timer down; on a pressed jump gives the
// body an upward impulse and refreshes the kart frame's velocity copy.
void ExtZp_State_197300(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// 0x197870 adds the vector at `vec`, projected into the chassis plane, normalised and scaled
// (by a speed factor between two parameters), to the kart's force vector (kart + 0x3b0);
// only when a level flag (table of bytes at 0x68b650) is set.
void ExtZp_State_197870(ExtZpCtx *c, uint32_t kart, uint32_t vec);
// 0x197d50 returns a factor (>= 1.0) for an angle-like input f12 (1.0 in maglev, state 4 or
// f12 <= 0), from acos(1 - f12) mapped through the kart's parameters 0xb0/0xb4/0xb8.
Ps2F ExtZp_State_197d50(ExtZpCtx *c, uint32_t kart, Ps2F f12);

// ------------------------------------------------------------------ needed from other parts
// (signatures derived from the disassembly; return = v0 unless noted)
uint32_t ExtZp_Kart_512ee0(ExtZpCtx *c, uint32_t obj, uint32_t cls);          // isKindOf(obj, cls)
uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0);                         // returns a0
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t frame);                          // refreshes frame+0x40/0x50/0x60
uint32_t ExtZp_Kart_1c1ff0(ExtZpCtx *c, uint32_t obj, uint32_t bit);          // (obj+8 >> bit) & 1
uint32_t ExtZp_Kart_15d3b0(ExtZpCtx *c, uint32_t obj);                        // obj+0x154 != 0
void ExtZp_Kart_196890(ExtZpCtx *c, uint32_t kart, uint32_t stage, Ps2F f12); // boost start
Ps2F ExtZp_Math_43af80(ExtZpCtx *c, Ps2F f12);                                // cosf
Ps2F ExtZp_Math_43b320(ExtZpCtx *c, Ps2F f12);                                // sinf
Ps2F ExtZp_Math_43bc40(ExtZpCtx *c, Ps2F f12);                                // acosf
uint32_t ExtZp_Kart_1207f0(ExtZpCtx *c, uint32_t v);                          // v = (0, 0, 0, 1)
uint32_t ExtZp_Integ_1207b0(ExtZpCtx *c, uint32_t d, uint32_t s);             // d = s (128 bit)
uint32_t ExtZp_Integ_1206c0(ExtZpCtx *c, uint32_t d, uint32_t s);             // d = s (128 bit)
uint32_t ExtZp_Kart_1206d0(ExtZpCtx *c, uint32_t d, uint32_t s);              // d.xyz += s.xyz
void ExtZp_Integ_1206f0(ExtZpCtx *c, uint32_t d, uint32_t a, uint32_t b);     // d = a + b (xyz)
void ExtZp_Integ_1207c0(ExtZpCtx *c, uint32_t d, uint32_t s, Ps2F f12);       // d = s * f12 (xyz)
void ExtZp_Integ_120750(ExtZpCtx *c, uint32_t d, uint32_t s, Ps2F f12);       // d = s * f12 (xyz)
Ps2F ExtZp_Integ_120780(ExtZpCtx *c, uint32_t a, uint32_t b);                 // returns f0 = a . b
void ExtZp_Kart_120720(ExtZpCtx *c, uint32_t d, uint32_t a, uint32_t b);    // d = a x b
void ExtZp_Kart_28b600(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t p, uint32_t out); // spline direction
// ExtZp_Kart_136110 and ExtZp_Kart_13dd40 are declared in native_extzonephys.h.

#endif
