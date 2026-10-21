#ifndef NATIVE_EXTZONEPHYS_S3_H
#define NATIVE_EXTZONEPHYS_S3_H

// THE ZONE PORT, PART S3: the contact solver frame, island building (part 2) and the
// contact list copy. Conventions and the memory form: include/platform/native_extzonephys.h.
// Objects are 32-bit addresses in the port's memory, as the source passes them.
// A "word vector" is {capacity +0, size +4, data +8}; the contact vector has the same
// head with elements of 0x80 bytes (virtual destructor in slot +8 of the element's table).
#include <platform/native_extzonephys.h>

// ------------------------------------------------------------------ H-KONTAKTLOESER (prefix Solver)
// Returns a0 (empty hook: the original does nothing).
uint32_t ExtZp_Solver_3a9920(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_3a9930(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_345660(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_37b400(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Solver_37b430(ExtZpCtx *c, uint32_t a0);
// Returns *(a0 + 8) + a1 * 4: the address of word a1 of a vector's data.
uint32_t ExtZp_Solver_348e00(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Solver_3af420(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Clears a small vector head: capacity, size and data = 0; returns a0 (a1 unused).
uint32_t ExtZp_Solver_345640(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Swaps the words at a0 and a1.
void ExtZp_Solver_37b410(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Copies a contact's data block (4 x 16 bytes, a byte at +0x40, words +0x44..+0x5C); returns a0.
uint32_t ExtZp_Solver_35bfa0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Solver_3723e0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Frees a vector's data when it has some and, when (int16)a1 > 0, the object itself; returns a0.
uint32_t ExtZp_Solver_3455e0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Releases a tree node (a1 = flags); returns a0.
uint32_t ExtZp_Solver_3a9040(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Releases an island solver object; returns a0.
uint32_t ExtZp_Solver_3a90a0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Solver_3af250(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Recursive release of a tree (a0 = owner, a1 = node).
void ExtZp_Solver_3a9760(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Contact destructor (virtual slot +8 of a contact's table): not in the assignment, needed by 13e0b0 / 37ae50.
uint32_t ExtZp_Solver_13e130(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Destroys all elements of a contact vector from the back, size = 0.
void ExtZp_Solver_13e0b0(ExtZpCtx *c, uint32_t a0);
// vector<contact>::insert(pos = a1, n = a2 copies of the contact at a3), growth by doubling.
void ExtZp_Solver_37ae50(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
// Island building, the union-find: find with path compression of entry a1.
uint32_t ExtZp_Solver_3af2f0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Merges two labels a1, a2 (the smaller wins).
void ExtZp_Solver_3af6d0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
// Ensures a word vector's capacity a1 (copy into a new block).
void ExtZp_Solver_34a640(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Collects the first not yet taken island's entries into the vector a1; returns 1 or 0.
uint32_t ExtZp_Solver_3af730(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Flattens the label chains: every non-negative entry of the table (+4 entries at +8) and of the list (+0x10 entries at +0x14) becomes its root (the entry whose table value is negative).
void ExtZp_Solver_3af430(ExtZpCtx *c, uint32_t a0);
void ExtZp_Solver_3af4f0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
// Builds the contacts of one island into the solver's vector a1; returns 1 or 0.
uint32_t ExtZp_Solver_3a9120(ExtZpCtx *c, uint32_t a0, uint32_t a1);

// ------------------------------------------------------------ needed from other parts
// Part b2 (prefix Integ): native_extzonephys_b2.h : 33c120, 33c170, 33f800.
// Part b1: native_extzonephys_b1.h : 35bc90.
#include <platform/native_extzonephys_b1.h>
#include <platform/native_extzonephys_b2.h>

#endif
