#ifndef NATIVE_EXTZONEPHYS_Z9B_H
#define NATIVE_EXTZONEPHYS_Z9B_H

// THE ZONE PORT, PART Z9B: removing a rigid body from the physics system (the cleanup the world
// runs when a body leaves it). Conventions and the memory form: include/platform/native_extzonephys.h.
// Objects are 32-bit addresses in the port's memory, as the source passes them.
#include <platform/native_extzonephys.h>

// ------------------------------------------------------------------ H-INTEGRATION (prefix Integ)
// 0x36edc0: a0 = the physics system, a1 = the body to remove. Takes the body out of every list of the
// system (body lists, pair lists, contact records, joint records) and the system out of the body's own
// list. Returns nothing (v0 is left over from the last helper call).
// The helper vectors (0x35f070, 0x37ab50, 0x37b440, 0x37ad60, 0x11c620, 0x36f5a0) are static in the part file.
void ExtZp_Integ_36edc0(ExtZpCtx *c, uint32_t a0, uint32_t a1);

// ------------------------------------------------------------ needed from other parts
// Part s3 (prefix Solver): native_extzonephys_s3.h : 3723e0 (copy of a contact's data block), 13e130 (contact destructor).
#include <platform/native_extzonephys_s3.h>

#endif
