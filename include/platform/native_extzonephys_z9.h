#ifndef NATIVE_EXTZONEPHYS_Z9_H
#define NATIVE_EXTZONEPHYS_Z9_H

// THE ZONE PORT, PART Z9: the kart reset state (state 9): its entry handler, its update, its
// exit test, and the removal of the kart's body from the rigid body system (Integ helpers).
// Conventions and the memory form: include/platform/native_extzonephys.h.
#include <platform/native_extzonephys.h>

// Kart reset state, entry (state table slot "enter"): removes the kart's body from the system
// (337480), clears the body's velocity and angular velocity (vector at 0x006A2160), cancels a
// boost and zeroes the state timer (kart + 0x500). Returns 0 (v0).
uint32_t ExtZp_State_192c80(ExtZpCtx *c, uint32_t kart);
// Kart reset state, update (a0 = kart, f12 = dt). Void (v0 undefined in the original). Clears
// the angular velocity; with the flag byte kart + 0x505 set also the velocity, else downforce
// (195e50) while the timer kart + 0x500 is above kart + 0x308 (kart + 0x460 is swapped for
// kart + 0x30C meanwhile). Adds dt to the timer; when it passes kart + 0x304 the respawn runs
// (337690 and 190540: queries EXT_ZP_Q_OTHER).
void ExtZp_State_194720(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// Kart reset state, exit test: returns 1 when the timer (kart + 0x500) is above kart + 0x304
// (also when unordered), else 0.
uint32_t ExtZp_State_194f70(ExtZpCtx *c, uint32_t kart);

// 0x337480 (tail jump): world a0, entity a1 -> 380ef0(*(*(a0 + 0x64) + 0x14), *(a1 + 0x94)). Void.
void ExtZp_Integ_337480(ExtZpCtx *c, uint32_t world, uint32_t entity);
// 0x380ef0: for every entry i of the body's list (count at body + 0x28C, vector head at body + 0x288)
// calls 36edc0(system + 0x14 as passed in a0, entry). Void.
void ExtZp_Integ_380ef0(ExtZpCtx *c, uint32_t system, uint32_t body);

// ------------------------------------------------------------ needed from other parts
// Worker z9b (native_extzonephys_z9b.h): 0x36edc0 removes a body from the system (a0 = *(system + 0x14),
// a1 = the body). Void (the original leaves v0 undefined).
void ExtZp_Integ_36edc0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Other parts (h, a/c, k0): 195e50, 1972a0, 136160 (returns a0), 136050, 190280, 13dd40, 190350.

#endif
