// THE SOURCE GAME'S CHASE CAMERA FOR ANTI-GRAVITY ZONES (fix round 2; included by
// platform/native_extcam.c, which holds the vector, orientation and projection
// helpers it uses).
//
// WHAT. The source game has no camera of its own for anti-gravity: one chase
// camera runs all the time, and the kart's maglev state only blends three of its
// parts over one second (the share of world up in the camera's up, the pitch
// lock of the view, the 2D velocity). This file rebuilds that chase camera step
// by step as the source exe runs it (the flow and every value with its address
// are in the round's camera spec, outside this tree) and shows it while the kart
// is in a zone. It has no spring, no look ahead, no occlusion ray to the kart and
// no step limits - the source has none.
//
// THE FRAME (Reload's own, not in the source). Off the zones Reload keeps its
// own camera (the retail one, exactly). So:
//  R1 the chase camera runs for seat 0 on every follow frame of a track with
//     zone records (the countdown included) and writes nothing by itself;
//  R2 it owns the picture from the first maglev frame until its share b is 0
//     again; maglev = the driver's zone (ExtPhys_DriverZone) and, while the
//     race has not started, the source's placing rule (a line 20 m down from
//     the kart: a zone record or no ground at all = maglev);
//  R3 b moves by dt / 1 s towards 1 in maglev and towards 0 off it (the
//     source's own 1 s of m_..._TimeTo/FromMagLev); the picture is the retail
//     eye and view blended with the chase camera's by b, the projection too;
//  R4 a mask grab holds the chase camera (as the source holds it while the kart
//     is reset) and lets b fall; after the grab and after ExtCam_OnTeleport the
//     chase camera starts afresh at the kart;
//  R5 with --dev-ext-cam-log one "[CTR ExtCam] chase" line per frame: every
//     input of the step, b, the retail and the written pose (for the offline
//     model of the measuring tools).
// The retail camera keeps running underneath: what it wrote to the push buffer
// in an owned frame is put back before its next tick (ExtCam_RestoreRetail from
// game/CAM.c), so its own state never sees the chase camera's pose.
//
// Without --ext-track, camera.follow_up "zones" or zone records nothing here
// runs and nothing is written.

int CTR_View_ProjectionDistance(int distanceToScreen); // game/native_view.c

// the source's chase camera parameters (1P near; the values of its camera table)
#define EXT_CHASE_M 64.0 // world units a metre (the track's conversion)
global_variable const double s_chaseNear[3] = {0.0, -3.05, 1.2};     // position behind, near (x side, y ahead, z up)
global_variable const double s_chaseFar[3] = {0.0, -3.7, 1.4};       // position behind, far
global_variable const double s_chaseBoost[3] = {0.0, -4.55, 1.5};    // position behind while boosting
global_variable const double s_chaseWallStart[3] = {0.0, -0.5, 1.0}; // the wall ray's start
#define EXT_CHASE_SPEED_NEAR 5.0
#define EXT_CHASE_SPEED_FAR 19.0
#define EXT_CHASE_LOOK_UP 1.7
#define EXT_CHASE_SPEED_KART_AT 5.0
#define EXT_CHASE_SPEED_VEL_AT 5.0
#define EXT_CHASE_YAW_RATE 0.05
#define EXT_CHASE_BEHIND_RATE 0.05
#define EXT_CHASE_BEHIND_RATE_BOOST 0.1
#define EXT_CHASE_UP_RATE 0.03
#define EXT_CHASE_SLIDE_VEL 0.5
#define EXT_CHASE_MAX_NONSLIDE_VEL 0.5
#define EXT_CHASE_PIVOT_AHEAD 0.45
#define EXT_CHASE_WORLD_UP 0.5
#define EXT_CHASE_FLOOR_MIN 1.25
#define EXT_CHASE_FLOOR_START 2.0
#define EXT_CHASE_FLOOR_BELOW 999.0
#define EXT_CHASE_FLOOR_NONE_ABOVE 1000.0
#define EXT_CHASE_WALL_BELOW 10.0
#define EXT_CHASE_WALL_NONE_ABOVE 15.0
#define EXT_CHASE_WALL_UP 0.5
#define EXT_CHASE_SMOOTH_FULL 0.05
#define EXT_CHASE_SMOOTH_ZERO_BELOW 5.0
#define EXT_CHASE_SMOOTH_FULL_ABOVE 10.0
#define EXT_CHASE_SMOOTH_SLOW 1.0
#define EXT_CHASE_LOOK_UP_WALL 0.3
#define EXT_CHASE_T_BLEND 1.0 // all six maglev transition times
#define EXT_CHASE_AVG_N 10
#define EXT_CHASE_SLIDE_RECENT 1.0
#define EXT_CHASE_VEL_ADJ 0.05
#define EXT_CHASE_LOW_SPEED 0.5
#define EXT_CHASE_FOV 90.0 // horizontal
#define EXT_CHASE_ASPECT 1.33
#define EXT_CHASE_BOOST_FOV 100.0
#define EXT_CHASE_BOOST_FOV_RATE 0.01
#define EXT_CHASE_FOV_CLOSE 0.5
#define EXT_CHASE_COMP 150.0     // every rate times dt[ms] * 0.001 * 150, at most 1
#define EXT_CHASE_PLACE_DOWN 20.0 // the placing line, metres

// the inputs of one step (world units, m/s; the kart's forward and up unit length)
typedef struct
{
	int dtMs;
	int mag, air, spin, slide, reverse, boost;
	double pos[3], prev[3];
	double f[3], u[3];
	int floorHit; // -1 not cast, 0 none, 1 hit at floorT along the ray
	double floorT;
	int wallHit;
	double wallT;
} ExtChaseIn;

typedef struct
{
	int ready;
	double tIn, tOut, t2In, t2Out, slideClock, fov;
	int f2;
	double ring[EXT_CHASE_AVG_N][3];
	int ringN, idx, full, latch;
	double vlast[3], ws, at[3], up[3], rel[3], c[3];
} ExtChaseState;

typedef struct
{
	double eye[3], look[3], up[3], fwd[3]; // up: the view's up (unit, across fwd)
	double fov, speed;
	double pivot[3], a[3], right[3];
	double floorFrom[3], floorDir[3], floorLen;
	double wallFrom[3], wallTo[3];
	int wallOn;
} ExtChaseOut;

internal double ExtChase_Rate(double r, int dtMs)
{
	const double k = r * (double)dtMs * 0.001 * EXT_CHASE_COMP;

	return (k >= 1.0) ? 1.0 : k;
}

// the share pattern of the source: (x - lo) / (hi - lo) clamped; hi <= lo: a step at lo
internal double ExtChase_Ramp(double x, double lo, double hi)
{
	if (hi - lo > 0.0)
	{
		const double t = (x - lo) / (hi - lo);

		return (t <= 0.0) ? 0.0 : ((t >= 1.0) ? 1.0 : t);
	}
	return (lo <= x) ? 1.0 : 0.0;
}

// the share of the maglev value: in maglev the time in it over 1 s, off it 1 - the time out of it over 1 s
internal double ExtChase_Share(double tIn, double tOut, int flag)
{
	return flag ? (tIn / EXT_CHASE_T_BLEND) : (1.0 - tOut / EXT_CHASE_T_BLEND);
}

internal void ExtChase_MixVec(double t, const double off[3], const double on[3], double out[3])
{
	int i;

	for (i = 0; i < 3; i++)
	{
		out[i] = (t >= 1.0) ? on[i] : ((t <= 0.0) ? off[i] : (off[i] * (1.0 - t) + on[i] * t));
	}
}

// zero-safe normalise (the source's FPU gives 0 for a zero vector)
internal void ExtChase_Unit(double v[3])
{
	if (ExtCam_Norm(v) < 1e-12)
	{
		v[0] = v[1] = v[2] = 0.0;
	}
}

// a point of the pivot frame (rows right, a, up; origin pivot), p in metres
internal void ExtChase_FramePoint(const ExtChaseOut *o, const double up[3], const double p[3], double out[3])
{
	int i;

	for (i = 0; i < 3; i++)
	{
		out[i] = o->pivot[i] + (o->right[i] * p[0] + o->a[i] * p[1] + up[i] * p[2]) * EXT_CHASE_M;
	}
}

// the camera's up target: off maglev half the kart's up and half world up, in maglev the kart's up
internal void ExtChase_UpTarget(const ExtChaseState *s, const double ku[3], int mag, double out[3])
{
	const double half[3] = {ku[0] * (1.0 - EXT_CHASE_WORLD_UP), ku[1] * (1.0 - EXT_CHASE_WORLD_UP) + EXT_CHASE_WORLD_UP,
	                        ku[2] * (1.0 - EXT_CHASE_WORLD_UP)};

	ExtChase_MixVec(ExtChase_Share(s->tIn, s->tOut, mag), half, ku, out);
}

// R4: a fresh state at the kart (the timers and the FOV run on)
internal void ExtChase_Reset(ExtChaseState *s, const ExtChaseIn *in)
{
	int i;

	s->ringN = 0;
	s->idx = 0;
	s->full = 0;
	s->latch = 0;
	s->ws = 0.0;
	for (i = 0; i < 3; i++)
	{
		s->vlast[i] = 0.0;
		s->at[i] = in->f[i];
		s->rel[i] = s_chaseNear[i];
		s->c[i] = 0.0;
	}
	ExtChase_UpTarget(s, in->u, in->mag, s->up);
	s->ready = 1;
}

internal void ExtChase_Init(ExtChaseState *s)
{
	memset(s, 0, sizeof(*s));
	s->fov = EXT_CHASE_FOV;
}

// Part one of a step: the timers, the velocity, the view direction and the pivot frame, the camera's up, the eye
// in the frame, the eye in the world before the collision and the floor ray.
internal void ExtChase_StepA(ExtChaseState *s, const ExtChaseIn *in, ExtChaseOut *o)
{
	const int dtMs = in->dtMs;
	const double dt = (double)dtMs * 0.001;
	double v[3], vb[3], sp, w, ap[3], vn[3], ut[3], sig;
	int i;

	if (!s->ready)
	{
		ExtChase_Reset(s, in);
	}
	s->slideClock = in->slide ? 0.0 : (s->slideClock + dt);
	// the maglev timers
	if (in->mag)
	{
		s->tIn += dt;
		s->tOut = 0.0;
		if (!in->air || s->f2)
		{
			s->f2 = 1;
			s->t2Out = 0.0;
			s->t2In += dt;
		}
		else
		{
			s->f2 = 0;
			s->t2Out += dt;
			s->t2In = 0.0;
		}
	}
	else
	{
		s->tIn = 0.0;
		s->tOut += dt;
		s->f2 = 0;
		s->t2Out += dt;
		s->t2In = 0.0;
	}
	// the velocity: the mean of the last 10, its up share 2D off maglev, the last one below 0.5 m/s
	for (i = 0; i < 3; i++)
	{
		v[i] = (dtMs > 0) ? ((in->pos[i] - in->prev[i]) * 1000.0 / (double)dtMs / EXT_CHASE_M) : 0.0;
	}
	if (s->ringN < EXT_CHASE_AVG_N)
	{
		memcpy(s->ring[s->ringN], v, sizeof(v));
		s->ringN++;
	}
	else
	{
		memcpy(s->ring[s->idx], v, sizeof(v));
	}
	s->idx++;
	if (s->idx >= EXT_CHASE_AVG_N)
	{
		s->full = 1;
		s->idx = 0;
	}
	if (s->full)
	{
		int k;

		for (i = 0; i < 3; i++)
		{
			double sum = 0.0;

			for (k = 0; k < EXT_CHASE_AVG_N; k++)
			{
				sum += s->ring[k][i];
			}
			vb[i] = sum / (double)EXT_CHASE_AVG_N;
		}
	}
	else
	{
		memcpy(vb, v, sizeof(v));
	}
	{
		const double t2 = ExtChase_Share(s->t2In, s->t2Out, s->f2);

		vb[1] = (t2 >= 1.0) ? vb[1] : ((t2 <= 0.0) ? 0.0 : (vb[1] * t2));
	}
	sp = sqrt(ExtCam_Dot(vb, vb));
	if ((sp <= EXT_CHASE_LOW_SPEED) || (in->spin && s->latch))
	{
		memcpy(vb, s->vlast, sizeof(vb));
		sp = sqrt(ExtCam_Dot(vb, vb));
		if (in->spin)
		{
			s->latch = 1;
		}
	}
	if (!in->spin)
	{
		s->latch = 0;
	}
	if (in->reverse)
	{
		for (i = 0; i < 3; i++)
		{
			vb[i] = -vb[i];
		}
		sp = -sp;
	}
	memcpy(s->vlast, vb, sizeof(vb));
	o->speed = sp;
	// the view direction and the pivot frame (rows right, a, kart up: the source does not re-orthogonalise them)
	ExtCam_Cross(in->f, in->u, o->right);
	for (i = 0; i < 3; i++)
	{
		o->pivot[i] = in->pos[i] + in->f[i] * EXT_CHASE_PIVOT_AHEAD * EXT_CHASE_M;
	}
	w = ExtChase_Ramp(sp, EXT_CHASE_SPEED_KART_AT, EXT_CHASE_SPEED_VEL_AT);
	if (!in->slide)
	{
		w = (w < EXT_CHASE_MAX_NONSLIDE_VEL) ? w : EXT_CHASE_MAX_NONSLIDE_VEL;
	}
	if (in->spin)
	{
		w = 1.0;
	}
	if (in->slide)
	{
		w = EXT_CHASE_SLIDE_VEL;
	}
	s->ws += (w - s->ws) * ExtChase_Rate(EXT_CHASE_VEL_ADJ, dtMs);
	memcpy(vn, vb, sizeof(vn));
	ExtChase_Unit(vn);
	{
		const double k = ExtChase_Rate(EXT_CHASE_YAW_RATE, dtMs);

		for (i = 0; i < 3; i++)
		{
			const double tgt = in->f[i] * (1.0 - s->ws) + vn[i] * s->ws;

			s->at[i] += (tgt - s->at[i]) * k;
		}
	}
	ExtChase_Unit(s->at);
	memcpy(ap, s->at, sizeof(ap));
	if (ap[1] >= 0.0)
	{
		ap[1] = 0.0; // no pitching up off maglev
		ExtChase_Unit(ap);
	}
	ExtChase_MixVec(ExtChase_Share(s->tIn, s->tOut, in->mag), ap, s->at, o->a);
	ExtChase_Unit(o->a);
	if (ExtCam_Dot(o->a, o->a) < 1e-18)
	{
		memcpy(o->a, in->f, sizeof(o->a));
	}
	// the camera's up (not normalised)
	ExtChase_UpTarget(s, in->u, in->mag, ut);
	{
		const double k = ExtChase_Rate(EXT_CHASE_UP_RATE, dtMs);

		for (i = 0; i < 3; i++)
		{
			s->up[i] += (ut[i] - s->up[i]) * k;
		}
	}
	// the eye in the frame
	sig = ExtChase_Ramp(sp, EXT_CHASE_SPEED_NEAR, EXT_CHASE_SPEED_FAR);
	{
		const double k = ExtChase_Rate(in->boost ? EXT_CHASE_BEHIND_RATE_BOOST : EXT_CHASE_BEHIND_RATE, dtMs);

		for (i = 0; i < 3; i++)
		{
			const double e = in->boost ? s_chaseBoost[i] : (s_chaseNear[i] + (s_chaseFar[i] - s_chaseNear[i]) * sig);

			s->rel[i] += (e - s->rel[i]) * k;
		}
	}
	// E0 and the floor ray: from E0 + 2 m along the camera's up to 1.25 m under E0
	ExtChase_FramePoint(o, in->u, s->rel, o->eye);
	{
		double un[3];

		memcpy(un, s->up, sizeof(un));
		ExtChase_Unit(un);
		for (i = 0; i < 3; i++)
		{
			o->floorFrom[i] = o->eye[i] + un[i] * EXT_CHASE_FLOOR_START * EXT_CHASE_M;
			o->floorDir[i] = -un[i];
		}
		o->floorLen = (EXT_CHASE_FLOOR_START + EXT_CHASE_FLOOR_MIN) * EXT_CHASE_M;
	}
}

// Part two, after the floor hit (in->floorHit / floorT): E1 and the wall ray (cast only under 15 m/s and not
// within 1 s after a slide).
internal void ExtChase_StepB(const ExtChaseState *s, const ExtChaseIn *in, ExtChaseOut *o, double e1[3])
{
	const double sa = fabs(o->speed);
	double un[3];
	int i;

	memcpy(un, s->up, sizeof(un));
	ExtChase_Unit(un);
	memcpy(e1, o->eye, 3 * sizeof(double));
	if ((sa < EXT_CHASE_FLOOR_NONE_ABOVE) && (in->floorHit == 1))
	{
		const double q = ExtChase_Ramp(sa, EXT_CHASE_FLOOR_BELOW, EXT_CHASE_FLOOR_NONE_ABOVE);

		for (i = 0; i < 3; i++)
		{
			const double hit = o->floorFrom[i] + o->floorDir[i] * in->floorT;

			e1[i] = o->eye[i] * q + (hit + un[i] * EXT_CHASE_FLOOR_MIN * EXT_CHASE_M) * (1.0 - q);
		}
	}
	o->wallOn = (sa < EXT_CHASE_WALL_NONE_ABOVE) && !(s->slideClock <= EXT_CHASE_SLIDE_RECENT);
	ExtChase_FramePoint(o, in->u, s_chaseWallStart, o->wallFrom);
	memcpy(o->wallTo, e1, sizeof(o->wallTo));
}

// Part three: the wall result, the smoothed correction, the look point, the view and the FOV.
internal void ExtChase_StepC(ExtChaseState *s, const ExtChaseIn *in, ExtChaseOut *o, const double e1[3])
{
	const int dtMs = in->dtMs;
	const double sa = fabs(o->speed);
	double un[3], e2[3], wallShift = 0.0, loc[3], lp[3], fp[3], sv[3];
	int i;

	memcpy(un, s->up, sizeof(un));
	ExtChase_Unit(un);
	memcpy(e2, e1, sizeof(e2));
	if (o->wallOn && (in->wallHit == 1))
	{
		double d[3];
		const double q = ExtChase_Ramp(sa, EXT_CHASE_WALL_BELOW, EXT_CHASE_WALL_NONE_ABOVE);

		for (i = 0; i < 3; i++)
		{
			d[i] = e1[i] - o->wallFrom[i];
		}
		ExtChase_Unit(d);
		for (i = 0; i < 3; i++)
		{
			e2[i] = e1[i] * q + (o->wallFrom[i] + d[i] * in->wallT) * (1.0 - q);
			d[i] = e2[i] - e1[i];
		}
		wallShift = sqrt(ExtCam_Dot(d, d));
		for (i = 0; i < 3; i++)
		{
			e2[i] += un[i] * wallShift * EXT_CHASE_WALL_UP;
		}
	}
	// the correction, smoothed in the frame (its own inverse: the frame is not orthogonal)
	{
		double r[3], axu[3], rxu[3], axr[3], det;
		const double fs = ExtChase_Ramp(sa, EXT_CHASE_SMOOTH_ZERO_BELOW, EXT_CHASE_SMOOTH_FULL_ABOVE);
		const double k = ExtChase_Rate(EXT_CHASE_SMOOTH_SLOW * (1.0 - fs) + EXT_CHASE_SMOOTH_FULL * fs, dtMs);

		for (i = 0; i < 3; i++)
		{
			r[i] = e2[i] - o->pivot[i];
		}
		ExtCam_Cross(o->a, in->u, axu);
		det = ExtCam_Dot(o->right, axu);
		if (fabs(det) < 1e-12)
		{
			loc[0] = loc[1] = loc[2] = 0.0;
		}
		else
		{
			// Cramer: r = x right + y a + z up
			ExtCam_Cross(r, in->u, rxu);
			ExtCam_Cross(o->a, r, axr);
			loc[0] = ExtCam_Dot(r, axu) / det;
			loc[1] = ExtCam_Dot(o->right, rxu) / det;
			loc[2] = ExtCam_Dot(o->right, axr) / det;
		}
		for (i = 0; i < 3; i++)
		{
			s->c[i] += ((loc[i] - s->rel[i] * EXT_CHASE_M) - s->c[i]) * k;
			lp[i] = s->rel[i] * EXT_CHASE_M + s->c[i];
		}
		for (i = 0; i < 3; i++)
		{
			o->eye[i] = o->pivot[i] + o->right[i] * lp[0] + o->a[i] * lp[1] + in->u[i] * lp[2];
		}
	}
	// the look point: over the pivot along the kart's up, lifted by a share of the wall's shift
	for (i = 0; i < 3; i++)
	{
		o->look[i] = o->pivot[i] + in->u[i] * (EXT_CHASE_LOOK_UP * EXT_CHASE_M + EXT_CHASE_LOOK_UP_WALL * wallShift);
	}
	// the view (look-at): f' = eye - look, s = up x f', the view's up = f' x s
	for (i = 0; i < 3; i++)
	{
		fp[i] = o->eye[i] - o->look[i];
	}
	ExtChase_Unit(fp);
	ExtCam_Cross(un, fp, sv);
	ExtChase_Unit(sv);
	ExtCam_Cross(fp, sv, o->up);
	for (i = 0; i < 3; i++)
	{
		o->fwd[i] = -fp[i];
	}
	// the FOV: towards the boost FOV while boosting, back to the normal one
	if (in->boost)
	{
		s->fov += (EXT_CHASE_BOOST_FOV - s->fov) * ExtChase_Rate(EXT_CHASE_BOOST_FOV_RATE, dtMs);
	}
	else if (s->fov - EXT_CHASE_FOV > EXT_CHASE_FOV_CLOSE)
	{
		s->fov += (EXT_CHASE_FOV - s->fov) * ExtChase_Rate(EXT_CHASE_BOOST_FOV_RATE, dtMs);
	}
	else
	{
		s->fov = EXT_CHASE_FOV;
	}
	o->fov = s->fov;
}

// A step without collision (the selftest): the rays hit nothing.
internal void ExtChase_StepFree(ExtChaseState *s, ExtChaseIn *in, ExtChaseOut *o)
{
	double e1[3];

	in->floorHit = 0;
	in->wallHit = 0;
	ExtChase_StepA(s, in, o);
	ExtChase_StepB(s, in, o, e1);
	ExtChase_StepC(s, in, o, e1);
}

// Reload's projection distance for the source's FOV (horizontal, aspect 1.33), the vertical FOV kept:
// H = (rect.h / 2) / (0x360/0x600 tan(fovY / 2)), tan(fovY / 2) = tan(fovX / 2) / 1.33 - 255.4 for 90 degrees at
// 512 x 216 (Reload's own 256).
internal double ExtChase_HOfFov(double fovDeg, int rectH)
{
	const double ty = tan(fovDeg * 3.14159265358979323846 / 360.0) / EXT_CHASE_ASPECT;

	return ((double)rectH * 0.5) / ((double)0x360 / (double)0x600 * ty);
}

global_variable struct
{
	const void *level1;
	ExtChaseState st;
	double b; // R3: the chase camera's share of the picture
	int wasGrab;
	int teleport;
	int haveRaw;
	ExtChaseOut raw; // the chase camera of the last step (held in a grab)
	int placeMag;    // the placing rule at the level's first frame (-1 not yet)
	int prevOwn;
	// the restore: the retail pose of the last frame this module wrote
	int wrote;
	SVec3 retailPos, retailRot;
	int retailH;
	int reportOn;
	u32 frames, owned, owns, floorHits, wallHits, wallCasts;
} s_extChase = {.placeMag = -1};

void ExtCam_OnTeleport(int seat)
{
	if (seat == 0)
	{
		s_extChase.teleport = 1;
	}
}

internal void ExtChase_Report(void)
{
	Platform_Log("[CTR ExtCam] chase at exit: %u frame(s), owned %u in %u ownership(s), floor hits %u, wall rays %u (hits %u)\n", s_extChase.frames,
	             s_extChase.owned, s_extChase.owns, s_extChase.floorHits, s_extChase.wallCasts, s_extChase.wallHits);
}

// game/CAM.c, CAM_ThTick before anything of the tick: what this module wrote into the push buffer in the frame
// before goes back to the retail camera's own values (pos, rot, projection distance), so the retail camera runs on
// underneath exactly as if it had been shown. Nothing when this module did not write.
void ExtCam_RestoreRetail(const struct GameTracker *gGT, struct PushBuffer *pb)
{
	if (!s_extChase.wrote || (gGT == NULL) || (pb != &gGT->pushBuffer[0]))
	{
		return;
	}
	s_extChase.wrote = 0;
	if ((const void *)gGT->level1 != s_extChase.level1)
	{
		return;
	}
	pb->pos = s_extChase.retailPos;
	pb->rot = s_extChase.retailRot;
	pb->distanceToScreen_PREV = s_extChase.retailH;
}

// The source's placing rule: a line 20 m down along the kart's up from just over it - a zone record (the source's
// maglev surface) or no ground at all = maglev.
internal int ExtChase_PlaceMag(const double pos[3], const double up[3])
{
	double from[3], dir[3];
	ExtCollLineHit hit;
	int i;

	for (i = 0; i < 3; i++)
	{
		from[i] = pos[i] + up[i] * (double)EXT_PHYS_PROBE_TOP;
		dir[i] = -up[i];
	}
	if (!ExtColl_LineQuery(from, dir, 0.0, (double)EXT_PHYS_PROBE_TOP + EXT_CHASE_PLACE_DOWN * EXT_CHASE_M, EXT_COLL_LINE_GROUND, 0, &hit))
	{
		return 1;
	}
	return (hit.zone != 0) ? 1 : 0;
}

// game/CAM.c, CAM_FollowDriver_Normal, after the retail camera of this frame is in the push buffer.
void ExtCam_Follow(const struct GameTracker *gGT, const struct Driver *d, struct PushBuffer *pb, const Vec3 *camPos, const Vec3 *lookAt)
{
	GTERegisters savedGte;
	ExtChaseIn in;
	ExtChaseOut o;
	ExtCamOrient retailO;
	double e1[3];
	int i, own, countdown;

	(void)camPos;
	(void)lookAt;
	if ((gGT == NULL) || (d == NULL) || (pb == NULL) || !ExtCam_On(gGT->levelID) || !s_extCam.follow || (gGT->numPlyrCurrGame != 1) ||
	    (d->driverID != 0) || (pb != &gGT->pushBuffer[0]))
	{
		return;
	}
	if (ExtColl_ZoneRecords() <= 0)
	{
		return; // a track without zones: retail exactly
	}
	if (s_extChase.level1 != (const void *)gGT->level1)
	{
		const void *lv = gGT->level1;
		const int reportOn = s_extChase.reportOn;

		memset(&s_extChase, 0, sizeof(s_extChase));
		s_extChase.level1 = lv;
		s_extChase.placeMag = -1;
		s_extChase.reportOn = reportOn;
		ExtChase_Init(&s_extChase.st);
	}
	if (!s_extChase.reportOn)
	{
		s_extChase.reportOn = 1;
		Platform_AtExitReport(ExtChase_Report);
	}
	savedGte = gteRegs;

	// the inputs: the drawn kart (forward = third column, up = second of its instance matrix), its move, its state
	memset(&in, 0, sizeof(in));
	memset(&o, 0, sizeof(o));
	in.dtMs = gGT->elapsedTimeMS;
	for (i = 0; i < 3; i++)
	{
		in.pos[i] = (double)(&d->posCurr.x)[i] / 256.0;
		in.prev[i] = (double)(&d->posPrev.x)[i] / 256.0;
		in.f[i] = (double)d->instSelf->matrix.m[i][2];
		in.u[i] = (double)d->instSelf->matrix.m[i][1];
	}
	ExtCam_Norm(in.f);
	ExtCam_Norm(in.u);
	countdown = ((gGT->gameMode1 & START_OF_RACE) != 0) || (gGT->trafficLightsTimer > 0);
	if (s_extChase.placeMag < 0)
	{
		s_extChase.placeMag = ExtChase_PlaceMag(in.pos, in.u);
	}
	in.mag = ((ExtPhys_DriverZone(d) != 0) || (countdown && s_extChase.placeMag)) ? 1 : 0;
	in.air = ((d->actionsFlagSet & ACTION_TOUCH_GROUND) == 0) ? 1 : 0;
	in.spin = (d->kartState == KS_SPINNING) ? 1 : 0;
	in.slide = (d->kartState == KS_DRIFTING) ? 1 : 0;
	in.reverse = (d->speed < 0) ? 1 : 0;
	in.boost = (d->reserves > 0) ? 1 : 0;
	in.floorHit = -1;
	in.wallHit = -1;

	// the retail camera of this frame
	s_extChase.retailPos = pb->pos;
	s_extChase.retailRot = pb->rot;
	s_extChase.retailH = pb->distanceToScreen_PREV;
	{
		MATRIX m;

		ConvertRotToMatrix(&m, &pb->rot);
		ExtCam_OrientOfMatrix(&m, &retailO);
	}

	// R4 and the step
	if (d->kartState == KS_MASK_GRABBED)
	{
		s_extChase.b -= (double)in.dtMs * 0.001 / EXT_CHASE_T_BLEND;
		s_extChase.b = (s_extChase.b < 0.0) ? 0.0 : s_extChase.b;
		s_extChase.wasGrab = 1;
		o = s_extChase.raw;
	}
	else
	{
		ExtCollLineHit hit;

		if (s_extChase.wasGrab || s_extChase.teleport || !s_extChase.st.ready)
		{
			ExtChase_Reset(&s_extChase.st, &in);
		}
		s_extChase.wasGrab = 0;
		ExtChase_StepA(&s_extChase.st, &in, &o);
		in.floorHit = 0;
		if ((fabs(o.speed) < EXT_CHASE_FLOOR_NONE_ABOVE) && ExtColl_LineQuery(o.floorFrom, o.floorDir, 0.0, o.floorLen, EXT_COLL_LINE_GROUND, 0, &hit))
		{
			in.floorHit = 1;
			in.floorT = hit.t;
			s_extChase.floorHits++;
		}
		ExtChase_StepB(&s_extChase.st, &in, &o, e1);
		if (o.wallOn)
		{
			double dir[3], len;

			for (i = 0; i < 3; i++)
			{
				dir[i] = o.wallTo[i] - o.wallFrom[i];
			}
			len = sqrt(ExtCam_Dot(dir, dir));
			in.wallHit = 0;
			s_extChase.wallCasts++;
			if ((len > 1e-6) && ExtColl_LineQuery(o.wallFrom, dir, 0.0, len, EXT_COLL_LINE_GROUND | EXT_COLL_LINE_WALL, 0, &hit))
			{
				in.wallHit = 1;
				in.wallT = hit.t;
				s_extChase.wallHits++;
			}
		}
		ExtChase_StepC(&s_extChase.st, &in, &o, e1);
		s_extChase.raw = o;
		s_extChase.haveRaw = 1;
		s_extChase.b += (in.mag ? 1.0 : -1.0) * (double)in.dtMs * 0.001 / EXT_CHASE_T_BLEND;
		s_extChase.b = (s_extChase.b < 0.0) ? 0.0 : ((s_extChase.b > 1.0) ? 1.0 : s_extChase.b);
	}
	s_extChase.frames++;
	own = ((s_extChase.b > 0.0) && s_extChase.haveRaw) ? 1 : 0;

	// R3: the picture - the retail eye, view and projection blended with the chase camera's by b
	if (own)
	{
		ExtCamOrient chaseO, outO;
		double axis[3], ang, eye[3];
		const double b = s_extChase.b;
		const double hFov = (double)CTR_View_ProjectionDistance((int)lround(ExtChase_HOfFov(s_extChase.raw.fov, pb->rect.h)));

		if (!ExtCam_OrientMake(&chaseO, s_extChase.raw.fwd, s_extChase.raw.up))
		{
			chaseO = retailO;
		}
		ExtCam_OrientDelta(&retailO, &chaseO, axis, &ang);
		ExtCam_Turn(retailO.f, axis, ang * b, outO.f);
		ExtCam_Turn(retailO.u, axis, ang * b, outO.u);
		if (!ExtCam_OrientMake(&outO, outO.f, outO.u))
		{
			outO = retailO;
		}
		for (i = 0; i < 3; i++)
		{
			eye[i] = (double)(&s_extChase.retailPos.x)[i] + (s_extChase.raw.eye[i] - (double)(&s_extChase.retailPos.x)[i]) * b;
		}
		pb->pos.x = (s16)lround(eye[0]);
		pb->pos.y = (s16)lround(eye[1]);
		pb->pos.z = (s16)lround(eye[2]);
		pb->rot = ExtCam_RotOf(&outO);
		pb->distanceToScreen_PREV = (int)lround((double)s_extChase.retailH + (hFov - (double)s_extChase.retailH) * b);
		s_extChase.wrote = 1;
		s_extCam.extFrame = gGT->timer;
		s_extChase.owned++;
		s_extChase.owns += s_extChase.prevOwn ? 0u : 1u;
	}
	s_extChase.prevOwn = own;
	if (g_cfg_extCamLogH > 0.0)
	{
		const int st = (in.spin ? 1 : 0) | (in.slide ? 2 : 0) | (in.reverse ? 4 : 0);
		const int fresh = ((d->kartState != KS_MASK_GRABBED) && (s_extChase.st.ringN == 1)) ? 1 : 0;

		Platform_Log("[CTR ExtCam] chase f %d own %d b %.6f mag %d air %d st %d boost %d grab %d tp %d dt %d pos %.5f %.5f %.5f prev %.5f %.5f %.5f "
		             "kf %.6f %.6f %.6f ku %.6f %.6f %.6f rpos %d %d %d rf %.6f %.6f %.6f ru %.6f %.6f %.6f rh %d rect %d %d floor %d %.4f "
		             "wall %d %.4f opos %d %d %d orot %d %d %d oh %d eye %.4f %.4f %.4f look %.4f %.4f %.4f up %.6f %.6f %.6f fov %.4f\n",
		             gGT->timer, own, s_extChase.b, in.mag, in.air, st, in.boost, (d->kartState == KS_MASK_GRABBED) ? 1 : 0, fresh, in.dtMs, in.pos[0],
		             in.pos[1], in.pos[2], in.prev[0], in.prev[1], in.prev[2], in.f[0], in.f[1], in.f[2], in.u[0], in.u[1], in.u[2],
		             (int)s_extChase.retailPos.x, (int)s_extChase.retailPos.y, (int)s_extChase.retailPos.z, retailO.f[0], retailO.f[1], retailO.f[2],
		             retailO.u[0], retailO.u[1], retailO.u[2], s_extChase.retailH, (int)pb->rect.w, (int)pb->rect.h, in.floorHit, in.floorT, in.wallHit,
		             in.wallT, (int)pb->pos.x, (int)pb->pos.y, (int)pb->pos.z, (int)pb->rot.x, (int)pb->rot.y, (int)pb->rot.z,
		             (int)pb->distanceToScreen_PREV, o.eye[0], o.eye[1], o.eye[2], o.look[0], o.look[1], o.look[2], o.up[0], o.up[1], o.up[2], o.fov);
	}
	gteRegs = savedGte;
}

// --------------------------------------------------------------------------
// Selftest of the chase camera (from ExtCam_Selftest): the same cases as the
// offline model's own selftest.
internal void ExtChase_TestIn(ExtChaseIn *in, int i, double speedMps, const double f[3], const double u[3], int mag)
{
	int k;

	memset(in, 0, sizeof(*in));
	in->dtMs = 33;
	in->mag = mag;
	for (k = 0; k < 3; k++)
	{
		in->f[k] = f[k];
		in->u[k] = u[k];
		in->pos[k] = f[k] * speedMps * EXT_CHASE_M * 0.033 * (double)i;
		in->prev[k] = f[k] * speedMps * EXT_CHASE_M * 0.033 * (double)(i - 1);
	}
}

// the kart's middle (H/2 over pos along u) in Reload's picture of the chase view (512 x 216, H of its FOV): y
internal double ExtChase_TestMidY(const ExtChaseOut *o, const double pos[3], const double u[3])
{
	double d[3], vz, vy;
	int i;

	for (i = 0; i < 3; i++)
	{
		d[i] = pos[i] + u[i] * 0.5 * EXT_CAM_KART_H - o->eye[i];
	}
	vz = ExtCam_Dot(d, o->fwd);
	vy = -ExtCam_Dot(d, o->up) * (double)0x360 / (double)0x600;
	return ExtChase_HOfFov(o->fov, 216) * vy / (vz * 108.0);
}

internal int ExtCamMod_Selftest(int *n, int *failures)
{
	const int before = *failures;
	const double fz[3] = {0.0, 0.0, -1.0}, uy[3] = {0.0, 1.0, 0.0};
	const double deg = 3.14159265358979323846 / 180.0;
	ExtChaseState s;
	ExtChaseIn in;
	ExtChaseOut o;
	int i;

#define EXTCHASE_CHECK(cond, ...)                                                                                                                       \
	do                                                                                                                                                 \
	{                                                                                                                                                  \
		(*n)++;                                                                                                                                        \
		if (!(cond))                                                                                                                                   \
		{                                                                                                                                              \
			(*failures)++;                                                                                                                             \
			printf("ext selftest cam chase: " __VA_ARGS__);                                                                                            \
			printf("\n");                                                                                                                              \
		}                                                                                                                                              \
	} while (0)

	EXTCHASE_CHECK(fabs(ExtChase_Rate(0.05, 33) - 0.2475) < 1e-9, "rate 0.05 at 33 ms");
	EXTCHASE_CHECK(fabs(ExtChase_HOfFov(90.0, 216) - 255.36) < 0.05, "H(90) %.3f", ExtChase_HOfFov(90.0, 216));
	// far rig at 25 m/s, flat
	ExtChase_Init(&s);
	for (i = 1; i <= 120; i++)
	{
		ExtChase_TestIn(&in, i, 25.0, fz, uy, 0);
		ExtChase_StepFree(&s, &in, &o);
	}
	{
		const double rel[3] = {o.eye[0] - o.pivot[0], o.eye[1] - o.pivot[1], o.eye[2] - o.pivot[2]};

		EXTCHASE_CHECK((fabs(rel[2] - 3.7 * EXT_CHASE_M) < 0.5) && (fabs(rel[1] - 1.4 * EXT_CHASE_M) < 0.5), "far rig %.2f %.2f", rel[2], rel[1]);
		EXTCHASE_CHECK(fabs(o.look[1] - o.pivot[1] - 1.7 * EXT_CHASE_M) < 1e-6, "look point 1.7 m");
		EXTCHASE_CHECK(fabs(ExtChase_TestMidY(&o, in.pos, uy) - 0.481) < 0.01, "kart middle far %.4f", ExtChase_TestMidY(&o, in.pos, uy));
	}
	// standing in maglev: near rig, the kart's middle at 0.595 (the source's framing)
	ExtChase_Init(&s);
	for (i = 1; i <= 90; i++)
	{
		ExtChase_TestIn(&in, i, 0.0, fz, uy, 1);
		ExtChase_StepFree(&s, &in, &o);
	}
	EXTCHASE_CHECK(fabs(ExtChase_TestMidY(&o, in.pos, uy) - 0.595) < 0.01, "kart middle near %.4f", ExtChase_TestMidY(&o, in.pos, uy));
	// the camera's up: outside half world up, 3 s into maglev the kart's up (tilted 40 degrees)
	{
		const double tilt[3] = {sin(40.0 * deg), cos(40.0 * deg), 0.0};
		double half[3] = {tilt[0] * 0.5, tilt[1] * 0.5 + 0.5, 0.0}, up59[3] = {0}, up75[3] = {0}, up149[3] = {0};

		ExtCam_Norm(half);
		ExtChase_Init(&s);
		for (i = 0; i < 150; i++)
		{
			ExtChase_TestIn(&in, i, 25.0, fz, tilt, i >= 60);
			ExtChase_StepFree(&s, &in, &o);
			if (i == 59)
			{
				memcpy(up59, s.up, sizeof(up59));
			}
			if (i == 75)
			{
				memcpy(up75, s.up, sizeof(up75));
			}
			if (i == 149)
			{
				memcpy(up149, s.up, sizeof(up149));
			}
		}
		EXTCHASE_CHECK(ExtCam_AngleDeg(up59, half) < 0.5, "outside: half world up (%.2f)", ExtCam_AngleDeg(up59, half));
		EXTCHASE_CHECK(ExtCam_AngleDeg(up149, tilt) < 0.5, "maglev: the kart's up (%.2f)", ExtCam_AngleDeg(up149, tilt));
		EXTCHASE_CHECK(ExtCam_AngleDeg(up75, tilt) > 2.0, "0.5 s into maglev still blending (%.2f)", ExtCam_AngleDeg(up75, tilt));
	}
	// the pitch lock: up a 20 degree slope, level outside maglev, free in it
	{
		const double sl[3] = {0.0, sin(20.0 * deg), -cos(20.0 * deg)};
		const double upn[3] = {0.0, cos(20.0 * deg), sin(20.0 * deg)};
		int mag;

		for (mag = 0; mag <= 1; mag++)
		{
			ExtChase_Init(&s);
			for (i = 0; i < 120; i++)
			{
				ExtChase_TestIn(&in, i, 25.0, sl, upn, mag);
				ExtChase_StepFree(&s, &in, &o);
			}
			EXTCHASE_CHECK(fabs(asin(o.a[1]) / deg - (mag ? 20.0 : 0.0)) < 0.5, "pitch %s maglev %.2f", mag ? "in" : "out", asin(o.a[1]) / deg);
		}
	}
	// the boost FOV towards 100 degrees
	ExtChase_Init(&s);
	for (i = 0; i < 3; i++)
	{
		ExtChase_TestIn(&in, i, 25.0, fz, uy, 1);
		in.boost = 1;
		ExtChase_StepFree(&s, &in, &o);
	}
	{
		const double k = 1.0 - ExtChase_Rate(0.01, 33);

		EXTCHASE_CHECK(fabs(o.fov - (100.0 - 10.0 * k * k * k)) < 1e-9, "boost FOV %.4f", o.fov);
	}
#undef EXTCHASE_CHECK
	return *failures - before;
}
