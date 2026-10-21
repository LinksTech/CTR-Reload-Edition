#ifndef NATIVE_EXTZONEPHYS_H
#define NATIVE_EXTZONEPHYS_H

#include <platform/native_ps2float.h>

// THE SOURCE GAME'S ZONE PHYSICS, PORTED STEP BY STEP (platform/native_extzonephys.c).
//
// Every function here is one function of the source game's executable, in the
// same order of operations, with the second console's float arithmetic
// (native_ps2float.c), so that it gives the same bits as the original for the
// same input. It computes in the source game's world (metres, z up) on its own
// state; Reload's side (platform/native_extzoneport.c) maps Reload's state in
// and out and answers the queries the source asks its physics engine.
// Standalone C (no game header), so an offline difference test can link it.
//
// The port follows the original literally, also where the original reads past
// the data it is given (noted per function); callers keep to the ranges the
// original's callers keep to.

// A test box (an oriented slab box): three axes and, per axis, the range of the
// projection P . axis that lies inside (both ends inside).
typedef struct ExtZpBox
{
	Ps2F axis[3][3];
	Ps2F range[3][2]; // [k][0] lowest, [k][1] highest
} ExtZpBox;

// A gravity spline: its kind and its nodes (position x y z, direction x y z).
enum
{
	EXT_ZP_SPLINE_PLANE = 0,
	EXT_ZP_SPLINE_POINT = 1,
	EXT_ZP_SPLINE_INVPOINT = 2,
};
typedef struct ExtZpSpline
{
	int type;
	int nodeCount;
	const Ps2F (*nodes)[6];
} ExtZpSpline;

// 1 = p lies in the box: for every axis lowest <= p . axis <= highest.
int ExtZp_BoxContains(const ExtZpBox *box, const Ps2F p[3]);
// The first box of the list (in list order) that holds p: 1 and *hit = its
// place, 0 and *hit untouched when none does (hit may be NULL).
int ExtZp_BoxListFirst(const ExtZpBox *const *list, int count, const Ps2F p[3], int *hit);
// Builds a box from a corner p0 and the three corners p1..p3 at the ends of its
// edges from p0: axis k = unit(p_k - p0), range k = [axis . p0, axis . p_k].
void ExtZp_BoxBuild(ExtZpBox *box, const Ps2F p0[3], const Ps2F p1[3], const Ps2F p2[3], const Ps2F p3[3]);

// The segment of the spline nearest to p, searched from node `node` one
// segment each way (segments lo..hi with lo = max(node - 1, 0), hi =
// min(node + 1, count - 2)); the first of equal distances; -1 when there is
// no segment in that window.
int ExtZp_SplineLocal(const ExtZpSpline *s, int node, const Ps2F p[3]);
// The nearest node of the whole spline (by distance to the node's position,
// the first of equal ones), then ExtZp_SplineLocal from it.
int ExtZp_SplineNearest(const ExtZpSpline *s, const Ps2F p[3]);
// The direction of the spline's force at p on segment `node` (node and
// node + 1; node == count: count - 1 and count, which the original reads past
// its nodes - callers give 0 .. count - 2): plane = unit of the directions
// blended by the unclamped segment parameter, point = unit towards the nearest
// point of the segment, inverse point = away from it, other kinds (0, 0, 1).
void ExtZp_SplineDir(const ExtZpSpline *s, int node, const Ps2F p[3], Ps2F out[3]);

// out.xyz = a x b, out.w = a.w (the vector unit's outer product pair).
void ExtZp_Cross(Ps2F out[4], const Ps2F a[4], const Ps2F b[4]);

// ------------------------------------------------------------------ the memory form
// The rest of the port works on the source's objects in the port's memory
// (include/platform/native_extzonemem.h): one C function per source function,
// arguments as the source passes them (32-bit addresses, floats as Ps2F), the
// fields at the source's offsets. A field is named where its meaning is known
// (below, or a comment at the access); the offsets are facts of the layout the
// port shares with the original, so that both can run on the same bytes.
#include <platform/native_extzonemem.h>

// Fields used across parts (the kart object K, its body H).
enum
{
	EXT_ZP_K_MAGLEV = 0x428,      // u8: the kart is in the maglev (anti-gravity) state
	EXT_ZP_B_VELOCITY = 0x60,     // vec4: the body's linear velocity (m/s, z up)
	EXT_ZP_B_ANGULAR = 0x70,      // vec4: the body's angular velocity
	EXT_ZP_B_INV_MASS = 0xA4,     // float: one over the body's mass
};

// Query kinds (calls the port hands to Reload; ExtZp_Query).
enum
{
	EXT_ZP_Q_RAY = 1,             // a ray against the track (start, end, hit record)
	EXT_ZP_Q_MATERIAL = 2,        // the surface of a contact's shape part
	EXT_ZP_Q_PORTAL = 3,          // a portal teleport
	EXT_ZP_Q_CAMERA_SPHERE = 4,   // the camera's sphere test
	EXT_ZP_Q_COLLIDE = 5,         // the collision detection of a step (contacts, penetration)
	EXT_ZP_Q_OTHER = 99,          // anything else the port does not compute (logged)
};

// Part K0 (platform/native_extzonephys_k0.c): small body helpers.
void ExtZp_Kart_1967e0(ExtZpCtx *c, uint32_t kart, uint32_t on);
void ExtZp_Kart_136110(ExtZpCtx *c, uint32_t body, uint32_t impulse);
void ExtZp_Kart_13dd40(ExtZpCtx *c, uint32_t body, uint32_t v);
void ExtZp_Kart_190350(ExtZpCtx *c, uint32_t body, uint32_t v);

// The other parts declare their functions in their own headers.
#include <platform/native_extzonephys_a.h>
#include <platform/native_extzonephys_b1.h>
#include <platform/native_extzonephys_b2.h>
#include <platform/native_extzonephys_c.h>
#include <platform/native_extzonephys_d.h>
#include <platform/native_extzonephys_e.h>
#include <platform/native_extzonephys_f.h>
#include <platform/native_extzonephys_g.h>
#include <platform/native_extzonephys_h.h>
#include <platform/native_extzonephys_i.h>
#include <platform/native_extzonephys_j.h>

#endif
