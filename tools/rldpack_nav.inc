// tools/rldpack_nav.inc - included by tools/rldpack.c (make only)
//
// THE START GRID AND THE NAV PATHS OF AN EXPORTED TRACK
//
// Many exported tracks carry restart points but no nav paths, and their eight
// start spots sit on one point. One of them puts all eight into a closed hall
// 13,000 units from the finish line: the player starts there, never reaches
// the road and never gets a lap; without a nav path the game creates no bot
// at all (BOTS_Driver_Init, BOTS.c:3105-3131). The LEV still says where the
// road runs - its restart ring, the checkpoints the game counts laps with -,
// and that is enough to make both:
//
//   start grid  when fewer than 8 of the start spots are distinct (the test
//               of the start-spots warning): two rows of four behind restart
//               point 0, facing restart point 1, each spot on the ground.
//   nav paths   when no path has more than one point: three paths along the
//               restart ring, one in the middle and one on each side.
//
// A LEV that has them keeps them, byte for byte. --keep-start-spots and
// --keep-nav leave them as exported. Where the ring does not give a safe
// result, nothing is written and the report says why.
//
// WHAT A NAV FRAME HOLDS, measured on the disc (Sunset Vista, 2310 frames):
//   - pos on the ground: in 128 of 133 samples the frame's y is the ground's
//     y exactly. A bot on its path takes pos as it is (BOTS.c:2005-2008), so
//     a frame in the air is a bot in the air.
//   - rot[1] the heading to the next frame, atan2(dx, dz) in 1/4096 turns,
//     rounded down to 1/256; rot[0] the pitch, negative uphill; rot[3] is
//     -rot[0] in all 2307 frames; rot[2] the roll, small - written as 0.
//   - distToNextNavXYZ / XZ: the distance to the next frame, 3D and flat.
//   - flags (drift, turbo, jump) 0: the generated line knows no tricks.
//   - pathChangeOpcode 0x0C00 in every frame (no change of path).
//   - goBackCount: the restart point whose stretch the frame lies in. The
//     game takes it as the bot's checkpoint (BOTS.c:2007) and counts its
//     laps with it (VehLap.c:69); a frame past restart point i carries i.
//   - frames about 240 apart (227..240 on the straights).
// The path runs once around and closes on itself: after the last frame the
// bot takes the first again (BOTS.c:2991-2995).
//
// Every number here is computed with + - * / only, like make-char (see
// CTR_RLDPACK_FP_OPTIONS in CMakeLists.txt): the square root by Newton steps
// and the angle by a fixed polynomial, not by the C library, so a container
// comes out byte for byte the same on every machine.

#define RLD_LEV_PTR_MESH      0x00u  // Level.ptr_mesh_info
#define RLD_LEV_PTR_RESTART   0x14cu // Level.ptr_restart_points
#define RLD_MESH_BYTES        0x20u
#define RLD_QUAD_BYTES        0x5cu
#define RLD_QUAD_FLAGS        0x12u
#define RLD_VERTEX_BYTES      0x10u
#define RLD_RESTART_BYTES     12u
#define RLD_NAV_HEADER_BYTES  0x4cu
#define RLD_NAV_FRAME_BYTES   0x14u

#define RLD_QUAD_GROUND       0x1000u
#define RLD_QUAD_KILL_PLANE   0x0200u
#define RLD_QUAD_TRIGGER      0x0040u
#define RLD_QUAD_NO_RESPONSE  0x0010u

#define RLD_NAV_STEP          240.0  // spacing of the frames, as on the disc
#define RLD_NAV_DENSE         32     // curve points per stretch, for the length
#define RLD_NAV_SNAP          384.0  // a ground counts this close to the line's height
#define RLD_NAV_SIDE_STEP     32     // the width is felt in steps of 32 ...
#define RLD_NAV_SIDE_MAX      640    // ... up to 640 to each side
#define RLD_NAV_SIDE_RISE     96.0   // a step higher or lower than this is a wall or an edge
#define RLD_NAV_LANE          200.0  // paths 0 and 2 run this far beside the middle
#define RLD_NAV_LANE_MARGIN   96.0   // and keep this far from the edge
#define RLD_NAV_LANE_WINDOW   3      // the narrowest width of 3 frames before and after
#define RLD_NAV_OPCODE        0x0c00
#define RLD_NAV_FRAMES_MAX    8000
#define RLD_NAV_GAP_RUN       8      // at most 8 frames in a row without ground (a jump) ...
#define RLD_NAV_GAP_SHARE     20     // ... and at most 1 in 20 in all
#define RLD_NAV_CELL          512

#define RLD_GRID_COLS         4
#define RLD_GRID_COL          320.0  // as exported tracks with a full grid have it
#define RLD_GRID_COL_MIN      96.0
#define RLD_GRID_EDGE         64.0
static const double s_rldGridRow[2] = {224.0, 544.0};

#define RLD_PI 3.14159265358979323846

struct RldAutoLev
{
	// start grid
	int gridMade;
	const char *gridWhy;     // set: not made, and why (NULL with gridMade 0: not needed or --keep-start-spots)
	int gridSpotsBefore;     // distinct start spots before
	int gridColumn;          // spacing of the columns used

	// nav paths
	int navMade;
	const char *navWhy;
	u32 navFrames;           // frames per path
	u32 navRing;             // restart points on the ring
	u32 navRestart;          // restart points in the LEV
	u32 navLap;              // length of the ring, flat
	u32 navNoGround;         // middle frames without ground under them
	u32 navSideFallback;     // side frames put onto the middle (no ground beside)
};

struct RldGroundTri
{
	double ax, ay, az, bx, by, bz, cx, cy, cz;
};

struct RldGround
{
	struct RldGroundTri *tris;
	u32 count;
	int cellX0, cellZ0, cellsX, cellsZ;
	u32 *cellFirst; // cellsX * cellsZ + 1 entries
	u32 *cellTris;
};

static double Rld_NavSqrt(double v)
{
	double x;
	int k;

	if (v <= 0.0)
	{
		return 0.0;
	}

	// A start above the root, then Newton: it falls monotonically onto it.
	x = (v > 1.0) ? v : 1.0;
	while ((x * 0.25 * x) > v)
	{
		x *= 0.5;
	}

	for (k = 0; k < 64; k++)
	{
		const double next = 0.5 * (x + (v / x));

		if (next >= x)
		{
			break;
		}
		x = next;
	}

	return x;
}

// atan2 in 1/4096 turns, -2048..2048. The polynomial is good to about
// 1e-5 rad, ten times finer than one unit of the result.
static double Rld_NavAngle(double y, double x)
{
	const double ax = (x < 0.0) ? -x : x;
	const double ay = (y < 0.0) ? -y : y;
	double a;
	double s;
	double r;

	if ((ax == 0.0) && (ay == 0.0))
	{
		return 0.0;
	}

	a = (ax > ay) ? (ay / ax) : (ax / ay);
	s = a * a;
	r = ((((-0.0464964749 * s) + 0.15931422) * s) - 0.327622764) * s * a + a;

	if (ay > ax)
	{
		r = (RLD_PI / 2.0) - r;
	}
	if (x < 0.0)
	{
		r = RLD_PI - r;
	}
	if (y < 0.0)
	{
		r = -r;
	}

	return r * 2048.0 / RLD_PI;
}

static int Rld_NavRound(double v)
{
	return (v < 0.0) ? -(int)(0.5 - v) : (int)(v + 0.5);
}

static int Rld_NavFloor(double v)
{
	const int i = (int)v;

	return ((double)i > v) ? (i - 1) : i;
}

static int Rld_NavClamp16(int v)
{
	return (v < -32768) ? -32768 : ((v > 32767) ? 32767 : v);
}

static void Rld_GroundFree(struct RldGround *g)
{
	free(g->tris);
	free(g->cellFirst);
	free(g->cellTris);
	memset(g, 0, sizeof(*g));
}

static int Rld_GroundCell(const struct RldGround *g, double x, double z)
{
	const int cx = Rld_NavFloor(x / RLD_NAV_CELL) - g->cellX0;
	const int cz = Rld_NavFloor(z / RLD_NAV_CELL) - g->cellZ0;

	if ((cx < 0) || (cz < 0) || (cx >= g->cellsX) || (cz >= g->cellsZ))
	{
		return -1;
	}

	return (cz * g->cellsX) + cx;
}

// The ground the bots and the karts drive on: every high-detail triangle of a
// quad block with GROUND, without kill plane, trigger or "no collision
// response" (the bot's probe asks the same, BOTS.c:2034-2035). NULL = read.
static const char *Rld_GroundRead(const u8 *lev, size_t size, struct RldGround *g)
{
	// The eight triangles of a quad block's nine vertices, as the collision cuts it.
	static const u8 tri[8][3] = {{0, 4, 5}, {4, 6, 5}, {6, 4, 1}, {5, 6, 2}, {8, 6, 7}, {7, 3, 8}, {1, 7, 6}, {2, 6, 8}};
	const u32 body = Rld_ReadLE32At(lev, 0);
	u32 mesh, quads, vertices, quadAt, vertexAt;
	u32 q, k, n = 0;
	int minX = 0, minZ = 0, maxX = 0, maxZ = 0;
	u32 cells;
	u32 *fill;

	memset(g, 0, sizeof(*g));

	if (((u64)body + RLD_LEV_BODY) > size)
	{
		return "the level body does not fit the file";
	}

	mesh = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_MESH);
	if ((mesh == 0u) || (((u64)mesh + RLD_MESH_BYTES) > body))
	{
		return "the LEV has no readable mesh_info";
	}

	quads = Rld_ReadLE32At(lev, RLD_LEV_BODY + mesh + 0x00u);
	vertices = Rld_ReadLE32At(lev, RLD_LEV_BODY + mesh + 0x04u);
	quadAt = Rld_ReadLE32At(lev, RLD_LEV_BODY + mesh + 0x0cu);
	vertexAt = Rld_ReadLE32At(lev, RLD_LEV_BODY + mesh + 0x10u);

	if ((quads == 0u) || (quads > 0x100000u) || (vertices > 0x100000u) || (((u64)quadAt + ((u64)quads * RLD_QUAD_BYTES)) > body) ||
	    (((u64)vertexAt + ((u64)vertices * RLD_VERTEX_BYTES)) > body))
	{
		return "the quad blocks or vertices of the LEV lie outside the level body";
	}

	g->tris = (struct RldGroundTri *)malloc((size_t)quads * 8u * sizeof(struct RldGroundTri));
	if (g->tris == NULL)
	{
		return "out of memory";
	}

	for (q = 0; q < quads; q++)
	{
		const size_t at = RLD_LEV_BODY + (size_t)quadAt + ((size_t)q * RLD_QUAD_BYTES);
		const u32 flags = (u32)Rld_ReadLE16At(lev, at + RLD_QUAD_FLAGS) & 0xffffu;
		int p[9][3];

		if (((flags & RLD_QUAD_GROUND) == 0u) || ((flags & (RLD_QUAD_KILL_PLANE | RLD_QUAD_TRIGGER | RLD_QUAD_NO_RESPONSE)) != 0u))
		{
			continue;
		}

		for (k = 0; k < 9u; k++)
		{
			const u32 index = (u32)Rld_ReadLE16At(lev, at + (2u * k)) & 0xffffu;
			const size_t v = RLD_LEV_BODY + (size_t)vertexAt + ((size_t)index * RLD_VERTEX_BYTES);

			if (index >= vertices)
			{
				break;
			}

			p[k][0] = (int)(s16)Rld_ReadLE16At(lev, v + 0u);
			p[k][1] = (int)(s16)Rld_ReadLE16At(lev, v + 2u);
			p[k][2] = (int)(s16)Rld_ReadLE16At(lev, v + 4u);
		}

		if (k != 9u)
		{
			continue; // a vertex index past the table: the game would not find this ground either
		}

		for (k = 0; k < 8u; k++)
		{
			struct RldGroundTri *t = &g->tris[n];
			const int *a = p[tri[k][0]];
			const int *b = p[tri[k][1]];
			const int *c = p[tri[k][2]];
			const long long ny = ((long long)(c[2] - a[2]) * (b[0] - a[0])) - ((long long)(c[0] - a[0]) * (b[2] - a[2]));
			int j;

			if (ny == 0)
			{
				continue; // standing up: no height to read
			}

			t->ax = a[0]; t->ay = a[1]; t->az = a[2];
			t->bx = b[0]; t->by = b[1]; t->bz = b[2];
			t->cx = c[0]; t->cy = c[1]; t->cz = c[2];

			for (j = 0; j < 3; j++)
			{
				const int *v = (j == 0) ? a : ((j == 1) ? b : c);

				if ((n == 0u) && (j == 0))
				{
					minX = maxX = v[0];
					minZ = maxZ = v[2];
				}
				minX = (v[0] < minX) ? v[0] : minX;
				maxX = (v[0] > maxX) ? v[0] : maxX;
				minZ = (v[2] < minZ) ? v[2] : minZ;
				maxZ = (v[2] > maxZ) ? v[2] : maxZ;
			}
			n++;
		}
	}

	g->count = n;
	if (n == 0u)
	{
		return "the LEV has no ground (no quad block with GROUND)";
	}

	// A grid of 512 x 512 cells, each with the triangles that reach into it.
	g->cellX0 = Rld_NavFloor((double)minX / RLD_NAV_CELL);
	g->cellZ0 = Rld_NavFloor((double)minZ / RLD_NAV_CELL);
	g->cellsX = Rld_NavFloor((double)maxX / RLD_NAV_CELL) - g->cellX0 + 1;
	g->cellsZ = Rld_NavFloor((double)maxZ / RLD_NAV_CELL) - g->cellZ0 + 1;
	cells = (u32)g->cellsX * (u32)g->cellsZ;
	g->cellFirst = (u32 *)calloc((size_t)cells + 1u, sizeof(u32));
	fill = (u32 *)calloc((size_t)cells + 1u, sizeof(u32));
	if ((g->cellFirst == NULL) || (fill == NULL))
	{
		free(fill);
		return "out of memory";
	}

	{
		int pass;

		for (pass = 0; pass < 2; pass++)
		{
			for (k = 0; k < n; k++)
			{
				const struct RldGroundTri *t = &g->tris[k];
				const double x0 = (t->ax < t->bx) ? ((t->ax < t->cx) ? t->ax : t->cx) : ((t->bx < t->cx) ? t->bx : t->cx);
				const double x1 = (t->ax > t->bx) ? ((t->ax > t->cx) ? t->ax : t->cx) : ((t->bx > t->cx) ? t->bx : t->cx);
				const double z0 = (t->az < t->bz) ? ((t->az < t->cz) ? t->az : t->cz) : ((t->bz < t->cz) ? t->bz : t->cz);
				const double z1 = (t->az > t->bz) ? ((t->az > t->cz) ? t->az : t->cz) : ((t->bz > t->cz) ? t->bz : t->cz);
				int cx, cz;

				for (cz = Rld_NavFloor(z0 / RLD_NAV_CELL); cz <= Rld_NavFloor(z1 / RLD_NAV_CELL); cz++)
				{
					for (cx = Rld_NavFloor(x0 / RLD_NAV_CELL); cx <= Rld_NavFloor(x1 / RLD_NAV_CELL); cx++)
					{
						const u32 cell = ((u32)(cz - g->cellZ0) * (u32)g->cellsX) + (u32)(cx - g->cellX0);

						if (pass == 0)
						{
							g->cellFirst[cell + 1u]++;
						}
						else
						{
							g->cellTris[g->cellFirst[cell] + fill[cell]++] = k;
						}
					}
				}
			}

			if (pass == 0)
			{
				u32 c;

				for (c = 0; c < cells; c++)
				{
					g->cellFirst[c + 1u] += g->cellFirst[c];
				}

				g->cellTris = (u32 *)malloc(((size_t)g->cellFirst[cells] + 1u) * sizeof(u32));
				if (g->cellTris == NULL)
				{
					free(fill);
					return "out of memory";
				}
			}
		}
	}

	free(fill);
	return NULL;
}

// The height of the ground under (x, z) that lies closest to y, at most
// RLD_NAV_SNAP away (a bridge over a road is another ground). 1 = found.
static int Rld_GroundAt(const struct RldGround *g, double x, double y, double z, double *groundY)
{
	const int cell = Rld_GroundCell(g, x, z);
	int found = 0;
	double best = 0.0;
	u32 i;

	if (cell < 0)
	{
		return 0;
	}

	for (i = g->cellFirst[cell]; i < g->cellFirst[cell + 1]; i++)
	{
		const struct RldGroundTri *t = &g->tris[g->cellTris[i]];
		const double d1 = ((x - t->bx) * (t->az - t->bz)) - ((t->ax - t->bx) * (z - t->bz));
		const double d2 = ((x - t->cx) * (t->bz - t->cz)) - ((t->bx - t->cx) * (z - t->cz));
		const double d3 = ((x - t->ax) * (t->cz - t->az)) - ((t->cx - t->ax) * (z - t->az));
		double ux, uy, uz, vx, vy, vz, nx, ny, nz, h, off;

		if (((d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0)) && ((d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0)))
		{
			continue;
		}

		ux = t->cx - t->ax; uy = t->cy - t->ay; uz = t->cz - t->az;
		vx = t->bx - t->ax; vy = t->by - t->ay; vz = t->bz - t->az;
		nx = (uy * vz) - (uz * vy);
		ny = (uz * vx) - (ux * vz);
		nz = (ux * vy) - (uy * vx);
		if (ny == 0.0)
		{
			continue;
		}

		h = t->ay - (((nx * (x - t->ax)) + (nz * (z - t->az))) / ny);
		off = (h > y) ? (h - y) : (y - h);
		if ((off <= RLD_NAV_SNAP) && (!found || (off < best)))
		{
			found = 1;
			best = off;
			*groundY = h;
		}
	}

	return found;
}

// How far the ground goes from (x, y, z) along (rx, rz) without a step of
// more than RLD_NAV_SIDE_RISE - the width of the road to that side.
static int Rld_GroundReach(const struct RldGround *g, double x, double y, double z, double rx, double rz)
{
	int reach = 0;
	int w;

	for (w = RLD_NAV_SIDE_STEP; w <= RLD_NAV_SIDE_MAX; w += RLD_NAV_SIDE_STEP)
	{
		double h;

		if (!Rld_GroundAt(g, x + (rx * w), y, z + (rz * w), &h) || ((h - y) > RLD_NAV_SIDE_RISE) || ((y - h) > RLD_NAV_SIDE_RISE))
		{
			break;
		}

		y = h;
		reach = w;
	}

	return reach;
}

// The restart ring: from point 0 along nextIndex_forward until 0 again. Returns
// its length, 0 = no ring.
static u32 Rld_RestartRing(const u8 *lev, size_t size, u32 *ring, u32 max, u32 *countOut)
{
	const u32 body = Rld_ReadLE32At(lev, 0);
	const u32 count = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_NUM_RESTART);
	const u32 table = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_RESTART);
	u32 n = 0;
	u32 at = 0;

	*countOut = count;
	if ((count < 3u) || (count > max) || (table == 0u) || (((u64)table + ((u64)count * RLD_RESTART_BYTES)) > body) ||
	    (((u64)body + RLD_LEV_BODY) > size))
	{
		return 0;
	}

	do
	{
		if (n >= count)
		{
			return 0;
		}
		ring[n++] = at;
		at = lev[RLD_LEV_BODY + table + (at * RLD_RESTART_BYTES) + 8u];
		if (at >= count)
		{
			return 0;
		}
	} while (at != 0u);

	return (n >= 3u) ? n : 0u;
}

static void Rld_RestartPos(const u8 *lev, u32 index, double *p)
{
	const u32 table = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_RESTART);
	const size_t at = RLD_LEV_BODY + (size_t)table + ((size_t)index * RLD_RESTART_BYTES);

	p[0] = (double)(s16)Rld_ReadLE16At(lev, at + 0u);
	p[1] = (double)(s16)Rld_ReadLE16At(lev, at + 2u);
	p[2] = (double)(s16)Rld_ReadLE16At(lev, at + 4u);
}

// Centripetal Catmull-Rom from p1 to p2 at t in 0..1: it does not overshoot
// where the restart points lie unevenly (one exported track: 320 to 2842 apart).
static void Rld_NavCurve(const double *p0, const double *p1, const double *p2, const double *p3, double t, double *out)
{
	const double *p[4] = {p0, p1, p2, p3};
	double k[4];
	double a[3][3], b[2][3];
	double tt;
	int i, j;

	k[0] = 0.0;
	for (i = 1; i < 4; i++)
	{
		const double dx = p[i][0] - p[i - 1][0], dy = p[i][1] - p[i - 1][1], dz = p[i][2] - p[i - 1][2];
		double d = Rld_NavSqrt(Rld_NavSqrt((dx * dx) + (dy * dy) + (dz * dz)));

		k[i] = k[i - 1] + ((d < 0.001) ? 0.001 : d);
	}

	tt = k[1] + ((k[2] - k[1]) * t);
	for (j = 0; j < 3; j++)
	{
		for (i = 0; i < 3; i++)
		{
			a[i][j] = (((k[i + 1] - tt) / (k[i + 1] - k[i])) * p[i][j]) + (((tt - k[i]) / (k[i + 1] - k[i])) * p[i + 1][j]);
		}
		for (i = 0; i < 2; i++)
		{
			b[i][j] = (((k[i + 2] - tt) / (k[i + 2] - k[i])) * a[i][j]) + (((tt - k[i]) / (k[i + 2] - k[i])) * a[i + 1][j]);
		}
		out[j] = (((k[2] - tt) / (k[2] - k[1])) * b[0][j]) + (((tt - k[1]) / (k[2] - k[1])) * b[1][j]);
	}
}

// The start grid, in place. 1 = written.
static int Rld_AutoGrid(u8 *lev, size_t size, const struct RldGround *ground, const u32 *ring, struct RldAutoLev *out)
{
	double p0[3], p1[3], dx, dz, len, rx, rz, column = RLD_GRID_COL;
	int spots[8][3];
	int yaw, row, c;

	Rld_RestartPos(lev, ring[0], p0);
	Rld_RestartPos(lev, ring[1], p1);
	dx = p1[0] - p0[0];
	dz = p1[2] - p0[2];
	len = Rld_NavSqrt((dx * dx) + (dz * dz));
	if (len < 1.0)
	{
		out->gridWhy = "restart points 0 and 1 lie on the same spot - no direction for the grid";
		return 0;
	}
	dx /= len;
	dz /= len;
	rx = -dz; // the driver's right
	rz = dx;

	// As wide as the road behind the line allows, at most 320 apart.
	for (row = 0; row < 2; row++)
	{
		const double cx = p0[0] - (dx * s_rldGridRow[row]);
		const double cz = p0[2] - (dz * s_rldGridRow[row]);
		double h;
		int left, right, narrow;

		if (!Rld_GroundAt(ground, cx, p0[1], cz, &h))
		{
			out->gridWhy = "there is no ground behind restart point 0";
			return 0;
		}

		left = Rld_GroundReach(ground, cx, h, cz, -rx, -rz);
		right = Rld_GroundReach(ground, cx, h, cz, rx, rz);
		narrow = (left < right) ? left : right;
		if ((((double)narrow - RLD_GRID_EDGE) * 2.0 / 3.0) < column)
		{
			column = ((double)narrow - RLD_GRID_EDGE) * 2.0 / 3.0;
		}
	}

	if (column < RLD_GRID_COL_MIN)
	{
		out->gridWhy = "the road behind restart point 0 is too narrow for four karts side by side";
		return 0;
	}

	for (row = 0; row < 2; row++)
	{
		for (c = 0; c < RLD_GRID_COLS; c++)
		{
			const double side = ((double)c - 1.5) * column;
			const double x = p0[0] - (dx * s_rldGridRow[row]) + (rx * side);
			const double z = p0[2] - (dz * s_rldGridRow[row]) + (rz * side);
			double h;

			if (!Rld_GroundAt(ground, x, p0[1], z, &h))
			{
				out->gridWhy = "a start spot behind restart point 0 has no ground under it";
				return 0;
			}

			spots[(row * RLD_GRID_COLS) + c][0] = Rld_NavClamp16(Rld_NavRound(x));
			spots[(row * RLD_GRID_COLS) + c][1] = Rld_NavClamp16(Rld_NavRound(h));
			spots[(row * RLD_GRID_COLS) + c][2] = Rld_NavClamp16(Rld_NavRound(z));
		}
	}

	// DriverSpawn.rot.y is the heading minus a quarter turn (VehBirth.c:153 adds it back).
	yaw = Rld_NavRound(Rld_NavAngle(dx, dz)) - 1024;
	while (yaw < -2048)
	{
		yaw += 4096;
	}
	while (yaw >= 2048)
	{
		yaw -= 4096;
	}

	for (c = 0; c < 8; c++)
	{
		const size_t at = RLD_LEV_BODY + RLD_LEV_DRIVER_SPAWN + (12u * (size_t)c);

		Rld_WriteLE16(&lev[at + 0u], (u32)(u16)spots[c][0]);
		Rld_WriteLE16(&lev[at + 2u], (u32)(u16)spots[c][1]);
		Rld_WriteLE16(&lev[at + 4u], (u32)(u16)spots[c][2]);
		Rld_WriteLE16(&lev[at + 6u], 0u);
		Rld_WriteLE16(&lev[at + 8u], (u32)(u16)yaw);
		Rld_WriteLE16(&lev[at + 10u], 0u);
	}

	(void)size;
	out->gridColumn = Rld_NavRound(column);
	out->gridMade = 1;
	return 1;
}

struct RldNavPoint
{
	double x, y, z;
	u32 stretch;   // the restart point the point lies behind
	int ground;    // 1 = on the ground
	int left, right;
};

// The three paths, appended to the level body. On success *levInOut is a new
// buffer (the old one freed) and *sizeInOut its size.
static int Rld_AutoNav(u8 **levInOut, size_t *sizeInOut, const struct RldGround *ground, const u32 *ring, u32 ringCount, struct RldAutoLev *out)
{
	u8 *lev = *levInOut;
	const size_t size = *sizeInOut;
	const u32 body = Rld_ReadLE32At(lev, 0);
	const u32 mapBytes = Rld_ReadLE32At(lev, RLD_LEV_BODY + body);
	const size_t mapEnd = RLD_LEV_BODY + (size_t)body + 4u + (size_t)mapBytes;
	const u32 dense = ringCount * (u32)RLD_NAV_DENSE;
	double (*curve)[3] = NULL;
	u32 *curveStretch = NULL;
	double *length = NULL;
	struct RldNavPoint *pts = NULL;
	double (*dir)[2] = NULL;
	double total;
	u32 frames, f, i, run = 0, longest = 0;
	struct RldPtrMap map;
	u32 table, newTable = 0, added = 0;
	u32 newSlots[4];
	size_t navBytes, newSize;
	u8 *grown = NULL;
	int ok = 0;

	if ((mapEnd > size) || !Rld_PtrMapRead(lev, size, &map))
	{
		out->navWhy = "the pointer map of the LEV cannot be read";
		return 0;
	}

	curve = (double (*)[3])malloc((size_t)dense * sizeof(*curve));
	curveStretch = (u32 *)malloc((size_t)dense * sizeof(u32));
	length = (double *)malloc(((size_t)dense + 1u) * sizeof(double));
	if ((curve == NULL) || (curveStretch == NULL) || (length == NULL))
	{
		out->navWhy = "out of memory";
		goto done;
	}

	// The curve through the ring, dense, and its flat length.
	for (i = 0; i < ringCount; i++)
	{
		double p[4][3];
		int s;

		Rld_RestartPos(lev, ring[(i + ringCount - 1u) % ringCount], p[0]);
		Rld_RestartPos(lev, ring[i], p[1]);
		Rld_RestartPos(lev, ring[(i + 1u) % ringCount], p[2]);
		Rld_RestartPos(lev, ring[(i + 2u) % ringCount], p[3]);
		for (s = 0; s < RLD_NAV_DENSE; s++)
		{
			Rld_NavCurve(p[0], p[1], p[2], p[3], (double)s / RLD_NAV_DENSE, curve[(i * RLD_NAV_DENSE) + (u32)s]);
			curveStretch[(i * RLD_NAV_DENSE) + (u32)s] = ring[i];
		}
	}

	length[0] = 0.0;
	for (i = 1; i <= dense; i++)
	{
		const double *a = curve[i - 1u];
		const double *b = curve[i % dense];
		const double dx = b[0] - a[0], dz = b[2] - a[2];

		length[i] = length[i - 1u] + Rld_NavSqrt((dx * dx) + (dz * dz));
	}
	total = length[dense];
	out->navLap = (u32)Rld_NavRound(total);

	frames = (u32)Rld_NavRound(total / RLD_NAV_STEP);
	frames = (frames < 8u) ? 8u : frames;
	if (frames > RLD_NAV_FRAMES_MAX)
	{
		out->navWhy = "the restart ring is too long for a nav path";
		goto done;
	}
	out->navFrames = frames;

	pts = (struct RldNavPoint *)calloc(frames, sizeof(*pts));
	dir = (double (*)[2])calloc(frames, sizeof(*dir));
	if ((pts == NULL) || (dir == NULL))
	{
		out->navWhy = "out of memory";
		goto done;
	}

	// Evenly every RLD_NAV_STEP along the curve.
	for (f = 0, i = 0; f < frames; f++)
	{
		const double want = total * (double)f / (double)frames;
		const double *a, *b;
		double t;

		while ((i + 1u < dense) && (length[i + 1u] < want))
		{
			i++;
		}
		a = curve[i];
		b = curve[(i + 1u) % dense];
		t = (length[i + 1u] > length[i]) ? ((want - length[i]) / (length[i + 1u] - length[i])) : 0.0;
		pts[f].x = a[0] + ((b[0] - a[0]) * t);
		pts[f].y = a[1] + ((b[1] - a[1]) * t);
		pts[f].z = a[2] + ((b[2] - a[2]) * t);
		pts[f].stretch = curveStretch[i];
	}

	for (f = 0; f < frames; f++)
	{
		const struct RldNavPoint *a = &pts[(f + frames - 1u) % frames];
		const struct RldNavPoint *b = &pts[(f + 1u) % frames];
		const double dx = b->x - a->x, dz = b->z - a->z;
		const double len = Rld_NavSqrt((dx * dx) + (dz * dz));

		dir[f][0] = (len > 0.0) ? (dx / len) : 0.0;
		dir[f][1] = (len > 0.0) ? (dz / len) : 1.0;
	}

	// Onto the ground, and the width of the road there.
	for (f = 0; f < frames; f++)
	{
		double h;

		if (Rld_GroundAt(ground, pts[f].x, pts[f].y, pts[f].z, &h))
		{
			pts[f].y = h;
			pts[f].ground = 1;
			pts[f].left = Rld_GroundReach(ground, pts[f].x, h, pts[f].z, dir[f][1], -dir[f][0]);
			pts[f].right = Rld_GroundReach(ground, pts[f].x, h, pts[f].z, -dir[f][1], dir[f][0]);
			run = 0;
		}
		else
		{
			out->navNoGround++;
			run++;
			longest = (run > longest) ? run : longest;
		}
	}

	if ((longest > RLD_NAV_GAP_RUN) || ((out->navNoGround * RLD_NAV_GAP_SHARE) > frames))
	{
		out->navWhy = "the line through the restart points leaves the ground too often - the restart points do not follow the road";
		goto done;
	}

	// Where the paths go: an existing table with free slots, or a new one.
	table = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_NAVTABLE);
	if (table != 0u)
	{
		if ((((u64)table + (4u * RLD_NAV_PATHS)) > body) || !Rld_PtrMapHas(&map, RLD_LEV_PTR_NAVTABLE))
		{
			out->navWhy = "the LEV's nav table lies outside the level body";
			goto done;
		}
	}
	else if (Rld_PtrMapHas(&map, RLD_LEV_PTR_NAVTABLE))
	{
		out->navWhy = "the LEV's nav table is 0 but listed as a pointer";
		goto done;
	}

	navBytes = ((table == 0u) ? (4u * RLD_NAV_PATHS) : 0u) + (RLD_NAV_PATHS * (RLD_NAV_HEADER_BYTES + ((size_t)frames * RLD_NAV_FRAME_BYTES)));
	newSize = size + navBytes + (4u * (RLD_NAV_PATHS + 1u));
	if ((newSize > RLD_LIMIT_LEVD) || (((u64)body + navBytes) >= 0x80000000u))
	{
		out->navWhy = "the LEV would grow past the 16 MB a container allows";
		goto done;
	}

	grown = (u8 *)calloc(newSize, 1);
	if (grown == NULL)
	{
		out->navWhy = "out of memory";
		goto done;
	}

	// body | nav data | map (old entries, then the new) | what followed the map
	{
		const u32 newBody = body + (u32)navBytes;
		u32 at = body;
		int k;

		memcpy(grown, lev, RLD_LEV_BODY + (size_t)body);
		Rld_WriteLE32(&grown[0], newBody);

		if (table == 0u)
		{
			newTable = at;
			at += 4u * RLD_NAV_PATHS;
			Rld_WriteLE32(&grown[RLD_LEV_BODY + RLD_LEV_PTR_NAVTABLE], newTable);
			newSlots[added++] = RLD_LEV_PTR_NAVTABLE;
		}
		else
		{
			newTable = table;
		}

		for (k = 0; k < RLD_NAV_PATHS; k++)
		{
			const double lane = (k == 1) ? 0.0 : ((k == 0) ? -1.0 : 1.0);
			const u32 header = at;
			const u32 slot = newTable + (4u * (u32)k);
			int (*frame)[3] = (int (*)[3])malloc((size_t)frames * sizeof(*frame));

			if (frame == NULL)
			{
				out->navWhy = "out of memory";
				goto done;
			}

			for (f = 0; f < frames; f++)
			{
				double x = pts[f].x, y = pts[f].y, z = pts[f].z;

				if ((lane != 0.0) && pts[f].ground)
				{
					int w = 0x7fffffff;
					int d;
					double off, h;

					for (d = -RLD_NAV_LANE_WINDOW; d <= RLD_NAV_LANE_WINDOW; d++)
					{
						const struct RldNavPoint *n = &pts[(u32)(((int)f + d + (int)frames) % (int)frames)];
						const int narrow = n->ground ? ((n->left < n->right) ? n->left : n->right) : 0;

						w = (narrow < w) ? narrow : w;
					}

					off = (double)w - RLD_NAV_LANE_MARGIN;
					off = (off > RLD_NAV_LANE) ? RLD_NAV_LANE : ((off < 0.0) ? 0.0 : off);
					x += -dir[f][1] * lane * off;
					z += dir[f][0] * lane * off;
					if (Rld_GroundAt(ground, x, pts[f].y, z, &h))
					{
						y = h;
					}
					else
					{
						x = pts[f].x;
						z = pts[f].z;
						out->navSideFallback++;
					}
				}

				frame[f][0] = Rld_NavClamp16(Rld_NavRound(x));
				frame[f][1] = Rld_NavClamp16(Rld_NavRound(y));
				frame[f][2] = Rld_NavClamp16(Rld_NavRound(z));
			}

			// The header: magic, count; posY_firstNode, last and the ramp tables 0
			// (the game sets last itself, BOTS.c:160).
			Rld_WriteLE16(&grown[RLD_LEV_BODY + header + 0u], (u32)(u16)RLD_NAV_MAGIC);
			Rld_WriteLE16(&grown[RLD_LEV_BODY + header + 2u], frames);
			at += RLD_NAV_HEADER_BYTES;

			for (f = 0; f < frames; f++)
			{
				const int *a = frame[f];
				const int *b = frame[(f + 1u) % frames];
				const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
				const double flat = Rld_NavSqrt((dx * dx) + (dz * dz));
				const double full = Rld_NavSqrt((dx * dx) + (dy * dy) + (dz * dz));
				double heading = Rld_NavAngle(dx, dz);
				const int pitch = (flat > 0.0) ? -Rld_NavRound(Rld_NavAngle(dy, flat) / 16.0) : 0;
				const int xyz = Rld_NavRound(full), xz = Rld_NavRound(flat);
				u8 *o = &grown[RLD_LEV_BODY + at];

				heading = (heading < 0.0) ? (heading + 4096.0) : heading;
				Rld_WriteLE16(&o[0x00], (u32)(u16)a[0]);
				Rld_WriteLE16(&o[0x02], (u32)(u16)a[1]);
				Rld_WriteLE16(&o[0x04], (u32)(u16)a[2]);
				o[0x06] = (u8)pitch;
				o[0x07] = (u8)((u32)Rld_NavFloor(heading / 16.0) & 0xffu);
				o[0x08] = 0u;
				o[0x09] = (u8)(-pitch);
				Rld_WriteLE16(&o[0x0a], (u32)((xyz > 0x7fff) ? 0x7fff : xyz));
				Rld_WriteLE16(&o[0x0c], (u32)((xz > 0x7fff) ? 0x7fff : xz));
				Rld_WriteLE16(&o[0x0e], 0u);
				Rld_WriteLE16(&o[0x10], RLD_NAV_OPCODE);
				o[0x12] = (u8)pts[f].stretch;
				o[0x13] = 0u;
				at += RLD_NAV_FRAME_BYTES;
			}

			free(frame);
			Rld_WriteLE32(&grown[RLD_LEV_BODY + slot], header);
			if (!Rld_PtrMapHas(&map, slot))
			{
				newSlots[added++] = slot;
			}
		}

		// The map: the old entries as they were, the new ones behind them.
		{
			const size_t mapAt = RLD_LEV_BODY + (size_t)newBody;
			u32 j;

			Rld_WriteLE32(&grown[mapAt], mapBytes + (4u * added));
			memcpy(&grown[mapAt + 4u], map.slots, mapBytes);
			for (j = 0; j < added; j++)
			{
				Rld_WriteLE32(&grown[mapAt + 4u + mapBytes + (4u * j)], newSlots[j]);
			}
			memcpy(&grown[mapAt + 4u + mapBytes + (4u * added)], &lev[mapEnd], size - mapEnd);
			newSize = mapAt + 4u + mapBytes + (4u * added) + (size - mapEnd);
		}
	}

	free(lev);
	*levInOut = grown;
	*sizeInOut = newSize;
	grown = NULL;
	out->navMade = 1;
	ok = 1;

done:
	free(grown);
	free(curve);
	free(curveStretch);
	free(length);
	free(pts);
	free(dir);
	return ok;
}

// make: the grid and the paths where the LEV lacks them. Reads the start spots
// and nav paths with Rld_LevModesRead, the same reading the warnings use.
static void Rld_AutoLevApply(u8 **levInOut, size_t *sizeInOut, u32 modes, int keepStart, int keepNav, struct RldAutoLev *out)
{
	struct RldLevModes before;
	struct RldGround ground;
	u32 *ring;
	u32 ringCount, restart = 0;
	int wantGrid, wantNav;
	const char *why;

	memset(out, 0, sizeof(*out));
	if ((modes & RLD_LAP_MODES) == 0u)
	{
		return;
	}

	Rld_LevModesRead(*levInOut, *sizeInOut, &before);
	out->gridSpotsBefore = before.startSpots;
	wantGrid = !keepStart && (before.startSpots < RLD_DRIVER_SPAWNS);
	wantNav = !keepNav && ((modes & RLD_MODE_RACE) != 0u) && (before.navPaths == 0);
	if (!wantGrid && !wantNav)
	{
		return;
	}

	ring = (u32 *)malloc(256u * sizeof(u32));
	if (ring == NULL)
	{
		out->gridWhy = wantGrid ? "out of memory" : NULL;
		out->navWhy = wantNav ? "out of memory" : NULL;
		return;
	}

	ringCount = Rld_RestartRing(*levInOut, *sizeInOut, ring, 256u, &restart);
	out->navRestart = restart;
	out->navRing = ringCount;
	if (ringCount == 0u)
	{
		why = "the restart points do not form a ring from point 0 back to point 0";
		out->gridWhy = wantGrid ? why : NULL;
		out->navWhy = wantNav ? why : NULL;
		free(ring);
		return;
	}

	why = Rld_GroundRead(*levInOut, *sizeInOut, &ground);
	if (why != NULL)
	{
		out->gridWhy = wantGrid ? why : NULL;
		out->navWhy = wantNav ? why : NULL;
		Rld_GroundFree(&ground);
		free(ring);
		return;
	}

	if (wantGrid)
	{
		(void)Rld_AutoGrid(*levInOut, *sizeInOut, &ground, ring, out);
	}

	if (wantNav)
	{
		(void)Rld_AutoNav(levInOut, sizeInOut, &ground, ring, ringCount, out);
	}

	Rld_GroundFree(&ground);
	free(ring);
}

//----------------------------------------------------------------------------------------
// SELF-TEST (rldpack selftest): a LEV in miniature - one flat quad block of
// 16000 x 16000 at y 0, eight restart points on a circle of radius 3000, all
// eight start spots on one point, no nav table.

#define RLD_NAV_TEST_BODY 0x700u
#define RLD_NAV_TEST_MAP  5u

static u8 *Rld_NavTestLev(size_t *sizeOut, u32 quadFlags, int openRing)
{
	// The disc's quad block layout: 0 1 2 3 the corners, 4 5 7 8 the edges, 6 the middle.
	static const int corner[9][2] = {{-8000, -8000}, {8000, -8000}, {-8000, 8000}, {8000, 8000}, {0, -8000},
	                                 {-8000, 0}, {0, 0}, {8000, 0}, {0, 8000}};
	static const int ring[8][2] = {{3000, 0}, {2121, 2121}, {0, 3000}, {-2121, 2121}, {-3000, 0}, {-2121, -2121}, {0, -3000}, {2121, -2121}};
	static const u32 pointers[RLD_NAV_TEST_MAP] = {0x000u, 0x134u, 0x14cu, 0x20cu, 0x210u};
	const size_t size = RLD_LEV_BODY + RLD_NAV_TEST_BODY + 4u + (4u * RLD_NAV_TEST_MAP);
	u8 *lev = (u8 *)calloc(size, 1);
	u8 *b;
	u32 i;

	if (lev == NULL)
	{
		return NULL;
	}
	b = &lev[RLD_LEV_BODY];

	Rld_WriteLE32(&lev[0], RLD_NAV_TEST_BODY);
	Rld_WriteLE32(&b[0x000], 0x200u);                // mesh_info
	Rld_WriteLE32(&b[0x134], 0x600u);                // SpawnType1, count 0
	Rld_WriteLE32(&b[0x148], 8u);                    // restart points
	Rld_WriteLE32(&b[0x14c], 0x500u);
	for (i = 0; i < 8u; i++)
	{
		Rld_WriteLE16(&b[RLD_LEV_DRIVER_SPAWN + (12u * i) + 0u], 100u);
		Rld_WriteLE16(&b[RLD_LEV_DRIVER_SPAWN + (12u * i) + 4u], 100u);
	}

	Rld_WriteLE32(&b[0x200], 1u);                    // one quad block
	Rld_WriteLE32(&b[0x204], 9u);                    // nine vertices
	Rld_WriteLE32(&b[0x20c], 0x400u);
	Rld_WriteLE32(&b[0x210], 0x300u);
	for (i = 0; i < 9u; i++)
	{
		Rld_WriteLE16(&b[0x300u + (16u * i) + 0u], (u32)(u16)corner[i][0]);
		Rld_WriteLE16(&b[0x300u + (16u * i) + 4u], (u32)(u16)corner[i][1]);
		Rld_WriteLE16(&b[0x400u + (2u * i)], i);
	}
	Rld_WriteLE16(&b[0x400u + RLD_QUAD_FLAGS], quadFlags);

	for (i = 0; i < 8u; i++)
	{
		u8 *p = &b[0x500u + (RLD_RESTART_BYTES * i)];

		Rld_WriteLE16(&p[0], (u32)(u16)ring[i][0]);
		Rld_WriteLE16(&p[4], (u32)(u16)ring[i][1]);
		Rld_WriteLE16(&p[6], 2400u - (300u * i));
		p[8] = (u8)((openRing && (i == 3u)) ? 3u : ((i + 1u) % 8u));
		p[9] = 0xffu;
		p[10] = (u8)((i + 7u) % 8u);
		p[11] = 0xffu;
	}

	Rld_WriteLE32(&b[RLD_NAV_TEST_BODY], 4u * RLD_NAV_TEST_MAP);
	for (i = 0; i < RLD_NAV_TEST_MAP; i++)
	{
		Rld_WriteLE32(&b[RLD_NAV_TEST_BODY + 4u + (4u * i)], pointers[i]);
	}

	*sizeOut = size;
	return lev;
}

static int Rld_SelftestAutoLev(void)
{
	// SHA-256 of the made LEV: the same bytes on every machine (see the head of this file).
	static const char golden[] = "45072fcc205a13ad8f0003d0eac3055752d501ef5d9e8c2129833bbec898c0f2";
	int bad = 0;
	size_t size = 0, size2;
	u8 *lev = Rld_NavTestLev(&size, RLD_QUAD_GROUND | 0x2000u, 0);
	u8 *copy = NULL;
	struct RldAutoLev report;
	struct RldLevModes modes;
	char digestText[65];

	if (lev == NULL)
	{
		printf("  FAIL start grid and nav paths: out of memory\n");
		return 1;
	}

	if (Rld_CheckLev(lev, size) != NULL)
	{
		bad |= 1; // the test LEV itself
	}

	// Made: grid and paths, and the game's own check takes the result.
	Rld_AutoLevApply(&lev, &size, RLD_MODE_RACE, 0, 0, &report);
	Rld_LevModesRead(lev, size, &modes);
	if (!report.gridMade || !report.navMade || (report.gridSpotsBefore != 1) || (report.navRing != 8u) || (modes.navPaths != 3) ||
	    (modes.startSpots != 8) || (report.navFrames < 70u) || (report.navFrames > 90u) || (Rld_CheckLev(lev, size) != NULL))
	{
		bad |= 2;
	}

	// Every frame on the ground; goBackCount climbs 0..7 once around.
	if (!(bad & 2))
	{
		const u32 body = Rld_ReadLE32At(lev, 0);
		const u32 table = Rld_ReadLE32At(lev, RLD_LEV_BODY + RLD_LEV_PTR_NAVTABLE);
		int k;

		for (k = 0; k < RLD_NAV_PATHS; k++)
		{
			const u32 header = Rld_ReadLE32At(lev, RLD_LEV_BODY + table + (4u * (u32)k));
			u32 f, last = 0;

			for (f = 0; (f < report.navFrames) && (header < body); f++)
			{
				const size_t at = RLD_LEV_BODY + header + RLD_NAV_HEADER_BYTES + ((size_t)f * RLD_NAV_FRAME_BYTES);
				const u32 stretch = lev[at + 0x12u];

				if ((Rld_ReadLE16At(lev, at + 2u) != 0) || (stretch < last) || ((f == 0u) && (stretch != 0u)) ||
				    (Rld_ReadLE16At(lev, at + 0x10u) != RLD_NAV_OPCODE))
				{
					bad |= 4;
				}
				last = stretch;
			}

			if (last != 7u)
			{
				bad |= 4;
			}
		}

		// The grid behind point 0 (3000, 0), heading +z there: z below 0, x around 3000.
		for (k = 0; k < 8; k++)
		{
			const size_t at = RLD_LEV_BODY + RLD_LEV_DRIVER_SPAWN + (12u * (size_t)k);
			const int x = (int)(s16)Rld_ReadLE16At(lev, at + 0u);
			const int z = (int)(s16)Rld_ReadLE16At(lev, at + 4u);

			if ((z >= 0) || (x < 2300) || (x > 3900) || (Rld_ReadLE16At(lev, at + 2u) != 0))
			{
				bad |= 8;
			}
		}
	}

	{
		u8 digest[32];
		int j;

		Sha256(lev, size, digest);
		for (j = 0; j < 32; j++)
		{
			snprintf(&digestText[j * 2], 3, "%02x", digest[j]);
		}
		if (strcmp(digestText, golden) != 0)
		{
			bad |= 16;
		}
	}

	// A second pass changes nothing: the LEV has both now.
	copy = (u8 *)malloc(size);
	if (copy != NULL)
	{
		memcpy(copy, lev, size);
		size2 = size;
		Rld_AutoLevApply(&copy, &size2, RLD_MODE_RACE, 0, 0, &report);
		if ((size2 != size) || (memcmp(copy, lev, size) != 0) || report.gridMade || report.navMade || (report.gridWhy != NULL) ||
		    (report.navWhy != NULL))
		{
			bad |= 32;
		}
		free(copy);
	}
	free(lev);

	// --keep-start-spots --keep-nav, an open ring, no ground: nothing changes, and
	// the last two say why.
	{
		static const struct
		{
			u32 flags;
			int openRing, keep;
		} cases[3] = {{RLD_QUAD_GROUND | 0x2000u, 0, 1}, {RLD_QUAD_GROUND | 0x2000u, 1, 0}, {RLD_QUAD_GROUND | RLD_QUAD_KILL_PLANE, 0, 0}};
		int c;

		for (c = 0; c < 3; c++)
		{
			u8 *a = Rld_NavTestLev(&size, cases[c].flags, cases[c].openRing);
			u8 *orig = Rld_NavTestLev(&size2, cases[c].flags, cases[c].openRing);

			if ((a == NULL) || (orig == NULL))
			{
				bad |= 64;
			}
			else
			{
				Rld_AutoLevApply(&a, &size, RLD_MODE_RACE, cases[c].keep, cases[c].keep, &report);
				if ((size != size2) || (memcmp(a, orig, size) != 0) || report.gridMade || report.navMade ||
				    ((report.navWhy == NULL) != (cases[c].keep != 0)) || ((report.gridWhy == NULL) != (cases[c].keep != 0)))
				{
					bad |= 64 << c;
				}
			}
			free(a);
			free(orig);
		}
	}

	if (bad != 0)
	{
		printf("  FAIL start grid and nav paths: case mask %d (made LEV %s)\n", bad, digestText);
		return 1;
	}

	printf("  ok   start grid and nav paths: made where missing, on the ground, the game's LEV check takes them, golden, second pass and keep change nothing, open ring and no ground refused\n");
	return 0;
}
