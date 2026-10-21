#ifndef NATIVE_EXTZONEPHYS_G_H
#define NATIVE_EXTZONEPHYS_G_H

// THE ZONE PORT, PART G: the driving state of a kart (states 2 and 3) and its
// friction / grip helpers (platform/native_extzonephys_g.c). Conventions and the
// memory form: include/platform/native_extzonephys.h.
// Arguments as the source passes them: K = the kart object (32-bit address in the
// port's memory), floats as Ps2F.
#include <platform/native_extzonephys.h>

// State 2 / 3 update of a kart (forward / backward driving): pressure and speed
// limit, drive and brake force on the body, state changes, boost, steering, spin
// damping, grip parameters, friction, jump, drag. dt = step in seconds. Returns nothing.
void ExtZp_State_192fc0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// Steering and yaw response of the driving kart; dt = step in seconds.
void ExtZp_State_194fd0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// Spin damping: a force on the body against its speed above a threshold.
void ExtZp_State_195820(ExtZpCtx *c, uint32_t kart);
// Grip / friction of the driving kart: the lateral and longitudinal speed parts
// are brought towards the wanted values (a1 = the wanted vec3 in memory, dt).
void ExtZp_State_195900(ExtZpCtx *c, uint32_t kart, uint32_t vec3, Ps2F dt);
// Friction parameters: *out = the lateral/longitudinal/cross friction triple chosen
// by the speed along the first row, then scaled by the surface factors and the brake input.
void ExtZp_State_195ce0(ExtZpCtx *c, uint32_t kart, uint32_t out, uint32_t fricA, uint32_t fricB);
// Returns f0 = the lower or upper speed limit of the kart (the caller does not use it).
Ps2F ExtZp_State_195e20(ExtZpCtx *c, uint32_t kart);
// Speed limit: pushes the body back when the speed is above the limit.
void ExtZp_State_196280(ExtZpCtx *c, uint32_t kart);

// ------------------------------------------------------------ needed from other parts
// (signatures derived from the disassembly)
// Part H (prefix State):
void ExtZp_State_195e50(ExtZpCtx *c, uint32_t kart);  // pressure / gravity
void ExtZp_State_196b40(ExtZpCtx *c, uint32_t kart, Ps2F dt);  // boost
void ExtZp_State_197300(ExtZpCtx *c, uint32_t kart, Ps2F dt);  // jump
void ExtZp_State_197870(ExtZpCtx *c, uint32_t kart, uint32_t vec); // drag
Ps2F ExtZp_State_197d50(ExtZpCtx *c, uint32_t kart, Ps2F slope); // returns f0 (a slope factor)
// Kart / glue / math helpers (other workers):
uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0);                 // returns a0
uint32_t ExtZp_Kart_1c1ff0(ExtZpCtx *c, uint32_t obj, uint32_t bit);  // (obj[8] >> bit) & 1 as 0/1
void ExtZp_Kart_1911b0(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t a2);
uint32_t ExtZp_Kart_1206d0(ExtZpCtx *c, uint32_t a, uint32_t b);      // a.xyz += b.xyz, returns a
uint32_t ExtZp_Kart_1207f0(ExtZpCtx *c, uint32_t a);                  // a = (0,0,0,1), returns a
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t frame);
void ExtZp_Glue_190280(ExtZpCtx *c, uint32_t frame);
Ps2F ExtZp_Math_43b068(ExtZpCtx *c, Ps2F x);                          // |x|
Ps2F ExtZp_Math_43af80(ExtZpCtx *c, Ps2F x);
Ps2F ExtZp_Math_43b320(ExtZpCtx *c, Ps2F x);
uint32_t ExtZp_Integ_1206c0(ExtZpCtx *c, uint32_t d, uint32_t s);        // 128-bit copy, returns d
void ExtZp_Integ_1206f0(ExtZpCtx *c, uint32_t d, uint32_t a, uint32_t b); // d.xyz = a.xyz + b.xyz (w = a.w)
void ExtZp_Kart_120720(ExtZpCtx *c, uint32_t d, uint32_t a, uint32_t b);    // d = a x b (w = a.w)
void ExtZp_Integ_120750(ExtZpCtx *c, uint32_t d, uint32_t s, Ps2F f);         // d = s.xyz * f (w = s.w)
Ps2F ExtZp_Integ_120780(ExtZpCtx *c, uint32_t a, uint32_t b);                 // dot(a.xyz, b.xyz)
uint32_t ExtZp_Integ_1207b0(ExtZpCtx *c, uint32_t d, uint32_t s);             // 128-bit copy, returns d
void ExtZp_Integ_1207c0(ExtZpCtx *c, uint32_t d, uint32_t s, Ps2F f);         // d = s.xyz * f (w = s.w)

#endif
