#ifndef NATIVE_EXTZONEPHYS_S2_H
#define NATIVE_EXTZONEPHYS_S2_H

#include <platform/native_extzonephys.h>

// THE ZONE PORT, PART S2 (platform/native_extzonephys_s2.c): the contact solver
// frame (numbers the 0x80-byte contacts, builds the islands, calls the resolver
// once per island) and the island builder's container helpers (a small ordered
// map with red-black balancing, a pair vector, iterators).
// Conventions as in native_extzonephys.h. Addresses are 32-bit addresses in the
// port's memory.

// ---- the frame
void ExtZp_Solver_3b1960(ExtZpCtx *c, uint32_t sub);                                   // subspace hook: resolver (+0x7C) with the detector's contact list
uint32_t ExtZp_Solver_381000(ExtZpCtx *c, uint32_t holder);                            // contact list = *(holder + 0x14) + 0xF8
void ExtZp_Solver_3ac5a0(ExtZpCtx *c, uint32_t res, uint32_t list, Ps2F h);            // numbers the contacts, runs the resolver (flat or per island)
uint32_t ExtZp_Solver_3a8da0(ExtZpCtx *c, uint32_t b, uint32_t list);                  // island builder: body ids, edge list, union-find; returns b

// ---- map (body pointer -> id), red-black tree
uint32_t ExtZp_Solver_3a8d80(ExtZpCtx *c, uint32_t b);                                 // b + 4
uint32_t ExtZp_Solver_3a8d90(ExtZpCtx *c, uint32_t m);                                 // m + 0xC
uint32_t ExtZp_Solver_3a9030(ExtZpCtx *c, uint32_t it, uint32_t node);                 // *it = node; returns it
uint32_t ExtZp_Solver_3a9940(ExtZpCtx *c, uint32_t m, uint32_t p);                     // empty map; returns m
uint32_t ExtZp_Solver_3a9210(ExtZpCtx *c, uint32_t m, uint32_t keyp);                  // find or insert; returns the key field address of the node
uint32_t ExtZp_Solver_3a99a0(ExtZpCtx *c, uint32_t m, uint32_t parent, uint32_t left, uint32_t leftmost, uint32_t pair); // insert a node; returns it
void ExtZp_Solver_340bf0(ExtZpCtx *c, uint32_t x, uint32_t root);                      // balancing after an insert
void ExtZp_Solver_340e30(ExtZpCtx *c, uint32_t x, uint32_t rootp);                     // rotate left
void ExtZp_Solver_340ed0(ExtZpCtx *c, uint32_t x, uint32_t rootp);                     // rotate right
void ExtZp_Solver_33ffd0(ExtZpCtx *c, uint32_t itp);                                   // iterator to the in-order successor

// ---- pair vector {cap +0, count +4, data +8}, element 8 bytes
uint32_t ExtZp_Solver_3a9010(ExtZpCtx *c, uint32_t v);                                 // empty; returns v
void ExtZp_Solver_3a92c0(ExtZpCtx *c, uint32_t v, uint32_t pos, uint32_t n, uint32_t src); // insert n copies of *src before pos
void ExtZp_Solver_3a96c0(ExtZpCtx *c, uint32_t p, uint32_t q);                         // swap the words at p and q
uint32_t ExtZp_Solver_3a96e0(ExtZpCtx *c, uint32_t p, uint32_t q);                     // returns p
void ExtZp_Solver_3a9960(ExtZpCtx *c, uint32_t v);                                     // clear (count = 0)
uint32_t ExtZp_Solver_3a96f0(ExtZpCtx *c, uint32_t v, uint32_t flag);                  // destroy: clear, free the data (flag > 0: also the object); returns v

// ---- union-find object {vec0 +0, vec1 +0xC}
uint32_t ExtZp_Solver_3af1f0(ExtZpCtx *c, uint32_t u);                                 // two empty vectors; returns u
uint32_t ExtZp_Solver_345640(ExtZpCtx *c, uint32_t v, uint32_t unused);                // {0,0,0}; returns v (called by 3af1f0; added by s2)
uint32_t ExtZp_Solver_345660(ExtZpCtx *c, uint32_t p);                                 // returns p (allocator constructor; called by 3af1f0; added by s2)

// ---- needed from other parts
uint32_t ExtZp_Solver_3a90a0(ExtZpCtx *c, uint32_t b, uint32_t flag);                  // builder clean-up (worker S3)
uint32_t ExtZp_Solver_3a9120(ExtZpCtx *c, uint32_t b, uint32_t v);                     // next island into v (worker S3)
void ExtZp_Solver_3af4f0(ExtZpCtx *c, uint32_t u, uint32_t e, uint32_t n);             // union-find over the edges (worker S3)
void ExtZp_Solver_3ab780(ExtZpCtx *c, uint32_t res, uint32_t v);                       // resolver type 1, slot +0x1C (worker S1)

#endif
