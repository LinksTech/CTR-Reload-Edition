// rldtread.inc - the tread count of an author's wheel, from its geometry
//
// AN INCLUDE, NOT A MODULE, after include/rldchar.inc (RldChar_F32,
// Rld_ReadLE16, RLDCHAR_WHEEL_*). tools/rldpack.c includes it: make-char
// works out the tread count of each wheel mesh once and writes it into WHLS
// version 3 (include/rldchar.inc, CNET-10); the game reads that number and
// never guesses one itself (platform/native_wheels.c, THE STROBOSCOPE).
// Every function is static; nothing here allocates, prints or reads a clock,
// and no C library maths is used: sums, products, divisions and a square
// root by halving, so every machine with the same floating point (x86 with
// SSE2: MSVC Win32, gcc and clang on x86-64) finds the same count. A
// compiler that fuses a * b - c * d into one step (FMA, the default of gcc
// and clang for ARM64) may decide a case at a limit (half a degree, 3 %,
// 0.2 %) the other way - there build rldpack with -ffp-contract=off.
//
// WHAT THE COUNT IS FOR. At top speed a wheel turns by more than half a tread
// pitch per tick and its tread seems to stand or to turn backwards. The game
// clamps the drawn roll to 0.45 of a pitch 2 pi / treads. A count smaller
// than the number of times anything of the wheel visibly repeats would let
// that part run backwards; a larger one only turns the wheel more slowly. So
// a count is given only when the geometry leaves no doubt; else none (0), and
// the game takes the clamp that is safe for every count up to
// RLDCHAR_WHEEL_TREADS_MAX.
//
// THE RULES, on the mesh as it is stored (axle along X):
// 0. The axle: the mean of the distinct vertex positions in y and z (closer
//    than RLDTREAD_SAME of the extent is the same position). A mesh
//    that repeats round its axle has its mean there, also where make-char
//    centred it on its box and the box is not the axle (an odd count of
//    lugs); a mesh that does not repeat fails rule 4 or 5 about any point.
// 1. The outer band: the points whose distance from the axle is at least
//    1 - RLDTREAD_BAND of the largest. Their directions about the axle,
//    sorted round the wheel and merged within half a degree, are the outer
//    angles (at least 4, else no count: "no tread at full radius").
// 2. The gaps between neighbouring outer angles: covered when an edge of a
//    triangle joins outer points of the two angles (the tyre runs on at full
//    radius - a lug top, a smooth tyre), open when none does (a groove).
//    No open gap: a smooth tyre, no count (a tread only painted in the
//    texture is not seen).
// 3. The candidate n is the number of open gaps. It must lie in
//    RLDCHAR_WHEEL_TREADS_MIN..MAX (4..32), and the open gaps must recur
//    evenly: the number of outer angles is a multiple of n and the
//    covered/open pattern repeats after every outer angles / n.
// 4. The outer band is n-fold symmetric: every outer point turned by 2 pi / n
//    about the axle lies within RLDTREAD_MATCH of the largest radius of an
//    outer point (x included).
// 5. Nothing of the wheel repeats more often, nor out of step with n. The
//    points off the axle fall into rings (the same distance from the axle
//    and the same x, within RLDTREAD_MATCH); a ring is smooth when an edge
//    joins every two neighbouring directions of it (a turned profile, a
//    smooth rim: the same however it is turned) and patterned otherwise
//    (lug tops, groove floors, spoke ends, bolts). Every patterned ring -
//    the band's own as well - must be n-fold symmetric (else its features
//    repeat out of step with n: 12 spokes on 10 lugs) and not symmetric
//    under a finer turn 2 pi / (k n), k >= 2: half the turn, and every
//    k n that divides the number of its directions (8 spokes on 4 lugs,
//    lugs whose tops fill half the pitch, ribs joined in pairs).
// Then every patterned ring repeats exactly n times round the wheel, so no
// geometric feature at full radius or below moves by half its pitch or more
// in a tick. Anything that fails a rule gets no count, and the reason
// (RldTread_Why). Not looked at: the texture (a painted pattern), and a
// smooth ring's own facets (a coarse polygon of a turned rim).

#ifndef RLDTREAD_INC
#define RLDTREAD_INC

#define RLDTREAD_BAND 0.03                        // the outer band: within 3 % of the largest radius
#define RLDTREAD_MERGE_COS2 0.99992384757819563   // cos^2 of half a degree: directions closer than that are one
#define RLDTREAD_MATCH 0.002                      // a turned point matches within 0.2 % of the largest radius
#define RLDTREAD_SAME 1e-5                         // rule 0: points within this part of the extent are one
#define RLDTREAD_POINTS_MAX RLDCHAR_WHEEL_VERTICES_MAX

enum RldTreadWhy
{
	RLDTREAD_FOUND,       // the count, by rules 0 to 5
	RLDTREAD_NO_MESH,     // no point off the axle
	RLDTREAD_NO_BAND,     // fewer than 4 outer angles
	RLDTREAD_SMOOTH,      // no open gap
	RLDTREAD_FEW,         // fewer than 4 open gaps
	RLDTREAD_MANY,        // more than 32 open gaps
	RLDTREAD_UNEVEN,      // the open gaps do not recur evenly
	RLDTREAD_ASYMMETRIC,  // the band is not n-fold symmetric
	RLDTREAD_OUT_OF_STEP, // a patterned ring is not n-fold symmetric (rim, spokes)
	RLDTREAD_FINER        // a patterned ring repeats more often than n
};

struct RldTreadResult
{
	u32 treads;   // RLDCHAR_WHEEL_TREADS_MIN..MAX, 0 = not determined
	int why;      // enum RldTreadWhy
	u32 outer;    // points in the outer band
	u32 angles;   // outer angles after merging
	u32 open;     // open gaps between them
	u32 rings;    // patterned rings checked by rule 5 (0 before it)
};

// Scratch of RldTread_Count, of the caller (static there): no allocation here.
struct RldTreadWork
{
	double p[RLDTREAD_POINTS_MAX][3]; // the points of the set looked at, about the axle
	double key[RLDTREAD_POINTS_MAX];  // their place round the wheel
	u32 order[RLDTREAD_POINTS_MAX];   // the set sorted by key
	u32 slotOf[RLDTREAD_POINTS_MAX];  // point of the set -> its direction
	u32 localOf[RLDTREAD_POINTS_MAX]; // vertex -> point of the set, or RLDTREAD_NONE
	u32 first[RLDTREAD_POINTS_MAX];   // direction -> its first point
	u32 ringOf[RLDTREAD_POINTS_MAX];  // vertex -> its ring (rule 5), RLDTREAD_NONE on the axle
	double ring[RLDTREAD_POINTS_MAX][2]; // ring -> its radius and x
	u8 covered[RLDTREAD_POINTS_MAX];
};
#define RLDTREAD_NONE 0xFFFFFFFFu


// cos and sin of 2 pi / n for n = 4..32, constants (no libm at run time).
static const double s_rldTreadTurn[RLDCHAR_WHEEL_TREADS_MAX + 1 - RLDCHAR_WHEEL_TREADS_MIN][2] = {
	{0.0, 1.0},                                       // 4
	{0.30901699437494745, 0.95105651629515353},       // 5
	{0.5, 0.8660254037844386},                        // 6
	{0.62348980185873348, 0.7818314824680298},        // 7
	{0.70710678118654757, 0.70710678118654757},       // 8
	{0.76604444311897801, 0.64278760968653936},       // 9
	{0.80901699437494745, 0.58778525229247314},       // 10
	{0.84125353283118121, 0.54064081745559756},       // 11
	{0.8660254037844386, 0.5},                        // 12
	{0.88545602565320991, 0.46472317204376856},       // 13
	{0.90096886790241915, 0.43388373911755812},       // 14
	{0.91354545764260087, 0.40673664307580021},       // 15
	{0.92387953251128674, 0.38268343236508978},       // 16
	{0.93247222940435581, 0.36124166618715292},       // 17
	{0.93969262078590843, 0.34202014332566871},       // 18
	{0.94581724170063464, 0.32469946920468351},       // 19
	{0.95105651629515353, 0.30901699437494745},       // 20
	{0.95557280578614068, 0.29475517441090421},       // 21
	{0.95949297361449737, 0.28173255684142967},       // 22
	{0.96291728734779924, 0.26979677115702427},       // 23
	{0.96592582628906831, 0.25881904510252074},       // 24
	{0.96858316112863108, 0.24868988716485479},       // 25
	{0.97094181742605201, 0.23931566428755777},       // 26
	{0.97304487057982381, 0.23061587074244017},       // 27
	{0.97492791218182362, 0.22252093395631439},       // 28
	{0.97662055571008666, 0.21497044021102407},       // 29
	{0.97814760073380569, 0.20791169081775934},       // 30
	{0.97952994125249448, 0.20129852008866009},       // 31
	{0.98078528040323043, 0.19509032201612828},       // 32
};

static double RldTread_Abs(double v)
{
	return (v < 0.0) ? -v : v;
}

// The square root of v >= 0 by halving (no libm): to the last bit.
static double RldTread_Root(double v)
{
	double lo = 0.0;
	double hi = (v > 1.0) ? v : 1.0;
	int k;

	for (k = 0; k < 200; k++)
	{
		const double mid = 0.5 * (lo + hi);

		if ((mid * mid) > v)
		{
			hi = mid;
		}
		else
		{
			lo = mid;
		}
	}
	return lo;
}

// The place of direction (y, z) round the axle, 0..4, rising with the angle
// atan2(z, y) from 0 to 2 pi - without trigonometry.
static double RldTread_Key(double y, double z)
{
	const double sum = RldTread_Abs(y) + RldTread_Abs(z);
	const double p = (sum > 0.0) ? (z / sum) : 0.0;

	if (y >= 0.0)
	{
		return (z >= 0.0) ? p : (4.0 + p);
	}
	return 2.0 - p;
}

// 1 = directions a and b (y, z) lie within half a degree of each other.
static int RldTread_Close(const double *a, const double *b)
{
	const double dot = (a[1] * b[1]) + (a[2] * b[2]);
	const double aa = (a[1] * a[1]) + (a[2] * a[2]);
	const double bb = (b[1] * b[1]) + (b[2] * b[2]);

	return (dot > 0.0) && ((dot * dot) >= (RLDTREAD_MERGE_COS2 * aa * bb));
}

// Insertion sort of the set by key (ties keep their order): at most 2048
// points, a few times per wheel.
static void RldTread_Sort(struct RldTreadWork *w, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++)
	{
		w->order[i] = i;
	}
	for (i = 1; i < count; i++)
	{
		const u32 at = w->order[i];
		u32 j = i;

		while ((j > 0u) && (w->key[w->order[j - 1u]] > w->key[at]))
		{
			w->order[j] = w->order[j - 1u];
			j--;
		}
		w->order[j] = at;
	}
}

// 1 = every point of the set turned by (c, s) about the axle lies within tol
// of one of the set.
static int RldTread_Symmetric(const struct RldTreadWork *w, u32 count, double c, double s, double tol)
{
	const double tol2 = tol * tol;
	u32 i;

	for (i = 0; i < count; i++)
	{
		const double *p = w->p[i];
		const double q[3] = {p[0], (p[1] * c) - (p[2] * s), (p[1] * s) + (p[2] * c)};
		int found = 0;
		u32 j;

		for (j = 0; (j < count) && !found; j++)
		{
			const double dx = q[0] - w->p[j][0];
			const double dy = q[1] - w->p[j][1];
			const double dz = q[2] - w->p[j][2];

			found = (((dx * dx) + (dy * dy) + (dz * dz)) <= tol2);
		}
		if (!found)
		{
			return 0;
		}
	}
	return 1;
}

// Vertex i of the mesh: f32 x, y, z.
static void RldTread_Vertex(const u8 *vertices, u32 i, double out[3])
{
	const u8 *v = &vertices[(size_t)i * RLDCHAR_WHEEL_VERTEX_BYTES];

	out[0] = RldChar_F32(&v[0]);
	out[1] = RldChar_F32(&v[4]);
	out[2] = RldChar_F32(&v[8]);
}

// The set: every vertex whose group[] is id, about the axle, into w->p
// (w->localOf of the others RLDTREAD_NONE); sorted round the wheel and
// merged into directions (w->slotOf, w->first); then the gaps between
// neighbouring directions (w->covered) from the edges joining two points of
// the set. Returns the number of points; *angles and *open the directions
// and the open gaps.
static u32 RldTread_Set(const u8 *vertices, u32 vertexCount, const u8 *indices, u32 triangleCount, const double axle[2], const u32 *group, u32 id,
                        struct RldTreadWork *w, u32 *anglesOut, u32 *openOut)
{
	u32 count = 0;
	u32 angles = 0;
	u32 open = 0;
	u32 i;

	for (i = 0; i < vertexCount; i++)
	{
		double p[3];

		w->localOf[i] = RLDTREAD_NONE;
		if (group[i] != id)
		{
			continue;
		}
		RldTread_Vertex(vertices, i, p);
		w->p[count][0] = p[0];
		w->p[count][1] = p[1] - axle[0];
		w->p[count][2] = p[2] - axle[1];
		w->key[count] = RldTread_Key(w->p[count][1], w->p[count][2]);
		w->localOf[i] = count;
		count++;
	}
	RldTread_Sort(w, count);
	for (i = 0; i < count; i++)
	{
		const u32 at = w->order[i];

		if ((angles == 0u) || !RldTread_Close(w->p[w->first[angles - 1u]], w->p[at]))
		{
			w->first[angles++] = at;
		}
		w->slotOf[at] = angles - 1u;
	}
	// Across 2 pi: the last directions merge into the first.
	while ((angles > 1u) && RldTread_Close(w->p[w->first[0]], w->p[w->first[angles - 1u]]))
	{
		for (i = 0; i < count; i++)
		{
			w->slotOf[i] = (w->slotOf[i] == (angles - 1u)) ? 0u : w->slotOf[i];
		}
		angles--;
	}

	memset(w->covered, 0, (angles != 0u) ? angles : 1u);
	for (i = 0; (angles > 1u) && (i < triangleCount); i++)
	{
		int e;

		for (e = 0; e < 3; e++)
		{
			const u32 a = Rld_ReadLE16(&indices[((size_t)i * 6u) + (2u * (u32)e)]);
			const u32 b = Rld_ReadLE16(&indices[((size_t)i * 6u) + (2u * (u32)((e + 1) % 3))]);
			u32 sa;
			u32 sb;

			if ((a >= vertexCount) || (b >= vertexCount) || (w->localOf[a] == RLDTREAD_NONE) || (w->localOf[b] == RLDTREAD_NONE))
			{
				continue;
			}
			sa = w->slotOf[w->localOf[a]];
			sb = w->slotOf[w->localOf[b]];
			if (sb == ((sa + 1u) % angles))
			{
				w->covered[sa] = 1u;
			}
			if (sa == ((sb + 1u) % angles))
			{
				w->covered[sb] = 1u;
			}
		}
	}
	for (i = 0; i < angles; i++)
	{
		open += w->covered[i] ? 0u : 1u;
	}
	*anglesOut = angles;
	*openOut = open;
	return count;
}

// 1 = the set (RldTread_Set) is also symmetric under a finer turn than
// 2 pi / n: half of it (cos and sin of the half angle from the table), and
// 2 pi / (k n) for every k >= 2 whose k n divides the directions (the turn
// from the mesh: from direction 1 to direction 1 + angles / (k n)).
static int RldTread_Finer(const struct RldTreadWork *w, u32 count, u32 angles, u32 n, double tol)
{
	const double c = s_rldTreadTurn[n - RLDCHAR_WHEEL_TREADS_MIN][0];
	const double s = s_rldTreadTurn[n - RLDCHAR_WHEEL_TREADS_MIN][1];
	const double ch = RldTread_Root(0.5 * (1.0 + c));
	u32 k;

	if (RldTread_Symmetric(w, count, ch, s / (2.0 * ch), tol))
	{
		return 1;
	}
	for (k = 2u; (angles >= 3u) && ((k * n) <= angles); k++)
	{
		const u32 m = k * n;
		const double *a;
		const double *b;
		double length;

		if ((angles % m) != 0u)
		{
			continue;
		}
		a = w->p[w->first[1]];
		b = w->p[w->first[1u + (angles / m)]];
		length = RldTread_Root(((a[1] * a[1]) + (a[2] * a[2])) * ((b[1] * b[1]) + (b[2] * b[2])));
		if ((length > 0.0) &&
		    RldTread_Symmetric(w, count, ((a[1] * b[1]) + (a[2] * b[2])) / length, ((a[1] * b[2]) - (a[2] * b[1])) / length, tol))
		{
			return 1;
		}
	}
	return 0;
}

// The tread count of one wheel mesh in the WHLS layout: vertexCount records
// of RLDCHAR_WHEEL_VERTEX_BYTES (f32 x, y, z first), triangleCount x 3 u16
// indices (an index past the vertices is skipped). out gets the count (0 =
// none) and why.
static void RldTread_Count(const u8 *vertices, u32 vertexCount, const u8 *indices, u32 triangleCount, struct RldTreadWork *w, struct RldTreadResult *out)
{
	double axle[2] = {0.0, 0.0};
	double extent = 0.0;
	double rmax2 = 0.0;
	double band2;
	double tol;
	u32 distinct = 0;
	u32 outer;
	u32 angles = 0;
	u32 open = 0;
	u32 rings = 0;
	u32 n;
	u32 i;

	memset(out, 0, sizeof(*out));
	out->why = RLDTREAD_NO_MESH;
	if (vertexCount > RLDTREAD_POINTS_MAX)
	{
		vertexCount = RLDTREAD_POINTS_MAX;
	}

	// Rule 0: the axle, the mean of the distinct positions (a vertex split for
	// its UV or its normal counts once, and so do two within
	// RLDTREAD_SAME of the extent - the same point, but for rounding).
	for (i = 0; i < vertexCount; i++)
	{
		double p[3];

		RldTread_Vertex(vertices, i, p);
		extent = (RldTread_Abs(p[1]) > extent) ? RldTread_Abs(p[1]) : extent;
		extent = (RldTread_Abs(p[2]) > extent) ? RldTread_Abs(p[2]) : extent;
	}
	for (i = 0; i < vertexCount; i++)
	{
		const double same = RLDTREAD_SAME * extent;
		double p[3];
		int seen = 0;
		u32 j;

		RldTread_Vertex(vertices, i, p);
		for (j = 0; (j < i) && !seen; j++)
		{
			double q[3];

			RldTread_Vertex(vertices, j, q);
			seen = (RldTread_Abs(q[0] - p[0]) <= same) && (RldTread_Abs(q[1] - p[1]) <= same) && (RldTread_Abs(q[2] - p[2]) <= same);
		}
		if (!seen)
		{
			axle[0] += p[1];
			axle[1] += p[2];
			distinct++;
		}
	}
	if (distinct == 0u)
	{
		return;
	}
	axle[0] /= (double)distinct;
	axle[1] /= (double)distinct;

	for (i = 0; i < vertexCount; i++)
	{
		double p[3];
		double r2;

		RldTread_Vertex(vertices, i, p);
		r2 = ((p[1] - axle[0]) * (p[1] - axle[0])) + ((p[2] - axle[1]) * (p[2] - axle[1]));
		rmax2 = (r2 > rmax2) ? r2 : rmax2;
	}
	if (!(rmax2 > 1e-12))
	{
		return;
	}
	tol = RLDTREAD_MATCH * RldTread_Root(rmax2);

	// Rules 1 and 2: the outer band (group 1 of ringOf for now), its
	// directions and gaps.
	band2 = rmax2 * (1.0 - RLDTREAD_BAND) * (1.0 - RLDTREAD_BAND);
	for (i = 0; i < vertexCount; i++)
	{
		double p[3];

		RldTread_Vertex(vertices, i, p);
		p[1] -= axle[0];
		p[2] -= axle[1];
		w->ringOf[i] = (((p[1] * p[1]) + (p[2] * p[2])) >= band2) ? 1u : 0u;
	}
	outer = RldTread_Set(vertices, vertexCount, indices, triangleCount, axle, w->ringOf, 1u, w, &angles, &open);
	out->outer = outer;
	out->angles = angles;
	out->open = open;
	if (angles < RLDCHAR_WHEEL_TREADS_MIN)
	{
		out->why = RLDTREAD_NO_BAND;
		return;
	}
	if (open == 0u)
	{
		out->why = RLDTREAD_SMOOTH;
		return;
	}

	// Rule 3: n open gaps, 4..32, recurring evenly.
	n = open;
	if (n < RLDCHAR_WHEEL_TREADS_MIN)
	{
		out->why = RLDTREAD_FEW;
		return;
	}
	if (n > RLDCHAR_WHEEL_TREADS_MAX)
	{
		out->why = RLDTREAD_MANY;
		return;
	}
	if ((angles % n) != 0u)
	{
		out->why = RLDTREAD_UNEVEN;
		return;
	}
	for (i = 0; i < angles; i++)
	{
		if (w->covered[i] != w->covered[(i + (angles / n)) % angles])
		{
			out->why = RLDTREAD_UNEVEN;
			return;
		}
	}

	// Rule 4: the band n-fold symmetric.
	if (!RldTread_Symmetric(w, outer, s_rldTreadTurn[n - RLDCHAR_WHEEL_TREADS_MIN][0], s_rldTreadTurn[n - RLDCHAR_WHEEL_TREADS_MIN][1], tol))
	{
		out->why = RLDTREAD_ASYMMETRIC;
		return;
	}

	// Rule 5: the rings of the points off the axle - the same radius and x
	// within tol; then each patterned ring n-fold and not finer.
	{
		u32 ringCount = 0;
		u32 r;

		for (i = 0; i < vertexCount; i++)
		{
			double p[3];
			double r2;
			double radius;

			RldTread_Vertex(vertices, i, p);
			r2 = ((p[1] - axle[0]) * (p[1] - axle[0])) + ((p[2] - axle[1]) * (p[2] - axle[1]));
			w->ringOf[i] = RLDTREAD_NONE;
			if (r2 <= (tol * tol))
			{
				continue;   // on the axle: the same however it is turned
			}
			radius = RldTread_Root(r2);
			for (r = 0; r < ringCount; r++)
			{
				if ((RldTread_Abs(radius - w->ring[r][0]) <= tol) && (RldTread_Abs(p[0] - w->ring[r][1]) <= tol))
				{
					break;
				}
			}
			if (r == ringCount)
			{
				w->ring[ringCount][0] = radius;
				w->ring[ringCount][1] = p[0];
				ringCount++;
			}
			w->ringOf[i] = r;
		}
		for (r = 0; r < ringCount; r++)
		{
			u32 ringAngles = 0;
			u32 ringOpen = 0;
			const u32 count = RldTread_Set(vertices, vertexCount, indices, triangleCount, axle, w->ringOf, r, w, &ringAngles, &ringOpen);

			if ((ringOpen == 0u) && (ringAngles > 1u))
			{
				continue;   // smooth: an edge joins every two neighbouring directions
			}
			rings++;
			out->rings = rings;
			if (!RldTread_Symmetric(w, count, s_rldTreadTurn[n - RLDCHAR_WHEEL_TREADS_MIN][0], s_rldTreadTurn[n - RLDCHAR_WHEEL_TREADS_MIN][1], tol))
			{
				out->why = RLDTREAD_OUT_OF_STEP;
				return;
			}
			if (RldTread_Finer(w, count, ringAngles, n, tol))
			{
				out->why = RLDTREAD_FINER;
				return;
			}
		}
	}

	out->treads = n;
	out->why = RLDTREAD_FOUND;
}

// The reason of a result in words, for the messages of make-char.
static const char *RldTread_Why(int why)
{
	switch (why)
	{
	case RLDTREAD_FOUND:
		return "the grooves of the tread repeat evenly round the wheel";
	case RLDTREAD_NO_MESH:
		return "the wheel has no point off its axle";
	case RLDTREAD_NO_BAND:
		return "fewer than 4 directions reach the full radius";
	case RLDTREAD_SMOOTH:
		return "the tyre is smooth at full radius, no groove between lugs";
	case RLDTREAD_FEW:
		return "fewer than 4 grooves";
	case RLDTREAD_MANY:
		return "more than 32 grooves";
	case RLDTREAD_UNEVEN:
		return "the grooves are not evenly spaced";
	case RLDTREAD_ASYMMETRIC:
		return "the lugs are not all alike round the wheel";
	case RLDTREAD_OUT_OF_STEP:
		return "a part of the wheel (rim, spokes) repeats out of step with the lugs";
	case RLDTREAD_FINER:
		return "a part of the wheel repeats more often than the lugs";
	default:
		return "unknown";
	}
}

#endif
