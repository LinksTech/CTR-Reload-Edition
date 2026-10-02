// rldpack_reduce.inc - automatic mesh reduction for make-char
//
// Part of the packer, never of the game. Self-contained: it needs only the C
// standard library (stdlib, string, stdio, stdarg), the types u8 and u32,
// s_machine, Rld_Say and CTR_NATIVE_BUILD_ID of tools/rldpack.c, the SHA-256
// of include/rldtrack.inc (THE CACHE), and RldMk_Progress of
// tools/rldpack_char.inc (progress lines for Reload Studio, which never change
// a result), so it can be included anywhere after those.
//
// WHAT IT DOES. A model with more triangles than the draw memory of a driver
// allows is reduced to a target per group (kart, driver, steering wheel), or
// to one budget the groups share, by half-edge collapses: a position u is
// moved onto a neighbor v that already exists, every triangle on the edge
// (u, v) disappears and every other triangle of u now uses v. No new position
// is ever made, so the hull of the model can only shrink, and every kept
// triangle continues one input triangle (sourceTriangle) with its corners in
// the same order; its position is either that corner's own or the corner's
// position was removed.
//
// COLORS (and any other corner attribute) are never mixed: each kept corner
// carries the attributes of one input corner at the SAME position
// (sourceCorner). A corner that a collapse moves from u to v takes those of a
// corner at v - of the colors there the one nearest to its own, so that on
// either side of a color seam each side keeps its color (RldRed_PickCorner).
// Taking the color of the input corner of the same slot instead, at a
// position the corner has left, paints a wedge of a foreign color across the
// moved triangle.
//
// WHICH COLLAPSE FIRST. The error measure is the quadric of Garland and
// Heckbert: each position holds the sum of the squared distance functions to
// the planes of its triangles, weighted with the triangle's area. Moving u
// onto v costs the sum of both quadrics measured at v. All collapses wait in
// one heap ordered by cost, ties by (u, v); the cheapest valid one is done,
// the quadric of u is added to v, and the edges around v are measured again.
// Additions to the plain measure:
//   - OPEN EDGES get an extra plane through the edge, perpendicular to its
//     triangle, so that the outline of an open surface keeps its shape. An
//     edge is open when all its triangles lie on the same side of it: their
//     third corners point the same way (within about 25 degrees) seen along
//     the edge. That is a single triangle, but also the outline of a sheet
//     modelled two-sided (hair, fins, ears) - front and back on one edge,
//     whether the back is an exact twin of the front or split along the other
//     diagonal - which would otherwise shrink at no cost. The plane is added
//     once per open edge, however many triangles it has.
//   - A SHAPE TERM: every position also holds a small quadric of its own
//     point (RLDRED_SHAPE_WEIGHT times its share of the area). Where the
//     planes cost nothing (flat areas) it prefers short collapses into the
//     middle of what was merged before, instead of index order, which would
//     leave long slivers.
//   - THE BOX STAYS: per group, the position with the lowest and the highest
//     value on each axis (the lowest index on a tie) is never moved, so a
//     reduced model keeps its length, width and height - the size make-char
//     fits it to.
//   - COLOR SEAMS (edges whose triangles carry different colors at one of its
//     positions) and CREASES (edges between triangles more than 60 degrees
//     apart) get the same kind of plane as open edges, from each of their
//     triangles: they may slide along themselves but cost when they move off
//     their line (RLDRED_SEAM_WEIGHT, RLDRED_CREASE_WEIGHT). Only such sharp
//     creases are guarded; the outline of a smooth curved part (where the
//     surface turns away from the viewer gradually) has no edge of its own
//     and keeps only what the face planes give it.
//   - THE COLOR COST: a collapse also costs the color it changes - the moved
//     corners that take another color and the colors of the dying triangles
//     that their positions do not keep, each times its share of the area
//     (RLDRED_COLOR_WEIGHT).
//   These last two only steer the order; the rules below are the same with
//   and without them. When the guarded order ends above a target, the
//   reduction runs again with the color cost only, then without both
//   (RldRed_Reduce; RldRedResult.stage says which run it took).
//   Colors come first, the outline second: on a driver of 1082 triangles
//   reduced to 796 (seen from 7 sides, 240 pixels each), the pixels whose
//   color is off by more than 60 fell from 3833 to 3525 against the plain
//   measure, the pixels where the outline differs rose from 105 to 201; on
//   two other drivers both fell (4557 to 2555 and 1574 to 1409; 172 to 32
//   and 9 to 7).
//
// WHAT IS REFUSED. A collapse is not done when
//   - u and v are not in the same group, or a triangle of either is in another
//     group (positions shared between groups are never moved and never a goal),
//     or u is one of the box positions above,
//   - the group is already at or below its target (with a shared budget: all
//     groups together are at or below it, or this group is at its floor),
//   - an edge would get more triangles than it or its partner had: for a third
//     corner w of a triangle on (u, v), the edges (u, w) and (v, w) become one
//     and may not end up with more triangles than the larger of them had
//     (the link condition, extended to edges with more than two triangles),
//   - u and v have a neighbor w in common that is no third corner of the edge
//     (the surface would pinch). Together with the rule before, this is the
//     link condition; on a two-sided sheet it holds for the front and the
//     back at once, because both share the positions,
//   - both are on an outline while (u, v) itself is no open edge: two
//     outlines would meet in one point. An outline is an edge with a single
//     triangle, or an open edge (see above) at a position that front and back
//     of a two-sided sheet share (it has an edge with more than two
//     triangles): the link condition alone would let a chord between two
//     outline positions pinch such a sheet. A closed thin part (comb teeth)
//     has open edges too, but its positions are not shared; the link
//     condition keeps it whole and its teeth may melt together,
//   - v or a third corner of the edge would be left without any triangle (a
//     position would vanish without its quadric being counted),
//   - a moved triangle would get the same three positions as a triangle v
//     already has: refused when both run the same way (a doubled triangle) or
//     when they run opposite ways but their input triangles did not face
//     opposite ways (a closed part folding flat); two triangles of a
//     two-sided sheet may become twins,
//   - the normal of a moved triangle turns by more than about 75 degrees
//     against its current normal (no fold, no flip) or its area becomes zero,
//   - the normal of a moved triangle turns by more than 60 degrees against
//     the normal of its input triangle (no slow turning over many collapses).
//     When this first phase runs dry above a target, every edge is measured
//     again and a second phase goes on with 84 degrees instead, so that
//     fine detail that cannot be kept (a comb of 0.1-unit teeth, say) can
//     still melt together - but no triangle ever turns to face away from its
//     input triangle, in no pose (RLDRED_SOURCE_COS_LATE),
//   - a moved triangle becomes a needle (RLDRED_QUALITY_MIN, a tip below about
//     5 degrees) worse than the worst triangle around u was before. So the
//     worst shape of the whole mesh never drops below the needle limit or
//     below the worst input triangle, while fans of needles (a long n-gon
//     split into triangles) can still be reduced.
//
// DETERMINISM. The same input gives the same output bit for bit: no time, no
// random numbers, no hash order. Every list is walked in a fixed order and
// the heap has a complete order (cost, u, v, versions). Stale heap entries are
// recognized by a version per position: any change around a position raises
// its version, the edges around it are measured and pushed again, and an
// entry whose versions are not the current ones is skipped (and dropped when
// the heap is compacted). One cost can be older than its versions: the color
// cost of u -> v also looks at the colors a third corner w of the edge keeps,
// and a collapse elsewhere at w raises the versions of w and its partner,
// not always those of u and v. Such an entry keeps its cost from when it was
// measured; measuring it again would change the order and so the results,
// for a small gain - it stays as it is. The square root is written out instead of taken
// from the C library, like the arithmetic of tools/rldpack_char.inc, so the
// result does not depend on the runtime library. Bit-exact results across
// builds also need IEEE double arithmetic without excess precision and
// without contracted multiply-adds - as this project builds with MSVC
// (SSE2, /fp:precise); an x87 or FMA build may round the last bit
// differently and then pick another of two nearly equal collapses.

//========================================================================================
// INTERFACE
//========================================================================================

#define RLDRED_GROUPS_MAX 8u

typedef struct RldRedMesh
{
	u32 positionCount;
	const double *position;   // 3 per position, game units
	u32 triangleCount;
	const u32 *triangle;      // 3 position indices per triangle
	const u8 *triangleGroup;  // per triangle: group 0..groupCount-1
	u32 groupCount;           // <= 8
	const u32 *groupTarget;   // per group: keep at most this many triangles (with totalTarget: at least)
	const u32 *cornerColor;   // 3 per triangle: the color of input corner 3*t+k as 0xRRGGBB; NULL = no
	                          // colors (no color seams, no color cost)
	u32 totalTarget;          // 0 = every group down to its own groupTarget; else the groups share this
	                          // budget: the cheapest collapse of any group goes first until all groups
	                          // together keep at most totalTarget, and groupTarget is each group's floor:
	                          // a group stops there - its last collapse may take it below by up to the
	                          // triangles on that edge less one (1 on a closed surface, up to 3 on the
	                          // edge of a two-sided sheet). Callers set every field (cornerColor NULL, 0).
} RldRedMesh;

typedef struct RldRedResult
{
	u32 triangleCount;
	u32 *triangle;        // 3 position indices per kept triangle; only positions of the input, no new ones
	u32 *sourceTriangle;  // per kept triangle: the input triangle it continues (same facing, same group)
	u32 *sourceCorner;    // per kept corner (3 per triangle): the input corner 3*t+k whose attributes
	                      // (color, uv) it carries; that input corner always sits at the same position
	                      // as the kept corner (triangle[i] == input triangle[sourceCorner[i]])
	double maxError;      // largest accepted collapse cost
	u32 stage;            // the run that reached the target: 0 guarded (seams, creases, color cost),
	                      // 1 color cost only, 2 unguarded (see RldRed_Reduce)
} RldRedResult;

//========================================================================================
// CONSTANTS
//========================================================================================

#define RLDRED_NONE 0xFFFFFFFFu

// Doubles per quadric: the upper triangle of the symmetric 4 x 4 matrix.
#define RLDRED_QUADRIC 10u

// The extra planes along open edges count this many times their edge length
// squared (the face planes count their area): moving an open outline costs
// about as much as moving a surface ten times the size.
#define RLDRED_BOUNDARY_WEIGHT 10.0

// The point quadric of each position, times a third of the area of its
// triangles: moving a merged cluster a distance e costs as much as moving a
// surface of the same area by about 0.03 e - enough to order flat areas, too
// little to outweigh a real change of shape.
#define RLDRED_SHAPE_WEIGHT 0.001

// A moved triangle may turn its normal by at most acos(0.25) = 75.5 degrees
// against its current normal and by at most acos(0.5) = 60 degrees against
// the normal of its input triangle. Compared squared: no square root needed.
#define RLDRED_FLIP_COS 0.25
#define RLDRED_SOURCE_COS 0.5

// In the second phase the limit against the input triangle is looser:
// acos(0.1) = 84 degrees, enough for fine detail (comb teeth) to melt
// together, but no triangle ever faces away from its input triangle - not
// in the neutral pose and not in any pose make-char makes: the poses turn a
// group as one block (the angle between two triangles of it stays) and
// stretch its height by 0.93 to 1.05, which changes that angle by less than
// 5 degrees.
#define RLDRED_SOURCE_COS_LATE 0.1

// COLOR SEAMS. An edge whose triangles carry different colors at one of its
// two positions (squared RGB distance above RLDRED_SEAM_COLOR2: a distance
// of 40, about what a 64 color palette moves a color anyway; in exported
// models such colors differ mostly either by a few steps or by far more) is
// a seam. Each triangle on it adds the plane through the edge,
// perpendicular to itself, with RLDRED_SEAM_WEIGHT times the squared edge
// length - two sides together as much as one open edge. A seam may slide
// along itself but moving it off its line costs like moving an outline.
// A seam between near colors holds less: below a distance of 100
// (RLDRED_SEAM_FULL2) the weight falls with the squared distance, so a step
// of 40 to 100 does not take the triangles a shape needs (never below the
// crease weight where the seam is a crease as well).
#define RLDRED_SEAM_COLOR2 1600u
#define RLDRED_SEAM_WEIGHT 5.0
#define RLDRED_SEAM_FULL2 10000u

// CREASES (sharp edges - the outline of angular parts; the outline of a
// smooth curved part has no such edge and is not guarded): an edge that is
// not open, whose two triangles' normals differ by more than acos(0.5) = 60
// degrees (and do not face away from each other as front and back of a
// sheet), gets the same planes with
// RLDRED_CREASE_WEIGHT - about twice the area of a triangle beside it, on
// top of the face planes, so a crease line keeps its place more firmly than
// a flat area. Sharper edges (thin fins) count as open edges already.
#define RLDRED_CREASE_COS 0.5
#define RLDRED_CREASE_WEIGHT 1.0

// THE COLOR COST of a collapse (RldRed_ColorCost): every moved corner takes
// the color of a corner at v (the nearest color there, RldRed_PickCorner),
// every triangle on the edge dies. A color that changes or that its position
// no longer has costs RLDRED_COLOR_WEIGHT x (a third of the area of its
// triangle) x (squared RGB distance / 195075, the squared distance from
// black to white). With 162 a step of 60 - a clearly visible wedge of
// another color - costs as much as moving the same area by one unit (about
// one step of the vertex grid of a frame); black to white as much as about
// 7 units, the length of a typical edge of a reduced model.
#define RLDRED_COLOR_WEIGHT 162.0
#define RLDRED_COLOR_FULL2 195075.0

// Two triangles on one edge lie on the same side of it when their third
// corners, seen along the edge, point within acos(0.9) = 25.8 degrees.
#define RLDRED_OPEN_COS 0.9

// Two input triangles face away from each other (front and back of a sheet)
// when their normals point within acos(0.9) = 25.8 degrees of opposite ways.
#define RLDRED_FACING_COS 0.9

// The shape of a triangle: q = 4 sqrt(3) area / (sum of its squared edges),
// 1 for an equilateral triangle, 0 for a line; 0.15 is a needle with a 5
// degree tip. A collapse may not leave a moved triangle below it unless no
// triangle of u was better before (the worst shape around u never gets worse).
#define RLDRED_QUALITY_MIN 0.15

// A guard against overflow of the corner arithmetic, far above any model.
// Not a guard against long runs: reducing that many takes minutes, not hours
// (25 closed spheres, 855,668 triangles after repair: make-char --check took
// 303 s, two passes of all three stages, the target not reached).
#define RLDRED_INPUT_MAX 0x00100000u

// Above this many input triangles make-char says beforehand how long the
// reduction will take (reduce-slow, RldRed_SaySlow).
#define RLDRED_SLOW_INPUT 60000u

// Positions must be finite and within this many units on every axis.
#define RLDRED_COORD_MAX 1000000.0

// The heap is compacted (stale entries dropped) when it has grown to twice
// its size after the last compaction, and at least to this many entries.
#define RLDRED_HEAP_COMPACT_MIN 4096u

//========================================================================================
// ARITHMETIC THAT ROUNDS THE SAME EVERYWHERE
//========================================================================================

// Square root without the C library: the value is scaled by powers of four
// into [0.25, 4] (exact), Newton's method runs a fixed number of steps there,
// and the result is scaled back by the same powers of two (exact again).
static double RldRed_Sqrt(double value)
{
	double x = value;
	double scale = 1.0;
	double r;
	int i;

	if (!(value > 0.0) || !(value < 1.0e300))
	{
		return (value > 0.0) ? value : 0.0;
	}

	while (x > 4.0)
	{
		x *= 0.25;
		scale *= 2.0;
	}
	while (x < 0.25)
	{
		x *= 4.0;
		scale *= 0.5;
	}

	r = 0.5 * (x + 1.0);
	for (i = 0; i < 10; i++)
	{
		r = 0.5 * (r + x / r);
	}

	return r * scale;
}

static void RldRed_Cross(const double *a, const double *b, const double *c, double out[3])
{
	const double u0 = b[0] - a[0];
	const double u1 = b[1] - a[1];
	const double u2 = b[2] - a[2];
	const double v0 = c[0] - a[0];
	const double v1 = c[1] - a[1];
	const double v2 = c[2] - a[2];

	out[0] = u1 * v2 - u2 * v1;
	out[1] = u2 * v0 - u0 * v2;
	out[2] = u0 * v1 - u1 * v0;
}

static double RldRed_Dot(const double *a, const double *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// 1 when the angle between a and b is below acos(cosine) (cosine >= 0), 0
// for a zero vector. Compared squared.
static int RldRed_Within(const double *a, const double *b, double cosine)
{
	const double d = RldRed_Dot(a, b);

	return (d > 0.0) && ((d * d) > (cosine * cosine * RldRed_Dot(a, a) * RldRed_Dot(b, b)));
}

// The same for any cosine from -1 to 1 (angles beyond 90 degrees too).
static int RldRed_TurnBelow(const double *a, const double *b, double cosine)
{
	const double d = RldRed_Dot(a, b);

	if (cosine >= 0.0)
	{
		return RldRed_Within(a, b, cosine);
	}
	return (d >= 0.0) || ((d * d) < (cosine * cosine * RldRed_Dot(a, a) * RldRed_Dot(b, b)));
}

// 1 when the angle between a and b is above 180 degrees - acos(cosine).
static int RldRed_Opposite(const double *a, const double *b, double cosine)
{
	const double d = RldRed_Dot(a, b);

	return (d < 0.0) && ((d * d) > (cosine * cosine * RldRed_Dot(a, a) * RldRed_Dot(b, b)));
}

// q squared (see RLDRED_QUALITY_MIN) of the triangle a, b, c with the normal
// (not unit length) normal: 12 |normal|^2 / (sum of squared edges)^2.
static double RldRed_Quality2(const double *a, const double *b, const double *c, const double *normal)
{
	const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double bc[3] = {c[0] - b[0], c[1] - b[1], c[2] - b[2]};
	const double ca[3] = {a[0] - c[0], a[1] - c[1], a[2] - c[2]};
	const double edges = RldRed_Dot(ab, ab) + RldRed_Dot(bc, bc) + RldRed_Dot(ca, ca);

	return (edges > 0.0) ? (12.0 * RldRed_Dot(normal, normal) / (edges * edges)) : 0.0;
}

// A quadric is the symmetric 4 x 4 matrix of the plane (a, b, c, d), stored as
// its upper triangle: aa ab ac ad bb bc bd cc cd dd.
static void RldRed_AddPlane(double *q, double a, double b, double c, double d, double weight)
{
	q[0] += weight * a * a;
	q[1] += weight * a * b;
	q[2] += weight * a * c;
	q[3] += weight * a * d;
	q[4] += weight * b * b;
	q[5] += weight * b * c;
	q[6] += weight * b * d;
	q[7] += weight * c * c;
	q[8] += weight * c * d;
	q[9] += weight * d * d;
}

// The squared distance sum of the quadric p + q at the point x.
static double RldRed_QuadricError(const double *p, const double *q, const double *x)
{
	double e;
	double s[RLDRED_QUADRIC];
	u32 i;

	for (i = 0; i < RLDRED_QUADRIC; i++)
	{
		s[i] = p[i] + q[i];
	}

	e = s[0] * x[0] * x[0] + 2.0 * s[1] * x[0] * x[1] + 2.0 * s[2] * x[0] * x[2] + 2.0 * s[3] * x[0] +
	    s[4] * x[1] * x[1] + 2.0 * s[5] * x[1] * x[2] + 2.0 * s[6] * x[1] +
	    s[7] * x[2] * x[2] + 2.0 * s[8] * x[2] +
	    s[9];

	// Rounding can leave a tiny negative rest where the true value is zero.
	return (e > 0.0) ? e : 0.0;
}

//========================================================================================
// THE STATE OF ONE REDUCTION
//========================================================================================

// One waiting collapse: move u onto v. stampU and stampV are the versions of
// u and v when the cost was measured.
struct RldRedCandidate
{
	double cost;
	u32 u;
	u32 v;
	u32 stampU;
	u32 stampV;
};

// The state of a position.
#define RLDRED_POSITION_UNUSED 0u   // no triangle uses it (any more)
#define RLDRED_POSITION_LIVE 1u     // all its triangles are in one group
#define RLDRED_POSITION_LOCKED 2u   // triangles of more than one group: never moved, never a goal

struct RldRedState
{
	const RldRedMesh *mesh;
	u32 n;                 // positions
	u32 m;                 // triangles

	double *position;      // 3 per position, relative to the middle of the box (smaller quadric terms)
	double *quadric;       // RLDRED_QUADRIC per position
	double *sourceNormal;  // 3 per triangle: the normal of the input triangle (not unit length)
	u32 *corner;           // 3 per triangle: the position each corner uses now
	u32 *attribute;        // 3 per triangle: the input corner whose attributes the corner carries; it
	                       // sits at the position the corner uses now
	u8 *triangleAlive;     // per triangle
	u32 *head;             // per position: its first corner (list of corners at that position)
	u32 *next;             // per corner: the next corner at the same position
	u32 *stamp;            // per position: the version
	u32 *valence;          // per position: its live triangles
	u8 *positionState;     // RLDRED_POSITION_*
	u8 *positionGroup;     // per position: the group of its triangles (when live)
	u8 *pinned;            // per position: a box position of its group, never moved

	// scratch, per position, valid where the mark array holds the current mark
	u32 *markA;
	u32 *markB;
	u32 *markC;
	u32 *markD;
	u32 *countU;           // triangles of u on the edge to this position
	u32 *countV;           // triangles of v on the edge to this position
	u32 *countUV;          // triangles with u, v and this position
	u32 *edgeOpen;         // RldRed_OnOpenEdge: 1 while the edge to this position looks open
	u32 *edgeCount;        // RldRed_OnOpenEdge: triangles on the edge to this position
	double *wing;          // 3 per position: RldRed_OnOpenEdge, the first wing of the edge
	u32 markNow;

	// scratch lists
	u32 *listU;            // triangles of u (m)
	u32 *listV;            // triangles of v (m)
	u32 *listX;            // triangles of some position (m)
	u32 *ring;             // neighbors of one position (n)
	u32 *touched;          // positions whose version was raised (n)

	u32 groupCount[RLDRED_GROUPS_MAX];

	struct RldRedCandidate *heap;
	size_t heapCount;
	size_t heapCapacity;
	size_t heapCompactAt;

	int strict;            // first phase: the normal limit against the input triangle holds
	int guard;             // 2 seams, creases and the color cost; 1 the color cost only; 0 none (see RldRed_Reduce)
	u32 total;             // live triangles of all groups
	double maxError;
};

static void RldRed_FreeState(struct RldRedState *s)
{
	free(s->position);
	free(s->quadric);
	free(s->sourceNormal);
	free(s->corner);
	free(s->attribute);
	free(s->triangleAlive);
	free(s->head);
	free(s->next);
	free(s->stamp);
	free(s->valence);
	free(s->positionState);
	free(s->positionGroup);
	free(s->pinned);
	free(s->markA);
	free(s->markB);
	free(s->markC);
	free(s->markD);
	free(s->countU);
	free(s->countV);
	free(s->countUV);
	free(s->edgeOpen);
	free(s->edgeCount);
	free(s->wing);
	free(s->listU);
	free(s->listV);
	free(s->listX);
	free(s->ring);
	free(s->touched);
	free(s->heap);
	memset(s, 0, sizeof(*s));
}

// A fresh mark value for the scratch arrays.
static u32 RldRed_NextMark(struct RldRedState *s)
{
	s->markNow++;
	return s->markNow;
}

// Clears the scratch marks long before the counter could wrap; called only
// between two collapses, where no mark is held. One collapse takes fewer than
// positionCount + 16 marks, far below the margin.
static void RldRed_MarkGuard(struct RldRedState *s)
{
	if (s->markNow > 0xF0000000u)
	{
		memset(s->markA, 0, (size_t)s->n * sizeof(u32));
		memset(s->markB, 0, (size_t)s->n * sizeof(u32));
		memset(s->markC, 0, (size_t)s->n * sizeof(u32));
		memset(s->markD, 0, (size_t)s->n * sizeof(u32));
		s->markNow = 0u;
	}
}

static int RldRed_HasCorner(const u32 *t, u32 x)
{
	return (t[0] == x) || (t[1] == x) || (t[2] == x);
}

// The squared RGB distance of two colors 0xRRGGBB (integers: exact).
static u32 RldRed_ColorDistance2(u32 a, u32 b)
{
	const int dr = (int)((a >> 16) & 0xFFu) - (int)((b >> 16) & 0xFFu);
	const int dg = (int)((a >> 8) & 0xFFu) - (int)((b >> 8) & 0xFFu);
	const int db = (int)(a & 0xFFu) - (int)(b & 0xFFu);

	return (u32)(dr * dr + dg * dg + db * db);
}

// The normal of triangle t as it is now (not unit length).
static void RldRed_Normal(const struct RldRedState *s, u32 t, double out[3])
{
	const u32 *c = &s->corner[3u * t];

	RldRed_Cross(&s->position[3u * c[0]], &s->position[3u * c[1]], &s->position[3u * c[2]], out);
}

// The wing of a triangle on the edge (a, b): the direction from the edge to
// its third corner, at right angles to the edge.
static void RldRed_Wing(const struct RldRedState *s, u32 a, u32 b, u32 third, double out[3])
{
	const double *pa = &s->position[3u * a];
	const double *pb = &s->position[3u * b];
	const double *pc = &s->position[3u * third];
	const double edge[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
	const double toward[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
	const double e2 = RldRed_Dot(edge, edge);
	const double along = (e2 > 0.0) ? (RldRed_Dot(toward, edge) / e2) : 0.0;

	out[0] = toward[0] - along * edge[0];
	out[1] = toward[1] - along * edge[1];
	out[2] = toward[2] - along * edge[2];
}

//========================================================================================
// THE HEAP: cost, then u, then v, then the versions - a complete order
//========================================================================================

static int RldRed_Before(const struct RldRedCandidate *a, const struct RldRedCandidate *b)
{
	if (a->cost != b->cost)
	{
		return a->cost < b->cost;
	}
	if (a->u != b->u)
	{
		return a->u < b->u;
	}
	if (a->v != b->v)
	{
		return a->v < b->v;
	}
	if (a->stampU != b->stampU)
	{
		return a->stampU < b->stampU;
	}
	return a->stampV < b->stampV;
}

static void RldRed_SiftDown(struct RldRedState *s, size_t i)
{
	const struct RldRedCandidate moving = s->heap[i];

	for (;;)
	{
		size_t child = 2u * i + 1u;

		if (child >= s->heapCount)
		{
			break;
		}
		if (((child + 1u) < s->heapCount) && RldRed_Before(&s->heap[child + 1u], &s->heap[child]))
		{
			child++;
		}
		if (!RldRed_Before(&s->heap[child], &moving))
		{
			break;
		}
		s->heap[i] = s->heap[child];
		i = child;
	}
	s->heap[i] = moving;
}

static int RldRed_HeapPush(struct RldRedState *s, const struct RldRedCandidate *c)
{
	size_t i;

	if (s->heapCount == s->heapCapacity)
	{
		const size_t capacity = (s->heapCapacity < 256u) ? 256u : (s->heapCapacity * 2u);
		struct RldRedCandidate *grown = (struct RldRedCandidate *)realloc(s->heap, capacity * sizeof(struct RldRedCandidate));

		if (grown == NULL)
		{
			return 0;
		}
		s->heap = grown;
		s->heapCapacity = capacity;
	}

	i = s->heapCount++;
	while (i > 0u)
	{
		const size_t parent = (i - 1u) / 2u;

		if (!RldRed_Before(c, &s->heap[parent]))
		{
			break;
		}
		s->heap[i] = s->heap[parent];
		i = parent;
	}
	s->heap[i] = *c;
	return 1;
}

static void RldRed_HeapPop(struct RldRedState *s, struct RldRedCandidate *out)
{
	*out = s->heap[0];
	s->heapCount--;
	if (s->heapCount != 0u)
	{
		s->heap[0] = s->heap[s->heapCount];
		RldRed_SiftDown(s, 0u);
	}
}

static int RldRed_GroupAbove(const struct RldRedState *s, u32 group)
{
	if ((s->mesh->totalTarget != 0u) && (s->total <= s->mesh->totalTarget))
	{
		return 0;
	}
	return s->groupCount[group] > s->mesh->groupTarget[group];
}

// 1 while the entry can still be done: both positions live with the versions
// it was measured with, u not pinned, the group above its target.
static int RldRed_Current(const struct RldRedState *s, const struct RldRedCandidate *c)
{
	return (s->positionState[c->u] == RLDRED_POSITION_LIVE) && (s->positionState[c->v] == RLDRED_POSITION_LIVE) && (c->stampU == s->stamp[c->u]) &&
	       (c->stampV == s->stamp[c->v]) && !s->pinned[c->u] && RldRed_GroupAbove(s, s->positionGroup[c->u]);
}

// Drops the stale entries (in array order) and builds the heap again from
// the bottom up; both deterministic.
static void RldRed_HeapCompact(struct RldRedState *s)
{
	size_t kept = 0;
	size_t i;

	for (i = 0; i < s->heapCount; i++)
	{
		if (RldRed_Current(s, &s->heap[i]))
		{
			s->heap[kept++] = s->heap[i];
		}
	}
	s->heapCount = kept;
	for (i = kept / 2u; i-- > 0u;)
	{
		RldRed_SiftDown(s, i);
	}

	s->heapCompactAt = 2u * kept;
	if (s->heapCompactAt < RLDRED_HEAP_COMPACT_MIN)
	{
		s->heapCompactAt = RLDRED_HEAP_COMPACT_MIN;
	}
}

//========================================================================================
// ADJACENCY: the live triangles and the neighbors of a position
//========================================================================================

// The live triangles at position x, in list order, into out (room for m).
// Corners of triangles that died on the way are unlinked here.
static u32 RldRed_Triangles(struct RldRedState *s, u32 x, u32 *out)
{
	u32 count = 0;
	u32 previous = RLDRED_NONE;
	u32 c = s->head[x];

	while (c != RLDRED_NONE)
	{
		const u32 following = s->next[c];

		if (!s->triangleAlive[c / 3u])
		{
			if (previous == RLDRED_NONE)
			{
				s->head[x] = following;
			}
			else
			{
				s->next[previous] = following;
			}
		}
		else
		{
			out[count++] = c / 3u;
			previous = c;
		}
		c = following;
	}

	return count;
}

// The neighbors of position x (every other corner of its live triangles), each
// once, in the order they are met, into s->ring. Uses markC.
static u32 RldRed_Ring(struct RldRedState *s, u32 x)
{
	const u32 count = RldRed_Triangles(s, x, s->listX);
	const u32 mark = RldRed_NextMark(s);
	u32 neighbors = 0;
	u32 i;
	u32 k;

	for (i = 0; i < count; i++)
	{
		const u32 *t = &s->corner[3u * s->listX[i]];

		for (k = 0; k < 3u; k++)
		{
			if ((t[k] != x) && (s->markC[t[k]] != mark))
			{
				s->markC[t[k]] = mark;
				s->ring[neighbors++] = t[k];
			}
		}
	}

	return neighbors;
}

// 1 when position x lies on an outline: on an edge with a single triangle,
// or - where x is shared by front and back of a two-sided sheet, seen as an
// edge at x with more than two triangles - on an open edge (all its
// triangles on one side of it, see the head of this file). A closed thin
// part (comb teeth) has the open edges but not the shared positions: there
// the link condition keeps the surface whole. triangles are all live
// triangles at x. Uses markB, edgeOpen, edgeCount and wing.
static int RldRed_OnOpenEdge(struct RldRedState *s, u32 x, const u32 *triangles, u32 count)
{
	const u32 mark = RldRed_NextMark(s);
	int shared = 0;
	int open = 0;
	u32 i;
	u32 k;

	for (i = 0; i < count; i++)
	{
		const u32 *t = &s->corner[3u * triangles[i]];

		for (k = 0; k < 3u; k++)
		{
			const u32 w = t[k];
			double wing[3];

			if (w == x)
			{
				continue;
			}
			RldRed_Wing(s, x, w, t[0] ^ t[1] ^ t[2] ^ x ^ w, wing);
			if (s->markB[w] != mark)
			{
				s->markB[w] = mark;
				s->edgeOpen[w] = 1u;
				s->edgeCount[w] = 0u;
				s->wing[3u * w + 0u] = wing[0];
				s->wing[3u * w + 1u] = wing[1];
				s->wing[3u * w + 2u] = wing[2];
			}
			else if (!RldRed_Within(&s->wing[3u * w], wing, RLDRED_OPEN_COS))
			{
				s->edgeOpen[w] = 0u;
			}
			s->edgeCount[w]++;
		}
	}

	for (i = 0; i < count; i++)
	{
		const u32 *t = &s->corner[3u * triangles[i]];

		for (k = 0; k < 3u; k++)
		{
			const u32 w = t[k];

			if (w == x)
			{
				continue;
			}
			if (s->edgeCount[w] == 1u)
			{
				return 1;
			}
			shared |= (s->edgeCount[w] > 2u);
			open |= (s->edgeOpen[w] != 0u);
		}
	}

	return shared && open;
}

//========================================================================================
// CANDIDATES
//========================================================================================

// The corner at v whose attributes corner `slot` of triangle tu takes when
// u -> v moves it there: of the corners at v now (the live triangles of v in
// listV, count nv, the triangles on the edge included), the one whose color
// is nearest to the corner's own; on a tie one of a triangle beside tu (it
// has one of tu's other two positions), then the first in list order.
// Colors are taken, never mixed: on one side of a seam a moved corner keeps
// that side's color, and without colors it takes a neighbor's. Returns the
// input corner (RldRedResult.sourceCorner).
static u32 RldRed_PickCorner(const struct RldRedState *s, u32 tu, u32 slot, u32 v, const u32 *listV, u32 nv)
{
	const u32 *color = s->mesh->cornerColor;
	const u32 a = s->corner[3u * tu + (slot + 1u) % 3u];
	const u32 b = s->corner[3u * tu + (slot + 2u) % 3u];
	const u32 own = (color != NULL) ? color[s->attribute[3u * tu + slot]] : 0u;
	u32 best = RLDRED_NONE;
	u32 bestDistance = 0;
	int bestBeside = 0;
	u32 j;
	u32 k;

	for (j = 0; j < nv; j++)
	{
		const u32 *w = &s->corner[3u * listV[j]];
		const int beside = RldRed_HasCorner(w, a) || RldRed_HasCorner(w, b);

		for (k = 0; k < 3u; k++)
		{
			if (w[k] == v)
			{
				const u32 candidate = s->attribute[3u * listV[j] + k];
				const u32 distance = (color != NULL) ? RldRed_ColorDistance2(own, color[candidate]) : 0u;

				if ((best == RLDRED_NONE) || (distance < bestDistance) || ((distance == bestDistance) && beside && !bestBeside))
				{
					best = candidate;
					bestDistance = distance;
					bestBeside = beside;
				}
			}
		}
	}

	return best;
}

// The nearest color to `own` that position q still has after u -> v (q is v
// or a third corner of the edge): the corners at q of the triangles that do
// not die and, at v, the colors the moved corners take. The squared
// distance; 0 when q keeps no corner. listU (count nu) and listV (count nv)
// hold the triangles of u and v; uses listX.
static u32 RldRed_NearestAfter(struct RldRedState *s, u32 u, u32 v, u32 q, u32 own, u32 nu, u32 nv)
{
	const u32 *color = s->mesh->cornerColor;
	const u32 count = (q == v) ? nv : RldRed_Triangles(s, q, s->listX);
	const u32 *list = (q == v) ? s->listV : s->listX;
	u32 best = 0;
	int any = 0;
	u32 i;
	u32 k;

	for (i = 0; i < count; i++)
	{
		const u32 *t = &s->corner[3u * list[i]];

		if (RldRed_HasCorner(t, u) && RldRed_HasCorner(t, v))
		{
			continue;  // dies
		}
		for (k = 0; k < 3u; k++)
		{
			if (t[k] == q)
			{
				const u32 distance = RldRed_ColorDistance2(own, color[s->attribute[3u * list[i] + k]]);

				best = (!any || (distance < best)) ? distance : best;
				any = 1;
			}
		}
	}
	for (i = 0; (i < nu) && (q == v); i++)
	{
		const u32 *t = &s->corner[3u * s->listU[i]];

		for (k = 0; (k < 3u) && !RldRed_HasCorner(t, v); k++)
		{
			if (t[k] == u)
			{
				const u32 distance = RldRed_ColorDistance2(own, color[RldRed_PickCorner(s, s->listU[i], k, v, s->listV, nv)]);

				best = (!any || (distance < best)) ? distance : best;
				any = 1;
			}
		}
	}

	return best;
}

// The color cost of u -> v (see RLDRED_COLOR_WEIGHT). Every triangle of u
// that is not on the edge moves: its corner at u takes the color of
// RldRed_PickCorner. Every triangle on the edge dies: each of its colors
// counts with the distance to the nearest color its position (v for the
// corner at u) still has afterwards - zero where the color goes on in a
// neighbor, the full step where the last triangle of a small patch (the
// highlight of an eye) would vanish. 0 without colors. Uses listU, listV
// and listX.
static double RldRed_ColorCost(struct RldRedState *s, u32 u, u32 v)
{
	const u32 *color = s->mesh->cornerColor;
	double sum = 0.0;
	u32 nu;
	u32 nv;
	u32 i;
	u32 k;

	if ((color == NULL) || !s->guard)
	{
		return 0.0;
	}

	nu = RldRed_Triangles(s, u, s->listU);
	nv = RldRed_Triangles(s, v, s->listV);
	for (i = 0; i < nu; i++)
	{
		const u32 tu = s->listU[i];
		const u32 *t = &s->corner[3u * tu];
		const int dies = RldRed_HasCorner(t, v);
		u32 distance = 0;
		double normal[3];

		for (k = 0; k < 3u; k++)
		{
			const u32 own = color[s->attribute[3u * tu + k]];

			if (dies)
			{
				distance += RldRed_NearestAfter(s, u, v, (t[k] == u) ? v : t[k], own, nu, nv);
			}
			else if (t[k] == u)
			{
				distance += RldRed_ColorDistance2(own, color[RldRed_PickCorner(s, tu, k, v, s->listV, nv)]);
			}
		}
		if (distance != 0u)
		{
			RldRed_Normal(s, tu, normal);
			sum += (0.5 * RldRed_Sqrt(RldRed_Dot(normal, normal)) / 3.0) * ((double)distance / RLDRED_COLOR_FULL2);
		}
	}

	return RLDRED_COLOR_WEIGHT * sum;
}

// Measures the collapse u -> v and puts it into the heap; collapses that can
// never be done (positions not live, u pinned, other groups, a group already
// at its target) are left out. 0 = out of memory.
static int RldRed_Push(struct RldRedState *s, u32 u, u32 v)
{
	struct RldRedCandidate c;

	if ((s->positionState[u] != RLDRED_POSITION_LIVE) || (s->positionState[v] != RLDRED_POSITION_LIVE) || s->pinned[u] ||
	    (s->positionGroup[u] != s->positionGroup[v]) || !RldRed_GroupAbove(s, s->positionGroup[u]))
	{
		return 1;
	}

	c.cost = RldRed_QuadricError(&s->quadric[RLDRED_QUADRIC * u], &s->quadric[RLDRED_QUADRIC * v], &s->position[3u * v]) + RldRed_ColorCost(s, u, v);
	c.u = u;
	c.v = v;
	c.stampU = s->stamp[u];
	c.stampV = s->stamp[v];
	return RldRed_HeapPush(s, &c);
}

//========================================================================================
// ONE COLLAPSE
//========================================================================================

// 1 when u -> v keeps the mesh valid (see the head of this file). Leaves the
// live triangles of u in listU (count *countU) for RldRed_Collapse.
static int RldRed_CanCollapse(struct RldRedState *s, u32 u, u32 v, u32 *countU)
{
	const u32 group = s->positionGroup[u];
	double firstWing[3] = {0.0, 0.0, 0.0};
	int edgeOpen = 1;
	u32 nu;
	u32 nv;
	u32 onEdge = 0;
	u32 moved = 0;
	double worst;
	u32 mark;
	u32 i;
	u32 j;
	u32 k;

	// Groups: both positions must be live (only one group around each) and in
	// the same group, u not pinned, and the group still above its target.
	if ((s->positionState[u] != RLDRED_POSITION_LIVE) || (s->positionState[v] != RLDRED_POSITION_LIVE) || (s->positionGroup[v] != group) ||
	    s->pinned[u] || !RldRed_GroupAbove(s, group))
	{
		return 0;
	}

	nu = RldRed_Triangles(s, u, s->listU);
	nv = RldRed_Triangles(s, v, s->listV);
	*countU = nu;

	for (i = 0; i < nu; i++)
	{
		const u32 *t = &s->corner[3u * s->listU[i]];

		if (s->mesh->triangleGroup[s->listU[i]] != group)
		{
			return 0;
		}
		if (RldRed_HasCorner(t, v))
		{
			double wing[3];

			// The edge (u, v) is open when all its triangles lie on one side of it.
			RldRed_Wing(s, u, v, t[0] ^ t[1] ^ t[2] ^ u ^ v, wing);
			if (onEdge == 0u)
			{
				firstWing[0] = wing[0];
				firstWing[1] = wing[1];
				firstWing[2] = wing[2];
			}
			else if (!RldRed_Within(firstWing, wing, RLDRED_OPEN_COS))
			{
				edgeOpen = 0;
			}
			onEdge++;
		}
		else
		{
			moved++;
		}
	}
	for (i = 0; i < nv; i++)
	{
		if (s->mesh->triangleGroup[s->listV[i]] != group)
		{
			return 0;
		}
	}

	// (u, v) is no edge any more, or v would be left without a triangle.
	if ((onEdge == 0u) || ((nv - onEdge + moved) == 0u))
	{
		return 0;
	}

	// The edges at u and at v, counted per neighbor w (markD): countU = (u, w),
	// countV = (v, w), countUV = triangles with u, v and w.
	mark = RldRed_NextMark(s);
	for (i = 0; i < nu; i++)
	{
		const u32 *t = &s->corner[3u * s->listU[i]];
		const int onUV = RldRed_HasCorner(t, v);

		for (k = 0; k < 3u; k++)
		{
			const u32 w = t[k];

			if ((w == u) || (w == v))
			{
				continue;
			}
			if (s->markD[w] != mark)
			{
				s->markD[w] = mark;
				s->countU[w] = 0u;
				s->countV[w] = 0u;
				s->countUV[w] = 0u;
			}
			s->countU[w]++;
			s->countUV[w] += onUV ? 1u : 0u;
		}
	}
	for (i = 0; i < nv; i++)
	{
		const u32 *t = &s->corner[3u * s->listV[i]];

		for (k = 0; k < 3u; k++)
		{
			const u32 w = t[k];

			if ((w == u) || (w == v) || (s->markD[w] != mark))
			{
				continue;
			}
			s->countV[w]++;
		}
	}

	// Each neighbor w of u once (markC).
	for (i = 0; i < nu; i++)
	{
		const u32 *t = &s->corner[3u * s->listU[i]];

		for (k = 0; k < 3u; k++)
		{
			const u32 w = t[k];

			if ((w == u) || (w == v) || (s->markC[w] == mark))
			{
				continue;
			}
			s->markC[w] = mark;

			if (s->countUV[w] != 0u)
			{
				// A third corner of the edge: it keeps a triangle, and the merged
				// edge (v, w) has no more triangles than the larger of the two had.
				const u32 after = (s->countU[w] - s->countUV[w]) + (s->countV[w] - s->countUV[w]);
				const u32 larger = (s->countU[w] > s->countV[w]) ? s->countU[w] : s->countV[w];

				if ((s->valence[w] == s->countUV[w]) || (after > larger))
				{
					return 0;
				}
			}
			else if (s->countV[w] != 0u)
			{
				// A common neighbor off the edge: the surface would pinch.
				return 0;
			}
		}
	}

	// Two positions on open edges may only be joined along an open edge;
	// across the inside two outlines would meet in one point.
	if (!edgeOpen && RldRed_OnOpenEdge(s, u, s->listU, nu) && RldRed_OnOpenEdge(s, v, s->listV, nv))
	{
		return 0;
	}

	// The worst shape among the triangles of u now: a moved triangle may end
	// up below RLDRED_QUALITY_MIN only when it is no worse than that (fans of
	// needles, as long n-gons split into triangles, stay reducible).
	worst = 1.0;
	for (i = 0; i < nu; i++)
	{
		const u32 *t = &s->corner[3u * s->listU[i]];
		double normal[3];
		double q;

		RldRed_Normal(s, s->listU[i], normal);
		q = RldRed_Quality2(&s->position[3u * t[0]], &s->position[3u * t[1]], &s->position[3u * t[2]], normal);
		worst = (q < worst) ? q : worst;
	}

	for (i = 0; i < nu; i++)
	{
		const u32 tu = s->listU[i];
		const u32 *t = &s->corner[3u * tu];
		u32 a = RLDRED_NONE;
		u32 b = RLDRED_NONE;
		double before[3];
		double after[3];
		double moved3[3][3];

		if (RldRed_HasCorner(t, v))
		{
			continue;
		}

		// a and b follow u in the order of the triangle.
		for (k = 0; k < 3u; k++)
		{
			if (t[k] == u)
			{
				a = t[(k + 1u) % 3u];
				b = t[(k + 2u) % 3u];
			}
		}

		// The triangle (v, a, b) must not exist already; turned round, only
		// when the two input triangles faced away from each other.
		for (j = 0; j < nv; j++)
		{
			const u32 tv = s->listV[j];
			const u32 *w = &s->corner[3u * tv];

			if (!RldRed_HasCorner(w, u) && RldRed_HasCorner(w, a) && RldRed_HasCorner(w, b))
			{
				const int sameWay = ((w[0] == v) && (w[1] == a)) || ((w[1] == v) && (w[2] == a)) || ((w[2] == v) && (w[0] == a));

				if (sameWay || !RldRed_Opposite(&s->sourceNormal[3u * tu], &s->sourceNormal[3u * tv], RLDRED_FACING_COS))
				{
					return 0;
				}
			}
		}

		// No flip, no fold, no zero area, no turning away from the input, no
		// new needle.
		for (k = 0; k < 3u; k++)
		{
			const u32 p = (t[k] == u) ? v : t[k];

			moved3[k][0] = s->position[3u * p + 0u];
			moved3[k][1] = s->position[3u * p + 1u];
			moved3[k][2] = s->position[3u * p + 2u];
		}
		RldRed_Normal(s, tu, before);
		RldRed_Cross(moved3[0], moved3[1], moved3[2], after);
		if (!RldRed_Within(before, after, RLDRED_FLIP_COS) ||
		    !RldRed_TurnBelow(&s->sourceNormal[3u * tu], after, s->strict ? RLDRED_SOURCE_COS : RLDRED_SOURCE_COS_LATE))
		{
			return 0;
		}
		{
			const double now = RldRed_Quality2(moved3[0], moved3[1], moved3[2], after);

			if ((now < (RLDRED_QUALITY_MIN * RLDRED_QUALITY_MIN)) && (now < worst))
			{
				return 0;
			}
		}
	}

	return 1;
}

// Does the checked collapse u -> v and measures the edges around v again.
// 0 = out of memory.
static int RldRed_Collapse(struct RldRedState *s, u32 u, u32 v, u32 nu, double cost)
{
	const u32 group = s->positionGroup[u];
	u32 touchedCount = 0;
	u32 moved = 0;
	u32 mark;
	u32 tail;
	u32 nv;
	u32 i;
	u32 k;

	// The moved corners take the attributes of a corner at v first, while the
	// triangles of v are still the ones before the collapse (as measured by
	// RldRed_ColorCost); the moved triangles are not among them.
	nv = RldRed_Triangles(s, v, s->listV);
	for (i = 0; i < nu; i++)
	{
		const u32 t = s->listU[i];
		const u32 *c = &s->corner[3u * t];

		if (!RldRed_HasCorner(c, v))
		{
			for (k = 0; k < 3u; k++)
			{
				if (c[k] == u)
				{
					s->attribute[3u * t + k] = RldRed_PickCorner(s, t, k, v, s->listV, nv);
				}
			}
		}
	}

	// The triangles on the edge die; the others of u now use v. Their third
	// corners are remembered: they lost a triangle and are measured again.
	mark = RldRed_NextMark(s);
	s->touched[touchedCount++] = v;
	s->markA[v] = mark;
	for (i = 0; i < nu; i++)
	{
		const u32 t = s->listU[i];
		u32 *c = &s->corner[3u * t];

		if (RldRed_HasCorner(c, v))
		{
			s->triangleAlive[t] = 0u;
			s->groupCount[group]--;
			s->total--;
			for (k = 0; k < 3u; k++)
			{
				s->valence[c[k]]--;
				if ((c[k] != u) && (s->markA[c[k]] != mark))
				{
					s->markA[c[k]] = mark;
					s->touched[touchedCount++] = c[k];
				}
			}
		}
		else
		{
			for (k = 0; k < 3u; k++)
			{
				if (c[k] == u)
				{
					c[k] = v;
				}
			}
			moved++;
		}
	}
	s->valence[v] += moved;
	s->valence[u] = 0u;

	// The corners of u join the list of v (dead ones are unlinked later).
	if (s->head[u] != RLDRED_NONE)
	{
		tail = s->head[u];
		while (s->next[tail] != RLDRED_NONE)
		{
			tail = s->next[tail];
		}
		s->next[tail] = s->head[v];
		s->head[v] = s->head[u];
		s->head[u] = RLDRED_NONE;
	}

	for (k = 0; k < RLDRED_QUADRIC; k++)
	{
		s->quadric[RLDRED_QUADRIC * v + k] += s->quadric[RLDRED_QUADRIC * u + k];
	}
	s->positionState[u] = RLDRED_POSITION_UNUSED;
	if (cost > s->maxError)
	{
		s->maxError = cost;
	}

	// Every neighbor of v changed its surroundings as well.
	{
		const u32 neighbors = RldRed_Ring(s, v);

		for (i = 0; i < neighbors; i++)
		{
			if (s->markA[s->ring[i]] != mark)
			{
				s->markA[s->ring[i]] = mark;
				s->touched[touchedCount++] = s->ring[i];
			}
		}
	}

	// New versions first, then the edges of every touched position in both
	// directions (an edge between two touched positions only once each way).
	for (i = 0; i < touchedCount; i++)
	{
		s->stamp[s->touched[i]]++;
	}
	for (i = 0; i < touchedCount; i++)
	{
		const u32 x = s->touched[i];
		const u32 neighbors = RldRed_Ring(s, x);

		for (k = 0; k < neighbors; k++)
		{
			const u32 w = s->ring[k];

			if (!RldRed_Push(s, x, w))
			{
				return 0;
			}
			if ((s->markA[w] != mark) && !RldRed_Push(s, w, x))
			{
				return 0;
			}
		}
	}

	return 1;
}

//========================================================================================
// SETUP
//========================================================================================

// Checks the input; 0 with a reason in why (never NULL here) when it cannot
// be reduced at all.
static int RldRed_CheckInput(const RldRedMesh *mesh, char *why, size_t whySize)
{
	u32 t;
	u32 k;

	if ((mesh == NULL) || (mesh->groupCount == 0u) || (mesh->groupCount > RLDRED_GROUPS_MAX))
	{
		snprintf(why, whySize, "reduce: %u groups; 1 to %u are possible.", (mesh == NULL) ? 0u : mesh->groupCount, RLDRED_GROUPS_MAX);
		return 0;
	}
	if (mesh->groupTarget == NULL)
	{
		snprintf(why, whySize, "reduce: no target per group.");
		return 0;
	}
	if ((mesh->positionCount > RLDRED_INPUT_MAX) || (mesh->triangleCount > RLDRED_INPUT_MAX))
	{
		snprintf(why, whySize, "reduce: %u positions and %u triangles; at most %u each.", mesh->positionCount, mesh->triangleCount, RLDRED_INPUT_MAX);
		return 0;
	}
	if (((mesh->positionCount != 0u) && (mesh->position == NULL)) ||
	    ((mesh->triangleCount != 0u) && ((mesh->triangle == NULL) || (mesh->triangleGroup == NULL))))
	{
		snprintf(why, whySize, "reduce: the position, triangle or group array is missing.");
		return 0;
	}

	for (t = 0; t < mesh->positionCount; t++)
	{
		for (k = 0; k < 3u; k++)
		{
			const double x = mesh->position[3u * t + k];

			// Also false for NaN.
			if (!((x >= -RLDRED_COORD_MAX) && (x <= RLDRED_COORD_MAX)))
			{
				snprintf(why, whySize, "reduce: position %u, axis %u is not finite or beyond %u units.", t, k, (u32)RLDRED_COORD_MAX);
				return 0;
			}
		}
	}

	for (t = 0; t < mesh->triangleCount; t++)
	{
		const u32 *c = &mesh->triangle[3u * t];

		for (k = 0; k < 3u; k++)
		{
			if (c[k] >= mesh->positionCount)
			{
				snprintf(why, whySize, "reduce: triangle %u uses position %u of %u.", t, c[k], mesh->positionCount);
				return 0;
			}
		}
		if ((c[0] == c[1]) || (c[1] == c[2]) || (c[0] == c[2]))
		{
			snprintf(why, whySize, "reduce: triangle %u uses position %u twice.", t, (c[0] == c[1]) ? c[0] : c[2]);
			return 0;
		}
		if (mesh->triangleGroup[t] >= mesh->groupCount)
		{
			snprintf(why, whySize, "reduce: triangle %u is in group %u of %u.", t, (u32)mesh->triangleGroup[t], mesh->groupCount);
			return 0;
		}
		for (k = 0; (k < 3u) && (mesh->cornerColor != NULL); k++)
		{
			if (mesh->cornerColor[3u * t + k] > 0x00FFFFFFu)
			{
				snprintf(why, whySize, "reduce: corner %u has the color 0x%08X; 0xRRGGBB expected.", 3u * t + k, mesh->cornerColor[3u * t + k]);
				return 0;
			}
		}
	}

	return 1;
}

// Allocates the state, builds the corner lists, the groups of the positions,
// the box positions and the quadrics. 0 = out of memory.
static int RldRed_Setup(struct RldRedState *s, const RldRedMesh *mesh, int guard)
{
	const u32 n = mesh->positionCount;
	const u32 m = mesh->triangleCount;
	const size_t np = (size_t)n + 1u;
	const size_t mp = (size_t)m + 1u;
	double lo[3] = {0.0, 0.0, 0.0};
	double hi[3] = {0.0, 0.0, 0.0};
	double area;
	int any = 0;
	u32 p;
	u32 t;
	u32 k;
	u32 g;

	memset(s, 0, sizeof(*s));
	s->mesh = mesh;
	s->n = n;
	s->m = m;
	s->heapCompactAt = RLDRED_HEAP_COMPACT_MIN;
	s->strict = 1;
	s->guard = guard;
	s->total = m;
	s->position = (double *)malloc(np * 3u * sizeof(double));
	s->quadric = (double *)calloc(np * RLDRED_QUADRIC, sizeof(double));
	s->sourceNormal = (double *)malloc(mp * 3u * sizeof(double));
	s->corner = (u32 *)malloc(mp * 3u * sizeof(u32));
	s->attribute = (u32 *)malloc(mp * 3u * sizeof(u32));
	s->triangleAlive = (u8 *)malloc(mp);
	s->head = (u32 *)malloc(np * sizeof(u32));
	s->next = (u32 *)malloc(mp * 3u * sizeof(u32));
	s->stamp = (u32 *)calloc(np, sizeof(u32));
	s->valence = (u32 *)calloc(np, sizeof(u32));
	s->positionState = (u8 *)calloc(np, 1u);
	s->positionGroup = (u8 *)calloc(np, 1u);
	s->pinned = (u8 *)calloc(np, 1u);
	s->markA = (u32 *)calloc(np, sizeof(u32));
	s->markB = (u32 *)calloc(np, sizeof(u32));
	s->markC = (u32 *)calloc(np, sizeof(u32));
	s->markD = (u32 *)calloc(np, sizeof(u32));
	s->countU = (u32 *)calloc(np, sizeof(u32));
	s->countV = (u32 *)calloc(np, sizeof(u32));
	s->countUV = (u32 *)calloc(np, sizeof(u32));
	s->edgeOpen = (u32 *)calloc(np, sizeof(u32));
	s->edgeCount = (u32 *)calloc(np, sizeof(u32));
	s->wing = (double *)calloc(np * 3u, sizeof(double));
	s->listU = (u32 *)malloc(mp * sizeof(u32));
	s->listV = (u32 *)malloc(mp * sizeof(u32));
	s->listX = (u32 *)malloc(mp * sizeof(u32));
	s->ring = (u32 *)malloc(np * sizeof(u32));
	s->touched = (u32 *)malloc(np * sizeof(u32));
	if ((s->position == NULL) || (s->quadric == NULL) || (s->sourceNormal == NULL) || (s->corner == NULL) || (s->attribute == NULL) || (s->triangleAlive == NULL) ||
	    (s->head == NULL) || (s->next == NULL) || (s->stamp == NULL) || (s->valence == NULL) || (s->positionState == NULL) ||
	    (s->positionGroup == NULL) || (s->pinned == NULL) || (s->markA == NULL) || (s->markB == NULL) || (s->markC == NULL) || (s->markD == NULL) ||
	    (s->countU == NULL) || (s->countV == NULL) || (s->countUV == NULL) || (s->edgeOpen == NULL) || (s->edgeCount == NULL) || (s->wing == NULL) ||
	    (s->listU == NULL) || (s->listV == NULL) || (s->listX == NULL) || (s->ring == NULL) || (s->touched == NULL))
	{
		return 0;
	}

	// The box of the used positions; the quadrics are built about its middle.
	for (t = 0; t < m; t++)
	{
		for (k = 0; k < 3u; k++)
		{
			const double *x = &mesh->position[3u * mesh->triangle[3u * t + k]];
			u32 a;

			for (a = 0; a < 3u; a++)
			{
				if (!any || (x[a] < lo[a]))
				{
					lo[a] = x[a];
				}
				if (!any || (x[a] > hi[a]))
				{
					hi[a] = x[a];
				}
			}
			any = 1;
		}
	}
	for (p = 0; p < n; p++)
	{
		for (k = 0; k < 3u; k++)
		{
			s->position[3u * p + k] = mesh->position[3u * p + k] - 0.5 * (lo[k] + hi[k]);
		}
		s->head[p] = RLDRED_NONE;
	}

	// Corner lists (built backwards, so each list runs in triangle order), the
	// valences and the group of each position.
	for (t = m; t-- > 0u;)
	{
		for (k = 3u; k-- > 0u;)
		{
			const u32 c = 3u * t + k;
			const u32 x = mesh->triangle[c];

			s->corner[c] = x;
			s->attribute[c] = c;
			s->next[c] = s->head[x];
			s->head[x] = c;
			s->valence[x]++;
		}
		s->triangleAlive[t] = 1u;
	}
	for (t = 0; t < m; t++)
	{
		const u8 group = mesh->triangleGroup[t];

		s->groupCount[group]++;
		for (k = 0; k < 3u; k++)
		{
			const u32 x = mesh->triangle[3u * t + k];

			if (s->positionState[x] == RLDRED_POSITION_UNUSED)
			{
				s->positionState[x] = RLDRED_POSITION_LIVE;
				s->positionGroup[x] = group;
			}
			else if (s->positionGroup[x] != group)
			{
				s->positionState[x] = RLDRED_POSITION_LOCKED;
			}
		}
	}

	// The box positions of each group: lowest and highest on every axis, the
	// lowest index on a tie.
	for (g = 0; g < mesh->groupCount; g++)
	{
		u32 lowAt[3] = {RLDRED_NONE, RLDRED_NONE, RLDRED_NONE};
		u32 highAt[3] = {RLDRED_NONE, RLDRED_NONE, RLDRED_NONE};

		for (p = 0; p < n; p++)
		{
			if ((s->positionState[p] != RLDRED_POSITION_LIVE) || (s->positionGroup[p] != g))
			{
				continue;
			}
			for (k = 0; k < 3u; k++)
			{
				const double x = s->position[3u * p + k];

				if ((lowAt[k] == RLDRED_NONE) || (x < s->position[3u * lowAt[k] + k]))
				{
					lowAt[k] = p;
				}
				if ((highAt[k] == RLDRED_NONE) || (x > s->position[3u * highAt[k] + k]))
				{
					highAt[k] = p;
				}
			}
		}
		for (k = 0; k < 3u; k++)
		{
			if (lowAt[k] != RLDRED_NONE)
			{
				s->pinned[lowAt[k]] = 1u;
				s->pinned[highAt[k]] = 1u;
			}
		}
	}

	// Face planes, weighted with the area; the shape term: the point of each
	// corner, weighted with a third of the area.
	for (t = 0; t < m; t++)
	{
		const u32 *c = &mesh->triangle[3u * t];
		double *normal = &s->sourceNormal[3u * t];
		double length;

		RldRed_Cross(&s->position[3u * c[0]], &s->position[3u * c[1]], &s->position[3u * c[2]], normal);
		length = RldRed_Sqrt(RldRed_Dot(normal, normal));
		if (!(length > 0.0))
		{
			continue;
		}
		area = 0.5 * length;

		{
			const double a = normal[0] / length;
			const double b = normal[1] / length;
			const double cc = normal[2] / length;
			const double d = -(a * s->position[3u * c[0] + 0u] + b * s->position[3u * c[0] + 1u] + cc * s->position[3u * c[0] + 2u]);

			for (k = 0; k < 3u; k++)
			{
				const double *x = &s->position[3u * c[k]];
				double *q = &s->quadric[RLDRED_QUADRIC * c[k]];
				const double shape = RLDRED_SHAPE_WEIGHT * area / 3.0;

				RldRed_AddPlane(q, a, b, cc, d, area);
				RldRed_AddPlane(q, 1.0, 0.0, 0.0, -x[0], shape);
				RldRed_AddPlane(q, 0.0, 1.0, 0.0, -x[1], shape);
				RldRed_AddPlane(q, 0.0, 0.0, 1.0, -x[2], shape);
			}
		}
	}

	// Open edges: a plane through the edge, perpendicular to the triangle, for
	// both ends - once per edge: from its triangle with the lowest index.
	// Seams and creases (edges that are not open): the same plane from every
	// triangle on the edge, each perpendicular to itself.
	for (t = 0; t < m; t++)
	{
		const u32 *c = &mesh->triangle[3u * t];
		const double *normal = &s->sourceNormal[3u * t];
		const int flat = !(RldRed_Dot(normal, normal) > 0.0);

		for (k = 0; k < 3u; k++)
		{
			const u32 a = c[k];
			const u32 b = c[(k + 1u) % 3u];
			const u32 count = RldRed_Triangles(s, a, s->listX);
			double wing[3];
			int sameSide = 1;
			int lowest = 1;
			int seam = 0;
			int crease = 0;
			u32 seamDistance = 0;
			u32 i;

			RldRed_Wing(s, a, b, c[(k + 2u) % 3u], wing);
			for (i = 0; i < count; i++)
			{
				const u32 o = s->listX[i];
				const u32 *oc = &s->corner[3u * o];
				const double *otherNormal = &s->sourceNormal[3u * o];
				double other[3];
				u32 j;

				if ((o == t) || !RldRed_HasCorner(oc, b))
				{
					continue;
				}
				RldRed_Wing(s, a, b, oc[0] ^ oc[1] ^ oc[2] ^ a ^ b, other);
				sameSide &= RldRed_Within(wing, other, RLDRED_OPEN_COS);
				lowest &= (o > t);

				// A seam: the colors of t and o differ at a or at b.
				for (j = 0; (j < 3u) && (mesh->cornerColor != NULL) && (guard >= 2); j++)
				{
					if ((oc[j] == a) || (oc[j] == b))
					{
						const u32 here = (c[0] == oc[j]) ? 0u : ((c[1] == oc[j]) ? 1u : 2u);
						const u32 distance = RldRed_ColorDistance2(mesh->cornerColor[3u * t + here], mesh->cornerColor[3u * o + j]);

						seam |= (distance > RLDRED_SEAM_COLOR2);
						seamDistance = (distance > seamDistance) ? distance : seamDistance;
					}
				}

				// A crease: the normals differ by more than 60 degrees, but the
				// two are not front and back of a sheet.
				if ((guard >= 2) && !flat && (RldRed_Dot(otherNormal, otherNormal) > 0.0) && !RldRed_Within(normal, otherNormal, RLDRED_CREASE_COS) &&
				    !RldRed_Opposite(normal, otherNormal, RLDRED_FACING_COS))
				{
					crease = 1;
				}
			}

			if ((sameSide && lowest) || (!sameSide && (seam || crease)))
			{
				const double *pa = &s->position[3u * a];
				const double *pb = &s->position[3u * b];
				const double edge[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
				const double e2 = RldRed_Dot(edge, edge);
				const double seamWeight = (seamDistance < RLDRED_SEAM_FULL2) ? (RLDRED_SEAM_WEIGHT * (double)seamDistance / (double)RLDRED_SEAM_FULL2) : RLDRED_SEAM_WEIGHT;
				const double creaseWeight = crease ? RLDRED_CREASE_WEIGHT : 0.0;
				const double weight = sameSide ? RLDRED_BOUNDARY_WEIGHT : ((seam && (seamWeight > creaseWeight)) ? seamWeight : creaseWeight);
				double side[3];
				double length;

				side[0] = edge[1] * normal[2] - edge[2] * normal[1];
				side[1] = edge[2] * normal[0] - edge[0] * normal[2];
				side[2] = edge[0] * normal[1] - edge[1] * normal[0];
				length = RldRed_Sqrt(RldRed_Dot(side, side));
				if (length > 0.0)
				{
					const double sa = side[0] / length;
					const double sb = side[1] / length;
					const double sc = side[2] / length;
					const double d = -(sa * pa[0] + sb * pa[1] + sc * pa[2]);

					RldRed_AddPlane(&s->quadric[RLDRED_QUADRIC * a], sa, sb, sc, d, weight * e2);
					RldRed_AddPlane(&s->quadric[RLDRED_QUADRIC * b], sa, sb, sc, d, weight * e2);
				}
			}
		}
	}

	return 1;
}

//========================================================================================
// THE REDUCTION
//========================================================================================

// The collapses the second phase did in the last RldRed_Reduce (0 when the
// first phase reached every target); read by the self-test. The packer runs
// one reduction at a time.
static u32 s_rldRedLateCollapses;

// Progress for Reload Studio (@progress reduce): the triangles removed so far
// against the triangles to remove. RldMk_Progress keeps the pace of the lines;
// it is called every RLDRED_PROGRESS_STEP heap entries or positions. A later
// stage (RldRed_Reduce) starts again at 0.
#define RLDRED_PROGRESS_STEP 256u

static u32 RldRed_ToRemove(const struct RldRedState *s)
{
	u32 goal = 0;
	u32 g;

	if (s->mesh->totalTarget != 0u)
	{
		return (s->m > s->mesh->totalTarget) ? (s->m - s->mesh->totalTarget) : 0u;
	}
	for (g = 0; g < s->mesh->groupCount; g++)
	{
		goal += (s->groupCount[g] > s->mesh->groupTarget[g]) ? (s->groupCount[g] - s->mesh->groupTarget[g]) : 0u;
	}
	return goal;
}

static void RldRed_Progress(const struct RldRedState *s, u32 goal)
{
	const u32 removed = s->m - s->total;

	RldMk_Progress("reduce", (removed < goal) ? removed : goal, goal);
}

void RldRed_Free(RldRedResult *result)
{
	if (result == NULL)
	{
		return;
	}
	free(result->triangle);
	free(result->sourceTriangle);
	free(result->sourceCorner);
	memset(result, 0, sizeof(*result));
}

// One reduction of the checked input; guard 2: seams, creases and color
// cost, 1: color cost only, 0: none. 1 = reached, out filled; 0 = not reached, -1 = out of memory: out
// empty, why says it.
static int RldRed_Run(const RldRedMesh *mesh, RldRedResult *out, char *why, size_t whySize, int guard)
{
	struct RldRedState s;
	u32 above = 0;
	u32 kept = 0;
	u32 goal;
	u32 pops = 0;
	u32 phase;
	u32 g;
	u32 p;
	u32 t;
	u32 i;

	if (!RldRed_Setup(&s, mesh, guard))
	{
		RldRed_FreeState(&s);
		snprintf(why, whySize, "reduce: out of memory (%u positions, %u triangles).", mesh->positionCount, mesh->triangleCount);
		return -1;
	}

	for (g = 0; g < mesh->groupCount; g++)
	{
		above += RldRed_GroupAbove(&s, g) ? 1u : 0u;
	}
	goal = RldRed_ToRemove(&s);
	RldRed_Progress(&s, goal);

	// The cheapest valid collapse, until every group is at its target or no
	// collapse is left. Two phases: when the first runs dry above a target,
	// the second goes on without the normal limit against the input triangle.
	s_rldRedLateCollapses = 0u;
	for (phase = 0; (phase < 2u) && (above != 0u); phase++)
	{
		s.strict = (phase == 0u);

		// Every edge in both directions, positions in index order.
		for (p = 0; p < s.n; p++)
		{
			const u32 neighbors = RldRed_Ring(&s, p);

			if ((p % RLDRED_PROGRESS_STEP) == 0u)
			{
				RldRed_Progress(&s, goal);
			}
			for (i = 0; i < neighbors; i++)
			{
				if (!RldRed_Push(&s, p, s.ring[i]))
				{
					RldRed_FreeState(&s);
					snprintf(why, whySize, "reduce: out of memory (%u positions, %u triangles).", mesh->positionCount, mesh->triangleCount);
					return -1;
				}
			}
		}

		while ((above != 0u) && (s.heapCount != 0u))
		{
			struct RldRedCandidate c;
			u32 nu = 0;

			RldRed_HeapPop(&s, &c);
			RldRed_MarkGuard(&s);
			if ((++pops % RLDRED_PROGRESS_STEP) == 0u)
			{
				RldRed_Progress(&s, goal);
			}
			if (!RldRed_Current(&s, &c) || !RldRed_CanCollapse(&s, c.u, c.v, &nu))
			{
				continue;
			}

			g = s.positionGroup[c.u];
			s_rldRedLateCollapses += s.strict ? 0u : 1u;
			if (!RldRed_Collapse(&s, c.u, c.v, nu, c.cost))
			{
				RldRed_FreeState(&s);
				snprintf(why, whySize, "reduce: out of memory (%u positions, %u triangles).", mesh->positionCount, mesh->triangleCount);
				return -1;
			}
			above = 0;
			for (g = 0; g < mesh->groupCount; g++)
			{
				above += RldRed_GroupAbove(&s, g) ? 1u : 0u;
			}
			if (s.heapCount >= s.heapCompactAt)
			{
				RldRed_HeapCompact(&s);
			}
		}
	}

	RldRed_Progress(&s, goal);
	if ((mesh->totalTarget != 0u) && (s.total > mesh->totalTarget))
	{
		snprintf(why, whySize, "reduce: %u triangles kept, the target is %u; no further collapse keeps the mesh valid.", s.total, mesh->totalTarget);
		RldRed_FreeState(&s);
		return 0;
	}
	for (g = 0; g < mesh->groupCount; g++)
	{
		if (RldRed_GroupAbove(&s, g))
		{
			snprintf(why, whySize, "reduce: group %u keeps %u triangles, its target is %u; no further collapse keeps the mesh valid.", g, s.groupCount[g],
			         mesh->groupTarget[g]);
			RldRed_FreeState(&s);
			return 0;
		}
	}

	for (t = 0; t < s.m; t++)
	{
		kept += s.triangleAlive[t] ? 1u : 0u;
	}
	out->triangle = (u32 *)malloc(((size_t)kept + 1u) * 3u * sizeof(u32));
	out->sourceTriangle = (u32 *)malloc(((size_t)kept + 1u) * sizeof(u32));
	out->sourceCorner = (u32 *)malloc(((size_t)kept + 1u) * 3u * sizeof(u32));
	if ((out->triangle == NULL) || (out->sourceTriangle == NULL) || (out->sourceCorner == NULL))
	{
		RldRed_Free(out);
		RldRed_FreeState(&s);
		snprintf(why, whySize, "reduce: out of memory (%u positions, %u triangles).", mesh->positionCount, mesh->triangleCount);
		return -1;
	}

	for (t = 0; t < s.m; t++)
	{
		if (s.triangleAlive[t])
		{
			out->triangle[3u * out->triangleCount + 0u] = s.corner[3u * t + 0u];
			out->triangle[3u * out->triangleCount + 1u] = s.corner[3u * t + 1u];
			out->triangle[3u * out->triangleCount + 2u] = s.corner[3u * t + 2u];
			out->sourceCorner[3u * out->triangleCount + 0u] = s.attribute[3u * t + 0u];
			out->sourceCorner[3u * out->triangleCount + 1u] = s.attribute[3u * t + 1u];
			out->sourceCorner[3u * out->triangleCount + 2u] = s.attribute[3u * t + 2u];
			out->sourceTriangle[out->triangleCount] = t;
			out->triangleCount++;
		}
	}
	out->maxError = s.maxError;

	RldRed_FreeState(&s);
	return 1;
}

//========================================================================================
// THE CACHE
//========================================================================================
//
// Reload Studio checks a model again after every change of a field, and one
// make-char run reduces the same model several times (a silent try, the
// build, and both again for "built twice"). With the cache on
// (s_rldRedCache; make-char sets it unless --no-cache) a reduced mesh is kept
// in the folder for temporary files (TEMP, else TMP) as
// rldpack-reduce-<slot>.bin and taken from there when the same input comes
// again.
//   - THE KEY is the SHA-256 of everything the result depends on: the build
//     ID of the program (another state of the code may reduce differently;
//     without a build ID that tells states apart - "unknown" or a dirty tree
//     without its hash - there is no cache), every input array, the group
//     targets and the shared budget. Unused positions are part of it too:
//     a key that changes for nothing only costs a reduction.
//   - THE FILE: "RLDREDC1", the key, the result (triangleCount, stage, the
//     collapses of the second phase, maxError, the three arrays as the
//     program holds them in memory) and the SHA-256 of all before it. It is
//     read only by the build that wrote it (the key says so), so the bytes
//     need no fixed order. A file that is missing, short, longer, of another
//     key, damaged or not plausible (an index out of range, sourceTriangle
//     not rising) counts as not there.
//   - EVERY ERROR of the cache (no folder, no memory, a file that cannot be
//     written) only means that the reduction runs as without it. A file is
//     written as <name>.part and then renamed, so a run that is stopped
//     leaves no half file under the name.
//   - RLDRED_CACHE_SLOTS files at most: the first byte of the key picks the
//     slot, a new model takes the place of an old one there. A file has
//     about 28 bytes per kept triangle (200 KB for a driver of 7200).
//   - Only reductions that reach the target are kept.
// The cache never changes a result: a hit gives the bytes the reduction of
// the same input gives in the same program. It only skips the work.

#define RLDRED_CACHE_SLOTS 16u
#define RLDRED_CACHE_MAGIC "RLDREDC1"
#define RLDRED_CACHE_HEAD (8u + 32u + 3u * 4u + 8u)

// 1 = on; make-char sets it, the self-tests leave it off (except the test of
// the cache itself).
static int s_rldRedCache;

// What the cache did, for the self-test: files read, files written.
static u32 s_rldRedCacheHits;
static u32 s_rldRedCacheWrites;

// The file of a slot; 0 = no folder for temporary files or the path too long.
static int RldRed_CachePath(const u8 *key, char *path, size_t pathSize)
{
	const char *folder = getenv("TEMP");
	size_t length;
	int written;

	if ((folder == NULL) || (folder[0] == '\0'))
	{
		folder = getenv("TMP");
	}
	if ((folder == NULL) || (folder[0] == '\0'))
	{
		return 0;
	}
	length = strlen(folder);
	written = snprintf(path, pathSize, "%s%srldpack-reduce-%02u.bin", folder, ((folder[length - 1u] == '/') || (folder[length - 1u] == '\\')) ? "" : "/",
	                   (u32)key[0] % RLDRED_CACHE_SLOTS);
	return (written > 0) && ((size_t)written < pathSize);
}

static void RldRed_CacheU32(struct Sha256 *sha, u32 value)
{
	Sha256_Update(sha, &value, sizeof(value));
}

// The key of an input that RldRed_CheckInput accepted. 0 = no build ID that
// tells states of the code apart: no cache.
static int RldRed_CacheKey(const RldRedMesh *mesh, u8 key[32])
{
	static const char id[] = CTR_NATIVE_BUILD_ID;
	const size_t idLength = strlen(id);
	struct Sha256 sha;

	if ((strcmp(id, "unknown") == 0) || ((idLength >= 6u) && (strcmp(&id[idLength - 6u], "-dirty") == 0)))
	{
		return 0;
	}

	Sha256_Init(&sha);
	Sha256_Update(&sha, RLDRED_CACHE_MAGIC, 8u);
	Sha256_Update(&sha, id, idLength + 1u);
	RldRed_CacheU32(&sha, mesh->positionCount);
	Sha256_Update(&sha, mesh->position, (size_t)mesh->positionCount * 3u * sizeof(double));
	RldRed_CacheU32(&sha, mesh->triangleCount);
	Sha256_Update(&sha, mesh->triangle, (size_t)mesh->triangleCount * 3u * sizeof(u32));
	Sha256_Update(&sha, mesh->triangleGroup, (size_t)mesh->triangleCount);
	RldRed_CacheU32(&sha, mesh->groupCount);
	Sha256_Update(&sha, mesh->groupTarget, (size_t)mesh->groupCount * sizeof(u32));
	RldRed_CacheU32(&sha, (mesh->cornerColor != NULL) ? 1u : 0u);
	if (mesh->cornerColor != NULL)
	{
		Sha256_Update(&sha, mesh->cornerColor, (size_t)mesh->triangleCount * 3u * sizeof(u32));
	}
	RldRed_CacheU32(&sha, mesh->totalTarget);
	Sha256_Final(&sha, key);
	return 1;
}

// 1 = out filled from the file of the key (free with RldRed_Free); 0 = not
// there, out empty.
static int RldRed_CacheRead(const RldRedMesh *mesh, const u8 *key, RldRedResult *out)
{
	char path[1024];
	u8 head[RLDRED_CACHE_HEAD];
	u8 digest[32];
	u8 *data = NULL;
	struct Sha256 sha;
	FILE *file;
	size_t size;
	u32 count;
	u32 late;
	u32 i;
	int ok;

	if (!RldRed_CachePath(key, path, sizeof(path)) || ((file = fopen(path, "rb")) == NULL))
	{
		return 0;
	}
	ok = (fread(head, 1u, sizeof(head), file) == sizeof(head)) && (memcmp(head, RLDRED_CACHE_MAGIC, 8u) == 0) && (memcmp(&head[8], key, 32u) == 0);
	memcpy(&count, &head[40], 4u);
	ok = ok && (count != 0u) && (count <= mesh->triangleCount);
	size = ok ? ((size_t)count * 7u * sizeof(u32) + 32u) : 0u;
	data = ok ? (u8 *)malloc(size) : NULL;
	ok = ok && (data != NULL) && (fread(data, 1u, size, file) == size) && (fgetc(file) == EOF);
	fclose(file);
	if (ok)
	{
		Sha256_Init(&sha);
		Sha256_Update(&sha, head, sizeof(head));
		Sha256_Update(&sha, data, size - 32u);
		Sha256_Final(&sha, digest);
		ok = (memcmp(digest, &data[size - 32u], 32u) == 0);
	}
	if (ok)
	{
		out->triangleCount = count;
		memcpy(&out->stage, &head[44], 4u);
		memcpy(&late, &head[48], 4u);
		memcpy(&out->maxError, &head[52], 8u);
		out->triangle = (u32 *)malloc(((size_t)count + 1u) * 3u * sizeof(u32));
		out->sourceTriangle = (u32 *)malloc(((size_t)count + 1u) * sizeof(u32));
		out->sourceCorner = (u32 *)malloc(((size_t)count + 1u) * 3u * sizeof(u32));
		ok = (out->stage < 3u) && (out->triangle != NULL) && (out->sourceTriangle != NULL) && (out->sourceCorner != NULL);
	}
	if (ok)
	{
		memcpy(out->triangle, data, (size_t)count * 3u * sizeof(u32));
		memcpy(out->sourceTriangle, &data[(size_t)count * 3u * sizeof(u32)], (size_t)count * sizeof(u32));
		memcpy(out->sourceCorner, &data[(size_t)count * 4u * sizeof(u32)], (size_t)count * 3u * sizeof(u32));
		for (i = 0; ok && (i < count); i++)
		{
			ok = (out->triangle[3u * i + 0u] < mesh->positionCount) && (out->triangle[3u * i + 1u] < mesh->positionCount) &&
			     (out->triangle[3u * i + 2u] < mesh->positionCount) && (out->sourceTriangle[i] < mesh->triangleCount) &&
			     ((i == 0u) || (out->sourceTriangle[i] > out->sourceTriangle[i - 1u])) && (out->sourceCorner[3u * i + 0u] < 3u * mesh->triangleCount) &&
			     (out->sourceCorner[3u * i + 1u] < 3u * mesh->triangleCount) && (out->sourceCorner[3u * i + 2u] < 3u * mesh->triangleCount);
		}
	}
	free(data);
	if (!ok)
	{
		RldRed_Free(out);
		return 0;
	}
	s_rldRedLateCollapses = late;
	s_rldRedCacheHits++;
	return 1;
}

// Keeps a result under its key; every failure is silent.
static void RldRed_CacheWrite(const u8 *key, const RldRedResult *r)
{
	char path[1024];
	char part[1040];
	u8 head[RLDRED_CACHE_HEAD];
	u8 digest[32];
	struct Sha256 sha;
	FILE *file;
	const size_t n = r->triangleCount;
	int ok;

	if (!RldRed_CachePath(key, path, sizeof(path)) || (snprintf(part, sizeof(part), "%s.part", path) >= (int)sizeof(part)))
	{
		return;
	}
	memcpy(head, RLDRED_CACHE_MAGIC, 8u);
	memcpy(&head[8], key, 32u);
	memcpy(&head[40], &r->triangleCount, 4u);
	memcpy(&head[44], &r->stage, 4u);
	memcpy(&head[48], &s_rldRedLateCollapses, 4u);
	memcpy(&head[52], &r->maxError, 8u);
	Sha256_Init(&sha);
	Sha256_Update(&sha, head, sizeof(head));
	Sha256_Update(&sha, r->triangle, n * 3u * sizeof(u32));
	Sha256_Update(&sha, r->sourceTriangle, n * sizeof(u32));
	Sha256_Update(&sha, r->sourceCorner, n * 3u * sizeof(u32));
	Sha256_Final(&sha, digest);

	if ((file = fopen(part, "wb")) == NULL)
	{
		return;
	}
	ok = (fwrite(head, 1u, sizeof(head), file) == sizeof(head)) && (fwrite(r->triangle, sizeof(u32), n * 3u, file) == (n * 3u)) &&
	     (fwrite(r->sourceTriangle, sizeof(u32), n, file) == n) && (fwrite(r->sourceCorner, sizeof(u32), n * 3u, file) == (n * 3u)) &&
	     (fwrite(digest, 1u, 32u, file) == 32u);
	ok = (fclose(file) == 0) && ok;
	if (ok)
	{
		remove(path);
		ok = (rename(part, path) == 0);
	}
	if (!ok)
	{
		remove(part);
		return;
	}
	s_rldRedCacheWrites++;
}

// THE NOTE BEFORE A LONG REDUCTION. The time grows a little faster than the
// triangles. Measured: make-char --check of a closed sphere with colors
// (Reload Studio's Check with Reduce to fit), the whole run, which reduces
// the same model four times (a silent try, the build, and both again for
// "built twice"): triangles after repair -> seconds; the last point is 10
// such spheres side by side. Between the points the time is interpolated,
// beyond the last one the last step goes on (a million triangles: 330 s; see
// RLDRED_INPUT_MAX for a measurement there).
static const u32 s_rldRedSlowCurve[][2] = {
	{0u, 0u}, {19036u, 4u}, {45624u, 12u}, {87352u, 26u}, {162780u, 49u}, {344604u, 110u},
};

static u32 RldRed_SlowSeconds(u32 triangles)
{
	const u32 count = (u32)(sizeof(s_rldRedSlowCurve) / sizeof(s_rldRedSlowCurve[0]));
	u32 i = 1;

	while (((i + 1u) < count) && (triangles > s_rldRedSlowCurve[i][0]))
	{
		i++;
	}
	{
		const double x0 = (double)s_rldRedSlowCurve[i - 1u][0];
		const double y0 = (double)s_rldRedSlowCurve[i - 1u][1];
		const double x1 = (double)s_rldRedSlowCurve[i][0];
		const double y1 = (double)s_rldRedSlowCurve[i][1];

		return (u32)(y0 + (y1 - y0) * ((double)triangles - x0) / (x1 - x0) + 0.5);
	}
}

// The triangles the note was said for last (0 = none): make-char reduces the
// same model in several passes, some of them silent; the note comes in the
// first one, once per model. Only with --machine (Reload Studio, which shows
// nothing else while it waits): a @msg line and the line on stderr; without
// it the self-test, whose remeshed models are that big, would print it.
static u32 s_rldRedSlowSaid;

// The triangles of the hulls tools/rldpack_remesh.inc made last (0 = none).
// A reduction of them and of the small groups kept beside them (a few dozen
// triangles, up to RLDRED_SLOW_SAME more) gets a note of its own: the author
// did not make the model that big, --remesh did.
#define RLDRED_SLOW_SAME 1000u

// The reductions of one make-char run in the curve; with the cache in use
// (on and a key: a build ID that tells states apart), the passes after the
// first read the file (THE CACHE).
#define RLDRED_SLOW_PASSES 4u

static u32 s_rldRedSlowHull;

// cached: the cache is on and has a key for this input (RldRed_Reduce).
static void RldRed_SaySlow(u32 triangles, int cached)
{
	const int remesh = (s_rldRedSlowHull != 0u) && (triangles >= s_rldRedSlowHull) && ((triangles - s_rldRedSlowHull) <= RLDRED_SLOW_SAME);
	const u32 seconds = cached ? ((RldRed_SlowSeconds(triangles) + RLDRED_SLOW_PASSES - 1u) / RLDRED_SLOW_PASSES) : RldRed_SlowSeconds(triangles);
	char text[256];
	char when[32];

	if (!s_machine || (triangles <= RLDRED_SLOW_INPUT) || (triangles == s_rldRedSlowSaid))
	{
		return;
	}
	s_rldRedSlowSaid = triangles;

	if (seconds < 120u)
	{
		snprintf(when, sizeof(when), "%u s", seconds);
	}
	else
	{
		snprintf(when, sizeof(when), "%u min", (seconds + 30u) / 60u);
	}
	if (remesh)
	{
		snprintf(text, sizeof(text), "The closed hulls give %u triangles; reducing them takes about %s.", triangles, when);
	}
	else
	{
		snprintf(text, sizeof(text), "Reducing %u triangles takes about %s; decimate the model in the modeling tool first to save time.", triangles, when);
	}
	fprintf(stderr, "info: %s\n", text);
	Rld_Say("info", "reduce-slow", NULL, "%s", text);
}

// 1 = the target is reached (every group at or below its target, or with
// totalTarget all groups together at or below it); out holds the kept
// triangles in the order of the input (free with RldRed_Free). 0 = not
// reached or the input is invalid: out is empty and why (may be NULL) says
// which group or triangle.
//
// The reduction runs in up to three stages (RldRedResult.stage): 0 guarded,
// with the seam and crease planes and the color cost; 1 with the color cost
// only; 2 unguarded, in the order of the plain measure (open edges, shape
// term, box). The guards change which collapse comes first, and the order
// decides how far the rules let a mesh go - a comb of very fine teeth, for
// one, ends above its target in stage 0. Each later stage gives up less:
// stage 1 still keeps colors from running into wedges along gradients.
// Stage 2 reaches what the plain measure reaches under the rules of this
// file; since the second phase allows 84 degrees against the input
// triangle (it allowed 134 before), fine detail that only melts by turning
// further can stay above its target where it once did not. All stages keep
// the same rules and pick the colors of the moved corners the same way.
int RldRed_Reduce(const RldRedMesh *mesh, RldRedResult *out, char *why, size_t whySize)
{
	char spare[4];
	u8 key[32];
	int cached;
	int done = 0;
	u32 stage;

	if ((why == NULL) || (whySize == 0u))
	{
		why = spare;
		whySize = sizeof(spare);
	}
	why[0] = '\0';
	if (out == NULL)
	{
		snprintf(why, whySize, "reduce: no result to fill.");
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (!RldRed_CheckInput(mesh, why, whySize))
	{
		return 0;
	}
	cached = s_rldRedCache && RldRed_CacheKey(mesh, key);
	if (cached && RldRed_CacheRead(mesh, key, out))
	{
		return 1;
	}
	RldRed_SaySlow(mesh->triangleCount, cached);

	for (stage = 0; stage < 3u; stage++)
	{
		done = RldRed_Run(mesh, out, why, whySize, 2 - (int)stage);
		if (done != 0)
		{
			break;
		}
	}
	if (done == 1)
	{
		why[0] = '\0';
		out->stage = stage;
		if (cached)
		{
			RldRed_CacheWrite(key, out);
		}
	}
	return done == 1;
}

//========================================================================================
// SELF-TEST
//========================================================================================
//
// Generated meshes only: a cube of 6 x N x N squares (closed, flat sides,
// sharp corners), the same lattice pushed onto spheres (closed, curved, up to
// 3072 triangles), an open flat grid (open outline), the same grid two-sided
// (back as an exact twin, and back split along the other diagonal as
// make-char splits a reversed quad), a fin and a bowtie (non-manifold input),
// a disc and a comb whose caps are fans of needles (long n-gons split; the
// comb only reducible in the second phase),
// groups side by side, groups that share positions and a group whose
// positions are all shared, parts that cannot be reduced, invalid input.
// Colored meshes (a two-colored sphere, a cube with a colored side and a
// stripe, a two-colored grid) and a shared budget follow (RldRed_TestColors,
// RldRed_TestShared).
// Every result is checked (RldRed_TestValid) for: targets reached, only input
// positions of the same group, sourceTriangle valid and rising, each corner
// at its input corner's position or that position gone, each source corner
// at the position of its corner and in the same group, no repeated corner,
// area above zero, the normal within 60 degrees of the input triangle's
// (within 84 where the second phase was needed), no shape worse than the
// needle limit or the worst input triangle, the box of every group the same,
// no new doubled triangle, no more edges with more than two triangles than
// the input had, and the same bytes (triangles, sourceTriangle,
// sourceCorner) from a second run.

struct RldRedTestMesh
{
	u32 positionCount;
	u32 positionCapacity;
	double *position;
	u32 triangleCount;
	u32 triangleCapacity;
	u32 *triangle;
	u8 *group;
	u32 *color;   // 3 per triangle, 0xRRGGBB (0 until a case paints them)
	int failed;   // out of memory while building
};

struct RldRedTest
{
	void (*report)(const char *line);
	int failures;
	int sourceTurn;   // RldRed_TestValid checks the 60 degrees against the input triangle (first phase only)
};

static void RldRed_TestLine(struct RldRedTest *test, int ok, const char *name, const char *format, ...)
{
	char text[320];
	char line[400];
	va_list list;

	if (ok)
	{
		snprintf(line, sizeof(line), "  ok   reduce %s", name);
	}
	else
	{
		va_start(list, format);
		vsnprintf(text, sizeof(text), format, list);
		va_end(list);
		snprintf(line, sizeof(line), "  FAIL reduce %s: %s", name, text);
		test->failures++;
	}

	if (test->report != NULL)
	{
		test->report(line);
	}
}

static void RldRed_TestFree(struct RldRedTestMesh *t)
{
	free(t->position);
	free(t->triangle);
	free(t->group);
	free(t->color);
	memset(t, 0, sizeof(*t));
}

static u32 RldRed_TestPosition(struct RldRedTestMesh *t, double x, double y, double z)
{
	if (t->positionCount == t->positionCapacity)
	{
		const u32 capacity = (t->positionCapacity < 64u) ? 64u : (2u * t->positionCapacity);
		double *grown = (double *)realloc(t->position, (size_t)capacity * 3u * sizeof(double));

		if (grown == NULL)
		{
			t->failed = 1;
			return 0u;
		}
		t->position = grown;
		t->positionCapacity = capacity;
	}

	t->position[3u * t->positionCount + 0u] = x;
	t->position[3u * t->positionCount + 1u] = y;
	t->position[3u * t->positionCount + 2u] = z;
	return t->positionCount++;
}

static void RldRed_TestTriangle(struct RldRedTestMesh *t, u32 a, u32 b, u32 c, u8 group)
{
	if (t->failed)
	{
		return;
	}
	if (t->triangleCount == t->triangleCapacity)
	{
		const u32 capacity = (t->triangleCapacity < 64u) ? 64u : (2u * t->triangleCapacity);
		u32 *grown = (u32 *)realloc(t->triangle, (size_t)capacity * 3u * sizeof(u32));
		u8 *grownGroup;
		u32 *grownColor;

		if (grown == NULL)
		{
			t->failed = 1;
			return;
		}
		t->triangle = grown;
		grownGroup = (u8 *)realloc(t->group, (size_t)capacity);
		if (grownGroup == NULL)
		{
			t->failed = 1;
			return;
		}
		t->group = grownGroup;
		grownColor = (u32 *)realloc(t->color, (size_t)capacity * 3u * sizeof(u32));
		if (grownColor == NULL)
		{
			t->failed = 1;
			return;
		}
		t->color = grownColor;
		t->triangleCapacity = capacity;
	}

	t->triangle[3u * t->triangleCount + 0u] = a;
	t->triangle[3u * t->triangleCount + 1u] = b;
	t->triangle[3u * t->triangleCount + 2u] = c;
	t->group[t->triangleCount] = group;
	t->color[3u * t->triangleCount + 0u] = 0u;
	t->color[3u * t->triangleCount + 1u] = 0u;
	t->color[3u * t->triangleCount + 2u] = 0u;
	t->triangleCount++;
}

// A cube [0, size]^3 of 6 x n x n squares, two triangles each, outward wound;
// with sphere != 0 every lattice point is pushed onto the sphere of radius
// size / 2 about the middle. Positions are shared along the cube's edges.
static void RldRed_TestCube(struct RldRedTestMesh *t, u32 n, double size, int sphere, double offset, u8 group)
{
	const u32 side = n + 1u;
	u32 *lattice = (u32 *)malloc((size_t)side * side * side * sizeof(u32));
	u32 face;
	u32 i;
	u32 j;
	u32 k;

	if (lattice == NULL)
	{
		t->failed = 1;
		return;
	}

	for (i = 0; i < side * side * side; i++)
	{
		lattice[i] = RLDRED_NONE;
	}

	for (face = 0; face < 6u; face++)
	{
		const u32 axis = face / 2u;
		const u32 level = (face % 2u) ? n : 0u;

		for (i = 0; i < n; i++)
		{
			for (j = 0; j < n; j++)
			{
				u32 corner[4];

				for (k = 0; k < 4u; k++)
				{
					u32 l[3];
					u32 index;

					l[axis] = level;
					l[(axis + 1u) % 3u] = i + ((k == 1u || k == 2u) ? 1u : 0u);
					l[(axis + 2u) % 3u] = j + ((k >= 2u) ? 1u : 0u);
					index = (l[0] * side + l[1]) * side + l[2];
					if (lattice[index] == RLDRED_NONE)
					{
						double x = size * (double)l[0] / (double)n;
						double y = size * (double)l[1] / (double)n;
						double z = size * (double)l[2] / (double)n;

						if (sphere)
						{
							const double h = 0.5 * size;
							const double dx = x - h;
							const double dy = y - h;
							const double dz = z - h;
							const double r = RldRed_Sqrt(dx * dx + dy * dy + dz * dz);

							x = h + h * dx / r;
							y = h + h * dy / r;
							z = h + h * dz / r;
						}
						lattice[index] = RldRed_TestPosition(t, x + offset, y, z);
					}
					corner[k] = lattice[index];
				}

				// (axis+1, axis+2) is right-handed about +axis: counter-clockwise
				// seen from outside on the far side, turned round on the near side.
				if (face % 2u)
				{
					RldRed_TestTriangle(t, corner[0], corner[1], corner[2], group);
					RldRed_TestTriangle(t, corner[0], corner[2], corner[3], group);
				}
				else
				{
					RldRed_TestTriangle(t, corner[0], corner[2], corner[1], group);
					RldRed_TestTriangle(t, corner[0], corner[3], corner[2], group);
				}
			}
		}
	}

	free(lattice);
}

// The kinds of grid RldRed_TestGrid makes.
#define RLDRED_TEST_ONE_SIDED 0
#define RLDRED_TEST_TWINS 1        // every triangle once more, turned round
#define RLDRED_TEST_QUAD_BACK 2    // the back as make-char splits a reversed quad

// A flat grid of n x n squares in z = 0, size x size, the front wound toward
// +z, starting at x = offset. The squares with i < n / 2 are in group, the
// others in secondGroup. A square (a, b, c, d) splits like make-char splits a
// quad (shorter diagonal, on a tie 0-2): (a, b, c) and (a, c, d); its back
// (d, c, b, a) splits into (d, c, b) and (d, b, a) - the other diagonal.
static void RldRed_TestGrid(struct RldRedTestMesh *t, u32 n, double size, double offset, u8 group, u8 secondGroup, int kind)
{
	const u32 first = t->positionCount;
	u32 i;
	u32 j;

	for (j = 0; j <= n; j++)
	{
		for (i = 0; i <= n; i++)
		{
			RldRed_TestPosition(t, offset + size * (double)i / (double)n, size * (double)j / (double)n, 0.0);
		}
	}

	for (j = 0; j < n; j++)
	{
		for (i = 0; i < n; i++)
		{
			const u32 a = first + j * (n + 1u) + i;
			const u32 b = a + 1u;
			const u32 c = a + n + 2u;
			const u32 d = a + n + 1u;
			const u8 g = (i < (n / 2u)) ? group : secondGroup;

			RldRed_TestTriangle(t, a, b, c, g);
			RldRed_TestTriangle(t, a, c, d, g);
			if (kind == RLDRED_TEST_TWINS)
			{
				RldRed_TestTriangle(t, a, c, b, g);
				RldRed_TestTriangle(t, a, d, c, g);
			}
			else if (kind == RLDRED_TEST_QUAD_BACK)
			{
				RldRed_TestTriangle(t, d, c, b, g);
				RldRed_TestTriangle(t, d, b, a, g);
			}
		}
	}
}

static RldRedMesh RldRed_TestView(const struct RldRedTestMesh *t, u32 groupCount, const u32 *targets)
{
	RldRedMesh mesh;

	mesh.positionCount = t->positionCount;
	mesh.position = t->position;
	mesh.triangleCount = t->triangleCount;
	mesh.triangle = t->triangle;
	mesh.triangleGroup = t->group;
	mesh.groupCount = groupCount;
	mesh.groupTarget = targets;
	mesh.cornerColor = NULL;
	mesh.totalTarget = 0u;
	return mesh;
}

static int RldRed_TestSameSet(const u32 *a, const u32 *b)
{
	return RldRed_HasCorner(a, b[0]) && RldRed_HasCorner(a, b[1]) && RldRed_HasCorner(a, b[2]);
}

static int RldRed_TestUsesPosition(const u32 *triangle, u32 count, u32 position)
{
	u32 i;

	for (i = 0; i < 3u * count; i++)
	{
		if (triangle[i] == position)
		{
			return 1;
		}
	}
	return 0;
}

// The edges (position pairs) used by more than two triangles.
static u32 RldRed_TestCrowdedEdges(const u32 *triangle, u32 count)
{
	u32 crowded = 0;
	u32 i;
	u32 j;
	u32 k;
	u32 l;

	for (i = 0; i < count; i++)
	{
		for (k = 0; k < 3u; k++)
		{
			const u32 a = triangle[3u * i + k];
			const u32 b = triangle[3u * i + (k + 1u) % 3u];
			u32 on = 0;
			int firstHere = 1;

			for (j = 0; j < count; j++)
			{
				const u32 *o = &triangle[3u * j];

				if (RldRed_HasCorner(o, a) && RldRed_HasCorner(o, b))
				{
					on++;
					if (j < i)
					{
						firstHere = 0;
					}
				}
			}
			for (l = 0; l < k; l++)
			{
				const u32 a2 = triangle[3u * i + l];
				const u32 b2 = triangle[3u * i + (l + 1u) % 3u];

				if (((a2 == a) && (b2 == b)) || ((a2 == b) && (b2 == a)))
				{
					firstHere = 0;
				}
			}
			crowded += ((on > 2u) && firstHere) ? 1u : 0u;
		}
	}

	return crowded;
}

// The checks every reduced result must pass; 1 = all passed, else detail says why.
static int RldRed_TestValid(const RldRedMesh *mesh, const RldRedResult *r, int sourceTurn, char *detail, size_t detailSize)
{
	u32 counts[RLDRED_GROUPS_MAX];
	double floor2;
	u32 g;
	u32 i;
	u32 j;
	u32 k;

	memset(counts, 0, sizeof(counts));
	floor2 = RLDRED_QUALITY_MIN * RLDRED_QUALITY_MIN;
	for (i = 0; i < mesh->triangleCount; i++)
	{
		const u32 *c = &mesh->triangle[3u * i];
		double normal[3];
		double q;

		RldRed_Cross(&mesh->position[3u * c[0]], &mesh->position[3u * c[1]], &mesh->position[3u * c[2]], normal);
		q = RldRed_Quality2(&mesh->position[3u * c[0]], &mesh->position[3u * c[1]], &mesh->position[3u * c[2]], normal);
		floor2 = (q < floor2) ? q : floor2;
	}

	for (i = 0; i < r->triangleCount; i++)
	{
		const u32 *c = &r->triangle[3u * i];
		const u32 source = r->sourceTriangle[i];
		const u32 *in;
		double now[3];
		double was[3];

		if ((source >= mesh->triangleCount) || ((i != 0u) && (source <= r->sourceTriangle[i - 1u])))
		{
			snprintf(detail, detailSize, "triangle %u: source %u invalid or not rising", i, source);
			return 0;
		}
		in = &mesh->triangle[3u * source];
		counts[mesh->triangleGroup[source]]++;
		if ((c[0] == c[1]) || (c[1] == c[2]) || (c[0] == c[2]))
		{
			snprintf(detail, detailSize, "triangle %u repeats a corner", i);
			return 0;
		}

		for (k = 0; k < 3u; k++)
		{
			int used = 0;

			if (c[k] >= mesh->positionCount)
			{
				snprintf(detail, detailSize, "triangle %u: position %u is not in the input", i, c[k]);
				return 0;
			}

			// The corner contract: the input corner's position, or that one is gone.
			if ((c[k] != in[k]) && RldRed_TestUsesPosition(r->triangle, r->triangleCount, in[k]))
			{
				snprintf(detail, detailSize, "triangle %u corner %u: position %u, but input position %u is still used", i, k, c[k], in[k]);
				return 0;
			}

			// The attributes come from an input corner of the same group at the
			// same position.
			if ((r->sourceCorner[3u * i + k] >= 3u * mesh->triangleCount) || (mesh->triangle[r->sourceCorner[3u * i + k]] != c[k]) ||
			    (mesh->triangleGroup[r->sourceCorner[3u * i + k] / 3u] != mesh->triangleGroup[source]))
			{
				snprintf(detail, detailSize, "triangle %u corner %u: source corner %u is not at position %u in the same group", i, k, r->sourceCorner[3u * i + k], c[k]);
				return 0;
			}

			// The corner must be a position some input triangle of the same group uses.
			for (j = 0; (j < 3u * mesh->triangleCount) && !used; j++)
			{
				used = (mesh->triangle[j] == c[k]) && (mesh->triangleGroup[j / 3u] == mesh->triangleGroup[source]);
			}
			if (!used)
			{
				snprintf(detail, detailSize, "triangle %u: position %u comes from another group", i, c[k]);
				return 0;
			}
		}

		// Area above zero; unless the second phase was needed, the normal within
		// 60 degrees of the input triangle's (a hair looser: the reduction
		// measures about the middle of the box).
		RldRed_Cross(&mesh->position[3u * c[0]], &mesh->position[3u * c[1]], &mesh->position[3u * c[2]], now);
		RldRed_Cross(&mesh->position[3u * in[0]], &mesh->position[3u * in[1]], &mesh->position[3u * in[2]], was);
		if (!(RldRed_Dot(now, now) > 0.0) || !RldRed_TurnBelow(was, now, (sourceTurn ? RLDRED_SOURCE_COS : RLDRED_SOURCE_COS_LATE) - 0.001))
		{
			snprintf(detail, detailSize, "triangle %u: no area, or turned too far from input triangle %u", i, source);
			return 0;
		}

		// No needle made: the shape is at least RLDRED_QUALITY_MIN or at least
		// that of the worst input triangle.
		{
			const double q = RldRed_Quality2(&mesh->position[3u * c[0]], &mesh->position[3u * c[1]], &mesh->position[3u * c[2]], now);

			if (q < 0.999 * floor2)
			{
				snprintf(detail, detailSize, "triangle %u: a needle (quality squared %g, limit %g)", i, q, floor2);
				return 0;
			}
		}

		// No doubled triangle, except where the input triangles were doubled
		// already or faced away from each other (two-sided sheets).
		for (j = 0; j < i; j++)
		{
			const u32 *d = &r->triangle[3u * j];
			const u32 *e = &mesh->triangle[3u * r->sourceTriangle[j]];
			double other[3];

			if (!RldRed_TestSameSet(d, c))
			{
				continue;
			}
			RldRed_Cross(&mesh->position[3u * e[0]], &mesh->position[3u * e[1]], &mesh->position[3u * e[2]], other);
			if (!RldRed_TestSameSet(e, in) && !(RldRed_Dot(other, was) < 0.0))
			{
				snprintf(detail, detailSize, "triangles %u and %u are doubled", j, i);
				return 0;
			}
		}
	}

	for (g = 0; g < mesh->groupCount; g++)
	{
		if ((mesh->totalTarget == 0u) && (counts[g] > mesh->groupTarget[g]))
		{
			snprintf(detail, detailSize, "group %u keeps %u, target %u", g, counts[g], mesh->groupTarget[g]);
			return 0;
		}
	}
	if ((mesh->totalTarget != 0u) && (r->triangleCount > mesh->totalTarget))
	{
		snprintf(detail, detailSize, "%u triangles kept, target %u", r->triangleCount, mesh->totalTarget);
		return 0;
	}

	// The box of every group stays.
	for (g = 0; g < mesh->groupCount; g++)
	{
		double box[2][2][3];
		int any[2] = {0, 0};
		u32 pass;

		memset(box, 0, sizeof(box));

		for (pass = 0; pass < 2u; pass++)
		{
			const u32 count = (pass == 0u) ? mesh->triangleCount : r->triangleCount;

			for (i = 0; i < count; i++)
			{
				const u32 source = (pass == 0u) ? i : r->sourceTriangle[i];
				const u32 *c = (pass == 0u) ? &mesh->triangle[3u * i] : &r->triangle[3u * i];

				if (mesh->triangleGroup[source] != g)
				{
					continue;
				}
				for (k = 0; k < 9u; k++)
				{
					const double x = mesh->position[3u * c[k / 3u] + k % 3u];

					if (!any[pass] || (x < box[pass][0][k % 3u]))
					{
						box[pass][0][k % 3u] = x;
					}
					if (!any[pass] || (x > box[pass][1][k % 3u]))
					{
						box[pass][1][k % 3u] = x;
					}
					if ((k % 3u) == 2u)
					{
						any[pass] = 1;
					}
				}
			}
		}
		if (any[0] && (!any[1] || (memcmp(box[0], box[1], sizeof(box[0])) != 0)))
		{
			snprintf(detail, detailSize, "group %u: the box changed", g);
			return 0;
		}
	}

	{
		const u32 before = RldRed_TestCrowdedEdges(mesh->triangle, mesh->triangleCount);
		const u32 after = RldRed_TestCrowdedEdges(r->triangle, r->triangleCount);

		if (after > before)
		{
			snprintf(detail, detailSize, "%u edges with more than two triangles, the input had %u", after, before);
			return 0;
		}
	}

	return 1;
}

// 1 when the result is a closed, consistently wound surface: every directed
// edge appears once and its reverse once.
static int RldRed_TestClosed(const RldRedResult *r)
{
	u32 i;
	u32 j;
	u32 k;
	u32 l;

	for (i = 0; i < r->triangleCount; i++)
	{
		for (k = 0; k < 3u; k++)
		{
			const u32 a = r->triangle[3u * i + k];
			const u32 b = r->triangle[3u * i + (k + 1u) % 3u];
			u32 same = 0;
			u32 reverse = 0;

			for (j = 0; j < r->triangleCount; j++)
			{
				for (l = 0; l < 3u; l++)
				{
					const u32 x = r->triangle[3u * j + l];
					const u32 y = r->triangle[3u * j + (l + 1u) % 3u];

					same += ((x == a) && (y == b)) ? 1u : 0u;
					reverse += ((x == b) && (y == a)) ? 1u : 0u;
				}
			}
			if ((same != 1u) || (reverse != 1u))
			{
				return 0;
			}
		}
	}

	return 1;
}

// Six times the signed volume enclosed by the triangles.
static double RldRed_TestVolume(const double *position, const u32 *triangle, u32 count)
{
	double volume = 0.0;
	u32 i;

	for (i = 0; i < count; i++)
	{
		const double *a = &position[3u * triangle[3u * i + 0u]];
		const double *b = &position[3u * triangle[3u * i + 1u]];
		const double *c = &position[3u * triangle[3u * i + 2u]];

		volume += a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
	}

	return volume;
}

// Twice the area projected onto z = 0: the part facing +z into up, the part
// facing -z into down (both positive).
static void RldRed_TestAreaZ(const double *position, const u32 *triangle, u32 count, double *up, double *down)
{
	u32 i;

	*up = 0.0;
	*down = 0.0;
	for (i = 0; i < count; i++)
	{
		double normal[3];

		RldRed_Cross(&position[3u * triangle[3u * i + 0u]], &position[3u * triangle[3u * i + 1u]], &position[3u * triangle[3u * i + 2u]], normal);
		if (normal[2] > 0.0)
		{
			*up += normal[2];
		}
		else
		{
			*down -= normal[2];
		}
	}
}

static int RldRed_TestNear(double value, double expected)
{
	const double drift = (value > expected) ? (value - expected) : (expected - value);

	return drift <= 1.0e-9 * ((expected > 0.0) ? expected : -expected);
}

// Runs the reduction twice; 1 when both succeeded with the same bytes and the
// first result passes RldRed_TestValid. The first result stays in r.
static int RldRed_TestRun(struct RldRedTest *test, const char *name, const RldRedMesh *mesh, RldRedResult *r)
{
	RldRedResult again;
	char why[200];
	char whyAgain[200];
	char detail[200];
	int ok;
	int okAgain;

	ok = RldRed_Reduce(mesh, r, why, sizeof(why));
	okAgain = RldRed_Reduce(mesh, &again, whyAgain, sizeof(whyAgain));
	if (!ok || !okAgain)
	{
		RldRed_TestLine(test, 0, name, "not reduced: %s", ok ? whyAgain : why);
		RldRed_Free(&again);
		return 0;
	}

	if ((again.triangleCount != r->triangleCount) || (again.stage != r->stage) || (memcmp(&again.maxError, &r->maxError, sizeof(double)) != 0) ||
	    (memcmp(again.triangle, r->triangle, (size_t)r->triangleCount * 3u * sizeof(u32)) != 0) ||
	    (memcmp(again.sourceTriangle, r->sourceTriangle, (size_t)r->triangleCount * sizeof(u32)) != 0) ||
	    (memcmp(again.sourceCorner, r->sourceCorner, (size_t)r->triangleCount * 3u * sizeof(u32)) != 0))
	{
		RldRed_TestLine(test, 0, name, "two runs differ (%u and %u triangles)", r->triangleCount, again.triangleCount);
		RldRed_Free(&again);
		return 0;
	}
	RldRed_Free(&again);

	if (!RldRed_TestValid(mesh, r, test->sourceTurn, detail, sizeof(detail)))
	{
		RldRed_TestLine(test, 0, name, "%s", detail);
		return 0;
	}

	return 1;
}

// A cube lattice of n x n squares per side pushed onto a sphere, reduced to
// target: closed, every triangle facing outward, at least 90 % of the volume
// and no more than all of it (the hull can only shrink).
static void RldRed_TestSphere(struct RldRedTest *test, u32 n, u32 target, const char *name)
{
	struct RldRedTestMesh sphere;
	RldRedResult r;
	int ok;

	memset(&sphere, 0, sizeof(sphere));
	memset(&r, 0, sizeof(r));
	RldRed_TestCube(&sphere, n, 64.0, 1, 0.0, 0u);
	if (sphere.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&sphere);
		return;
	}

	{
		const RldRedMesh mesh = RldRed_TestView(&sphere, 1u, &target);
		const double volume = RldRed_TestVolume(sphere.position, sphere.triangle, sphere.triangleCount);

		if (RldRed_TestRun(test, name, &mesh, &r))
		{
			const double kept = RldRed_TestVolume(sphere.position, r.triangle, r.triangleCount);
			u32 outward = 0;
			u32 i;

			for (i = 0; i < r.triangleCount; i++)
			{
				const double *a = &sphere.position[3u * r.triangle[3u * i + 0u]];
				const double *b = &sphere.position[3u * r.triangle[3u * i + 1u]];
				const double *c = &sphere.position[3u * r.triangle[3u * i + 2u]];
				const double middle[3] = {(a[0] + b[0] + c[0]) / 3.0 - 32.0, (a[1] + b[1] + c[1]) / 3.0 - 32.0, (a[2] + b[2] + c[2]) / 3.0 - 32.0};
				double normal[3];

				RldRed_Cross(a, b, c, normal);
				outward += (RldRed_Dot(normal, middle) > 0.0) ? 1u : 0u;
			}

			ok = (r.triangleCount <= target) && (r.triangleCount + 2u >= target) && RldRed_TestClosed(&r) && (outward == r.triangleCount) &&
			     (kept >= 0.9 * volume) && (kept <= volume);
			RldRed_TestLine(test, ok, name, "%u triangles, closed %d, %u of them outward, volume %.1f of %.1f", r.triangleCount, RldRed_TestClosed(&r), outward,
			                kept, volume);
		}
	}

	RldRed_Free(&r);
	RldRed_TestFree(&sphere);
}

// A grid of 16 x 16 squares (size 32) of the given kind reduced to target:
// the front and the back (if any) each keep the whole area and the 4 corners
// stay.
static void RldRed_TestSheet(struct RldRedTest *test, int kind, u32 target, const char *name)
{
	struct RldRedTestMesh grid;
	RldRedResult r;
	int ok;

	memset(&grid, 0, sizeof(grid));
	memset(&r, 0, sizeof(r));
	RldRed_TestGrid(&grid, 16u, 32.0, 0.0, 0u, 0u, kind);
	if (grid.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&grid);
		return;
	}

	{
		const RldRedMesh mesh = RldRed_TestView(&grid, 1u, &target);
		double upBefore;
		double downBefore;

		RldRed_TestAreaZ(grid.position, grid.triangle, grid.triangleCount, &upBefore, &downBefore);
		if (RldRed_TestRun(test, name, &mesh, &r))
		{
			const u32 corners = (u32)RldRed_TestUsesPosition(r.triangle, r.triangleCount, 0u) + (u32)RldRed_TestUsesPosition(r.triangle, r.triangleCount, 16u) +
			                    (u32)RldRed_TestUsesPosition(r.triangle, r.triangleCount, 17u * 16u) +
			                    (u32)RldRed_TestUsesPosition(r.triangle, r.triangleCount, 17u * 17u - 1u);
			double up;
			double down;

			RldRed_TestAreaZ(grid.position, r.triangle, r.triangleCount, &up, &down);
			ok = (r.triangleCount <= target) && (r.triangleCount + 4u >= target) && (corners == 4u) && RldRed_TestNear(up, upBefore) &&
			     ((kind == RLDRED_TEST_ONE_SIDED) ? (down == 0.0) : RldRed_TestNear(down, downBefore));
			RldRed_TestLine(test, ok, name, "%u triangles, %u corners, area front %.6f back %.6f of %.6f %.6f", r.triangleCount, corners, up, down, upBefore,
			                downBefore);
		}
	}

	RldRed_Free(&r);
	RldRed_TestFree(&grid);
}

// Deep reductions, as far as the rules allow: the checks of every result
// (RldRed_TestValid) and that the count went at least down to limit. kind 0:
// a sphere of 8 x 8 squares per side; 1: a strip of 32 x 1 squares; 2 and 3:
// the two-sided sheets (twins, quads) of 16 x 16 squares; 4: a cube of 8 x 8;
// 5: a sheet of 16 x 16 squares, corrugated (every other column raised).
static void RldRed_TestDeep(struct RldRedTest *test, int kind, u32 target, u32 limit, const char *name)
{
	struct RldRedTestMesh t;
	RldRedResult r;
	RldRedMesh mesh;
	char why[200];

	memset(&t, 0, sizeof(t));
	memset(&r, 0, sizeof(r));
	if ((kind == 0) || (kind == 4))
	{
		RldRed_TestCube(&t, 8u, 64.0, kind == 0, 0.0, 0u);
	}
	else if (kind == 1)
	{
		u32 i;

		for (i = 0; i <= 32u; i++)
		{
			RldRed_TestPosition(&t, 2.0 * (double)i, 0.0, 0.0);
			RldRed_TestPosition(&t, 2.0 * (double)i, 2.0, 0.0);
		}
		for (i = 0; i < 32u; i++)
		{
			RldRed_TestTriangle(&t, 2u * i, 2u * i + 2u, 2u * i + 3u, 0u);
			RldRed_TestTriangle(&t, 2u * i, 2u * i + 3u, 2u * i + 1u, 0u);
		}
	}
	else if (kind == 5)
	{
		u32 i;

		// Corrugated: every other grid column raised by 2.
		RldRed_TestGrid(&t, 16u, 32.0, 0.0, 0u, 0u, RLDRED_TEST_ONE_SIDED);
		for (i = 0; (i < t.positionCount) && !t.failed; i++)
		{
			t.position[3u * i + 2u] = ((i % 17u) % 2u) ? 2.0 : 0.0;
		}
	}
	else
	{
		RldRed_TestGrid(&t, 16u, 32.0, 0.0, 0u, 0u, (kind == 2) ? RLDRED_TEST_TWINS : RLDRED_TEST_QUAD_BACK);
	}
	if (t.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&t);
		return;
	}

	// The rules stop it somewhere: run until no collapse is left, then check
	// that result (the target is below what can be reached).
	mesh = RldRed_TestView(&t, 1u, &target);
	if (RldRed_Reduce(&mesh, &r, why, sizeof(why)))
	{
		RldRed_TestLine(test, 0, name, "target %u reached, expected to stop above it", target);
	}
	else
	{
		u32 reached = 0;

		// The count where it stopped is in why; reduce to exactly that.
		if (sscanf(why, "reduce: group 0 keeps %u", &reached) != 1)
		{
			RldRed_TestLine(test, 0, name, "why \"%s\"", why);
		}
		else
		{
			mesh = RldRed_TestView(&t, 1u, &reached);
			if (RldRed_TestRun(test, name, &mesh, &r))
			{
				RldRed_TestLine(test, r.triangleCount <= limit, name, "stopped at %u triangles, expected at most %u", r.triangleCount, limit);
			}
		}
	}

	RldRed_Free(&r);
	RldRed_TestFree(&t);
}

static void RldRed_SelfTestShapes(struct RldRedTest *test)
{
	struct RldRedTestMesh cube;
	RldRedResult r;
	u32 target;
	int ok;

	memset(&cube, 0, sizeof(cube));
	memset(&r, 0, sizeof(r));

	// The cube: 768 triangles to 100. Flat sides and sharp corners can be
	// kept exactly: the 8 corners stay, the volume stays.
	RldRed_TestCube(&cube, 8u, 64.0, 0, 0.0, 0u);
	target = 100u;
	if (!cube.failed)
	{
		const RldRedMesh mesh = RldRed_TestView(&cube, 1u, &target);
		const double volume = RldRed_TestVolume(cube.position, cube.triangle, cube.triangleCount);

		if (RldRed_TestRun(test, "cube", &mesh, &r))
		{
			const double kept = RldRed_TestVolume(cube.position, r.triangle, r.triangleCount);
			u32 corners = 0;
			u32 p;

			for (p = 0; p < cube.positionCount; p++)
			{
				const double *x = &cube.position[3u * p];
				const int isCorner = ((x[0] == 0.0) || (x[0] == 64.0)) && ((x[1] == 0.0) || (x[1] == 64.0)) && ((x[2] == 0.0) || (x[2] == 64.0));

				corners += (isCorner && RldRed_TestUsesPosition(r.triangle, r.triangleCount, p)) ? 1u : 0u;
			}
			ok = (r.triangleCount <= target) && (r.triangleCount + 2u >= target) && RldRed_TestClosed(&r) && (corners == 8u) && RldRed_TestNear(kept, volume);
			RldRed_TestLine(test, ok, "cube 768 -> 100: closed, 8 corners, same volume", "%u triangles, closed %d, %u corners, volume %.6f of %.6f",
			                r.triangleCount, RldRed_TestClosed(&r), corners, kept, volume);
		}
		RldRed_Free(&r);
	}
	else
	{
		RldRed_TestLine(test, 0, "cube", "out of memory");
	}
	RldRed_TestFree(&cube);

	// Spheres: closed, wound outward, most of the volume. The second is about
	// the size of a large model, reduced to the make-char target.
	RldRed_TestSphere(test, 8u, 200u, "sphere 768 -> 200: closed, outward, volume kept");
	RldRed_TestSphere(test, 16u, 797u, "sphere 3072 -> 797: closed, outward, volume kept");

	// Open sheets: the outline is straight, so the area must stay exactly and
	// the 4 corners must stay - one-sided, two-sided with exact twins, and
	// two-sided as make-char splits the reversed quads (the back on the other
	// diagonal: no edge has a single triangle, no back triangle is a twin).
	RldRed_TestSheet(test, RLDRED_TEST_ONE_SIDED, 60u, "open grid 512 -> 60: outline and area kept");
	RldRed_TestSheet(test, RLDRED_TEST_TWINS, 120u, "two-sided sheet, twins, 1024 -> 120: outline kept");
	RldRed_TestSheet(test, RLDRED_TEST_QUAD_BACK, 120u, "two-sided sheet, quads, 1024 -> 120: outline kept");

	// As deep as the rules allow.
	RldRed_TestDeep(test, 0, 1u, 8u, "sphere 768 as deep as allowed");
	RldRed_TestDeep(test, 1, 1u, 5u, "strip 64 as deep as allowed");
	RldRed_TestDeep(test, 2, 1u, 2u, "two-sided sheet, twins, as deep as allowed");
	RldRed_TestDeep(test, 3, 1u, 2u, "two-sided sheet, quads, as deep as allowed");
	RldRed_TestDeep(test, 4, 1u, 4u, "cube 768 as deep as allowed");
	RldRed_TestDeep(test, 5, 1u, 2u, "corrugated sheet 512 as deep as allowed");
}

// A fin: a vertical wall of 8 x 4 squares standing on the middle line of a
// flat 8 x 8 grid, sharing its positions (edges with three triangles). A
// bowtie: two such grids beside it touching at one corner position.
// A disc: a flat cylinder of n corners around (radius 32, 8 thick), each cap
// one n-gon split into a fan from its corner 0 (n - 2 needles that all share
// that corner, like a long n-gon split into triangles), the rim n quads.
// Reduced to target: closed, at least 85 % of the volume, no new needle
// (RldRed_TestValid).
static void RldRed_TestDisc(struct RldRedTest *test, u32 n, u32 target, const char *name)
{
	struct RldRedTestMesh t;
	RldRedResult r;
	double c = 1.0;
	double sn = 0.0;
	double sc = 0.0;
	double ss = 0.0;
	double term = 1.0;
	const double step = 6.283185307179586 / (double)n;
	u32 i;
	u32 k;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(&r, 0, sizeof(r));

	// The corners on the circle by rotating with a fixed step; its sine and
	// cosine by their series - no sine from the C library.
	for (k = 0; k < 12u; k++)
	{
		if ((k % 2u) == 0u)
		{
			sc += ((k % 4u) == 0u) ? term : -term;
		}
		else
		{
			ss += ((k % 4u) == 1u) ? term : -term;
		}
		term *= step / (double)(k + 1u);
	}
	for (i = 0; i < n; i++)
	{
		const double nc = c * sc - sn * ss;

		RldRed_TestPosition(&t, 32.0 * c, 32.0 * sn, 4.0);
		RldRed_TestPosition(&t, 32.0 * c, 32.0 * sn, -4.0);
		sn = sn * sc + c * ss;
		c = nc;
	}
	for (i = 2; i < n; i++)
	{
		RldRed_TestTriangle(&t, 0u, 2u * (i - 1u), 2u * i, 0u);             // top, facing +z
		RldRed_TestTriangle(&t, 1u, 2u * i + 1u, 2u * (i - 1u) + 1u, 0u);  // bottom, facing -z
	}
	for (i = 0; i < n; i++)
	{
		const u32 a = 2u * i;
		const u32 b = 2u * ((i + 1u) % n);

		RldRed_TestTriangle(&t, a + 1u, b + 1u, b, 0u);
		RldRed_TestTriangle(&t, a + 1u, b, a, 0u);
	}
	if (t.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&t);
		return;
	}

	{
		const RldRedMesh mesh = RldRed_TestView(&t, 1u, &target);
		const double volume = RldRed_TestVolume(t.position, t.triangle, t.triangleCount);

		if (RldRed_TestRun(test, name, &mesh, &r))
		{
			const double kept = RldRed_TestVolume(t.position, r.triangle, r.triangleCount);

			ok = (r.triangleCount <= target) && (r.triangleCount + 2u >= target) && RldRed_TestClosed(&r) && (kept >= 0.85 * volume) && (kept <= volume);
			RldRed_TestLine(test, ok, name, "%u triangles, closed %d, volume %.1f of %.1f", r.triangleCount, RldRed_TestClosed(&r), kept, volume);
		}
	}

	RldRed_Free(&r);
	RldRed_TestFree(&t);
}

// A comb, 8 thick: a block 64 x 16 with `teeth` teeth 24 long on its top
// edge, each a quarter of 64 / teeth wide (an eighth of a unit for 256) with
// gaps as wide. The caps are long n-gons split into triangles: the block a
// fan of needles from its lower left corner, each tooth a quad - 4104
// triangles. Teeth this fine cannot all be kept; at about 400 triangles the
// walls of the gaps must turn as the teeth melt together, which the first
// phase forbids: reached only by the second. The guarded order (creases)
// ends above 400; a later stage (RldRedResult.stage) reaches it. Checked: target reached,
// closed, the second phase did collapses, the box kept, no new needle
// (RldRed_TestValid, with the 84 degrees of the second phase).
static void RldRed_TestComb(struct RldRedTest *test, u32 teeth, u32 target, const char *name)
{
	struct RldRedTestMesh t;
	RldRedResult r;
	const u32 m = 2u * teeth;           // P_0 .. P_m on the top edge of the block
	const u32 first = 2u;               // 2D point j: 0 = BL, 1 = BR, 2 + j = P_j, 3 + m + j = T_j
	u32 *outline;
	u32 count = 0;
	u32 j;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(&r, 0, sizeof(r));
	outline = (u32 *)malloc(((size_t)2u * m + 8u) * sizeof(u32));
	if (outline == NULL)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		return;
	}

	// Each 2D point p is the position pair 2p (top, z = 4) and 2p + 1 (bottom).
	RldRed_TestPosition(&t, -32.0, 0.0, 4.0);
	RldRed_TestPosition(&t, -32.0, 0.0, -4.0);
	RldRed_TestPosition(&t, 32.0, 0.0, 4.0);
	RldRed_TestPosition(&t, 32.0, 0.0, -4.0);
	for (j = 0; j <= m; j++)
	{
		const double x = -32.0 + 64.0 * (double)j / (double)m;

		RldRed_TestPosition(&t, x, 16.0, 4.0);
		RldRed_TestPosition(&t, x, 16.0, -4.0);
	}
	for (j = 0; j < m; j++)
	{
		const double x = -32.0 + 64.0 * (double)j / (double)m;

		RldRed_TestPosition(&t, x, 40.0, 4.0);
		RldRed_TestPosition(&t, x, 40.0, -4.0);
	}

	// The caps, counter-clockwise seen from +z: the block as a fan from BL,
	// the teeth as quads.
	for (j = 0; j < m; j++)
	{
		const u32 a = 0u;
		const u32 b = first + j + 1u;
		const u32 c = first + j;

		RldRed_TestTriangle(&t, 2u * a, 2u * b, 2u * c, 0u);
		RldRed_TestTriangle(&t, 2u * a + 1u, 2u * c + 1u, 2u * b + 1u, 0u);
	}
	RldRed_TestTriangle(&t, 0u, 2u, 2u * (first + m), 0u);
	RldRed_TestTriangle(&t, 1u, 2u * (first + m) + 1u, 3u, 0u);
	for (j = 0; j < teeth; j++)
	{
		const u32 p0 = first + 2u * j;
		const u32 p1 = p0 + 1u;
		const u32 t0 = 3u + m + 2u * j;
		const u32 t1 = t0 + 1u;

		RldRed_TestTriangle(&t, 2u * p0, 2u * p1, 2u * t1, 0u);
		RldRed_TestTriangle(&t, 2u * p0, 2u * t1, 2u * t0, 0u);
		RldRed_TestTriangle(&t, 2u * p0 + 1u, 2u * t1 + 1u, 2u * p1 + 1u, 0u);
		RldRed_TestTriangle(&t, 2u * p0 + 1u, 2u * t0 + 1u, 2u * t1 + 1u, 0u);
	}

	// The rim along the outline, counter-clockwise: BL, BR, P_m, then each
	// tooth from the right: P_2k+1, T_2k+1, T_2k, P_2k.
	outline[count++] = 0u;
	outline[count++] = 1u;
	outline[count++] = first + m;
	for (j = teeth; j-- > 0u;)
	{
		outline[count++] = first + 2u * j + 1u;
		outline[count++] = 3u + m + 2u * j + 1u;
		outline[count++] = 3u + m + 2u * j;
		outline[count++] = first + 2u * j;
	}
	for (j = 0; j < count; j++)
	{
		const u32 a = 2u * outline[j];
		const u32 b = 2u * outline[(j + 1u) % count];

		RldRed_TestTriangle(&t, a + 1u, b + 1u, b, 0u);
		RldRed_TestTriangle(&t, a + 1u, b, a, 0u);
	}
	free(outline);
	if (t.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&t);
		return;
	}

	{
		const RldRedMesh mesh = RldRed_TestView(&t, 1u, &target);
		int done;

		test->sourceTurn = 0;
		done = RldRed_TestRun(test, name, &mesh, &r);
		test->sourceTurn = 1;
		if (done)
		{
			ok = (r.triangleCount <= target) && (r.triangleCount + 2u >= target) && RldRed_TestClosed(&r) && (s_rldRedLateCollapses != 0u);
			RldRed_TestLine(test, ok, name, "%u triangles, closed %d, %u collapses in the second phase, stage %u", r.triangleCount,
			                RldRed_TestClosed(&r), s_rldRedLateCollapses, r.stage);
		}
	}

	RldRed_Free(&r);
	RldRed_TestFree(&t);
}

// A ribbon: a quarter circle of radius 20, `segments` quads long and 0.5
// wide, one-sided or two-sided (kind as RldRed_TestGrid), reduced as far as
// the rules allow. It may get short, but its side facing +z stays one piece
// and is never pinched: no position on more than two outline edges.
static void RldRed_TestRibbon(struct RldRedTest *test, u32 segments, int kind, u32 limit, const char *name)
{
	struct RldRedTestMesh t;
	RldRedResult r;
	RldRedMesh mesh;
	double c = 1.0;
	double sn = 0.0;
	double sc = 0.0;
	double ss = 0.0;
	double term = 1.0;
	const double step = 1.5707963267948966 / (double)segments;
	char why[200];
	u32 target = 1u;
	u32 reached = 0;
	u32 i;
	u32 k;

	memset(&t, 0, sizeof(t));
	memset(&r, 0, sizeof(r));
	for (k = 0; k < 12u; k++)
	{
		if ((k % 2u) == 0u)
		{
			sc += ((k % 4u) == 0u) ? term : -term;
		}
		else
		{
			ss += ((k % 4u) == 1u) ? term : -term;
		}
		term *= step / (double)(k + 1u);
	}
	for (i = 0; i <= segments; i++)
	{
		const double nc = c * sc - sn * ss;

		RldRed_TestPosition(&t, 20.0 * c, 20.0 * sn, 0.0);
		RldRed_TestPosition(&t, 20.5 * c, 20.5 * sn, 0.0);
		sn = sn * sc + c * ss;
		c = nc;
	}
	for (i = 0; i < segments; i++)
	{
		const u32 a = 2u * i;
		const u32 b = a + 2u;
		const u32 cc = a + 3u;
		const u32 d = a + 1u;

		// a, b on the inner arc, d, c on the outer: (a, b, c, d) faces -z.
		RldRed_TestTriangle(&t, a, cc, b, 0u);
		RldRed_TestTriangle(&t, a, d, cc, 0u);
		if (kind == RLDRED_TEST_TWINS)
		{
			RldRed_TestTriangle(&t, a, b, cc, 0u);
			RldRed_TestTriangle(&t, a, cc, d, 0u);
		}
		else if (kind == RLDRED_TEST_QUAD_BACK)
		{
			RldRed_TestTriangle(&t, a, b, d, 0u);
			RldRed_TestTriangle(&t, b, cc, d, 0u);
		}
	}
	if (t.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&t);
		return;
	}

	mesh = RldRed_TestView(&t, 1u, &target);
	if (RldRed_Reduce(&mesh, &r, why, sizeof(why)) || (sscanf(why, "reduce: group 0 keeps %u", &reached) != 1))
	{
		RldRed_TestLine(test, 0, name, "expected to stop above 1 triangle, why \"%s\"", why);
	}
	else
	{
		RldRed_Free(&r);
		mesh = RldRed_TestView(&t, 1u, &reached);
		if (RldRed_TestRun(test, name, &mesh, &r))
		{
			u32 *label = (u32 *)calloc((size_t)r.triangleCount + 1u, sizeof(u32));
			u32 *outline = (u32 *)calloc((size_t)t.positionCount + 1u, sizeof(u32));
			u32 pieces = 0;
			u32 pinched = 0;
			u32 j;

			if ((label == NULL) || (outline == NULL))
			{
				RldRed_TestLine(test, 0, name, "out of memory");
			}
			else
			{
				// The triangles facing +z (one side): label 0 = the other side.
				for (i = 0; i < r.triangleCount; i++)
				{
					const u32 *q = &r.triangle[3u * i];
					double normal[3];

					RldRed_Cross(&t.position[3u * q[0]], &t.position[3u * q[1]], &t.position[3u * q[2]], normal);
					label[i] = (normal[2] > 0.0) ? RLDRED_NONE : 0u;
				}

				// Pieces: joined across shared edges (two shared corners).
				for (i = 0; i < r.triangleCount; i++)
				{
					int grew = 1;

					if (label[i] != RLDRED_NONE)
					{
						continue;
					}
					label[i] = ++pieces;
					while (grew)
					{
						grew = 0;
						for (j = 0; j < r.triangleCount; j++)
						{
							u32 o;

							for (o = 0; (o < r.triangleCount) && (label[j] == RLDRED_NONE); o++)
							{
								const u32 *x = &r.triangle[3u * j];
								const u32 *y = &r.triangle[3u * o];
								const u32 shared = (u32)RldRed_HasCorner(y, x[0]) + (u32)RldRed_HasCorner(y, x[1]) + (u32)RldRed_HasCorner(y, x[2]);

								if ((label[o] == pieces) && (shared == 2u))
								{
									label[j] = pieces;
									grew = 1;
								}
							}
						}
					}
				}

				// Outline edges (a single triangle of this side); a position on
				// more than two of them is a pinch.
				for (i = 0; i < r.triangleCount; i++)
				{
					u32 k;

					if (label[i] == 0u)
					{
						continue;
					}
					for (k = 0; k < 3u; k++)
					{
						const u32 ea = r.triangle[3u * i + k];
						const u32 eb = r.triangle[3u * i + (k + 1u) % 3u];
						u32 on = 0;

						for (j = 0; j < r.triangleCount; j++)
						{
							on += ((label[j] != 0u) && RldRed_HasCorner(&r.triangle[3u * j], ea) && RldRed_HasCorner(&r.triangle[3u * j], eb)) ? 1u : 0u;
						}
						if (on == 1u)
						{
							outline[ea]++;
							outline[eb]++;
						}
					}
				}
				for (j = 0; j < t.positionCount; j++)
				{
					pinched += (outline[j] > 2u) ? 1u : 0u;
				}

				RldRed_TestLine(test, (pieces == 1u) && (pinched == 0u) && (r.triangleCount <= limit), name,
				                "stopped at %u triangles (limit %u), one side in %u pieces, %u pinched positions", r.triangleCount, limit, pieces, pinched);
			}
			free(label);
			free(outline);
		}
	}

	RldRed_Free(&r);
	RldRed_TestFree(&t);
}

static void RldRed_SelfTestNonManifold(struct RldRedTest *test)
{
	struct RldRedTestMesh fin;
	struct RldRedTestMesh bowtie;
	RldRedResult r;
	u32 target;
	u32 i;
	u32 j;
	int ok;

	memset(&fin, 0, sizeof(fin));
	memset(&bowtie, 0, sizeof(bowtie));
	memset(&r, 0, sizeof(r));

	RldRed_TestGrid(&fin, 8u, 32.0, 0.0, 0u, 0u, RLDRED_TEST_ONE_SIDED);
	{
		const u32 wall = fin.positionCount;

		// Row 0 of the wall is grid line j = 4 (y = 16): positions 4 * 9 + i.
		for (j = 1; j <= 4u; j++)
		{
			for (i = 0; i <= 8u; i++)
			{
				RldRed_TestPosition(&fin, 4.0 * (double)i, 16.0, 4.0 * (double)j);
			}
		}
		for (j = 0; j < 4u; j++)
		{
			for (i = 0; i < 8u; i++)
			{
				const u32 a = (j == 0u) ? (36u + i) : (wall + (j - 1u) * 9u + i);
				const u32 b = a + 1u;
				const u32 d = wall + j * 9u + i;
				const u32 c = d + 1u;

				RldRed_TestTriangle(&fin, a, b, c, 0u);
				RldRed_TestTriangle(&fin, a, c, d, 0u);
			}
		}
	}
	target = 90u;
	if (!fin.failed)
	{
		const RldRedMesh mesh = RldRed_TestView(&fin, 1u, &target);

		ok = RldRed_TestRun(test, "fin", &mesh, &r);
		if (ok)
		{
			RldRed_TestLine(test, (r.triangleCount <= target) && (r.triangleCount + 3u >= target), "fin 192 -> 90: no new crowded edges", "%u triangles",
			                r.triangleCount);
		}
		RldRed_Free(&r);
	}
	else
	{
		RldRed_TestLine(test, 0, "fin", "out of memory");
	}

	RldRed_TestGrid(&bowtie, 8u, 32.0, 0.0, 0u, 0u, RLDRED_TEST_ONE_SIDED);
	{
		const u32 second = bowtie.positionCount;

		// The second grid lies beyond the far corner (32, 32) of the first and
		// uses that corner (position 80) as its own first corner.
		for (j = 0; j <= 8u; j++)
		{
			for (i = 0; i <= 8u; i++)
			{
				RldRed_TestPosition(&bowtie, 32.0 + 4.0 * (double)i, 32.0 + 4.0 * (double)j, 0.0);
			}
		}
		for (j = 0; j < 8u; j++)
		{
			for (i = 0; i < 8u; i++)
			{
				const u32 base = second + j * 9u + i;
				const u32 a = (base == second) ? 80u : base;
				const u32 b = base + 1u;
				const u32 c = base + 10u;
				const u32 d = base + 9u;

				RldRed_TestTriangle(&bowtie, a, b, c, 0u);
				RldRed_TestTriangle(&bowtie, a, c, d, 0u);
			}
		}
	}
	target = 60u;
	if (!bowtie.failed)
	{
		const RldRedMesh mesh = RldRed_TestView(&bowtie, 1u, &target);

		ok = RldRed_TestRun(test, "bowtie", &mesh, &r);
		if (ok)
		{
			double up;
			double down;
			double upBefore;
			double downBefore;

			RldRed_TestAreaZ(bowtie.position, bowtie.triangle, bowtie.triangleCount, &upBefore, &downBefore);
			RldRed_TestAreaZ(bowtie.position, r.triangle, r.triangleCount, &up, &down);
			RldRed_TestLine(test, (r.triangleCount <= target) && (r.triangleCount + 2u >= target) && RldRed_TestNear(up, upBefore),
			                "bowtie 256 -> 60: area kept, no new crowded edges", "%u triangles, area %.6f of %.6f", r.triangleCount, up, upBefore);
		}
		RldRed_Free(&r);
	}
	else
	{
		RldRed_TestLine(test, 0, "bowtie", "out of memory");
	}

	RldRed_TestFree(&fin);
	RldRed_TestFree(&bowtie);
}

static void RldRed_SelfTestGroups(struct RldRedTest *test)
{
	struct RldRedTestMesh three;
	struct RldRedTestMesh shared;
	struct RldRedTestMesh locked;
	RldRedResult r;
	char why[200];
	u32 targets[3];
	int ok;

	memset(&three, 0, sizeof(three));
	memset(&shared, 0, sizeof(shared));
	memset(&locked, 0, sizeof(locked));
	memset(&r, 0, sizeof(r));

	// Group 0 a cube that stays whole, group 1 a sphere beside it reduced,
	// group 2 a small cube reduced a little.
	RldRed_TestCube(&three, 4u, 32.0, 0, 0.0, 0u);
	RldRed_TestCube(&three, 8u, 64.0, 1, 100.0, 1u);
	RldRed_TestCube(&three, 2u, 16.0, 0, 200.0, 2u);
	targets[0] = 1000u;
	targets[1] = 300u;
	targets[2] = 40u;
	if (!three.failed)
	{
		const RldRedMesh mesh = RldRed_TestView(&three, 3u, targets);

		if (RldRed_TestRun(test, "groups", &mesh, &r))
		{
			u32 whole = 0;
			u32 counts[3] = {0u, 0u, 0u};
			u32 i;

			for (i = 0; i < r.triangleCount; i++)
			{
				const u32 source = r.sourceTriangle[i];

				counts[three.group[source]]++;
				if ((three.group[source] == 0u) && (memcmp(&r.triangle[3u * i], &three.triangle[3u * source], 3u * sizeof(u32)) == 0))
				{
					whole++;
				}
			}

			ok = (whole == 192u) && (counts[0] == 192u) && (counts[1] <= 300u) && (counts[1] >= 298u) && (counts[2] <= 40u) && (counts[2] >= 38u);
			RldRed_TestLine(test, ok, "groups: one kept whole, two reduced on their own", "group 0 %u (%u unchanged) of 192, group 1 %u (300), group 2 %u (40)",
			                counts[0], whole, counts[1], counts[2]);
		}
		RldRed_Free(&r);
	}
	else
	{
		RldRed_TestLine(test, 0, "groups", "out of memory");
	}

	// Two groups that share the positions of one grid line: those positions
	// are never moved and never a goal, so no triangle reaches across.
	RldRed_TestGrid(&shared, 16u, 32.0, 0.0, 0u, 1u, RLDRED_TEST_ONE_SIDED);
	targets[0] = 100u;
	targets[1] = 100u;
	if (!shared.failed)
	{
		const RldRedMesh mesh = RldRed_TestView(&shared, 2u, targets);

		if (RldRed_TestRun(test, "groups sharing positions", &mesh, &r))
		{
			double area;
			double kept;
			double none;
			u32 seam = 0;
			u32 j;

			RldRed_TestAreaZ(shared.position, shared.triangle, shared.triangleCount, &area, &none);
			RldRed_TestAreaZ(shared.position, r.triangle, r.triangleCount, &kept, &none);
			for (j = 0; j <= 16u; j++)
			{
				seam += (u32)RldRed_TestUsesPosition(r.triangle, r.triangleCount, j * 17u + 8u);
			}
			ok = (r.triangleCount <= 200u) && (seam == 17u) && RldRed_TestNear(kept, area);
			RldRed_TestLine(test, ok, "groups sharing positions: the seam stays", "%u triangles, %u of 17 seam positions, area %.6f of %.6f", r.triangleCount,
			                seam, kept, area);
		}
		RldRed_Free(&r);
	}
	else
	{
		RldRed_TestLine(test, 0, "groups sharing positions", "out of memory");
	}

	// Group 1 uses exactly the positions of group 0 (the same grid turned
	// round): all its positions are shared, so it cannot lose a triangle.
	RldRed_TestGrid(&locked, 4u, 16.0, 0.0, 0u, 0u, RLDRED_TEST_ONE_SIDED);
	if (!locked.failed)
	{
		const u32 front = locked.triangleCount;
		u32 i;

		for (i = 0; i < front; i++)
		{
			RldRed_TestTriangle(&locked, locked.triangle[3u * i + 0u], locked.triangle[3u * i + 2u], locked.triangle[3u * i + 1u], 1u);
		}
	}
	targets[0] = 32u;
	targets[1] = 20u;
	if (!locked.failed)
	{
		const RldRedMesh mesh = RldRed_TestView(&locked, 2u, targets);

		why[0] = '\0';
		ok = !RldRed_Reduce(&mesh, &r, why, sizeof(why)) && (r.triangle == NULL) && (strstr(why, "group 1 keeps 32") != NULL);
		RldRed_TestLine(test, ok, "group with every position shared: not reachable", "why \"%s\"", why);
		RldRed_Free(&r);
	}
	else
	{
		RldRed_TestLine(test, 0, "group with every position shared", "out of memory");
	}

	RldRed_TestFree(&three);
	RldRed_TestFree(&shared);
	RldRed_TestFree(&locked);
}

// 1 when the reduction refuses mesh and why contains reason.
static int RldRed_TestRefused(const RldRedMesh *mesh, const char *reason)
{
	RldRedResult r;
	char why[200];
	int refused;

	why[0] = '\0';
	refused = !RldRed_Reduce(mesh, &r, why, sizeof(why)) && (r.triangleCount == 0u) && (r.triangle == NULL) && (strstr(why, reason) != NULL);
	RldRed_Free(&r);
	return refused;
}

static void RldRed_SelfTestLimits(struct RldRedTest *test)
{
	struct RldRedTestMesh loose;
	struct RldRedTestMesh tetra;
	struct RldRedTestMesh bad;
	RldRedMesh mesh;
	RldRedResult r;
	char why[200];
	u32 target;
	u32 targets[2];
	u32 i;
	int ok;

	memset(&loose, 0, sizeof(loose));
	memset(&tetra, 0, sizeof(tetra));
	memset(&bad, 0, sizeof(bad));
	memset(&r, 0, sizeof(r));

	// Twelve parts of one triangle each: none can lose a triangle.
	for (i = 0; i < 12u; i++)
	{
		const u32 a = RldRed_TestPosition(&loose, 10.0 * (double)i, 0.0, 0.0);
		const u32 b = RldRed_TestPosition(&loose, 10.0 * (double)i + 4.0, 0.0, 0.0);
		const u32 c = RldRed_TestPosition(&loose, 10.0 * (double)i, 4.0, 0.0);

		RldRed_TestTriangle(&loose, a, b, c, 0u);
	}
	target = 6u;
	mesh = RldRed_TestView(&loose, 1u, &target);
	why[0] = '\0';
	ok = !loose.failed && (RldRed_Reduce(&mesh, &r, why, sizeof(why)) == 0) && (strstr(why, "group 0 keeps 12") != NULL) && (r.triangleCount == 0u) &&
	     (r.triangle == NULL);
	RldRed_TestLine(test, ok, "parts of one triangle: target not reachable", "result %u triangles, why \"%s\"", r.triangleCount, why);
	RldRed_Free(&r);

	// A tetrahedron is the smallest closed part: no edge can go.
	{
		const u32 a = RldRed_TestPosition(&tetra, 0.0, 0.0, 0.0);
		const u32 b = RldRed_TestPosition(&tetra, 8.0, 0.0, 0.0);
		const u32 c = RldRed_TestPosition(&tetra, 0.0, 8.0, 0.0);
		const u32 d = RldRed_TestPosition(&tetra, 0.0, 0.0, 8.0);

		RldRed_TestTriangle(&tetra, a, c, b, 0u);
		RldRed_TestTriangle(&tetra, a, b, d, 0u);
		RldRed_TestTriangle(&tetra, a, d, c, 0u);
		RldRed_TestTriangle(&tetra, b, c, d, 0u);
	}
	target = 2u;
	mesh = RldRed_TestView(&tetra, 1u, &target);
	why[0] = '\0';
	ok = !tetra.failed && (RldRed_Reduce(&mesh, &r, why, sizeof(why)) == 0) && (strstr(why, "group 0 keeps 4") != NULL);
	RldRed_TestLine(test, ok, "tetrahedron: target not reachable", "why \"%s\"", why);
	RldRed_Free(&r);

	// At the target already: the input comes back unchanged.
	target = 4u;
	mesh = RldRed_TestView(&tetra, 1u, &target);
	ok = !tetra.failed && RldRed_Reduce(&mesh, &r, why, sizeof(why)) && (r.triangleCount == 4u) && (r.maxError == 0.0) &&
	     (memcmp(r.triangle, tetra.triangle, 12u * sizeof(u32)) == 0) && (r.sourceTriangle[0] == 0u) && (r.sourceTriangle[3] == 3u);
	RldRed_TestLine(test, ok, "at the target: unchanged", "%u triangles", r.triangleCount);
	RldRed_Free(&r);

	// Invalid input. First the valid baseline, then one fault at a time, each
	// refused for its own reason.
	{
		const u32 a = RldRed_TestPosition(&bad, 0.0, 0.0, 0.0);
		const u32 b = RldRed_TestPosition(&bad, 1.0, 0.0, 0.0);
		const u32 c = RldRed_TestPosition(&bad, 0.0, 1.0, 0.0);

		RldRed_TestTriangle(&bad, a, b, c, 0u);
	}
	targets[0] = 1u;
	targets[1] = 1u;
	ok = !bad.failed;
	if (ok)
	{
		double huge;
		u32 failedAt = 0;

		mesh = RldRed_TestView(&bad, 1u, targets);
		ok = RldRed_Reduce(&mesh, &r, why, sizeof(why)) && (r.triangleCount == 1u);
		RldRed_Free(&r);
		failedAt = ok ? 0u : 1u;

		bad.group[0] = 1u;
		ok = ok && RldRed_TestRefused(&mesh, "is in group 1 of 1");
		bad.group[0] = 0u;
		failedAt = (ok || failedAt) ? failedAt : 2u;

		bad.triangle[2] = 3u;
		ok = ok && RldRed_TestRefused(&mesh, "uses position 3 of 3");
		failedAt = (ok || failedAt) ? failedAt : 3u;

		bad.triangle[2] = 0u;
		ok = ok && RldRed_TestRefused(&mesh, "twice");
		bad.triangle[2] = 2u;
		failedAt = (ok || failedAt) ? failedAt : 4u;

		mesh = RldRed_TestView(&bad, 9u, targets);
		ok = ok && RldRed_TestRefused(&mesh, "9 groups");
		mesh = RldRed_TestView(&bad, 1u, NULL);
		ok = ok && RldRed_TestRefused(&mesh, "no target");
		mesh = RldRed_TestView(&bad, 1u, targets);
		failedAt = (ok || failedAt) ? failedAt : 5u;

		// An infinite coordinate (made at run time), not a number, too far.
		huge = bad.position[3] * 1.0e300;
		huge *= 1.0e300;
		bad.position[4] = huge;
		ok = ok && RldRed_TestRefused(&mesh, "position 1, axis 1 is not finite");
		bad.position[4] = huge - huge;
		ok = ok && RldRed_TestRefused(&mesh, "position 1, axis 1 is not finite");
		bad.position[4] = 4.0 * RLDRED_COORD_MAX;
		ok = ok && RldRed_TestRefused(&mesh, "position 1, axis 1 is not finite");
		bad.position[4] = 0.0;
		failedAt = (ok || failedAt) ? failedAt : 6u;

		RldRed_TestLine(test, ok, "invalid input: each fault refused for its reason", "step %u failed", failedAt);
	}
	else
	{
		RldRed_TestLine(test, 0, "invalid input", "out of memory");
	}

	RldRed_TestFree(&loose);
	RldRed_TestFree(&tetra);
	RldRed_TestFree(&bad);
}

// Colored meshes. kind 0: the sphere of 16 x 16 squares per side (3072
// triangles), the half z >= 32 white, the other blue - a seam along the
// equator, on a smooth surface. kind 1: the cube of 8 x 8 squares per side
// (768), the side x = 64 red, on the side z = 64 a green stripe 16 < x < 32,
// the rest grey - seams on creases and across a flat side. kind 2: the flat
// open grid of 16 x 16 squares (512), the half x < 16 one color, the other
// half another - a seam and nothing else. Every triangle has one color at
// all three corners. Reduced to target, checked: no kept triangle mixes two
// colors (its three source corners have one color), each color stays on its
// side (the corners of a white triangle at z >= 32, a red one at x = 64, a
// green one at z = 64 between x = 16 and 32, ...), and on the cube and the
// grid, whose seams are straight, every color keeps its area exactly.
// Plus RldRed_TestValid: every source corner at the position of its corner.
#define RLDRED_TEST_WHITE 0xE0E0E0u
#define RLDRED_TEST_BLUE 0x2040C0u
#define RLDRED_TEST_RED 0xC02020u
#define RLDRED_TEST_GREEN 0x20A040u
#define RLDRED_TEST_GREY 0x808080u

// The color of triangle i of a colored test mesh (kind as above), from the
// positions of its corners.
static u32 RldRed_TestPaint(const struct RldRedTestMesh *t, int kind, const u32 *c)
{
	const double *a = &t->position[3u * c[0]];
	const double *b = &t->position[3u * c[1]];
	const double *d = &t->position[3u * c[2]];

	if (kind == 0)
	{
		return ((a[2] + b[2] + d[2]) >= 96.0) ? RLDRED_TEST_WHITE : RLDRED_TEST_BLUE;
	}
	if (kind == 1)
	{
		const double x = (a[0] + b[0] + d[0]) / 3.0;

		if ((a[0] == 64.0) && (b[0] == 64.0) && (d[0] == 64.0))
		{
			return RLDRED_TEST_RED;
		}
		if ((a[2] == 64.0) && (b[2] == 64.0) && (d[2] == 64.0) && (x > 16.0) && (x < 32.0))
		{
			return RLDRED_TEST_GREEN;
		}
		return RLDRED_TEST_GREY;
	}
	return ((a[0] + b[0] + d[0]) < 48.0) ? RLDRED_TEST_RED : RLDRED_TEST_BLUE;
}

// 1 when the corner at x lies where color may be (kind as above).
static int RldRed_TestColorPlace(int kind, u32 color, const double *x)
{
	if (kind == 0)
	{
		return (color == RLDRED_TEST_WHITE) ? (x[2] >= 32.0) : (x[2] <= 32.0);
	}
	if (kind == 1)
	{
		if (color == RLDRED_TEST_RED)
		{
			return x[0] == 64.0;
		}
		if (color == RLDRED_TEST_GREEN)
		{
			return (x[2] == 64.0) && (x[0] >= 16.0) && (x[0] <= 32.0);
		}
		return 1;
	}
	return (color == RLDRED_TEST_RED) ? (x[0] <= 16.0) : (x[0] >= 16.0);
}

static void RldRed_TestColors(struct RldRedTest *test, int kind, u32 target, const char *name)
{
	struct RldRedTestMesh t;
	RldRedResult r;
	RldRedMesh mesh;
	u32 i;

	memset(&t, 0, sizeof(t));
	memset(&r, 0, sizeof(r));
	if (kind == 2)
	{
		RldRed_TestGrid(&t, 16u, 32.0, 0.0, 0u, 0u, RLDRED_TEST_ONE_SIDED);
	}
	else
	{
		RldRed_TestCube(&t, (kind == 0) ? 16u : 8u, 64.0, kind == 0, 0.0, 0u);
	}
	if (t.failed)
	{
		RldRed_TestLine(test, 0, name, "out of memory");
		RldRed_TestFree(&t);
		return;
	}
	for (i = 0; i < t.triangleCount; i++)
	{
		const u32 color = RldRed_TestPaint(&t, kind, &t.triangle[3u * i]);

		t.color[3u * i + 0u] = color;
		t.color[3u * i + 1u] = color;
		t.color[3u * i + 2u] = color;
	}

	mesh = RldRed_TestView(&t, 1u, &target);
	mesh.cornerColor = t.color;
	if (RldRed_TestRun(test, name, &mesh, &r))
	{
		// Twice the area of each color before and after: red, green, the
		// others (compared on the cube and the grid).
		double area[2][3];
		u32 mixed = 0;
		u32 misplaced = 0;
		u32 pass;
		int ok;

		memset(area, 0, sizeof(area));
		for (pass = 0; pass < 2u; pass++)
		{
			const u32 count = (pass == 0u) ? t.triangleCount : r.triangleCount;

			for (i = 0; i < count; i++)
			{
				const u32 *c = (pass == 0u) ? &t.triangle[3u * i] : &r.triangle[3u * i];
				const u32 color = (pass == 0u) ? t.color[3u * i] : t.color[r.sourceCorner[3u * i]];
				double normal[3];
				u32 k;

				RldRed_Cross(&t.position[3u * c[0]], &t.position[3u * c[1]], &t.position[3u * c[2]], normal);
				area[pass][(color == RLDRED_TEST_RED) ? 0 : ((color == RLDRED_TEST_GREEN) ? 1 : 2)] += RldRed_Sqrt(RldRed_Dot(normal, normal));
				if (pass == 0u)
				{
					continue;
				}
				for (k = 0; k < 3u; k++)
				{
					const u32 corner = t.color[r.sourceCorner[3u * i + k]];

					mixed += ((k != 0u) && (corner != color)) ? 1u : 0u;
					misplaced += RldRed_TestColorPlace(kind, corner, &t.position[3u * c[k]]) ? 0u : 1u;
				}
			}
		}

		ok = (r.triangleCount <= target) && (r.triangleCount + 4u >= target) && (mixed == 0u) && (misplaced == 0u) && (r.stage == 0u);
		if (kind != 2)
		{
			ok = ok && RldRed_TestClosed(&r);
		}
		if (kind != 0)
		{
			ok = ok && RldRed_TestNear(area[1][0], area[0][0]) && RldRed_TestNear(area[1][1], area[0][1]) && RldRed_TestNear(area[1][2], area[0][2]);
		}
		RldRed_TestLine(test, ok, name, "%u triangles, %u mixed corners, %u corners on the wrong side, area red %.6f of %.6f, green %.6f of %.6f, stage %u",
		                r.triangleCount, mixed, misplaced, 0.5 * area[1][0], 0.5 * area[0][0], 0.5 * area[1][1], 0.5 * area[0][1], r.stage);
	}

	RldRed_Free(&r);
	RldRed_TestFree(&t);
}

// A shared budget (totalTarget): group 0 the sphere of 8 x 8 squares per
// side (768 triangles, curved: every collapse costs), group 1 beside it the
// flat grid of 16 x 16 squares (512, flat: collapses nearly free), together
// 1280 reduced to 700 with the floors 300 and 60. Checked: the total reached,
// no group further below its floor than its last collapse can take it (the
// triangles on an edge less one: here at most 1, up to 3 on two-sided
// sheets),
// and the flat grid gave up a larger part than the sphere. Then floors that
// leave no room (700 and 500): refused, naming the total.
static void RldRed_TestShared(struct RldRedTest *test)
{
	struct RldRedTestMesh t;
	RldRedResult r;
	RldRedMesh mesh;
	char why[200];
	u32 floors[2];
	u32 counts[2] = {0u, 0u};
	u32 i;
	int ok;

	memset(&t, 0, sizeof(t));
	memset(&r, 0, sizeof(r));
	RldRed_TestCube(&t, 8u, 64.0, 1, 0.0, 0u);
	RldRed_TestGrid(&t, 16u, 32.0, 100.0, 1u, 1u, RLDRED_TEST_ONE_SIDED);
	if (t.failed)
	{
		RldRed_TestLine(test, 0, "shared budget", "out of memory");
		RldRed_TestFree(&t);
		return;
	}

	floors[0] = 300u;
	floors[1] = 60u;
	mesh = RldRed_TestView(&t, 2u, floors);
	mesh.totalTarget = 700u;
	if (RldRed_TestRun(test, "shared budget", &mesh, &r))
	{
		for (i = 0; i < r.triangleCount; i++)
		{
			counts[t.group[r.sourceTriangle[i]]]++;
		}
		ok = (r.triangleCount <= 700u) && (r.triangleCount >= 698u) && ((counts[0] + 1u) >= floors[0]) && ((counts[1] + 1u) >= floors[1]) &&
		     (r.stage == 0u) &&
		     ((u64)counts[1] * 768u < (u64)counts[0] * 512u);
		RldRed_TestLine(test, ok, "shared budget 1280 -> 700: the cheap group gives more", "%u triangles: sphere %u of 768 (floor 300), grid %u of 512 (floor 60)",
		                r.triangleCount, counts[0], counts[1]);
	}
	RldRed_Free(&r);

	floors[0] = 700u;
	floors[1] = 500u;
	why[0] = '\0';
	ok = !RldRed_Reduce(&mesh, &r, why, sizeof(why)) && (r.triangle == NULL) && (strstr(why, "the target is 700") != NULL);
	RldRed_TestLine(test, ok, "shared budget, floors leave no room: not reachable", "why \"%s\"", why);
	RldRed_Free(&r);

	RldRed_TestFree(&t);
}

// The number of failed cases; 0 = passed. Each case reports one line (no
// line break at the end) through report, which may be NULL.
// The cache (on only here): the sphere of 16 x 16 squares per side (3072
// triangles) reduced to 400 without the cache, then twice with it - the
// first run writes the file, the second reads it - then with the file
// damaged (one byte changed) and cut short: each time the same result as
// without the cache, and the bad files are not taken. Without a build ID
// that tells states of the code apart there is no cache; the case says so.
static int RldRed_TestSame(const RldRedResult *a, const RldRedResult *b)
{
	const size_t n = a->triangleCount;

	return (a->triangleCount == b->triangleCount) && (a->stage == b->stage) && (memcmp(&a->maxError, &b->maxError, sizeof(double)) == 0) &&
	       (memcmp(a->triangle, b->triangle, n * 3u * sizeof(u32)) == 0) && (memcmp(a->sourceTriangle, b->sourceTriangle, n * sizeof(u32)) == 0) &&
	       (memcmp(a->sourceCorner, b->sourceCorner, n * 3u * sizeof(u32)) == 0);
}

static void RldRed_TestCache(struct RldRedTest *test)
{
	struct RldRedTestMesh t;
	RldRedResult plain;
	RldRedResult r;
	RldRedMesh mesh;
	char why[200];
	char path[1024];
	u8 key[32];
	const u32 target = 400u;
	const int before = s_rldRedCache;
	u32 hits;
	u32 writes;
	int ok;
	int i;

	memset(&t, 0, sizeof(t));
	memset(&plain, 0, sizeof(plain));
	memset(&r, 0, sizeof(r));
	RldRed_TestCube(&t, 16u, 64.0, 1, 0.0, 0u);
	mesh = RldRed_TestView(&t, 1u, &target);
	if (t.failed || !RldRed_Reduce(&mesh, &plain, why, sizeof(why)))
	{
		RldRed_TestLine(test, 0, "cache", "no result without the cache: %s", t.failed ? "out of memory" : why);
		RldRed_Free(&plain);
		RldRed_TestFree(&t);
		return;
	}
	if (!RldRed_CacheKey(&mesh, key) || !RldRed_CachePath(key, path, sizeof(path)))
	{
		RldRed_TestLine(test, 1, "cache: off without a build ID that tells states apart or without TEMP", "%s", CTR_NATIVE_BUILD_ID);
		RldRed_Free(&plain);
		RldRed_TestFree(&t);
		return;
	}

	s_rldRedCache = 1;
	remove(path);
	hits = s_rldRedCacheHits;
	writes = s_rldRedCacheWrites;
	ok = RldRed_Reduce(&mesh, &r, why, sizeof(why)) && RldRed_TestSame(&plain, &r) && (s_rldRedCacheHits == hits) && (s_rldRedCacheWrites == writes + 1u);
	RldRed_Free(&r);
	ok = ok && RldRed_Reduce(&mesh, &r, why, sizeof(why)) && RldRed_TestSame(&plain, &r) && (s_rldRedCacheHits == hits + 1u) &&
	     (s_rldRedCacheWrites == writes + 1u);
	RldRed_Free(&r);
	RldRed_TestLine(test, ok, "cache: written once, then read - the same bytes as without it", "hits %u, writes %u", s_rldRedCacheHits - hits,
	                s_rldRedCacheWrites - writes);

	// Damaged: one byte in the middle of the result, then the file cut short.
	for (i = 0; i < 2; i++)
	{
		FILE *file = fopen(path, "r+b");
		long size = 0;

		ok = (file != NULL);
		if (ok)
		{
			ok = (fseek(file, 0, SEEK_END) == 0) && ((size = ftell(file)) > 100);
			if (ok && (i == 0))
			{
				int byte;

				ok = (fseek(file, size / 2, SEEK_SET) == 0) && ((byte = fgetc(file)) != EOF) && (fseek(file, size / 2, SEEK_SET) == 0) &&
				     (fputc(byte ^ 0xFF, file) != EOF);
			}
			fclose(file);
		}
		if (ok && (i == 1))
		{
			u8 *data = (u8 *)malloc((size_t)size);

			file = fopen(path, "rb");
			ok = (data != NULL) && (file != NULL) && (fread(data, 1u, (size_t)size, file) == (size_t)size);
			if (file != NULL)
			{
				fclose(file);
			}
			file = ok ? fopen(path, "wb") : NULL;
			ok = ok && (file != NULL) && (fwrite(data, 1u, (size_t)size - 1u, file) == (size_t)size - 1u);
			if (file != NULL)
			{
				fclose(file);
			}
			free(data);
		}
		hits = s_rldRedCacheHits;
		ok = ok && RldRed_Reduce(&mesh, &r, why, sizeof(why)) && RldRed_TestSame(&plain, &r) && (s_rldRedCacheHits == hits);
		RldRed_Free(&r);
		RldRed_TestLine(test, ok, (i == 0) ? "cache: a damaged file is not taken, the result the same" : "cache: a file cut short is not taken, the result the same",
		                "hits %u", s_rldRedCacheHits - hits);
	}

	remove(path);
	s_rldRedCache = before;
	RldRed_Free(&plain);
	RldRed_TestFree(&t);
}

int RldRed_SelfTest(void (*report)(const char *line))
{
	struct RldRedTest test;

	test.report = report;
	test.failures = 0;
	test.sourceTurn = 1;
	RldRed_SelfTestShapes(&test);
	RldRed_SelfTestNonManifold(&test);
	RldRed_TestDisc(&test, 1024u, 797u, "disc of 1024 corners, caps as fans of needles, 4092 -> 797");
	RldRed_TestComb(&test, 256u, 400u, "comb of 256 teeth, caps as fans of needles, 4104 -> 400 (second phase)");
	RldRed_TestRibbon(&test, 32u, RLDRED_TEST_ONE_SIDED, 8u, "ribbon, one-sided, as deep as allowed: never pinched");
	RldRed_TestRibbon(&test, 32u, RLDRED_TEST_TWINS, 8u, "ribbon, two-sided twins, as deep as allowed: never pinched");
	RldRed_TestRibbon(&test, 32u, RLDRED_TEST_QUAD_BACK, 8u, "ribbon, two-sided quads, as deep as allowed: never pinched");
	RldRed_SelfTestGroups(&test);
	RldRed_SelfTestLimits(&test);
	RldRed_TestColors(&test, 0, 400u, "two-colored sphere 3072 -> 400: the seam stays sharp, no color on the wrong side");
	RldRed_TestColors(&test, 1, 100u, "cube with a red side and a green stripe 768 -> 100: colors and their areas kept");
	RldRed_TestColors(&test, 2, 60u, "two-colored open grid 512 -> 60: the seam stays, both areas kept");
	RldRed_TestShared(&test);
	RldRed_TestCache(&test);
	return test.failures;
}
