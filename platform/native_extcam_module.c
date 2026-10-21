// THE RELOAD CAMERA MODULE FOR ANTI-GRAVITY ZONES (night run 1, built on fix
// round 1 attempt 2; included by platform/native_extcam.c, which holds the
// vector, orientation, projection and occlusion helpers it uses).
//
// Only with --ext-track, camera.follow_up "zones" and zone records; on every
// other track and outside its own frames the retail camera stays exactly as it
// is (nothing is written). What it does per frame of seat 0 while it OWNS the
// camera:
//  - Ownership: from the frame a zone frame of the nav path lies at most
//    own_ahead_frames of driving time ahead of the kart (or the kart is in a
//    zone) until handover_since frames after the last such frame ("hot": in a
//    zone or a zone ahead) - so never later than handover_since frames after
//    the last zone frame; the hand-over to the retail camera happens between
//    frame handover_since and the next (no minimum hold: the tail is the
//    hysteresis).
//  - The kart's orientation (course = moving direction turned by rotCurr.w,
//    up = the drawn kart's up) is followed by a quaternion with a critically
//    damped spring (time constant orient_tau): no Euler angles.
//  - The eye sits in the kart's smoothed frame behind the pivot (pivot_ahead in
//    front of the kart), d behind and h up along the smoothed up, d and h between
//    the near and far values by the kart's share of its top speed (the
//    original's chase camera: 3.05/1.2 m near, 3.7/1.4 m far, 64 units a metre).
//  - The view aims at a point look_ahead in front of the kart's middle so that
//    it sits at (0, frame_y) of the picture (G1 before the original's framing);
//    the camera's up is the smoothed up across the view.
//  - Occlusion: the eye moves on the line to the kart's middle to the largest
//    free share (the measure line's own test, now and occl_ahead frames ahead),
//    by at most max_pull_L kart lengths a frame; in a tube (both side rays hit)
//    it may come closer (min_eye_tube_L instead of min_eye_L).
//  - At most EXT_CAM_TURN_MAX degrees a frame (checked on the drawn up), the eye
//    blended in from the retail one over in_frames and back out by
//    handover_since; at handover_since - 0 the view is half way to the retail one.
// All values are run-time parameters: exttrack.json camera.module { name: value }.

typedef struct
{
	double orientTau;   // s, the spring of the kart's orientation
	double dNear, hNear, dFar, hFar;
	double pivotAhead, lookAhead;
	double speedLo, speedHi; // share of the top speed for near / far
	double rigRate;     // share per 30 Hz frame of the eye's distance and height
	double frameY, frameBox;
	int ownAheadFrames, handoverSince, inFrames, occlAhead;
	double kartLength;  // L, world units
	double maxPullL, minEyeL, minEyeTubeL, grow, tubeRayL;
	double upBlendS;  // s, the linear blend of the target up between off-zone (half world up) and zone (kart up)
	double jumpMaxL;  // the eye moves at most this many kart lengths a frame (G1: 1 L)
	int substeps;     // spring steps per frame
} ExtCamModParams;

global_variable const ExtCamModParams s_extCamModDefaults = {
    .orientTau = 0.06, // critically damped with the target's own turn fed forward: a turning target is followed
                       // without lag, a jump settles to 37 % in 2.15 tau = 0.13 s (original at 0.13 s, up 0.22 s)
    .dNear = 195.0, .hNear = 77.0, .dFar = 237.0, .hFar = 90.0,
    .pivotAhead = 29.0, .lookAhead = 29.0,
    .speedLo = 0.2, .speedHi = 0.7,
    .rigRate = 0.25,
    .frameY = 0.25, .frameBox = 0.42,
    .ownAheadFrames = 25, .handoverSince = 44, .inFrames = 12, .occlAhead = 6,
    .kartLength = 89.9, // L (PLAN NL1 3a): length of the retail kart model of driver 0 in world units
    .maxPullL = 0.6, .minEyeL = 1.0, .minEyeTubeL = 0.6, .grow = 0.04, .tubeRayL = 4.0,
    .upBlendS = 1.0, .jumpMaxL = 0.97, .substeps = 4,
};

typedef struct
{
	double w, x, y, z;
} ExtCamQuat;

global_variable struct
{
	const void *level1;
	ExtCamModParams p;
	int paramsRead;
	int owned;
	int ownStart;      // gGT->timer the ownership began
	int lastZoneFrame; // -1 none on this level
	int lastHotFrame;  // gGT->timer of the last frame in a zone or with a zone ahead
	ExtCamQuat q;      // the smoothed kart orientation
	ExtCamQuat qtPrev; // the target of the frame before (its turn is fed forward)
	int haveQt;
	int teleported;    // ExtCam_OnTeleport: the next frame starts the module's state at the new place
	double tUp;        // 0..1: share of the zone's target up (kart up) against the off-zone one
	double omega[3];   // its angular velocity (rad/s, world)
	double dCur, hCur;
	double sigma;
	double prevKart[3];
	int havePrevKart;
	double weight;
	int reportOn;
	u32 frames, owns, pulled, snaps, raised, blocked, tubeFrames, limited, halved, framingOut, lookaheadOwns, eyeClamped, eyeJumpKept;
} s_extCamMod = {.lastZoneFrame = -1};

// --------------------------------------------------------------------------
// quaternions (w, x, y, z); a frame (f, u) is the rotation with columns
// r = u x f, u, f (local x right, y up, z forward)

internal ExtCamQuat ExtCamQ_Mul(ExtCamQuat a, ExtCamQuat b)
{
	ExtCamQuat q;

	q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	return q;
}

internal ExtCamQuat ExtCamQ_Conj(ExtCamQuat a)
{
	ExtCamQuat q = {a.w, -a.x, -a.y, -a.z};

	return q;
}

internal ExtCamQuat ExtCamQ_Norm(ExtCamQuat a)
{
	const double l = sqrt(a.w * a.w + a.x * a.x + a.y * a.y + a.z * a.z);
	ExtCamQuat q = {1.0, 0.0, 0.0, 0.0};

	if (l > 1e-12)
	{
		q.w = a.w / l;
		q.x = a.x / l;
		q.y = a.y / l;
		q.z = a.z / l;
	}
	return q;
}

internal void ExtCamQ_Rotate(ExtCamQuat q, const double v[3], double out[3])
{
	const ExtCamQuat p = {0.0, v[0], v[1], v[2]};
	const ExtCamQuat r = ExtCamQ_Mul(ExtCamQ_Mul(q, p), ExtCamQ_Conj(q));

	out[0] = r.x;
	out[1] = r.y;
	out[2] = r.z;
}

internal ExtCamQuat ExtCamQ_FromFrame(const double f[3], const double u[3])
{
	double r[3], m[3][3];
	ExtCamQuat q;
	double t;
	int i;

	ExtCam_Cross(u, f, r);
	for (i = 0; i < 3; i++)
	{
		m[i][0] = r[i];
		m[i][1] = u[i];
		m[i][2] = f[i];
	}
	t = m[0][0] + m[1][1] + m[2][2];
	if (t > 0.0)
	{
		const double s = sqrt(t + 1.0) * 2.0;

		q.w = 0.25 * s;
		q.x = (m[2][1] - m[1][2]) / s;
		q.y = (m[0][2] - m[2][0]) / s;
		q.z = (m[1][0] - m[0][1]) / s;
	}
	else if ((m[0][0] > m[1][1]) && (m[0][0] > m[2][2]))
	{
		const double s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;

		q.w = (m[2][1] - m[1][2]) / s;
		q.x = 0.25 * s;
		q.y = (m[0][1] + m[1][0]) / s;
		q.z = (m[0][2] + m[2][0]) / s;
	}
	else if (m[1][1] > m[2][2])
	{
		const double s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;

		q.w = (m[0][2] - m[2][0]) / s;
		q.x = (m[0][1] + m[1][0]) / s;
		q.y = 0.25 * s;
		q.z = (m[1][2] + m[2][1]) / s;
	}
	else
	{
		const double s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;

		q.w = (m[1][0] - m[0][1]) / s;
		q.x = (m[0][2] + m[2][0]) / s;
		q.y = (m[1][2] + m[2][1]) / s;
		q.z = 0.25 * s;
	}
	return ExtCamQ_Norm(q);
}

internal void ExtCamQ_Frame(ExtCamQuat q, double f[3], double u[3])
{
	const double z[3] = {0.0, 0.0, 1.0}, y[3] = {0.0, 1.0, 0.0};

	ExtCamQ_Rotate(q, z, f);
	ExtCamQ_Rotate(q, y, u);
}

// The rotation vector (axis times angle, radians) of q, the shorter way round.
internal void ExtCamQ_Log(ExtCamQuat q, double v[3])
{
	double s, a;

	if (q.w < 0.0)
	{
		q.w = -q.w;
		q.x = -q.x;
		q.y = -q.y;
		q.z = -q.z;
	}
	s = sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
	if (s < 1e-12)
	{
		v[0] = v[1] = v[2] = 0.0;
		return;
	}
	a = 2.0 * atan2(s, q.w);
	v[0] = q.x / s * a;
	v[1] = q.y / s * a;
	v[2] = q.z / s * a;
}

internal ExtCamQuat ExtCamQ_Exp(const double v[3])
{
	const double a = sqrt(ExtCam_Dot(v, v));
	ExtCamQuat q = {1.0, 0.0, 0.0, 0.0};

	if (a > 1e-12)
	{
		const double s = sin(0.5 * a) / a;

		q.w = cos(0.5 * a);
		q.x = v[0] * s;
		q.y = v[1] * s;
		q.z = v[2] * s;
	}
	return q;
}

// One step of the critically damped spring of q towards target (time constant tau, dt seconds).
internal void ExtCamQ_SpringStep(ExtCamQuat *q, double omega[3], ExtCamQuat target, double tau, double dt)
{
	const double k = 1.0 / (tau * tau), c = 2.0 / tau;
	double e[3], dv[3];
	int i;

	ExtCamQ_Log(ExtCamQ_Mul(target, ExtCamQ_Conj(*q)), e);
	for (i = 0; i < 3; i++)
	{
		omega[i] += (k * e[i] - c * omega[i]) * dt;
		dv[i] = omega[i] * dt;
	}
	*q = ExtCamQ_Norm(ExtCamQ_Mul(ExtCamQ_Exp(dv), *q));
}

// The same spring with the target's own angular velocity fed forward (the turn
// from prevTarget to target in dt; a turn over 7 degrees a frame is a jump and
// not fed forward), in `steps` sub-steps: a smoothly turning target is followed
// without the lag of a plain spring.
internal void ExtCamQ_SpringStepFF(ExtCamQuat *q, double omega[3], ExtCamQuat target, ExtCamQuat prevTarget, double tau, double dt, int steps)
{
	const double k = 1.0 / (tau * tau), c = 2.0 / tau, h = dt / (double)steps;
	double wt[3];
	int i, st;

	ExtCamQ_Log(ExtCamQ_Mul(target, ExtCamQ_Conj(prevTarget)), wt);
	if (sqrt(ExtCam_Dot(wt, wt)) > 7.0 * 3.14159265358979323846 / 180.0)
	{
		wt[0] = wt[1] = wt[2] = 0.0;
	}
	for (i = 0; i < 3; i++)
	{
		wt[i] /= dt;
	}
	for (st = 0; st < steps; st++)
	{
		// the target moves on with its own turn: at the end of this sub-step it is that far before `target`
		const double back = -(dt - (double)(st + 1) * h);
		const double bv[3] = {wt[0] * back, wt[1] * back, wt[2] * back};
		const ExtCamQuat tgt = ExtCamQ_Norm(ExtCamQ_Mul(ExtCamQ_Exp(bv), target));
		double e[3], dv[3];

		ExtCamQ_Log(ExtCamQ_Mul(tgt, ExtCamQ_Conj(*q)), e);
		for (i = 0; i < 3; i++)
		{
			omega[i] += (k * e[i] + c * (wt[i] - omega[i])) * h;
			dv[i] = omega[i] * h;
		}
		*q = ExtCamQ_Norm(ExtCamQ_Mul(ExtCamQ_Exp(dv), *q));
	}
}

// --------------------------------------------------------------------------
// parameters (exttrack.json camera.module; missing keys: the defaults)

internal void ExtCamMod_ReadParams(void)
{
	const ExtJsonDoc *doc = ExtTrack_Json();
	const int cam = ExtTrack_Section("camera");
	const int mod = ((doc != NULL) && (cam >= 0)) ? ExtJson_Get(doc, cam, "module") : -1;
	ExtCamModParams *p = &s_extCamMod.p;

	*p = s_extCamModDefaults;
	if ((doc == NULL) || (mod < 0))
	{
		return;
	}
#define EXTCAM_MOD_NUM(field, key) p->field = ExtJson_Number(doc, ExtJson_Get(doc, mod, key), p->field)
#define EXTCAM_MOD_INT(field, key) p->field = (int)ExtJson_Number(doc, ExtJson_Get(doc, mod, key), (double)p->field)
	EXTCAM_MOD_NUM(orientTau, "orient_tau");
	EXTCAM_MOD_NUM(dNear, "d_near");
	EXTCAM_MOD_NUM(hNear, "h_near");
	EXTCAM_MOD_NUM(dFar, "d_far");
	EXTCAM_MOD_NUM(hFar, "h_far");
	EXTCAM_MOD_NUM(pivotAhead, "pivot_ahead");
	EXTCAM_MOD_NUM(lookAhead, "look_ahead");
	EXTCAM_MOD_NUM(speedLo, "speed_lo");
	EXTCAM_MOD_NUM(speedHi, "speed_hi");
	EXTCAM_MOD_NUM(rigRate, "rig_rate");
	EXTCAM_MOD_NUM(frameY, "frame_y");
	EXTCAM_MOD_NUM(frameBox, "frame_box");
	EXTCAM_MOD_INT(ownAheadFrames, "own_ahead_frames");
	EXTCAM_MOD_INT(handoverSince, "handover_since");
	EXTCAM_MOD_INT(inFrames, "in_frames");
	EXTCAM_MOD_INT(occlAhead, "occl_ahead");
	EXTCAM_MOD_NUM(kartLength, "kart_length");
	EXTCAM_MOD_NUM(maxPullL, "max_pull_l");
	EXTCAM_MOD_NUM(minEyeL, "min_eye_l");
	EXTCAM_MOD_NUM(minEyeTubeL, "min_eye_tube_l");
	EXTCAM_MOD_NUM(grow, "grow");
	EXTCAM_MOD_NUM(tubeRayL, "tube_ray_l");
	EXTCAM_MOD_NUM(upBlendS, "up_blend_s");
	EXTCAM_MOD_NUM(jumpMaxL, "jump_max_l");
	EXTCAM_MOD_INT(substeps, "substeps");
#undef EXTCAM_MOD_NUM
#undef EXTCAM_MOD_INT
	// never later than the gate's 45 frames, never shorter than a sane spring
	p->handoverSince = (p->handoverSince > 44) ? 44 : ((p->handoverSince < 2) ? 2 : p->handoverSince);
	p->orientTau = (p->orientTau < 0.03) ? 0.03 : p->orientTau;
	p->inFrames = (p->inFrames < 1) ? 1 : p->inFrames;
	p->kartLength = (p->kartLength < 1.0) ? 1.0 : p->kartLength;
	p->upBlendS = (p->upBlendS < 0.05) ? 0.05 : p->upBlendS;
	p->substeps = (p->substeps < 1) ? 1 : ((p->substeps > 16) ? 16 : p->substeps);
}

internal void ExtCamMod_Report(void)
{
	Platform_Log("[CTR ExtCam] module at exit: %u frame(s) owned in %u ownership(s) (%u begun by the look ahead), pulled in %u, "
	             "snapped in %u, lifted %u, no free place %u, tube %u, step cut %u, halved %u, kart's middle outside the box %u, eye step "
	             "shortened %u, eye step over the limit kept (else occluded) %u\n",
	             s_extCamMod.frames, s_extCamMod.owns, s_extCamMod.lookaheadOwns, s_extCamMod.pulled, s_extCamMod.snaps, s_extCamMod.raised,
	             s_extCamMod.blocked, s_extCamMod.tubeFrames, s_extCamMod.limited, s_extCamMod.halved, s_extCamMod.framingOut,
	             s_extCamMod.eyeClamped, s_extCamMod.eyeJumpKept);
}

// 1 = a zone frame of the nav path at the kart lies within `frames` frames of
// driving at `step` units a frame ahead (the path of the nearest frame, walked
// forward; a frame farther than `near` from the kart does not count as its path).
internal int ExtCamMod_ZoneAhead(const double kart[3], double step, int frames, double nearMax)
{
	short up[3];
	int path = -1, frame = -1;
	const struct NavHeader *hdr;
	const struct NavFrame *arr;
	double budget, last[3];
	int i, n;

	if (!ExtNav_UpAt(kart, up, &path, &frame) || (path < 0) || (path >= 3) || (frame < 0))
	{
		return 0;
	}
	hdr = sdata->NavPath_ptrHeader[path];
	arr = sdata->NavPath_ptrNavFrameArray[path];
	if ((hdr == NULL) || (arr == NULL) || (hdr->numPoints <= 0))
	{
		return 0;
	}
	n = hdr->numPoints;
	last[0] = (double)arr[frame].pos.x;
	last[1] = (double)arr[frame].pos.y;
	last[2] = (double)arr[frame].pos.z;
	{
		const double dv[3] = {last[0] - kart[0], last[1] - kart[1], last[2] - kart[2]};

		if (sqrt(ExtCam_Dot(dv, dv)) > nearMax)
		{
			return 0;
		}
	}
	budget = step * (double)frames;
	for (i = 0; (i < n) && (budget >= 0.0); i++)
	{
		const struct NavFrame *f = &arr[(frame + i) % n];
		const double p[3] = {(double)f->pos.x, (double)f->pos.y, (double)f->pos.z};
		const double dv[3] = {p[0] - last[0], p[1] - last[1], p[2] - last[2]};

		budget -= sqrt(ExtCam_Dot(dv, dv));
		memcpy(last, p, sizeof(p));
		if ((budget >= 0.0) && (ExtNav_FrameZone(f) != 0))
		{
			return 1;
		}
	}
	return 0;
}

// The eye at share s of off from mid (lifted by lift along up), in whole units:
// 1 = at least minEye from mid and nothing between it and mid (the measure
// line's test), also from the predicted kart (moved by ahead) when ahead != NULL.
internal int ExtCamMod_EyeFree(const double mid[3], const double off[3], double s, double lift, const double up[3], double minEye,
                               const double *ahead, double eye[3])
{
	double at, len, dv[3];
	int k;

	for (k = 0; k < 3; k++)
	{
		eye[k] = (double)lround(mid[k] + s * off[k] + lift * up[k]);
		dv[k] = eye[k] - mid[k];
	}
	if (sqrt(ExtCam_Dot(dv, dv)) < minEye)
	{
		return 0;
	}
	if (ExtCam_MeasureOccluded(eye, mid, 0.5 * EXT_CAM_KART_H, &at, &len))
	{
		return 0;
	}
	if (ahead != NULL)
	{
		const double e2[3] = {eye[0] + ahead[0], eye[1] + ahead[1], eye[2] + ahead[2]};
		const double m2[3] = {mid[0] + ahead[0], mid[1] + ahead[1], mid[2] + ahead[2]};

		if (ExtCam_MeasureOccluded(e2, m2, 0.5 * EXT_CAM_KART_H, &at, &len))
		{
			return 0;
		}
	}
	return 1;
}

// 1 = the kart's middle has collision on both sides within reach (a tube).
internal int ExtCamMod_InTube(const double mid[3], const double right[3], double reach)
{
	ExtCollLineHit hit;
	const double neg[3] = {-right[0], -right[1], -right[2]};

	return ExtColl_LineQuery(mid, right, 0.0, reach, EXT_COLL_LINE_GROUND | EXT_COLL_LINE_WALL, 0, &hit) &&
	       ExtColl_LineQuery(mid, neg, 0.0, reach, EXT_COLL_LINE_GROUND | EXT_COLL_LINE_WALL, 0, &hit);
}

// A wanted jump of the kart (a teleport portal; the caller sets the retail
// camera's CAMERA_FLAG_DIRECTION_CHANGED as well): for seat 0, while the module
// owns the camera, its next frame starts at the new place - the orientation
// spring set to the kart, the eye's share back to 1, no step limits against the
// frame before (the jump is wanted). Not owning: nothing (the retail camera
// resets itself). Writes this file's state only.
void ExtCam_OnTeleport(int seat)
{
	if ((seat != 0) || !s_extCamMod.owned)
	{
		return;
	}
	s_extCamMod.teleported = 1;
	s_extCamMod.haveQt = 0;
	s_extCamMod.havePrevKart = 0;
	s_extCamMod.sigma = 1.0;
	s_extCamMod.omega[0] = s_extCamMod.omega[1] = s_extCamMod.omega[2] = 0.0;
}

// game/CAM.c, CAM_FollowDriver_Normal, after the retail camera of this frame is
// in the push buffer (camPos, lookAt: its wanted camera and look point), also in
// a mask grab. Returns at once, writing nothing, unless the module owns the camera.
void ExtCam_Follow(const struct GameTracker *gGT, const struct Driver *d, struct PushBuffer *pb, const Vec3 *camPos, const Vec3 *lookAt)
{
	const ExtCamModParams *p = &s_extCamMod.p;
	GTERegisters savedGte;
	int zone, grab, ahead, havePrev, framed, tail;
	double P[3], n2[3], mid[3], vel[3], course[3], fT[3], uT[3], fS[3], uS[3], rS[3];
	double pivot[3], rel[3], eyeWant[3], off[3], eye[3], E[3], T[3];
	double step, dt, prevEye[3];
	ExtCamOrient retailO, prevO, outO;
	int i;

	(void)camPos;
	(void)lookAt;
	if ((gGT == NULL) || (d == NULL) || (pb == NULL) || !ExtCam_On(gGT->levelID) || !s_extCam.follow || (gGT->numPlyrCurrGame != 1) ||
	    (d->driverID != 0))
	{
		return;
	}
	if (s_extCamMod.level1 != (const void *)gGT->level1)
	{
		const ExtCamQuat one = {1.0, 0.0, 0.0, 0.0};

		s_extCamMod.level1 = gGT->level1;
		s_extCamMod.owned = 0;
		s_extCamMod.lastZoneFrame = -1;
		s_extCamMod.havePrevKart = 0;
		s_extCamMod.q = one;
		s_extCamMod.paramsRead = 0;
	}
	if (!s_extCamMod.paramsRead)
	{
		ExtCamMod_ReadParams();
		s_extCamMod.paramsRead = 1;
	}
	if (ExtColl_ZoneRecords() <= 0)
	{
		return; // a track without zones: retail exactly
	}
	for (i = 0; i < 3; i++)
	{
		P[i] = (double)((&d->posCurr.x)[i] >> 8);
	}
	for (i = 0; i < 3; i++)
	{
		vel[i] = s_extCamMod.havePrevKart ? (P[i] - s_extCamMod.prevKart[i]) : 0.0;
	}
	memcpy(s_extCamMod.prevKart, P, sizeof(P));
	s_extCamMod.havePrevKart = 1;
	step = sqrt(ExtCam_Dot(vel, vel));
	step = (step < 10.0) ? 10.0 : ((step > 200.0) ? 200.0 : step);

	zone = ExtPhys_DriverZone(d);
	grab = (d->kartState == KS_MASK_GRABBED);
	if (zone != 0)
	{
		s_extCamMod.lastZoneFrame = gGT->timer;
	}
	ahead = !grab && ExtCamMod_ZoneAhead(P, step, p->ownAheadFrames, 4.0 * p->kartLength);
	// ownership: a zone or a zone ahead ("hot"), then handoverSince frames of tail
	if ((zone != 0) || ahead)
	{
		s_extCamMod.lastHotFrame = gGT->timer;
	}
	if (!s_extCamMod.owned)
	{
		if ((zone == 0) && !ahead)
		{
			return; // retail exactly
		}
		s_extCamMod.owned = 1;
		s_extCamMod.ownStart = gGT->timer;
		s_extCamMod.weight = 0.0;
		s_extCamMod.sigma = 1.0;
		s_extCamMod.owns++;
		s_extCamMod.lookaheadOwns += (zone == 0) ? 1u : 0u;
		s_extCamMod.omega[0] = s_extCamMod.omega[1] = s_extCamMod.omega[2] = 0.0;
		s_extCamMod.dCur = p->dNear;
		s_extCamMod.hCur = p->hNear;
		s_extCamMod.q.w = 0.0; // set from the kart below
		s_extCamMod.q.x = s_extCamMod.q.y = s_extCamMod.q.z = 0.0;
		s_extCamMod.haveQt = 0;
		s_extCamMod.tUp = (zone != 0) ? 1.0 : 0.0;
		if (!s_extCamMod.reportOn)
		{
			s_extCamMod.reportOn = 1;
			Platform_AtExitReport(ExtCamMod_Report);
		}
	}
	tail = gGT->timer - s_extCamMod.lastHotFrame;
	if (tail > p->handoverSince)
	{
		s_extCamMod.owned = 0;
		return; // retail exactly from here on
	}
	savedGte = gteRegs;

	// the kart: its middle (the measure line's point), its course and up
	for (i = 0; i < 3; i++)
	{
		n2[i] = (double)(&d->AxisAngle2_normalVec.x)[i];
	}
	if (ExtCam_Norm(n2) < 1e-6)
	{
		n2[0] = 0.0;
		n2[1] = 1.0;
		n2[2] = 0.0;
	}
	for (i = 0; i < 3; i++)
	{
		mid[i] = P[i] + 0.5 * EXT_CAM_KART_H * n2[i];
	}
	dt = (double)gGT->elapsedTimeMS * 0.001;
	dt = (dt < 1.0 / 120.0) ? (1.0 / 120.0) : ((dt > 0.1) ? 0.1 : dt);
	// the target up: off a zone half world up (the original's m_PercentWorldUp 0.5), in a zone the kart's up; the
	// share moves linearly over upBlendS (the original's 1 s; KA-T), continuously (no restart on a short visit)
	s_extCamMod.tUp += ((zone != 0) ? 1.0 : -1.0) * dt / p->upBlendS;
	s_extCamMod.tUp = (s_extCamMod.tUp < 0.0) ? 0.0 : ((s_extCamMod.tUp > 1.0) ? 1.0 : s_extCamMod.tUp);
	{
		double uN[3] = {n2[0], n2[1] + 1.0, n2[2]};

		if (ExtCam_Norm(uN) < 1e-6)
		{
			memcpy(uN, n2, sizeof(uN));
		}
		for (i = 0; i < 3; i++)
		{
			uT[i] = (1.0 - s_extCamMod.tUp) * uN[i] + s_extCamMod.tUp * n2[i];
		}
		if (ExtCam_Norm(uT) < 1e-6)
		{
			memcpy(uT, n2, sizeof(uT));
		}
	}
	// the course: the kart's moving direction (matrixMovingDir), across the target up
	course[0] = d->matrixMovingDir.m[0][2] / 4096.0;
	course[1] = d->matrixMovingDir.m[1][2] / 4096.0;
	course[2] = d->matrixMovingDir.m[2][2] / 4096.0;
	{
		const double a = ExtCam_Dot(course, uT);

		for (i = 0; i < 3; i++)
		{
			fT[i] = course[i] - a * uT[i];
		}
	}
	if (ExtCam_Norm(fT) < 1e-6)
	{
		// the course along the up: any direction across it
		const double t[3] = {(fabs(uT[0]) < 0.9) ? 1.0 : 0.0, (fabs(uT[0]) < 0.9) ? 0.0 : 1.0, 0.0};

		ExtCam_Cross(t, uT, fT);
		ExtCam_Norm(fT);
	}
	// 1. the smoothed orientation: quaternion spring with the target's turn fed forward
	{
		const ExtCamQuat qt = ExtCamQ_FromFrame(fT, uT);

		if ((s_extCamMod.q.w == 0.0 && s_extCamMod.q.x == 0.0 && s_extCamMod.q.y == 0.0 && s_extCamMod.q.z == 0.0) || !s_extCamMod.haveQt)
		{
			s_extCamMod.q = qt;
			s_extCamMod.omega[0] = s_extCamMod.omega[1] = s_extCamMod.omega[2] = 0.0;
		}
		else
		{
			ExtCamQ_SpringStepFF(&s_extCamMod.q, s_extCamMod.omega, qt, s_extCamMod.qtPrev, p->orientTau, dt, p->substeps);
		}
		s_extCamMod.qtPrev = qt;
		s_extCamMod.haveQt = 1;
	}
	ExtCamQ_Frame(s_extCamMod.q, fS, uS);
	ExtCam_Cross(uS, fS, rS);
	ExtCam_Norm(rS);
	// 2. the eye in the smoothed frame: distance and height by the speed share, smoothed
	{
		const double top = (d->const_Speed_ClassStat > 0) ? (double)d->const_Speed_ClassStat : 1.0;
		double s = (fabs((double)d->speed) / top - p->speedLo) / ((p->speedHi > p->speedLo) ? (p->speedHi - p->speedLo) : 1.0);
		const double k = 1.0 - pow(1.0 - p->rigRate, dt * 30.0);

		s = (s < 0.0) ? 0.0 : ((s > 1.0) ? 1.0 : s);
		s_extCamMod.dCur += k * ((p->dNear + s * (p->dFar - p->dNear)) - s_extCamMod.dCur);
		s_extCamMod.hCur += k * ((p->hNear + s * (p->hFar - p->hNear)) - s_extCamMod.hCur);
	}
	for (i = 0; i < 3; i++)
	{
		pivot[i] = P[i] + p->pivotAhead * fS[i];
		rel[i] = -s_extCamMod.dCur * fS[i] + s_extCamMod.hCur * uS[i];
		eyeWant[i] = pivot[i] + rel[i];
		off[i] = eyeWant[i] - mid[i];
	}
	havePrev = (s_extCam.prevFollowFrame == gGT->timer - 1) && !s_extCamMod.teleported;
	s_extCamMod.teleported = 0;
	for (i = 0; i < 3; i++)
	{
		prevEye[i] = (double)pb->matrix_Camera.t[i];
	}
	// 3. occlusion: the largest free share now and 3..15 frames ahead (so the pull-in starts early), moved inwards by
	//    at most what the eye's step leaves beside the kart's own move (G1: eye step <= 1 L)
	if (grab)
	{
		for (i = 0; i < 3; i++)
		{
			eye[i] = (double)(&pb->pos.x)[i]; // the mask holds the kart: the eye stays (as retail)
		}
	}
	else
	{
		static const double scales[8] = {1.0, 0.85, 0.7, 0.55, 0.42, 0.32, 0.25, 0.2};
		const int tube = ExtCamMod_InTube(mid, rS, p->tubeRayL * p->kartLength);
		const double minEye = (tube ? p->minEyeTubeL : p->minEyeL) * p->kartLength;
		const double offLen = sqrt(ExtCam_Dot(off, off));
		static const int horizons[4] = {3, 6, 10, 15};
		double sFree = -1.0, sNow = -1.0, sNew;
		int sIdx, hIdx;

		s_extCamMod.tubeFrames += tube ? 1u : 0u;
		for (sIdx = 0; sIdx < 8; sIdx++)
		{
			if (ExtCamMod_EyeFree(mid, off, scales[sIdx], 0.0, uS, minEye, NULL, eye))
			{
				sNow = scales[sIdx];
				break;
			}
		}
		// the largest share free now and at every horizon ahead (occl_ahead scales the horizons: 6 = as listed)
		sFree = sNow;
		for (hIdx = 0; (hIdx < 4) && (sFree > 0.0); hIdx++)
		{
			double aheadV[3];
			double sH = -1.0;

			for (i = 0; i < 3; i++)
			{
				aheadV[i] = vel[i] * (double)horizons[hIdx] * (double)p->occlAhead / 6.0;
			}
			for (sIdx = 0; sIdx < 8; sIdx++)
			{
				if ((scales[sIdx] <= sFree + 1e-9) && ExtCamMod_EyeFree(mid, off, scales[sIdx], 0.0, uS, minEye, aheadV, eye))
				{
					sH = scales[sIdx];
					break;
				}
			}
			if (sH > 0.0)
			{
				sFree = sH;
			}
		}
		if (sNow < 0.0)
		{
			// nothing free on the line: lifted along up, else the closest place (counted)
			const double lift = 0.5 * offLen;
			int found = 0;

			for (sIdx = 2; (sIdx < 6) && !found; sIdx += 2)
			{
				found = ExtCamMod_EyeFree(mid, off, scales[sIdx], lift, uS, minEye, NULL, eye);
			}
			if (found)
			{
				s_extCamMod.raised++;
			}
			else
			{
				s_extCamMod.blocked++;
				(void)ExtCamMod_EyeFree(mid, off, 1.0, 0.0, uS, minEye, NULL, eye);
			}
			s_extCamMod.sigma = 1.0;
		}
		else
		{
			// inwards the eye moves with the kart: the step left beside the kart's move, at most maxPullL
			const double budget = p->jumpMaxL * p->kartLength * 0.95 - sqrt(ExtCam_Dot(vel, vel));
			const double cap = p->maxPullL * p->kartLength;
			const double maxStep = (offLen > 1e-6) ? (((budget < 0.0) ? 0.0 : ((budget > cap) ? cap : budget)) / offLen) : 1.0;

			sNew = s_extCamMod.sigma;
			if (sNew > sFree)
			{
				sNew = (sNew - maxStep > sFree) ? (sNew - maxStep) : sFree;
			}
			else
			{
				sNew = (sNew + p->grow < sFree) ? (sNew + p->grow) : sFree;
			}
			if (!ExtCamMod_EyeFree(mid, off, sNew, 0.0, uS, minEye, NULL, eye))
			{
				// the rate would leave the eye behind something: the largest free share at once
				sNew = sNow;
				(void)ExtCamMod_EyeFree(mid, off, sNew, 0.0, uS, minEye, NULL, eye);
				s_extCamMod.snaps++;
			}
			s_extCamMod.sigma = sNew;
			if (sNew < 1.0)
			{
				s_extCamMod.pulled++;
				s_extCam.pullFrame = gGT->timer;
			}
		}
	}
	// 4. the eye blended with the retail one: in over inFrames, out so that it is retail at handoverSince
	if (tail <= p->handoverSince - p->inFrames)
	{
		s_extCamMod.weight += 1.0 / (double)p->inFrames;
		s_extCamMod.weight = (s_extCamMod.weight > 1.0) ? 1.0 : s_extCamMod.weight;
	}
	else
	{
		const double wOut = (double)(p->handoverSince - tail) / (double)p->inFrames;

		s_extCamMod.weight = (wOut < s_extCamMod.weight) ? wOut : s_extCamMod.weight;
		s_extCamMod.weight = (s_extCamMod.weight < 0.0) ? 0.0 : s_extCamMod.weight;
	}
	for (i = 0; i < 3; i++)
	{
		E[i] = (double)(&pb->pos.x)[i] + s_extCamMod.weight * (eye[i] - (double)(&pb->pos.x)[i]);
		T[i] = mid[i] + p->lookAhead * fS[i];
	}
	// the eye's step against the eye drawn in the frame before: at most jumpMaxL; the shortened eye only when nothing
	// lies between it and the kart's middle (0 occluded frames come first)
	if (havePrev && !grab)
	{
		const double dv[3] = {E[0] - prevEye[0], E[1] - prevEye[1], E[2] - prevEye[2]};
		const double dl = sqrt(ExtCam_Dot(dv, dv));
		const double lim = p->jumpMaxL * p->kartLength;

		if (dl > lim)
		{
			double e2[3], at, len, mv[3];

			for (i = 0; i < 3; i++)
			{
				e2[i] = (double)lround(prevEye[i] + dv[i] * (lim - 0.5) / dl);
				mv[i] = e2[i] - mid[i];
			}
			if (!ExtCam_MeasureOccluded(e2, mid, 0.5 * EXT_CAM_KART_H, &at, &len) && (sqrt(ExtCam_Dot(mv, mv)) >= 0.5 * EXT_CAM_KART_H))
			{
				memcpy(E, e2, sizeof(e2));
				s_extCamMod.eyeClamped++;
			}
			else
			{
				s_extCamMod.eyeJumpKept++;
			}
		}
	}
	{
		MATRIX m;

		ConvertRotToMatrix(&m, &pb->rot);
		ExtCam_OrientOfMatrix(&m, &retailO);
	}
	prevO = retailO;
	if (havePrev)
	{
		ExtCam_OrientOfMatrix(&pb->matrix_Camera, &prevO);
	}
	// 5. the view: aimed (the point look_ahead in front of the kart's middle at frame_y, the kart's middle kept in
	//    the box); at handoverSince half way to the retail view
	framed = ExtCam_AimAt(E, T, uS, fS, pb->distanceToScreen_PREV, pb->rect.h, p->frameY, &outO);
	if (!framed)
	{
		outO = retailO;
	}
	if (tail >= p->handoverSince)
	{
		double axis[3], ang;
		ExtCamOrient half;

		ExtCam_OrientDelta(&outO, &retailO, axis, &ang);
		ExtCam_Turn(outO.f, axis, 0.5 * ang, half.f);
		ExtCam_Turn(outO.u, axis, 0.5 * ang, half.u);
		if (ExtCam_OrientMake(&half, half.f, half.u))
		{
			outO = half;
		}
		framed = 0;
	}
	// 6. at most EXT_CAM_TURN_MAX from the frame before (checked on the drawn up); the kart's middle outside the box:
	//    aim at the middle itself, then up to EXT_CAM_TURN_CHECK
	{
		SVec3 rot;
		int cut = 0, halvings = 0;
		double drawnStep = 0.0;

		if (havePrev)
		{
			double pic[3];

			rot = ExtCam_LimitedRot(&prevO, &outO, &cut, &halvings, &drawnStep);
			ExtCam_InPicture(&rot, E, pb, mid, pic);
			if (framed && ((pic[2] <= 0.0) || (fabs(pic[0]) > p->frameBox) || (fabs(pic[1]) > p->frameBox)))
			{
				ExtCamOrient aimMid;

				if (ExtCam_AimAt(E, mid, uS, fS, pb->distanceToScreen_PREV, pb->rect.h, p->frameY, &aimMid))
				{
					outO = aimMid;
				}
				rot = ExtCam_LimitedRotMax(&prevO, &outO, EXT_CAM_TURN_CHECK, &cut, &halvings, &drawnStep);
				ExtCam_InPicture(&rot, E, pb, mid, pic);
				s_extCamMod.framingOut += ((pic[2] <= 0.0) || (fabs(pic[0]) > p->frameBox) || (fabs(pic[1]) > p->frameBox)) ? 1u : 0u;
			}
			s_extCamMod.limited += cut ? 1u : 0u;
			s_extCamMod.halved += (halvings > 0) ? 1u : 0u;
		}
		else
		{
			rot = ExtCam_RotOf(&outO);
		}
		pb->pos.x = (s16)lround(E[0]);
		pb->pos.y = (s16)lround(E[1]);
		pb->pos.z = (s16)lround(E[2]);
		pb->rot = rot;
	}
	s_extCam.extFrame = gGT->timer;
	s_extCamMod.frames++;
	gteRegs = savedGte;
}

// --------------------------------------------------------------------------
// Selftest of the module (from ExtCam_Selftest): quaternion round trips, the
// spring's time constant, a run over the top (no Euler flip) and the share rule.
internal int ExtCamMod_Selftest(int *n, int *failures)
{
	const int before = *failures;
	int c;

	// frame -> quaternion -> frame
	for (c = 0; c < 64; c++)
	{
		const double a = (double)c * 0.41, b = (double)c * 0.23 - 3.0;
		double f[3] = {cos(a) * cos(b), sin(b), sin(a) * cos(b)}, u0[3] = {0.0, 1.0, 0.0}, u[3], f2[3], u2[3], r[3];
		ExtCamQuat q;

		ExtCam_Cross(f, u0, r);
		if (ExtCam_Norm(r) < 1e-6)
		{
			continue;
		}
		ExtCam_Cross(r, f, u);
		ExtCam_Norm(u);
		ExtCam_Turn(u, f, (double)c * 0.7, u);
		q = ExtCamQ_FromFrame(f, u);
		ExtCamQ_Frame(q, f2, u2);
		(*n)++;
		if ((ExtCam_AngleDeg(f, f2) > 0.01) || (ExtCam_AngleDeg(u, u2) > 0.01))
		{
			(*failures)++;
			printf("ext selftest cam module: frame %d through the quaternion %.4f / %.4f deg off\n", c, ExtCam_AngleDeg(f, f2), ExtCam_AngleDeg(u, u2));
		}
	}
	// the spring: a 90-degree step about the course - after 0.2 s (6 frames of 30 Hz) about 35 % left (critically damped:
	// (1 + t/tau) e^(-t/tau), tau 0.09)
	{
		const double f[3] = {0.0, 0.0, 1.0}, u[3] = {0.0, 1.0, 0.0}, u90[3] = {1.0, 0.0, 0.0};
		ExtCamQuat q = ExtCamQ_FromFrame(f, u), target = ExtCamQ_FromFrame(f, u90);
		double omega[3] = {0.0, 0.0, 0.0}, f2[3], u2[3], left;
		const double tau = 0.09, dt = 1.0 / 30.0;
		int k;

		for (k = 0; k < 6; k++)
		{
			ExtCamQ_SpringStep(&q, omega, target, tau, dt);
		}
		ExtCamQ_Frame(q, f2, u2);
		left = ExtCam_AngleDeg(u2, u90) / 90.0;
		(*n)++;
		if ((left < 0.20) || (left > 0.50))
		{
			(*failures)++;
			printf("ext selftest cam module: after 0.2 s %.2f of the step left (0.20..0.50 expected)\n", left);
		}
		for (k = 0; k < 60; k++)
		{
			ExtCamQ_SpringStep(&q, omega, target, tau, dt);
		}
		ExtCamQ_Frame(q, f2, u2);
		(*n)++;
		if (ExtCam_AngleDeg(u2, u90) > 0.5)
		{
			(*failures)++;
			printf("ext selftest cam module: the spring does not settle (%.2f deg left)\n", ExtCam_AngleDeg(u2, u90));
		}
	}
	// over the top: the kart's up turns about its side by 360 degrees in 120 frames; the smoothed up turns on without
	// a jump (no frame step over 10 degrees) and ends where the kart is
	{
		const double side[3] = {1.0, 0.0, 0.0};
		double f0[3] = {0.0, 0.0, 1.0}, u0[3] = {0.0, 1.0, 0.0};
		ExtCamQuat q = ExtCamQ_FromFrame(f0, u0);
		double omega[3] = {0.0, 0.0, 0.0}, prevU[3], worst = 0.0;
		int k;

		memcpy(prevU, u0, sizeof(prevU));
		for (k = 1; k <= 150; k++)
		{
			const double ang = (k <= 120) ? (2.0 * 3.14159265358979323846 * (double)k / 120.0) : 0.0;
			double f[3], u[3], fs[3], us[3];

			ExtCam_Turn(f0, side, ang, f);
			ExtCam_Turn(u0, side, ang, u);
			ExtCamQ_SpringStep(&q, omega, ExtCamQ_FromFrame(f, u), 0.09, 1.0 / 30.0);
			ExtCamQ_Frame(q, fs, us);
			worst = (ExtCam_AngleDeg(us, prevU) > worst) ? ExtCam_AngleDeg(us, prevU) : worst;
			memcpy(prevU, us, sizeof(prevU));
		}
		(*n)++;
		if ((worst > 10.0) || (ExtCam_AngleDeg(prevU, u0) > 1.0))
		{
			(*failures)++;
			printf("ext selftest cam module: over the top the up stepped %.2f deg at most and ended %.2f deg off\n", worst, ExtCam_AngleDeg(prevU, u0));
		}
	}
	// the spring with the target's turn fed forward: a target turning 3 degrees a frame about the side (a loop) is
	// followed within 1 degree after 20 frames (a plain spring of the same tau lags ~2 tau x rate), and a 90-degree
	// jump still settles (not fed forward)
	{
		const double side[3] = {1.0, 0.0, 0.0};
		const double f0[3] = {0.0, 0.0, 1.0}, u0[3] = {0.0, 1.0, 0.0};
		ExtCamQuat q = ExtCamQ_FromFrame(f0, u0), prevT = q;
		double omega[3] = {0.0, 0.0, 0.0}, worstLate = 0.0;
		int k;

		for (k = 1; k <= 80; k++)
		{
			const double ang = 3.0 * (double)k * 3.14159265358979323846 / 180.0;
			double f[3], u[3], fs[3], us[3];
			ExtCamQuat t;

			ExtCam_Turn(f0, side, ang, f);
			ExtCam_Turn(u0, side, ang, u);
			t = ExtCamQ_FromFrame(f, u);
			ExtCamQ_SpringStepFF(&q, omega, t, prevT, 0.06, 1.0 / 30.0, 4);
			prevT = t;
			ExtCamQ_Frame(q, fs, us);
			if ((k > 20) && (ExtCam_AngleDeg(us, u) > worstLate))
			{
				worstLate = ExtCam_AngleDeg(us, u);
			}
		}
		(*n)++;
		if (worstLate > 1.0)
		{
			(*failures)++;
			printf("ext selftest cam module: fed-forward spring lags %.2f deg behind a 3 deg/frame turn\n", worstLate);
		}
	}
	{
		const double f0[3] = {0.0, 0.0, 1.0}, u0[3] = {0.0, 1.0, 0.0}, u90[3] = {1.0, 0.0, 0.0};
		const ExtCamQuat start = ExtCamQ_FromFrame(f0, u0), t = ExtCamQ_FromFrame(f0, u90);
		ExtCamQuat q = start;
		double omega[3] = {0.0, 0.0, 0.0}, fs[3], us[3];
		int k;

		ExtCamQ_SpringStepFF(&q, omega, t, start, 0.06, 1.0 / 30.0, 4);
		for (k = 0; k < 30; k++)
		{
			ExtCamQ_SpringStepFF(&q, omega, t, t, 0.06, 1.0 / 30.0, 4);
		}
		ExtCamQ_Frame(q, fs, us);
		(*n)++;
		if (ExtCam_AngleDeg(us, u90) > 0.5)
		{
			(*failures)++;
			printf("ext selftest cam module: fed-forward spring does not settle after a jump (%.2f deg left)\n", ExtCam_AngleDeg(us, u90));
		}
	}
	return *failures - before;
}
