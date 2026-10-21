#ifndef NATIVE_EXTZONEPHYS_J_H
#define NATIVE_EXTZONEPHYS_J_H

#include <platform/native_extzonephys.h>

// Part J (platform/native_extzonephys_j.c): the kart's contact callback and its
// two small helpers (effect slot clearing). See the layout comment in the .c file
// for the contact record the callback reads.

// 0x1b1380: sets / clears the "object slot value" of an effect object (ov = the slot object).
void ExtZp_Contact_1b1380(ExtZpCtx *c, uint32_t ov, uint32_t value);
// 0x1c2040: clears effect bit n of an effect set e (with the side effects of some bits).
void ExtZp_Contact_1c2040(ExtZpCtx *c, uint32_t e, uint32_t n, uint32_t flag);
// 0x197e30: the contact callback (vtable +0x2C), called once per contact point.
void ExtZp_Contact_197e30(ExtZpCtx *c, uint32_t kart, uint32_t contact);

// ---- needed from other parts (signatures derived from the disassembly)
// worker C:
uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0);              // returns a0 (identity)
void ExtZp_Kart_1911b0(ExtZpCtx *c, uint32_t kart, uint32_t state, uint32_t a2); // state change filter -> 0x190f60
uint32_t ExtZp_Kart_1c1ff0(ExtZpCtx *c, uint32_t e, uint32_t n);   // 1 when effect bit n of set e is on
// worker A:
void ExtZp_Glue_136050(ExtZpCtx *c, uint32_t body);                // body housekeeping (void)
// (ExtZp_Kart_136110 and ExtZp_Kart_13dd40 are in the part K0 header.)

#endif
