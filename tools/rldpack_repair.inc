// rldpack_repair.inc - mesh repair for make-char: weld, clean, orient, close small holes
//
// Part of the packer, never of the game. Self-contained: it needs only the C
// standard library (stdlib, string, stdio, stdarg) and the types u8, u32 and
// s64 of tools/rldpack.c, so it can be included anywhere after those.
//
// WHY. The game draws a driver with back face culling: a triangle is seen
// from its front only. Models from modelling programs often are not ready for
// that - corners that should be one point are split (export rounding, seams),
// some faces are turned the wrong way, small holes let the eye see through
// the model, and triangles without area or doubled triangles waste draw
// memory. RldRep_Repair fixes what can be fixed without inventing shape: it
// never makes a new position and never moves one by more than the weld
// distance. What it cannot close it reports per triangle (open[]), so that
// make-char can show the cost of drawing those parts two-sided.
//
// THE STEPS, one after the other:
//
//   1  WELD. Positions closer than weldDistance become one. The input is
//      walked in index order; a position that is not taken yet stands for
//      itself and takes every later free position within weldDistance of
//      ITSELF (not of a position it took before). So a weld set is never a
//      chain: every member is within weldDistance of its representative, no
//      position moves farther than that, and a row of points 0.9 apart with
//      a weld distance of 1 does not collapse into one point. The
//      representative is the lowest index of its set; a position within
//      reach of two representatives goes to the lower one. Only positions
//      that a triangle uses take part. A grid of cells a little larger than
//      weldDistance, sorted by (cell, index), finds the neighbors.
//   2  CLEAN. A triangle whose corners meet after welding, or whose height
//      over its longest side is below RLDREP_FLAT of that side (no area), is
//      degenerate and dropped. Of exact doubles (the same three positions the
//      same way round) the first is kept. Twins - the same three positions
//      the other way round - are a two-sided surface made on purpose: both
//      stay, are never turned, and count in twins.
//   2b CRACKS. An open edge a-b beside a chain of open edges a-c1-..-b whose
//      positions all lie on the segment a-b (within the weld distance) is a
//      crack of no width: a T-junction, where one side was cut at c1.. and
//      the other was not. Its triangle (a, b, x) is split into the fan
//      (a, c1, x) .. (cj, b, x) - no new position, the same plane - and the
//      crack is closed. A ci carries the input corner of the other side at
//      the same position, so the colors stay continuous; where the other
//      side has a color seam at ci, the triangle is split only when both
//      ends of its edge have one color (see RldRep_SplitCracks). Hole
//      filling cannot close such a crack (its loop has no area).
//   3  ORIENT. Triangles are joined across edges that have exactly two
//      triangles (a pair of twins counts as one triangle, its first) into
//      pieces; a breadth-first walk from the lowest triangle of each piece
//      turns the triangles so that both of every such edge run it in
//      opposite directions. Edges with three or more triangles (a fin on a
//      body) are not crossed, so a fin does not decide the body and the body
//      does not decide the fin. Then each piece as a whole faces outward:
//        closed piece (every edge of it is run equally often in both
//        directions by its own triangles): the enclosed volume must be
//        positive. A closed piece without volume (a flat sheet modelled with
//        front and back) keeps the winding most of its area had.
//        open piece: see THE OUTWARD RULE FOR OPEN PIECES below.
//   4  HOLES. The open edges (one triangle) form boundary loops, run against
//      the direction of their triangles. At each position the loop goes on
//      with the open edge reached by walking round the position through its
//      fan of triangles (across edges with two triangles), so two holes that
//      touch at one position are two loops; a walk that meets an edge with
//      three or more triangles ends the loop unfilled. A loop of at most
//      holeMaxEdges edges is filled by ear clipping in the plane of the loop
//      (Newell's normal); a loop that passes one position twice (two holes
//      touching there) is first cut into loops of their own. Each step cuts
//      the convex ear with the smallest angle at its tip (a sharp corner
//      first leaves the rest of the loop rounder; ties: the first in loop
//      order, which starts at the lowest triangle). An ear must not be a
//      needle (RLDREP_NEEDLE: a slit without width stays open), must contain
//      no other loop position and must not make an edge the mesh or an
//      earlier fill already has (that would give an edge three triangles).
//      A loop is filled completely or not at all, and not at all when a
//      fill triangle would pass through another triangle (a leg in a
//      cockpit, a part stuck through the hole): that hole stays open and is
//      drawn two-sided. Not filled either: a loop
//      whose fill would face against the surface around it (area-weighted
//      mean cosine between the fill normal and the triangles on the loop
//      below RLDREP_HOLE_FACING, -0.5). That is the outline of a single
//      sheet (a wing, a flag, cosine -1): "closing" it would only lay a
//      back side onto the sheet. The walls around the missing face of a box
//      or the open end of a tube stand at right angles to the fill (cosine
//      0), and that hole is closed - an exhaust pipe of up to holeMaxEdges
//      edges gets a lid. The corners of a fill take the attributes (color)
//      of the triangle on the loop edge that leaves the position. No new
//      position is made.
//   5  RESULT. A closed piece that a fill made closed is checked once more
//      by its volume. open[] marks every triangle of a piece that is still
//      open (twins are 0: they are two-sided already); the counts say what
//      was done.
//
// THE OUTWARD RULE FOR OPEN PIECES. An open surface has no inside, so there
// is no exact rule; this one decides only when the evidence is clear and
// otherwise keeps what the artist exported:
//   a  RAYS. From the middle of up to RLDREP_RAY_SAMPLES of the largest
//      triangles of the piece a ray goes along the normal and one against
//      it, against all triangles of the model. Where exactly one of the two
//      escapes, that side is outside; the votes are weighted by area. When
//      the side that wins leads by more than RLDREP_RAY_MARGIN of the
//      sampled area, it decides. This is what makes a body open at the
//      bottom, a hair strand or a sleeve (rays from the outside escape, rays
//      inward hit the other wall) face outward even when it was exported
//      inside out.
//   b  THE WINDING OF THE INPUT. When the rays do not decide - a thin wing
//      or a flag is seen from both sides, both rays escape - the piece keeps
//      the winding that at least RLDREP_MAJORITY of its area had in the
//      input: the artist's normals are the best information left, and the
//      fewest triangles turn.
//   c  THE MIDDLE OF THE MODEL. When even the input is split (the walk had
//      to turn nearly half of the piece), the piece faces away from the
//      area-weighted middle of the model, if clearly (RLDREP_CENTROID_MARGIN).
//   d  Otherwise the winding of its lowest triangle stays.
// Limits of the rule: a single-sided sheet seen from both sides has no right
// side in a culled game - that is why make-char draws open pieces two-sided;
// each piece is decided on its own, so of two mirrored open pieces (a left
// and a right wing) one may be turned and the other not - harmless, as both
// are drawn two-sided;
// a wing lying over the body faces up (its downward rays hit the body); a
// cup faces its outside out. The rays cost triangles x samples; at most
// RLDREP_RAY_BUDGET triangle tests are spent, the largest pieces first, the
// rest goes on with b to d.
//
// DEFAULTS (for make-char). RldRep_DefaultWeld: the longest side of the box /
// RLDREP_WELD_DIVISOR (1024), 0.11 game units at Crash's length of 112.4.
// make-char stores one byte per axis and frame, so one quantization step is
// about the extent / 255; the weld moves a position by at most a quarter of a
// step along the longest axis and less than half a step along the shortest
// (60 to 86 units wide: 0.24 to 0.34 per step) - nothing that welding moves
// can be seen after quantization. Measured on four exported models (Blender
// x 64, before --fit): the split corners of seams lie 0 to 0.064 units apart
// (export rounding, 0.001 m at most), while the shortest real edge is 0.168
// units and the closest positions of two surfaces not joined by an edge 0.137
// units. 0.11 welds every split corner and none of the others.
// RLDREP_HOLE_MAX_EDGES_DEFAULT (8): on the same models the gaps that close
// have 3 to 8 edges (at the sides and the front of a kart, 8 = two gaps that
// touch at one position; small gaps in drivers), while the smallest opening
// made on purpose is the cockpit of the kart with 12 edges - a lid there
// would cut through the driver sitting in it.
//
// DETERMINISM. The same input gives the same output bit for bit: no time, no
// random numbers, no hash order; every sort has a complete tie-break, walks
// go in index order. The square root is written out (as in
// tools/rldpack_reduce.inc) instead of taken from the C library; the rest is
// + - * / and comparisons, which IEEE double arithmetic without excess
// precision and without contracted multiply-adds rounds the same everywhere -
// as this project builds with MSVC (SSE2, /fp:precise).

//========================================================================================
// INTERFACE
//========================================================================================

typedef struct RldRepInput
{
	u32 positionCount;
	const double *position;   // 3 per position, game units
	u32 triangleCount;
	const u32 *triangle;      // 3 position indices per triangle
	const u32 *cornerColor;   // 3 per triangle, 0xRRGGBB; not read (sourceCorner says which corner an output corner carries), may be NULL
	double weldDistance;      // positions closer than this are one (game units); 0 = only equal ones
	u32 holeMaxEdges;         // boundary loops with at most this many edges are closed; 0 = none
} RldRepInput;

typedef struct RldRepResult
{
	u32 *positionMap;         // per input position: the input position that stands for it now (lowest index of its weld set)
	u32 triangleCount;
	u32 *triangle;            // 3 input position indices (representatives) per triangle, outward winding
	u32 *sourceCorner;        // per output corner: the input corner 3*t+k whose attributes (color) it carries; a triangle
	                          // from the input carries its own corners, except a triangle split at a crack (step 2b): its
	                          // new corner carries the corner of the other side at the same position, or of the nearer end
	                          // of the split edge. Positions always come from triangle[], never from sourceCorner.
	u8 *added;                // per output triangle: 1 = closes a hole (new), 0 = from the input
	u8 *open;                 // per output triangle: 1 = part of a component that is still open afterwards
	u32 welded;               // used positions that were welded onto another one
	u32 degenerate;           // input triangles dropped: corners met, or no area
	u32 duplicate;            // input triangles dropped: an exact double of an earlier one
	u32 flipped;              // input triangles whose winding was turned (a split one counts once)
	u32 holesClosed;          // boundary loops filled
	u32 holeTriangles;        // triangles added for them (added[] = 1)
	u32 holesLeft;            // boundary pieces still open afterwards (loops joined at a position count once)
	u32 openEdgesBefore;      // edges with one triangle in the input as given (no welding)
	u32 openEdgesAfter;       // and in the result
	u32 nonManifoldEdges;     // edges of the result with more than two triangles (a pair of twins counts as one)
	u32 twins;                // pairs of triangles on the same positions, opposite ways round (kept, two-sided on purpose)
	u32 components;           // pieces of the result: triangles joined across edges with two triangles
	u32 componentsOpen;       // of those, the pieces with triangles that have open[] = 1
	u32 cracksSplit;          // input triangles split at a T-junction (step 2b)
} RldRepResult;

//========================================================================================
// CONSTANTS
//========================================================================================

#define RLDREP_NONE 0xFFFFFFFFu

// A guard against overflow of the corner arithmetic, far above any model.
#define RLDREP_INPUT_MAX 0x00100000u

// Positions and the weld distance must be finite and within this many units.
#define RLDREP_COORD_MAX 1000000.0

// holeMaxEdges above this is refused: ear clipping costs the cube of the loop
// length, and loops this long are openings, not holes.
#define RLDREP_HOLE_LIMIT 256u

// RldRep_DefaultWeld: the longest side of the box divided by this (see the
// head of the file).
#define RLDREP_WELD_DIVISOR 1024.0

// Step 2b follows a crack along at most this many positions.
#define RLDREP_SPLIT_MAX 16u
#define RLDREP_SPLIT_STRIDE (2u + 2u * RLDREP_SPLIT_MAX)

// A fill triangle thinner than this fraction of its longest side (height /
// longest side; a tip below about 1.1 degrees) is a needle: it closes a slit
// that cannot be seen - at a side of 15 units it is under 0.15 units wide,
// less than half a quantization step of a model of Crash's size (0.44) - and
// still costs 28 bytes of draw memory and an outline the reducer must keep.
// Such a slit stays open (its piece is drawn two-sided).
#define RLDREP_NEEDLE 0.01

// Fills must not pass through other triangles: a crossing counts when an
// edge passes a triangle strictly inside (this margin in the barycentric and
// edge parameters), so triangles that only touch do not count.
#define RLDREP_CROSS_MARGIN 1.0e-9

// The suggested holeMaxEdges (see DEFAULTS at the head of the file): a hole
// of up to 8 edges is a missing quad or two, a gap where two pieces of a seam
// did not meet, or two such gaps touching; the smallest opening made on
// purpose on the measured models has 12.
#define RLDREP_HOLE_MAX_EDGES_DEFAULT 8u

// A triangle is flat (degenerate) when its height over its longest side is at
// most this fraction of that side: 2e-4 units over a side of 100 units.
// Compared squared, no square root: |cross|^2 <= (flat * longest^2)^2.
#define RLDREP_FLAT 1.0e-6

// A closed piece has no volume when |volume| <= this times area^1.5 (a cube
// has 0.068): a flat sheet with front and back, where rounding decides the
// sign.
#define RLDREP_VOLUME_MIN 1.0e-9

// The outward rule for open pieces (see the head of the file).
#define RLDREP_RAY_SAMPLES 16u
#define RLDREP_RAY_BUDGET 40000000u
#define RLDREP_RAY_MARGIN 0.2
#define RLDREP_MAJORITY 0.6
#define RLDREP_CENTROID_MARGIN 0.2

// A ray hit closer than this times the diagonal of the box is the triangle's
// own plane (a neighbor in the same plane, a twin), not a wall.
#define RLDREP_RAY_NEAR 1.0e-6

// A fill must not face against the surface around its loop: the mean cosine
// between the fill normal and the normals of the triangles on the loop,
// weighted by area, must be above this. The outline of a flat sheet gives -1,
// a missing face of a box or the end of a tube 0, a hole in a curved surface
// up to 1.
#define RLDREP_HOLE_FACING (-0.5)

// The weld grid uses cells of at least this fraction of the largest
// coordinate, so that cell numbers stay far inside 64 bits.
#define RLDREP_CELL_MIN (1.0 / 1099511627776.0)

//========================================================================================
// ARITHMETIC THAT ROUNDS THE SAME EVERYWHERE
//========================================================================================

// Square root without the C library: the value is scaled by powers of four
// into [0.25, 4] (exact), Newton's method runs a fixed number of steps there,
// and the result is scaled back by the same powers of two (exact again).
static double RldRep_Sqrt(double value)
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

// Floor for values well inside 64 bits (the weld grid keeps them below 2^41).
static s64 RldRep_Floor(double value)
{
	s64 whole = (s64)value;

	if ((double)whole > value)
	{
		whole--;
	}
	return whole;
}

static double RldRep_Dot(const double *a, const double *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void RldRep_CrossV(const double *u, const double *v, double out[3])
{
	out[0] = u[1] * v[2] - u[2] * v[1];
	out[1] = u[2] * v[0] - u[0] * v[2];
	out[2] = u[0] * v[1] - u[1] * v[0];
}

// (b - a) x (c - a): the normal of the triangle a, b, c, twice its area long.
static void RldRep_Cross(const double *a, const double *b, const double *c, double out[3])
{
	const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};

	RldRep_CrossV(u, v, out);
}

static double RldRep_Distance2(const double *a, const double *b)
{
	const double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};

	return RldRep_Dot(d, d);
}

// 1 when the triangle a, b, c has no area (see RLDREP_FLAT).
static int RldRep_Flat(const double *a, const double *b, const double *c)
{
	double n[3];
	double longest = RldRep_Distance2(a, b);
	const double l1 = RldRep_Distance2(b, c);
	const double l2 = RldRep_Distance2(c, a);

	if (l1 > longest)
	{
		longest = l1;
	}
	if (l2 > longest)
	{
		longest = l2;
	}
	RldRep_Cross(a, b, c, n);
	return RldRep_Dot(n, n) <= (RLDREP_FLAT * RLDREP_FLAT * longest * longest);
}

//========================================================================================
// THE STATE OF ONE REPAIR
//========================================================================================

// One half-edge: corner k -> k + 1 of triangle t, on the edge (lo, hi).
struct RldRepEdge
{
	u32 lo;
	u32 hi;
	u32 t;
	u32 k;
};

// The working triangles are the kept input triangles in input order, then
// the fills.
struct RldRepState
{
	const RldRepInput *in;
	u32 n;                 // positions
	u32 m;                 // working triangles
	u32 capacity;          // room for working triangles

	u32 *map;              // per position: its representative
	u32 *tri;              // 3 per triangle: representatives, current winding
	u32 *corner;           // 3 per triangle: the input corner each corner carries
	u8 *twin;              // 1 = first of a pair of twins, 2 = second (both never turned)
	u8 *added;             // a fill
	u8 *parity;            // the winding is turned against the input
	u32 *source;           // per triangle: the input triangle it comes from (a fan: its split one), RLDREP_NONE for a fill
	double *area;          // per triangle

	// pieces (RldRep_Orient)
	u32 *patch;            // per triangle; RLDREP_NONE for the second of twins
	u8 *rel;               // per triangle: turned against the first triangle of its piece
	u32 *nb;               // 3 per triangle: the triangle across that edge when it has two
	u8 *same;              // 3 per triangle: both run that edge the same way
	u32 *queue;            // per triangle
	u32 patchCount;
	u8 *closed;            // per piece
	u8 *turn;              // per piece: the decision
	double *patchArea;     // per piece
	double *area0;         // per piece: area of triangles with rel 0
	double *area1;         // and with rel 1
	double *volume;        // per piece, with rel applied
	double *vote;          // per piece: middle-of-the-model vote
	double *rayIn;         // per piece: area of samples that face inward
	double *rayOut;        // and outward
	double *raySum;        // all sampled area

	double center[3];      // middle of the box of the used positions
	double diagonal;       // diagonal of that box
	double centroid[3];    // area-weighted middle of the triangles
};

static void RldRep_FreeState(struct RldRepState *s)
{
	free(s->map);
	free(s->tri);
	free(s->corner);
	free(s->twin);
	free(s->added);
	free(s->parity);
	free(s->source);
	free(s->area);
	free(s->patch);
	free(s->rel);
	free(s->nb);
	free(s->same);
	free(s->queue);
	free(s->closed);
	free(s->turn);
	free(s->patchArea);
	free(s->area0);
	free(s->area1);
	free(s->volume);
	free(s->vote);
	free(s->rayIn);
	free(s->rayOut);
	free(s->raySum);
	memset(s, 0, sizeof(*s));
}

static const double *RldRep_At(const struct RldRepState *s, u32 p)
{
	return &s->in->position[3u * p];
}

static double RldRep_TriangleArea(const struct RldRepState *s, u32 t)
{
	double n[3];

	RldRep_Cross(RldRep_At(s, s->tri[3u * t]), RldRep_At(s, s->tri[3u * t + 1u]), RldRep_At(s, s->tri[3u * t + 2u]), n);
	return 0.5 * RldRep_Sqrt(RldRep_Dot(n, n));
}

// Turns triangle t: corners 1 and 2 change places, with what they carry.
static void RldRep_Flip(struct RldRepState *s, u32 t)
{
	u32 x = s->tri[3u * t + 1u];

	s->tri[3u * t + 1u] = s->tri[3u * t + 2u];
	s->tri[3u * t + 2u] = x;
	x = s->corner[3u * t + 1u];
	s->corner[3u * t + 1u] = s->corner[3u * t + 2u];
	s->corner[3u * t + 2u] = x;
	s->parity[t] ^= 1u;
}

//========================================================================================
// EDGES
//========================================================================================

static int RldRep_CompareEdge(const void *x, const void *y)
{
	const struct RldRepEdge *a = (const struct RldRepEdge *)x;
	const struct RldRepEdge *b = (const struct RldRepEdge *)y;

	if (a->lo != b->lo)
	{
		return (a->lo < b->lo) ? -1 : 1;
	}
	if (a->hi != b->hi)
	{
		return (a->hi < b->hi) ? -1 : 1;
	}
	if (a->t != b->t)
	{
		return (a->t < b->t) ? -1 : 1;
	}
	return (a->k < b->k) ? -1 : ((a->k > b->k) ? 1 : 0);
}

// The half-edges of the triangles 0..count-1 of tri (those with skip[t] equal
// to skipValue left out when skip is not NULL; corners that meet give no
// edge), sorted by edge, then triangle and corner: one run per edge. NULL when
// out of memory.
static struct RldRepEdge *RldRep_Edges(const u32 *tri, u32 count, const u8 *skip, u8 skipValue, size_t *countOut)
{
	struct RldRepEdge *e = (struct RldRepEdge *)malloc(((size_t)count * 3u + 1u) * sizeof(*e));
	size_t c = 0;
	u32 t;

	*countOut = 0;
	if (e == NULL)
	{
		return NULL;
	}

	for (t = 0; t < count; t++)
	{
		u32 k;

		if ((skip != NULL) && (skip[t] == skipValue))
		{
			continue;
		}
		for (k = 0; k < 3u; k++)
		{
			const u32 a = tri[3u * t + k];
			const u32 b = tri[3u * t + ((k + 1u) % 3u)];

			if (a == b)
			{
				continue;
			}
			e[c].lo = (a < b) ? a : b;
			e[c].hi = (a < b) ? b : a;
			e[c].t = t;
			e[c].k = k;
			c++;
		}
	}

	qsort(e, c, sizeof(*e), RldRep_CompareEdge);
	*countOut = c;
	return e;
}

// The length of the run (one edge) that starts at i.
static size_t RldRep_Run(const struct RldRepEdge *e, size_t count, size_t i)
{
	size_t r = 1;

	while (((i + r) < count) && (e[i + r].lo == e[i].lo) && (e[i + r].hi == e[i].hi))
	{
		r++;
	}
	return r;
}

// Is the edge (a, b) in the sorted half-edges?
static int RldRep_HasEdge(const struct RldRepEdge *e, size_t count, u32 a, u32 b)
{
	const u32 lo = (a < b) ? a : b;
	const u32 hi = (a < b) ? b : a;
	size_t low = 0;
	size_t high = count;

	while (low < high)
	{
		const size_t mid = low + (high - low) / 2u;

		if ((e[mid].lo < lo) || ((e[mid].lo == lo) && (e[mid].hi < hi)))
		{
			low = mid + 1u;
		}
		else
		{
			high = mid;
		}
	}
	return (low < count) && (e[low].lo == lo) && (e[low].hi == hi);
}

// Edges with exactly one triangle.
static u32 RldRep_OpenEdges(const u32 *tri, u32 count, int *ok)
{
	size_t edgeCount;
	struct RldRepEdge *e = RldRep_Edges(tri, count, NULL, 0u, &edgeCount);
	size_t i = 0;
	u32 open = 0;

	if (e == NULL)
	{
		*ok = 0;
		return 0u;
	}
	while (i < edgeCount)
	{
		const size_t r = RldRep_Run(e, edgeCount, i);

		open += (r == 1u) ? 1u : 0u;
		i += r;
	}
	free(e);
	return open;
}

//========================================================================================
// STEP 1: WELD
//========================================================================================

struct RldRepCell
{
	s64 c[3];
	u32 i;
};

static int RldRep_CompareCell(const void *x, const void *y)
{
	const struct RldRepCell *a = (const struct RldRepCell *)x;
	const struct RldRepCell *b = (const struct RldRepCell *)y;
	int k;

	for (k = 0; k < 3; k++)
	{
		if (a->c[k] != b->c[k])
		{
			return (a->c[k] < b->c[k]) ? -1 : 1;
		}
	}
	return (a->i < b->i) ? -1 : ((a->i > b->i) ? 1 : 0);
}

// The first entry of the cell key (or the place it would be).
static size_t RldRep_FindCell(const struct RldRepCell *cells, size_t count, const s64 key[3])
{
	size_t low = 0;
	size_t high = count;

	while (low < high)
	{
		const size_t mid = low + (high - low) / 2u;
		int before = 0;
		int k;

		for (k = 0; k < 3; k++)
		{
			if (cells[mid].c[k] != key[k])
			{
				before = cells[mid].c[k] < key[k];
				break;
			}
		}
		if (before)
		{
			low = mid + 1u;
		}
		else
		{
			high = mid;
		}
	}
	return low;
}

// s->map for every position; *welded = used positions mapped to another one.
static int RldRep_Weld(struct RldRepState *s, u32 *welded)
{
	const RldRepInput *in = s->in;
	const double d = in->weldDistance;
	struct RldRepCell *cells;
	u8 *used;
	double largest = 0.0;
	double cell;
	size_t count = 0;
	u32 i;

	*welded = 0;
	used = (u8 *)calloc((size_t)s->n + 1u, 1u);
	cells = (struct RldRepCell *)malloc(((size_t)s->n + 1u) * sizeof(*cells));
	if ((used == NULL) || (cells == NULL))
	{
		free(used);
		free(cells);
		return 0;
	}

	for (i = 0; i < (3u * in->triangleCount); i++)
	{
		used[in->triangle[i]] = 1u;
	}
	for (i = 0; i < s->n; i++)
	{
		u32 k;

		s->map[i] = used[i] ? RLDREP_NONE : i;
		for (k = 0; k < 3u; k++)
		{
			const double v = in->position[3u * i + k];

			if (used[i] && ((v > largest) || (-v > largest)))
			{
				largest = (v < 0.0) ? -v : v;
			}
		}
	}

	// Cells a little larger than the weld distance: every position within
	// reach lies in one of the 27 cells around, even when the division rounds
	// at a cell border. Never below RLDREP_CELL_MIN of the largest coordinate
	// (cell numbers stay below 2^41), never zero.
	cell = d * 1.000001;
	if (cell < (largest * RLDREP_CELL_MIN))
	{
		cell = largest * RLDREP_CELL_MIN;
	}
	if (!(cell > 0.0))
	{
		cell = 1.0;
	}

	for (i = 0; i < s->n; i++)
	{
		u32 k;

		if (!used[i])
		{
			continue;
		}
		for (k = 0; k < 3u; k++)
		{
			cells[count].c[k] = RldRep_Floor(in->position[3u * i + k] / cell);
		}
		cells[count].i = i;
		count++;
	}
	qsort(cells, count, sizeof(*cells), RldRep_CompareCell);

	// Representatives in index order; each takes the free positions within
	// reach of itself (see the head of the file).
	for (i = 0; i < s->n; i++)
	{
		const double *p = RldRep_At(s, i);
		s64 own[3];
		int dx;

		if (s->map[i] != RLDREP_NONE)
		{
			continue;
		}
		s->map[i] = i;
		{
			u32 k;

			for (k = 0; k < 3u; k++)
			{
				own[k] = RldRep_Floor(p[k] / cell);
			}
		}

		for (dx = -1; dx <= 1; dx++)
		{
			int dy;

			for (dy = -1; dy <= 1; dy++)
			{
				int dz;

				for (dz = -1; dz <= 1; dz++)
				{
					const s64 key[3] = {own[0] + dx, own[1] + dy, own[2] + dz};
					size_t j = RldRep_FindCell(cells, count, key);

					while ((j < count) && (cells[j].c[0] == key[0]) && (cells[j].c[1] == key[1]) && (cells[j].c[2] == key[2]))
					{
						const u32 q = cells[j].i;

						if ((s->map[q] == RLDREP_NONE) && (RldRep_Distance2(p, RldRep_At(s, q)) <= (d * d)))
						{
							s->map[q] = i;
							(*welded)++;
						}
						j++;
					}
				}
			}
		}
	}

	free(used);
	free(cells);
	return 1;
}

//========================================================================================
// STEP 2: CLEAN
//========================================================================================

// A triangle turned so that its lowest position comes first (the winding
// stays): two triangles are exact doubles when (v0, v1, v2) are equal, twins
// when one is (v0, v2, v1) of the other.
struct RldRepKey
{
	u32 v[3];
	u32 t;
};

static int RldRep_CompareKey(const void *x, const void *y)
{
	const struct RldRepKey *a = (const struct RldRepKey *)x;
	const struct RldRepKey *b = (const struct RldRepKey *)y;
	int k;

	for (k = 0; k < 3; k++)
	{
		if (a->v[k] != b->v[k])
		{
			return (a->v[k] < b->v[k]) ? -1 : 1;
		}
	}
	return (a->t < b->t) ? -1 : ((a->t > b->t) ? 1 : 0);
}

static void RldRep_MakeKey(const u32 c[3], u32 t, struct RldRepKey *key)
{
	u32 first = 0;

	if (c[1] < c[first])
	{
		first = 1;
	}
	if (c[2] < c[first])
	{
		first = 2;
	}
	key->v[0] = c[first];
	key->v[1] = c[(first + 1u) % 3u];
	key->v[2] = c[(first + 2u) % 3u];
	key->t = t;
}

// The first key with these positions, or count.
static size_t RldRep_FindKey(const struct RldRepKey *keys, size_t count, u32 a, u32 b, u32 c)
{
	size_t low = 0;
	size_t high = count;
	const u32 want[3] = {a, b, c};

	while (low < high)
	{
		const size_t mid = low + (high - low) / 2u;
		int before = 0;
		int k;

		for (k = 0; k < 3; k++)
		{
			if (keys[mid].v[k] != want[k])
			{
				before = keys[mid].v[k] < want[k];
				break;
			}
		}
		if (before)
		{
			low = mid + 1u;
		}
		else
		{
			high = mid;
		}
	}
	if ((low < count) && (keys[low].v[0] == a) && (keys[low].v[1] == b) && (keys[low].v[2] == c))
	{
		return low;
	}
	return count;
}

// Fills the working triangles from the input: welded, without degenerate
// triangles and exact doubles, twins marked.
static int RldRep_Clean(struct RldRepState *s, RldRepResult *out)
{
	const RldRepInput *in = s->in;
	struct RldRepKey *keys;
	u8 *drop;
	size_t count = 0;
	size_t i;
	u32 t;

	keys = (struct RldRepKey *)malloc(((size_t)in->triangleCount + 1u) * sizeof(*keys));
	drop = (u8 *)calloc((size_t)in->triangleCount + 1u, 1u);
	if ((keys == NULL) || (drop == NULL))
	{
		free(keys);
		free(drop);
		return 0;
	}

	for (t = 0; t < in->triangleCount; t++)
	{
		u32 c[3];
		u32 k;

		for (k = 0; k < 3u; k++)
		{
			c[k] = s->map[in->triangle[3u * t + k]];
		}
		if ((c[0] == c[1]) || (c[1] == c[2]) || (c[0] == c[2]) || RldRep_Flat(RldRep_At(s, c[0]), RldRep_At(s, c[1]), RldRep_At(s, c[2])))
		{
			drop[t] = 1u;
			out->degenerate++;
			continue;
		}
		RldRep_MakeKey(c, t, &keys[count]);
		count++;
	}

	qsort(keys, count, sizeof(*keys), RldRep_CompareKey);
	for (i = 1; i < count; i++)
	{
		if ((keys[i].v[0] == keys[i - 1u].v[0]) && (keys[i].v[1] == keys[i - 1u].v[1]) && (keys[i].v[2] == keys[i - 1u].v[2]))
		{
			drop[keys[i].t] = 1u;
			out->duplicate++;
		}
	}

	// Kept triangles in input order. The twin marks need the working index,
	// so the input index is translated through queue (free until step 3).
	s->m = 0;
	for (t = 0; t < in->triangleCount; t++)
	{
		u32 k;

		s->queue[t] = RLDREP_NONE;
		if (drop[t])
		{
			continue;
		}
		for (k = 0; k < 3u; k++)
		{
			s->tri[3u * s->m + k] = s->map[in->triangle[3u * t + k]];
			s->corner[3u * s->m + k] = 3u * t + k;
		}
		s->twin[s->m] = 0u;
		s->added[s->m] = 0u;
		s->parity[s->m] = 0u;
		s->source[s->m] = t;
		s->area[s->m] = RldRep_TriangleArea(s, s->m);
		s->queue[t] = s->m;
		s->m++;
	}

	for (i = 0; i < count; i++)
	{
		size_t j;

		if (drop[keys[i].t] || ((i > 0u) && (keys[i].v[0] == keys[i - 1u].v[0]) && (keys[i].v[1] == keys[i - 1u].v[1]) &&
		                        (keys[i].v[2] == keys[i - 1u].v[2])))
		{
			continue;
		}
		j = RldRep_FindKey(keys, count, keys[i].v[0], keys[i].v[2], keys[i].v[1]);
		if ((j < count) && (keys[i].t < keys[j].t))
		{
			s->twin[s->queue[keys[i].t]] = 1u;
			s->twin[s->queue[keys[j].t]] = 2u;
			out->twins++;
		}
	}

	free(keys);
	free(drop);
	return 1;
}

//========================================================================================
// STEP 2B: CRACKS AT T-JUNCTIONS
//========================================================================================

static size_t RldRep_FindRun(const struct RldRepEdge *e, size_t count, u32 a, u32 b, size_t *length);
static u32 RldRep_CornerAt(const struct RldRepState *s, u32 t, u32 v);

// The color of input corner c (0 for all when the input has no colors).
static u32 RldRep_Color(const struct RldRepState *s, u32 c)
{
	return (s->in->cornerColor != NULL) ? s->in->cornerColor[c] : 0u;
}

// The positions next to each position (from the sorted half-edges): the
// neighbors of p are list[start[p]..start[p + 1]), ascending. NULL lists when
// out of memory.
static int RldRep_Neighbors(const struct RldRepEdge *e, size_t edgeCount, u32 n, u32 **startOut, u32 **listOut)
{
	u32 *start = (u32 *)calloc((size_t)n + 2u, sizeof(u32));
	u32 *list = (u32 *)malloc((2u * edgeCount + 1u) * sizeof(u32));
	u32 *fill;
	size_t i;
	u32 p;

	*startOut = NULL;
	*listOut = NULL;
	if ((start == NULL) || (list == NULL))
	{
		free(start);
		free(list);
		return 0;
	}

	// Each edge once (the first half-edge of its run), both ways.
	for (i = 0; i < edgeCount; i++)
	{
		if ((i == 0u) || (e[i].lo != e[i - 1u].lo) || (e[i].hi != e[i - 1u].hi))
		{
			start[e[i].lo + 1u]++;
			start[e[i].hi + 1u]++;
		}
	}
	for (p = 0; p < n; p++)
	{
		start[p + 1u] += start[p];
	}
	fill = (u32 *)malloc(((size_t)n + 1u) * sizeof(u32));
	if (fill == NULL)
	{
		free(start);
		free(list);
		return 0;
	}
	memcpy(fill, start, (size_t)n * sizeof(u32));
	for (i = 0; i < edgeCount; i++)
	{
		if ((i == 0u) || (e[i].lo != e[i - 1u].lo) || (e[i].hi != e[i - 1u].hi))
		{
			list[fill[e[i].lo]++] = e[i].hi;
			list[fill[e[i].hi]++] = e[i].lo;
		}
	}
	free(fill);

	// The edges are sorted by (lo, hi): the neighbors above p come in order,
	// those below are sorted here (insertion, lists are short).
	for (p = 0; p < n; p++)
	{
		u32 x;

		for (x = start[p] + 1u; x < start[p + 1u]; x++)
		{
			const u32 v = list[x];
			u32 y = x;

			while ((y > start[p]) && (list[y - 1u] > v))
			{
				list[y] = list[y - 1u];
				y--;
			}
			list[y] = v;
		}
	}

	*startOut = start;
	*listOut = list;
	return 1;
}

// The chain of positions c1..cj (into chain, at most RLDREP_SPLIT_MAX) that
// runs from a to b along OPEN edges of the mesh (one triangle each: the other
// side of the crack; an edge with more triangles is no crack), every ci on
// the segment a-b (within tolerance of it, strictly between a and b, each
// further along than the one before; of several the nearest to a, ties by
// index). edgeTriangle[0..j] gets the triangle of each chain edge (a-c1 ..
// cj-b). 0 when there is none.
static u32 RldRep_Chain(const struct RldRepState *s, const struct RldRepEdge *e, size_t edgeCount, const u32 *start, const u32 *list, u32 a, u32 b,
                        u32 x, double tolerance, u32 *chain, u32 *edgeTriangle)
{
	const double *pa = RldRep_At(s, a);
	const double *pb = RldRep_At(s, b);
	const double ab[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
	const double ab2 = RldRep_Dot(ab, ab);
	double along = 0.0;
	u32 cur = a;
	u32 count = 0;

	if (!(ab2 > 0.0))
	{
		return 0u;
	}

	while (count < RLDREP_SPLIT_MAX)
	{
		u32 best = RLDREP_NONE;
		u32 bestTriangle = RLDREP_NONE;
		double bestAlong = 0.0;
		u32 i;

		for (i = start[cur]; i < start[cur + 1u]; i++)
		{
			const u32 q = list[i];
			const double *pq;
			double aq[3];
			double side[3];
			double t;
			size_t length;
			const size_t at = RldRep_FindRun(e, edgeCount, cur, q, &length);

			if (length != 1u)
			{
				continue;
			}
			if ((q == b) && (count > 0u))
			{
				edgeTriangle[count] = e[at].t;
				return count;
			}
			if ((q == a) || (q == b) || (q == x))
			{
				continue;
			}
			pq = RldRep_At(s, q);
			aq[0] = pq[0] - pa[0];
			aq[1] = pq[1] - pa[1];
			aq[2] = pq[2] - pa[2];
			t = RldRep_Dot(aq, ab) / ab2;
			RldRep_CrossV(aq, ab, side);
			if (!(t > (along + RLDREP_FLAT)) || !(t < (1.0 - RLDREP_FLAT)) || (RldRep_Dot(side, side) > (tolerance * tolerance * ab2)))
			{
				continue;
			}
			if ((best == RLDREP_NONE) || (t < bestAlong))
			{
				best = q;
				bestTriangle = e[at].t;
				bestAlong = t;
			}
		}
		if (best == RLDREP_NONE)
		{
			return 0u;
		}
		edgeTriangle[count] = bestTriangle;
		chain[count++] = best;
		cur = best;
		along = bestAlong;
	}
	return 0u;
}

// Step 2b: an open edge a -> b of triangle (a, b, x) with a chain of open
// edges a - c1 - ... - b beside it, every ci on the segment a-b, is a crack
// of no width (a T-junction: the other side was cut at c1.. and this side was
// not). The triangle is split into the fan (a, c1, x), (c1, c2, x), ..,
// (cj, b, x): no new position, the same plane, and the crack is closed.
// Tolerance: the weld distance (plus RLDREP_FLAT of the edge).
// THE COLOR AT ci. The new corner takes the input corner of the other side at
// the same position - the corner at ci of the triangle on the chain edge
// before ci - when the triangles on both chain edges at ci have one color
// there: the color stays continuous across the closed crack. Where they do
// not (a color seam ends at ci), the triangle is split only when both ends of
// its edge have the same color (ci then carries the nearer end, which has
// it); otherwise it is not split. So no corner gets a color that the surface
// does not have at its position.
// A triangle is split at one edge at most; the triangles stay in input order
// (the fan where its triangle was). At most as many triangles are added as
// the input has.
static int RldRep_SplitCracks(struct RldRepState *s, RldRepResult *out)
{
	struct RldRepEdge *e;
	size_t edgeCount;
	u32 *start = NULL;
	u32 *list = NULL;
	u32 *chain = NULL;   // per working triangle RLDREP_SPLIT_STRIDE entries: k, count, c1.., then the corners they carry
	u32 *tri = NULL;
	u32 *corner = NULL;
	u8 *twin = NULL;
	u32 *source = NULL;
	u32 extra = 0;
	u32 count;
	size_t i;
	u32 t;
	int ok = 0;

	e = RldRep_Edges(s->tri, s->m, NULL, 0u, &edgeCount);
	if (e == NULL)
	{
		return 0;
	}
	chain = (u32 *)malloc(((size_t)s->m + 1u) * RLDREP_SPLIT_STRIDE * sizeof(u32));
	if ((chain == NULL) || !RldRep_Neighbors(e, edgeCount, s->n, &start, &list))
	{
		goto done;
	}
	for (t = 0; t < s->m; t++)
	{
		chain[(size_t)t * RLDREP_SPLIT_STRIDE + 1u] = 0u;
	}

	i = 0;
	while (i < edgeCount)
	{
		const size_t r = RldRep_Run(e, edgeCount, i);

		if (r == 1u)
		{
			const u32 tt = e[i].t;
			const u32 k = e[i].k;
			u32 *c = &chain[(size_t)tt * RLDREP_SPLIT_STRIDE];
			const u32 a = s->tri[3u * tt + k];
			const u32 b = s->tri[3u * tt + ((k + 1u) % 3u)];
			const u32 x = s->tri[3u * tt + ((k + 2u) % 3u)];
			const double tolerance = s->in->weldDistance + RLDREP_FLAT * RldRep_Sqrt(RldRep_Distance2(RldRep_At(s, a), RldRep_At(s, b)));

			if (c[1] == 0u)
			{
				u32 edgeTriangle[RLDREP_SPLIT_MAX + 1u];
				const u32 j = RldRep_Chain(s, e, edgeCount, start, list, a, b, x, tolerance, &c[2], edgeTriangle);
				const u32 ca = s->corner[3u * tt + k];
				const u32 cb = s->corner[3u * tt + ((k + 1u) % 3u)];
				u32 m;
				int flat = 0;

				// Every triangle of the fan must have area.
				for (m = 0; m <= j; m++)
				{
					const u32 p = (m == 0u) ? a : c[1u + m];
					const u32 q = (m == j) ? b : c[2u + m];

					flat = flat || RldRep_Flat(RldRep_At(s, p), RldRep_At(s, q), RldRep_At(s, x));
				}

				// The corner each ci carries (see the color rule above).
				for (m = 1; (m <= j) && !flat; m++)
				{
					const u32 ci = c[1u + m];
					const u32 before = RldRep_CornerAt(s, edgeTriangle[m - 1u], ci);
					const u32 after = RldRep_CornerAt(s, edgeTriangle[m], ci);

					if ((before != RLDREP_NONE) && (after != RLDREP_NONE) && (RldRep_Color(s, before) == RldRep_Color(s, after)))
					{
						c[1u + RLDREP_SPLIT_MAX + m] = before;
					}
					else if (RldRep_Color(s, ca) == RldRep_Color(s, cb))
					{
						c[1u + RLDREP_SPLIT_MAX + m] =
						    (RldRep_Distance2(RldRep_At(s, ci), RldRep_At(s, a)) <= RldRep_Distance2(RldRep_At(s, ci), RldRep_At(s, b))) ? ca : cb;
					}
					else
					{
						flat = 1;
					}
				}
				if ((j > 0u) && !flat && ((extra + j) <= s->in->triangleCount))
				{
					c[0] = k;
					c[1] = j;
					extra += j;
					out->cracksSplit++;
				}
			}
		}
		i += r;
	}

	if (extra == 0u)
	{
		ok = 1;
		goto done;
	}

	tri = (u32 *)malloc(((size_t)s->m + extra) * 3u * sizeof(u32));
	corner = (u32 *)malloc(((size_t)s->m + extra) * 3u * sizeof(u32));
	twin = (u8 *)malloc((size_t)s->m + extra);
	source = (u32 *)malloc(((size_t)s->m + extra) * sizeof(u32));
	if ((tri == NULL) || (corner == NULL) || (twin == NULL) || (source == NULL))
	{
		goto done;
	}
	count = 0;
	for (t = 0; t < s->m; t++)
	{
		const u32 *c = &chain[(size_t)t * RLDREP_SPLIT_STRIDE];

		if (c[1] == 0u)
		{
			memcpy(&tri[3u * count], &s->tri[3u * t], 3u * sizeof(u32));
			memcpy(&corner[3u * count], &s->corner[3u * t], 3u * sizeof(u32));
			twin[count] = s->twin[t];
			source[count] = s->source[t];
			count++;
		}
		else
		{
			const u32 k = c[0];
			const u32 a = s->tri[3u * t + k];
			const u32 b = s->tri[3u * t + ((k + 1u) % 3u)];
			const u32 x = s->tri[3u * t + ((k + 2u) % 3u)];
			const u32 ca = s->corner[3u * t + k];
			const u32 cb = s->corner[3u * t + ((k + 1u) % 3u)];
			const u32 cx = s->corner[3u * t + ((k + 2u) % 3u)];
			u32 m;

			for (m = 0; m <= c[1]; m++)
			{
				const u32 p = (m == 0u) ? a : c[1u + m];
				const u32 q = (m == c[1]) ? b : c[2u + m];

				tri[3u * count + 0u] = p;
				tri[3u * count + 1u] = q;
				tri[3u * count + 2u] = x;
				corner[3u * count + 0u] = (m == 0u) ? ca : c[1u + RLDREP_SPLIT_MAX + m];
				corner[3u * count + 1u] = (m == c[1]) ? cb : c[2u + RLDREP_SPLIT_MAX + m];
				corner[3u * count + 2u] = cx;
				twin[count] = 0u;
				source[count] = s->source[t];
				count++;
			}
		}
	}

	memcpy(s->tri, tri, (size_t)count * 3u * sizeof(u32));
	memcpy(s->corner, corner, (size_t)count * 3u * sizeof(u32));
	memcpy(s->twin, twin, count);
	memcpy(s->source, source, (size_t)count * sizeof(u32));
	s->m = count;
	for (t = 0; t < s->m; t++)
	{
		s->added[t] = 0u;
		s->parity[t] = 0u;
		s->area[t] = RldRep_TriangleArea(s, t);
	}
	ok = 1;

done:
	free(e);
	free(start);
	free(list);
	free(chain);
	free(tri);
	free(corner);
	free(twin);
	free(source);
	return ok;
}

//========================================================================================
// STEP 3: ORIENT
//========================================================================================

// 1 when a ray from o along the unit vector d hits a working triangle other
// than skip farther than near.
static int RldRep_RayHits(const struct RldRepState *s, const double o[3], const double d[3], u32 skip, double near)
{
	u32 u;

	for (u = 0; u < s->m; u++)
	{
		const double *a;
		const double *b;
		const double *c;
		double e1[3];
		double e2[3];
		double p[3];
		double q[3];
		double sv[3];
		double det;
		double inv;
		double bu;
		double bv;
		int k;

		if (u == skip)
		{
			continue;
		}
		a = RldRep_At(s, s->tri[3u * u]);
		b = RldRep_At(s, s->tri[3u * u + 1u]);
		c = RldRep_At(s, s->tri[3u * u + 2u]);
		for (k = 0; k < 3; k++)
		{
			e1[k] = b[k] - a[k];
			e2[k] = c[k] - a[k];
			sv[k] = o[k] - a[k];
		}

		// Moeller-Trumbore. A ray in the plane of the triangle passes it.
		RldRep_CrossV(d, e2, p);
		det = RldRep_Dot(e1, p);
		if ((det * det) <= (1.0e-24 * RldRep_Dot(e1, e1) * RldRep_Dot(e2, e2)))
		{
			continue;
		}
		inv = 1.0 / det;
		bu = RldRep_Dot(sv, p) * inv;
		if ((bu < 0.0) || (bu > 1.0))
		{
			continue;
		}
		RldRep_CrossV(sv, e1, q);
		bv = RldRep_Dot(d, q) * inv;
		if ((bv < 0.0) || ((bu + bv) > 1.0))
		{
			continue;
		}
		if ((RldRep_Dot(e2, q) * inv) > near)
		{
			return 1;
		}
	}
	return 0;
}

struct RldRepSample
{
	u32 patch;
	u32 t;
	double area;
};

static int RldRep_CompareSample(const void *x, const void *y)
{
	const struct RldRepSample *a = (const struct RldRepSample *)x;
	const struct RldRepSample *b = (const struct RldRepSample *)y;

	if (a->patch != b->patch)
	{
		return (a->patch < b->patch) ? -1 : 1;
	}
	if (a->area != b->area)
	{
		return (a->area > b->area) ? -1 : 1;
	}
	return (a->t < b->t) ? -1 : ((a->t > b->t) ? 1 : 0);
}

// The pieces in the order the rays are spent: largest area first, ties by
// piece. (patch is the piece, area its area, t unused.)
static int RldRep_ComparePiece(const void *x, const void *y)
{
	const struct RldRepSample *a = (const struct RldRepSample *)x;
	const struct RldRepSample *b = (const struct RldRepSample *)y;

	if (a->area != b->area)
	{
		return (a->area > b->area) ? -1 : 1;
	}
	return (a->patch < b->patch) ? -1 : ((a->patch > b->patch) ? 1 : 0);
}

// The middle of triangle t and its normal (twice its area long), turned by
// rel[t].
static void RldRep_Middle(const struct RldRepState *s, u32 t, double middle[3], double normal[3])
{
	const double *a = RldRep_At(s, s->tri[3u * t]);
	const double *b = RldRep_At(s, s->tri[3u * t + 1u]);
	const double *c = RldRep_At(s, s->tri[3u * t + 2u]);
	int k;

	RldRep_Cross(a, b, c, normal);
	for (k = 0; k < 3; k++)
	{
		middle[k] = (a[k] + b[k] + c[k]) / 3.0;
		if (s->rel[t])
		{
			normal[k] = -normal[k];
		}
	}
}

// The rays of step 3a for the open pieces (see the head of the file).
static int RldRep_CastRays(struct RldRepState *s)
{
	struct RldRepSample *samples;
	struct RldRepSample *pieces;
	u32 *first;
	size_t sampleCount = 0;
	size_t pieceCount = 0;
	u32 maxRays = (s->m > 0u) ? (RLDREP_RAY_BUDGET / s->m) : 0u;
	u32 rays = 0;
	const double near = RLDREP_RAY_NEAR * s->diagonal;
	u32 t;
	u32 p;
	size_t i;

	samples = (struct RldRepSample *)malloc(((size_t)s->m + 1u) * sizeof(*samples));
	pieces = (struct RldRepSample *)malloc(((size_t)s->patchCount + 1u) * sizeof(*pieces));
	first = (u32 *)malloc(((size_t)s->patchCount + 1u) * sizeof(u32));
	if ((samples == NULL) || (pieces == NULL) || (first == NULL))
	{
		free(samples);
		free(pieces);
		free(first);
		return 0;
	}

	for (t = 0; t < s->m; t++)
	{
		if ((s->patch[t] != RLDREP_NONE) && !s->closed[s->patch[t]])
		{
			samples[sampleCount].patch = s->patch[t];
			samples[sampleCount].t = t;
			samples[sampleCount].area = s->area[t];
			sampleCount++;
		}
	}
	qsort(samples, sampleCount, sizeof(*samples), RldRep_CompareSample);

	for (p = 0; p < s->patchCount; p++)
	{
		first[p] = RLDREP_NONE;
		if (!s->closed[p])
		{
			pieces[pieceCount].patch = p;
			pieces[pieceCount].t = 0;
			pieces[pieceCount].area = s->patchArea[p];
			pieceCount++;
		}
	}
	for (i = sampleCount; i > 0u; i--)
	{
		first[samples[i - 1u].patch] = (u32)(i - 1u);
	}
	qsort(pieces, pieceCount, sizeof(*pieces), RldRep_ComparePiece);

	for (i = 0; i < pieceCount; i++)
	{
		const u32 piece = pieces[i].patch;
		u32 j = first[piece];
		u32 taken = 0;

		while ((j < sampleCount) && (samples[j].patch == piece) && (taken < RLDREP_RAY_SAMPLES) && ((rays + 2u) <= maxRays))
		{
			const u32 u = samples[j].t;
			double middle[3];
			double normal[3];
			double length;

			RldRep_Middle(s, u, middle, normal);
			length = RldRep_Sqrt(RldRep_Dot(normal, normal));
			if (length > 0.0)
			{
				const double up[3] = {normal[0] / length, normal[1] / length, normal[2] / length};
				const double down[3] = {-up[0], -up[1], -up[2]};
				const int hitUp = RldRep_RayHits(s, middle, up, u, near);
				const int hitDown = RldRep_RayHits(s, middle, down, u, near);

				rays += 2u;
				s->raySum[piece] += s->area[u];
				if (!hitUp && hitDown)
				{
					s->rayOut[piece] += s->area[u];
				}
				else if (hitUp && !hitDown)
				{
					s->rayIn[piece] += s->area[u];
				}
			}
			taken++;
			j++;
		}
	}

	free(samples);
	free(pieces);
	free(first);
	return 1;
}

// Pieces, one winding per piece, and each piece outward. full = 1: every rule
// (step 3); full = 0: closed pieces by their volume only, open pieces keep
// their winding (step 5, after the fills).
static int RldRep_Orient(struct RldRepState *s, int full)
{
	struct RldRepEdge *e;
	size_t edgeCount;
	size_t i;
	u32 t;
	u32 p;

	// The second of each pair of twins is no part of any piece.
	e = RldRep_Edges(s->tri, s->m, s->twin, 2u, &edgeCount);
	if (e == NULL)
	{
		return 0;
	}

	for (i = 0; i < (3u * (size_t)s->m); i++)
	{
		s->nb[i] = RLDREP_NONE;
		s->same[i] = 0u;
	}
	i = 0;
	while (i < edgeCount)
	{
		const size_t r = RldRep_Run(e, edgeCount, i);

		if (r == 2u)
		{
			const struct RldRepEdge *x = &e[i];
			const struct RldRepEdge *y = &e[i + 1u];
			const u8 same = (s->tri[3u * x->t + x->k] == s->tri[3u * y->t + y->k]) ? 1u : 0u;

			s->nb[3u * x->t + x->k] = y->t;
			s->nb[3u * y->t + y->k] = x->t;
			s->same[3u * x->t + x->k] = same;
			s->same[3u * y->t + y->k] = same;
		}
		i += r;
	}

	// The walk: from the lowest triangle of each piece, in index order.
	s->patchCount = 0;
	for (t = 0; t < s->m; t++)
	{
		s->patch[t] = RLDREP_NONE;
		s->rel[t] = 0u;
	}
	for (t = 0; t < s->m; t++)
	{
		u32 head = 0;
		u32 tail = 0;

		if ((s->twin[t] == 2u) || (s->patch[t] != RLDREP_NONE))
		{
			continue;
		}
		s->patch[t] = s->patchCount;
		s->queue[tail++] = t;
		while (head < tail)
		{
			const u32 u = s->queue[head++];
			u32 k;

			for (k = 0; k < 3u; k++)
			{
				const u32 v = s->nb[3u * u + k];

				if ((v != RLDREP_NONE) && (s->patch[v] == RLDREP_NONE))
				{
					s->patch[v] = s->patchCount;
					s->rel[v] = (u8)(s->rel[u] ^ s->same[3u * u + k]);
					s->queue[tail++] = v;
				}
			}
		}
		s->patchCount++;
	}

	for (p = 0; p < s->patchCount; p++)
	{
		s->closed[p] = 1u;
		s->turn[p] = 0u;
		s->patchArea[p] = 0.0;
		s->area0[p] = 0.0;
		s->area1[p] = 0.0;
		s->volume[p] = 0.0;
		s->vote[p] = 0.0;
		s->rayIn[p] = 0.0;
		s->rayOut[p] = 0.0;
		s->raySum[p] = 0.0;
	}

	// Closed: every edge of a piece is run equally often both ways by the
	// piece's own triangles (with rel applied).
	i = 0;
	while (i < edgeCount)
	{
		const size_t r = RldRep_Run(e, edgeCount, i);
		size_t x;

		for (x = i; x < (i + r); x++)
		{
			const u32 px = s->patch[e[x].t];
			int sum = 0;
			int seen = 0;
			size_t y;

			for (y = i; y < x; y++)
			{
				if (s->patch[e[y].t] == px)
				{
					seen = 1;
					break;
				}
			}
			if (seen)
			{
				continue;
			}
			for (y = x; y < (i + r); y++)
			{
				if (s->patch[e[y].t] == px)
				{
					const int forward = (s->tri[3u * e[y].t + e[y].k] == e[y].lo) ? 1 : -1;

					sum += s->rel[e[y].t] ? -forward : forward;
				}
			}
			if (sum != 0)
			{
				s->closed[px] = 0u;
			}
		}
		i += r;
	}
	free(e);

	// Area, volume (about the middle of the box) and the vote of the middle
	// of the model, per piece.
	for (t = 0; t < s->m; t++)
	{
		const u32 piece = s->patch[t];
		double a[3];
		double b[3];
		double c[3];
		double bc[3];
		double middle[3];
		double normal[3];
		double away[3];
		double length;
		double det;
		int k;

		if (piece == RLDREP_NONE)
		{
			continue;
		}
		for (k = 0; k < 3; k++)
		{
			a[k] = RldRep_At(s, s->tri[3u * t])[k] - s->center[k];
			b[k] = RldRep_At(s, s->tri[3u * t + 1u])[k] - s->center[k];
			c[k] = RldRep_At(s, s->tri[3u * t + 2u])[k] - s->center[k];
		}
		RldRep_CrossV(b, c, bc);
		det = RldRep_Dot(a, bc) / 6.0;
		s->volume[piece] += s->rel[t] ? -det : det;
		s->patchArea[piece] += s->area[t];
		if (s->rel[t])
		{
			s->area1[piece] += s->area[t];
		}
		else
		{
			s->area0[piece] += s->area[t];
		}

		RldRep_Middle(s, t, middle, normal);
		for (k = 0; k < 3; k++)
		{
			away[k] = middle[k] - s->centroid[k];
		}
		length = RldRep_Sqrt(RldRep_Dot(away, away));
		if (length > 0.0)
		{
			s->vote[piece] += 0.5 * RldRep_Dot(normal, away) / length;
		}
	}

	if (full && !RldRep_CastRays(s))
	{
		return 0;
	}

	for (p = 0; p < s->patchCount; p++)
	{
		const double area = s->patchArea[p];
		const double tiny = RLDREP_VOLUME_MIN * area * RldRep_Sqrt(area);
		u8 turn;

		if (s->closed[p] && (s->volume[p] > tiny))
		{
			turn = 0u;
		}
		else if (s->closed[p] && (s->volume[p] < -tiny))
		{
			turn = 1u;
		}
		else if (!full || s->closed[p])
		{
			// No volume, or after the fills: the fewest triangles turn.
			turn = (s->area1[p] > s->area0[p]) ? 1u : 0u;
		}
		else if ((s->raySum[p] > 0.0) && ((s->rayIn[p] - s->rayOut[p]) > (RLDREP_RAY_MARGIN * s->raySum[p])))
		{
			turn = 1u;
		}
		else if ((s->raySum[p] > 0.0) && ((s->rayOut[p] - s->rayIn[p]) > (RLDREP_RAY_MARGIN * s->raySum[p])))
		{
			turn = 0u;
		}
		else if (s->area1[p] >= (RLDREP_MAJORITY * area))
		{
			turn = 1u;
		}
		else if (s->area0[p] >= (RLDREP_MAJORITY * area))
		{
			turn = 0u;
		}
		else
		{
			turn = (s->vote[p] < (-RLDREP_CENTROID_MARGIN * area)) ? 1u : 0u;
		}
		s->turn[p] = turn;
	}

	// Twins are never turned: the pair faces both ways already.
	for (t = 0; t < s->m; t++)
	{
		if ((s->patch[t] != RLDREP_NONE) && (s->twin[t] == 0u) && (s->rel[t] ^ s->turn[s->patch[t]]))
		{
			RldRep_Flip(s, t);
		}
	}
	return 1;
}

//========================================================================================
// STEP 4: HOLES
//========================================================================================

// One boundary half-edge: from -> to, against the direction of the edge's
// only triangle t (corner k -> k + 1 of t runs to -> from).
struct RldRepBoundary
{
	u32 from;
	u32 to;
	u32 t;
	u32 k;
};

static int RldRep_CompareBoundary(const void *x, const void *y)
{
	const struct RldRepBoundary *a = (const struct RldRepBoundary *)x;
	const struct RldRepBoundary *b = (const struct RldRepBoundary *)y;

	if (a->t != b->t)
	{
		return (a->t < b->t) ? -1 : 1;
	}
	return (a->k < b->k) ? -1 : ((a->k > b->k) ? 1 : 0);
}

// The boundary edge of triangle t, corner k, in the sorted list; RLDREP_NONE
// when there is none.
static u32 RldRep_FindBoundary(const struct RldRepBoundary *list, size_t count, u32 t, u32 k)
{
	size_t low = 0;
	size_t high = count;

	while (low < high)
	{
		const size_t mid = low + (high - low) / 2u;

		if ((list[mid].t < t) || ((list[mid].t == t) && (list[mid].k < k)))
		{
			low = mid + 1u;
		}
		else
		{
			high = mid;
		}
	}
	return ((low < count) && (list[low].t == t) && (list[low].k == k)) ? (u32)low : RLDREP_NONE;
}

// The first half-edge of the edge (a, b) in the sorted half-edges and the
// number of its triangles (0 when the edge does not exist).
static size_t RldRep_FindRun(const struct RldRepEdge *e, size_t count, u32 a, u32 b, size_t *length)
{
	const u32 lo = (a < b) ? a : b;
	const u32 hi = (a < b) ? b : a;
	size_t low = 0;
	size_t high = count;

	while (low < high)
	{
		const size_t mid = low + (high - low) / 2u;

		if ((e[mid].lo < lo) || ((e[mid].lo == lo) && (e[mid].hi < hi)))
		{
			low = mid + 1u;
		}
		else
		{
			high = mid;
		}
	}
	*length = ((low < count) && (e[low].lo == lo) && (e[low].hi == hi)) ? RldRep_Run(e, count, low) : 0u;
	return low;
}

// The corner of triangle t at position v (3 when none).
static u32 RldRep_CornerOf(const struct RldRepState *s, u32 t, u32 v)
{
	u32 k;

	for (k = 0; k < 3u; k++)
	{
		if (s->tri[3u * t + k] == v)
		{
			break;
		}
	}
	return k;
}

// The boundary edge that follows b in its loop. It starts at y = b->to and is
// found by walking round y through the fan of triangles on this side: from
// the triangle of b across its other edge at y to the triangle on the far
// side (the edge must have exactly two triangles, running it opposite ways),
// and on, until a triangle's edge at y is open. So two holes that touch at
// one position are two loops, each closed on its own side. 0 when the walk
// meets an edge with three or more triangles or with two that run it the
// same way (no loop is made through such a position).
static int RldRep_NextBoundary(const struct RldRepState *s, const struct RldRepEdge *e, size_t edgeCount, const struct RldRepBoundary *b,
                               struct RldRepBoundary *next)
{
	const u32 y = b->to;
	u32 cur = b->t;
	u32 j = b->k;
	u32 steps;

	for (steps = 0; steps < s->m; steps++)
	{
		// cur runs w -> y with corner (j + 2) -> j.
		const u32 w = s->tri[3u * cur + ((j + 2u) % 3u)];
		size_t length;
		const size_t at = RldRep_FindRun(e, edgeCount, w, y, &length);
		u32 u;

		if (length == 1u)
		{
			next->from = y;
			next->to = w;
			next->t = cur;
			next->k = (j + 2u) % 3u;
			return 1;
		}
		if (length != 2u)
		{
			return 0;
		}
		u = (e[at].t == cur) ? e[at + 1u].t : e[at].t;
		j = RldRep_CornerOf(s, u, y);
		if ((u == cur) || (u == b->t) || (j > 2u) || (s->tri[3u * u + ((j + 1u) % 3u)] != w))
		{
			return 0;
		}
		cur = u;
	}
	return 0;
}

// The working corner of triangle t at position v carries this input corner.
static u32 RldRep_CornerAt(const struct RldRepState *s, u32 t, u32 v)
{
	const u32 k = RldRep_CornerOf(s, t, v);

	return (k < 3u) ? s->corner[3u * t + k] : RLDREP_NONE;
}

// Is the edge (a, b) among count triangles (3 positions each)?
// 1 when the triangle a, b, c is a needle (see RLDREP_NEEDLE) or flat.
static int RldRep_Needle(const double *a, const double *b, const double *c)
{
	double n[3];
	double longest = RldRep_Distance2(a, b);
	const double l1 = RldRep_Distance2(b, c);
	const double l2 = RldRep_Distance2(c, a);

	if (l1 > longest)
	{
		longest = l1;
	}
	if (l2 > longest)
	{
		longest = l2;
	}
	RldRep_Cross(a, b, c, n);
	return RldRep_Dot(n, n) <= (RLDREP_NEEDLE * RLDREP_NEEDLE * longest * longest);
}

// 1 when the segment p-q passes through the triangle a, b, c strictly inside
// (see RLDREP_CROSS_MARGIN); a segment in the plane of the triangle does not.
static int RldRep_SegmentCrosses(const double *p, const double *q, const double *a, const double *b, const double *c)
{
	const double d[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
	const double e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
	const double sv[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
	double pv[3];
	double qv[3];
	double det;
	double inv;
	double u;
	double v;
	double t;

	RldRep_CrossV(d, e2, pv);
	det = RldRep_Dot(e1, pv);
	if ((det * det) <= (1.0e-24 * RldRep_Dot(d, d) * RldRep_Dot(e1, e1) * RldRep_Dot(e2, e2)))
	{
		return 0;
	}
	inv = 1.0 / det;
	u = RldRep_Dot(sv, pv) * inv;
	if (!(u > RLDREP_CROSS_MARGIN) || !(u < (1.0 - RLDREP_CROSS_MARGIN)))
	{
		return 0;
	}
	RldRep_CrossV(sv, e1, qv);
	v = RldRep_Dot(d, qv) * inv;
	if (!(v > RLDREP_CROSS_MARGIN) || !((u + v) < (1.0 - RLDREP_CROSS_MARGIN)))
	{
		return 0;
	}
	t = RldRep_Dot(e2, qv) * inv;
	return (t > RLDREP_CROSS_MARGIN) && (t < (1.0 - RLDREP_CROSS_MARGIN));
}

// 1 when the triangles (positions) x and y, which share no position, pass
// through each other: an edge of one through the other.
static int RldRep_TrianglesCross(const struct RldRepState *s, const u32 *x, const u32 *y)
{
	double lo[3];
	double hi[3];
	u32 k;
	int a;

	// Boxes apart: no crossing.
	for (a = 0; a < 3; a++)
	{
		lo[a] = RldRep_At(s, x[0])[a];
		hi[a] = lo[a];
		for (k = 1; k < 3u; k++)
		{
			const double v = RldRep_At(s, x[k])[a];

			lo[a] = (v < lo[a]) ? v : lo[a];
			hi[a] = (v > hi[a]) ? v : hi[a];
		}
		if ((RldRep_At(s, y[0])[a] < lo[a]) && (RldRep_At(s, y[1])[a] < lo[a]) && (RldRep_At(s, y[2])[a] < lo[a]))
		{
			return 0;
		}
		if ((RldRep_At(s, y[0])[a] > hi[a]) && (RldRep_At(s, y[1])[a] > hi[a]) && (RldRep_At(s, y[2])[a] > hi[a]))
		{
			return 0;
		}
	}
	for (k = 0; k < 3u; k++)
	{
		if (RldRep_SegmentCrosses(RldRep_At(s, x[k]), RldRep_At(s, x[(k + 1u) % 3u]), RldRep_At(s, y[0]), RldRep_At(s, y[1]), RldRep_At(s, y[2])) ||
		    RldRep_SegmentCrosses(RldRep_At(s, y[k]), RldRep_At(s, y[(k + 1u) % 3u]), RldRep_At(s, x[0]), RldRep_At(s, x[1]), RldRep_At(s, x[2])))
		{
			return 1;
		}
	}
	return 0;
}

static int RldRep_SharePosition(const u32 *x, const u32 *y)
{
	u32 i;
	u32 j;

	for (i = 0; i < 3u; i++)
	{
		for (j = 0; j < 3u; j++)
		{
			if (x[i] == y[j])
			{
				return 1;
			}
		}
	}
	return 0;
}

// 1 when one of the count fill triangles passes through a working triangle
// or through another fill triangle (pairs that share a position only touch).
static int RldRep_FillCrosses(const struct RldRepState *s, const u32 *fill, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		const u32 *f = &fill[3u * i];
		u32 t;

		for (t = 0; t < s->m; t++)
		{
			if (!RldRep_SharePosition(f, &s->tri[3u * t]) && RldRep_TrianglesCross(s, f, &s->tri[3u * t]))
			{
				return 1;
			}
		}
		for (t = 0; t < i; t++)
		{
			if (!RldRep_SharePosition(f, &fill[3u * t]) && RldRep_TrianglesCross(s, f, &fill[3u * t]))
			{
				return 1;
			}
		}
	}
	return 0;
}

static int RldRep_FillHasEdge(const u32 *fill, u32 count, u32 a, u32 b)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		u32 k;

		for (k = 0; k < 3u; k++)
		{
			const u32 x = fill[3u * i + k];
			const u32 y = fill[3u * i + ((k + 1u) % 3u)];

			if (((x == a) && (y == b)) || ((x == b) && (y == a)))
			{
				return 1;
			}
		}
	}
	return 0;
}

// Ear clipping of one loop (positions loop[0..count-1] in loop order, the
// input corners they carry in source[], the triangles of its edges in
// loopTriangle[]). The fill goes to fill/fillSource (count - 2 triangles); no
// new edge may be one of the mesh (e) or of the fills of earlier loops
// (earlier). 0 = no fill (see step 4 at the head of the file); -1 = out of
// memory.
static int RldRep_FillLoop(const struct RldRepState *s, const struct RldRepEdge *e, size_t edgeCount, const u32 *earlier, u32 earlierCount,
                           const u32 *loop, const u32 *source, const u32 *loopTriangle, u32 count, u32 *fill, u32 *fillSource)
{
	u32 *left;
	double normal[3] = {0.0, 0.0, 0.0};
	double facing = 0.0;
	double around = 0.0;
	double nn;
	double perimeter = 0.0;
	u32 remaining = count;
	u32 made = 0;
	u32 i;

	// Newell's normal, about the first position (smaller terms).
	for (i = 0; i < count; i++)
	{
		const double *o = RldRep_At(s, loop[0]);
		const double *a = RldRep_At(s, loop[i]);
		const double *b = RldRep_At(s, loop[(i + 1u) % count]);
		const double ax = a[0] - o[0];
		const double ay = a[1] - o[1];
		const double az = a[2] - o[2];
		const double bx = b[0] - o[0];
		const double by = b[1] - o[1];
		const double bz = b[2] - o[2];

		normal[0] += (ay - by) * (az + bz);
		normal[1] += (az - bz) * (ax + bx);
		normal[2] += (ax - bx) * (ay + by);
		perimeter += RldRep_Distance2(a, b);
	}
	nn = RldRep_Dot(normal, normal);
	if (!(nn > (RLDREP_FLAT * RLDREP_FLAT * perimeter * perimeter)))
	{
		return 0;
	}

	// The surface around the loop must not face against the fill: sum of
	// area x cosine = sum of (cross . N) / (2 |N|), against the sum of the
	// areas.
	for (i = 0; i < count; i++)
	{
		const u32 t = loopTriangle[i];
		double n[3];

		RldRep_Cross(RldRep_At(s, s->tri[3u * t]), RldRep_At(s, s->tri[3u * t + 1u]), RldRep_At(s, s->tri[3u * t + 2u]), n);
		facing += 0.5 * RldRep_Dot(n, normal);
		around += s->area[t];
	}
	if (!(facing > (RLDREP_HOLE_FACING * around * RldRep_Sqrt(nn))))
	{
		return 0;
	}

	left = (u32 *)malloc(((size_t)count + 1u) * sizeof(u32));
	if (left == NULL)
	{
		return -1;
	}
	for (i = 0; i < count; i++)
	{
		left[i] = i;
	}

	while (remaining > 3u)
	{
		u32 best = RLDREP_NONE;
		double bestCos = 0.0;

		for (i = 0; i < remaining; i++)
		{
			const u32 ip = left[(i + remaining - 1u) % remaining];
			const u32 iv = left[i];
			const u32 in2 = left[(i + 1u) % remaining];
			const double *a = RldRep_At(s, loop[ip]);
			const double *v = RldRep_At(s, loop[iv]);
			const double *d = RldRep_At(s, loop[in2]);
			double turn[3];
			double pa[3];
			double pd[3];
			double la;
			double ld;
			double cosine;
			double ka;
			double kd;
			u32 w;
			int k;
			int inside = 0;

			RldRep_Cross(a, v, d, turn);
			if (!(RldRep_Dot(turn, normal) > 0.0) || RldRep_Needle(a, v, d))
			{
				continue;
			}
			if (RldRep_HasEdge(e, edgeCount, loop[ip], loop[in2]) || RldRep_FillHasEdge(fill, made, loop[ip], loop[in2]) ||
			    RldRep_FillHasEdge(earlier, earlierCount, loop[ip], loop[in2]))
			{
				continue;
			}
			for (w = 0; w < remaining; w++)
			{
				const u32 iw = left[w];
				const double *q = RldRep_At(s, loop[iw]);
				double s0[3];
				double s1[3];
				double s2[3];

				if ((iw == ip) || (iw == iv) || (iw == in2))
				{
					continue;
				}
				RldRep_Cross(a, v, q, s0);
				RldRep_Cross(v, d, q, s1);
				RldRep_Cross(d, a, q, s2);
				if ((RldRep_Dot(s0, normal) >= 0.0) && (RldRep_Dot(s1, normal) >= 0.0) && (RldRep_Dot(s2, normal) >= 0.0))
				{
					inside = 1;
					break;
				}
			}
			if (inside)
			{
				continue;
			}

			// The angle at the tip, in the plane of the loop.
			for (k = 0; k < 3; k++)
			{
				pa[k] = a[k] - v[k];
				pd[k] = d[k] - v[k];
			}
			ka = RldRep_Dot(pa, normal) / nn;
			kd = RldRep_Dot(pd, normal) / nn;
			for (k = 0; k < 3; k++)
			{
				pa[k] -= ka * normal[k];
				pd[k] -= kd * normal[k];
			}
			la = RldRep_Dot(pa, pa);
			ld = RldRep_Dot(pd, pd);
			if (!(la > 0.0) || !(ld > 0.0))
			{
				continue;
			}
			cosine = RldRep_Dot(pa, pd) / RldRep_Sqrt(la * ld);
			if ((best == RLDREP_NONE) || (cosine > bestCos))
			{
				best = i;
				bestCos = cosine;
			}
		}

		if (best == RLDREP_NONE)
		{
			free(left);
			return 0;
		}

		{
			const u32 ip = left[(best + remaining - 1u) % remaining];
			const u32 iv = left[best];
			const u32 in2 = left[(best + 1u) % remaining];

			fill[3u * made + 0u] = loop[ip];
			fill[3u * made + 1u] = loop[iv];
			fill[3u * made + 2u] = loop[in2];
			fillSource[3u * made + 0u] = source[ip];
			fillSource[3u * made + 1u] = source[iv];
			fillSource[3u * made + 2u] = source[in2];
			made++;
		}
		for (i = best; (i + 1u) < remaining; i++)
		{
			left[i] = left[i + 1u];
		}
		remaining--;
	}

	{
		const double *a = RldRep_At(s, loop[left[0]]);
		const double *v = RldRep_At(s, loop[left[1]]);
		const double *d = RldRep_At(s, loop[left[2]]);
		double turn[3];

		RldRep_Cross(a, v, d, turn);
		if (!(RldRep_Dot(turn, normal) > 0.0) || RldRep_Needle(a, v, d))
		{
			free(left);
			return 0;
		}
		for (i = 0; i < 3u; i++)
		{
			fill[3u * made + i] = loop[left[i]];
			fillSource[3u * made + i] = source[left[i]];
		}
		made++;
	}

	free(left);
	return RldRep_FillCrosses(s, fill, made) ? 0 : 1;
}

// One loop through distinct positions: filled (1), not filled (0), out of
// memory (-1). Loops of fewer than three edges are not filled.
static int RldRep_FillAndAdd(struct RldRepState *s, RldRepResult *out, const struct RldRepEdge *e, size_t edgeCount, u32 firstAdded, const u32 *loop,
                             const u32 *source, const u32 *loopTriangle, u32 count, u32 *fill, u32 *fillSource)
{
	int result;
	u32 j;

	if (count < 3u)
	{
		return 0;
	}
	result = RldRep_FillLoop(s, e, edgeCount, &s->tri[3u * firstAdded], s->m - firstAdded, loop, source, loopTriangle, count, fill, fillSource);
	if (result <= 0)
	{
		return result;
	}
	for (j = 0; j < (count - 2u); j++)
	{
		const u32 t = s->m;
		u32 k;

		for (k = 0; k < 3u; k++)
		{
			s->tri[3u * t + k] = fill[3u * j + k];
			s->corner[3u * t + k] = fillSource[3u * j + k];
		}
		s->twin[t] = 0u;
		s->added[t] = 1u;
		s->parity[t] = 0u;
		s->source[t] = RLDREP_NONE;
		s->area[t] = RldRep_TriangleArea(s, t);
		s->m++;
	}
	out->holesClosed++;
	out->holeTriangles += count - 2u;
	return 1;
}

// Step 4: every boundary loop of at most holeMaxEdges edges is filled where
// it can be.
static int RldRep_Holes(struct RldRepState *s, RldRepResult *out)
{
	const u32 limit = s->in->holeMaxEdges;
	const u32 firstAdded = s->m;
	struct RldRepEdge *e;
	struct RldRepBoundary *boundary = NULL;
	u8 *visited = NULL;
	u32 *loop = NULL;
	u32 *loopEdge = NULL;
	u32 *source = NULL;
	u32 *loopTriangle = NULL;
	u32 *fill = NULL;
	u32 *fillSource = NULL;
	size_t edgeCount;
	size_t boundaryCount = 0;
	size_t i;
	int ok = 0;

	if (limit < 3u)
	{
		return 1;
	}

	e = RldRep_Edges(s->tri, s->m, NULL, 0u, &edgeCount);
	if (e == NULL)
	{
		return 0;
	}

	boundary = (struct RldRepBoundary *)malloc((edgeCount + 1u) * sizeof(*boundary));
	visited = (u8 *)calloc(edgeCount + 1u, 1u);
	loop = (u32 *)malloc(((size_t)limit + 1u) * sizeof(u32));
	loopEdge = (u32 *)malloc(((size_t)limit + 1u) * sizeof(u32));
	source = (u32 *)malloc(((size_t)limit + 1u) * sizeof(u32));
	loopTriangle = (u32 *)malloc(((size_t)limit + 1u) * sizeof(u32));
	fill = (u32 *)malloc(((size_t)limit + 1u) * 3u * sizeof(u32));
	fillSource = (u32 *)malloc(((size_t)limit + 1u) * 3u * sizeof(u32));
	if ((boundary == NULL) || (visited == NULL) || (loop == NULL) || (loopEdge == NULL) || (source == NULL) || (loopTriangle == NULL) ||
	    (fill == NULL) || (fillSource == NULL))
	{
		goto done;
	}

	i = 0;
	while (i < edgeCount)
	{
		const size_t r = RldRep_Run(e, edgeCount, i);

		if (r == 1u)
		{
			const u32 t = e[i].t;
			const u32 k = e[i].k;
			struct RldRepBoundary *b = &boundary[boundaryCount++];

			b->from = s->tri[3u * t + ((k + 1u) % 3u)];
			b->to = s->tri[3u * t + k];
			b->t = t;
			b->k = k;
		}
		i += r;
	}
	qsort(boundary, boundaryCount, sizeof(*boundary), RldRep_CompareBoundary);

	// Loops in the order of their first triangle; each boundary edge is
	// walked once.
	for (i = 0; i < boundaryCount; i++)
	{
		u32 at = (u32)i;
		u32 count = 0;
		int closed = 0;
		int result;
		u32 j;

		if (visited[i])
		{
			continue;
		}
		for (;;)
		{
			struct RldRepBoundary next;
			u32 n;

			visited[at] = 1u;
			if (count < limit)
			{
				loop[count] = boundary[at].from;
				loopEdge[count] = at;
			}
			count++;
			if (!RldRep_NextBoundary(s, e, edgeCount, &boundary[at], &next))
			{
				break;
			}
			n = RldRep_FindBoundary(boundary, boundaryCount, next.t, next.k);
			if (n == (u32)i)
			{
				closed = 1;
				break;
			}
			if ((n == RLDREP_NONE) || visited[n])
			{
				break;
			}
			at = n;
		}

		if (!closed || (count < 3u) || (count > limit))
		{
			continue;
		}
		for (j = 0; j < count; j++)
		{
			source[j] = RldRep_CornerAt(s, boundary[loopEdge[j]].t, loop[j]);
			loopTriangle[j] = boundary[loopEdge[j]].t;
		}

		// A loop through one position twice (two holes that the fan walk
		// could not tell apart there) is cut at that position into loops of
		// their own: the first repeat closes the loop between, which is
		// taken out and filled alone, and so on; the rest is filled last.
		for (;;)
		{
			u32 first = RLDREP_NONE;
			u32 again = RLDREP_NONE;
			u32 x;

			for (j = 1; (j < count) && (again == RLDREP_NONE); j++)
			{
				for (x = 0; x < j; x++)
				{
					if (loop[x] == loop[j])
					{
						first = x;
						again = j;
						break;
					}
				}
			}
			if (again == RLDREP_NONE)
			{
				result = RldRep_FillAndAdd(s, out, e, edgeCount, firstAdded, loop, source, loopTriangle, count, fill, fillSource);
				if (result < 0)
				{
					goto done;
				}
				break;
			}
			result = RldRep_FillAndAdd(s, out, e, edgeCount, firstAdded, &loop[first], &source[first], &loopTriangle[first], again - first, fill,
			                           fillSource);
			if (result < 0)
			{
				goto done;
			}
			for (x = again; x < count; x++)
			{
				loop[first + x - again] = loop[x];
				source[first + x - again] = source[x];
				loopTriangle[first + x - again] = loopTriangle[x];
			}
			count -= again - first;
		}
	}
	ok = 1;

done:
	free(e);
	free(boundary);
	free(visited);
	free(loop);
	free(loopEdge);
	free(source);
	free(loopTriangle);
	free(fill);
	free(fillSource);
	return ok;
}

//========================================================================================
// STEP 5: THE RESULT
//========================================================================================

static u32 RldRep_Root(u32 *parent, u32 i)
{
	while (parent[i] != i)
	{
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

// Open and non-manifold edges and the boundary pieces of the working
// triangles.
static int RldRep_Count(struct RldRepState *s, RldRepResult *out)
{
	size_t edgeCount;
	struct RldRepEdge *e = RldRep_Edges(s->tri, s->m, NULL, 0u, &edgeCount);
	u32 *parent;
	u8 *onBoundary;
	size_t i;
	u32 p;

	parent = (u32 *)malloc(((size_t)s->n + 1u) * sizeof(u32));
	onBoundary = (u8 *)calloc((size_t)s->n + 1u, 1u);
	if ((e == NULL) || (parent == NULL) || (onBoundary == NULL))
	{
		free(e);
		free(parent);
		free(onBoundary);
		return 0;
	}
	for (p = 0; p < s->n; p++)
	{
		parent[p] = p;
	}

	i = 0;
	while (i < edgeCount)
	{
		const size_t r = RldRep_Run(e, edgeCount, i);
		u32 count = 0;
		size_t x;

		for (x = i; x < (i + r); x++)
		{
			// The second of a pair of twins does not count.
			count += (s->twin[e[x].t] == 2u) ? 0u : 1u;
		}
		if (count > 2u)
		{
			out->nonManifoldEdges++;
		}
		if (r == 1u)
		{
			const u32 a = RldRep_Root(parent, e[i].lo);
			const u32 b = RldRep_Root(parent, e[i].hi);

			out->openEdgesAfter++;
			onBoundary[e[i].lo] = 1u;
			onBoundary[e[i].hi] = 1u;
			if (a != b)
			{
				parent[(a > b) ? a : b] = (a > b) ? b : a;
			}
		}
		i += r;
	}
	for (p = 0; p < s->n; p++)
	{
		if (onBoundary[p] && (RldRep_Root(parent, p) == p))
		{
			out->holesLeft++;
		}
	}

	free(e);
	free(parent);
	free(onBoundary);
	return 1;
}

//========================================================================================
// THE REPAIR
//========================================================================================

static void RldRep_Why(char *why, size_t whySize, const char *text, u32 value)
{
	if ((why != NULL) && (whySize > 0u))
	{
		snprintf(why, whySize, text, value);
	}
}

static int RldRep_CheckInput(const RldRepInput *in, char *why, size_t whySize)
{
	u32 i;

	if (in->positionCount > RLDREP_INPUT_MAX)
	{
		RldRep_Why(why, whySize, "%u positions are more than the repair takes", in->positionCount);
		return 0;
	}
	if (in->triangleCount > RLDREP_INPUT_MAX)
	{
		RldRep_Why(why, whySize, "%u triangles are more than the repair takes", in->triangleCount);
		return 0;
	}
	if (((in->positionCount > 0u) && (in->position == NULL)) || ((in->triangleCount > 0u) && (in->triangle == NULL)))
	{
		RldRep_Why(why, whySize, "no data for %u triangles", in->triangleCount);
		return 0;
	}
	if (!(in->weldDistance >= 0.0) || !(in->weldDistance <= RLDREP_COORD_MAX))
	{
		RldRep_Why(why, whySize, "the weld distance is not a number from 0 to %u", (u32)RLDREP_COORD_MAX);
		return 0;
	}
	if (in->holeMaxEdges > RLDREP_HOLE_LIMIT)
	{
		RldRep_Why(why, whySize, "holes of more than %u edges are not closed", RLDREP_HOLE_LIMIT);
		return 0;
	}
	for (i = 0; i < (3u * in->positionCount); i++)
	{
		const double v = in->position[i];

		if (!(v >= -RLDREP_COORD_MAX) || !(v <= RLDREP_COORD_MAX))
		{
			RldRep_Why(why, whySize, "position %u is not finite or too far out", i / 3u);
			return 0;
		}
	}
	for (i = 0; i < (3u * in->triangleCount); i++)
	{
		if (in->triangle[i] >= in->positionCount)
		{
			RldRep_Why(why, whySize, "triangle %u uses a position that does not exist", i / 3u);
			return 0;
		}
	}
	return 1;
}

void RldRep_Free(RldRepResult *result)
{
	if (result == NULL)
	{
		return;
	}
	free(result->positionMap);
	free(result->triangle);
	free(result->sourceCorner);
	free(result->added);
	free(result->open);
	memset(result, 0, sizeof(*result));
}

// The suggested weld distance for make-char: the longest side of the box of
// the positions a triangle of in uses (as RldRep_Repair measures it) /
// RLDREP_WELD_DIVISOR (see the head of the file). 0 for no triangles.
double RldRep_DefaultWeldUsed(const RldRepInput *in)
{
	double lo[3] = {0.0, 0.0, 0.0};
	double hi[3] = {0.0, 0.0, 0.0};
	double longest = 0.0;
	u32 i;
	int k;

	if ((in == NULL) || (in->position == NULL) || (in->triangle == NULL))
	{
		return 0.0;
	}
	for (i = 0; i < (3u * in->triangleCount); i++)
	{
		const double *p;

		if (in->triangle[i] >= in->positionCount)
		{
			return 0.0;
		}
		p = &in->position[3u * in->triangle[i]];
		for (k = 0; k < 3; k++)
		{
			lo[k] = ((i == 0u) || (p[k] < lo[k])) ? p[k] : lo[k];
			hi[k] = ((i == 0u) || (p[k] > hi[k])) ? p[k] : hi[k];
		}
	}
	for (k = 0; k < 3; k++)
	{
		longest = ((hi[k] - lo[k]) > longest) ? (hi[k] - lo[k]) : longest;
	}
	return longest / RLDREP_WELD_DIVISOR;
}

// The same over ALL given positions - equal to RldRep_DefaultWeldUsed when
// every position is used (make-char drops unused ones before the repair).
double RldRep_DefaultWeld(u32 positionCount, const double *position)
{
	double lo[3];
	double hi[3];
	double longest = 0.0;
	u32 i;
	int k;

	if ((positionCount == 0u) || (position == NULL))
	{
		return 0.0;
	}
	for (k = 0; k < 3; k++)
	{
		lo[k] = position[k];
		hi[k] = position[k];
	}
	for (i = 1; i < positionCount; i++)
	{
		for (k = 0; k < 3; k++)
		{
			const double v = position[3u * i + (u32)k];

			lo[k] = (v < lo[k]) ? v : lo[k];
			hi[k] = (v > hi[k]) ? v : hi[k];
		}
	}
	for (k = 0; k < 3; k++)
	{
		longest = ((hi[k] - lo[k]) > longest) ? (hi[k] - lo[k]) : longest;
	}
	return longest / RLDREP_WELD_DIVISOR;
}

// 1 = repaired (also when holes remain): out holds the result (free with
// RldRep_Free). 0 = invalid input or out of memory: out is empty and why (may
// be NULL) says which.
int RldRep_Repair(const RldRepInput *in, RldRepResult *out, char *why, size_t whySize)
{
	struct RldRepState s;
	size_t room;
	double lo[3] = {0.0, 0.0, 0.0};
	double hi[3] = {0.0, 0.0, 0.0};
	double total = 0.0;
	int first = 1;
	int ok = 1;
	u32 t;
	u32 i;
	int k;

	if (out != NULL)
	{
		memset(out, 0, sizeof(*out));
	}
	if ((why != NULL) && (whySize > 0u))
	{
		why[0] = '\0';
	}
	if ((in == NULL) || (out == NULL))
	{
		RldRep_Why(why, whySize, "no input (%u)", 0u);
		return 0;
	}
	if (!RldRep_CheckInput(in, why, whySize))
	{
		return 0;
	}

	memset(&s, 0, sizeof(s));
	s.in = in;
	s.n = in->positionCount;

	out->openEdgesBefore = RldRep_OpenEdges(in->triangle, in->triangleCount, &ok);

	// Step 2b adds at most as many triangles as the input has; it closes
	// open edges and opens none. Fills add at most one triangle per open edge
	// (a loop of k edges gets k - 2), and there are at most three open edges
	// per input triangle.
	room = (size_t)in->triangleCount * 5u + 1u;
	s.capacity = (u32)room;
	s.map = (u32 *)malloc(((size_t)s.n + 1u) * sizeof(u32));
	s.tri = (u32 *)malloc(room * 3u * sizeof(u32));
	s.corner = (u32 *)malloc(room * 3u * sizeof(u32));
	s.twin = (u8 *)malloc(room);
	s.added = (u8 *)malloc(room);
	s.parity = (u8 *)malloc(room);
	s.source = (u32 *)malloc(room * sizeof(u32));
	s.area = (double *)malloc(room * sizeof(double));
	s.patch = (u32 *)malloc(room * sizeof(u32));
	s.rel = (u8 *)malloc(room);
	s.nb = (u32 *)malloc(room * 3u * sizeof(u32));
	s.same = (u8 *)malloc(room * 3u);
	s.queue = (u32 *)malloc(room * sizeof(u32));
	s.closed = (u8 *)malloc(room);
	s.turn = (u8 *)malloc(room);
	s.patchArea = (double *)malloc(room * sizeof(double));
	s.area0 = (double *)malloc(room * sizeof(double));
	s.area1 = (double *)malloc(room * sizeof(double));
	s.volume = (double *)malloc(room * sizeof(double));
	s.vote = (double *)malloc(room * sizeof(double));
	s.rayIn = (double *)malloc(room * sizeof(double));
	s.rayOut = (double *)malloc(room * sizeof(double));
	s.raySum = (double *)malloc(room * sizeof(double));
	if (!ok || (s.map == NULL) || (s.tri == NULL) || (s.corner == NULL) || (s.twin == NULL) || (s.added == NULL) || (s.parity == NULL) || (s.source == NULL) ||
	    (s.area == NULL) || (s.patch == NULL) || (s.rel == NULL) || (s.nb == NULL) || (s.same == NULL) || (s.queue == NULL) ||
	    (s.closed == NULL) || (s.turn == NULL) || (s.patchArea == NULL) || (s.area0 == NULL) || (s.area1 == NULL) || (s.volume == NULL) ||
	    (s.vote == NULL) || (s.rayIn == NULL) || (s.rayOut == NULL) || (s.raySum == NULL))
	{
		RldRep_FreeState(&s);
		RldRep_Free(out);
		RldRep_Why(why, whySize, "out of memory (%u triangles)", in->triangleCount);
		return 0;
	}

	// The box of the used positions: its middle for the volumes, its
	// diagonal for the ray tolerance.
	for (i = 0; i < (3u * in->triangleCount); i++)
	{
		const double *p = &in->position[3u * in->triangle[i]];

		for (k = 0; k < 3; k++)
		{
			if (first || (p[k] < lo[k]))
			{
				lo[k] = p[k];
			}
			if (first || (p[k] > hi[k]))
			{
				hi[k] = p[k];
			}
		}
		first = 0;
	}
	if (first)
	{
		for (k = 0; k < 3; k++)
		{
			lo[k] = 0.0;
			hi[k] = 0.0;
		}
	}
	for (k = 0; k < 3; k++)
	{
		s.center[k] = 0.5 * (lo[k] + hi[k]);
		s.centroid[k] = 0.0;
	}
	{
		const double d[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};

		s.diagonal = RldRep_Sqrt(RldRep_Dot(d, d));
	}

	ok = RldRep_Weld(&s, &out->welded) && RldRep_Clean(&s, out) && RldRep_SplitCracks(&s, out);

	if (ok)
	{
		for (t = 0; t < s.m; t++)
		{
			double middle[3];
			double normal[3];

			s.rel[t] = 0u;
			RldRep_Middle(&s, t, middle, normal);
			for (k = 0; k < 3; k++)
			{
				s.centroid[k] += s.area[t] * middle[k];
			}
			total += s.area[t];
		}
		for (k = 0; k < 3; k++)
		{
			s.centroid[k] = (total > 0.0) ? (s.centroid[k] / total) : s.center[k];
		}
	}

	ok = ok && RldRep_Orient(&s, 1) && RldRep_Holes(&s, out) && RldRep_Orient(&s, 0) && RldRep_Count(&s, out);

	if (ok)
	{
		out->positionMap = (u32 *)malloc(((size_t)s.n + 1u) * sizeof(u32));
		out->triangle = (u32 *)malloc(((size_t)s.m + 1u) * 3u * sizeof(u32));
		out->sourceCorner = (u32 *)malloc(((size_t)s.m + 1u) * 3u * sizeof(u32));
		out->added = (u8 *)malloc((size_t)s.m + 1u);
		out->open = (u8 *)malloc((size_t)s.m + 1u);
		ok = (out->positionMap != NULL) && (out->triangle != NULL) && (out->sourceCorner != NULL) && (out->added != NULL) && (out->open != NULL);
	}
	if (!ok)
	{
		RldRep_FreeState(&s);
		RldRep_Free(out);
		RldRep_Why(why, whySize, "out of memory (%u triangles)", in->triangleCount);
		return 0;
	}

	memcpy(out->positionMap, s.map, (size_t)s.n * sizeof(u32));
	memcpy(out->triangle, s.tri, (size_t)s.m * 3u * sizeof(u32));
	memcpy(out->sourceCorner, s.corner, (size_t)s.m * 3u * sizeof(u32));
	memcpy(out->added, s.added, (size_t)s.m);
	out->triangleCount = s.m;

	// An open piece made of twins only has no triangle with open[] = 1 and
	// does not count as open (turn marks the pieces counted).
	for (i = 0; i < s.patchCount; i++)
	{
		s.turn[i] = 0u;
	}
	for (t = 0; t < s.m; t++)
	{
		const u32 piece = s.patch[t];

		out->open[t] = (u8)(((s.twin[t] == 0u) && (piece != RLDREP_NONE) && !s.closed[piece]) ? 1u : 0u);

		// The fan of a split triangle turns as one: counted once.
		if (!s.added[t] && s.parity[t] && ((t == 0u) || (s.source[t] != s.source[t - 1u])))
		{
			out->flipped++;
		}
		if (out->open[t] && !s.turn[piece])
		{
			s.turn[piece] = 1u;
			out->componentsOpen++;
		}
	}
	out->components = s.patchCount;

	RldRep_FreeState(&s);
	return 1;
}

//========================================================================================
// SELF-TEST
//========================================================================================
//
// Cubes with split corners (exact and 0.0005 apart), with turned triangles,
// inside out, with cracks (T-junctions: split with the color of the other
// side, with a seam, at a chain edge with two triangles, a slit too wide for
// the weld distance), a fill that would cross another triangle, with a 4-edge hole (closed, and left open with holeMaxEdges 3,
// and inside out), a sphere with a 16-edge hole (left open and turned by the
// rays; closed with holeMaxEdges 16), a wing beside a cube (kept as exported
// both ways, its outline not filled), a fin on a cube (non-manifold edge, the
// cube stays closed), degenerate triangles, exact doubles and twins, two
// holes touching at one position (an octahedron), the tie
// rules (weld: lower representative, no chains; ears: the first in loop
// order; open pieces: the lowest triangle keeps its winding), a bowtie, a
// larger sphere with split corners and turned triangles, and invalid input.
// Every result is checked (RldRep_TestValid) for: representatives are the
// lowest index of their set and within the weld distance, triangles use
// representatives only, no repeated corner, no flat triangle, no exact
// double, every source corner lies at its corner's position (a corner of a
// split triangle on an edge of its input triangle), kept triangles carry the
// corners of one input triangle in input order, fills come last and match
// holeTriangles, flipped matches the normals against the input, every
// edge with two triangles (no twin) is run both ways, open[] is 0 or 1, and
// a second run gives the same bytes.

struct RldRepTestMesh
{
	u32 positionCount;
	u32 positionCapacity;
	double *position;
	u32 triangleCount;
	u32 triangleCapacity;
	u32 *triangle;
	u32 *color;
	int failed;   // out of memory while building
};

struct RldRepTest
{
	void (*report)(const char *line);
	int failures;
};

static void RldRep_TestLine(struct RldRepTest *test, int ok, const char *name, const char *format, ...)
{
	char text[320];
	char line[400];
	va_list list;

	if (ok)
	{
		snprintf(line, sizeof(line), "  ok   repair %s", name);
	}
	else
	{
		va_start(list, format);
		vsnprintf(text, sizeof(text), format, list);
		va_end(list);
		snprintf(line, sizeof(line), "  FAIL repair %s: %s", name, text);
		test->failures++;
	}

	if (test->report != NULL)
	{
		test->report(line);
	}
}

static void RldRep_TestFree(struct RldRepTestMesh *t)
{
	free(t->position);
	free(t->triangle);
	free(t->color);
	memset(t, 0, sizeof(*t));
}

static u32 RldRep_TestPosition(struct RldRepTestMesh *t, double x, double y, double z)
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

// Corner colors are 0x100000 + the corner's number: each corner its own.
static void RldRep_TestTriangle(struct RldRepTestMesh *t, u32 a, u32 b, u32 c)
{
	u32 k;

	if (t->triangleCount == t->triangleCapacity)
	{
		const u32 capacity = (t->triangleCapacity < 64u) ? 64u : (2u * t->triangleCapacity);
		u32 *grown = (u32 *)realloc(t->triangle, (size_t)capacity * 3u * sizeof(u32));
		u32 *colors;

		if (grown == NULL)
		{
			t->failed = 1;
			return;
		}
		t->triangle = grown;
		colors = (u32 *)realloc(t->color, (size_t)capacity * 3u * sizeof(u32));
		if (colors == NULL)
		{
			t->failed = 1;
			return;
		}
		t->color = colors;
		t->triangleCapacity = capacity;
	}

	t->triangle[3u * t->triangleCount + 0u] = a;
	t->triangle[3u * t->triangleCount + 1u] = b;
	t->triangle[3u * t->triangleCount + 2u] = c;
	for (k = 0; k < 3u; k++)
	{
		t->color[3u * t->triangleCount + k] = 0x100000u + 3u * t->triangleCount + k;
	}
	t->triangleCount++;
}

// The faces of a cube, outward and counter-clockwise; corner c is at
// (c & 1, (c >> 1) & 1, (c >> 2) & 1). Order: -x, +x, -y, +y, -z, +z.
static const u32 s_rldRepCubeFace[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};

#define RLDREP_TEST_SPLIT 1     // every face its own four positions
#define RLDREP_TEST_INSIDE_OUT 2

// A cube of side size at origin (12 triangles, face f -> triangles 2f and
// 2f + 1), without face skip (6 = none). Split: the copies of face f lie
// f x jitter further along x.
static u32 RldRep_TestCube(struct RldRepTestMesh *t, double x, double y, double z, double size, int flags, u32 skip, double jitter)
{
	const u32 base = t->positionCount;
	u32 f;
	u32 c;

	if (!(flags & RLDREP_TEST_SPLIT))
	{
		for (c = 0; c < 8u; c++)
		{
			RldRep_TestPosition(t, x + size * (double)(c & 1u), y + size * (double)((c >> 1) & 1u), z + size * (double)((c >> 2) & 1u));
		}
	}

	for (f = 0; f < 6u; f++)
	{
		u32 q[4];
		u32 k;

		for (k = 0; k < 4u; k++)
		{
			c = s_rldRepCubeFace[f][k];
			if (flags & RLDREP_TEST_SPLIT)
			{
				q[k] = RldRep_TestPosition(t, x + size * (double)(c & 1u) + (double)f * jitter, y + size * (double)((c >> 1) & 1u),
				                           z + size * (double)((c >> 2) & 1u));
			}
			else
			{
				q[k] = base + c;
			}
		}
		if (f == skip)
		{
			continue;
		}
		if (flags & RLDREP_TEST_INSIDE_OUT)
		{
			RldRep_TestTriangle(t, q[0], q[2], q[1]);
			RldRep_TestTriangle(t, q[0], q[3], q[2]);
		}
		else
		{
			RldRep_TestTriangle(t, q[0], q[1], q[2]);
			RldRep_TestTriangle(t, q[0], q[2], q[3]);
		}
	}
	return base;
}

// The sine and cosine of k / n turns for the test shapes, by the angle sum
// formulas from exact values (no C library): n must be 16 or 32.
static void RldRep_TestTurn(u32 k, u32 n, double *sine, double *cosine)
{
	// cos and sin of 1/32 turn (11.25 degrees), rounded to double.
	const double c1 = 0.98078528040323043;
	const double s1 = 0.19509032201612825;
	double c = 1.0;
	double s = 0.0;
	u32 steps = (n == 16u) ? (2u * k) : k;
	u32 i;

	for (i = 0; i < steps; i++)
	{
		const double cn = c * c1 - s * s1;
		const double sn = s * c1 + c * s1;

		c = cn;
		s = sn;
	}
	*sine = s;
	*cosine = c;
}

// A sphere of radius r about (x, y, z): poles on y, rings - 1 rings of
// segments positions (segments 16 or 32, rings 8 or 16), outward; without
// the triangles at the north pole when open. Every triangle is turned when
// its normal would point inward, so the result is outward whatever the
// rounding; inside out reverses all.
static void RldRep_TestSphere(struct RldRepTestMesh *t, double x, double y, double z, double r, u32 segments, u32 rings, int open, int insideOut)
{
	const u32 north = RldRep_TestPosition(t, x, y + r, z);
	u32 south;
	u32 ring;
	u32 j;
	u32 first = t->triangleCount;
	u32 i;

	for (ring = 1; ring < rings; ring++)
	{
		double sp;
		double cp;

		RldRep_TestTurn(ring, 2u * rings, &sp, &cp);
		for (j = 0; j < segments; j++)
		{
			double sa;
			double ca;

			RldRep_TestTurn(j, segments, &sa, &ca);
			RldRep_TestPosition(t, x + r * sp * ca, y + r * cp, z + r * sp * sa);
		}
	}
	south = RldRep_TestPosition(t, x, y - r, z);

	for (j = 0; j < segments; j++)
	{
		const u32 j1 = (j + 1u) % segments;

		if (!open)
		{
			RldRep_TestTriangle(t, north, north + 1u + j, north + 1u + j1);
		}
		for (ring = 1; (ring + 1u) < rings; ring++)
		{
			const u32 a = north + 1u + (ring - 1u) * segments;
			const u32 b = a + segments;

			RldRep_TestTriangle(t, a + j, b + j, b + j1);
			RldRep_TestTriangle(t, a + j, b + j1, a + j1);
		}
		RldRep_TestTriangle(t, south, north + 1u + (rings - 2u) * segments + j1, north + 1u + (rings - 2u) * segments + j);
	}

	for (i = first; (i < t->triangleCount) && !t->failed; i++)
	{
		const double *a = &t->position[3u * t->triangle[3u * i]];
		const double *b = &t->position[3u * t->triangle[3u * i + 1u]];
		const double *c = &t->position[3u * t->triangle[3u * i + 2u]];
		double n[3];
		const double m[3] = {(a[0] + b[0] + c[0]) / 3.0 - x, (a[1] + b[1] + c[1]) / 3.0 - y, (a[2] + b[2] + c[2]) / 3.0 - z};
		int inward;

		RldRep_Cross(a, b, c, n);
		inward = RldRep_Dot(n, m) < 0.0;
		if (inward != (insideOut != 0))
		{
			const u32 swap = t->triangle[3u * i + 1u];

			t->triangle[3u * i + 1u] = t->triangle[3u * i + 2u];
			t->triangle[3u * i + 2u] = swap;
		}
	}
}

static RldRepInput RldRep_TestInput(const struct RldRepTestMesh *t, double weld, u32 holeMax)
{
	RldRepInput in;

	in.positionCount = t->positionCount;
	in.position = t->position;
	in.triangleCount = t->triangleCount;
	in.triangle = t->triangle;
	in.cornerColor = t->color;
	in.weldDistance = weld;
	in.holeMaxEdges = holeMax;
	return in;
}

static int RldRep_TestSameKey(const u32 *a, const u32 *b, int reverse)
{
	u32 k;

	for (k = 0; k < 3u; k++)
	{
		if (reverse)
		{
			if ((a[0] == b[k]) && (a[1] == b[(k + 2u) % 3u]) && (a[2] == b[(k + 1u) % 3u]))
			{
				return 1;
			}
		}
		else if ((a[0] == b[k]) && (a[1] == b[(k + 1u) % 3u]) && (a[2] == b[(k + 2u) % 3u]))
		{
			return 1;
		}
	}
	return 0;
}

// The checks every result must pass (see the head of the self-test).
static int RldRep_TestValid(const RldRepInput *in, const RldRepResult *r, char *detail, size_t detailSize)
{
	u8 *twin;
	u32 lastSource = 0;
	u32 splitFans = 0;
	int seenAdded = 0;
	u32 flipped = 0;
	u32 added = 0;
	u32 i;
	u32 t;

	for (i = 0; i < in->positionCount; i++)
	{
		const u32 rep = r->positionMap[i];

		if ((rep > i) || (r->positionMap[rep] != rep) ||
		    (RldRep_Distance2(&in->position[3u * i], &in->position[3u * rep]) > (in->weldDistance * in->weldDistance)))
		{
			snprintf(detail, detailSize, "position %u -> %u is no valid representative", i, rep);
			return 0;
		}
	}

	twin = (u8 *)calloc((size_t)r->triangleCount + 1u, 1u);
	if (twin == NULL)
	{
		snprintf(detail, detailSize, "out of memory");
		return 0;
	}
	for (t = 0; t < r->triangleCount; t++)
	{
		const u32 *c = &r->triangle[3u * t];
		u32 u;
		u32 k;

		int exact = 1;

		for (k = 0; k < 3u; k++)
		{
			const u32 source = r->sourceCorner[3u * t + k];
			int on = 0;

			if ((c[k] >= in->positionCount) || (r->positionMap[c[k]] != c[k]) || (source >= (3u * in->triangleCount)))
			{
				free(twin);
				snprintf(detail, detailSize, "triangle %u corner %u: position %u, source corner %u", t, k, c[k], source);
				return 0;
			}
			if (r->positionMap[in->triangle[source]] == c[k])
			{
				continue;
			}

			// Only a triangle split at a crack (step 2b) has a corner away
			// from its source corner: on an edge of its input triangle.
			exact = 0;
			if (!r->added[t] && (r->cracksSplit > 0u))
			{
				const u32 base = 3u * (source / 3u);
				u32 j;

				for (j = 0; j < 3u; j++)
				{
					const double *a = &in->position[3u * r->positionMap[in->triangle[base + j]]];
					const double *b = &in->position[3u * r->positionMap[in->triangle[base + ((j + 1u) % 3u)]]];
					const double *q = &in->position[3u * c[k]];
					const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
					const double aq[3] = {q[0] - a[0], q[1] - a[1], q[2] - a[2]};
					const double ab2 = RldRep_Dot(ab, ab);
					const double along = RldRep_Dot(aq, ab);
					const double tolerance = in->weldDistance + 1.0e-6 * RldRep_Sqrt(ab2);
					double side[3];

					RldRep_CrossV(aq, ab, side);
					on = on || ((along > 0.0) && (along < ab2) && (RldRep_Dot(side, side) <= (tolerance * tolerance * ab2)));
				}
			}
			if (!on)
			{
				free(twin);
				snprintf(detail, detailSize, "triangle %u corner %u: position %u is not at its source corner %u", t, k, c[k], source);
				return 0;
			}
		}
		if ((c[0] == c[1]) || (c[1] == c[2]) || (c[0] == c[2]) ||
		    RldRep_Flat(&in->position[3u * c[0]], &in->position[3u * c[1]], &in->position[3u * c[2]]))
		{
			free(twin);
			snprintf(detail, detailSize, "triangle %u is degenerate", t);
			return 0;
		}

		if (r->added[t])
		{
			seenAdded = 1;
			added++;
		}
		else
		{
			const u32 s0 = r->sourceCorner[3u * t];
			const u32 s1 = r->sourceCorner[3u * t + 1u];
			const u32 s2 = r->sourceCorner[3u * t + 2u];
			const u32 *source = &in->triangle[3u * (s0 / 3u)];
			double n[3];
			double m[3];

			// A triangle of a split fan may carry a corner of the other side
			// of the crack: its input order and turn are not checked here
			// (the crack cases check them by their numbers).
			if (seenAdded || ((((s0 / 3u) != (s1 / 3u)) || ((s0 / 3u) != (s2 / 3u))) && (r->cracksSplit == 0u)))
			{
				free(twin);
				snprintf(detail, detailSize, "triangle %u: source corners %u %u %u from different triangles", t, s0, s1, s2);
				return 0;
			}
			if (((s0 / 3u) != (s1 / 3u)) || ((s0 / 3u) != (s2 / 3u)))
			{
				splitFans++;
				goto checked;
			}

			// Kept triangles in input order (the fan of a split one together),
			// all three corners from one input triangle, each once (exact).
			if ((exact && (((s0 % 3u) + (s1 % 3u) + (s2 % 3u)) != 3u)) || ((t > 0u) && ((s0 / 3u) < lastSource)) ||
			    ((t > 0u) && exact && ((s0 / 3u) == lastSource)))
			{
				free(twin);
				snprintf(detail, detailSize, "triangle %u: source corners %u %u %u out of order", t, s0, s1, s2);
				return 0;
			}

			// Turned: the normal points against the input triangle's (counted
			// once per input triangle).
			RldRep_Cross(&in->position[3u * c[0]], &in->position[3u * c[1]], &in->position[3u * c[2]], n);
			RldRep_Cross(&in->position[3u * r->positionMap[source[0]]], &in->position[3u * r->positionMap[source[1]]],
			             &in->position[3u * r->positionMap[source[2]]], m);
			if ((t == 0u) || ((s0 / 3u) != lastSource))
			{
				flipped += (RldRep_Dot(n, m) < 0.0) ? 1u : 0u;
			}
			lastSource = s0 / 3u;
		}
	checked:
		if (r->open[t] > 1u)
		{
			free(twin);
			snprintf(detail, detailSize, "triangle %u: open %u", t, r->open[t]);
			return 0;
		}

		for (u = 0; u < t; u++)
		{
			if (RldRep_TestSameKey(&r->triangle[3u * u], c, 0))
			{
				free(twin);
				snprintf(detail, detailSize, "triangles %u and %u are doubles", u, t);
				return 0;
			}
			if (RldRep_TestSameKey(&r->triangle[3u * u], c, 1))
			{
				twin[u] = 1u;
				twin[t] = 1u;
			}
		}
	}
	if (((splitFans == 0u) && (flipped != r->flipped)) || (added != r->holeTriangles))
	{
		free(twin);
		snprintf(detail, detailSize, "flipped %u (counted %u), added %u (counted %u)", flipped, r->flipped, added, r->holeTriangles);
		return 0;
	}

	// Every edge with two triangles (no twin) is run both ways.
	for (t = 0; t < r->triangleCount; t++)
	{
		u32 k;

		if (twin[t])
		{
			continue;
		}
		for (k = 0; k < 3u; k++)
		{
			const u32 a = r->triangle[3u * t + k];
			const u32 b = r->triangle[3u * t + ((k + 1u) % 3u)];
			u32 users = 0;
			u32 twins = 0;
			u32 forward = 0;
			u32 u;

			for (u = 0; u < r->triangleCount; u++)
			{
				u32 j;

				for (j = 0; j < 3u; j++)
				{
					const u32 x = r->triangle[3u * u + j];
					const u32 y = r->triangle[3u * u + ((j + 1u) % 3u)];

					if (((x == a) && (y == b)) || ((x == b) && (y == a)))
					{
						users++;
						twins += twin[u];
						forward += (x == a) ? 1u : 0u;
					}
				}
			}
			if ((users == 2u) && (twins == 0u) && (forward != 1u))
			{
				free(twin);
				snprintf(detail, detailSize, "edge %u-%u of triangle %u is run the same way twice", a, b, t);
				return 0;
			}
		}
	}

	free(twin);
	return 1;
}

static int RldRep_TestSameResult(const RldRepResult *a, const RldRepResult *b, u32 positionCount)
{
	return (a->triangleCount == b->triangleCount) && (memcmp(a->positionMap, b->positionMap, (size_t)positionCount * sizeof(u32)) == 0) &&
	       (memcmp(a->triangle, b->triangle, (size_t)a->triangleCount * 3u * sizeof(u32)) == 0) &&
	       (memcmp(a->sourceCorner, b->sourceCorner, (size_t)a->triangleCount * 3u * sizeof(u32)) == 0) &&
	       (memcmp(a->added, b->added, a->triangleCount) == 0) && (memcmp(a->open, b->open, a->triangleCount) == 0) &&
	       (a->welded == b->welded) && (a->degenerate == b->degenerate) && (a->duplicate == b->duplicate) && (a->flipped == b->flipped) &&
	       (a->holesClosed == b->holesClosed) && (a->holeTriangles == b->holeTriangles) && (a->holesLeft == b->holesLeft) &&
	       (a->openEdgesBefore == b->openEdgesBefore) && (a->openEdgesAfter == b->openEdgesAfter) &&
	       (a->nonManifoldEdges == b->nonManifoldEdges) && (a->twins == b->twins) && (a->components == b->components) &&
	       (a->componentsOpen == b->componentsOpen) && (a->cracksSplit == b->cracksSplit);
}

// Runs the repair twice and checks the result; 1 = r holds a valid result
// (free with RldRep_Free), 0 = reported as failed.
static int RldRep_TestRun(struct RldRepTest *test, const char *name, const struct RldRepTestMesh *mesh, double weld, u32 holeMax, RldRepResult *r)
{
	RldRepInput in;
	RldRepResult again;
	char why[160];
	char detail[200];

	memset(r, 0, sizeof(*r));
	if (mesh->failed)
	{
		RldRep_TestLine(test, 0, name, "out of memory while building");
		return 0;
	}
	in = RldRep_TestInput(mesh, weld, holeMax);
	if (!RldRep_Repair(&in, r, why, sizeof(why)))
	{
		RldRep_TestLine(test, 0, name, "refused: %s", why);
		return 0;
	}
	if (!RldRep_TestValid(&in, r, detail, sizeof(detail)))
	{
		RldRep_TestLine(test, 0, name, "%s", detail);
		RldRep_Free(r);
		return 0;
	}
	if (!RldRep_Repair(&in, &again, why, sizeof(why)) || !RldRep_TestSameResult(r, &again, in.positionCount))
	{
		RldRep_Free(&again);
		RldRep_TestLine(test, 0, name, "a second run differs");
		RldRep_Free(r);
		return 0;
	}
	RldRep_Free(&again);
	return 1;
}

// The volume of triangles first..first+count-1 of a result.
static double RldRep_TestVolume(const struct RldRepTestMesh *mesh, const RldRepResult *r, u32 first, u32 count)
{
	double v = 0.0;
	u32 t;

	for (t = first; t < (first + count); t++)
	{
		double bc[3];

		RldRep_CrossV(&mesh->position[3u * r->triangle[3u * t + 1u]], &mesh->position[3u * r->triangle[3u * t + 2u]], bc);
		v += RldRep_Dot(&mesh->position[3u * r->triangle[3u * t]], bc) / 6.0;
	}
	return v;
}

// 1 when every triangle of the result faces away from the point m.
static int RldRep_TestOutward(const struct RldRepTestMesh *mesh, const RldRepResult *r, const double m[3])
{
	u32 t;

	for (t = 0; t < r->triangleCount; t++)
	{
		const double *a = &mesh->position[3u * r->triangle[3u * t]];
		const double *b = &mesh->position[3u * r->triangle[3u * t + 1u]];
		const double *c = &mesh->position[3u * r->triangle[3u * t + 2u]];
		const double away[3] = {(a[0] + b[0] + c[0]) / 3.0 - m[0], (a[1] + b[1] + c[1]) / 3.0 - m[1], (a[2] + b[2] + c[2]) / 3.0 - m[2]};
		double n[3];

		RldRep_Cross(a, b, c, n);
		if (!(RldRep_Dot(n, away) > 0.0))
		{
			return 0;
		}
	}
	return 1;
}

static u32 RldRep_TestOpenCount(const RldRepResult *r)
{
	u32 count = 0;
	u32 t;

	for (t = 0; t < r->triangleCount; t++)
	{
		count += r->open[t];
	}
	return count;
}

static void RldRep_SelfTestCubes(struct RldRepTest *test)
{
	const double middle[3] = {0.5, 0.5, 0.5};
	struct RldRepTestMesh mesh;
	RldRepResult r;
	const char *name;
	u32 t;

	// Split corners, exactly equal and 0.0005 apart.
	memset(&mesh, 0, sizeof(mesh));
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, RLDREP_TEST_SPLIT, 6u, 0.0);
	name = "cube, every corner split per face, weld 0: 24 -> 8 positions, closed";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 0u, &r))
	{
		RldRep_TestLine(test,
		                (r.welded == 16u) && (r.triangleCount == 12u) && (r.openEdgesBefore == 24u) && (r.openEdgesAfter == 0u) && (r.flipped == 0u) &&
		                    (r.components == 1u) && (r.componentsOpen == 0u) && (RldRep_TestOpenCount(&r) == 0u) && (r.holesLeft == 0u) &&
		                    (r.nonManifoldEdges == 0u) && (RldRep_TestVolume(&mesh, &r, 0u, 12u) > 0.99),
		                name, "welded %u, %u triangles, open edges %u -> %u, flipped %u, pieces %u/%u open", r.welded, r.triangleCount, r.openEdgesBefore,
		                r.openEdgesAfter, r.flipped, r.components, r.componentsOpen);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, RLDREP_TEST_SPLIT, 6u, 0.0001);
	name = "cube, split corners up to 0.0005 apart, weld 0.001: welded to the lowest index";
	if (RldRep_TestRun(test, name, &mesh, 0.001, 0u, &r))
	{
		RldRep_TestLine(test, (r.welded == 16u) && (r.openEdgesAfter == 0u) && (r.componentsOpen == 0u) && (r.positionMap[23] < 4u), name,
		                "welded %u, open edges %u, open pieces %u", r.welded, r.openEdgesAfter, r.componentsOpen);
		RldRep_Free(&r);
	}
	name = "the same, weld 0.00005: six open faces, each kept outward by the rays";
	if (RldRep_TestRun(test, name, &mesh, 0.00005, 0u, &r))
	{
		RldRep_TestLine(test,
		                (r.welded == 0u) && (r.openEdgesAfter == 24u) && (r.components == 6u) && (r.componentsOpen == 6u) && (r.holesLeft == 6u) &&
		                    (r.flipped == 0u) && (RldRep_TestOpenCount(&r) == 12u),
		                name, "welded %u, open edges %u, pieces %u/%u open, holes left %u, flipped %u", r.welded, r.openEdgesAfter, r.components,
		                r.componentsOpen, r.holesLeft, r.flipped);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// Turned triangles, and the whole cube inside out.
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 6u, 0.0);
	for (t = 0; t < 12u; t++)
	{
		if ((t == 1u) || (t == 4u) || (t == 5u) || (t == 9u) || (t == 10u))
		{
			const u32 swap = mesh.triangle[3u * t + 1u];

			mesh.triangle[3u * t + 1u] = mesh.triangle[3u * t + 2u];
			mesh.triangle[3u * t + 2u] = swap;
		}
	}
	name = "cube with five triangles turned: turned back, volume positive";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 0u, &r))
	{
		RldRep_TestLine(test, (r.flipped == 5u) && (RldRep_TestVolume(&mesh, &r, 0u, 12u) > 0.99) && RldRep_TestOutward(&mesh, &r, middle), name,
		                "flipped %u", r.flipped);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, RLDREP_TEST_INSIDE_OUT, 6u, 0.0);
	name = "cube inside out: all 12 turned";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 0u, &r))
	{
		RldRep_TestLine(test, (r.flipped == 12u) && RldRep_TestOutward(&mesh, &r, middle), name, "flipped %u", r.flipped);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// Cracks: the top face cut at m (the middle of the edge 4-5, or off it by
	// lift), the -y face not (its edge 5-4 runs past m). byPosition: every
	// corner gets the color of its position (as a PLY with vertex colors);
	// otherwise every corner its own color (a color seam everywhere).
	{
		static const double lifts[2] = {0.0, 0.0005};
		u32 variant;

		for (variant = 0; variant < 7u; variant++)
		{
			const double lift = (variant >= 5u) ? lifts[1] : lifts[0];
			const int byPosition = (variant == 0u) || (variant == 3u) || (variant == 6u);
			const double weld = (variant == 6u) ? 0.001 : 0.0;
			u32 m;
			u32 c;

			if (variant == 4u)
			{
				continue;
			}
			RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 5u, 0.0);
			m = RldRep_TestPosition(&mesh, 0.5, lift, 1.0);
			RldRep_TestTriangle(&mesh, 4u, m, 6u);
			RldRep_TestTriangle(&mesh, m, 5u, 7u);
			RldRep_TestTriangle(&mesh, m, 7u, 6u);
			if (variant == 3u)
			{
				// A fin on the chain edge 5-m: that edge has two triangles.
				RldRep_TestTriangle(&mesh, 5u, m, RldRep_TestPosition(&mesh, 0.5, -1.0, 1.5));
			}
			for (c = 0; byPosition && (c < (3u * mesh.triangleCount)); c++)
			{
				mesh.color[c] = 0x200000u + mesh.triangle[c];
			}
			if (variant == 2u)
			{
				// The ends of the split edge (corners 16 and 17) of one color.
				mesh.color[17] = mesh.color[16];
			}

			if (variant == 0u)
			{
				name = "crack (T-junction), colors by position: split, m carries the corner of the other side, closed without a fill";
				if (RldRep_TestRun(test, name, &mesh, weld, 8u, &r))
				{
					RldRep_TestLine(test,
					                (r.cracksSplit == 1u) && (r.triangleCount == 14u) && (r.holesClosed == 0u) && (r.openEdgesBefore == 3u) &&
					                    (r.openEdgesAfter == 0u) && (r.componentsOpen == 0u) && (r.flipped == 0u) && (r.triangle[15] == 5u) &&
					                    (r.triangle[16] == 8u) && (r.triangle[17] == 0u) && (r.triangle[18] == 8u) && (r.triangle[19] == 4u) &&
					                    (r.triangle[20] == 0u) && (r.sourceCorner[16] == 33u) && (r.sourceCorner[18] == 33u) &&
					                    (RldRep_TestVolume(&mesh, &r, 0u, 14u) > 0.99),
					                name, "split %u, %u triangles, closed %u, open edges %u -> %u, m carries %u %u", r.cracksSplit, r.triangleCount,
					                r.holesClosed, r.openEdgesBefore, r.openEdgesAfter, r.sourceCorner[16], r.sourceCorner[18]);
					RldRep_Free(&r);
				}
			}
			else if (variant == 1u)
			{
				name = "crack, a color seam at m and ends of different colors: not split, the slit stays open (no needle fill)";
				if (RldRep_TestRun(test, name, &mesh, weld, 8u, &r))
				{
					RldRep_TestLine(test, (r.cracksSplit == 0u) && (r.holesClosed == 0u) && (r.openEdgesAfter == 3u) && (r.triangleCount == 13u), name,
					                "split %u, closed %u, open edges %u", r.cracksSplit, r.holesClosed, r.openEdgesAfter);
					RldRep_Free(&r);
				}
			}
			else if (variant == 2u)
			{
				name = "crack, a color seam at m, ends of one color: split, m carries the nearer end";
				if (RldRep_TestRun(test, name, &mesh, weld, 8u, &r))
				{
					RldRep_TestLine(test,
					                (r.cracksSplit == 1u) && (r.openEdgesAfter == 0u) && (r.sourceCorner[16] == 16u) && (r.sourceCorner[18] == 16u),
					                name, "split %u, open edges %u, m carries %u %u", r.cracksSplit, r.openEdgesAfter, r.sourceCorner[16],
					                r.sourceCorner[18]);
					RldRep_Free(&r);
				}
			}
			else if (variant == 3u)
			{
				name = "crack whose chain edge has two triangles (a fin): not split";
				if (RldRep_TestRun(test, name, &mesh, weld, 8u, &r))
				{
					RldRep_TestLine(test, (r.cracksSplit == 0u) && (r.holesClosed == 0u), name, "split %u, closed %u", r.cracksSplit, r.holesClosed);
					RldRep_Free(&r);
				}
			}
			else if (variant == 5u)
			{
				name = "slit 0.0005 wide over 1, weld 0: no crack within reach, a needle fill refused, stays open";
				if (RldRep_TestRun(test, name, &mesh, weld, 8u, &r))
				{
					RldRep_TestLine(test, (r.cracksSplit == 0u) && (r.holesClosed == 0u) && (r.openEdgesAfter == 3u), name,
					                "split %u, closed %u, open edges %u", r.cracksSplit, r.holesClosed, r.openEdgesAfter);
					RldRep_Free(&r);
				}
			}
			else
			{
				name = "the same slit, weld 0.001: split as a crack, closed";
				if (RldRep_TestRun(test, name, &mesh, weld, 8u, &r))
				{
					RldRep_TestLine(test, (r.cracksSplit == 1u) && (r.holesClosed == 0u) && (r.openEdgesAfter == 0u), name,
					                "split %u, closed %u, open edges %u", r.cracksSplit, r.holesClosed, r.openEdgesAfter);
					RldRep_Free(&r);
				}
			}
			RldRep_TestFree(&mesh);
		}
	}

	// A fill that would pass through another piece: a triangle stuck through
	// the 4-edge hole of the top face.
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 5u, 0.0);
	RldRep_TestTriangle(&mesh, RldRep_TestPosition(&mesh, 0.5, 0.5, 0.5), RldRep_TestPosition(&mesh, 0.6, 0.5, 1.5),
	                    RldRep_TestPosition(&mesh, 0.4, 0.6, 1.5));
	name = "cube with a 4-edge hole and a triangle stuck through it: the fill would cross it, the hole stays open";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		RldRep_TestLine(test, (r.holesClosed == 0u) && (r.holeTriangles == 0u) && (r.openEdgesAfter == 7u), name, "closed %u, open edges %u",
		                r.holesClosed, r.openEdgesAfter);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// A 4-edge hole: the top (+z) face missing.
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 5u, 0.0);
	name = "cube with a 4-edge hole, holeMaxEdges 8: closed by 2 triangles (7 6 4, 4 5 7: first ear in loop order)";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		const u32 *f = &r.triangle[30];
		int sources = 1;

		for (t = 30; t < 36u; t++)
		{
			// Each fill corner carries a corner of a triangle on the loop.
			sources = sources && (mesh.triangle[r.sourceCorner[t]] == r.triangle[t]);
		}
		RldRep_TestLine(test,
		                (r.holesClosed == 1u) && (r.holeTriangles == 2u) && (r.triangleCount == 12u) && (r.openEdgesBefore == 4u) &&
		                    (r.openEdgesAfter == 0u) && (r.componentsOpen == 0u) && (r.holesLeft == 0u) && r.added[10] && r.added[11] &&
		                    (f[0] == 7u) && (f[1] == 6u) && (f[2] == 4u) && (f[3] == 4u) && (f[4] == 5u) && (f[5] == 7u) && sources &&
		                    RldRep_TestOutward(&mesh, &r, middle),
		                name, "closed %u, %u triangles, open edges %u -> %u, fill %u %u %u / %u %u %u", r.holesClosed, r.holeTriangles, r.openEdgesBefore,
		                r.openEdgesAfter, f[0], f[1], f[2], f[3], f[4], f[5]);
		RldRep_Free(&r);
	}
	name = "the same, holeMaxEdges 3: stays open, every triangle open";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 3u, &r))
	{
		RldRep_TestLine(test,
		                (r.holesClosed == 0u) && (r.holesLeft == 1u) && (r.openEdgesAfter == 4u) && (r.componentsOpen == 1u) &&
		                    (RldRep_TestOpenCount(&r) == 10u) && (r.flipped == 0u),
		                name, "closed %u, left %u, open edges %u, open triangles %u, flipped %u", r.holesClosed, r.holesLeft, r.openEdgesAfter,
		                RldRep_TestOpenCount(&r), r.flipped);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, RLDREP_TEST_INSIDE_OUT, 5u, 0.0);
	name = "cube with a hole, inside out, holeMaxEdges 0: the rays turn all 10";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 0u, &r))
	{
		RldRep_TestLine(test, (r.flipped == 10u) && RldRep_TestOutward(&mesh, &r, middle) && (r.holesLeft == 1u), name, "flipped %u, left %u", r.flipped,
		                r.holesLeft);
		RldRep_Free(&r);
	}
	name = "the same, holeMaxEdges 4: turned, then closed";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 4u, &r))
	{
		RldRep_TestLine(test, (r.flipped == 10u) && (r.holesClosed == 1u) && (r.componentsOpen == 0u) && RldRep_TestOutward(&mesh, &r, middle), name,
		                "flipped %u, closed %u, open pieces %u", r.flipped, r.holesClosed, r.componentsOpen);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);
}

static void RldRep_SelfTestShapes(struct RldRepTest *test)
{
	const double origin[3] = {0.0, 0.0, 0.0};
	const double middle[3] = {0.5, 0.5, 0.5};
	struct RldRepTestMesh mesh;
	RldRepResult r;
	const char *name;
	u32 wing;
	u32 t;
	int k;

	memset(&mesh, 0, sizeof(mesh));

	// A sphere with a 16-edge hole.
	RldRep_TestSphere(&mesh, 0.0, 0.0, 0.0, 10.0, 16u, 8u, 1, 1);
	name = "sphere inside out with a 16-edge hole, holeMaxEdges 8: open, turned outward by the rays";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		RldRep_TestLine(test,
		                (r.flipped == r.triangleCount) && (r.triangleCount == 208u) && (r.holesClosed == 0u) && (r.holesLeft == 1u) &&
		                    (r.componentsOpen == 1u) && (RldRep_TestOpenCount(&r) == 208u) && RldRep_TestOutward(&mesh, &r, origin),
		                name, "%u triangles, flipped %u, closed %u, left %u", r.triangleCount, r.flipped, r.holesClosed, r.holesLeft);
		RldRep_Free(&r);
	}
	name = "the same, holeMaxEdges 16: closed by 14 triangles, outward";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 16u, &r))
	{
		RldRep_TestLine(test,
		                (r.holesClosed == 1u) && (r.holeTriangles == 14u) && (r.componentsOpen == 0u) && (r.openEdgesAfter == 0u) &&
		                    (RldRep_TestVolume(&mesh, &r, 0u, r.triangleCount) > 0.0) && RldRep_TestOutward(&mesh, &r, origin),
		                name, "closed %u, %u triangles, open pieces %u", r.holesClosed, r.holeTriangles, r.componentsOpen);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// A wing beside a cube: two quads at y = 0.5 from x = 2 to 4, up or down.
	for (k = 0; k < 2; k++)
	{
		u32 w[6];
		u32 i;

		RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 6u, 0.0);
		wing = mesh.triangleCount;
		for (i = 0; i < 6u; i++)
		{
			w[i] = RldRep_TestPosition(&mesh, 2.0 + (double)(i % 3u), 0.5, (double)(i / 3u));
		}
		for (i = 0; i < 2u; i++)
		{
			if (k == 0)
			{
				RldRep_TestTriangle(&mesh, w[i], w[i + 3u], w[i + 4u]);
				RldRep_TestTriangle(&mesh, w[i], w[i + 4u], w[i + 1u]);
			}
			else
			{
				RldRep_TestTriangle(&mesh, w[i], w[i + 4u], w[i + 3u]);
				RldRep_TestTriangle(&mesh, w[i], w[i + 1u], w[i + 4u]);
			}
		}
		name = (k == 0) ? "wing beside a cube, facing up: kept, outline (6 edges) not filled, wing open"
		                : "wing beside a cube, facing down: kept as exported too";
		if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
		{
			int same = 1;

			for (t = 3u * wing; t < (3u * mesh.triangleCount); t++)
			{
				same = same && (r.triangle[t] == mesh.triangle[t]);
			}
			RldRep_TestLine(test,
			                same && (r.flipped == 0u) && (r.holesClosed == 0u) && (r.holesLeft == 1u) && (r.components == 2u) &&
			                    (r.componentsOpen == 1u) && (RldRep_TestOpenCount(&r) == 4u) && !r.open[0] && r.open[wing],
			                name, "flipped %u, closed %u, left %u, pieces %u/%u open", r.flipped, r.holesClosed, r.holesLeft, r.components, r.componentsOpen);
			RldRep_Free(&r);
		}
		RldRep_TestFree(&mesh);
	}

	// A fin on the edge 4-5 of a cube: three triangles on that edge.
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 6u, 0.0);
	RldRep_TestTriangle(&mesh, 4u, 5u, RldRep_TestPosition(&mesh, 0.5, -1.0, 2.0));
	name = "fin on a cube edge: one non-manifold edge, the cube closed, the fin open";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		RldRep_TestLine(test,
		                (r.nonManifoldEdges == 1u) && (r.components == 2u) && (r.componentsOpen == 1u) && (RldRep_TestOpenCount(&r) == 1u) &&
		                    r.open[12] && (r.flipped == 0u) && (RldRep_TestVolume(&mesh, &r, 0u, 12u) > 0.99),
		                name, "non-manifold %u, pieces %u/%u open, flipped %u", r.nonManifoldEdges, r.components, r.componentsOpen, r.flipped);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// Degenerate triangles: a repeated corner, three corners on a line, and
	// two corners that the weld makes one.
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 6u, 0.0);
	RldRep_TestTriangle(&mesh, 0u, 0u, 7u);
	RldRep_TestTriangle(&mesh, 0u, RldRep_TestPosition(&mesh, 0.5, 0.0, 0.0), 1u);
	{
		const u32 a = RldRep_TestPosition(&mesh, 5.0, 5.0, 5.0);
		const u32 b = RldRep_TestPosition(&mesh, 5.0004, 5.0, 5.0);
		const u32 c = RldRep_TestPosition(&mesh, 6.0, 5.0, 5.0);

		RldRep_TestTriangle(&mesh, a, b, c);
	}
	name = "degenerate triangles: repeated corner, on a line, welded together - dropped";
	if (RldRep_TestRun(test, name, &mesh, 0.001, 0u, &r))
	{
		RldRep_TestLine(test, (r.degenerate == 3u) && (r.triangleCount == 12u) && (r.welded == 1u) && (r.componentsOpen == 0u), name,
		                "degenerate %u, %u triangles, welded %u", r.degenerate, r.triangleCount, r.welded);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// Exact doubles and twins.
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 6u, 0.0);
	RldRep_TestTriangle(&mesh, mesh.triangle[1], mesh.triangle[2], mesh.triangle[0]);
	RldRep_TestTriangle(&mesh, mesh.triangle[9], mesh.triangle[11], mesh.triangle[10]);
	{
		const u32 a = RldRep_TestPosition(&mesh, 5.0, 0.0, 0.0);
		const u32 b = RldRep_TestPosition(&mesh, 6.0, 0.0, 0.0);
		const u32 c = RldRep_TestPosition(&mesh, 5.0, 1.0, 0.0);

		RldRep_TestTriangle(&mesh, a, b, c);
		RldRep_TestTriangle(&mesh, b, a, c);
	}
	name = "exact double dropped, twins kept (on a cube face and alone), never turned, not open";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		RldRep_TestLine(test,
		                (r.duplicate == 1u) && (r.twins == 2u) && (r.triangleCount == 15u) && (r.flipped == 0u) && (r.componentsOpen == 0u) &&
		                    (RldRep_TestOpenCount(&r) == 0u) && (r.nonManifoldEdges == 0u) && (r.holesClosed == 0u) &&
		                    (r.sourceCorner[3u * 12u] == 3u * 13u) && (r.sourceCorner[3u * 13u] == 3u * 14u) && (r.sourceCorner[3u * 14u] == 3u * 15u),
		                name, "duplicate %u, twins %u, %u triangles, flipped %u, open pieces %u, non-manifold %u", r.duplicate, r.twins, r.triangleCount,
		                r.flipped, r.componentsOpen, r.nonManifoldEdges);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// A bowtie: two triangles on one position, their outlines meet there.
	{
		const u32 a = RldRep_TestPosition(&mesh, 0.0, 0.0, 0.0);
		const u32 b = RldRep_TestPosition(&mesh, 1.0, 1.0, 0.0);
		const u32 c = RldRep_TestPosition(&mesh, 1.0, -1.0, 0.0);
		const u32 d = RldRep_TestPosition(&mesh, -1.0, 1.0, 0.0);
		const u32 e = RldRep_TestPosition(&mesh, -1.0, -1.0, 0.0);

		RldRep_TestTriangle(&mesh, a, c, b);
		RldRep_TestTriangle(&mesh, a, d, e);
	}
	name = "bowtie: the loops share a position, not filled, one boundary piece";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		RldRep_TestLine(test, (r.holesClosed == 0u) && (r.holesLeft == 1u) && (r.openEdgesAfter == 6u) && (r.componentsOpen == 2u), name,
		                "closed %u, left %u, open edges %u, open pieces %u", r.holesClosed, r.holesLeft, r.openEdgesAfter, r.componentsOpen);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// An octahedron without two top triangles that touch only at +z: one
	// loop of 6 edges through +z twice, cut into two loops of 3.
	RldRep_TestPosition(&mesh, 1.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, -1.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 0.0, 1.0, 0.0);
	RldRep_TestPosition(&mesh, 0.0, -1.0, 0.0);
	RldRep_TestPosition(&mesh, 0.0, 0.0, 1.0);
	RldRep_TestPosition(&mesh, 0.0, 0.0, -1.0);
	RldRep_TestTriangle(&mesh, 2u, 1u, 4u);
	RldRep_TestTriangle(&mesh, 3u, 0u, 4u);
	RldRep_TestTriangle(&mesh, 2u, 0u, 5u);
	RldRep_TestTriangle(&mesh, 1u, 2u, 5u);
	RldRep_TestTriangle(&mesh, 3u, 1u, 5u);
	RldRep_TestTriangle(&mesh, 0u, 3u, 5u);
	name = "octahedron, two holes touching at one position: the loop is cut there, both closed";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 8u, &r))
	{
		RldRep_TestLine(test,
		                (r.holesClosed == 2u) && (r.holeTriangles == 2u) && (r.triangleCount == 8u) && (r.openEdgesAfter == 0u) &&
		                    (r.componentsOpen == 0u) && RldRep_TestOutward(&mesh, &r, origin),
		                name, "closed %u, %u triangles, open edges %u, open pieces %u", r.holesClosed, r.holeTriangles, r.openEdgesAfter, r.componentsOpen);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	(void)middle;
}

static void RldRep_SelfTestTies(struct RldRepTest *test)
{
	struct RldRepTestMesh mesh;
	RldRepResult r;
	const char *name;
	u32 t;

	memset(&mesh, 0, sizeof(mesh));

	// Weld: a position exactly between two representatives goes to the lower
	// one; a row 0.9 apart with a weld distance of 1 is no chain.
	RldRep_TestPosition(&mesh, 0.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 2.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 1.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 10.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 10.9, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 11.8, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 0.0, 20.0, 0.0);
	RldRep_TestPosition(&mesh, 0.0, 0.0, 20.0);
	for (t = 0; t < 6u; t++)
	{
		RldRep_TestTriangle(&mesh, t, 6u, 7u);
	}
	name = "weld ties: between two representatives -> the lower; no chains (0, 0.9, 1.8 -> 0, 0, 1.8)";
	if (RldRep_TestRun(test, name, &mesh, 1.0, 0u, &r))
	{
		RldRep_TestLine(test,
		                (r.positionMap[0] == 0u) && (r.positionMap[1] == 1u) && (r.positionMap[2] == 0u) && (r.positionMap[3] == 3u) &&
		                    (r.positionMap[4] == 3u) && (r.positionMap[5] == 5u) && (r.welded == 2u) && (r.duplicate == 2u),
		                name, "map %u %u %u %u %u %u, welded %u, duplicate %u", r.positionMap[0], r.positionMap[1], r.positionMap[2], r.positionMap[3],
		                r.positionMap[4], r.positionMap[5], r.welded, r.duplicate);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);

	// An open piece split half and half, seen from both sides, with its middle
	// in its own plane: no rule decides, the lowest triangle keeps its winding.
	RldRep_TestPosition(&mesh, 0.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 1.0, 0.0, 0.0);
	RldRep_TestPosition(&mesh, 1.0, 1.0, 0.0);
	RldRep_TestPosition(&mesh, 0.0, 1.0, 0.0);
	RldRep_TestTriangle(&mesh, 0u, 1u, 2u);
	RldRep_TestTriangle(&mesh, 0u, 3u, 2u);
	name = "open square, half turned, no rule decides: triangle 0 keeps its winding";
	if (RldRep_TestRun(test, name, &mesh, 0.0, 0u, &r))
	{
		RldRep_TestLine(test,
		                (r.flipped == 1u) && (r.triangle[0] == 0u) && (r.triangle[1] == 1u) && (r.triangle[2] == 2u) && (r.triangle[3] == 0u) &&
		                    (r.triangle[4] == 2u) && (r.triangle[5] == 3u) && (r.sourceCorner[4] == 5u) && (r.sourceCorner[5] == 4u),
		                name, "flipped %u, triangles %u %u %u / %u %u %u", r.flipped, r.triangle[0], r.triangle[1], r.triangle[2], r.triangle[3],
		                r.triangle[4], r.triangle[5]);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&mesh);
}

// Larger: a sphere of 32 x 16 with every corner split per triangle (0.0001
// apart at most), every seventh triangle turned and a 32-edge hole.
static void RldRep_SelfTestLarge(struct RldRepTest *test)
{
	const double origin[3] = {0.0, 0.0, 0.0};
	struct RldRepTestMesh sphere;
	struct RldRepTestMesh mesh;
	RldRepResult r;
	const char *name = "sphere 32 x 16, corners split per triangle, every 7th turned, 32-edge hole: welded, outward, open";
	u32 t;

	memset(&sphere, 0, sizeof(sphere));
	memset(&mesh, 0, sizeof(mesh));
	RldRep_TestSphere(&sphere, 0.0, 0.0, 0.0, 30.0, 32u, 16u, 1, 0);
	for (t = 0; (t < sphere.triangleCount) && !sphere.failed; t++)
	{
		u32 c[3];
		u32 k;

		for (k = 0; k < 3u; k++)
		{
			const double *p = &sphere.position[3u * sphere.triangle[3u * t + k]];
			const double shift = 0.00001 * (double)((t * 3u + k) % 11u);

			c[k] = RldRep_TestPosition(&mesh, p[0] + shift, p[1], p[2] - shift);
		}
		if ((t % 7u) == 3u)
		{
			RldRep_TestTriangle(&mesh, c[0], c[2], c[1]);
		}
		else
		{
			RldRep_TestTriangle(&mesh, c[0], c[1], c[2]);
		}
	}
	mesh.failed |= sphere.failed;
	if (RldRep_TestRun(test, name, &mesh, 0.001, 8u, &r))
	{
		RldRep_TestLine(test,
		                (r.welded == (mesh.positionCount - (sphere.positionCount - 1u))) && (r.triangleCount == sphere.triangleCount) &&
		                    (r.flipped == ((sphere.triangleCount + 3u) / 7u)) && (r.holesLeft == 1u) && (r.openEdgesAfter == 32u) &&
		                    (r.componentsOpen == 1u) && RldRep_TestOutward(&mesh, &r, origin),
		                name, "welded %u, %u triangles, flipped %u, left %u, open edges %u", r.welded, r.triangleCount, r.flipped, r.holesLeft,
		                r.openEdgesAfter);
		RldRep_Free(&r);
	}
	RldRep_TestFree(&sphere);
	RldRep_TestFree(&mesh);
}

static int RldRep_TestRefused(const struct RldRepTestMesh *mesh, double weld, u32 holeMax)
{
	RldRepInput in = RldRep_TestInput(mesh, weld, holeMax);
	RldRepResult r;
	char why[160];
	int refused;

	why[0] = '\0';
	refused = !RldRep_Repair(&in, &r, why, sizeof(why)) && (why[0] != '\0') && (r.triangle == NULL) && (r.triangleCount == 0u);
	RldRep_Free(&r);
	return refused;
}

static void RldRep_SelfTestLimits(struct RldRepTest *test)
{
	struct RldRepTestMesh mesh;
	double zero = 0.0;
	int ok;

	memset(&mesh, 0, sizeof(mesh));
	RldRep_TestCube(&mesh, 0.0, 0.0, 0.0, 1.0, 0, 6u, 0.0);
	if (mesh.failed)
	{
		RldRep_TestLine(test, 0, "invalid input", "out of memory");
		RldRep_TestFree(&mesh);
		return;
	}

	ok = RldRep_TestRefused(&mesh, -1.0, 0u) && RldRep_TestRefused(&mesh, 0.0, RLDREP_HOLE_LIMIT + 1u);
	mesh.triangle[5] = 8u;
	ok = ok && RldRep_TestRefused(&mesh, 0.0, 0u);
	mesh.triangle[5] = 3u;
	mesh.position[4] = zero / zero;
	ok = ok && RldRep_TestRefused(&mesh, 0.0, 0u);
	mesh.position[4] = 0.0;
	ok = ok && !RldRep_TestRefused(&mesh, 0.0, 0u);
	RldRep_TestLine(test, ok, "invalid input refused: weld below 0, holeMaxEdges above 256, position out of range, NaN", "accepted");
	RldRep_TestFree(&mesh);
}

// The number of failed cases; 0 = passed. Each case reports one line (no
// line break at the end) through report, which may be NULL.
int RldRep_SelfTest(void (*report)(const char *line))
{
	struct RldRepTest test;

	test.report = report;
	test.failures = 0;
	RldRep_SelfTestCubes(&test);
	RldRep_SelfTestShapes(&test);
	RldRep_SelfTestTies(&test);
	RldRep_SelfTestLarge(&test);
	RldRep_SelfTestLimits(&test);
	return test.failures;
}
