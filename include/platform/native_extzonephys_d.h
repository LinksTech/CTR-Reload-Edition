#ifndef NATIVE_EXTZONEPHYS_D_H
#define NATIVE_EXTZONEPHYS_D_H

#include <platform/native_extzonephys.h>

// Part D of the zone port (platform/native_extzonephys_d.c): the per-frame zone
// test of the kart, the gravity spline choice and the boost stage. Memory form,
// conventions as in native_extzonephys.h.

// ---- box / list / spline tests on the objects' memory
// A box object: axis k at +0x04 + 0xC * k (three floats), range k at +0x28 + 8 * k
// (lowest, highest). a0 = box, a1 = point (three floats). Returns 1 when the point
// lies inside all three ranges, else 0. No memory is written.
uint32_t ExtZp_Kart_23cf10(ExtZpCtx *c, uint32_t box, uint32_t p);
// A list of boxes: list + 4 = first entry; an entry is {box, next}. a0 = list,
// a1 = out (may be 0), a2 = point. Returns 1 for the first entry whose box holds
// the point and then writes out[0] = entry, out[1] = 0 (when out != 0); else 0.
uint32_t ExtZp_Kart_23d110(ExtZpCtx *c, uint32_t list, uint32_t out, uint32_t p);
// A spline object: +0x08 kind (0 plane, 1 point, 2 inverse point), +0x10 -> list
// {+0x08 node count, +0x10 -> nodes of 24 bytes: position xyz, direction xyz}.
// Local search: the segment nearest to the point p (a2) among the segments
// node - 1 .. node + 1 (a1 = node); returns its index or -1 (as int32 in v0).
uint32_t ExtZp_Kart_28b3f0(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t p);
// Global search: the nearest node to the point p (a1) over the whole spline, then
// the local search from it. Returns the segment index or -1.
uint32_t ExtZp_Kart_28b320(ExtZpCtx *c, uint32_t spline, uint32_t p);
// The direction of the spline's force at p (a2) on segment a1: writes three floats
// to out (a3).
void ExtZp_Kart_28b600(ExtZpCtx *c, uint32_t spline, uint32_t node, uint32_t p, uint32_t out);
// Cross product, out (a0) = a (a1) x b (a2), w of out = w of a (16-byte vectors).
void ExtZp_Kart_120720(ExtZpCtx *c, uint32_t out, uint32_t a, uint32_t b);

// ---- kart helpers
// Boost start: f12 = intensity, a0 = kart, a1 = stage (table at kart + 0x1E4, 12 bytes
// per stage: speed, time, wheelie). Stage 7 state (kart + 0x3E0 == 7): no effect.
void ExtZp_Kart_196890(ExtZpCtx *c, uint32_t kart, uint32_t stage, Ps2F intensity);
// Takes the stage's table values into the boost fields (no intensity scaling).
void ExtZp_Kart_196aa0(ExtZpCtx *c, uint32_t kart, uint32_t stage);
// Spline choice: a1 = index of the spline in the kart's list (kart + 0xA8) or -1
// to clear; stores the index at kart + 0x464 and the nearest segment of that
// spline (and of the second list at kart + 0xAC) at +0x468 / +0x46C.
void ExtZp_Kart_198bf0(ExtZpCtx *c, uint32_t kart, uint32_t index);

// ---- the zone test per frame
// The kart's zone test: the lists of the zone table (global at 0x6F3B20) tested against
// the body's position: portal boxes (portal query), surface-type boost zones, maglev on/off
// lists, gravity (grav) and magnetic gravity volumes with the spline choice, driver zone
// flags and events, and a wall-like box that pushes the body out (rewrites the body's
// position and velocity). a0 = kart. Does nothing when the kart has no driver (+0x38 == 0).
void ExtZp_Kart_1911f0(ExtZpCtx *c, uint32_t kart);
// 0x396f00 (not assigned to another part): refreshes a body's rotation rows from its
// quaternion: H + 0xE0 = matrix from the quaternion (H + 0x40), H + 0x110 = position
// (H + 0x30), H + 0x120.. = rows (H + 0xE0) * (H + 0xB0), counter H + 0x294 += 1.
void ExtZp_Integ_396f00(ExtZpCtx *c, uint32_t body);

// Other parts used (declared in their headers, included by the .c): 1a37b0, 1206d0, 1207f0,
// 136160, 512ee0, 1911b0 (part C), 120750.. 1207c0, 1206f0, 3f6780, 3f6b20 (part B2), 399ca0
// (B1), 136050 (A), cos/sin 43af80 / 43b320 (E).

#endif
