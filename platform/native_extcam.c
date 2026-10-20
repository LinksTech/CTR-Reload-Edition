// THE EXTERNAL TRACK'S CAMERA (--ext-track, see platform/engine_exttrack.c;
// day round 1 of 2026-10-20). Only with the switch AND the "camera" section
// of exttrack.json; without it every camera stays exactly retail.
//
// "intro": "none"   no start fly-in (game/CAM.c, the rule of the track
//                   containers): the fly-in reads the DONOR's camera path
//                   (pointers[ST1_CAMERA_PATH]), which shows the donor's
//                   positions over the external track.
// "end": "spin"     the end of the race turns round the driver (the battle
//                   camera, CAM_EndOfRace_Battle) instead of the donor's
//                   end-of-race cameras (pointers[ST1_CAMERA_EOR], chosen by
//                   the donor's restart points).
// "follow_up": "zones"  B3: while the player's kart is in an anti-gravity zone
//                   (ExtPhys_DriverZone, platform/native_extphys.c) the follow
//                   camera of CAM_FollowDriver_Normal is laid into the kart's
//                   frame (forward, up of the zone) instead of the world's:
//                   the retail offsets of this frame (distance, height, look
//                   point - zoom and speed included) taken in the kart's
//                   heading frame with world up and set again with the kart's
//                   up; the camera's up follows the kart's (at most
//                   EXT_CAM_ROLL_STEP per frame), the camera is pulled in
//                   front of a surface between kart and camera
//                   (ExtColl_ProbeSurface). In and out over EXT_CAM_BLEND
//                   frames. Drawing side of the game state: the push buffer's
//                   position and rotation only (as the retail camera writes
//                   them); what else reads the push buffer (sound, culling)
//                   sees this camera, as with the retail one.
// Log (gate B3, format of T1_M5_P0 section 1): each frame of seat 0 with the
// zone camera in use: "[CTR ExtCam] frame F zone Z clear C up A roll R w W raw C0",
// C = the ray kart -> camera free (1) or not (0) where the camera ends up (after
// the pull-in: what the player sees), C0 the same before the pull-in, A = the
// angle camera up to kart up (degrees), R = the change of the camera's roll
// against the frame before (degrees), W = the blend weight; one summary at exit.

#define EXT_CAM_BLEND 15
#define EXT_CAM_ROLL_STEP 8.0  // degrees per frame
#define EXT_CAM_LIFT 30        // the ray starts this far above the kart (its up)
#define EXT_CAM_MARGIN 40      // the camera stays this far in front of a surface
#define EXT_CAM_MIN_DIST 80

global_variable struct
{
	const void *level1; // the level the blend belongs to
	int intro;      // 1 = no fly-in
	int end;        // 1 = spin at the end
	int follow;     // 1 = zones
	int introLogged;
	int reportOn;
	double weight;  // 0..1 blend
	double up[3];   // the camera's up of the frame before
	int haveUp;
	double rollUp[3]; // the camera's up of the frame before (the roll step)
	int haveRoll;
	u32 frames, clear, pulled;
	double angleMax;
	double rollStepMax;
} s_extCam;

internal void ExtCam_ReadJson(void)
{
	const ExtJsonDoc *doc = ExtTrack_Json();
	const int cam = ExtTrack_Section("camera");

	s_extCam.intro = 0;
	s_extCam.end = 0;
	s_extCam.follow = 0;
	if ((doc == NULL) || (cam < 0))
	{
		return;
	}
	s_extCam.intro = (strcmp(ExtJson_String(doc, ExtJson_Get(doc, cam, "intro"), ""), "none") == 0);
	s_extCam.end = (strcmp(ExtJson_String(doc, ExtJson_Get(doc, cam, "end"), ""), "spin") == 0);
	s_extCam.follow = (strcmp(ExtJson_String(doc, ExtJson_Get(doc, cam, "follow_up"), ""), "zones") == 0);
}

// 1 = this level of the slot uses the camera section (the few keys are read on
// every call; a new level - another level1 - starts the blend from retail).
internal int ExtCam_On(int levelID)
{
	const struct GameTracker *gGT = sdata->gGT;

	if (!ExtTrack_Active() || !ExtTrack_ActiveForLevel(levelID))
	{
		return 0;
	}
	ExtCam_ReadJson();
	if ((gGT != NULL) && ((const void *)gGT->level1 != s_extCam.level1))
	{
		s_extCam.level1 = gGT->level1;
		s_extCam.weight = 0.0;
		s_extCam.haveUp = 0;
		s_extCam.haveRoll = 0;
		s_extCam.introLogged = 0;
	}
	return 1;
}

int ExtCam_NoFlyIn(int levelID)
{
	const int on = ExtCam_On(levelID) && s_extCam.intro;

	if (on && !s_extCam.introLogged)
	{
		s_extCam.introLogged = 1;
		Platform_Log("[CTR ExtCam] level %d: no fly-in (camera.intro none - the path would be the donor's)\n", levelID);
	}
	return on;
}

int ExtCam_EndSpin(int levelID)
{
	const int on = ExtCam_On(levelID) && s_extCam.end;

	if (on)
	{
		Platform_Log("[CTR ExtCam] level %d: end of the race - the camera turns round the driver (camera.end spin)\n", levelID);
	}
	return on;
}

// --------------------------------------------------------------------------
// Vector helpers (doubles; world units)

internal double ExtCam_Dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

internal void ExtCam_Cross(const double a[3], const double b[3], double out[3])
{
	const double x = a[1] * b[2] - a[2] * b[1];
	const double y = a[2] * b[0] - a[0] * b[2];
	const double z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

internal double ExtCam_Norm(double v[3])
{
	const double l = sqrt(ExtCam_Dot(v, v));

	if (l > 1e-9)
	{
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
	return l;
}

internal double ExtCam_AngleDeg(const double a[3], const double b[3])
{
	double c = ExtCam_Dot(a, b) / (sqrt(ExtCam_Dot(a, a) * ExtCam_Dot(b, b)) + 1e-12);

	c = (c > 1.0) ? 1.0 : ((c < -1.0) ? -1.0 : c);
	return acos(c) * 180.0 / 3.14159265358979323846;
}

// The camera's up and right of push-buffer rotation rot as the game builds its
// matrix (ConvertRotToMatrix): the matrix's columns or rows, whichever holds
// the view direction fwd; the sign of up taken at roll 0, where it has a
// positive world y (the retail camera is never upside down: its pitch is within
// +-90 degrees), and kept for the rolled matrix.
internal void ExtCam_AxesOf(const SVec3 *rot, int rep, double up[3], double right[3], double fwd[3])
{
	MATRIX m;
	int k;

	ConvertRotToMatrix(&m, rot);
	for (k = 0; k < 3; k++)
	{
		right[k] = ((rep == 0) ? m.m[k][0] : m.m[0][k]) / 4096.0;
		up[k] = ((rep == 0) ? m.m[k][1] : m.m[1][k]) / 4096.0;
		fwd[k] = ((rep == 0) ? m.m[k][2] : m.m[2][k]) / 4096.0;
	}
}

internal void ExtCam_Axes(const SVec3 *rot, const double fwd[3], double up[3], double right[3])
{
	SVec3 flat = *rot;
	double u0[3], r0[3], f0[3], u1[3], r1[3], f1[3];
	int rep;
	double sign;
	int k;

	flat.z = 0;
	ExtCam_AxesOf(&flat, 0, u0, r0, f0);
	ExtCam_AxesOf(&flat, 1, u1, r1, f1);
	rep = (fabs(ExtCam_Dot(f0, fwd)) >= fabs(ExtCam_Dot(f1, fwd))) ? 0 : 1;
	sign = (((rep == 0) ? u0[1] : u1[1]) < 0.0) ? -1.0 : 1.0;
	ExtCam_AxesOf(rot, rep, up, right, f0);
	for (k = 0; k < 3; k++)
	{
		up[k] *= sign;
	}
}

// The roll (game units, 4096 = 360 degrees) that turns the camera of yaw/pitch
// rot (roll 0) so its up comes closest to want.
internal s16 ExtCam_RollFor(SVec3 rot, const double fwd[3], const double want[3])
{
	double up0[3], right0[3], up1[3], right1[3], p[3];
	double a;
	double sign;

	rot.z = 0;
	ExtCam_Axes(&rot, fwd, up0, right0);
	// which way a positive roll turns the up
	rot.z = 256;
	ExtCam_Axes(&rot, fwd, up1, right1);
	sign = (ExtCam_Dot(up1, right0) >= 0.0) ? 1.0 : -1.0;
	// want in the plane across fwd
	a = ExtCam_Dot(want, fwd);
	p[0] = want[0] - a * fwd[0];
	p[1] = want[1] - a * fwd[1];
	p[2] = want[2] - a * fwd[2];
	if (ExtCam_Norm(p) < 1e-6)
	{
		return 0;
	}
	a = atan2(ExtCam_Dot(p, right0), ExtCam_Dot(p, up0)) * sign;
	return (s16)((int)lround(a * 4096.0 / (2.0 * 3.14159265358979323846)) & 0xfff);
}

internal void ExtCam_Report(void)
{
	Platform_Log("[CTR ExtCam] at exit: %u zone camera frame(s) of seat 0, ray free %u (%.1f %%), pulled in %u, angle camera up to kart up max %.1f deg, "
	             "roll step max %.1f deg\n",
	             s_extCam.frames, s_extCam.clear, (s_extCam.frames > 0u) ? 100.0 * (double)s_extCam.clear / (double)s_extCam.frames : 0.0, s_extCam.pulled,
	             s_extCam.angleMax, s_extCam.rollStepMax);
}

// 1 = the ray from a to b is free of surfaces (both ways: a surface facing
// either end counts); *t = the share of a->b to the first surface facing a.
internal int ExtCam_RayFree(const double a[3], const double b[3], double *t)
{
	double d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const double len = ExtCam_Norm(d);
	SVec3 from, dir, from2, dir2;
	ExtCollHit hit;
	int isFree = 1;

	*t = 1.0;
	if (len < 1.0)
	{
		return 1;
	}
	from = (SVec3){(s16)a[0], (s16)a[1], (s16)a[2]};
	dir = (SVec3){(s16)(d[0] * 4096.0), (s16)(d[1] * 4096.0), (s16)(d[2] * 4096.0)};
	if (ExtColl_ProbeSurface(&from, &dir, (int)len, &hit))
	{
		const double h[3] = {(double)hit.pos.x - a[0], (double)hit.pos.y - a[1], (double)hit.pos.z - a[2]};

		*t = sqrt(ExtCam_Dot(h, h)) / len;
		isFree = 0;
	}
	from2 = (SVec3){(s16)b[0], (s16)b[1], (s16)b[2]};
	dir2 = (SVec3){(s16)(-dir.x), (s16)(-dir.y), (s16)(-dir.z)};
	if (ExtColl_ProbeSurface(&from2, &dir2, (int)len, &hit))
	{
		isFree = 0;
	}
	return isFree;
}

// game/CAM.c, CAM_FollowDriver_Normal, after the retail camera of this frame
// is in the push buffer (camPos, lookAt: its wanted camera and look point).
void ExtCam_Follow(const struct GameTracker *gGT, const struct Driver *d, struct PushBuffer *pb, const Vec3 *camPos, const Vec3 *lookAt)
{
	SVec3 su, sf;
	int zone;
	double P[3], U[3], F[3], R[3], Fh[3], Rh[3];
	const double Y[3] = {0.0, 1.0, 0.0};
	double oc[3], ol[3], C[3], L[3], Ct[3], Lt[3], Ut[3];
	double w;
	int k;

	if ((gGT == NULL) || (d == NULL) || (pb == NULL) || (d->instSelf == NULL) || !ExtCam_On(gGT->levelID) || !s_extCam.follow ||
	    (gGT->numPlyrCurrGame != 1) || (d->driverID != 0))
	{
		return;
	}
	zone = ExtPhys_DriverZone(d);
	s_extCam.weight += (zone != 0) ? (1.0 / EXT_CAM_BLEND) : (-1.0 / EXT_CAM_BLEND);
	s_extCam.weight = (s_extCam.weight < 0.0) ? 0.0 : ((s_extCam.weight > 1.0) ? 1.0 : s_extCam.weight);
	if (s_extCam.weight <= 0.0)
	{
		s_extCam.haveUp = 0;
		s_extCam.haveRoll = 0;
		return; // retail exactly
	}
	if (!s_extCam.reportOn)
	{
		s_extCam.reportOn = 1;
		Platform_AtExitReport(ExtCam_Report);
	}
	w = s_extCam.weight;
	ExtPhys_DriverUp(d, &su);
	ExtPhys_DriverForward(d, &sf);
	P[0] = (double)d->instSelf->matrix.t[0];
	P[1] = (double)d->instSelf->matrix.t[1];
	P[2] = (double)d->instSelf->matrix.t[2];
	U[0] = su.x / 4096.0;
	U[1] = su.y / 4096.0;
	U[2] = su.z / 4096.0;
	F[0] = sf.x / 4096.0;
	F[1] = sf.y / 4096.0;
	F[2] = sf.z / 4096.0;
	ExtCam_Norm(U);
	// forward across up, right of both
	{
		const double a = ExtCam_Dot(F, U);

		for (k = 0; k < 3; k++)
		{
			F[k] -= a * U[k];
		}
	}
	if (ExtCam_Norm(F) < 1e-6)
	{
		return;
	}
	ExtCam_Cross(F, U, R);
	// the retail frame: the heading on the ground plane, world up
	Fh[0] = F[0];
	Fh[1] = 0.0;
	Fh[2] = F[2];
	if (ExtCam_Norm(Fh) < 1e-3)
	{
		// facing straight up or down: the heading of the retail camera itself
		Fh[0] = (double)(lookAt->x - camPos->x);
		Fh[1] = 0.0;
		Fh[2] = (double)(lookAt->z - camPos->z);
		if (ExtCam_Norm(Fh) < 1e-3)
		{
			return;
		}
	}
	ExtCam_Cross(Fh, Y, Rh);
	for (k = 0; k < 3; k++)
	{
		oc[k] = (double)(&camPos->x)[k] - P[k];
		ol[k] = (double)(&lookAt->x)[k] - P[k];
	}
	{
		const double ca = ExtCam_Dot(oc, Fh), cb = ExtCam_Dot(oc, Y), cc = ExtCam_Dot(oc, Rh);
		const double la = ExtCam_Dot(ol, Fh), lb = ExtCam_Dot(ol, Y), lc = ExtCam_Dot(ol, Rh);

		for (k = 0; k < 3; k++)
		{
			Ct[k] = P[k] + ca * F[k] + cb * U[k] + cc * R[k];
			Lt[k] = P[k] + la * F[k] + lb * U[k] + lc * R[k];
			C[k] = (1.0 - w) * (double)(&camPos->x)[k] + w * Ct[k];
			L[k] = (1.0 - w) * (double)(&lookAt->x)[k] + w * Lt[k];
			Ut[k] = (1.0 - w) * Y[k] + w * U[k];
		}
	}
	ExtCam_Norm(Ut);
	// the ray kart -> camera: pull the camera in front of a surface
	{
		double A[3], t;
		int isFree;
		int rawFree;

		for (k = 0; k < 3; k++)
		{
			A[k] = P[k] + EXT_CAM_LIFT * U[k];
		}
		rawFree = ExtCam_RayFree(A, C, &t);
		isFree = rawFree;
		if (!isFree && (t < 1.0))
		{
			double v[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
			const double len = ExtCam_Norm(v);
			double keep = t * len - EXT_CAM_MARGIN;

			keep = (keep < EXT_CAM_MIN_DIST) ? EXT_CAM_MIN_DIST : keep;
			if (keep < len)
			{
				for (k = 0; k < 3; k++)
				{
					C[k] = A[k] + keep * v[k];
				}
				s_extCam.pulled++;
			}
			// what the player sees: the ray to the camera where it is now
			isFree = ExtCam_RayFree(A, C, &t);
		}
		s_extCam.clear += isFree ? 1u : 0u;
		s_extCam.frames++;
		// the camera's up: towards the wanted one, at most EXT_CAM_ROLL_STEP per frame
		if (!s_extCam.haveUp)
		{
			memcpy(s_extCam.up, Y, sizeof(Y));
			s_extCam.haveUp = 1;
		}
		{
			const double ang = ExtCam_AngleDeg(s_extCam.up, Ut);
			const double f = (ang > EXT_CAM_ROLL_STEP) ? (EXT_CAM_ROLL_STEP / ang) : 1.0;

			for (k = 0; k < 3; k++)
			{
				s_extCam.up[k] += f * (Ut[k] - s_extCam.up[k]);
			}
			ExtCam_Norm(s_extCam.up);
		}
		// the push buffer: position, yaw and pitch as the retail code makes them, the roll from the up
		{
			const double dir[3] = {C[0] - L[0], C[1] - L[1], C[2] - L[2]};
			double fwd[3] = {L[0] - C[0], L[1] - C[1], L[2] - C[2]};
			const int horiz = (int)sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
			SVec3 rot;
			double camUp[3], camRight[3];

			ExtCam_Norm(fwd);
			pb->pos.x = (s16)lround(C[0]);
			pb->pos.y = (s16)lround(C[1]);
			pb->pos.z = (s16)lround(C[2]);
			rot.y = (s16)ratan2((int)dir[0], (int)dir[2]);
			rot.x = (s16)(0x800 - ratan2((int)dir[1], horiz));
			rot.z = ExtCam_RollFor(rot, fwd, s_extCam.up);
			pb->rot = rot;
			ExtCam_Axes(&rot, fwd, camUp, camRight);
			{
				const double a = ExtCam_AngleDeg(camUp, U);
				double step = 0.0;

				// THE ROLL STEP as one sees it: the turn of the camera's up about its
				// view direction since the frame before - both ups laid into the plane
				// across this frame's view direction. (Anlauf 1 took the difference of
				// the push buffer's roll angle: near a steep view the yaw of ratan2
				// swings and the roll angle turns the other way, the camera itself
				// does not - run b2: 180-degree "flips" with the up 1-2 degrees off.)
				if (s_extCam.haveRoll)
				{
					double p0[3], p1[3];
					const double d0 = ExtCam_Dot(s_extCam.rollUp, fwd);
					const double d1 = ExtCam_Dot(camUp, fwd);

					for (k = 0; k < 3; k++)
					{
						p0[k] = s_extCam.rollUp[k] - d0 * fwd[k];
						p1[k] = camUp[k] - d1 * fwd[k];
					}
					if ((ExtCam_Norm(p0) > 1e-6) && (ExtCam_Norm(p1) > 1e-6))
					{
						step = ExtCam_AngleDeg(p0, p1);
					}
				}
				memcpy(s_extCam.rollUp, camUp, sizeof(camUp));
				s_extCam.angleMax = (a > s_extCam.angleMax) ? a : s_extCam.angleMax;
				s_extCam.rollStepMax = (step > s_extCam.rollStepMax) ? step : s_extCam.rollStepMax;
				s_extCam.haveRoll = 1;
				Platform_Log("[CTR ExtCam] frame %d zone %d clear %d up %.1f roll %.1f w %.2f raw %d\n", gGT->timer, zone, isFree, a, step, w, rawFree);
			}
		}
	}
}

// --------------------------------------------------------------------------
// Selftest (called by ExtJson_Selftest, --ext-selftest json): the roll the
// camera gets turns the game's own camera matrix (ConvertRotToMatrix) so its up
// meets the wanted up, for views from level to steep and ups all round.
int ExtCam_Selftest(int *n, int *failures)
{
	static const int yaws[3] = {0, 1000, 3000};
	static const int pitches[3] = {-600, 0, 700};
	const int before = *failures;
	int yi, pi, ai;

	for (yi = 0; yi < 3; yi++)
	{
		for (pi = 0; pi < 3; pi++)
		{
			for (ai = 0; ai < 8; ai++)
			{
				SVec3 rot;
				double fwd[3], up0[3], right0[3], want[3], up[3], right[3], a;
				const double turn = (double)ai * 45.0 * 3.14159265358979323846 / 180.0;
				int k;

				rot.x = (s16)pitches[pi];
				rot.y = (s16)yaws[yi];
				rot.z = 0;
				// the view direction of this yaw/pitch: the matrix axis the game uses for it (find it as ExtCam_Axes does)
				{
					MATRIX m;

					ConvertRotToMatrix(&m, &rot);
					fwd[0] = m.m[0][2] / 4096.0;
					fwd[1] = m.m[1][2] / 4096.0;
					fwd[2] = m.m[2][2] / 4096.0;
				}
				ExtCam_Axes(&rot, fwd, up0, right0);
				for (k = 0; k < 3; k++)
				{
					want[k] = cos(turn) * up0[k] + sin(turn) * right0[k];
				}
				rot.z = ExtCam_RollFor(rot, fwd, want);
				ExtCam_Axes(&rot, fwd, up, right);
				a = ExtCam_AngleDeg(up, want);
				(*n)++;
				if (a > 1.0)
				{
					(*failures)++;
					printf("ext selftest cam: yaw %d pitch %d turn %d deg: camera up %.2f deg off the wanted\n", yaws[yi], pitches[pi], ai * 45, a);
				}
			}
		}
	}
	return *failures - before;
}
