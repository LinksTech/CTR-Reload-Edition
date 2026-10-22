// THE ZONE PORT'S CONTACTS - see include/platform/native_extzonecontact.h.
//
// WHAT. The source physics' collision detector is not ported (a large mesh
// tree search). Its outputs are: per step the list of contact records (P+0xF8),
// the list of touching pairs (P+0xC8) and the list of overlapping pairs
// (P+0xEC, which starts the penetration handling); after the step it hands
// every record to the subspace's contact listener, which ends in the kart's
// contact callback. This file computes those outputs from a triangle provider
// (Reload's collision in the game, a test geometry offline) and writes them
// into the port's memory in the source's layout, so that the ported solver, the
// ported penetration handling and the ported callback run unchanged on them.
// The geometry is computed in double precision and stored as floats: a
// stand-in, so it is not bit-exact against the original (the original's own
// mesh test is not ported); everything that reads it is.
//
// The list keeping of the detector (0x3771C0, 0x377290, 0x36F840, 0x3770E0,
// 0x376B90) follows the original's code on the lists the port reads; the
// detector's own caches (which only speed up its mesh search) are not kept.
//
// Standalone C: compiles alone for the offline tests. Reload's triangle
// provider lives in native_extzonecontact_reload.c (game build only).

#include <platform/native_extzonephys.h>
#include <platform/native_extzonecontact.h>
#include <platform/native_extzonephys_j.h>
#include <math.h>
#include <string.h>

#define ZC_KART_VTABLE 0x006CCDD0u // the kart action's class (body + 0x28 -> owner, owner + 0 == this: a kart)
#define ZC_F_ONE 0x3F800000u
#define ZC_TRI_CACHE 512

// The arena (cfg.recordArena, EXT_ZC_ARENA_BYTES) is the source's heap for the port: the block allocator
// 0x33C1B0 and its release 0x33C3B0 are served from it, and the detector's vectors grow in it the way the
// source's vectors do (a block with the allocator header of 0x33C120, the old block released as 0x33C170
// does). Blocks live until they are released, as in the source; a released block is kept in a free list in
// port memory and given out again for the same size. Head of the arena: +0 magic, +4 top (next unused
// byte), +8 first free block (a free block: +4 its size, +8 the next free block).
#define ZC_HEAP_MAGIC 0x5A434850u
#define ZC_HEAP_HEAD 0x10u
#define ZC_BLOCK_TAG 0x02345656u  // the allocator header's tag word (0x33C120)
#define ZC_BLOCK_FREED 0xDEADBEEFu // the tag word of a released block (0x33C170)
#define ZC_VEC_TAG 14u             // the tag the source's vectors allocate with (measured: P+0x1C, P+0xEC, P+0xF8 in slot6)

typedef struct ZcTriInfo
{
	int key;
	int surface;
	double n[3]; // the triangle's normal towards the body it touched last
} ZcTriInfo;

static ExtZcConfig s_zc;
static int s_zcReady;
static uint32_t s_zcHeapFull; // allocations the arena could not serve (log line)
static ExtZpMem *s_zcMem;     // the memory of the last answer (ExtZc_HeapStats)
static ZcTriInfo s_zcTri[ZC_TRI_CACHE];
static int s_zcTriCount;

// ------------------------------------------------------------------ small helpers

// The nearest float of v as the port's bits (denormals as signed 0, beyond the
// largest float the largest float: the second console's float has neither).
static Ps2F zc_bits(double v)
{
	float f;
	uint32_t b;
	if (v > 3.4028234663852886e38)
		v = 3.4028234663852886e38;
	if (v < -3.4028234663852886e38)
		v = -3.4028234663852886e38;
	f = (float)v;
	memcpy(&b, &f, 4);
	if ((b & 0x7F800000u) == 0u)
		b &= 0x80000000u;
	return b;
}
static double zc_d(Ps2F v) { return Ps2F_ToDouble(v); }
static void zc_put_vec(uint8_t *rec, uint32_t off, const double v[3], Ps2F w)
{
	Ps2F t[4];
	t[0] = zc_bits(v[0]);
	t[1] = zc_bits(v[1]);
	t[2] = zc_bits(v[2]);
	t[3] = w;
	memcpy(rec + off, t, 16);
}

// a vector of the source: {capacity +0, count +4, data +8}
static uint32_t zc_vcount(ExtZpMem *m, uint32_t v) { return ZmR32(m, v + 4u); }
static uint32_t zc_vdata(ExtZpMem *m, uint32_t v) { return ZmR32(m, v + 8u); }

// ------------------------------------------------------------------ the heap (see the arena above)

static uint32_t zc_heap(ExtZpMem *m)
{
	const uint32_t a = s_zc.recordArena;
	if (ZmR32(m, a) != ZC_HEAP_MAGIC)
	{
		ZmW32(m, a + 0u, ZC_HEAP_MAGIC);
		ZmW32(m, a + 4u, a + ZC_HEAP_HEAD);
		ZmW32(m, a + 8u, 0u);
	}
	return a;
}
// 0x33C1B0: a block of `bytes` (rounded to 16), zeroed; 0 when the arena is used up (counted).
static uint32_t zc_alloc(ExtZpMem *m, uint32_t bytes)
{
	const uint32_t a = zc_heap(m), size = (bytes + 15u) & ~15u;
	uint32_t prev = a + 8u, blk = ZmR32(m, a + 8u), top;
	while (blk != 0u)
	{
		if (ZmR32(m, blk + 4u) == size)
		{
			ZmW32(m, prev, ZmR32(m, blk + 8u));
			break;
		}
		prev = blk + 8u;
		blk = ZmR32(m, blk + 8u);
	}
	if (blk == 0u)
	{
		top = ZmR32(m, a + 4u);
		if ((size == 0u) || (top + size > a + (uint32_t)EXT_ZC_ARENA_BYTES))
		{
			s_zcHeapFull++;
			return 0u;
		}
		blk = top;
		ZmW32(m, a + 4u, top + size);
	}
	for (uint32_t i = 0; i < size; i += 4u)
		ZmW32(m, blk + i, 0u);
	return blk;
}
// 0x33C3B0 (block, bytes, tag): the block goes to the free list.
static void zc_release(ExtZpMem *m, uint32_t blk, uint32_t bytes)
{
	const uint32_t a = zc_heap(m);
	if (blk == 0u)
		return;
	ZmW32(m, blk + 4u, (bytes + 15u) & ~15u);
	ZmW32(m, blk + 8u, ZmR32(m, a + 8u));
	ZmW32(m, a + 8u, blk);
}
// 0x33C120 / 0x33C170 inline: a block with the allocator header (tag, size, tag argument), the data behind it.
static uint32_t zc_block_new(ExtZpMem *m, uint32_t size)
{
	const uint32_t blk = zc_alloc(m, size + 0x10u);
	if (blk == 0u)
		return 0u;
	ZmW32(m, blk + 0x0u, ZC_BLOCK_TAG);
	ZmW32(m, blk + 0x4u, size);
	ZmW32(m, blk + 0x8u, ZC_VEC_TAG);
	return blk + 0x10u;
}
static void zc_block_free(ExtZpMem *m, uint32_t blockData)
{
	uint32_t size;
	if (blockData == 0u)
		return;
	ZmW32(m, blockData - 0x10u, ZC_BLOCK_FREED);
	size = ZmR32(m, blockData - 0xCu);
	zc_release(m, blockData - 0x10u, size + 0x10u);
}

// Makes room for `need` elements of `size` bytes as the source's vector insert does (0x3674C0): the capacity
// (at least 1) doubles until it holds them, the elements move to a new block, the old block is released.
static void zc_vreserve(ExtZpCtx *c, uint32_t v, uint32_t need, uint32_t size)
{
	ExtZpMem *m = c->m;
	uint32_t cap = ZmR32(m, v), count = zc_vcount(m, v), old = zc_vdata(m, v), dst;
	uint8_t tmp[256];
	if (need <= cap)
		return;
	if (cap == 0u)
		cap = 1u;
	while (cap < need)
		cap <<= 1;
	dst = zc_block_new(m, cap * size);
	if (dst == 0u)
		return;
	{
		uint32_t bytes = count * size, done = 0u;
		while (done < bytes)
		{
			uint32_t n = (bytes - done > sizeof(tmp)) ? (uint32_t)sizeof(tmp) : bytes - done;
			ExtZpMem_Read(m, old + done, tmp, n);
			ExtZpMem_Write(m, dst + done, tmp, n);
			done += n;
		}
	}
	zc_block_free(m, old);
	ZmW32(m, v, cap);
	ZmW32(m, v + 8u, dst);
}
static void zc_vpush(ExtZpCtx *c, uint32_t v, const void *elem, uint32_t size)
{
	ExtZpMem *m = c->m;
	uint32_t count = zc_vcount(m, v);
	zc_vreserve(c, v, count + 1u, size);
	if (count >= ZmR32(m, v))
		return; // the arena is used up (counted in s_zcHeapFull)
	ExtZpMem_Write(m, zc_vdata(m, v) + count * size, elem, size);
	ZmW32(m, v + 4u, count + 1u);
}
static int zc_pair_in(ExtZpMem *m, uint32_t v, uint32_t a, uint32_t b)
{
	uint32_t n = zc_vcount(m, v), d = zc_vdata(m, v);
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t x = ZmR32(m, d + 8u * i), y = ZmR32(m, d + 8u * i + 4u);
		if (((x == a) && (y == b)) || ((x == b) && (y == a)))
			return 1;
	}
	return 0;
}
static void zc_pair_add_unique(ExtZpCtx *c, uint32_t v, uint32_t a, uint32_t b)
{
	uint32_t e[2] = {a, b};
	if (!zc_pair_in(c->m, v, a, b))
		zc_vpush(c, v, e, 8u);
}
// body flag byte (body + 0x18): bit 0 or bit 2 = fixed (no response)
static int zc_static(ExtZpMem *m, uint32_t body) { return (ZmR8(m, body + 0x18u) & 5u) != 0u; }
// the shell (collision wrapper, shell + 0x18 = body) of a body: the detector's dynamic shells (P+0x1C vector), else the track shell
static uint32_t zc_shell_of(ExtZpMem *m, uint32_t P, uint32_t body)
{
	uint32_t n = ZmR32(m, P + 0x20u), d = ZmR32(m, P + 0x24u);
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t s = ZmR32(m, d + 4u * i);
		if (ZmR32(m, s + 0x18u) == body)
			return s;
	}
	return s_zc.trackShell;
}
static void zc_tri_remember(const ExtZcTri *t, const double n[3])
{
	int i;
	for (i = 0; i < s_zcTriCount; i++)
		if (s_zcTri[i].key == t->key)
			break;
	if (i == s_zcTriCount)
	{
		if (s_zcTriCount == ZC_TRI_CACHE)
			i = (int)((unsigned)t->key % ZC_TRI_CACHE);
		else
			s_zcTriCount++;
	}
	s_zcTri[i].key = t->key;
	s_zcTri[i].surface = t->surface;
	s_zcTri[i].n[0] = n[0];
	s_zcTri[i].n[1] = n[1];
	s_zcTri[i].n[2] = n[2];
}
static const ZcTriInfo *zc_tri_find(int key)
{
	for (int i = 0; i < s_zcTriCount; i++)
		if (s_zcTri[i].key == key)
			return &s_zcTri[i];
	return NULL;
}

// ------------------------------------------------------------------ geometry: sphere against triangle

// The point of triangle (a, b, c) nearest to p (regions of the triangle: faces, edges, corners).
static void zc_closest(const double p[3], const double a[3], const double b[3], const double c[3], double q[3])
{
	double ab[3], ac[3], ap[3], bp[3], cp[3], d1, d2, d3, d4, d5, d6, va, vb, vc, v, w, den;
	int k;
	for (k = 0; k < 3; k++)
	{
		ab[k] = b[k] - a[k];
		ac[k] = c[k] - a[k];
		ap[k] = p[k] - a[k];
	}
	d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
	d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
	if ((d1 <= 0.0) && (d2 <= 0.0))
	{
		memcpy(q, a, sizeof(double) * 3);
		return;
	}
	for (k = 0; k < 3; k++)
		bp[k] = p[k] - b[k];
	d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
	d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
	if ((d3 >= 0.0) && (d4 <= d3))
	{
		memcpy(q, b, sizeof(double) * 3);
		return;
	}
	vc = d1 * d4 - d3 * d2;
	if ((vc <= 0.0) && (d1 >= 0.0) && (d3 <= 0.0))
	{
		v = d1 / (d1 - d3);
		for (k = 0; k < 3; k++)
			q[k] = a[k] + v * ab[k];
		return;
	}
	for (k = 0; k < 3; k++)
		cp[k] = p[k] - c[k];
	d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
	d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
	if ((d6 >= 0.0) && (d5 <= d6))
	{
		memcpy(q, c, sizeof(double) * 3);
		return;
	}
	vb = d5 * d2 - d1 * d6;
	if ((vb <= 0.0) && (d2 >= 0.0) && (d6 <= 0.0))
	{
		w = d2 / (d2 - d6);
		for (k = 0; k < 3; k++)
			q[k] = a[k] + w * ac[k];
		return;
	}
	va = d3 * d6 - d5 * d4;
	if ((va <= 0.0) && ((d4 - d3) >= 0.0) && ((d5 - d6) >= 0.0))
	{
		w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		for (k = 0; k < 3; k++)
			q[k] = b[k] + w * (c[k] - b[k]);
		return;
	}
	den = 1.0 / (va + vb + vc);
	v = vb * den;
	w = vc * den;
	for (k = 0; k < 3; k++)
		q[k] = a[k] + ab[k] * v + ac[k] * w;
}

static int zc_sphere_tri(const double ctr[3], double radius, double tol, const ExtZcTri *t, uint8_t rec[EXT_ZC_REC_SIZE], double nOut[3], double *gapOut)
{
	double q[3], d[3], len, n[3], wa[3], pt[3], gap, fn[3], e1[3], e2[3], fl;
	int k;
	if (!t->solid)
		return 0;
	zc_closest(ctr, t->a, t->b, t->c, q);
	for (k = 0; k < 3; k++)
		d[k] = ctr[k] - q[k];
	len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	gap = len - radius;
	if (gap > tol)
		return 0;
	for (k = 0; k < 3; k++)
	{
		e1[k] = t->b[k] - t->a[k];
		e2[k] = t->c[k] - t->a[k];
	}
	fn[0] = e1[1] * e2[2] - e1[2] * e2[1];
	fn[1] = e1[2] * e2[0] - e1[0] * e2[2];
	fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
	fl = sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
	if (len > 1e-9)
	{
		for (k = 0; k < 3; k++)
			n[k] = d[k] / len;
	}
	else if (fl > 1e-12)
	{
		for (k = 0; k < 3; k++)
			n[k] = fn[k] / fl;
	}
	else
		return 0;
	for (k = 0; k < 3; k++)
	{
		wa[k] = ctr[k] - n[k] * radius;
		pt[k] = 0.5 * (wa[k] + q[k]);
	}
	memset(rec, 0, EXT_ZC_REC_SIZE);
	{
		const uint32_t vt = s_zc.recordClass, keyA = 0xFFFFFFFFu, keyB = (uint32_t)t->key;
		const Ps2F g = zc_bits(gap);
		memcpy(rec + EXT_ZC_REC_VTABLE, &vt, 4);
		memcpy(rec + EXT_ZC_REC_GAP, &g, 4);
		memcpy(rec + EXT_ZC_REC_KEY_A, &keyA, 4);
		memcpy(rec + EXT_ZC_REC_KEY_B, &keyB, 4);
	}
	zc_put_vec(rec, EXT_ZC_REC_NORMAL, n, ZC_F_ONE);
	zc_put_vec(rec, EXT_ZC_REC_POINT, pt, ZC_F_ONE);
	zc_put_vec(rec, EXT_ZC_REC_WITNESS_A, wa, ZC_F_ONE);
	zc_put_vec(rec, EXT_ZC_REC_WITNESS_B, q, ZC_F_ONE);
	if (nOut != NULL)
		memcpy(nOut, n, sizeof(double) * 3);
	if (gapOut != NULL)
		*gapOut = gap;
	return 1;
}

int ExtZc_SphereTriangle(const Ps2F centre[3], Ps2F radius, Ps2F tol, const ExtZcTri *tri, uint8_t rec[EXT_ZC_REC_SIZE])
{
	double c3[3] = {zc_d(centre[0]), zc_d(centre[1]), zc_d(centre[2])};
	return zc_sphere_tri(c3, zc_d(radius), zc_d(tol), tri, rec, NULL, NULL);
}

// All records of one dynamic body (its shell) against the geometry: appended to
// `recs` (vector), the pair (shell, track) to `pairs` if any, to `pen` if one overlaps.
// Returns the number of records; *overlap = 1 if one has a negative gap.
static int zc_body_records(ExtZpCtx *c, uint32_t shell, Ps2F tol, uint32_t recs, uint32_t pairs, uint32_t pen, int *overlap)
{
	ExtZpMem *m = c->m;
	static ExtZcTri tris[EXT_ZC_MAX_TRIS];
	uint32_t body = ZmR32(m, shell + 0x18u), trackBody = ZmR32(m, s_zc.trackShell + 0x18u);
	double ctr[3], radius = zc_d(s_zc.radius), t = zc_d(tol);
	int nt, made = 0, k;
	*overlap = 0;
	if ((body == 0u) || zc_static(m, body) || (s_zc.triangles == NULL) || ((s_zc.onlyShell != 0u) && (shell != s_zc.onlyShell)))
		return 0;
	for (k = 0; k < 3; k++)
		ctr[k] = zc_d(ZmRF(m, body + 0x30u + 4u * (uint32_t)k));
	nt = s_zc.triangles(s_zc.user, ctr, radius + t, tris, EXT_ZC_MAX_TRIS);
	for (int i = 0; i < nt; i++)
	{
		uint8_t rec[EXT_ZC_REC_SIZE];
		double n[3], gap;
		if (!zc_sphere_tri(ctr, radius, t, &tris[i], rec, n, &gap))
			continue;
		{
			uint32_t w[2] = {shell, s_zc.trackShell};
			Ps2F mat[4] = {ZmRF(m, body + 0x1B8u), ZmRF(m, trackBody + 0x1B8u), ZmRF(m, body + 0x1B4u), ZmRF(m, trackBody + 0x1B4u)};
			memcpy(rec + EXT_ZC_REC_SHELL_A, w, 8);
			memcpy(rec + EXT_ZC_REC_FRICTION_A, mat, 16);
		}
		zc_tri_remember(&tris[i], n);
		zc_vpush(c, recs, rec, EXT_ZC_REC_SIZE);
		made++;
		if (gap < 0.0)
			*overlap = 1;
	}
	if ((made > 0) && (pairs != 0u))
		zc_pair_add_unique(c, pairs, shell, s_zc.trackShell);
	if (*overlap && (pen != 0u))
		zc_pair_add_unique(c, pen, shell, s_zc.trackShell);
	return made;
}

// ------------------------------------------------------------------ the answers

// 0x3B18F0: the detection of a step.
static uint32_t zc_detect(ExtZpCtx *c, uint32_t sub)
{
	ExtZpMem *m = c->m;
	uint32_t det = ZmR32(m, sub + EXT_ZC_SUB_DETECTOR), P, n, d;
	Ps2F tol;
	if (det == 0u)
		return 0u;
	P = ZmR32(m, det + EXT_ZC_DET_INNER);
	tol = ZmRF(m, P + EXT_ZC_P_TOLERANCE);
	ZmW32(m, P + EXT_ZC_P_CONTACTS + 4u, 0u);
	ZmW32(m, P + EXT_ZC_P_PENETRATING + 4u, 0u);
	ZmW32(m, P + EXT_ZC_P_PAIRS + 4u, 0u);
	n = ZmR32(m, P + 0x20u);
	d = ZmR32(m, P + 0x24u);
	for (uint32_t i = 0; i < n; i++)
	{
		int ov;
		(void)zc_body_records(c, ZmR32(m, d + 4u * i), tol, P + EXT_ZC_P_CONTACTS, P + EXT_ZC_P_PAIRS, P + EXT_ZC_P_PENETRATING, &ov);
	}
	return (zc_vcount(m, P + EXT_ZC_P_PENETRATING) != 0u) ? 1u : 0u;
}

// 0x380FA0 (a0 = DET, a1 = body vector (4-byte), a2 = new pairs, a3 = records, t0 = pairs): detection for these bodies.
static void zc_detect_set(ExtZpCtx *c, const ExtZpQueryArgs *in)
{
	ExtZpMem *m = c->m;
	uint32_t P = ZmR32(m, in->a[0] + EXT_ZC_DET_INNER), n = zc_vcount(m, in->a[1]), d = zc_vdata(m, in->a[1]);
	Ps2F tol = ZmRF(m, P + EXT_ZC_P_TOLERANCE);
	for (uint32_t i = 0; i < n; i++)
	{
		int ov;
		uint32_t body = ZmR32(m, d + 4u * i);
		(void)zc_body_records(c, zc_shell_of(m, P, body), tol, in->a[3], in->a[4], in->a[2], &ov);
	}
}

// 0x380FB0 (a0 = DET, a1 = body A, a2 = body B, a3 = u8 flag out, t0 = record vector out): one pair's test.
static void zc_detect_pair(ExtZpCtx *c, const ExtZpQueryArgs *in)
{
	ExtZpMem *m = c->m;
	uint32_t P = ZmR32(m, in->a[0] + EXT_ZC_DET_INNER), dyn = in->a[1];
	Ps2F tol = ZmRF(m, P + EXT_ZC_P_TOLERANCE);
	int ov = 0;
	if (zc_static(m, dyn))
		dyn = in->a[2];
	(void)zc_body_records(c, zc_shell_of(m, P, dyn), tol, in->a[4], 0u, 0u, &ov);
	ZmW8(m, in->a[3], (uint8_t)(ov ? 1u : 0u));
}

// 0x3771C0 (a0 = P): the pairs of P+0xC8 whose bodies both lack flag bit 1 go to P+0xE0, then P+0xC8 is emptied.
static void zc_pairs_before(ExtZpCtx *c, uint32_t P)
{
	ExtZpMem *m = c->m;
	uint32_t n = zc_vcount(m, P + 0xC8u), d = zc_vdata(m, P + 0xC8u);
	ZmW32(m, P + 0xE0u + 4u, 0u);
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t a = ZmR32(m, d + 8u * i), b = ZmR32(m, d + 8u * i + 4u);
		if (((ZmR8(m, ZmR32(m, a + 0x18u) + 0x18u) & 2u) == 0u) && ((ZmR8(m, ZmR32(m, b + 0x18u) + 0x18u) & 2u) == 0u))
		{
			uint32_t e[2] = {a, b};
			zc_vpush(c, P + 0xE0u, e, 8u);
		}
	}
	ZmW32(m, P + 0xC8u + 4u, 0u);
}

// 0x377290 (a0 = P, a1 = pair listener): pair begin/end events (not used by the kart) and P+0xD4 := P+0xC8.
static void zc_pairs_after(ExtZpCtx *c, uint32_t P)
{
	ExtZpMem *m = c->m;
	uint32_t n = zc_vcount(m, P + 0xC8u), d = zc_vdata(m, P + 0xC8u);
	ZmW32(m, P + 0xD4u + 4u, 0u);
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t e[2] = {ZmR32(m, d + 8u * i), ZmR32(m, d + 8u * i + 4u)};
		zc_vpush(c, P + 0xD4u, e, 8u);
	}
}

// 0x377620 (a0 = P, a1 = contact listener): one event per record; the listener ends in the kart's contact callback.
// The event the callback reads (see the header): built here for every side whose body is owned by a kart.
static void zc_contact_events(ExtZpCtx *c, uint32_t P)
{
	ExtZpMem *m = c->m;
	uint32_t n = zc_vcount(m, P + EXT_ZC_P_CONTACTS), d = zc_vdata(m, P + EXT_ZC_P_CONTACTS);
	for (uint32_t i = 0; i < n; i++)
	{
		uint32_t r = d + EXT_ZC_REC_SIZE * i;
		for (int side = 0; side < 2; side++)
		{
			uint32_t shell = ZmR32(m, r + (side ? EXT_ZC_REC_SHELL_B : EXT_ZC_REC_SHELL_A)), body, kart;
			if (shell == 0u)
				continue;
			body = ZmR32(m, shell + 0x18u);
			kart = (body != 0u) ? ZmR32(m, body + 0x28u) : 0u;
			if ((kart == 0u) || (ZmR32(m, kart) != ZC_KART_VTABLE))
				continue;
			{
				uint32_t ev = ExtZp_Push(c, 0x80u), cp = ExtZp_Push(c, EXT_ZC_EV_SIZE);
				uint32_t otherKey = ZmR32(m, r + (side ? EXT_ZC_REC_KEY_A : EXT_ZC_REC_KEY_B));
				const ZcTriInfo *ti = zc_tri_find((int)otherKey);
				Ps2Vec4 nv = ZmRV(m, r + EXT_ZC_REC_NORMAL);
				// the event object: +0x0C the record, +0x10/+0x14 the shells, +0x20 the point, +0x30 the normal
				ZmW32(m, ev + 0x0Cu, r);
				ZmW32(m, ev + 0x10u, ZmR32(m, r + EXT_ZC_REC_SHELL_A));
				ZmW32(m, ev + 0x14u, ZmR32(m, r + EXT_ZC_REC_SHELL_B));
				ZmWV(m, ev + 0x20u, ZmRV(m, r + EXT_ZC_REC_POINT));
				ZmWV(m, ev + 0x30u, nv);
				if (side)
				{
					// the kart is body B: the normal towards it is the record's turned around
					nv.v[0] = Ps2F_Neg(nv.v[0]);
					nv.v[1] = Ps2F_Neg(nv.v[1]);
					nv.v[2] = Ps2F_Neg(nv.v[2]);
				}
				ZmW32(m, cp + EXT_ZC_EV_SURFACE, (uint32_t)((ti != NULL) ? ti->surface : 1));
				ZmWV(m, cp + EXT_ZC_EV_POINT, ZmRV(m, r + EXT_ZC_REC_POINT));
				ZmWV(m, cp + EXT_ZC_EV_NORMAL, nv);
				ZmW8(m, cp + EXT_ZC_EV_SIDE, (uint8_t)(side ? 0u : 1u)); // the other object's side
				ZmW32(m, cp + EXT_ZC_EV_PAIR, ev);
				ExtZp_Contact_197e30(c, kart, cp);
				ExtZp_Pop(c, EXT_ZC_EV_SIZE);
				ExtZp_Pop(c, 0x80u);
			}
		}
	}
}

// 0x3770E0 (a0 = P, a1 = the pair as one 64-bit word): adds it to P+0xB0 unless there (either order).
static void zc_known_add(ExtZpCtx *c, uint32_t P, uint32_t a, uint32_t b)
{
	zc_pair_add_unique(c, P + EXT_ZC_P_KNOWN_PAIRS, a, b);
}

// 0x36F840 (a0 = P, a1 = pair listener): every overlapping pair (P+0xEC) into the known pairs.
static void zc_pen_known(ExtZpCtx *c, uint32_t P)
{
	ExtZpMem *m = c->m;
	uint32_t n = zc_vcount(m, P + EXT_ZC_P_PENETRATING), d = zc_vdata(m, P + EXT_ZC_P_PENETRATING);
	for (uint32_t i = 0; i < n; i++)
		zc_known_add(c, P, ZmR32(m, d + 8u * i), ZmR32(m, d + 8u * i + 4u));
}

void ExtZc_HeapStats(uint32_t *usedBytes, uint32_t *refused)
{
	*usedBytes = 0u;
	*refused = s_zcHeapFull;
	if (s_zcReady && (s_zcMem != NULL) && (ZmR32(s_zcMem, s_zc.recordArena) == ZC_HEAP_MAGIC))
		*usedBytes = ZmR32(s_zcMem, s_zc.recordArena + 4u) - s_zc.recordArena;
}

void ExtZc_Configure(const ExtZcConfig *cfg)
{
	s_zc = *cfg;
	s_zcReady = (cfg != NULL) && (cfg->recordArena != 0u);
	s_zcHeapFull = 0;
	s_zcMem = NULL;
	s_zcTriCount = 0;
}

int ExtZc_Answer(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in, ExtZpQueryResult *out)
{
	ExtZpMem *m = c->m;
	if (!s_zcReady)
		return 0;
	s_zcMem = m;
	if (kind == EXT_ZP_Q_MATERIAL)
	{
		if (in->func != 0x00390F40u)
			return 0;
		{
			const ZcTriInfo *ti = zc_tri_find((int)in->a[2]);
			Ps2Vec4 v;
			v.v[0] = (ti != NULL) ? zc_bits(ti->n[0]) : 0u;
			v.v[1] = (ti != NULL) ? zc_bits(ti->n[1]) : 0u;
			v.v[2] = (ti != NULL) ? zc_bits(ti->n[2]) : ZC_F_ONE;
			v.v[3] = 0u;
			ZmWV(m, in->a[0], v);
			out->v0 = in->a[0];
		}
		return 1;
	}
	if (kind == EXT_ZP_Q_OTHER)
	{
		// the source's block allocator (a0 bytes, a1 tag) and its release (a0 block, a1 bytes, a2 tag): the arena's heap
		if (in->func == 0x0033C1B0u)
		{
			out->v0 = zc_alloc(m, in->a[0]);
			return 1;
		}
		if (in->func == 0x0033C3B0u)
		{
			zc_release(m, in->a[0], in->a[1]);
			return 1;
		}
		// operator delete (0x4BA240): the port calls it only for objects of the source's operator new, none of them from this heap
		if (in->func == 0x004BA240u)
			return 1;
		// the subspace's virtual function +0x20 (asked by the contact solver 0x3B1960): the detector's tolerance,
		// in the source 0x3B2600 -> 0x380F80 = f32 [[[sub + 0x70] + 0x14] + 0x14] (0 without a detector); the port's own
		if (in->func == 0x003B2600u)
		{
			out->f0 = ExtZp_Step_3b2600(c, in->a[0]);
			return 1;
		}
		return 0;
	}
	if (kind != EXT_ZP_Q_COLLIDE)
		return 0;
	switch (in->func)
	{
	case 0x003B18F0u:
		out->v0 = zc_detect(c, in->a[0]);
		return 1;
	case 0x003771C0u:
		zc_pairs_before(c, in->a[0]);
		return 1;
	case 0x00377290u:
		zc_pairs_after(c, in->a[0]);
		return 1;
	case 0x00377620u:
		zc_contact_events(c, in->a[0]);
		return 1;
	case 0x0036F840u:
		zc_pen_known(c, in->a[0]);
		return 1;
	case 0x003770E0u:
		zc_known_add(c, in->a[0], in->a[1], in->a[2]);
		return 1;
	case 0x00376B90u:
		// registers the moved bodies with the detector's caches (not kept here) and empties two of its lists
		ZmW32(m, in->a[0] + 0x6Cu, 0u);
		ZmW32(m, in->a[0] + 0x78u, 0u);
		return 1;
	case 0x003AFE10u:
		ExtZp_Pen_3afe10(c, in->a[0]);
		return 1;
	case 0x00380FA0u:
		zc_detect_set(c, in);
		return 1;
	case 0x00380FB0u:
		zc_detect_pair(c, in);
		return 1;
	default:
		return 0;
	}
}

// ------------------------------------------------------------------ selftest (no game state)

static int zc_near(Ps2F a, double b, double eps) { return fabs(zc_d(a) - b) <= eps; }

int ExtZc_Selftest(void)
{
	int checks = 0, failures = 0;
	// one measured record of the original (a kart resting on flat ground): centre z 59.66425, gap 0.08875,
	// normal (0,0,1), witness A z = centre - 0.52, witness B z = ground, point = their middle
	ExtZcTri t;
	uint8_t rec[EXT_ZC_REC_SIZE];
	Ps2F ctr[3] = {zc_bits(233.53947), zc_bits(157.09421), zc_bits(59.66425)};
	Ps2F f[4];
	memset(&t, 0, sizeof(t));
	t.a[0] = 230.0; t.a[1] = 150.0; t.a[2] = 59.0555;
	t.b[0] = 240.0; t.b[1] = 150.0; t.b[2] = 59.0555;
	t.c[0] = 230.0; t.c[1] = 165.0; t.c[2] = 59.0555;
	t.key = 12;
	t.solid = 1;
	t.surface = 24;
	checks++;
	if (!ExtZc_SphereTriangle(ctr, 0x3F051EB8u, 0x3DCCCCCDu, &t, rec))
		failures++;
	memcpy(f, rec + EXT_ZC_REC_NORMAL, 16);
	checks++;
	if (!(zc_near(f[0], 0.0, 1e-6) && zc_near(f[1], 0.0, 1e-6) && zc_near(f[2], 1.0, 1e-6) && (f[3] == ZC_F_ONE)))
		failures++;
	memcpy(f, rec + EXT_ZC_REC_GAP, 4);
	checks++;
	if (!zc_near(f[0], 0.08875, 2e-5))
		failures++;
	memcpy(f, rec + EXT_ZC_REC_POINT, 16);
	checks++;
	if (!(zc_near(f[2], 59.09987, 2e-4) && zc_near(f[0], 233.53947, 1e-4)))
		failures++;
	memcpy(f, rec + EXT_ZC_REC_WITNESS_A, 16);
	checks++;
	if (!zc_near(f[2], 59.14425, 1e-4))
		failures++;
	{
		uint32_t kb, ka;
		memcpy(&ka, rec + EXT_ZC_REC_KEY_A, 4);
		memcpy(&kb, rec + EXT_ZC_REC_KEY_B, 4);
		checks++;
		if ((ka != 0xFFFFFFFFu) || (kb != 12u))
			failures++;
	}
	// beyond the tolerance: no record; an edge contact: the normal points from the edge to the centre
	ctr[2] = zc_bits(59.0555 + 0.52 + 0.2);
	checks++;
	if (ExtZc_SphereTriangle(ctr, 0x3F051EB8u, 0x3DCCCCCDu, &t, rec))
		failures++;
	ctr[0] = zc_bits(229.7);
	ctr[2] = zc_bits(59.0555 + 0.3);
	checks++;
	if (!ExtZc_SphereTriangle(ctr, 0x3F051EB8u, 0x3DCCCCCDu, &t, rec))
		failures++;
	memcpy(f, rec + EXT_ZC_REC_NORMAL, 16);
	checks++;
	if (!(zc_near(f[0], -0.7071, 2e-3) && zc_near(f[2], 0.7071, 2e-3)))
		failures++;
	{
		// the heap: a vector grows 1 -> 2 -> 4 -> 8 in blocks with the allocator header, every old block is released,
		// a released block is given out again for its size, and nothing is read from an absent page
		const ExtZcConfig keep = s_zc;
		const int keepReady = s_zcReady;
		const uint32_t keepFull = s_zcHeapFull, v = 0x01000000u, arena = 0x01C00000u;
		ExtZpMem *mem = ExtZpMem_Create();
		ExtZpCtx hc;
		static const uint8_t zero[256] = {0};
		uint32_t firstBlock, d;
		memset(&hc, 0, sizeof(hc));
		hc.m = mem;
		for (uint32_t a = 0; a < (uint32_t)EXT_ZC_ARENA_BYTES; a += (uint32_t)sizeof(zero))
			ExtZpMem_Write(mem, arena + a, zero, (uint32_t)sizeof(zero));
		ExtZpMem_Write(mem, v, zero, 16u);
		memset(&s_zc, 0, sizeof(s_zc));
		s_zc.recordArena = arena;
		s_zcReady = 1;
		s_zcHeapFull = 0;
		for (uint32_t i = 0; i < 5u; i++)
		{
			const uint32_t e[2] = {i, ~i};
			zc_vpush(&hc, v, e, 8u);
		}
		d = ZmR32(mem, v + 8u);
		firstBlock = arena + ZC_HEAP_HEAD; // the capacity-1 block (0x20 bytes), released at the first growth
		checks++;
		if ((ZmR32(mem, v) != 8u) || (ZmR32(mem, v + 4u) != 5u) || (ZmR32(mem, d - 0x10u) != ZC_BLOCK_TAG) || (ZmR32(mem, d - 0xCu) != 64u) ||
		    (ZmR32(mem, d + 8u * 4u) != 4u) || (ZmR32(mem, d + 8u * 4u + 4u) != ~4u))
			failures++;
		checks++;
		// the free list holds (last released first) the capacity-4, -2 and -1 blocks; the two 0x20-byte ones come back
		// for 0x20-byte requests (the capacity-2 block right behind the first one), the third request takes new room
		if ((ZmR32(mem, firstBlock) != ZC_BLOCK_FREED) || (zc_alloc(mem, 0x18u) != firstBlock + 0x20u) || (zc_alloc(mem, 0x20u) != firstBlock) ||
		    (zc_alloc(mem, 0x20u) <= d))
			failures++;
		checks++;
		if ((ExtZpMem_Faults(mem) != 0u) || (s_zcHeapFull != 0u))
			failures++;
		ExtZpMem_Destroy(mem);
		s_zc = keep;
		s_zcReady = keepReady;
		s_zcHeapFull = keepFull;
		s_zcMem = NULL;
	}
	(void)checks;
	return (failures == 0) ? 0 : 1;
}

// the ported penetration handling (parts pa and pb)
#include "native_extzonecontact_pa.c"
#include "native_extzonecontact_pb.c"
