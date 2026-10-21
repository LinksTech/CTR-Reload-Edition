#ifndef NATIVE_EXTZONEPHYS_C_H
#define NATIVE_EXTZONEPHYS_C_H

#include <platform/native_extzonephys.h>

// Part C of the zone port (platform/native_extzonephys_c.c): the kart update
// frame and its small helpers. Memory form, conventions as in
// native_extzonephys.h. Fields of the kart object K used here:
//   +0x2C -> list of body entries (first entry E; E + 0x94 = body H)
//   +0x38 / +0x3C: flags that let the update run     +0x3B0 force accumulator, +0x3C0 force
//   +0x3D0 last velocity    +0x3E0 state number    +0x402 contact flag    +0x410 up vector
//   +0x428 maglev byte      +0x42C speed           +0x434 / +0x454 / +0x45C timers
//   +0x464 / +0x46C spline node numbers    +0x528 air time    +0x54C standstill time

// ---- small helpers
// a.xyz += b.xyz (vec4s); returns a.
uint32_t ExtZp_Kart_1206d0(ExtZpCtx *c, uint32_t a, uint32_t b);
// v = (0, 0, 0, 1); returns v.
uint32_t ExtZp_Kart_1207f0(ExtZpCtx *c, uint32_t v);
// Identity: returns its argument.
uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0);
// 1 when the word at obj + 0x154 is not zero.
uint32_t ExtZp_Kart_15d3b0(ExtZpCtx *c, uint32_t obj);
// Byte at obj + 0x1c + i for i < 4, else 1.
uint32_t ExtZp_Kart_1a37b0(ExtZpCtx *c, uint32_t obj, uint32_t i);
// Bit n of the word at obj + 8 / obj + 0xC (as 0 or 1).
uint32_t ExtZp_Kart_1c1ff0(ExtZpCtx *c, uint32_t obj, uint32_t n);
uint32_t ExtZp_Kart_1c2010(ExtZpCtx *c, uint32_t obj, uint32_t n);
// Type test: asks the object's type (a virtual call, answered by the query
// EXT_ZP_Q_OTHER with func = the method found in the object's table) and walks
// the parent chain (+0x38) for cls. Returns 1 / 0.
uint32_t ExtZp_Kart_512ee0(ExtZpCtx *c, uint32_t obj, uint32_t cls);

// ---- state change and the state table
// Method pointer {adjust, index, target} at address mp inside the state table
// (0x6F53F0 + 36 * state + 0 enter / 0xC update / 0x18 exit): 4ba960 = slot holds
// something. The table is the original's start-up data; the port knows it as a
// switch (states 1..14, empty slot = 0).
uint32_t ExtZp_Kart_4ba960(ExtZpCtx *c, uint32_t mp);
// 4ba920: calls the slot's handler with this = a0 (+ adjust 0), a1, a2 (enter /
// exit handlers) or f12 (update handlers); returns the handler's v0 (0 for update
// handlers, which return nothing).
uint32_t ExtZp_Kart_4ba920(ExtZpCtx *c, uint32_t mp, uint32_t a0, uint32_t a1, uint32_t a2, Ps2F f12);
// State change: K+0x3E0 = new, exit handler of the old state (zero result takes
// the change back), enter handler of the new one (non-zero result = redirect to that state).
void ExtZp_Kart_190f60(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t arg);
// Filter: changes the state when K+0x38 is set or 2 <= state < 7.
void ExtZp_Kart_1911b0(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t arg);

// ---- the kart update per controller call: dt in seconds.
void ExtZp_Kart_190670(ExtZpCtx *c, uint32_t kart, Ps2F dt);

// ---- needed from other parts (signatures from the disassembly)
// zones (worker D)
void ExtZp_Kart_1911f0(ExtZpCtx *c, uint32_t kart);
// ground probe and the steps after it (worker F)
void ExtZp_Kart_191bc0(ExtZpCtx *c, uint32_t kart);
void ExtZp_Kart_191f60(ExtZpCtx *c, uint32_t kart);
void ExtZp_Kart_192220(ExtZpCtx *c, uint32_t kart);
void ExtZp_Kart_192650(ExtZpCtx *c, uint32_t kart);
void ExtZp_Kart_1927a0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
void ExtZp_Kart_197510(ExtZpCtx *c, uint32_t kart, uint32_t up, Ps2F dt, Ps2F rate); // alignment to the up vector at `up`
void ExtZp_Kart_1979d0(ExtZpCtx *c, uint32_t kart);
void ExtZp_Kart_1963d0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// spline direction (worker D)
void ExtZp_Kart_28b600(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t p, uint32_t out);
// body glue (worker A)
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t body);
void ExtZp_Glue_190280(ExtZpCtx *c, uint32_t body);
// (ExtZp_Kart_136110, 13dd40, 190350 are in native_extzonephys.h.)
// state handlers of the anti-gravity path (workers G/H/I; states 2..6); the other states' handlers are left (ExtZp_Leave)
uint32_t ExtZp_State_192860(ExtZpCtx *c, uint32_t kart);
uint32_t ExtZp_State_192890(ExtZpCtx *c, uint32_t kart);
uint32_t ExtZp_State_194d20(ExtZpCtx *c, uint32_t kart);
uint32_t ExtZp_State_194dd0(ExtZpCtx *c, uint32_t kart);
void ExtZp_State_192fc0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
void ExtZp_State_1936b0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
void ExtZp_State_1938d0(ExtZpCtx *c, uint32_t kart, Ps2F dt);

#endif
