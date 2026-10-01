// rldpack_remesh.inc - a closed hull per group for make-char (fallback, on request)
//
// Part of the packer, never of the game. Self-contained: it needs only the C
// standard library (stdlib, string, stdio, stdarg) and the types u8, u32 and
// u64 of tools/rldpack.c, so it can be included anywhere after those.
//
// WHAT IT DOES. A model whose surface is open, doubled or tangled (holes,
// loose sheets, parts stuck into each other) is replaced, group by group, by
// a new surface that is closed, two-manifold and wound outward - the shape
// the game's one-sided triangles need. The new surface has many triangles;
// the reducer (tools/rldpack_reduce.inc) brings it down afterwards. Per group:
//   1. A VOXEL GRID: `resolution` cells along the longest side of the group's
//      box, RLDREM_MARGIN empty cells around it. Every cell that a triangle of
//      the group touches is a surface cell (the separating axis test of
//      Akenine-Moeller, the box widened by RLDREM_BOX_SLACK of a cell so that
//      a triangle on a cell face marks both cells).
//   2. OUTSIDE AND INSIDE: a flood fill from the corner cell through the cells
//      that are no surface cells (six neighbors) finds the outside; every
//      other cell is inside. So holes smaller than a cell and hollow spaces
//      close, and an open part (a sheet) becomes a body at least one cell
//      thick: its surface cells are inside. The fill also stops at cells that
//      a ray vote calls inside (RldRem_VoteInside: odd crossings along at
//      least four of the six axis rays), so that an open body with a hole
//      wider than a cell is filled instead of becoming a thin double shell.
//      This also fills what is open on purpose: a one-sided bowl or cup is
//      closed over its rim and becomes solid (its inside counts as inside for
//      four of the six rays). A model that must keep such an opening is not
//      one for this fallback.
//   3. THE NEW SURFACE: marching tetrahedra on the cell centers. Each cube of
//      eight neighboring centers is split into the six tetrahedra around its
//      main diagonal (all cubes the same way, so the tetrahedra meet face to
//      face), and every edge from an inside to an outside center gets one
//      position at its middle. The zero set of a piecewise linear function on
//      such a mesh is a closed two-manifold without boundary, and every
//      triangle is wound so that its normal points to the outside center
//      (decided in exact integer arithmetic). Equal edges give the same
//      position: positions are found by a key per lattice edge, sorted.
//   4. BACK ONTO THE MODEL: each position moves to the closest point of the
//      group's own triangles, RLDREM_SURFACE_GAP of a cell short of it on the
//      side it came from (so the two faces of a thin part never meet), at
//      most RLDREM_PULL_LIMIT cells. Before that,
//      RLDREM_SMOOTH_STEPS rounds of averaging with the neighbors take off the
//      stairs of the grid (see the constant for why). A move that would turn
//      a triangle by 90 degrees or more against its grid triangle, or make it
//      a needle (RLDREM_GUARD_QUALITY), is halved for the positions of that
//      triangle, up to RLDREM_GUARD_LEVELS times, then dropped (the position
//      stays where the grid put it, where every triangle is good).
//   5. COLORS: every new position takes the color of the closest point of
//      the group's triangles: the color of that triangle's corner nearest to
//      the point. Equally close triangles (front and back of a two-sided
//      sheet) are told apart by the side the position came from (the grid
//      position before step 4), then by the lower triangle index.
//   6. A CHECK of the result per group: every edge has exactly two triangles,
//      running opposite ways; no two triangles have the same positions; the
//      enclosed volume is positive. A group that fails it fails the call.
//
// AFTER THE REDUCTION (optional): RldRem_Recolor takes the corner colors of a
// reduced hull again from the source surface; the colors the reducer carries
// over are those of one of the hundreds of hull triangles each kept triangle
// stands for.
//
// DETERMINISM. The same input gives the same output bit for bit: no time, no
// random numbers, no hash order. Every loop runs in index order, every sort
// has unique keys, every tie is broken by index. The square root is written
// out instead of taken from the C library (as in tools/rldpack_reduce.inc).
// Bit-exact results across builds need IEEE double arithmetic without excess
// precision and without contracted multiply-adds (MSVC, SSE2, /fp:precise).

//========================================================================================
// INTERFACE
//========================================================================================

#define RLDREM_GROUPS_MAX 8u

typedef struct RldRemInput
{
	u32 positionCount;
	const double *position;     // 3 per position, game units
	u32 triangleCount;
	const u32 *triangle;        // 3 position indices per triangle
	const u32 *cornerColor;     // 3 per triangle, 0xRRGGBB
	const u8 *triangleGroup;    // group per triangle; each group is remeshed on its own
	u32 groupCount;             // <= 8
	u32 resolution;             // cells along the longest side of each group (e.g. 64)
} RldRemInput;

typedef struct RldRemResult
{
	u32 positionCount;
	double *position;           // new positions, 3 each
	u32 triangleCount;
	u32 *triangle;              // closed, outward, manifold; 3 position indices each
	u32 *cornerColor;           // 3 per triangle: the color of the closest point of the source surface
	u8 *triangleGroup;          // per triangle
} RldRemResult;

//========================================================================================
// CONSTANTS
//========================================================================================

#define RLDREM_NONE 0xFFFFFFFFu

// Cells along the longest side of a group: below 8 nothing of a driver is
// left; above 128 the hull of a driver gets close to RLDREM_INPUT_MAX
// triangles and the reduction takes long, for no shape the 797 triangles of
// a driver could still show.
#define RLDREM_RESOLUTION_MIN 8u
#define RLDREM_RESOLUTION_MAX 128u

// Empty cells around the box of a group: the flood fill starts in a corner
// cell that no triangle can touch, and the outside surrounds the model.
#define RLDREM_MARGIN 2u

// A guard against overflow of the corner arithmetic, far above any model;
// the same bound as the reducer's input, which the result goes to next.
#define RLDREM_INPUT_MAX 0x00100000u

// Positions must be finite and within this many units on every axis.
#define RLDREM_COORD_MAX 1000000.0

// The box of a cell is widened by this share of a cell for the overlap test,
// so that rounding never lets a triangle on a cell face slip between two
// cells (the flood fill would leak through).
#define RLDREM_BOX_SLACK 1.0e-6

// Rounds of neighbor averaging (weight RLDREM_SMOOTH_FACTOR) before the
// positions move onto the model. The grid surface is a staircase up to about
// a cell away from the model; averaging turns the stairs into a slope first,
// so that the pull below starts closer to the model and is cut at
// RLDREM_PULL_LIMIT less often (on four test models about a third as often
// as without averaging, and the mean distance between the reduced hull and
// the source was mostly 8 to 14 % lower). Averaging also shrinks the hull a
// little; the pull takes that back. More rounds would start to melt thin parts.
#define RLDREM_SMOOTH_STEPS 4u
#define RLDREM_SMOOTH_FACTOR 0.5

// A pulled position stays this many cells off the model, on the side its grid
// position lies (the grid positions are never on the model: they lie on cell
// faces next to an outside cell, which no triangle touches). Both sides of a
// part thinner than a cell are pulled onto the same triangles; without the gap
// they would land on the same points and their triangles would cut through
// each other. 0.05 cells is about 0.1 game units at resolution 64.
#define RLDREM_SURFACE_GAP 0.05

// The work of one group (cells tested against triangles, triangles measured
// for a closest point, columns tested for a ray) may not exceed this many
// steps. Models of a few thousand triangles need well below a tenth of it
// at resolution 128; thousands of long splinters through the same cells
// would need minutes - the group is refused instead (a few seconds).
#define RLDREM_WORK_MAX 400000000u

// The exact search for the closest point looks this many rings of cells
// around a point. Farther points (hull that closes a hole, the top of a filled
// bowl) take the closest of the triangles around the surface cell nearest to
// their cell instead (RldRem_Nearest): the exact search would cost the cube of
// the distance per point.
#define RLDREM_NEAR_RINGS 3u

// A position moves at most this many cells onto the model. The grid surface
// lies between the centers of surface cells and outside cells, so the model
// is within about one cell of it; a longer pull would cross to a surface on
// the other side of a thin part.
#define RLDREM_PULL_LIMIT 1.0

// The guard of step 4: a triangle with a shape below q = 0.1 (see
// RldRem_Quality2; a needle with a tip of about 3 degrees) or turned by 90
// degrees or more against its grid triangle has the move of its positions
// halved, at most RLDREM_GUARD_LEVELS times; then they stay on the grid.
// Grid triangles have q >= 0.8, so the guard always ends.
#define RLDREM_GUARD_QUALITY 0.1
#define RLDREM_GUARD_LEVELS 4u

// Cell flags.
#define RLDREM_SURFACE 1u
#define RLDREM_OUTSIDE 2u
#define RLDREM_VOTED 4u    // the rays call it inside (RldRem_VoteInside)

// A cell whose center sees the model an odd number of times along at least
// this many of the six axis rays counts as inside (see RldRem_VoteInside).
#define RLDREM_VOTES_INSIDE 4u

//========================================================================================
// ARITHMETIC THAT ROUNDS THE SAME EVERYWHERE
//========================================================================================

// Square root without the C library, as RldRed_Sqrt: scaled by powers of four
// into [0.25, 4] (exact), a fixed number of Newton steps, scaled back.
static double RldRem_Sqrt(double value)
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

static double RldRem_Dot(const double *a, const double *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void RldRem_Sub(const double *a, const double *b, double out[3])
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

// (b - a) x (c - a): the normal of the triangle a, b, c, twice its area long.
static void RldRem_Normal(const double *a, const double *b, const double *c, double out[3])
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

static double RldRem_Abs(double x)
{
	return (x < 0.0) ? -x : x;
}

// q squared of the triangle a, b, c: q = 4 sqrt(3) area / (sum of its squared
// edges), 1 equilateral, 0 a line; q^2 = 12 |normal|^2 / (sum of squared edges)^2.
static double RldRem_Quality2(const double *a, const double *b, const double *c, const double *normal)
{
	double ab[3];
	double bc[3];
	double ca[3];
	double edges;

	RldRem_Sub(b, a, ab);
	RldRem_Sub(c, b, bc);
	RldRem_Sub(a, c, ca);
	edges = RldRem_Dot(ab, ab) + RldRem_Dot(bc, bc) + RldRem_Dot(ca, ca);
	return (edges > 0.0) ? (12.0 * RldRem_Dot(normal, normal) / (edges * edges)) : 0.0;
}

// The closest point of the segment a-b to p.
static void RldRem_ClosestOnSegment(const double *p, const double *a, const double *b, double out[3])
{
	double ab[3];
	double ap[3];
	double len2;
	double t;

	RldRem_Sub(b, a, ab);
	RldRem_Sub(p, a, ap);
	len2 = RldRem_Dot(ab, ab);
	t = (len2 > 0.0) ? (RldRem_Dot(ap, ab) / len2) : 0.0;
	if (!(t > 0.0))
	{
		t = 0.0;
	}
	if (t > 1.0)
	{
		t = 1.0;
	}
	out[0] = a[0] + t * ab[0];
	out[1] = a[1] + t * ab[1];
	out[2] = a[2] + t * ab[2];
}

// The closest point of the triangle a, b, c to p (Ericson, Real-Time Collision
// Detection 5.1.5, by Voronoi regions). A triangle without area is taken as
// its three edges.
static void RldRem_ClosestOnTriangle(const double *p, const double *a, const double *b, const double *c, double out[3])
{
	double ab[3];
	double ac[3];
	double ap[3];
	double bp[3];
	double cp[3];
	double n[3];
	double d1;
	double d2;
	double d3;
	double d4;
	double d5;
	double d6;
	double va;
	double vb;
	double vc;
	double v;
	double w;
	double denom;

	RldRem_Normal(a, b, c, n);
	if (!(RldRem_Dot(n, n) > 0.0))
	{
		double q[3];
		double best;
		double d[3];
		double e;

		RldRem_ClosestOnSegment(p, a, b, out);
		RldRem_Sub(p, out, d);
		best = RldRem_Dot(d, d);
		RldRem_ClosestOnSegment(p, b, c, q);
		RldRem_Sub(p, q, d);
		e = RldRem_Dot(d, d);
		if (e < best)
		{
			best = e;
			memcpy(out, q, sizeof(q));
		}
		RldRem_ClosestOnSegment(p, c, a, q);
		RldRem_Sub(p, q, d);
		e = RldRem_Dot(d, d);
		if (e < best)
		{
			memcpy(out, q, sizeof(q));
		}
		return;
	}

	RldRem_Sub(b, a, ab);
	RldRem_Sub(c, a, ac);
	RldRem_Sub(p, a, ap);
	d1 = RldRem_Dot(ab, ap);
	d2 = RldRem_Dot(ac, ap);
	if ((d1 <= 0.0) && (d2 <= 0.0))
	{
		memcpy(out, a, 3u * sizeof(double));
		return;
	}

	RldRem_Sub(p, b, bp);
	d3 = RldRem_Dot(ab, bp);
	d4 = RldRem_Dot(ac, bp);
	if ((d3 >= 0.0) && (d4 <= d3))
	{
		memcpy(out, b, 3u * sizeof(double));
		return;
	}

	vc = d1 * d4 - d3 * d2;
	if ((vc <= 0.0) && (d1 >= 0.0) && (d3 <= 0.0))
	{
		v = d1 / (d1 - d3);
		out[0] = a[0] + v * ab[0];
		out[1] = a[1] + v * ab[1];
		out[2] = a[2] + v * ab[2];
		return;
	}

	RldRem_Sub(p, c, cp);
	d5 = RldRem_Dot(ab, cp);
	d6 = RldRem_Dot(ac, cp);
	if ((d6 >= 0.0) && (d5 <= d6))
	{
		memcpy(out, c, 3u * sizeof(double));
		return;
	}

	vb = d5 * d2 - d1 * d6;
	if ((vb <= 0.0) && (d2 >= 0.0) && (d6 <= 0.0))
	{
		w = d2 / (d2 - d6);
		out[0] = a[0] + w * ac[0];
		out[1] = a[1] + w * ac[1];
		out[2] = a[2] + w * ac[2];
		return;
	}

	va = d3 * d6 - d5 * d4;
	if ((va <= 0.0) && ((d4 - d3) >= 0.0) && ((d5 - d6) >= 0.0))
	{
		w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		out[0] = b[0] + w * (c[0] - b[0]);
		out[1] = b[1] + w * (c[1] - b[1]);
		out[2] = b[2] + w * (c[2] - b[2]);
		return;
	}

	denom = 1.0 / (va + vb + vc);
	v = vb * denom;
	w = vc * denom;
	out[0] = a[0] + ab[0] * v + ac[0] * w;
	out[1] = a[1] + ab[1] * v + ac[1] * w;
	out[2] = a[2] + ab[2] * v + ac[2] * w;
}

// 1 when the triangle a, b, c touches the axis-aligned box of the given center
// and half size (closed box). The separating axis test of Akenine-Moeller:
// the three box axes, the triangle's normal and the nine crosses of box axes
// and triangle edges.
static int RldRem_TriangleBox(const double *center, double half, const double *a, const double *b, const double *c)
{
	double v[3][3];
	double e[3][3];
	double n[3];
	double r;
	double d;
	u32 i;
	u32 k;

	RldRem_Sub(a, center, v[0]);
	RldRem_Sub(b, center, v[1]);
	RldRem_Sub(c, center, v[2]);

	// The box axes.
	for (k = 0; k < 3u; k++)
	{
		double lo = v[0][k];
		double hi = v[0][k];

		for (i = 1; i < 3u; i++)
		{
			lo = (v[i][k] < lo) ? v[i][k] : lo;
			hi = (v[i][k] > hi) ? v[i][k] : hi;
		}
		if ((lo > half) || (hi < -half))
		{
			return 0;
		}
	}

	RldRem_Sub(v[1], v[0], e[0]);
	RldRem_Sub(v[2], v[1], e[1]);
	RldRem_Sub(v[0], v[2], e[2]);

	// The nine crosses: axis k x edge i.
	for (i = 0; i < 3u; i++)
	{
		for (k = 0; k < 3u; k++)
		{
			double axis[3];
			double p0;
			double p1;
			double p2;
			double lo;
			double hi;

			axis[k] = 0.0;
			axis[(k + 1u) % 3u] = -e[i][(k + 2u) % 3u];
			axis[(k + 2u) % 3u] = e[i][(k + 1u) % 3u];
			p0 = RldRem_Dot(axis, v[0]);
			p1 = RldRem_Dot(axis, v[1]);
			p2 = RldRem_Dot(axis, v[2]);
			lo = (p0 < p1) ? p0 : p1;
			lo = (p2 < lo) ? p2 : lo;
			hi = (p0 > p1) ? p0 : p1;
			hi = (p2 > hi) ? p2 : hi;
			r = half * (RldRem_Abs(axis[0]) + RldRem_Abs(axis[1]) + RldRem_Abs(axis[2]));
			if ((lo > r) || (hi < -r))
			{
				return 0;
			}
		}
	}

	// The plane of the triangle.
	n[0] = e[0][1] * e[1][2] - e[0][2] * e[1][1];
	n[1] = e[0][2] * e[1][0] - e[0][0] * e[1][2];
	n[2] = e[0][0] * e[1][1] - e[0][1] * e[1][0];
	d = RldRem_Dot(n, v[0]);
	r = half * (RldRem_Abs(n[0]) + RldRem_Abs(n[1]) + RldRem_Abs(n[2]));
	return RldRem_Abs(d) <= r;
}

//========================================================================================
// THE GRID OF ONE GROUP
//========================================================================================

// A grid of cubic cells over a set of triangles: which triangles touch each
// cell (for the closest point) and the flags of the cells.
struct RldRemGrid
{
	double origin[3];       // the low corner of cell 0
	double cell;            // edge length of a cell
	u32 n[3];               // cells per axis
	u32 cellCount;
	u32 *start;             // per cell + 1: where its triangles start in list
	u32 *list;              // triangle numbers (into tri), per cell ascending
	u8 *flag;               // RLDREM_SURFACE, RLDREM_OUTSIDE

	const double *position;
	u32 triCount;
	const u32 *tri;         // 3 position indices per triangle of this grid
	const u32 *triIndex;    // per triangle of this grid: its number in the caller's list (tie-break)
	double *normal;         // 3 per triangle
	u32 *stamp;             // per triangle: the query that measured it last
	u32 query;
	u64 work;               // steps so far (RLDREM_WORK_MAX)
	int overrun;            // the work ran out: results are incomplete
	u32 *seed;              // per cell: a surface cell nearest to it (steps through 26 neighbors)
	u32 ringLimit;          // rings of the exact search (RLDREM_NEAR_RINGS)
};

// The work budget of a group; the self-test lowers it to try the refusal.
static u64 s_rldRemWorkMax = RLDREM_WORK_MAX;

// Counts steps of work; 0 once the budget of the group is used up.
static int RldRem_Work(struct RldRemGrid *g, u32 steps)
{
	g->work += steps;
	if (g->work > s_rldRemWorkMax)
	{
		g->overrun = 1;
	}
	return !g->overrun;
}

static void RldRem_FreeGrid(struct RldRemGrid *g)
{
	free(g->start);
	free(g->list);
	free(g->flag);
	free(g->normal);
	free(g->stamp);
	free(g->seed);
	memset(g, 0, sizeof(*g));
}

// The cell of a coordinate on one axis, clamped to the grid.
static u32 RldRem_CellOf(const struct RldRemGrid *g, u32 axis, double x)
{
	const double f = (x - g->origin[axis]) / g->cell;

	if (!(f > 0.0))
	{
		return 0u;
	}
	if (f >= (double)g->n[axis])
	{
		return g->n[axis] - 1u;
	}
	// f is positive here: the cast truncates, which is floor.
	return (u32)f;
}

// Clips the convex polygon in[count] (3 doubles per corner) to lo <= x[axis]
// <= hi; the result (at most count + 2 corners) goes to out. Returns its
// corner count (0 = nothing left).
static u32 RldRem_ClipSlab(const double *in, u32 count, u32 axis, double lo, double hi, double *out)
{
	double mid[3u * 9u];
	u32 midCount = 0;
	u32 outCount = 0;
	u32 i;
	u32 k;

	// Against lo, then against hi.
	for (i = 0; i < count; i++)
	{
		const double *a = &in[3u * i];
		const double *b = &in[3u * ((i + 1u) % count)];

		if (a[axis] >= lo)
		{
			memcpy(&mid[3u * midCount++], a, 3u * sizeof(double));
		}
		if ((a[axis] < lo) != (b[axis] < lo))
		{
			const double t = (lo - a[axis]) / (b[axis] - a[axis]);

			for (k = 0; k < 3u; k++)
			{
				mid[3u * midCount + k] = a[k] + t * (b[k] - a[k]);
			}
			mid[3u * midCount + axis] = lo;
			midCount++;
		}
	}
	for (i = 0; i < midCount; i++)
	{
		const double *a = &mid[3u * i];
		const double *b = &mid[3u * ((i + 1u) % midCount)];

		if (a[axis] <= hi)
		{
			memcpy(&out[3u * outCount++], a, 3u * sizeof(double));
		}
		if ((a[axis] > hi) != (b[axis] > hi))
		{
			const double t = (hi - a[axis]) / (b[axis] - a[axis]);

			for (k = 0; k < 3u; k++)
			{
				out[3u * outCount + k] = a[k] + t * (b[k] - a[k]);
			}
			out[3u * outCount + axis] = hi;
			outCount++;
		}
	}
	return outCount;
}

// The cells a convex polygon may touch along one axis: from its lowest to its
// highest value, widened by the half cell and one more cell against rounding.
static void RldRem_PolygonCells(const struct RldRemGrid *g, const double *poly, u32 count, u32 axis, double slack, u32 *first, u32 *last)
{
	double mn = poly[axis];
	double mx = poly[axis];
	u32 i;

	for (i = 1; i < count; i++)
	{
		mn = (poly[3u * i + axis] < mn) ? poly[3u * i + axis] : mn;
		mx = (poly[3u * i + axis] > mx) ? poly[3u * i + axis] : mx;
	}
	*first = RldRem_CellOf(g, axis, mn - slack - g->cell);
	*last = RldRem_CellOf(g, axis, mx + slack + g->cell);
}

// Sets up the grid over the triangles: origin, cell, n from the box lo..hi
// with `margin` cells around it, and lists the triangles of every cell.
// 0 = out of memory or too many cells (why says which).
static int RldRem_BuildGrid(struct RldRemGrid *g, const double *lo, const double *hi, double cell, u32 margin, const double *position, u32 triCount,
                            const u32 *tri, const u32 *triIndex, char *why, size_t whySize)
{
	const double slack = 0.5 * cell * (1.0 + RLDREM_BOX_SLACK);
	const double widen = cell * RLDREM_BOX_SLACK;   // the clipping is generous, the overlap test decides
	u64 cells = 1u;
	u32 *pairCell = NULL;
	u32 *pairTri = NULL;
	u32 pairCount = 0;
	u32 pairCapacity = 0;
	u32 t;
	u32 k;

	memset(g, 0, sizeof(*g));
	g->cell = cell;
	g->ringLimit = RLDREM_NEAR_RINGS;
	g->position = position;
	g->triCount = triCount;
	g->tri = tri;
	g->triIndex = triIndex;
	for (k = 0; k < 3u; k++)
	{
		const double span = (hi[k] - lo[k]) / cell;

		g->origin[k] = lo[k] - (double)margin * cell;
		// span is at most resolution (plus rounding) here.
		g->n[k] = (u32)span + 1u + 2u * margin;
		cells *= (u64)g->n[k];
	}
	if (cells > 0x04000000u)
	{
		snprintf(why, whySize, "remesh: a grid of %u x %u x %u cells is too large.", g->n[0], g->n[1], g->n[2]);
		return 0;
	}
	g->cellCount = (u32)cells;

	g->start = (u32 *)calloc((size_t)g->cellCount + 1u, sizeof(u32));
	g->flag = (u8 *)calloc((size_t)g->cellCount + 1u, 1u);
	g->normal = (double *)malloc(((size_t)triCount + 1u) * 3u * sizeof(double));
	g->stamp = (u32 *)calloc((size_t)triCount + 1u, sizeof(u32));
	g->seed = (u32 *)malloc(((size_t)g->cellCount + 1u) * sizeof(u32));
	if ((g->start == NULL) || (g->flag == NULL) || (g->normal == NULL) || (g->stamp == NULL) || (g->seed == NULL))
	{
		snprintf(why, whySize, "remesh: out of memory.");
		RldRem_FreeGrid(g);
		return 0;
	}

	// Every (cell, triangle) that touch, in triangle order.
	for (t = 0; t < triCount; t++)
	{
		const double *a = &position[3u * tri[3u * t + 0u]];
		const double *b = &position[3u * tri[3u * t + 1u]];
		const double *c = &position[3u * tri[3u * t + 2u]];
		double corner[9];
		u32 c0[3];
		u32 c1[3];
		u32 x;
		u32 y;
		u32 z;

		RldRem_Normal(a, b, c, &g->normal[3u * t]);
		for (k = 0; k < 3u; k++)
		{
			double mn = a[k];
			double mx = a[k];

			mn = (b[k] < mn) ? b[k] : mn;
			mn = (c[k] < mn) ? c[k] : mn;
			mx = (b[k] > mx) ? b[k] : mx;
			mx = (c[k] > mx) ? c[k] : mx;
			c0[k] = RldRem_CellOf(g, k, mn - slack);
			c1[k] = RldRem_CellOf(g, k, mx + slack);
		}

		// Only the cells near the triangle are tested: per layer of cells
		// along z the triangle is clipped to the layer, per row along y the
		// rest again, and only the cells along x that this piece spans (one
		// more on each side) meet the exact test. A long thin triangle across
		// the grid costs its length, not its box.
		corner[0] = a[0];
		corner[1] = a[1];
		corner[2] = a[2];
		corner[3] = b[0];
		corner[4] = b[1];
		corner[5] = b[2];
		corner[6] = c[0];
		corner[7] = c[1];
		corner[8] = c[2];
		for (z = c0[2]; z <= c1[2]; z++)
		{
			const double zCenter = g->origin[2] + ((double)z + 0.5) * cell;
			double layer[3u * 9u];
			const u32 layerCount = RldRem_ClipSlab(corner, 3u, 2u, zCenter - slack - widen, zCenter + slack + widen, layer);
			u32 y0;
			u32 y1;

			if (layerCount == 0u)
			{
				continue;
			}
			RldRem_PolygonCells(g, layer, layerCount, 1u, slack, &y0, &y1);
			for (y = y0; y <= y1; y++)
			{
				const double yCenter = g->origin[1] + ((double)y + 0.5) * cell;
				double row[3u * 9u];
				const u32 rowCount = RldRem_ClipSlab(layer, layerCount, 1u, yCenter - slack - widen, yCenter + slack + widen, row);
				u32 x0;
				u32 x1;

				if (rowCount == 0u)
				{
					continue;
				}
				RldRem_PolygonCells(g, row, rowCount, 0u, slack, &x0, &x1);
				if (!RldRem_Work(g, x1 - x0 + 2u))
				{
					free(pairCell);
					free(pairTri);
					snprintf(why, whySize, "remesh: too complex for remesh at this resolution (the triangles touch too many cells).");
					RldRem_FreeGrid(g);
					return 0;
				}
				for (x = x0; x <= x1; x++)
				{
					double center[3];

					center[0] = g->origin[0] + ((double)x + 0.5) * cell;
					center[1] = yCenter;
					center[2] = zCenter;
					if (!RldRem_TriangleBox(center, slack, a, b, c))
					{
						continue;
					}
					if (pairCount == pairCapacity)
					{
						const u32 capacity = (pairCapacity < 1024u) ? 1024u : (2u * pairCapacity);
						u32 *grownCell;
						u32 *grownTri;

						// At most 2^28 pairs: twice that in bytes still fits a 32-bit size_t.
						if (pairCapacity >= 0x10000000u)
						{
							free(pairCell);
							free(pairTri);
							snprintf(why, whySize, "remesh: too many cells touched.");
							RldRem_FreeGrid(g);
							return 0;
						}
						grownCell = (u32 *)realloc(pairCell, (size_t)capacity * sizeof(u32));
						if (grownCell != NULL)
						{
							pairCell = grownCell;
						}
						grownTri = (u32 *)realloc(pairTri, (size_t)capacity * sizeof(u32));
						if (grownTri != NULL)
						{
							pairTri = grownTri;
						}
						if ((grownCell == NULL) || (grownTri == NULL))
						{
							free(pairCell);
							free(pairTri);
							snprintf(why, whySize, "remesh: out of memory.");
							RldRem_FreeGrid(g);
							return 0;
						}
						pairCapacity = capacity;
					}
					pairCell[pairCount] = x + g->n[0] * (y + g->n[1] * z);
					pairTri[pairCount] = t;
					pairCount++;
				}
			}
		}
	}

	// Counting sort by cell; within a cell the triangles stay ascending.
	g->list = (u32 *)malloc(((size_t)pairCount + 1u) * sizeof(u32));
	if (g->list == NULL)
	{
		free(pairCell);
		free(pairTri);
		snprintf(why, whySize, "remesh: out of memory.");
		RldRem_FreeGrid(g);
		return 0;
	}
	for (t = 0; t < pairCount; t++)
	{
		g->start[pairCell[t] + 1u]++;
		g->flag[pairCell[t]] = (u8)RLDREM_SURFACE;
	}
	for (t = 0; t < g->cellCount; t++)
	{
		g->start[t + 1u] += g->start[t];
	}
	{
		u32 *fill = (u32 *)malloc(((size_t)g->cellCount + 1u) * sizeof(u32));

		if (fill == NULL)
		{
			free(pairCell);
			free(pairTri);
			snprintf(why, whySize, "remesh: out of memory.");
			RldRem_FreeGrid(g);
			return 0;
		}
		memcpy(fill, g->start, (size_t)g->cellCount * sizeof(u32));
		for (t = 0; t < pairCount; t++)
		{
			g->list[fill[pairCell[t]]++] = pairTri[t];
		}
		free(fill);
	}
	free(pairCell);
	free(pairTri);

	// The nearest surface cell of every cell: a search in steps through the
	// 26 neighbors from all surface cells at once, in index order (the first
	// to arrive wins).
	{
		u32 *queue = (u32 *)malloc(((size_t)g->cellCount + 1u) * sizeof(u32));
		u32 head = 0;
		u32 tail = 0;
		const u32 nx = g->n[0];
		const u32 nxy = g->n[0] * g->n[1];

		if (queue == NULL)
		{
			snprintf(why, whySize, "remesh: out of memory.");
			RldRem_FreeGrid(g);
			return 0;
		}
		for (t = 0; t < g->cellCount; t++)
		{
			g->seed[t] = RLDREM_NONE;
			if (g->start[t + 1u] != g->start[t])
			{
				g->seed[t] = t;
				queue[tail++] = t;
			}
		}
		while (head < tail)
		{
			const u32 at = queue[head++];
			const u32 x = at % nx;
			const u32 y = (at / nx) % g->n[1];
			const u32 z = at / nxy;
			u32 dz;

			for (dz = 0; dz < 3u; dz++)
			{
				u32 dy;

				if (((z + dz) < 1u) || ((z + dz - 1u) >= g->n[2]))
				{
					continue;
				}
				for (dy = 0; dy < 3u; dy++)
				{
					u32 dx;

					if (((y + dy) < 1u) || ((y + dy - 1u) >= g->n[1]))
					{
						continue;
					}
					for (dx = 0; dx < 3u; dx++)
					{
						u32 next;

						if (((x + dx) < 1u) || ((x + dx - 1u) >= nx))
						{
							continue;
						}
						next = (x + dx - 1u) + nx * ((y + dy - 1u) + g->n[1] * (z + dz - 1u));
						if (g->seed[next] == RLDREM_NONE)
						{
							g->seed[next] = g->seed[at];
							queue[tail++] = next;
						}
					}
				}
			}
		}
		free(queue);
	}
	return 1;
}

// Measures triangle t of the grid against p (once per query) and keeps it when
// it is closer than the best so far. side (may be NULL): on equal distance a
// triangle whose normal points to side wins, then the lower index.
static void RldRem_Measure(struct RldRemGrid *g, u32 t, const double *p, const double *side, u32 *best, double *bestD2, int *bestFacing, double *bestPoint)
{
	const u32 *c = &g->tri[3u * t];
	double q[3];
	double d[3];
	double d2;
	int facing;

	if (g->stamp[t] == g->query)
	{
		return;
	}
	g->stamp[t] = g->query;

	RldRem_ClosestOnTriangle(p, &g->position[3u * c[0]], &g->position[3u * c[1]], &g->position[3u * c[2]], q);
	RldRem_Sub(p, q, d);
	d2 = RldRem_Dot(d, d);
	facing = (side != NULL) && (RldRem_Dot(&g->normal[3u * t], side) > 0.0);

	if ((*best != RLDREM_NONE) &&
	    ((d2 > *bestD2) ||
	     ((d2 == *bestD2) && ((facing < *bestFacing) || ((facing == *bestFacing) && (g->triIndex[t] > g->triIndex[*best]))))))
	{
		return;
	}
	*best = t;
	*bestD2 = d2;
	*bestFacing = facing;
	memcpy(bestPoint, q, sizeof(q));
}

// The triangles of one cell, measured against p (with the work counted).
static int RldRem_MeasureCell(struct RldRemGrid *g, u32 cell, const double *p, const double *side, u32 *best, double *bestD2, int *bestFacing,
                              double *point)
{
	u32 i;

	if (!RldRem_Work(g, 1u + g->start[cell + 1u] - g->start[cell]))
	{
		return 0;
	}
	for (i = g->start[cell]; i < g->start[cell + 1u]; i++)
	{
		RldRem_Measure(g, g->list[i], p, side, best, bestD2, bestFacing, point);
	}
	return 1;
}

// The triangle of the grid closest to p and its closest point; the cells are
// searched in rings around the cell of p. A point in a cell of ring r is at
// least (r - 1) cells away, so after ring r nothing closer than r cells can be
// missed, nor an equally close one (the stop is strict). After ringLimit rings
// without that proof, the cells around the surface cell nearest to p's cell
// (seed) are measured too and the closest of all found is taken: exact up to
// about ringLimit cells, an approximation beyond. RLDREM_NONE when the grid
// has no triangle or the work ran out.
static u32 RldRem_Nearest(struct RldRemGrid *g, const double *p, const double *side, double point[3], double *distance2)
{
	u32 best = RLDREM_NONE;
	double bestD2 = 0.0;
	int bestFacing = 0;
	u32 home[3];
	u32 ringMax = 0;
	u32 r;
	u32 k;
	int proven = 0;

	*distance2 = 0.0;
	g->query++;
	if (g->query == 0u)
	{
		memset(g->stamp, 0, (size_t)g->triCount * sizeof(u32));
		g->query = 1u;
	}

	for (k = 0; k < 3u; k++)
	{
		home[k] = RldRem_CellOf(g, k, p[k]);
		ringMax = (home[k] > ringMax) ? home[k] : ringMax;
		ringMax = ((g->n[k] - 1u - home[k]) > ringMax) ? (g->n[k] - 1u - home[k]) : ringMax;
	}
	ringMax = (ringMax < g->ringLimit) ? ringMax : g->ringLimit;

	for (r = 0; r <= ringMax; r++)
	{
		u32 lo[3];
		u32 hi[3];
		u32 x;
		u32 y;
		u32 z;

		for (k = 0; k < 3u; k++)
		{
			lo[k] = (home[k] >= r) ? (home[k] - r) : 0u;
			hi[k] = ((home[k] + r) < g->n[k]) ? (home[k] + r) : (g->n[k] - 1u);
		}
		for (z = lo[2]; z <= hi[2]; z++)
		{
			const int zEdge = ((z + r) == home[2]) || (z == (home[2] + r));

			for (y = lo[1]; y <= hi[1]; y++)
			{
				const int edge = zEdge || ((y + r) == home[1]) || (y == (home[1] + r));

				// Only the shell of ring r: the inside was searched before;
				// a row inside has only its two ends on the shell.
				x = lo[0];
				while (x <= hi[0])
				{
					if ((edge || ((x + r) == home[0]) || (x == (home[0] + r))) &&
					    !RldRem_MeasureCell(g, x + g->n[0] * (y + g->n[1] * z), p, side, &best, &bestD2, &bestFacing, point))
					{
						return RLDREM_NONE;
					}
					x = (edge || (x != lo[0]) || (x == hi[0])) ? (x + 1u) : hi[0];
				}
			}
		}
		if ((best != RLDREM_NONE) && (bestD2 < ((double)r * g->cell) * ((double)r * g->cell)))
		{
			proven = 1;
			break;
		}
	}

	if (!proven && (g->seed[home[0] + g->n[0] * (home[1] + g->n[1] * home[2])] != RLDREM_NONE))
	{
		const u32 seed = g->seed[home[0] + g->n[0] * (home[1] + g->n[1] * home[2])];
		const u32 sx = seed % g->n[0];
		const u32 sy = (seed / g->n[0]) % g->n[1];
		const u32 sz = seed / (g->n[0] * g->n[1]);
		u32 x;
		u32 y;
		u32 z;

		for (z = (sz > 0u) ? (sz - 1u) : 0u; (z <= (sz + 1u)) && (z < g->n[2]); z++)
		{
			for (y = (sy > 0u) ? (sy - 1u) : 0u; (y <= (sy + 1u)) && (y < g->n[1]); y++)
			{
				for (x = (sx > 0u) ? (sx - 1u) : 0u; (x <= (sx + 1u)) && (x < g->n[0]); x++)
				{
					if (!RldRem_MeasureCell(g, x + g->n[0] * (y + g->n[1] * z), p, side, &best, &bestD2, &bestFacing, point))
					{
						return RLDREM_NONE;
					}
				}
			}
		}
	}

	*distance2 = bestD2;
	return best;
}

// THE RAY VOTE. A flood fill alone runs through every hole wider than a cell:
// the inside of an open body (a driver without a bottom, a kart body open at
// the wheels) would become outside, and the hull a thin shell around both
// faces of its skin - twice the triangles, half of them never seen. So each
// cell center also casts six rays along the axes to the edge of the grid and
// counts the triangles of the group each ray passes through. An odd count
// means the ray left a closed surface once more than it entered it; when at
// least RLDREM_VOTES_INSIDE of the six rays say so, the cell is inside and
// stops the flood fill. A hole only misleads the rays that pass through it,
// and the parity does not depend on how the triangles are wound.
//
// A ray along axis a through the center (u, v) of a column of cells hits a
// triangle when (u, v) lies in the triangle projected along a. A point on an
// edge belongs to exactly one of two triangles that meet there from opposite
// sides (the top-left rule of rasterizers on the triangle turned
// counterclockwise; the edge function is always computed from the
// lexicographically lower end, so both triangles see the same value). A ray
// along a triangle's plane (no projected area) does not count.
struct RldRemCross
{
	u32 line;      // the column: index u + n[u] * v
	double at;     // where the ray meets the triangle along axis a
};

static int RldRem_CompareCross(const void *p, const void *q)
{
	const struct RldRemCross *x = (const struct RldRemCross *)p;
	const struct RldRemCross *y = (const struct RldRemCross *)q;

	if (x->line != y->line)
	{
		return (x->line < y->line) ? -1 : 1;
	}
	return (x->at < y->at) ? -1 : ((x->at > y->at) ? 1 : 0);
}

// The edge function of the edge a -> b at p (2D: index 0 u, 1 v), positive
// left of the edge; computed from the lower end of the edge, so a -> b and
// b -> a give exactly opposite values.
static double RldRem_EdgeSide(const double *a, const double *b, const double *p)
{
	const int swap = (b[0] < a[0]) || ((b[0] == a[0]) && (b[1] < a[1]));
	const double *lo = swap ? b : a;
	const double *hi = swap ? a : b;
	const double e = (hi[0] - lo[0]) * (p[1] - lo[1]) - (hi[1] - lo[1]) * (p[0] - lo[0]);

	return swap ? -e : e;
}

// 1 when p is in the counterclockwise 2D triangle a, b, c (top-left rule).
static int RldRem_InTriangle2(const double *a, const double *b, const double *c, const double *p)
{
	const double *corner[3];
	u32 k;

	corner[0] = a;
	corner[1] = b;
	corner[2] = c;
	for (k = 0; k < 3u; k++)
	{
		const double *from = corner[k];
		const double *to = corner[(k + 1u) % 3u];
		const double e = RldRem_EdgeSide(from, to, p);

		if (e < 0.0)
		{
			return 0;
		}
		if (e == 0.0)
		{
			const double du = to[0] - from[0];
			const double dv = to[1] - from[1];

			// Counterclockwise, the left edges run down and the top edges left.
			if (!((dv < 0.0) || ((dv == 0.0) && (du < 0.0))))
			{
				return 0;
			}
		}
	}
	return 1;
}

// Counts per cell the rays (of six) that see an odd number of triangles and
// flags the cells with at least RLDREM_VOTES_INSIDE as RLDREM_VOTED.
// 0 = out of memory.
static int RldRem_VoteInside(struct RldRemGrid *g)
{
	u8 *votes = (u8 *)calloc((size_t)g->cellCount + 1u, 1u);
	struct RldRemCross *cross = NULL;
	u32 crossCount = 0;
	u32 crossCapacity = 0;
	u32 a;
	u32 t;

	if (votes == NULL)
	{
		return 0;
	}

	for (a = 0; a < 3u; a++)
	{
		const u32 au = (a + 1u) % 3u;
		const u32 av = (a + 2u) % 3u;
		u32 first;

		crossCount = 0;
		for (t = 0; t < g->triCount; t++)
		{
			const double *p0 = &g->position[3u * g->tri[3u * t + 0u]];
			const double *p1 = &g->position[3u * g->tri[3u * t + 1u]];
			const double *p2 = &g->position[3u * g->tri[3u * t + 2u]];
			const double *n = &g->normal[3u * t];
			double q[3][2];
			double area;
			u32 lo[2];
			u32 hi[2];
			u32 j;
			u32 k;

			q[0][0] = p0[au];
			q[0][1] = p0[av];
			q[1][0] = p1[au];
			q[1][1] = p1[av];
			q[2][0] = p2[au];
			q[2][1] = p2[av];
			area = (q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) - (q[2][0] - q[0][0]) * (q[1][1] - q[0][1]);
			if ((area == 0.0) || (n[a] == 0.0))
			{
				continue;
			}
			for (k = 0; k < 2u; k++)
			{
				double mn = q[0][k];
				double mx = q[0][k];

				mn = (q[1][k] < mn) ? q[1][k] : mn;
				mn = (q[2][k] < mn) ? q[2][k] : mn;
				mx = (q[1][k] > mx) ? q[1][k] : mx;
				mx = (q[2][k] > mx) ? q[2][k] : mx;
				lo[k] = RldRem_CellOf(g, (k == 0u) ? au : av, mn);
				hi[k] = RldRem_CellOf(g, (k == 0u) ? au : av, mx);
			}

			for (k = lo[1]; k <= hi[1]; k++)
			{
				// The columns of this row that can lie in the triangle: where
				// the row's line meets the edges, one more on each side.
				const double v = g->origin[av] + ((double)k + 0.5) * g->cell;
				double uLo = 0.0;
				double uHi = 0.0;
				int any = 0;
				u32 e;
				u32 j0;
				u32 j1;

				for (e = 0; e < 3u; e++)
				{
					const double *from = q[e];
					const double *to = q[(e + 1u) % 3u];
					double u;

					if (((from[1] < v) && (to[1] < v)) || ((from[1] > v) && (to[1] > v)))
					{
						continue;
					}
					u = (to[1] != from[1]) ? (from[0] + (v - from[1]) * (to[0] - from[0]) / (to[1] - from[1])) : from[0];
					uLo = (!any || (u < uLo)) ? u : uLo;
					uHi = (!any || (u > uHi)) ? u : uHi;
					if (to[1] == from[1])
					{
						uLo = (to[0] < uLo) ? to[0] : uLo;
						uHi = (to[0] > uHi) ? to[0] : uHi;
					}
					any = 1;
				}
				if (!any)
				{
					continue;
				}
				j0 = RldRem_CellOf(g, au, uLo - g->cell);
				j1 = RldRem_CellOf(g, au, uHi + g->cell);
				j0 = (j0 > lo[0]) ? j0 : lo[0];
				j1 = (j1 < hi[0]) ? j1 : hi[0];
				if (!RldRem_Work(g, 1u + ((j1 >= j0) ? (j1 - j0) : 0u)))
				{
					free(cross);
					free(votes);
					return 0;
				}
				for (j = j0; j <= j1; j++)
				{
					double p[2];
					int in;

					p[0] = g->origin[au] + ((double)j + 0.5) * g->cell;
					p[1] = v;
					in = (area > 0.0) ? RldRem_InTriangle2(q[0], q[1], q[2], p) : RldRem_InTriangle2(q[0], q[2], q[1], p);
					if (!in)
					{
						continue;
					}
					if (crossCount == crossCapacity)
					{
						const u32 capacity = (crossCapacity < 1024u) ? 1024u : (2u * crossCapacity);
						struct RldRemCross *grown;

						if (crossCapacity >= 0x04000000u)
						{
							free(cross);
							free(votes);
							return 0;
						}
						grown = (struct RldRemCross *)realloc(cross, (size_t)capacity * sizeof(struct RldRemCross));
						if (grown == NULL)
						{
							free(cross);
							free(votes);
							return 0;
						}
						cross = grown;
						crossCapacity = capacity;
					}
					cross[crossCount].line = j + g->n[au] * k;
					cross[crossCount].at = p0[a] - (n[au] * (p[0] - p0[au]) + n[av] * (p[1] - p0[av])) / n[a];
					crossCount++;
				}
			}
		}

		// Per column: the crossings before and after each center.
		if (crossCount != 0u)
		{
			qsort(cross, crossCount, sizeof(struct RldRemCross), RldRem_CompareCross);
		}
		for (first = 0; first < crossCount;)
		{
			const u32 line = cross[first].line;
			const u32 j = line % g->n[au];
			const u32 k = line / g->n[au];
			u32 end = first;
			u32 before = first;
			u32 upTo = first;
			u32 i;

			while ((end < crossCount) && (cross[end].line == line))
			{
				end++;
			}
			for (i = 0; i < g->n[a]; i++)
			{
				const double center = g->origin[a] + ((double)i + 0.5) * g->cell;
				u32 at[3];
				u32 cell;

				while ((before < end) && (cross[before].at < center))
				{
					before++;
				}
				while ((upTo < end) && (cross[upTo].at <= center))
				{
					upTo++;
				}
				at[a] = i;
				at[au] = j;
				at[av] = k;
				cell = at[0] + g->n[0] * (at[1] + g->n[1] * at[2]);
				votes[cell] = (u8)(votes[cell] + ((before - first) & 1u) + ((end - upTo) & 1u));
			}
			first = end;
		}
	}

	for (t = 0; t < g->cellCount; t++)
	{
		if (votes[t] >= RLDREM_VOTES_INSIDE)
		{
			g->flag[t] |= (u8)RLDREM_VOTED;
		}
	}
	free(cross);
	free(votes);
	return 1;
}

// Marks every cell that is reachable from cell 0 through cells that neither
// a triangle touches nor the rays call inside (six neighbors) as outside.
// 0 = out of memory.
static int RldRem_FloodOutside(struct RldRemGrid *g)
{
	u32 *queue = (u32 *)malloc(((size_t)g->cellCount + 1u) * sizeof(u32));
	u32 head = 0;
	u32 tail = 0;
	const u32 nx = g->n[0];
	const u32 nxy = g->n[0] * g->n[1];

	if (queue == NULL)
	{
		return 0;
	}
	// Cell 0 lies in the margin: no triangle touches it.
	g->flag[0] |= (u8)RLDREM_OUTSIDE;
	queue[tail++] = 0u;
	while (head < tail)
	{
		const u32 cell = queue[head++];
		const u32 x = cell % nx;
		const u32 y = (cell / nx) % g->n[1];
		const u32 z = cell / nxy;
		u32 next[6];
		u32 count = 0;
		u32 i;

		if (x > 0u)
		{
			next[count++] = cell - 1u;
		}
		if ((x + 1u) < nx)
		{
			next[count++] = cell + 1u;
		}
		if (y > 0u)
		{
			next[count++] = cell - nx;
		}
		if ((y + 1u) < g->n[1])
		{
			next[count++] = cell + nx;
		}
		if (z > 0u)
		{
			next[count++] = cell - nxy;
		}
		if ((z + 1u) < g->n[2])
		{
			next[count++] = cell + nxy;
		}
		for (i = 0; i < count; i++)
		{
			if (g->flag[next[i]] == 0u)
			{
				g->flag[next[i]] = (u8)RLDREM_OUTSIDE;
				queue[tail++] = next[i];
			}
		}
	}
	free(queue);
	return 1;
}

//========================================================================================
// MARCHING TETRAHEDRA
//========================================================================================

// The six tetrahedra of a cube around its main diagonal 0-7 (corner bits: 1 x,
// 2 y, 4 z): 0, one axis, two axes, 7 - for each order of the axes. Any two
// corners of one are comparable (one's bits contain the other's), so each edge
// runs from a lower corner in one of seven directions (the bits that differ).
static const u8 s_rldRemTetra[6][4] = {
	{0u, 1u, 3u, 7u}, {0u, 1u, 5u, 7u}, {0u, 2u, 3u, 7u}, {0u, 2u, 6u, 7u}, {0u, 4u, 5u, 7u}, {0u, 4u, 6u, 7u},
};

static int RldRem_CompareKey(const void *a, const void *b)
{
	const u64 x = *(const u64 *)a;
	const u64 y = *(const u64 *)b;

	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

// The key of the edge from corner `lower` to corner `upper` of the cube whose
// low center is (x, y, z): the low end's center index times 8 plus the
// direction bits. Equal edges of neighboring cubes get the same key.
static u64 RldRem_EdgeKey(const struct RldRemGrid *g, u32 x, u32 y, u32 z, u32 lower, u32 upper)
{
	const u32 cx = x + (lower & 1u);
	const u32 cy = y + ((lower >> 1) & 1u);
	const u32 cz = z + ((lower >> 2) & 1u);

	return ((u64)(cx + g->n[0] * (cy + g->n[1] * cz)) << 3) | (u64)(lower ^ upper);
}

// Twice the position of the middle of an edge, in center units (exact).
static void RldRem_KeyDoubled(const struct RldRemGrid *g, u64 key, s64 out[3])
{
	const u32 center = (u32)(key >> 3);
	const u32 dir = (u32)(key & 7u);

	out[0] = 2 * (s64)(center % g->n[0]) + (s64)(dir & 1u);
	out[1] = 2 * (s64)((center / g->n[0]) % g->n[1]) + (s64)((dir >> 1) & 1u);
	out[2] = 2 * (s64)(center / (g->n[0] * g->n[1])) + (s64)((dir >> 2) & 1u);
}

// The position of an edge middle in game units.
static void RldRem_KeyPosition(const struct RldRemGrid *g, u64 key, double out[3])
{
	s64 d[3];
	u32 k;

	RldRem_KeyDoubled(g, key, d);
	for (k = 0; k < 3u; k++)
	{
		out[k] = g->origin[k] + ((double)d[k] + 1.0) * 0.5 * g->cell;
	}
}

struct RldRemKeys
{
	u64 *key;      // 3 per triangle
	u32 count;     // triangles
	u32 capacity;
	int failed;
};

// Adds the triangle of edge keys a, b, c, wound so that its normal points to
// the corner `out` (doubled center units, exact integers).
static void RldRem_AddTriangle(const struct RldRemGrid *g, struct RldRemKeys *keys, u64 a, u64 b, u64 c, const s64 *out)
{
	s64 pa[3];
	s64 pb[3];
	s64 pc[3];
	s64 u[3];
	s64 v[3];
	s64 n[3];
	s64 side;

	if (keys->failed)
	{
		return;
	}
	if (keys->count == keys->capacity)
	{
		const u32 capacity = (keys->capacity < 4096u) ? 4096u : (2u * keys->capacity);
		u64 *grown;

		if (keys->capacity >= RLDREM_INPUT_MAX)
		{
			keys->failed = 2;
			return;
		}
		grown = (u64 *)realloc(keys->key, (size_t)capacity * 3u * sizeof(u64));
		if (grown == NULL)
		{
			keys->failed = 1;
			return;
		}
		keys->key = grown;
		keys->capacity = capacity;
	}

	RldRem_KeyDoubled(g, a, pa);
	RldRem_KeyDoubled(g, b, pb);
	RldRem_KeyDoubled(g, c, pc);
	u[0] = pb[0] - pa[0];
	u[1] = pb[1] - pa[1];
	u[2] = pb[2] - pa[2];
	v[0] = pc[0] - pa[0];
	v[1] = pc[1] - pa[1];
	v[2] = pc[2] - pa[2];
	n[0] = u[1] * v[2] - u[2] * v[1];
	n[1] = u[2] * v[0] - u[0] * v[2];
	n[2] = u[0] * v[1] - u[1] * v[0];
	side = n[0] * (out[0] - pa[0]) + n[1] * (out[1] - pa[1]) + n[2] * (out[2] - pa[2]);

	keys->key[3u * keys->count + 0u] = a;
	keys->key[3u * keys->count + 1u] = (side > 0) ? b : c;
	keys->key[3u * keys->count + 2u] = (side > 0) ? c : b;
	keys->count++;
}

// The zero set of inside (-) / outside (+) over all tetrahedra of the grid.
static void RldRem_March(const struct RldRemGrid *g, struct RldRemKeys *keys)
{
	u32 x;
	u32 y;
	u32 z;

	for (z = 0; (z + 1u) < g->n[2]; z++)
	{
		for (y = 0; (y + 1u) < g->n[1]; y++)
		{
			for (x = 0; (x + 1u) < g->n[0]; x++)
			{
				u32 inside = 0;   // bit per cube corner
				u32 corner;
				u32 t;

				for (corner = 0; corner < 8u; corner++)
				{
					const u32 cell = (x + (corner & 1u)) + g->n[0] * ((y + ((corner >> 1) & 1u)) + g->n[1] * (z + ((corner >> 2) & 1u)));

					if (!(g->flag[cell] & RLDREM_OUTSIDE))
					{
						inside |= 1u << corner;
					}
				}
				if ((inside == 0u) || (inside == 0xFFu))
				{
					continue;
				}

				for (t = 0; t < 6u; t++)
				{
					const u8 *q = s_rldRemTetra[t];
					u32 in[4];
					u32 outs[4];
					u32 inCount = 0;
					u32 outCount = 0;
					s64 outCorner[3];
					u32 i;

					for (i = 0; i < 4u; i++)
					{
						if (inside & (1u << q[i]))
						{
							in[inCount++] = q[i];
						}
						else
						{
							outs[outCount++] = q[i];
						}
					}
					if ((inCount == 0u) || (outCount == 0u))
					{
						continue;
					}
					outCorner[0] = 2 * (s64)(x + (outs[0] & 1u));
					outCorner[1] = 2 * (s64)(y + ((outs[0] >> 1) & 1u));
					outCorner[2] = 2 * (s64)(z + ((outs[0] >> 2) & 1u));

#define RLDREM_KEY(p, r) RldRem_EdgeKey(g, x, y, z, ((p) < (r)) ? (p) : (r), ((p) < (r)) ? (r) : (p))
					if (inCount == 1u)
					{
						RldRem_AddTriangle(g, keys, RLDREM_KEY(in[0], outs[0]), RLDREM_KEY(in[0], outs[1]), RLDREM_KEY(in[0], outs[2]), outCorner);
					}
					else if (outCount == 1u)
					{
						RldRem_AddTriangle(g, keys, RLDREM_KEY(outs[0], in[0]), RLDREM_KEY(outs[0], in[1]), RLDREM_KEY(outs[0], in[2]), outCorner);
					}
					else
					{
						// Two and two: the quad in[0]-outs[0], in[0]-outs[1],
						// in[1]-outs[1], in[1]-outs[0], cut along its shorter
						// diagonal (exact; the first one on a tie). Both lie
						// inside the tetrahedron, so either keeps the surface
						// closed; the shorter gives triangles of q >= 0.8
						// instead of 0.49.
						const u64 k00 = RLDREM_KEY(in[0], outs[0]);
						const u64 k01 = RLDREM_KEY(in[0], outs[1]);
						const u64 k11 = RLDREM_KEY(in[1], outs[1]);
						const u64 k10 = RLDREM_KEY(in[1], outs[0]);
						s64 a[3];
						s64 b[3];
						s64 c[3];
						s64 d[3];
						s64 first = 0;
						s64 second = 0;
						u32 axis;

						RldRem_KeyDoubled(g, k00, a);
						RldRem_KeyDoubled(g, k11, b);
						RldRem_KeyDoubled(g, k01, c);
						RldRem_KeyDoubled(g, k10, d);
						for (axis = 0; axis < 3u; axis++)
						{
							first += (b[axis] - a[axis]) * (b[axis] - a[axis]);
							second += (d[axis] - c[axis]) * (d[axis] - c[axis]);
						}
						if (second < first)
						{
							RldRem_AddTriangle(g, keys, k00, k01, k10, outCorner);
							RldRem_AddTriangle(g, keys, k01, k11, k10, outCorner);
						}
						else
						{
							RldRem_AddTriangle(g, keys, k00, k01, k11, outCorner);
							RldRem_AddTriangle(g, keys, k00, k11, k10, outCorner);
						}
					}
#undef RLDREM_KEY
				}
			}
		}
	}
}

//========================================================================================
// THE HULL OF ONE GROUP
//========================================================================================

// Counts and checks: every edge has exactly two triangles running opposite
// ways, no two triangles share all positions, the volume is positive.
// 1 = good; otherwise why says what.
struct RldRemEdge
{
	u32 a;
	u32 b;     // a < b
	u32 forward;   // 1 when the triangle runs a -> b
};

static int RldRem_CompareEdge(const void *p, const void *q)
{
	const struct RldRemEdge *x = (const struct RldRemEdge *)p;
	const struct RldRemEdge *y = (const struct RldRemEdge *)q;

	if (x->a != y->a)
	{
		return (x->a < y->a) ? -1 : 1;
	}
	if (x->b != y->b)
	{
		return (x->b < y->b) ? -1 : 1;
	}
	return (x->forward < y->forward) ? -1 : ((x->forward > y->forward) ? 1 : 0);
}

static int RldRem_CompareTriple(const void *p, const void *q)
{
	const u32 *x = (const u32 *)p;
	const u32 *y = (const u32 *)q;
	u32 k;

	for (k = 0; k < 3u; k++)
	{
		if (x[k] != y[k])
		{
			return (x[k] < y[k]) ? -1 : 1;
		}
	}
	return 0;
}

static void RldRem_Sort3(const u32 *in, u32 out[3])
{
	u32 s;

	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
	if (out[0] > out[1])
	{
		s = out[0];
		out[0] = out[1];
		out[1] = s;
	}
	if (out[1] > out[2])
	{
		s = out[1];
		out[1] = out[2];
		out[2] = s;
	}
	if (out[0] > out[1])
	{
		s = out[0];
		out[0] = out[1];
		out[1] = s;
	}
}

// The signed volume of a set of triangles (six times it, exact enough).
static double RldRem_Volume6(const double *position, const u32 *triangle, u32 count)
{
	double v = 0.0;
	u32 t;

	for (t = 0; t < count; t++)
	{
		const double *a = &position[3u * triangle[3u * t + 0u]];
		const double *b = &position[3u * triangle[3u * t + 1u]];
		const double *c = &position[3u * triangle[3u * t + 2u]];

		v += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
	}
	return v;
}

static int RldRem_CheckHull(const double *position, const u32 *triangle, u32 count, char *why, size_t whySize)
{
	struct RldRemEdge *edge;
	u32 *sorted;
	u32 t;
	u32 k;
	int ok = 1;

	if (count == 0u)
	{
		snprintf(why, whySize, "remesh: the hull has no triangle.");
		return 0;
	}
	edge = (struct RldRemEdge *)malloc((size_t)count * 3u * sizeof(struct RldRemEdge));
	sorted = (u32 *)malloc((size_t)count * 3u * sizeof(u32));
	if ((edge == NULL) || (sorted == NULL))
	{
		free(edge);
		free(sorted);
		snprintf(why, whySize, "remesh: out of memory.");
		return 0;
	}

	for (t = 0; t < count; t++)
	{
		const u32 *c = &triangle[3u * t];

		for (k = 0; k < 3u; k++)
		{
			const u32 a = c[k];
			const u32 b = c[(k + 1u) % 3u];
			struct RldRemEdge *e = &edge[3u * t + k];

			e->a = (a < b) ? a : b;
			e->b = (a < b) ? b : a;
			e->forward = (a < b) ? 1u : 0u;
		}
		RldRem_Sort3(c, &sorted[3u * t]);
		if ((sorted[3u * t] == sorted[3u * t + 1u]) || (sorted[3u * t + 1u] == sorted[3u * t + 2u]))
		{
			snprintf(why, whySize, "remesh: triangle %u uses a position twice.", t);
			ok = 0;
		}
	}

	if (ok)
	{
		qsort(edge, (size_t)count * 3u, sizeof(struct RldRemEdge), RldRem_CompareEdge);
		for (t = 0; t < (3u * count); t += 2u)
		{
			// Sorted, each edge must be a pair: backward then forward.
			if (((t + 1u) >= (3u * count)) || (edge[t].a != edge[t + 1u].a) || (edge[t].b != edge[t + 1u].b) || (edge[t].forward != 0u) ||
			    (edge[t + 1u].forward != 1u) || (((t + 2u) < (3u * count)) && (edge[t + 2u].a == edge[t].a) && (edge[t + 2u].b == edge[t].b)))
			{
				snprintf(why, whySize, "remesh: the edge %u-%u does not have exactly two triangles running opposite ways.", edge[t].a, edge[t].b);
				ok = 0;
				break;
			}
		}
	}

	if (ok)
	{
		qsort(sorted, count, 3u * sizeof(u32), RldRem_CompareTriple);
		for (t = 1; t < count; t++)
		{
			if (RldRem_CompareTriple(&sorted[3u * (t - 1u)], &sorted[3u * t]) == 0)
			{
				snprintf(why, whySize, "remesh: two triangles on the positions %u %u %u.", sorted[3u * t], sorted[3u * t + 1u], sorted[3u * t + 2u]);
				ok = 0;
				break;
			}
		}
	}

	if (ok && !(RldRem_Volume6(position, triangle, count) > 0.0))
	{
		snprintf(why, whySize, "remesh: the hull encloses no positive volume.");
		ok = 0;
	}

	free(edge);
	free(sorted);
	return ok;
}

// Numbers of the last RldRem_Remesh, summed over its groups (for the packer's
// report and the self-test).
struct RldRemStats
{
	u32 groups;
	u32 cells;          // all grid cells
	u32 surfaceCells;
	u32 insideCells;    // surface cells included
	u32 votedCells;     // no surface cells, but the rays call them inside
	u32 pulled;         // positions moved onto the model in full
	u32 clamped;        // of them, the pull was cut at RLDREM_PULL_LIMIT
	u32 guarded;        // positions whose move the guard halved or dropped
	u32 dropped;        // of them, left on the grid
	u32 guardPasses;
	u64 work;           // the most steps one group took (RLDREM_WORK_MAX)
};
static struct RldRemStats s_rldRemStats;

// Appends the hull of the group's triangles (numbers in `list`) to out.
static int RldRem_Group(const RldRemInput *in, u8 group, const u32 *list, u32 listCount, RldRemResult *out, char *why, size_t whySize)
{
	struct RldRemGrid g;
	struct RldRemKeys keys;
	u32 *tri = NULL;
	u64 *vertexKey = NULL;
	u32 *triangle = NULL;
	double *base = NULL;       // the grid position of each new position
	double *smooth = NULL;
	double *target = NULL;
	double *pos = NULL;
	u8 *level = NULL;
	u8 *mark = NULL;
	u32 *adjStart = NULL;
	u32 *adj = NULL;
	u32 *colors = NULL;
	u32 vertexCount = 0;
	double lo[3];
	double hi[3];
	double longest = 0.0;
	double cell;
	u32 t;
	u32 v;
	u32 k;
	int ok = 0;

	memset(&g, 0, sizeof(g));
	memset(&keys, 0, sizeof(keys));

	// The box of the group.
	for (k = 0; k < 3u; k++)
	{
		lo[k] = in->position[3u * in->triangle[3u * list[0]] + k];
		hi[k] = lo[k];
	}
	for (t = 0; t < listCount; t++)
	{
		for (v = 0; v < 3u; v++)
		{
			const double *p = &in->position[3u * in->triangle[3u * list[t] + v]];

			for (k = 0; k < 3u; k++)
			{
				lo[k] = (p[k] < lo[k]) ? p[k] : lo[k];
				hi[k] = (p[k] > hi[k]) ? p[k] : hi[k];
			}
		}
	}
	for (k = 0; k < 3u; k++)
	{
		longest = ((hi[k] - lo[k]) > longest) ? (hi[k] - lo[k]) : longest;
	}
	if (!(longest > 0.0))
	{
		snprintf(why, whySize, "remesh: group %u has no size (all its positions are equal).", (u32)group);
		return 0;
	}
	cell = longest / (double)in->resolution;

	tri = (u32 *)malloc((size_t)listCount * 3u * sizeof(u32));
	if (tri == NULL)
	{
		snprintf(why, whySize, "remesh: out of memory.");
		return 0;
	}
	for (t = 0; t < listCount; t++)
	{
		memcpy(&tri[3u * t], &in->triangle[3u * list[t]], 3u * sizeof(u32));
	}

	// 1, 2: grid, surface cells, outside.
	if (!RldRem_BuildGrid(&g, lo, hi, cell, RLDREM_MARGIN, in->position, listCount, tri, list, why, whySize))
	{
		free(tri);
		return 0;
	}
	if (!RldRem_VoteInside(&g) || !RldRem_FloodOutside(&g))
	{
		snprintf(why, whySize, g.overrun ? "remesh: too complex for remesh at this resolution (group %u)." : "remesh: out of memory.", (u32)group);
		goto done;
	}
	s_rldRemStats.groups++;
	s_rldRemStats.cells += g.cellCount;
	for (t = 0; t < g.cellCount; t++)
	{
		s_rldRemStats.surfaceCells += (g.flag[t] & RLDREM_SURFACE) ? 1u : 0u;
		s_rldRemStats.insideCells += (g.flag[t] & RLDREM_OUTSIDE) ? 0u : 1u;
		s_rldRemStats.votedCells += ((g.flag[t] & (RLDREM_VOTED | RLDREM_SURFACE)) == RLDREM_VOTED) ? 1u : 0u;
	}

	// 3: the surface; its positions are the sorted unique edge keys.
	RldRem_March(&g, &keys);
	if (keys.failed)
	{
		snprintf(why, whySize, (keys.failed == 2) ? "remesh: group %u gets more than %u triangles; lower the resolution." : "remesh: out of memory.",
		         (u32)group, RLDREM_INPUT_MAX);
		goto done;
	}
	if (keys.count == 0u)
	{
		snprintf(why, whySize, "remesh: group %u gives no surface.", (u32)group);
		goto done;
	}
	vertexKey = (u64 *)malloc((size_t)keys.count * 3u * sizeof(u64));
	triangle = (u32 *)malloc((size_t)keys.count * 3u * sizeof(u32));
	if ((vertexKey == NULL) || (triangle == NULL))
	{
		snprintf(why, whySize, "remesh: out of memory.");
		goto done;
	}
	memcpy(vertexKey, keys.key, (size_t)keys.count * 3u * sizeof(u64));
	qsort(vertexKey, (size_t)keys.count * 3u, sizeof(u64), RldRem_CompareKey);
	for (t = 0; t < (3u * keys.count); t++)
	{
		if ((vertexCount == 0u) || (vertexKey[vertexCount - 1u] != vertexKey[t]))
		{
			vertexKey[vertexCount++] = vertexKey[t];
		}
	}
	for (t = 0; t < (3u * keys.count); t++)
	{
		u32 low = 0;
		u32 high = vertexCount;

		while ((high - low) > 1u)
		{
			const u32 mid = low + (high - low) / 2u;

			if (vertexKey[mid] <= keys.key[t])
			{
				low = mid;
			}
			else
			{
				high = mid;
			}
		}
		triangle[t] = low;
	}

	base = (double *)malloc((size_t)vertexCount * 3u * sizeof(double));
	smooth = (double *)malloc((size_t)vertexCount * 3u * sizeof(double));
	target = (double *)malloc((size_t)vertexCount * 3u * sizeof(double));
	pos = (double *)malloc((size_t)vertexCount * 3u * sizeof(double));
	level = (u8 *)calloc((size_t)vertexCount, 1u);
	mark = (u8 *)calloc((size_t)vertexCount, 1u);
	colors = (u32 *)malloc((size_t)vertexCount * sizeof(u32));
	adjStart = (u32 *)calloc((size_t)vertexCount + 1u, sizeof(u32));
	adj = (u32 *)malloc((size_t)keys.count * 6u * sizeof(u32));
	if ((base == NULL) || (smooth == NULL) || (target == NULL) || (pos == NULL) || (level == NULL) || (mark == NULL) || (colors == NULL) ||
	    (adjStart == NULL) || (adj == NULL))
	{
		snprintf(why, whySize, "remesh: out of memory.");
		goto done;
	}
	for (v = 0; v < vertexCount; v++)
	{
		RldRem_KeyPosition(&g, vertexKey[v], &base[3u * v]);
	}

	// The neighbors of each position: on a closed manifold every edge has two
	// triangles, so each triangle's edge a -> b gives b as a neighbor of a
	// exactly once (the other triangle gives a to b).
	for (t = 0; t < (3u * keys.count); t++)
	{
		adjStart[triangle[t] + 1u]++;
	}
	for (v = 0; v < vertexCount; v++)
	{
		adjStart[v + 1u] += adjStart[v];
	}
	{
		u32 *fill = (u32 *)malloc(((size_t)vertexCount + 1u) * sizeof(u32));

		if (fill == NULL)
		{
			snprintf(why, whySize, "remesh: out of memory.");
			goto done;
		}
		memcpy(fill, adjStart, (size_t)vertexCount * sizeof(u32));
		for (t = 0; t < keys.count; t++)
		{
			for (k = 0; k < 3u; k++)
			{
				adj[fill[triangle[3u * t + k]]++] = triangle[3u * t + (k + 1u) % 3u];
			}
		}
		free(fill);
	}

	// 4: smooth, then pull onto the model.
	memcpy(smooth, base, (size_t)vertexCount * 3u * sizeof(double));
	for (k = 0; k < RLDREM_SMOOTH_STEPS; k++)
	{
		memcpy(pos, smooth, (size_t)vertexCount * 3u * sizeof(double));
		for (v = 0; v < vertexCount; v++)
		{
			double sum[3] = {0.0, 0.0, 0.0};
			const u32 count = adjStart[v + 1u] - adjStart[v];
			u32 i;
			u32 a;

			for (i = adjStart[v]; i < adjStart[v + 1u]; i++)
			{
				for (a = 0; a < 3u; a++)
				{
					sum[a] += pos[3u * adj[i] + a];
				}
			}
			for (a = 0; a < 3u; a++)
			{
				smooth[3u * v + a] = pos[3u * v + a] + RLDREM_SMOOTH_FACTOR * (sum[a] / (double)count - pos[3u * v + a]);
			}
		}
	}
	for (v = 0; v < vertexCount; v++)
	{
		double q[3];
		double d[3];
		double d2;
		const double limit = RLDREM_PULL_LIMIT * cell;

		if (RldRem_Nearest(&g, &smooth[3u * v], NULL, q, &d2) == RLDREM_NONE)
		{
			memcpy(q, &smooth[3u * v], sizeof(q));
		}
		else
		{
			// RLDREM_SURFACE_GAP off the model, toward the grid position.
			double out[3];
			double length2;

			RldRem_Sub(&base[3u * v], q, out);
			length2 = RldRem_Dot(out, out);

			if (length2 > 0.0)
			{
				const double f = RLDREM_SURFACE_GAP * cell / RldRem_Sqrt(length2);

				for (k = 0; k < 3u; k++)
				{
					q[k] += f * out[k];
				}
			}
		}
		RldRem_Sub(q, &smooth[3u * v], d);
		d2 = RldRem_Dot(d, d);
		if (d2 > (limit * limit))
		{
			const double f = limit / RldRem_Sqrt(d2);

			for (k = 0; k < 3u; k++)
			{
				q[k] = smooth[3u * v + k] + f * d[k];
			}
			s_rldRemStats.clamped++;
		}
		memcpy(&target[3u * v], q, sizeof(q));
		memcpy(&pos[3u * v], q, sizeof(q));
	}

	if (g.overrun)
	{
		snprintf(why, whySize, "remesh: too complex for remesh at this resolution (group %u).", (u32)group);
		goto done;
	}

	// The guard: halve the move of the positions of bad triangles.
	for (;;)
	{
		u32 bad = 0;

		s_rldRemStats.guardPasses++;
		for (t = 0; t < keys.count; t++)
		{
			const u32 *c = &triangle[3u * t];
			double n[3];
			double n0[3];

			RldRem_Normal(&pos[3u * c[0]], &pos[3u * c[1]], &pos[3u * c[2]], n);
			RldRem_Normal(&base[3u * c[0]], &base[3u * c[1]], &base[3u * c[2]], n0);
			if ((RldRem_Dot(n, n0) > 0.0) &&
			    (RldRem_Quality2(&pos[3u * c[0]], &pos[3u * c[1]], &pos[3u * c[2]], n) >= (RLDREM_GUARD_QUALITY * RLDREM_GUARD_QUALITY)))
			{
				continue;
			}
			for (k = 0; k < 3u; k++)
			{
				if (level[c[k]] < RLDREM_GUARD_LEVELS)
				{
					mark[c[k]] = 1u;
					bad++;
				}
			}
		}
		if (bad == 0u)
		{
			break;
		}
		for (v = 0; v < vertexCount; v++)
		{
			double f;

			if (!mark[v])
			{
				continue;
			}
			mark[v] = 0u;
			if (level[v] == 0u)
			{
				s_rldRemStats.guarded++;
			}
			level[v]++;
			f = (level[v] >= RLDREM_GUARD_LEVELS) ? 0.0 : (1.0 / (double)(1u << level[v]));
			if (level[v] >= RLDREM_GUARD_LEVELS)
			{
				s_rldRemStats.dropped++;
			}
			for (k = 0; k < 3u; k++)
			{
				pos[3u * v + k] = base[3u * v + k] + f * (target[3u * v + k] - base[3u * v + k]);
			}
		}
	}
	for (v = 0; v < vertexCount; v++)
	{
		s_rldRemStats.pulled += (level[v] == 0u) ? 1u : 0u;
	}

	// 5: colors.
	for (v = 0; v < vertexCount; v++)
	{
		double q[3];
		double side[3];
		double d2;
		u32 best;

		RldRem_Sub(&base[3u * v], &pos[3u * v], side);
		best = RldRem_Nearest(&g, &pos[3u * v], side, q, &d2);
		colors[v] = 0x808080u;
		if (best != RLDREM_NONE)
		{
			const u32 source = list[best];
			u32 corner = 0;
			double bestCorner = 0.0;

			for (k = 0; k < 3u; k++)
			{
				double d[3];
				double e;

				RldRem_Sub(&in->position[3u * in->triangle[3u * source + k]], q, d);
				e = RldRem_Dot(d, d);
				if ((k == 0u) || (e < bestCorner))
				{
					bestCorner = e;
					corner = k;
				}
			}
			colors[v] = in->cornerColor[3u * source + corner] & 0xFFFFFFu;
		}
	}

	if (g.overrun)
	{
		snprintf(why, whySize, "remesh: too complex for remesh at this resolution (group %u).", (u32)group);
		goto done;
	}

	// 6: the check.
	if (!RldRem_CheckHull(pos, triangle, keys.count, why, whySize))
	{
		goto done;
	}

	// Append.
	if (((u64)out->positionCount + vertexCount > RLDREM_INPUT_MAX) || ((u64)out->triangleCount + keys.count > RLDREM_INPUT_MAX))
	{
		snprintf(why, whySize, "remesh: the hull gets more than %u triangles; lower the resolution.", RLDREM_INPUT_MAX);
		goto done;
	}
	{
		double *grownPosition = (double *)realloc(out->position, ((size_t)out->positionCount + vertexCount) * 3u * sizeof(double));
		u32 *grownTriangle;
		u32 *grownColor;
		u8 *grownGroup;

		if (grownPosition == NULL)
		{
			snprintf(why, whySize, "remesh: out of memory.");
			goto done;
		}
		out->position = grownPosition;
		grownTriangle = (u32 *)realloc(out->triangle, ((size_t)out->triangleCount + keys.count) * 3u * sizeof(u32));
		if (grownTriangle == NULL)
		{
			snprintf(why, whySize, "remesh: out of memory.");
			goto done;
		}
		out->triangle = grownTriangle;
		grownColor = (u32 *)realloc(out->cornerColor, ((size_t)out->triangleCount + keys.count) * 3u * sizeof(u32));
		if (grownColor == NULL)
		{
			snprintf(why, whySize, "remesh: out of memory.");
			goto done;
		}
		out->cornerColor = grownColor;
		grownGroup = (u8 *)realloc(out->triangleGroup, (size_t)out->triangleCount + keys.count);
		if (grownGroup == NULL)
		{
			snprintf(why, whySize, "remesh: out of memory.");
			goto done;
		}
		out->triangleGroup = grownGroup;
	}
	memcpy(&out->position[3u * out->positionCount], pos, (size_t)vertexCount * 3u * sizeof(double));
	for (t = 0; t < keys.count; t++)
	{
		for (k = 0; k < 3u; k++)
		{
			out->triangle[3u * (out->triangleCount + t) + k] = out->positionCount + triangle[3u * t + k];
			out->cornerColor[3u * (out->triangleCount + t) + k] = colors[triangle[3u * t + k]];
		}
		out->triangleGroup[out->triangleCount + t] = group;
	}
	out->positionCount += vertexCount;
	out->triangleCount += keys.count;
	ok = 1;

done:
	s_rldRemStats.work = (g.work > s_rldRemStats.work) ? g.work : s_rldRemStats.work;
	RldRem_FreeGrid(&g);
	free(keys.key);
	free(tri);
	free(vertexKey);
	free(triangle);
	free(base);
	free(smooth);
	free(target);
	free(pos);
	free(level);
	free(mark);
	free(colors);
	free(adjStart);
	free(adj);
	return ok;
}

//========================================================================================
// ENTRY POINTS
//========================================================================================

void RldRem_Free(RldRemResult *result)
{
	if (result == NULL)
	{
		return;
	}
	free(result->position);
	free(result->triangle);
	free(result->cornerColor);
	free(result->triangleGroup);
	memset(result, 0, sizeof(*result));
}

static int RldRem_CheckInput(const RldRemInput *in, char *why, size_t whySize)
{
	u32 t;
	u32 k;

	if ((in == NULL) || (in->groupCount == 0u) || (in->groupCount > RLDREM_GROUPS_MAX))
	{
		snprintf(why, whySize, "remesh: %u groups; 1 to %u are possible.", (in == NULL) ? 0u : in->groupCount, RLDREM_GROUPS_MAX);
		return 0;
	}
	if ((in->resolution < RLDREM_RESOLUTION_MIN) || (in->resolution > RLDREM_RESOLUTION_MAX))
	{
		snprintf(why, whySize, "remesh: resolution %u; %u to %u are possible.", in->resolution, RLDREM_RESOLUTION_MIN, RLDREM_RESOLUTION_MAX);
		return 0;
	}
	if ((in->positionCount > RLDREM_INPUT_MAX) || (in->triangleCount > RLDREM_INPUT_MAX))
	{
		snprintf(why, whySize, "remesh: %u positions and %u triangles; at most %u each.", in->positionCount, in->triangleCount, RLDREM_INPUT_MAX);
		return 0;
	}
	if ((in->triangleCount == 0u) || (in->position == NULL) || (in->triangle == NULL) || (in->cornerColor == NULL) || (in->triangleGroup == NULL))
	{
		snprintf(why, whySize, "remesh: no triangles, or the position, triangle, color or group array is missing.");
		return 0;
	}
	for (t = 0; t < in->positionCount; t++)
	{
		for (k = 0; k < 3u; k++)
		{
			const double x = in->position[3u * t + k];

			// Also false for NaN.
			if (!((x >= -RLDREM_COORD_MAX) && (x <= RLDREM_COORD_MAX)))
			{
				snprintf(why, whySize, "remesh: position %u, axis %u is not finite or beyond %u units.", t, k, (u32)RLDREM_COORD_MAX);
				return 0;
			}
		}
	}
	for (t = 0; t < in->triangleCount; t++)
	{
		for (k = 0; k < 3u; k++)
		{
			if (in->triangle[3u * t + k] >= in->positionCount)
			{
				snprintf(why, whySize, "remesh: triangle %u uses position %u of %u.", t, in->triangle[3u * t + k], in->positionCount);
				return 0;
			}
		}
		if (in->triangleGroup[t] >= in->groupCount)
		{
			snprintf(why, whySize, "remesh: triangle %u is in group %u of %u.", t, (u32)in->triangleGroup[t], in->groupCount);
			return 0;
		}
	}
	return 1;
}

// 1 = out holds a closed, outward, manifold hull per group that has triangles,
// groups in ascending order (free with RldRem_Free). 0 = invalid input or a
// group failed: out is empty and why (may be NULL) says which.
int RldRem_Remesh(const RldRemInput *in, RldRemResult *out, char *why, size_t whySize)
{
	char spare[4];
	u32 *list = NULL;
	u32 group;
	u32 t;

	if (why == NULL)
	{
		why = spare;
		whySize = sizeof(spare);
	}
	why[0] = '\0';
	memset(&s_rldRemStats, 0, sizeof(s_rldRemStats));
	if (out == NULL)
	{
		snprintf(why, whySize, "remesh: no result.");
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (!RldRem_CheckInput(in, why, whySize))
	{
		return 0;
	}

	list = (u32 *)malloc((size_t)in->triangleCount * sizeof(u32));
	if (list == NULL)
	{
		snprintf(why, whySize, "remesh: out of memory.");
		return 0;
	}
	for (group = 0; group < in->groupCount; group++)
	{
		u32 count = 0;

		for (t = 0; t < in->triangleCount; t++)
		{
			if (in->triangleGroup[t] == group)
			{
				list[count++] = t;
			}
		}
		if (count == 0u)
		{
			continue;
		}
		if (!RldRem_Group(in, (u8)group, list, count, out, why, whySize))
		{
			free(list);
			RldRem_Free(out);
			return 0;
		}
	}
	free(list);
	return 1;
}

// OPTIONAL, after a reduction of the hull: new corner colors from the source.
// The reducer keeps for each triangle the colors of one hull triangle it
// continues; after hundreds of hull triangles became one, that is the color of
// whichever survived - a speck of an eye can end up as the color of a cheek.
// This takes each corner's color again from the source surface instead: the
// closest point of the source triangles of the same group to a point a third
// of the way from the corner to the triangle's middle (so a triangle across a
// color border takes its colors from its own area, not from its neighbors),
// the color of that source triangle's corner nearest to the point. Equally
// close source triangles: the one facing the way the triangle faces, then the
// lower index. `source` is the input of RldRem_Remesh (its resolution sets the
// search grid); position, triangle and triangleGroup describe the mesh to
// color (for example the hull's positions with the reducer's triangles);
// cornerColor (3 per triangle) is overwritten for every triangle whose group
// has source triangles. 1 = done; 0 = invalid input or out of memory (why).
int RldRem_Recolor(const RldRemInput *source, u32 positionCount, const double *position, u32 triangleCount, const u32 *triangle, const u8 *triangleGroup,
                   u32 *cornerColor, char *why, size_t whySize)
{
	char spare[4];
	u32 *list = NULL;
	u32 *tri = NULL;
	u32 group;
	u32 t;
	u32 k;
	int ok = 1;

	if (why == NULL)
	{
		why = spare;
		whySize = sizeof(spare);
	}
	why[0] = '\0';
	if (!RldRem_CheckInput(source, why, whySize))
	{
		return 0;
	}
	if ((triangleCount > RLDREM_INPUT_MAX) || (positionCount > RLDREM_INPUT_MAX) ||
	    ((triangleCount != 0u) && ((position == NULL) || (triangle == NULL) || (triangleGroup == NULL) || (cornerColor == NULL))))
	{
		snprintf(why, whySize, "recolor: too large, or an array is missing.");
		return 0;
	}
	for (t = 0; t < (3u * triangleCount); t++)
	{
		if (triangle[t] >= positionCount)
		{
			snprintf(why, whySize, "recolor: triangle %u uses position %u of %u.", t / 3u, triangle[t], positionCount);
			return 0;
		}
	}

	list = (u32 *)malloc((size_t)source->triangleCount * sizeof(u32));
	tri = (u32 *)malloc((size_t)source->triangleCount * 3u * sizeof(u32));
	if ((list == NULL) || (tri == NULL))
	{
		free(list);
		free(tri);
		snprintf(why, whySize, "recolor: out of memory.");
		return 0;
	}

	for (group = 0; ok && (group < source->groupCount); group++)
	{
		struct RldRemGrid g;
		double lo[3];
		double hi[3];
		double longest = 0.0;
		u32 count = 0;

		for (t = 0; t < source->triangleCount; t++)
		{
			if (source->triangleGroup[t] == group)
			{
				memcpy(&tri[3u * count], &source->triangle[3u * t], 3u * sizeof(u32));
				list[count++] = t;
			}
		}
		if (count == 0u)
		{
			continue;
		}
		for (k = 0; k < 3u; k++)
		{
			lo[k] = source->position[3u * tri[0] + k];
			hi[k] = lo[k];
		}
		for (t = 0; t < (3u * count); t++)
		{
			for (k = 0; k < 3u; k++)
			{
				const double x = source->position[3u * tri[t] + k];

				lo[k] = (x < lo[k]) ? x : lo[k];
				hi[k] = (x > hi[k]) ? x : hi[k];
			}
		}
		for (k = 0; k < 3u; k++)
		{
			longest = ((hi[k] - lo[k]) > longest) ? (hi[k] - lo[k]) : longest;
		}
		if (!(longest > 0.0))
		{
			snprintf(why, whySize, "recolor: group %u has no size (all its positions are equal).", group);
			ok = 0;
			break;
		}
		if (!RldRem_BuildGrid(&g, lo, hi, longest / (double)source->resolution, RLDREM_MARGIN, source->position, count, tri, list, why, whySize))
		{
			ok = 0;
			break;
		}

		for (t = 0; t < triangleCount; t++)
		{
			const double *p[3];
			double n[3];
			double middle[3];

			if (triangleGroup[t] != group)
			{
				continue;
			}
			for (k = 0; k < 3u; k++)
			{
				p[k] = &position[3u * triangle[3u * t + k]];
			}
			RldRem_Normal(p[0], p[1], p[2], n);
			for (k = 0; k < 3u; k++)
			{
				middle[k] = (p[0][k] + p[1][k] + p[2][k]) / 3.0;
			}
			for (k = 0; k < 3u; k++)
			{
				double at[3];
				double q[3];
				double d2;
				u32 best;
				u32 a;

				for (a = 0; a < 3u; a++)
				{
					at[a] = p[k][a] + (middle[a] - p[k][a]) / 3.0;
				}
				best = RldRem_Nearest(&g, at, n, q, &d2);
				if (best != RLDREM_NONE)
				{
					const u32 s = list[best];
					u32 corner = 0;
					double near2 = 0.0;

					for (a = 0; a < 3u; a++)
					{
						double d[3];
						double e;

						RldRem_Sub(&source->position[3u * source->triangle[3u * s + a]], q, d);
						e = RldRem_Dot(d, d);
						if ((a == 0u) || (e < near2))
						{
							near2 = e;
							corner = a;
						}
					}
					cornerColor[3u * t + k] = source->cornerColor[3u * s + corner] & 0xFFFFFFu;
				}
			}
		}
		if (g.overrun)
		{
			snprintf(why, whySize, "recolor: too complex at this resolution (group %u).", group);
			ok = 0;
		}
		RldRem_FreeGrid(&g);
	}

	free(list);
	free(tri);
	return ok;
}

//========================================================================================
// SELF-TEST
//========================================================================================

struct RldRemTestMesh
{
	u32 positionCount;
	u32 positionCapacity;
	double *position;
	u32 triangleCount;
	u32 triangleCapacity;
	u32 *triangle;
	u32 *color;   // 3 per triangle
	u8 *group;
	int failed;
};

struct RldRemTest
{
	void (*report)(const char *line);
	int failures;
};

static void RldRem_TestLine(struct RldRemTest *test, int ok, const char *name, const char *format, ...)
{
	char text[320];
	char line[400];
	va_list list;

	if (ok)
	{
		snprintf(line, sizeof(line), "  ok   remesh %s", name);
	}
	else
	{
		va_start(list, format);
		vsnprintf(text, sizeof(text), format, list);
		va_end(list);
		snprintf(line, sizeof(line), "  FAIL remesh %s: %s", name, text);
		test->failures++;
	}
	if (test->report != NULL)
	{
		test->report(line);
	}
}

static void RldRem_TestFree(struct RldRemTestMesh *t)
{
	free(t->position);
	free(t->triangle);
	free(t->color);
	free(t->group);
	memset(t, 0, sizeof(*t));
}

static u32 RldRem_TestPosition(struct RldRemTestMesh *t, double x, double y, double z)
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

static void RldRem_TestTriangle(struct RldRemTestMesh *t, u32 a, u32 b, u32 c, u32 color, u8 group)
{
	u32 k;

	if (t->failed)
	{
		return;
	}
	if (t->triangleCount == t->triangleCapacity)
	{
		const u32 capacity = (t->triangleCapacity < 64u) ? 64u : (2u * t->triangleCapacity);
		u32 *grown = (u32 *)realloc(t->triangle, (size_t)capacity * 3u * sizeof(u32));
		u32 *grownColor;
		u8 *grownGroup;

		if (grown == NULL)
		{
			t->failed = 1;
			return;
		}
		t->triangle = grown;
		grownColor = (u32 *)realloc(t->color, (size_t)capacity * 3u * sizeof(u32));
		if (grownColor == NULL)
		{
			t->failed = 1;
			return;
		}
		t->color = grownColor;
		grownGroup = (u8 *)realloc(t->group, (size_t)capacity);
		if (grownGroup == NULL)
		{
			t->failed = 1;
			return;
		}
		t->group = grownGroup;
		t->triangleCapacity = capacity;
	}
	t->triangle[3u * t->triangleCount + 0u] = a;
	t->triangle[3u * t->triangleCount + 1u] = b;
	t->triangle[3u * t->triangleCount + 2u] = c;
	for (k = 0; k < 3u; k++)
	{
		t->color[3u * t->triangleCount + k] = color;
	}
	t->group[t->triangleCount] = group;
	t->triangleCount++;
}

// The box lo..hi as 12 outward triangles; the top (+y) has the color top,
// the rest the color rest.
static void RldRem_TestBox(struct RldRemTestMesh *t, const double *lo, const double *hi, u32 top, u32 rest, u8 group)
{
	// Corner c: bit 0 x, bit 1 y, bit 2 z.
	static const u8 quads[6][4] = {
		{0u, 4u, 6u, 2u}, // -x
		{1u, 3u, 7u, 5u}, // +x
		{0u, 1u, 5u, 4u}, // -y
		{2u, 6u, 7u, 3u}, // +y
		{0u, 2u, 3u, 1u}, // -z
		{4u, 5u, 7u, 6u}, // +z
	};
	u32 corner[8];
	u32 c;
	u32 f;

	for (c = 0; c < 8u; c++)
	{
		corner[c] = RldRem_TestPosition(t, (c & 1u) ? hi[0] : lo[0], (c & 2u) ? hi[1] : lo[1], (c & 4u) ? hi[2] : lo[2]);
	}
	for (f = 0; f < 6u; f++)
	{
		const u32 color = (f == 3u) ? top : rest;

		RldRem_TestTriangle(t, corner[quads[f][0]], corner[quads[f][1]], corner[quads[f][2]], color, group);
		RldRem_TestTriangle(t, corner[quads[f][0]], corner[quads[f][2]], corner[quads[f][3]], color, group);
	}
}

// A sphere of the given radius about center from `rings` rings (a power of
// two) of `segments` = 2 rings positions (the cosine and sine of the angles
// come from the rotation of a unit vector by repeated multiplication - no
// library). hole 1 leaves out the triangles around the north pole (a cap one
// ring wide), hole 2 a single triangle of the middle band.
static void RldRem_TestSphere(struct RldRemTestMesh *t, const double *center, double radius, u32 rings, u32 segments, int hole, u8 group)
{
	double ringCos[64];
	double ringSin[64];
	double segCos[64];
	double segSin[64];
	double stepCos;
	double stepSin;
	u32 north;
	u32 south;
	u32 first;
	u32 i;
	u32 j;

	if ((rings >= 64u) || (segments > 64u) || (segments != (2u * rings)) || ((rings & (rings - 1u)) != 0u))
	{
		t->failed = 1;
		return;
	}

	// Angles as unit vectors: start at (1, 0), turn by the step. The step for
	// the rings is pi / rings, for the segments 2 pi / segments, from a small
	// angle by doubling (cos 2a = 2 cos^2 a - 1, sin 2a = 2 sin a cos a).
	{
		double c = 1.0 - 0.5 * (3.14159265358979323846 / 1024.0) * (3.14159265358979323846 / 1024.0);
		double s = 3.14159265358979323846 / 1024.0;
		u32 n;

		// One more term of each series is far below what the test needs.
		s = s - s * s * s / 6.0;
		for (n = 1024u; n > rings; n /= 2u)
		{
			const double c2 = 2.0 * c * c - 1.0;
			const double s2 = 2.0 * s * c;

			c = c2;
			s = s2;
		}
		stepCos = c;
		stepSin = s;
	}
	ringCos[0] = 1.0;
	ringSin[0] = 0.0;
	for (i = 1; i <= rings; i++)
	{
		ringCos[i] = ringCos[i - 1u] * stepCos - ringSin[i - 1u] * stepSin;
		ringSin[i] = ringSin[i - 1u] * stepCos + ringCos[i - 1u] * stepSin;
	}
	// Segments: 2 pi / segments is the ring step, as segments = 2 rings.
	segCos[0] = 1.0;
	segSin[0] = 0.0;
	for (j = 1; j < segments; j++)
	{
		segCos[j] = segCos[j - 1u] * stepCos - segSin[j - 1u] * stepSin;
		segSin[j] = segSin[j - 1u] * stepCos + segCos[j - 1u] * stepSin;
	}

	north = RldRem_TestPosition(t, center[0], center[1] + radius, center[2]);
	first = t->positionCount;
	for (i = 1; i < rings; i++)
	{
		for (j = 0; j < segments; j++)
		{
			RldRem_TestPosition(t, center[0] + radius * ringSin[i] * segCos[j], center[1] + radius * ringCos[i], center[2] + radius * ringSin[i] * segSin[j]);
		}
	}
	south = RldRem_TestPosition(t, center[0], center[1] - radius, center[2]);

	for (j = 0; j < segments; j++)
	{
		const u32 j1 = (j + 1u) % segments;

		if (hole != 1)
		{
			RldRem_TestTriangle(t, north, first + j1, first + j, 0xFF0000u, group);
		}
		for (i = 1; (i + 1u) < rings; i++)
		{
			const u32 a = first + (i - 1u) * segments + j;
			const u32 b = first + (i - 1u) * segments + j1;
			const u32 c = first + i * segments + j;
			const u32 d = first + i * segments + j1;

			if ((hole != 2) || (i != (rings / 2u)) || (j != 0u))
			{
				RldRem_TestTriangle(t, a, b, d, 0x00FF00u, group);
			}
			RldRem_TestTriangle(t, a, d, c, 0x00FF00u, group);
		}
		RldRem_TestTriangle(t, south, first + (rings - 2u) * segments + j, first + (rings - 2u) * segments + j1, 0x0000FFu, group);
	}
}

static RldRemInput RldRem_TestView(const struct RldRemTestMesh *t, u32 groupCount, u32 resolution)
{
	RldRemInput in;

	in.positionCount = t->positionCount;
	in.position = t->position;
	in.triangleCount = t->triangleCount;
	in.triangle = t->triangle;
	in.cornerColor = t->color;
	in.triangleGroup = t->group;
	in.groupCount = groupCount;
	in.resolution = resolution;
	return in;
}

// The largest distance of a result position to the source triangles, by brute
// force (the test meshes are small).
static double RldRem_TestFarthest(const struct RldRemTestMesh *t, const RldRemResult *r, u32 group)
{
	double far2 = 0.0;
	u32 v;
	u32 i;

	for (i = 0; i < (3u * r->triangleCount); i++)
	{
		double best = -1.0;
		u32 s;

		if (r->triangleGroup[i / 3u] != group)
		{
			continue;
		}
		v = r->triangle[i];
		for (s = 0; s < t->triangleCount; s++)
		{
			double q[3];
			double d[3];
			double e;

			if (t->group[s] != group)
			{
				continue;
			}
			RldRem_ClosestOnTriangle(&r->position[3u * v], &t->position[3u * t->triangle[3u * s]], &t->position[3u * t->triangle[3u * s + 1u]],
			                         &t->position[3u * t->triangle[3u * s + 2u]], q);
			RldRem_Sub(&r->position[3u * v], q, d);
			e = RldRem_Dot(d, d);
			if ((best < 0.0) || (e < best))
			{
				best = e;
			}
		}
		far2 = (best > far2) ? best : far2;
	}
	return RldRem_Sqrt(far2);
}

// Checks each group of the result on its own (closed, opposite edges, no
// doubles, volume) and that no position is shared between groups.
static int RldRem_TestHulls(const RldRemResult *r, u32 groupCount, char *detail, size_t detailSize)
{
	u32 *triangle = (u32 *)malloc(((size_t)r->triangleCount + 1u) * 3u * sizeof(u32));
	u8 *owner = (u8 *)malloc((size_t)r->positionCount + 1u);
	u32 group;
	u32 t;
	u32 k;
	int ok = 1;

	detail[0] = '\0';
	if ((triangle == NULL) || (owner == NULL))
	{
		free(triangle);
		free(owner);
		snprintf(detail, detailSize, "out of memory");
		return 0;
	}
	memset(owner, 0xFF, (size_t)r->positionCount + 1u);
	for (t = 0; ok && (t < r->triangleCount); t++)
	{
		for (k = 0; k < 3u; k++)
		{
			const u32 p = r->triangle[3u * t + k];

			if (p >= r->positionCount)
			{
				snprintf(detail, detailSize, "triangle %u uses position %u of %u", t, p, r->positionCount);
				ok = 0;
			}
			else if ((owner[p] != 0xFFu) && (owner[p] != r->triangleGroup[t]))
			{
				snprintf(detail, detailSize, "position %u in groups %u and %u", p, (u32)owner[p], (u32)r->triangleGroup[t]);
				ok = 0;
			}
			else
			{
				owner[p] = r->triangleGroup[t];
			}
		}
	}
	for (group = 0; ok && (group < groupCount); group++)
	{
		u32 count = 0;

		for (t = 0; t < r->triangleCount; t++)
		{
			if (r->triangleGroup[t] == group)
			{
				memcpy(&triangle[3u * count], &r->triangle[3u * t], 3u * sizeof(u32));
				count++;
			}
		}
		if ((count != 0u) && !RldRem_CheckHull(r->position, triangle, count, detail, detailSize))
		{
			ok = 0;
		}
	}
	free(triangle);
	free(owner);
	return ok;
}

static int RldRem_TestComparePosition(const void *p, const void *q)
{
	const double *a = (const double *)p;
	const double *b = (const double *)q;
	u32 k;

	for (k = 0; k < 3u; k++)
	{
		if (a[k] != b[k])
		{
			return (a[k] < b[k]) ? -1 : 1;
		}
	}
	return 0;
}

// The number of positions that lie exactly on an earlier one.
static u32 RldRem_TestCoincident(const RldRemResult *r)
{
	double *sorted = (double *)malloc(((size_t)r->positionCount + 1u) * 3u * sizeof(double));
	u32 same = 0;
	u32 i;

	if (sorted == NULL)
	{
		return RLDREM_NONE;
	}
	memcpy(sorted, r->position, (size_t)r->positionCount * 3u * sizeof(double));
	qsort(sorted, r->positionCount, 3u * sizeof(double), RldRem_TestComparePosition);
	for (i = 1; i < r->positionCount; i++)
	{
		same += (RldRem_TestComparePosition(&sorted[3u * (i - 1u)], &sorted[3u * i]) == 0) ? 1u : 0u;
	}
	free(sorted);
	return same;
}

static double RldRem_TestVolume(const RldRemResult *r, u32 group)
{
	double v = 0.0;
	u32 t;

	for (t = 0; t < r->triangleCount; t++)
	{
		if (r->triangleGroup[t] == group)
		{
			v += RldRem_Volume6(r->position, &r->triangle[3u * t], 1u);
		}
	}
	return v / 6.0;
}

static int RldRem_TestSame(const RldRemResult *a, const RldRemResult *b)
{
	return (a->positionCount == b->positionCount) && (a->triangleCount == b->triangleCount) &&
	       (memcmp(a->position, b->position, (size_t)a->positionCount * 3u * sizeof(double)) == 0) &&
	       (memcmp(a->triangle, b->triangle, (size_t)a->triangleCount * 3u * sizeof(u32)) == 0) &&
	       (memcmp(a->cornerColor, b->cornerColor, (size_t)a->triangleCount * 3u * sizeof(u32)) == 0) &&
	       (memcmp(a->triangleGroup, b->triangleGroup, (size_t)a->triangleCount) == 0);
}

static void RldRem_SelfTestShapes(struct RldRemTest *test)
{
	static const double boxLo[3] = {0.0, 0.0, 0.0};
	static const double boxHi[3] = {10.0, 10.0, 10.0};
	static const double sphereCenter[3] = {5.0, 5.0, 5.0};
	struct RldRemTestMesh m;
	RldRemInput in;
	RldRemResult r;
	RldRemResult again;
	char detail[256];
	char why[256];
	double far;
	double volume;
	int ok;

	// A CUBE: closed, about its volume, every position close to its faces.
	memset(&m, 0, sizeof(m));
	RldRem_TestBox(&m, boxLo, boxHi, 0xFF0000u, 0x0000FFu, 0u);
	in = RldRem_TestView(&m, 1u, 32u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	if (ok)
	{
		far = RldRem_TestFarthest(&m, &r, 0u);
		volume = RldRem_TestVolume(&r, 0u);
		RldRem_TestLine(test, RldRem_TestHulls(&r, 1u, detail, sizeof(detail)) && (volume > 950.0) && (volume < 1100.0) && (far < (10.0 / 32.0)),
		                "cube 10, resolution 32: closed, volume about 1000, within a cell", "%u triangles, volume %.1f, farthest %.3f %s", r.triangleCount,
		                volume, far, detail);

		// DETERMINISM: the same again, bit for bit.
		ok = RldRem_Remesh(&in, &again, why, sizeof(why));
		RldRem_TestLine(test, ok && RldRem_TestSame(&r, &again), "cube twice: the same bits", "%s", ok ? "different" : why);
		RldRem_Free(&again);

		// COLORS: the red top stays red, the blue sides blue (away from the edges).
		{
			u32 wrong = 0;
			u32 top = 0;
			u32 rest = 0;
			u32 i;

			for (i = 0; i < (3u * r.triangleCount); i++)
			{
				const double *p = &r.position[3u * r.triangle[i]];
				const int nearEdge = (p[0] < 1.0) || (p[0] > 9.0) || (p[2] < 1.0) || (p[2] > 9.0);

				if ((p[1] > 9.9) && !nearEdge)
				{
					top++;
					wrong += (r.cornerColor[i] != 0xFF0000u) ? 1u : 0u;
				}
				else if (p[1] < 9.0)
				{
					rest++;
					wrong += (r.cornerColor[i] != 0x0000FFu) ? 1u : 0u;
				}
			}
			RldRem_TestLine(test, (wrong == 0u) && (top > 0u) && (rest > 0u), "two-colored box: each side keeps its color", "%u of %u corners wrong", wrong,
			                top + rest);

			// RECOLOR: the colors wiped, taken again from the source.
			memset(r.cornerColor, 0, (size_t)r.triangleCount * 3u * sizeof(u32));
			ok = RldRem_Recolor(&in, r.positionCount, r.position, r.triangleCount, r.triangle, r.triangleGroup, r.cornerColor, why, sizeof(why));
			wrong = 0;
			for (i = 0; ok && (i < r.triangleCount); i++)
			{
				const double *p = &r.position[3u * r.triangle[3u * i]];
				const double *q = &r.position[3u * r.triangle[3u * i + 1u]];
				const double *s = &r.position[3u * r.triangle[3u * i + 2u]];
				const double low = (p[1] < q[1]) ? ((p[1] < s[1]) ? p[1] : s[1]) : ((q[1] < s[1]) ? q[1] : s[1]);
				u32 k;

				// Triangles wholly on the top away from its edges, or wholly
				// below 9: one color each.
				const int inner = (p[0] > 1.0) && (p[0] < 9.0) && (p[2] > 1.0) && (p[2] < 9.0);

				for (k = 0; k < 3u; k++)
				{
					const u32 c = r.cornerColor[3u * i + k];

					if ((low > 9.9) && inner)
					{
						wrong += (c != 0xFF0000u) ? 1u : 0u;
					}
					else if ((p[1] < 9.0) && (q[1] < 9.0) && (s[1] < 9.0))
					{
						wrong += (c != 0x0000FFu) ? 1u : 0u;
					}
				}
			}
			RldRem_TestLine(test, ok && (wrong == 0u), "recolor of the two-colored box from the source", "%s, %u corners wrong", ok ? "done" : why, wrong);
		}
		RldRem_Free(&r);
	}
	else
	{
		RldRem_TestLine(test, 0, "cube 10, resolution 32", "%s", m.failed ? "out of memory" : why);
	}
	RldRem_TestFree(&m);

	// A SPHERE WITH A SMALL HOLE (one triangle of about 1 x 1 left out, the
	// cells 0.83 wide cannot pass it): closed and filled.
	memset(&m, 0, sizeof(m));
	RldRem_TestSphere(&m, sphereCenter, 5.0, 16u, 32u, 2, 0u);
	in = RldRem_TestView(&m, 1u, 12u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	if (ok)
	{
		far = RldRem_TestFarthest(&m, &r, 0u);
		volume = RldRem_TestVolume(&r, 0u);
		// 4/3 pi 5^3 = 523.6; the source is inscribed (a bit less).
		RldRem_TestLine(test, RldRem_TestHulls(&r, 1u, detail, sizeof(detail)) && (volume > 470.0) && (volume < 560.0) && (far < (10.0 / 12.0)),
		                "sphere with a small hole, resolution 12: closed and filled", "%u triangles, volume %.1f, farthest %.3f %s", r.triangleCount, volume,
		                far, detail);
		RldRem_Free(&r);
	}
	else
	{
		RldRem_TestLine(test, 0, "sphere with a small hole", "%s", m.failed ? "out of memory" : why);
	}
	RldRem_TestFree(&m);

	// A SPHERE WITH A LARGE HOLE (the cap around the north pole, about 2 units
	// wide, 4 cells): the flood fill alone would run in and leave a thin shell;
	// the rays keep it filled. Over the hole the hull is up to the hole's
	// radius (0.98) away from the source.
	memset(&m, 0, sizeof(m));
	RldRem_TestSphere(&m, sphereCenter, 5.0, 16u, 32u, 1, 0u);
	in = RldRem_TestView(&m, 1u, 24u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	if (ok)
	{
		far = RldRem_TestFarthest(&m, &r, 0u);
		volume = RldRem_TestVolume(&r, 0u);
		RldRem_TestLine(test, RldRem_TestHulls(&r, 1u, detail, sizeof(detail)) && (volume > 470.0) && (volume < 560.0) && (far < 0.98),
		                "sphere with a large hole, resolution 24: closed and filled", "%u triangles, volume %.1f, farthest %.3f %s", r.triangleCount, volume, far,
		                detail);
		RldRem_Free(&r);
	}
	else
	{
		RldRem_TestLine(test, 0, "sphere with a large hole", "%s", m.failed ? "out of memory" : why);
	}
	RldRem_TestFree(&m);

	// AN OPEN PLANE: a closed thin body around it.
	memset(&m, 0, sizeof(m));
	{
		const u32 a = RldRem_TestPosition(&m, 0.0, 0.0, 0.0);
		const u32 b = RldRem_TestPosition(&m, 10.0, 0.0, 0.0);
		const u32 c = RldRem_TestPosition(&m, 10.0, 0.0, 10.0);
		const u32 d = RldRem_TestPosition(&m, 0.0, 0.0, 10.0);

		RldRem_TestTriangle(&m, a, c, b, 0xFFFFFFu, 0u);
		RldRem_TestTriangle(&m, a, d, c, 0xFFFFFFu, 0u);
	}
	in = RldRem_TestView(&m, 1u, 32u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	if (ok)
	{
		far = RldRem_TestFarthest(&m, &r, 0u);
		volume = RldRem_TestVolume(&r, 0u);
		const u32 same = RldRem_TestCoincident(&r);

		// Positions where the guard kept part of the move stay up to about a
		// cell and a quarter away along the rim.
		RldRem_TestLine(test, RldRem_TestHulls(&r, 1u, detail, sizeof(detail)) && (volume > 0.0) && (far < (1.25 * 10.0 / 32.0)) && (same == 0u),
		                "open plane, resolution 32: a closed thin body, no two positions on one point", "%u triangles, volume %.3f, farthest %.3f, %u positions on another %s",
		                r.triangleCount, volume, far, same, detail);
		RldRem_Free(&r);
	}
	else
	{
		RldRem_TestLine(test, 0, "open plane", "%s", m.failed ? "out of memory" : why);
	}
	RldRem_TestFree(&m);

	// TWO GROUPS that overlap: a hull each, no position shared, each about its
	// own volume; group 1 comes first in the input.
	memset(&m, 0, sizeof(m));
	{
		static const double lo2[3] = {6.0, 6.0, 6.0};
		static const double hi2[3] = {16.0, 16.0, 16.0};

		RldRem_TestBox(&m, lo2, hi2, 0x00FF00u, 0x00FF00u, 1u);
		RldRem_TestBox(&m, boxLo, boxHi, 0xFF0000u, 0xFF0000u, 0u);
	}
	in = RldRem_TestView(&m, 2u, 24u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	if (ok)
	{
		const double v0 = RldRem_TestVolume(&r, 0u);
		const double v1 = RldRem_TestVolume(&r, 1u);
		u32 inOrder = 1;
		u32 colorOk = 1;
		u32 t;

		for (t = 0; t < r.triangleCount; t++)
		{
			inOrder &= ((t == 0u) || (r.triangleGroup[t - 1u] <= r.triangleGroup[t])) ? 1u : 0u;
			colorOk &= (r.cornerColor[3u * t] == ((r.triangleGroup[t] == 0u) ? 0xFF0000u : 0x00FF00u)) ? 1u : 0u;
		}
		RldRem_TestLine(test, RldRem_TestHulls(&r, 2u, detail, sizeof(detail)) && inOrder && colorOk && (v0 > 900.0) && (v0 < 1150.0) && (v1 > 900.0) &&
		                          (v1 < 1150.0),
		                "two overlapping groups: a hull each, in group order", "volumes %.1f %.1f, order %u, colors %u %s", v0, v1, inOrder, colorOk, detail);
		RldRem_Free(&r);
	}
	else
	{
		RldRem_TestLine(test, 0, "two overlapping groups", "%s", m.failed ? "out of memory" : why);
	}
	RldRem_TestFree(&m);
}

// A plate thinner than a cell: both faces are pulled onto it, and stay
// RLDREM_SURFACE_GAP of a cell apart on their own sides - no position on
// another, the hull still closed. Long splinters through the same cells:
// the work stays small, and with a tiny budget the group is refused for that.
static void RldRem_SelfTestThin(struct RldRemTest *test)
{
	static const double plateLo[3] = {0.0, 0.0, 0.0};
	static const double plateHi[3] = {10.0, 0.05, 10.0};
	struct RldRemTestMesh m;
	RldRemInput in;
	RldRemResult r;
	char detail[256];
	char why[256];
	u32 i;
	int ok;

	memset(&m, 0, sizeof(m));
	RldRem_TestBox(&m, plateLo, plateHi, 0xFF0000u, 0x0000FFu, 0u);
	in = RldRem_TestView(&m, 1u, 32u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	if (ok)
	{
		const u32 same = RldRem_TestCoincident(&r);
		u32 above = 0;
		u32 below = 0;
		u32 wrongSide = 0;

		// Away from the rim each position lies above the top or below the bottom.
		for (i = 0; i < r.positionCount; i++)
		{
			const double *p = &r.position[3u * i];

			if ((p[0] > 1.0) && (p[0] < 9.0) && (p[2] > 1.0) && (p[2] < 9.0))
			{
				above += (p[1] > 0.05) ? 1u : 0u;
				below += (p[1] < 0.0) ? 1u : 0u;
				wrongSide += ((p[1] >= 0.0) && (p[1] <= 0.05)) ? 1u : 0u;
			}
		}
		RldRem_TestLine(test, RldRem_TestHulls(&r, 1u, detail, sizeof(detail)) && (same == 0u) && (wrongSide == 0u) && (above > 0u) && (below > 0u),
		                "plate 0.05 thick, resolution 32 (cells 0.31): faces apart, no two positions on one point",
		                "%u positions on another, %u inside the plate, %u above, %u below %s", same, wrongSide, above, below, detail);
		RldRem_Free(&r);
	}
	else
	{
		RldRem_TestLine(test, 0, "thin plate", "%s", m.failed ? "out of memory" : why);
	}
	RldRem_TestFree(&m);

	// 2000 splinters fanned out from one edge of a 10-unit box to its far side.
	memset(&m, 0, sizeof(m));
	for (i = 0; i < 2000u; i++)
	{
		const double f = (double)i / 2000.0;
		const u32 a = RldRem_TestPosition(&m, 0.001 * f, 0.0, 0.0);
		const u32 b = RldRem_TestPosition(&m, 10.0, 10.0, 10.0 - 10.0 * f);
		const u32 c = RldRem_TestPosition(&m, 10.0, 10.0 - 0.001, 10.0 - 10.0 * f + 0.002);

		RldRem_TestTriangle(&m, a, b, c, 0x808080u, 0u);
	}
	in = RldRem_TestView(&m, 1u, 32u);
	ok = !m.failed && RldRem_Remesh(&in, &r, why, sizeof(why));
	RldRem_TestLine(test, ok && RldRem_TestHulls(&r, 1u, detail, sizeof(detail)) && (s_rldRemStats.work < (RLDREM_WORK_MAX / 10u)),
	                "2000 splinters through the same cells, resolution 32: closed, below a tenth of the work budget", "%s, work %u", ok ? detail : why,
	                (u32)s_rldRemStats.work);
	if (ok)
	{
		RldRem_Free(&r);
	}
	s_rldRemWorkMax = 100000u;
	ok = !m.failed && !RldRem_Remesh(&in, &r, why, sizeof(why)) && (strstr(why, "too complex") != NULL) && (r.triangleCount == 0u);
	s_rldRemWorkMax = RLDREM_WORK_MAX;
	RldRem_TestLine(test, ok, "splinters with a budget of 100000 steps: refused as too complex", "%s", why);
	RldRem_TestFree(&m);
}

static void RldRem_SelfTestLimits(struct RldRemTest *test)
{
	static const double boxLo[3] = {0.0, 0.0, 0.0};
	static const double boxHi[3] = {10.0, 10.0, 10.0};
	struct RldRemTestMesh m;
	RldRemInput in;
	RldRemResult r;
	char why[256];
	u32 failedAt = 0;
	int ok = 1;

	memset(&m, 0, sizeof(m));
	RldRem_TestBox(&m, boxLo, boxHi, 0u, 0u, 0u);
	if (m.failed)
	{
		RldRem_TestLine(test, 0, "invalid input", "out of memory");
		RldRem_TestFree(&m);
		return;
	}

	in = RldRem_TestView(&m, 1u, 4u);
	ok &= !RldRem_Remesh(&in, &r, why, sizeof(why)) && (strstr(why, "resolution") != NULL) && (r.triangleCount == 0u);
	failedAt = (!ok && (failedAt == 0u)) ? 1u : failedAt;
	in = RldRem_TestView(&m, 9u, 32u);
	ok &= !RldRem_Remesh(&in, &r, why, sizeof(why)) && (strstr(why, "groups") != NULL);
	failedAt = (!ok && (failedAt == 0u)) ? 2u : failedAt;
	{
		const u32 kept = m.triangle[5];
		u32 i;

		m.triangle[5] = 99u;
		in = RldRem_TestView(&m, 1u, 32u);
		ok &= !RldRem_Remesh(&in, &r, why, sizeof(why)) && (strstr(why, "uses position") != NULL);
		failedAt = (!ok && (failedAt == 0u)) ? 3u : failedAt;
		m.triangle[5] = kept;
		m.group[3] = 1u;
		ok &= !RldRem_Remesh(&in, &r, why, sizeof(why)) && (strstr(why, "group 1 of 1") != NULL);
		failedAt = (!ok && (failedAt == 0u)) ? 4u : failedAt;
		m.group[3] = 0u;
		for (i = 0; i < (3u * m.positionCount); i++)
		{
			m.position[i] = 1.0;
		}
	}
	ok &= !RldRem_Remesh(&in, &r, why, sizeof(why)) && (strstr(why, "no size") != NULL);
	failedAt = (!ok && (failedAt == 0u)) ? 5u : failedAt;

	RldRem_TestLine(test, ok, "invalid input: each fault refused for its reason", "step %u failed", failedAt);
	RldRem_TestFree(&m);
}

// The number of failed cases; 0 = passed. Each case reports one line (no
// line break at the end) through report, which may be NULL.
int RldRem_SelfTest(void (*report)(const char *line))
{
	struct RldRemTest test;

	test.report = report;
	test.failures = 0;
	RldRem_SelfTestShapes(&test);
	RldRem_SelfTestThin(&test);
	RldRem_SelfTestLimits(&test);
	return test.failures;
}
