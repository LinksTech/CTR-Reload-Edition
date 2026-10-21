#ifndef NATIVE_EXTZONEPHYS_A_H
#define NATIVE_EXTZONEPHYS_A_H

// THE ZONE PORT, PART A: the kart's physics glue (the action the physics world calls
// for the kart) and the physics world's step (substep count, time keeping, substep
// loop). Conventions and the memory form: include/platform/native_extzonephys.h.
// Arguments as the source passes them: objects are 32-bit addresses in the port's
// memory, floats are Ps2F. A "time" is a pair (whole seconds as int32, fraction as
// float); where the source passes such a pair in ONE 64-bit register, the port takes
// a single uint64_t (low word = whole seconds, high word = fraction bits).
#include <platform/native_extzonephys.h>

// ------------------------------------------------------------------ K-GLUE (prefix Glue)
// Refreshes a body shell's rows from its rigid body: the shell's three direction rows
// (+0x10 up-ish, +0x20, +0x30) are the fixed unit vectors turned by the body's 3x3
// matrix at body + 0xE0.
void ExtZp_Glue_190370(ExtZpCtx *c, uint32_t shell);
// Copies the body's velocity (body + 0x60) into the shell (+0x40) and the unit
// direction of it into shell + 0x50; shell + 0x60 = the speed; below a tiny speed the
// direction is the shell's row at +0x10.
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t shell);
// The same for the angular velocity (body + 0x70 -> shell + 0x70, unit direction at
// shell + 0x80, magnitude at shell + 0x90; below a tiny magnitude the direction is
// (0, 0, 0, 1)).
void ExtZp_Glue_190280(ExtZpCtx *c, uint32_t shell);
// The kart action's apply (slot +0x1C): refreshes every body shell of the kart,
// calls the kart update (slot +0x20) with dt = time - last time, and, if the kart's
// byte +0x28 is set, adds -gravity * mass to every body's force (cancels the world's
// gravity). a0 = kart, a1 = the world's subspace (not read), a2 = the time pair.
void ExtZp_Glue_33afa0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint64_t a2);

// ------------------------------------------------------------------ the world step (prefix Step)
// World step: dt (seconds) is capped at world + 0x5C, the substep count
// n = clamp(int(dt * world + 0x58 + 0.5), 1, world + 0x60), then the environment's
// step. a0 = world, f12 = dt.
void ExtZp_Step_336b60(ExtZpCtx *c, uint32_t a0, Ps2F f12);
// Returns a0 + 8 (the data pointer slot of a small array object).
uint32_t ExtZp_Step_345a10(ExtZpCtx *c, uint32_t a0);
// Returns the float at (*(a0 + 0x14)) + 0x14.
Ps2F ExtZp_Step_380f80(ExtZpCtx *c, uint32_t a0);
// Sets the body's time pair (a1: low word seconds, high word fraction bits) at +0x5C/+0x60 and
// stores it into the entry of its table (table at +0x40, index at +0x68, entries of 8 bytes).
void ExtZp_Step_38eaf0(ExtZpCtx *c, uint32_t a0, uint64_t a1);
// One world step of a subspace: a0 = subspace, a1 = the target time pair, a2 = substep count.
void ExtZp_Step_3b1750(ExtZpCtx *c, uint32_t a0, uint64_t a1, uint32_t a2);
// Subspace substep up to the target time (slot +0x30). a0 = result buffer (8 bytes:
// the subspace time pair afterwards), a1 = subspace, a2 = the target time pair.
void ExtZp_Step_3b1b20(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint64_t a2);
// Run-time substep of the subspace subclass (slot +0x30 of vtable 0x6DC1D0); same arguments as 3b1b20.
// Without a collision detector it calls 3b1b20; otherwise one pass up to the target time (no halving).
void ExtZp_Step_3af920(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint64_t a2);
// Time pair (a0: seconds at +0, fraction at +4) += f12, fraction brought into (-1, 1). Returns a0.
uint32_t ExtZp_Step_3b21e0(ExtZpCtx *c, uint32_t a0, Ps2F f12);
// Calls slot +0x20 of every listener of the subspace with the subspace time.
void ExtZp_Step_3b2280(ExtZpCtx *c, uint32_t a0);
// Collision detector tolerance of the subspace (slot +0x20): returns it (0 without detector).
Ps2F ExtZp_Step_3b2600(ExtZpCtx *c, uint32_t a0);
// Sets the collision detector tolerance of the subspace (slot +0x24) to f12.
void ExtZp_Step_3b2640(ExtZpCtx *c, uint32_t a0, Ps2F f12);
// Environment step: time pair at +0x20/+0x24 += f12 (fraction normalised), then every
// subspace of the list (+0x18 count, +0x1C table) steps with a1 substeps.
void ExtZp_Step_3b2db0(ExtZpCtx *c, uint32_t a0, uint32_t a1, Ps2F f12);
// ceilf / floorf of the executable's math library (this part uses both).
Ps2F ExtZp_Step_43ae78(ExtZpCtx *c, Ps2F f12);
Ps2F ExtZp_Step_43b0b0(ExtZpCtx *c, Ps2F f12);

// ------------------------------------------------------------ needed from other parts
// (signatures derived from the disassembly)
// Worker C (prefix Kart): the kart update, a0 = kart, f12 = dt (the register of the kart is a0).
void ExtZp_Kart_190670(ExtZpCtx *c, uint32_t a0, Ps2F f12);
// Worker B1 (prefix Integ), called from the substep loop through the system's table:
void ExtZp_Integ_39adb0(ExtZpCtx *c, uint32_t a0, uint64_t a1); // slot +0x3C, a1 = time pair
void ExtZp_Integ_39ad50(ExtZpCtx *c, uint32_t a0);                             // slot +0x40
void ExtZp_Integ_38e3e0(ExtZpCtx *c, uint32_t a0);                             // slot +0x38
void ExtZp_Integ_38e7c0(ExtZpCtx *c, uint32_t a0, uint64_t a1); // slot +0x4C, a1 = time pair

#endif
