// THE PORTED CHASE CAMERA IN THE RACE (include/platform/native_extchasecam_glue.h).
//
// Included by platform/native_extcam_module.c (unity build: after the external
// collision, the zone port and its start world). Runs only when the track's
// physics.source asks for the port (ExtZp_PortWanted) and its start world holds
// every page the camera's per-frame update reads (measured on the source's own
// memory with the port: CAM_GLUE_PAGES below); otherwise the zone camera of
// platform/native_extcam_module.c stays as it was.
//
// Where the camera reads the kart (offsets of the source's objects, see the
// round's call path report):
//   driver table T = [0x6F49E0]; the camera's kart record KC = [T + 0x20]
//     KC+0x5C vec3 position (the kart's reference point at the ground, metres)
//     KC+0x68 quaternion x y z w (the kart's orientation; rows right, forward, up)
//     KC+0x78 vec3 velocity (m/s)
//   the kart state block S = [KC + 8]
//     +0x28 ground, +0x30 maglev, +0x34 held (the camera keeps its view),
//     +0x3C speed (m/s; in maglev in the chassis plane, else horizontal),
//     +0x40 in the air, +0x44 time in the air (s), +0x48 slide
//   the clock C = [[0x6F3B00] + 4]: +0x8 frame time (ms), +0xC time (ms)
// The rest of what the camera reads is the start world as it was (its own
// object, its parameter block, the camera manager, the field of view).
// What the camera hands back: C+0x2C8 eye, C+0x2BC view direction, C+0x2D4 up
// (all metres / unit, z up), C+0x144 horizontal field of view (degrees).
// Its two collision questions (0x11B270: the ground ray along the camera's up,
// the wall sweep of radius 0.135 m) are answered with Reload's exact line query
// (the sweep's radius is not modelled: noted in the step log as a line).

#include <platform/native_extchasecam_glue.h>

#define CAM_GLUE_DRIVERS 0x006F49E0u
#define CAM_GLUE_CLOCK 0x006F3B00u
#define CAM_GLUE_MANAGER 0x006F3640u
#define CAM_GLUE_VT_CAMERA 0x006CBC70u
#define CAM_GLUE_VIEW 0x01D00000u // the view matrix the update writes (a free page below the port's stack)

// The pages one per-frame update of the camera reads (read trace of the port on the source's memory, the anti-gravity test track).
global_variable const uint32_t s_camGluePages[] = {0x00612000u, 0x006B6000u, 0x006CB000u, 0x006F3000u, 0x006F4000u,
                                                   0x006F5000u, 0x0078A000u, 0x0078B000u, 0x0078E000u, 0x00791000u,
                                                   0x007B9000u, 0x007BA000u, 0x007E9000u, 0x007F0000u, 0x007F1000u,
                                                   0x007FE000u, 0x0088A000u, 0x0088C000u, 0x00C89000u};
#define CAM_GLUE_PAGE_COUNT ((int)(sizeof(s_camGluePages) / sizeof(s_camGluePages[0])))

global_variable struct
{
	const void *level;
	int checked, ready;
	ExtZpMem *m;
	ExtZpCtx ctx;
	uint32_t cam, kc, s, clock;
	uint32_t timeMs;
	double airTime;
	int teleport;
	int reportOn;
	u32 steps, sphereQueries, sphereHits, otherQueries;
	// the answer writes of the current query (step log)
	unsigned char ans[256];
	uint32_t ansLen, ansWrites;
} s_camGlue;

internal void ExtChaseGlue_Report(void)
{
	Platform_Log("[CTR ExtChaseGlue] at exit: %u step(s), sphere/ray questions %u (hits %u), other questions %u\n", s_camGlue.steps,
	             s_camGlue.sphereQueries, s_camGlue.sphereHits, s_camGlue.otherQueries);
}

internal void ExtChaseGlue_WriteF(uint32_t addr, double v)
{
	ZmWF(s_camGlue.m, addr, ExtZp_ToPs2(v));
}

internal void ExtChaseGlue_WriteVec(uint32_t addr, const double v[3])
{
	ExtChaseGlue_WriteF(addr, v[0]);
	ExtChaseGlue_WriteF(addr + 4u, v[1]);
	ExtChaseGlue_WriteF(addr + 8u, v[2]);
}

internal void ExtChaseGlue_ReadVec(uint32_t addr, double v[3])
{
	v[0] = Ps2F_ToDouble(ZmRF(s_camGlue.m, addr));
	v[1] = Ps2F_ToDouble(ZmRF(s_camGlue.m, addr + 4u));
	v[2] = Ps2F_ToDouble(ZmRF(s_camGlue.m, addr + 8u));
}

internal double ExtChaseGlue_Dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

internal void ExtChaseGlue_Cross(const double a[3], const double b[3], double o[3])
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

internal int ExtChaseGlue_Unit(double v[3])
{
	const double n = sqrt(ExtChaseGlue_Dot(v, v));
	if (n < 1e-12)
	{
		return 0;
	}
	v[0] /= n;
	v[1] /= n;
	v[2] /= n;
	return 1;
}

// The quaternion (x, y, z, w) whose matrix (the source's 0x58B9D0: rows of the standard rotation matrix) has the
// rows r0, r1, r2.
internal void ExtChaseGlue_Quat(const double r0[3], const double r1[3], const double r2[3], double q[4])
{
	double R[3][3], tr, s;

	for (int k = 0; k < 3; k++)
	{
		R[0][k] = r0[k];
		R[1][k] = r1[k];
		R[2][k] = r2[k];
	}
	tr = R[0][0] + R[1][1] + R[2][2];

	if (tr > 0.0)
	{
		s = sqrt(tr + 1.0) * 2.0;
		q[3] = 0.25 * s;
		q[0] = (R[2][1] - R[1][2]) / s;
		q[1] = (R[0][2] - R[2][0]) / s;
		q[2] = (R[1][0] - R[0][1]) / s;
	}
	else if ((R[0][0] > R[1][1]) && (R[0][0] > R[2][2]))
	{
		s = sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2.0;
		q[3] = (R[2][1] - R[1][2]) / s;
		q[0] = 0.25 * s;
		q[1] = (R[0][1] + R[1][0]) / s;
		q[2] = (R[0][2] + R[2][0]) / s;
	}
	else if (R[1][1] > R[2][2])
	{
		s = sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2.0;
		q[3] = (R[0][2] - R[2][0]) / s;
		q[0] = (R[0][1] + R[1][0]) / s;
		q[1] = 0.25 * s;
		q[2] = (R[1][2] + R[2][1]) / s;
	}
	else
	{
		s = sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2.0;
		q[3] = (R[1][0] - R[0][1]) / s;
		q[0] = (R[0][2] + R[2][0]) / s;
		q[1] = (R[1][2] + R[2][1]) / s;
		q[2] = 0.25 * s;
	}
}

// ------------------------------------------------------------------ the camera's questions

internal void ExtChaseGlue_AnswerWrite(uint32_t addr, const void *bytes, uint32_t len)
{
	ExtZpMem_Write(s_camGlue.m, addr, bytes, len);
	if (s_camGlue.ansLen + 8u + ((len + 3u) & ~3u) <= sizeof(s_camGlue.ans))
	{
		memcpy(s_camGlue.ans + s_camGlue.ansLen, &addr, 4);
		memcpy(s_camGlue.ans + s_camGlue.ansLen + 4, &len, 4);
		memset(s_camGlue.ans + s_camGlue.ansLen + 8, 0, (len + 3u) & ~3u);
		memcpy(s_camGlue.ans + s_camGlue.ansLen + 8, bytes, len);
		s_camGlue.ansLen += 8u + ((len + 3u) & ~3u);
		s_camGlue.ansWrites++;
	}
}

// 0x11B270 (a0 = start, a1 = end, a2 = mask, a3 = out vec3, f12 = radius; v0 = hit): the first hit of the segment
// on Reload's collision (ground; with the wall mask 0x8000000 also walls) -> its point in a3, v0 = 1.
internal void ExtChaseGlue_AnswerSphere(ExtZpCtx *c, const ExtZpQueryArgs *in, ExtZpQueryResult *out)
{
	double a[3], b[3], ra[3], rb[3], dir[3], len;
	ExtCollLineHit hit;
	const int cls = ((in->a[2] & 0x08000000u) != 0) ? (EXT_COLL_LINE_GROUND | EXT_COLL_LINE_WALL) : EXT_COLL_LINE_GROUND;

	(void)c;
	s_camGlue.sphereQueries++;
	for (int k = 0; k < 3; k++)
	{
		a[k] = Ps2F_ToDouble(ZmRF(s_camGlue.m, in->a[0] + 4u * (uint32_t)k));
		b[k] = Ps2F_ToDouble(ZmRF(s_camGlue.m, in->a[1] + 4u * (uint32_t)k));
	}
	ExtZp_PointFromSource(a, ra);
	ExtZp_PointFromSource(b, rb);
	for (int k = 0; k < 3; k++)
	{
		dir[k] = rb[k] - ra[k];
	}
	len = sqrt(ExtChaseGlue_Dot(dir, dir));
	out->v0 = 0;
	if ((len <= 0.0) || !ExtColl_LineQuery(ra, dir, 0.0, len, cls, 0, &hit))
	{
		return;
	}
	{
		double p[3];
		Ps2F rec[3];
		ExtZp_PointToSource(hit.pos, p);
		rec[0] = ExtZp_ToPs2(p[0]);
		rec[1] = ExtZp_ToPs2(p[1]);
		rec[2] = ExtZp_ToPs2(p[2]);
		ExtChaseGlue_AnswerWrite(in->a[3], rec, sizeof(rec));
	}
	s_camGlue.sphereHits++;
	out->v0 = 1;
}

// The query callback of the camera: the sphere/ray test here (logged in the step log's format), everything else
// as the zone port answers it (rays; other kinds 0, logged there).
internal void ExtChaseGlue_Query(ExtZpCtx *c, int kind, const ExtZpQueryArgs *in, ExtZpQueryResult *out)
{
	if (kind != EXT_ZP_Q_CAMERA_SPHERE)
	{
		s_camGlue.otherQueries++;
		ExtZp_ReloadQuery(c, kind, in, out);
		return;
	}
	s_camGlue.ansLen = 0;
	s_camGlue.ansWrites = 0;
	memset(out, 0, sizeof(*out));
	ExtChaseGlue_AnswerSphere(c, in, out);
	if (ExtZp_LogOn())
	{
		uint32_t head[23];
		unsigned char blob[sizeof(head) + sizeof(s_camGlue.ans)];
		static uint32_t seq;

		head[0] = seq++;
		head[1] = (uint32_t)kind;
		head[2] = in->func;
		memcpy(&head[3], in->a, 32);
		memcpy(&head[11], in->f, 32);
		head[19] = out->v0;
		head[20] = out->v1;
		head[21] = out->f0;
		head[22] = s_camGlue.ansWrites;
		memcpy(blob, head, sizeof(head));
		memcpy(blob + sizeof(head), s_camGlue.ans, s_camGlue.ansLen);
		ExtZp_LogBlob(EXT_ZP_TAG('Q', 'R', 'Y', ' '), blob, (uint32_t)sizeof(head) + s_camGlue.ansLen);
	}
}

// ------------------------------------------------------------------ the level

internal void ExtChaseGlue_LogPages(uint32_t tag)
{
	unsigned char buf[4 + 4096];

	for (int i = 0; i <= CAM_GLUE_PAGE_COUNT; i++)
	{
		const uint32_t pg = (i < CAM_GLUE_PAGE_COUNT) ? s_camGluePages[i] : CAM_GLUE_VIEW;
		uint8_t *bytes = ExtZpMem_Page(s_camGlue.m, pg, 0);

		if (bytes == NULL)
		{
			continue;
		}
		memcpy(buf, &pg, 4);
		memcpy(buf + 4, bytes, 4096);
		ExtZp_LogBlob(tag, buf, sizeof(buf));
	}
}

int ExtChaseGlue_Ready(const void *level)
{
	uint32_t world = 0, missing = 0, firstMissing = 0;
	Ps2F dt = 0;

	if (level == s_camGlue.level && s_camGlue.checked)
	{
		return s_camGlue.ready;
	}
	s_camGlue.level = level;
	s_camGlue.checked = 1;
	s_camGlue.ready = 0;
	s_camGlue.teleport = 1;
	s_camGlue.airTime = 0.0;
	if (!ExtZp_PortWanted())
	{
		return 0; // not a track of the port: nothing, no line
	}
	if (s_camGlue.m == NULL)
	{
		s_camGlue.m = ExtZpMem_Create();
	}
	else
	{
		ExtZpMem_Clear(s_camGlue.m);
	}
	if ((s_camGlue.m == NULL) || !ExtZp_WorldLoad(s_camGlue.m, &world, &dt))
	{
		Platform_Log("[CTR ExtChaseGlue] off: no start world for the camera\n");
		return 0;
	}
	for (int i = 0; i < CAM_GLUE_PAGE_COUNT; i++)
	{
		if (ExtZpMem_Page(s_camGlue.m, s_camGluePages[i], 0) == NULL)
		{
			missing++;
			firstMissing = firstMissing ? firstMissing : s_camGluePages[i];
		}
	}
	if (missing != 0)
	{
		Platform_Log("[CTR ExtChaseGlue] off: the start world lacks %u of the camera's %d pages (first 0x%08X)\n", missing, CAM_GLUE_PAGE_COUNT,
		             firstMissing);
		return 0;
	}
	ExtZp_CtxInit(&s_camGlue.ctx, s_camGlue.m, ExtChaseGlue_Query, NULL);
	s_camGlue.cam = ZmR32(s_camGlue.m, ZmR32(s_camGlue.m, CAM_GLUE_MANAGER) + 0x18u);
	s_camGlue.kc = ZmR32(s_camGlue.m, ZmR32(s_camGlue.m, CAM_GLUE_DRIVERS) + 0x20u);
	s_camGlue.s = ZmR32(s_camGlue.m, s_camGlue.kc + 8u);
	s_camGlue.clock = ZmR32(s_camGlue.m, ZmR32(s_camGlue.m, CAM_GLUE_CLOCK) + 4u);
	s_camGlue.timeMs = ZmR32(s_camGlue.m, s_camGlue.clock + 0xCu);
	if ((ZmR32(s_camGlue.m, s_camGlue.cam + 4u) != CAM_GLUE_VT_CAMERA) || (s_camGlue.kc == 0) || (s_camGlue.s == 0) || (s_camGlue.clock == 0))
	{
		Platform_Log("[CTR ExtChaseGlue] off: the start world's camera objects are not where expected (cam 0x%08X kc 0x%08X)\n", s_camGlue.cam,
		             s_camGlue.kc);
		return 0;
	}
	if (!s_camGlue.reportOn)
	{
		s_camGlue.reportOn = 1;
		Platform_AtExitReport(ExtChaseGlue_Report);
	}
	Platform_Log("[CTR ExtChaseGlue] on: ported chase camera 0x%08X, kart record 0x%08X, state 0x%08X, clock 0x%08X (%d pages)\n",
	             s_camGlue.cam, s_camGlue.kc, s_camGlue.s, s_camGlue.clock, ExtZp_WorldPages());
	s_camGlue.ready = 1;
	return 1;
}

void ExtChaseGlue_OnTeleport(void)
{
	s_camGlue.teleport = 1;
}

// ------------------------------------------------------------------ one frame

int ExtChaseGlue_Step(const void *level, const ExtChaseGlueIn *in, ExtChaseGlueOut *out)
{
	ExtZpMem *m;
	double f[3], u[3], sf[3], su[3], sr[3], q[4], p[3], v[3], vs[3], speed;
	const double scale = ExtZp_Source()->scale;

	if (!ExtChaseGlue_Ready(level) || (in == NULL) || (out == NULL))
	{
		return 0;
	}
	m = s_camGlue.m;

	// the kart's frame in the source's axes: rows right (= forward x up), forward, up
	memcpy(f, in->fwd, sizeof(f));
	memcpy(u, in->up, sizeof(u));
	if (!ExtChaseGlue_Unit(f))
	{
		return 0;
	}
	{
		const double d = ExtChaseGlue_Dot(u, f);
		u[0] -= d * f[0];
		u[1] -= d * f[1];
		u[2] -= d * f[2];
	}
	if (!ExtChaseGlue_Unit(u))
	{
		return 0;
	}
	ExtZp_DirToSource(f, sf);
	ExtZp_DirToSource(u, su);
	ExtChaseGlue_Cross(sf, su, sr);
	ExtChaseGlue_Quat(sr, sf, su, q);
	ExtZp_PointToSource(in->pos, p);
	for (int k = 0; k < 3; k++)
	{
		v[k] = in->vel[k] / scale;
	}
	ExtZp_DirToSource(v, vs);
	if (in->maglev)
	{
		const double d = ExtChaseGlue_Dot(vs, su);
		double w[3];

		w[0] = vs[0] - d * su[0];
		w[1] = vs[1] - d * su[1];
		w[2] = vs[2] - d * su[2];
		speed = sqrt(ExtChaseGlue_Dot(w, w));
	}
	else
	{
		speed = sqrt(vs[0] * vs[0] + vs[1] * vs[1]);
	}
	s_camGlue.airTime = in->ground ? 0.0 : s_camGlue.airTime + (double)in->dtMs * 0.001;

	// the kart record, the state block, the clock
	ExtChaseGlue_WriteVec(s_camGlue.kc + 0x5Cu, p);
	ExtChaseGlue_WriteF(s_camGlue.kc + 0x68u, q[0]);
	ExtChaseGlue_WriteF(s_camGlue.kc + 0x6Cu, q[1]);
	ExtChaseGlue_WriteF(s_camGlue.kc + 0x70u, q[2]);
	ExtChaseGlue_WriteF(s_camGlue.kc + 0x74u, q[3]);
	ExtChaseGlue_WriteVec(s_camGlue.kc + 0x78u, vs);
	ZmW8(m, s_camGlue.s + 0x28u, in->ground ? 1 : 0);
	ZmW8(m, s_camGlue.s + 0x30u, in->maglev ? 1 : 0);
	ZmW8(m, s_camGlue.s + 0x34u, in->freeze ? 1 : 0);
	ExtChaseGlue_WriteF(s_camGlue.s + 0x3Cu, speed);
	ZmW8(m, s_camGlue.s + 0x40u, in->ground ? 0 : 1);
	ExtChaseGlue_WriteF(s_camGlue.s + 0x44u, in->ground ? 0.0 : s_camGlue.airTime);
	ZmW8(m, s_camGlue.s + 0x48u, in->slide ? 1 : 0);
	s_camGlue.timeMs += (uint32_t)in->dtMs;
	ZmW32(m, s_camGlue.clock + 0x8u, (uint32_t)in->dtMs);
	ZmW32(m, s_camGlue.clock + 0xCu, s_camGlue.timeMs);
	if (s_camGlue.teleport)
	{
		ZmW8(m, s_camGlue.cam + 0x14u, 1); // the camera's own teleport flag: it starts afresh at the kart
		s_camGlue.teleport = 0;
	}

	// the step (step log: kind 2, the camera's update 0x122430)
	{
		const int log = ExtZp_LogOn();

		if (log)
		{
			uint32_t head[4], args[17];
			char note[160];

			head[0] = (uint32_t)in->frame;
			head[1] = 0u;
			head[2] = (uint32_t)EXT_ZP_LOG_STEP_CAMERA;
			head[3] = 0x00122430u;
			memset(args, 0, sizeof(args));
			args[0] = s_camGlue.cam;
			args[1] = CAM_GLUE_VIEW;
			ExtZp_LogBegin(EXT_ZP_TAG('S', 'T', 'E', 'P'), head, 4);
			ExtZp_LogBlob(EXT_ZP_TAG('A', 'R', 'G', 'S'), args, sizeof(args));
			snprintf(note, sizeof(note), "CAM=0x%08X KC=0x%08X S=0x%08X CLOCK=0x%08X VW=0x%08X", s_camGlue.cam, s_camGlue.kc, s_camGlue.s,
			         s_camGlue.clock, CAM_GLUE_VIEW);
			ExtZp_LogBlob(EXT_ZP_TAG('N', 'O', 'T', 'E'), note, (uint32_t)strlen(note));
			ExtChaseGlue_LogPages(EXT_ZP_TAG('P', 'R', 'E', ' '));
		}
		ExtZp_Cam_122430(&s_camGlue.ctx, s_camGlue.cam, CAM_GLUE_VIEW, 0u);
		if (log)
		{
			const uint32_t ret[2] = {0u, 0u};

			ExtChaseGlue_LogPages(EXT_ZP_TAG('P', 'O', 'S', 'T'));
			ExtZp_LogBlob(EXT_ZP_TAG('R', 'E', 'T', ' '), ret, sizeof(ret));
			ExtZp_LogEnd();
		}
	}
	s_camGlue.steps++;

	// the picture
	{
		double eye[3], dir[3], up[3];

		ExtChaseGlue_ReadVec(s_camGlue.cam + 0x2C8u, eye);
		ExtChaseGlue_ReadVec(s_camGlue.cam + 0x2BCu, dir);
		ExtChaseGlue_ReadVec(s_camGlue.cam + 0x2D4u, up);
		ExtZp_PointFromSource(eye, out->eye);
		ExtZp_DirFromSource(dir, out->fwd);
		ExtZp_DirFromSource(up, out->up);
		if (!ExtChaseGlue_Unit(out->fwd))
		{
			return 0;
		}
		{
			const double d = ExtChaseGlue_Dot(out->up, out->fwd);
			out->up[0] -= d * out->fwd[0];
			out->up[1] -= d * out->fwd[1];
			out->up[2] -= d * out->fwd[2];
		}
		if (!ExtChaseGlue_Unit(out->up))
		{
			return 0;
		}
		out->fovDeg = Ps2F_ToDouble(ZmRF(m, s_camGlue.cam + 0x144u));
		if (!(out->fovDeg > 1.0 && out->fovDeg < 179.0))
		{
			out->fovDeg = 90.0;
		}
	}
	return 1;
}

// ------------------------------------------------------------------ selftest

void ExtChaseGlue_Selftest(int *n, int *failures)
{
	// the quaternion of a frame, through the port's own quaternion-to-matrix function, gives the frame back
	static const double frames[4][2][3] = {
		{{-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
		{{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
		{{0.6, 0.0, 0.8}, {-0.8, 0.0, 0.6}},
		{{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}},
	};
	ExtZpMem *m = ExtZpMem_Create();
	ExtZpCtx c;

	if (m == NULL)
	{
		(*n)++;
		(*failures)++;
		return;
	}
	ExtZp_CtxInit(&c, m, NULL, NULL);
	for (int i = 0; i < 4; i++)
	{
		double f[3], u[3], r[3], q[4], err = 0.0;
		const uint32_t qa = 0x00100000u, ma = 0x00100100u;

		memcpy(f, frames[i][0], sizeof(f));
		memcpy(u, frames[i][1], sizeof(u));
		ExtChaseGlue_Cross(f, u, r);
		ExtChaseGlue_Quat(r, f, u, q);
		for (int k = 0; k < 4; k++)
		{
			ZmWF(m, qa + 4u * (uint32_t)k, ExtZp_ToPs2(q[k]));
		}
		ExtZp_Cam_58b9d0(&c, ma, qa);
		for (int k = 0; k < 3; k++)
		{
			err = fmax(err, fabs(Ps2F_ToDouble(ZmRF(m, ma + 4u * (uint32_t)k)) - r[k]));
			err = fmax(err, fabs(Ps2F_ToDouble(ZmRF(m, ma + 0x10u + 4u * (uint32_t)k)) - f[k]));
			err = fmax(err, fabs(Ps2F_ToDouble(ZmRF(m, ma + 0x20u + 4u * (uint32_t)k)) - u[k]));
		}
		(*n)++;
		if (err > 1e-5)
		{
			(*failures)++;
			Platform_Log("[CTR ExtChaseGlue] selftest FAIL: frame %d quaternion round trip error %g\n", i, err);
		}
	}
	ExtZpMem_Destroy(m);
}
