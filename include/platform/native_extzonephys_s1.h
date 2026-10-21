#ifndef NATIVE_EXTZONEPHYS_S1_H
#define NATIVE_EXTZONEPHYS_S1_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, PART S1 (platform/native_extzonephys_s1.c): the runtime contact
// solver "type 1" (core): point velocity of a body, the contact search, the
// impulse application, the iteration loop and the two resolver entries.
// Memory form, conventions as in native_extzonephys.h. A contact record is 0x80
// bytes: +0x08 / +0x0C shape of body 1 / 2 (shape + 0x18 -> body), +0x10 normal,
// +0x20 contact point, +0x54 penetration depth.

// out (a0) = velocity of the point p (a2) on body H (a1): v + w x (p - pos); returns out.
uint32_t ExtZp_Solver_396fc0(ExtZpCtx *c, uint32_t out, uint32_t H, uint32_t p);
// Round-robin search (cursor at obj + 4, count at obj + 8) for the next contact whose relative
// normal velocity (obj + 0x20) is below thr (f12). Returns 1 on a hit.
uint32_t ExtZp_Solver_3ac1d0(ExtZpCtx *c, uint32_t obj, uint32_t list, uint32_t pc, uint32_t prel, uint32_t pidx, Ps2F thr);
// Applies the impulse vector at a0 to the two bodies of contact a1 (velocity and angular velocity).
void ExtZp_Solver_3ac800(ExtZpCtx *c, uint32_t imp, uint32_t contact);
// Position correction of a contact (a0) for a penetration scale f12.
void ExtZp_Solver_3abdc0(ExtZpCtx *c, uint32_t contact, Ps2F scale);
// Iteration loop of the solver (a0) over the contact list (a1).
void ExtZp_Solver_3ac0c0(ExtZpCtx *c, uint32_t solver, uint32_t list);
// Resolver +0x20: impulse vector (out = a0) of contact a2 for solver a1; a3 = relative velocity record.
uint32_t ExtZp_Solver_3ab9d0(ExtZpCtx *c, uint32_t out, uint32_t solver, uint32_t contact, uint32_t rel);
// Resolver +0x1C: prepares the contacts, then runs the loop.
void ExtZp_Solver_3ab780(ExtZpCtx *c, uint32_t solver, uint32_t list);

// Needed from other parts (headers: native_extzonephys_b1.h / _b2.h / _d.h):
//   ExtZp_Kart_120720 (d), ExtZp_Integ_35bc90 (b1), ExtZp_Integ_35c3f0, _34bb20, _120750, _120780 (b2).

#endif
