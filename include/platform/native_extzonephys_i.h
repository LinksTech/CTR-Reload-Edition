#ifndef NATIVE_EXTZONEPHYS_I_H
#define NATIVE_EXTZONEPHYS_I_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, PART I: the kart states 4 (air) and 5 / 6 (sliding): enter,
// update, exit (platform/native_extzonephys_i.c). One function per function of
// the source executable; conventions in native_extzonephys.h.
//
// Fields of the kart K used here (offsets in the source's layout):
//   0x2c   pointer to a holder whose first word is the kart's entity E
//   E+0x10 / 0x30 / 0x50   rows of the chassis frame / the track normal frame
//   E+0x94 the body (position +0x30, velocity +0x60, angular +0x70)
//   0x3e0  state number       0x42c  height above the track (m)
//   0x490  landing boost wish 0x498  air time (s)       0x49c  slide time (s)
//   0x4a0 / 0x4a4  slide stage counters   0x4a8 / 0x4ac  slide angle values
//   0x4b0 / 0x4b1  slide flags            0x494 / 0x495  jump lock / no ground below

// State 4 (air) enter: air time 0, jump lock, "no ground" flag cleared. Returns 0.
uint32_t ExtZp_State_192860(ExtZpCtx *c, uint32_t kart);
// State 5 / 6 (slide) enter: returns 2 (state 2 instead) when the slide may not start, else 0.
uint32_t ExtZp_State_192890(ExtZpCtx *c, uint32_t kart);
// State 4 (air) update for one time step dt.
void ExtZp_State_1936b0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// State 5 / 6 (slide) update for one time step dt.
void ExtZp_State_1938d0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// State 4 (air) exit: the landing boost by air time. Returns 1.
uint32_t ExtZp_State_194d20(ExtZpCtx *c, uint32_t kart);
// State 5 / 6 (slide) exit: starts the slide-out turn and a decay. Returns 1.
uint32_t ExtZp_State_194dd0(ExtZpCtx *c, uint32_t kart);
// The air state's steering-free turn and damping (called by 1936b0).
void ExtZp_State_195410(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// Stores the slide-out parameters (f13 -> +0x480, f12 -> +0x484, f14 -> +0x488, a1 -> +0x48c).
void ExtZp_State_1957e0(ExtZpCtx *c, uint32_t kart, uint32_t flag, Ps2F f12, Ps2F f13, Ps2F f14);
// Stores / clears the turn request (+0x4ec .. +0x4fc).
void ExtZp_State_195c80(ExtZpCtx *c, uint32_t kart, uint32_t vec3, uint32_t flag, Ps2F f12);
void ExtZp_State_195cd0(ExtZpCtx *c, uint32_t kart);
// The slide progress: 0 .. 1 over the stage, or the stored value in states 1 and 0xd.
Ps2F ExtZp_State_198900(ExtZpCtx *c, uint32_t kart);

// ---- needed from other parts (signatures derived from the disassembly; as in the other part headers)
void ExtZp_Kart_1911b0(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t a2);
void ExtZp_Kart_196890(ExtZpCtx *c, uint32_t kart, uint32_t stage, Ps2F intensity);
uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Kart_1206d0(ExtZpCtx *c, uint32_t a, uint32_t b);
uint32_t ExtZp_Kart_1207f0(ExtZpCtx *c, uint32_t a);
void ExtZp_State_195900(ExtZpCtx *c, uint32_t kart, uint32_t vec3, Ps2F dt);
void ExtZp_State_195e50(ExtZpCtx *c, uint32_t kart); // (part h: the original does not read its f12)
void ExtZp_State_196280(ExtZpCtx *c, uint32_t kart);
void ExtZp_State_196b40(ExtZpCtx *c, uint32_t kart, Ps2F dt);
void ExtZp_State_197300(ExtZpCtx *c, uint32_t kart, Ps2F dt);
void ExtZp_State_197870(ExtZpCtx *c, uint32_t kart, uint32_t vec); // (part h)
Ps2F ExtZp_State_197d50(ExtZpCtx *c, uint32_t kart, Ps2F f12);
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t body);
void ExtZp_Glue_190280(ExtZpCtx *c, uint32_t body);
Ps2F ExtZp_Math_43b068(ExtZpCtx *c, Ps2F x);
Ps2F ExtZp_Math_43af80(ExtZpCtx *c, Ps2F x);
Ps2F ExtZp_Math_43b320(ExtZpCtx *c, Ps2F x);
Ps2F ExtZp_Math_43bc40(ExtZpCtx *c, Ps2F x);
uint32_t ExtZp_Integ_1206c0(ExtZpCtx *c, uint32_t dst, uint32_t src);
void ExtZp_Integ_1206f0(ExtZpCtx *c, uint32_t dst, uint32_t a, uint32_t b);
void ExtZp_Integ_120750(ExtZpCtx *c, uint32_t dst, uint32_t a, Ps2F s);
Ps2F ExtZp_Integ_120780(ExtZpCtx *c, uint32_t a, uint32_t b);
uint32_t ExtZp_Integ_1207b0(ExtZpCtx *c, uint32_t dst, uint32_t src);
void ExtZp_Integ_1207c0(ExtZpCtx *c, uint32_t dst, uint32_t a, Ps2F s);
void ExtZp_Kart_120720(ExtZpCtx *c, uint32_t d, uint32_t a, uint32_t b);

#endif
