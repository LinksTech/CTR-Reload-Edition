#ifndef NATIVE_EXTZONECONTACT_H
#define NATIVE_EXTZONECONTACT_H

// THE ZONE PORT'S CONTACTS (platform/native_extzonecontact.c).
//
// The source physics finds its contacts with a collision detector the port does
// not copy (a large mesh tree search). This part stands in for it: it answers
// the port's collision queries (EXT_ZP_Q_COLLIDE) from Reload's own collision
// geometry and writes the results into the port's memory in the source's own
// form, so that the ported contact solver, the ported penetration handling and
// the kart's ported contact callback read them exactly as the original reads
// its detector's output. It also holds the port of the penetration handling
// (the time search that sets bodies back to the last moment without
// penetration), prefix Pen.
//
// What is measured on the original (world dumps of a running race, three
// frames): every contact is one (sphere, triangle) pair whose gap between the
// sphere's surface and the triangle is at most the detector's tolerance
// (0.1 m); the record holds the triangle's normal (pointing from the triangle
// to the sphere's body), the point on the sphere's surface (witness A), the
// point on the triangle (witness B), their midpoint (the contact point) and the
// gap; several triangles give several records for the same body. The kart's
// shape is one sphere around the body's position (radius 0.52 m).
//
// Standalone C (no game header) except the Reload geometry provider at the
// end, so that the offline difference test links the same file.

#include <platform/native_extzonephys.h>

// ------------------------------------------------------------------ the source's layout

// A contact record (0x80 bytes; detector list P+0xF8, solver copies).
enum
{
	EXT_ZC_REC_SIZE = 0x80,
	EXT_ZC_REC_VTABLE = 0x00,     // u32: the record class (the value the original stores, read from the original's first record)
	EXT_ZC_REC_SHELL_A = 0x08,    // u32: the shell of body A (the dynamic body; body = *(shell + 0x18))
	EXT_ZC_REC_SHELL_B = 0x0C,    // u32: the shell of body B (the track)
	EXT_ZC_REC_NORMAL = 0x10,     // vec4: unit normal from B towards A, w = 1
	EXT_ZC_REC_POINT = 0x20,      // vec4: contact point = midpoint of the witnesses, w = 1
	EXT_ZC_REC_WITNESS_A = 0x30,  // vec4: the point on A's sphere (centre - normal * radius), w = 1
	EXT_ZC_REC_WITNESS_B = 0x40,  // vec4: the point on the triangle, w = 1
	EXT_ZC_REC_FLAG = 0x50,       // u8: copied by the solver, not read (0)
	EXT_ZC_REC_GAP = 0x54,        // float: distance between the witnesses (metres; > 0 apart, < 0 overlapping)
	EXT_ZC_REC_FRICTION_A = 0x58, // float: friction of body A (body + 0x1B8)
	EXT_ZC_REC_FRICTION_B = 0x5C, // float: friction of body B
	EXT_ZC_REC_ELASTIC_A = 0x60,  // float: elasticity of body A (body + 0x1B4)
	EXT_ZC_REC_ELASTIC_B = 0x64,  // float: elasticity of body B
	EXT_ZC_REC_KEY_A = 0x68,      // s32: shape part of A (-1: the sphere has none)
	EXT_ZC_REC_KEY_B = 0x6C,      // s32: shape part of B (the triangle's number)
	EXT_ZC_REC_NUMBER = 0x70,     // s32: written by the solver
};

// The detector: subspace + 0x70 -> DET, DET + 0x14 -> P. Lists of P are vectors
// {capacity +0, count +4, data +8}; a pair entry is 8 bytes (shell A, shell B).
enum
{
	EXT_ZC_SUB_DETECTOR = 0x70,
	EXT_ZC_DET_INNER = 0x14,
	EXT_ZC_P_TOLERANCE = 0x14,    // float: the detector's tolerance (0.1 m in the original)
	EXT_ZC_P_TOL_STEP = 0x18,     // float: half of it during a substep, 0 outside (written by the substep)
	EXT_ZC_P_KNOWN_PAIRS = 0xB0,  // pairs whose penetration the time search could not remove
	EXT_ZC_P_PAIRS = 0xC8,        // pairs in contact
	EXT_ZC_P_PENETRATING = 0xEC,  // pairs found overlapping in this step (count != 0 = penetration)
	EXT_ZC_P_CONTACTS = 0xF8,     // the contact records
	EXT_ZC_VEC_CAPACITY = 0x0,
	EXT_ZC_VEC_COUNT = 0x4,
	EXT_ZC_VEC_DATA = 0x8,
};

// ------------------------------------------------------------------ the geometry Reload provides

// A triangle in the source's world (metres, z up), corners in the order that
// makes (b - a) x (c - a) its front normal; key = a stable number of the
// triangle (written as the record's shape part B); solid = 0 for triangles
// that never touch (trigger, kill).
typedef struct ExtZcTri
{
	double a[3], b[3], c[3];
	int key;
	int solid;
	int surface; // the source's surface type for the kart's contact callback (24 = maglev zone record, 27 = wall, 1 = other ground)
} ExtZcTri;

// The provider: up to max triangles that may lie within `reach` of `centre`
// (a box test is enough; the exact test is done here). Returns the count.
typedef int (*ExtZcTrianglesFn)(void *user, const double centre[3], double reach, ExtZcTri *out, int max);

// ------------------------------------------------------------------ setup (once per race, before the first step)

typedef struct ExtZcConfig
{
	ExtZcTrianglesFn triangles; // the geometry (Reload: ExtZc_ReloadTriangles)
	void *user;
	uint32_t trackShell;        // the shell the records name as body B (the track's static body, from the source world)
	uint32_t recordClass;       // the value for EXT_ZC_REC_VTABLE
	uint32_t recordArena;       // port memory for the record vectors (EXT_ZC_ARENA_BYTES, 16-byte aligned)
	Ps2F radius;                // the sphere radius of a kart body (0.52 m: 0x3F051EB8)
	uint32_t onlyShell;         // 0: every dynamic shell of the detector (P + 0x1C list); else only this shell gets records
} ExtZcConfig;
enum
{
	EXT_ZC_ARENA_BYTES = 0x8000, // room for 3 x 64 records and the pair lists
	EXT_ZC_MAX_TRIS = 256,       // triangles looked at per body and query
};
void ExtZc_Configure(const ExtZcConfig *cfg);

// ------------------------------------------------------------------ the answer

// For ExtZp_ReloadQuery (kind EXT_ZP_Q_COLLIDE). Returns 1 when in->func is one
// of these (out filled, answers written into the port's memory), 0 otherwise:
//   0x3B18F0  detect (a0 = subspace): for every dynamic body of the subspace's
//             systems the records of all triangles within the tolerance
//             (P + 0x14) of its sphere -> P + 0xF8 (replaced), overlapping pairs
//             -> P + 0xEC; for every record of a body owned by a kart action the
//             kart's contact callback (port of 0x197E30) once, with the event
//             built from the record; v0 = 1 if P + 0xEC is not empty.
//   0x3771C0, 0x377290, 0x377620, 0x36F840  the detector's list keeping
//             before/after the step and after a penetration (pair list P + 0xC8)
//   0x3AFE10  the penetration handling: ExtZp_Pen_3afe10 (ported below)
//   0x380FA0  detect for a given set of bodies (a0 = DET, a1 = the body set,
//             a2/a3/t0 = the output vectors: new pairs, records, pairs)
//   0x380FB0  one pair's overlap test (a0 = DET, a1 = pair): v0 = 1 if the
//             pair overlaps, out records for it as 0x380FA0
// Also kind EXT_ZP_Q_MATERIAL, func 0x390F40 (a0 = 16-byte output, a1 = the
// shell whose shape part the kart's callback asks for, the record's key): the
// normal of that triangle (vec4, w = 0) from the last detection.
int ExtZc_Answer(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in, ExtZpQueryResult *out);

// The event the kart's contact callback (port of 0x197E30, a1) reads, built per
// record in the port's stack: +0x00 s32 surface type, +0x10 vec4 contact point,
// +0x20 vec4 normal, +0x58 u8 side (0: the kart is body A), +0x5C u32 -> a pair
// object whose +0x0C is the record. Fields not listed are 0.
enum
{
	EXT_ZC_EV_SIZE = 0x60,
	EXT_ZC_EV_SURFACE = 0x00,
	EXT_ZC_EV_POINT = 0x10,
	EXT_ZC_EV_NORMAL = 0x20,
	EXT_ZC_EV_SIDE = 0x58,
	EXT_ZC_EV_PAIR = 0x5C,
	EXT_ZC_PAIR_RECORD = 0x0C,
};

// The record of a sphere (centre, radius) against one triangle when the gap is
// at most tol: 1 and rec filled (the 0x80 bytes, shells and materials left to
// the caller), 0 otherwise. Pure, for the tests.
int ExtZc_SphereTriangle(const Ps2F centre[3], Ps2F radius, Ps2F tol, const ExtZcTri *tri, uint8_t rec[EXT_ZC_REC_SIZE]);

// ------------------------------------------------------------------ the ported penetration handling (prefix Pen)
// One C function per source function, conventions as include/platform/native_extzonephys.h.

// The subspace's handling of a penetration (called after the detection found
// overlapping pairs): for every pair the time search below, then the detection
// again for the bodies it moved, until no new pair is found. a0 = subspace.
void ExtZp_Pen_3afe10(ExtZpCtx *c, uint32_t a0);
// The time search of one pair (a1 = 8-byte pair entry: shell A, shell B): halves
// the time between the bodies' times until the pair no longer overlaps (step
// <= 1e-4) and sets the bodies there (slot +0x2C of the body); a pair it cannot
// resolve goes to P + 0xB0. a0 = subspace.
void ExtZp_Pen_3b06d0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// A body set back to the time fraction t of its last step (rigid body slot
// +0x2C): recomputed from the body's saved state (+0x150 ..) with t * (+0x1A0);
// t == 0 restores the saved state. a0 = body, f12 = t.
void ExtZp_Pen_398c80(ExtZpCtx *c, uint32_t a0, Ps2F f12);
// Resizes a record vector to a1 records (new ones are copies of a default record).
void ExtZp_Pen_3b0bb0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
// Removes the records [a1, a2) of the vector a0; returns a1.
uint32_t ExtZp_Pen_3b0c90(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
// The list helpers the handling calls (vectors {capacity, count, data} of
// records, pairs and bodies; part pa).
uint32_t ExtZp_Pen_13dfd0(ExtZpCtx *c, uint32_t a0, uint32_t a1); // destroys a record vector (a1 > 0: frees the object too); returns a0
uint32_t ExtZp_Pen_13e040(ExtZpCtx *c, uint32_t a0, uint32_t a1);
void ExtZp_Pen_13e1a0(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Pen_345cc0(ExtZpCtx *c, uint32_t a0);              // the end of a 4-byte vector
uint32_t ExtZp_Pen_359210(ExtZpCtx *c, uint32_t a0);              // a0 + 8 (the data slot)
uint32_t ExtZp_Pen_359220(ExtZpCtx *c, uint32_t a0, uint32_t a1); // an empty vector
void ExtZp_Pen_359260(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2); // a pair vector := [a1, a2)
void ExtZp_Pen_3674c0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3); // insert a2 copies of the pair *a3 at a1
void ExtZp_Pen_3678b0(ExtZpCtx *c, uint32_t a0, uint32_t a1);
uint32_t ExtZp_Pen_3678d0(ExtZpCtx *c, uint32_t a0);
uint32_t ExtZp_Pen_36e250(ExtZpCtx *c, uint32_t a0, uint32_t a1); // zeroes the three header words; returns a0
uint32_t ExtZp_Pen_370820(ExtZpCtx *c, uint32_t a0);              // returns a0
void ExtZp_Pen_379bc0(ExtZpCtx *c, uint32_t a0, uint32_t a1);     // resizes a pair vector
uint32_t ExtZp_Pen_37aa80(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t ExtZp_Pen_37e3f0(ExtZpCtx *c, uint32_t a0, uint32_t a1); // a copy of a 4-byte vector
void ExtZp_Pen_37f0f0(ExtZpCtx *c, uint32_t a0, uint32_t a1, uint32_t a2);
uint32_t ExtZp_Pen_381010(ExtZpCtx *c, uint32_t a0);              // detector -> P + 0xEC
uint32_t ExtZp_Pen_381020(ExtZpCtx *c, uint32_t a0);              // detector -> P + 0xC8

// ------------------------------------------------------------------ Reload's geometry
// The provider over Reload's external collision (native_extcoll: the world mesh
// of the level, triangles in Reload units, converted to the source's metres
// with the zone port's frame). Only in the game build.
int ExtZc_ReloadTriangles(void *user, const double centre[3], double reach, ExtZcTri *out, int max);

// --ext-selftest zonecontact: "ext selftest zonecontact passed: N checks, 0 failures".
int ExtZc_Selftest(void);

#endif
