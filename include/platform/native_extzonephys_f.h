#ifndef NATIVE_EXTZONEPHYS_F_H
#define NATIVE_EXTZONEPHYS_F_H

// THE ZONE PORT, PART F: ground probe, driver input latch, wheel-parameter blend,
// node tracking, spring-back along the surface, alignment controller and a boost
// zone of a kart (platform/native_extzonephys_f.c). Conventions and the memory
// form: include/platform/native_extzonephys.h.
// Arguments as the source passes them: K = the kart object (32-bit address in the
// port's memory), floats as Ps2F. All functions return nothing.
#include <platform/native_extzonephys.h>

// Ground probe and reference "up" vector (K+0x410). With ground contact set: the
// vector is normalised. Without: a ray is cast (maglev off: along minus the
// chassis up for the length K+0x50; maglev on: 10 units along minus the chassis
// up) and the hit's normal and surface type are taken. Afterwards the owner's
// effect bits can force surface type 0x16.
void ExtZp_Kart_191bc0(ExtZpCtx *c, uint32_t kart);
// Latches the owner's input block (or the default block) into the kart's
// steering / pedal fields, their edge flags and the "pressed" flags; may flip the
// signs of the two steering values; resets everything when the kart is idle.
void ExtZp_Kart_191f60(ExtZpCtx *c, uint32_t kart);
// Blends four wheel parameters between two stored values by a ratio taken from
// two integers, clamps them with the owner's effect bit 2, multiplies them by
// the surface entry's factor and by the factors of the owner's flags.
void ExtZp_Kart_192220(ExtZpCtx *c, uint32_t kart);
// Node tracking along the two splines of the kart (K+0xA8 and K+0xAC): keeps the
// current node index of each in K+0x468 / K+0x46C.
void ExtZp_Kart_192650(ExtZpCtx *c, uint32_t kart);
// Counts down the timer K+0x550 by dt; clears it on expiry and when the kart is
// in a state in which it must not run.
void ExtZp_Kart_1927a0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// Spring-back along the surface: pushes the body's velocity (body + 0x80) along
// the reference direction when the kart is farther from the surface than allowed.
void ExtZp_Kart_1963d0(ExtZpCtx *c, uint32_t kart, Ps2F dt);
// Alignment controller: turns the body's chassis towards the wanted up vector
// `want` (a vec3 in memory) with a gain from f12 / f13; writes the angular
// velocity of the body. f12 = the step, f13 = the time constant.
void ExtZp_Kart_197510(ExtZpCtx *c, uint32_t kart, uint32_t want, Ps2F f12, Ps2F f13);
// Boost zone of a kart: enters a zone (K+0x564) at the body's position, and while
// inside adds the zone's direction times strength to K+0x3B0.
void ExtZp_Kart_1979d0(ExtZpCtx *c, uint32_t kart);

// ------------------------------------------------------------ needed from other parts
// (signatures derived from the disassembly; the names follow the other parts' headers)
uint32_t ExtZp_Kart_1c1ff0(ExtZpCtx *c, uint32_t obj, uint32_t bit);  // (obj[8] >> bit) & 1 as 0/1   (part C)
uint32_t ExtZp_Kart_1c2010(ExtZpCtx *c, uint32_t obj, uint32_t bit);  // (obj[0xC] >> bit) & 1 as 0/1 (part C)
uint32_t ExtZp_Kart_15d3b0(ExtZpCtx *c, uint32_t obj);                // obj[0x154] != 0 as 0/1
uint32_t ExtZp_Kart_136160(ExtZpCtx *c, uint32_t a0);                 // returns a0
void ExtZp_Glue_190280(ExtZpCtx *c, uint32_t body);                   // part G declares it as well
Ps2F ExtZp_Math_43bc40(ExtZpCtx *c, Ps2F x);                          // acosf (worker E)
// Node search of part D (memory form of the local spline search): spline object, node, point (vec3 in memory);
// returns the nearest segment index (or -1 as uint32).
uint32_t ExtZp_Kart_28b3f0(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t point);
// Zone tests of the phantom list (other part): is the point (vec3 in memory) inside zone `zone`? (0/1)
uint32_t ExtZp_Kart_23cf10(ExtZpCtx *c, uint32_t zone, uint32_t point);
// First zone of the list that holds the point: 0/1, *(u32*)out = the entry's address.
uint32_t ExtZp_Kart_23d110(ExtZpCtx *c, uint32_t list, uint32_t out, uint32_t point);

#endif
