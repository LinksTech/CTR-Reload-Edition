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
//
// THE MEASURE LINE (--dev-ext-cam-log <H>, main.c; measuring only, 0 = off;
// fix round 1 of 2026-10-20, gate F2): one line per frame of seat 0 (one
// player), the camera AS DRAWN - read after the frame was drawn, from the push
// buffer's matrices that PushBuffer_SetMatrixVP built for it (matrix_Camera:
// the eye and the camera's axes; matrix_ViewProj with distanceToScreen_PREV and
// rect: the projection the engine draws with, Engine_CameraFromPushBuffer). So
// the line of frame F is written in the logic of frame F + 1, from what was kept
// of frame F (the kart, its zone, the flags) and the matrices drawn for it. It
// reads only: no game state, no scratchpad, no GTE register is touched (the
// occlusion test is ExtColl_LineQuery, exact and read-only). Format:
//   [CTR ExtCam] measure frame F zone Z since S lap L fin FI follow FO end E
//     grab G ext X pull P kart KX KY KZ eye EX EY EZ up UX UY UZ ndc NX NY NW
//     mid MX MY MW top TX TY TW occ O at A len LN
// zone = ExtPhys_DriverZone after the logic; since = frames since the last
// frame in a zone (0 in a zone, -1 none yet); lap = lapIndex; fin = the race is
// over for the kart (ACTION_RACE_FINISHED); follow = CAM_FollowDriver_Normal ran
// for seat 0 in F, end = then with a transition or the end-of-race turn
// (CAMERA_FLAG_TRANSITION_AWAY | CAMERA_FLAG_BATTLE_END_OF_RACE); grab = mask
// grab; ext = this camera set the push buffer in F; pull = it pulled the camera
// in (or lifted it, or took the retail one); kart = the driver's position
// (posCurr >> 8); eye = matrix_Camera.t; up = minus the matrix's second column
// (the picture's rows run down), unit length; ndc = the driver's position in the
// picture, x and y in -1..1 over the view rectangle (y down), and w = its depth
// in front of the eye (<= 0: behind it); mid / top = the same for the points
// 0.5 H and H along the kart's up (AxisAngle2_normalVec, the drawn kart's);
// occ = 1 when a ground or wall triangle (both faces) lies on the segment from
// the eye towards the kart's middle (the mid point, 0.5 H along the kart's up:
// gate F2's reference point, PLAN section 5 21:40), leaving out its last H / 2;
// at = that hit's distance from the eye (-1 none), len = eye to the kart's
// middle. Once per level "measure on" with H,
// at exit the number of lines and how often the eye moved between the logic and
// the drawing (the push buffer's position of F against matrix_Camera.t).

#define EXT_CAM_BLEND 15
#define EXT_CAM_ROLL_STEP 8.0  // degrees per frame
#define EXT_CAM_LIFT 30        // the ray starts this far above the kart (its up)
#define EXT_CAM_MARGIN 40      // the camera stays this far in front of a surface
#define EXT_CAM_MIN_DIST 40   // closest pull-in (Anlauf 3: 80 before)
#define EXT_CAM_RAISE 120     // the lift of the last resort along the kart's up

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
	u32 raised, retail, blockedLogged;
	double angleMax;
	double rollStepMax;
	// for the measure line (--dev-ext-cam-log): the frame (gGT->timer) in which
	// CAM_FollowDriver_Normal ran for seat 0 / this camera set the push buffer /
	// pulled the camera in; -1 never
	int followFrame, followEnd;
	int extFrame;
	int pullFrame;
} s_extCam = {.followFrame = -1, .extFrame = -1, .pullFrame = -1};

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
	Platform_Log("[CTR ExtCam] at exit: %u zone camera frame(s) of seat 0, ray free %u (%.1f %%), pulled in %u, lifted %u, retail camera taken %u, "
	             "angle camera up to kart up max %.1f deg, roll step max %.1f deg\n",
	             s_extCam.frames, s_extCam.clear, (s_extCam.frames > 0u) ? 100.0 * (double)s_extCam.clear / (double)s_extCam.frames : 0.0, s_extCam.pulled,
	             s_extCam.raised, s_extCam.retail, s_extCam.angleMax, s_extCam.rollStepMax);
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
		// A surface facing the camera: its place from a as well (Anlauf 3 - before, only the
		// surface facing the kart set *t, and a camera behind one facing it was never pulled in).
		const double h[3] = {(double)hit.pos.x - b[0], (double)hit.pos.y - b[1], (double)hit.pos.z - b[2]};
		const double ta = (len - sqrt(ExtCam_Dot(h, h))) / len;

		*t = (ta < *t) ? ((ta > 0.0) ? ta : 0.0) : *t;
		isFree = 0;
	}
	return isFree;
}

// --------------------------------------------------------------------------
// THE MEASURE LINE (see the head of this file)

double g_cfg_extCamLogH = 0.0; // --dev-ext-cam-log <H>: the kart's height in world units (PLAN 3a: 66.2); 0 = off

typedef struct
{
	int valid;
	const void *level1;
	int frame;
	int zone, since, lap, fin, follow, end, grab, ext, pull;
	double kart[3];
	double kartUp[3];
	s16 pbPos[3];
	int screen; // distanceToScreen_PREV
	int rectW, rectH;
} ExtCamMeasureFrame;

global_variable struct
{
	ExtCamMeasureFrame kept; // the frame whose picture is drawn next
	const void *level1;
	int lastZoneFrame;  // -1 none on this level
	int headerLevelLogged;
	int reportOn;
	u32 lines;
	u32 eyeMoved;
} s_extCamMeasure = {.lastZoneFrame = -1};

// The camera's up and its eye from a push buffer's matrix_Camera (the matrix
// ConvertRotToMatrix makes of pb->rot: its columns are the camera's axes in the
// world - x right, y down the picture, z the view; PushBuffer_SetMatrixVP).
internal void ExtCam_MeasureUp(const MATRIX *camera, double up[3])
{
	up[0] = -(double)camera->m[0][1];
	up[1] = -(double)camera->m[1][1];
	up[2] = -(double)camera->m[2][1];
	ExtCam_Norm(up);
}

// A world point in the picture: view = R p / 4096 + t with R, t of
// matrix_ViewProj (its rows already squeezed as the game's), then the screen as
// Engine_CameraFromPushBuffer / Engine_ProjectPoint make it: offset + H * v.xy / v.z,
// offset = rect / 2. Out: x, y in -1..1 over the view rectangle (y down), w = v.z.
internal void ExtCam_MeasureProject(const MATRIX *viewProj, int screen, int rectW, int rectH, const double p[3], double out[3])
{
	double v[3];
	int r;

	for (r = 0; r < 3; r++)
	{
		v[r] = ((double)viewProj->m[r][0] * p[0] + (double)viewProj->m[r][1] * p[1] + (double)viewProj->m[r][2] * p[2]) / 4096.0 + (double)viewProj->t[r];
	}
	out[2] = v[2];
	if ((fabs(v[2]) < 1e-9) || ((rectW >> 1) <= 0) || ((rectH >> 1) <= 0))
	{
		out[0] = 0.0;
		out[1] = 0.0;
		return;
	}
	out[0] = (double)screen * v[0] / (v[2] * (double)(rectW >> 1));
	out[1] = (double)screen * v[1] / (v[2] * (double)(rectH >> 1));
}

// The segment eye -> kart (its middle) without its last `keep` units: 1 = a ground or wall
// triangle lies on it (both faces), *at = the first hit's distance from the eye.
internal int ExtCam_MeasureOccluded(const double eye[3], const double kart[3], double keep, double *at, double *len)
{
	const double d[3] = {kart[0] - eye[0], kart[1] - eye[1], kart[2] - eye[2]};
	ExtCollLineHit hit;

	*len = sqrt(ExtCam_Dot(d, d));
	*at = -1.0;
	if (*len - keep <= 0.0)
	{
		return 0;
	}
	if (!ExtColl_LineQuery(eye, d, 0.0, *len - keep, EXT_COLL_LINE_GROUND | EXT_COLL_LINE_WALL, 0, &hit))
	{
		return 0;
	}
	*at = hit.t;
	return 1;
}

internal void ExtCam_MeasureReport(void)
{
	Platform_Log("[CTR ExtCam] measure at exit: %u line(s), eye moved between the logic and the drawing in %u frame(s)\n", s_extCamMeasure.lines,
	             s_extCamMeasure.eyeMoved);
}

// game/CAM.c, CAM_FollowDriver_Normal of every driver: notes the frame in which
// it ran for seat 0 (and whether the camera turns away). Writes this file's
// state only.
void ExtCam_NoteFollow(const struct GameTracker *gGT, const struct Driver *d, int turningAway)
{
	if ((g_cfg_extCamLogH <= 0.0) || (gGT == NULL) || (d == NULL) || (d->driverID != 0))
	{
		return;
	}
	s_extCam.followFrame = gGT->timer;
	s_extCam.followEnd = turningAway;
}

// The dispatcher ExtTrack_OnLogicFrame, after the game logic of a frame (the
// picture of the frame before is drawn by now): the line of the kept frame, then
// this frame kept.
void ExtCam_OnLogicFrame(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	const struct PushBuffer *pb;
	ExtCamMeasureFrame *k = &s_extCamMeasure.kept;

	if ((g_cfg_extCamLogH <= 0.0) || (gGT == NULL))
	{
		return;
	}
	d = gGT->drivers[0];
	pb = &gGT->pushBuffer[0];
	if ((gGT->level1 != s_extCamMeasure.level1) || (d == NULL) || (gGT->numPlyrCurrGame != 1))
	{
		s_extCamMeasure.level1 = gGT->level1;
		s_extCamMeasure.lastZoneFrame = -1;
		s_extCamMeasure.headerLevelLogged = 0;
		k->valid = 0;
		if ((d == NULL) || (gGT->numPlyrCurrGame != 1))
		{
			return;
		}
	}
	if (!s_extCamMeasure.headerLevelLogged)
	{
		s_extCamMeasure.headerLevelLogged = 1;
		Platform_Log("[CTR ExtCam] measure on: seat 0, H %.1f, the occlusion test runs to the kart's middle and leaves out the last %.1f units before it\n",
		             g_cfg_extCamLogH, 0.5 * g_cfg_extCamLogH);
		if (!s_extCamMeasure.reportOn)
		{
			s_extCamMeasure.reportOn = 1;
			Platform_AtExitReport(ExtCam_MeasureReport);
		}
	}
	if (k->valid && (k->level1 == gGT->level1) && (gGT->timer != k->frame))
	{
		// the picture of frame k->frame: the matrices PushBuffer_SetMatrixVP built for it
		double eye[3], up[3], ndc[3], mid[3], top[3], p[3], midPoint[3], at, len;
		const double keep = 0.5 * g_cfg_extCamLogH;
		int occ;
		int i;

		for (i = 0; i < 3; i++)
		{
			eye[i] = (double)pb->matrix_Camera.t[i];
		}
		ExtCam_MeasureUp(&pb->matrix_Camera, up);
		ExtCam_MeasureProject(&pb->matrix_ViewProj, k->screen, k->rectW, k->rectH, k->kart, ndc);
		for (i = 0; i < 3; i++)
		{
			midPoint[i] = k->kart[i] + 0.5 * g_cfg_extCamLogH * k->kartUp[i];
		}
		ExtCam_MeasureProject(&pb->matrix_ViewProj, k->screen, k->rectW, k->rectH, midPoint, mid);
		for (i = 0; i < 3; i++)
		{
			p[i] = k->kart[i] + g_cfg_extCamLogH * k->kartUp[i];
		}
		ExtCam_MeasureProject(&pb->matrix_ViewProj, k->screen, k->rectW, k->rectH, p, top);
		// the reference point of gate F2 (PLAN section 5, 21:40): the kart's middle
		occ = ExtCam_MeasureOccluded(eye, midPoint, keep, &at, &len);
		if ((pb->matrix_Camera.t[0] != k->pbPos[0]) || (pb->matrix_Camera.t[1] != k->pbPos[1]) || (pb->matrix_Camera.t[2] != k->pbPos[2]))
		{
			s_extCamMeasure.eyeMoved++;
		}
		s_extCamMeasure.lines++;
		Platform_Log("[CTR ExtCam] measure frame %d zone %d since %d lap %d fin %d follow %d end %d grab %d ext %d pull %d kart %.0f %.0f %.0f eye %.0f %.0f %.0f "
		             "up %.4f %.4f %.4f ndc %.4f %.4f %.1f mid %.4f %.4f %.1f top %.4f %.4f %.1f occ %d at %.1f len %.1f\n",
		             k->frame, k->zone, k->since, k->lap, k->fin, k->follow, k->end, k->grab, k->ext, k->pull, k->kart[0], k->kart[1], k->kart[2], eye[0],
		             eye[1], eye[2], up[0], up[1], up[2], ndc[0], ndc[1], ndc[2], mid[0], mid[1], mid[2], top[0], top[1], top[2], occ, at, len);
	}
	// keep this frame (drawn next)
	k->valid = 1;
	k->level1 = gGT->level1;
	k->frame = gGT->timer;
	k->zone = ExtPhys_DriverZone(d);
	if (k->zone != 0)
	{
		s_extCamMeasure.lastZoneFrame = gGT->timer;
	}
	k->since = (k->zone != 0) ? 0 : ((s_extCamMeasure.lastZoneFrame >= 0) ? (gGT->timer - s_extCamMeasure.lastZoneFrame) : -1);
	k->lap = (int)d->lapIndex;
	k->fin = ((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0) ? 1 : 0;
	k->follow = (s_extCam.followFrame == gGT->timer) ? 1 : 0;
	k->end = (k->follow && s_extCam.followEnd) ? 1 : 0;
	k->grab = (d->kartState == KS_MASK_GRABBED) ? 1 : 0;
	k->ext = (s_extCam.extFrame == gGT->timer) ? 1 : 0;
	k->pull = (s_extCam.pullFrame == gGT->timer) ? 1 : 0;
	k->kart[0] = (double)(d->posCurr.x >> 8);
	k->kart[1] = (double)(d->posCurr.y >> 8);
	k->kart[2] = (double)(d->posCurr.z >> 8);
	k->kartUp[0] = (double)d->AxisAngle2_normalVec.x;
	k->kartUp[1] = (double)d->AxisAngle2_normalVec.y;
	k->kartUp[2] = (double)d->AxisAngle2_normalVec.z;
	if (ExtCam_Norm(k->kartUp) < 1e-6)
	{
		k->kartUp[0] = 0.0;
		k->kartUp[1] = 1.0;
		k->kartUp[2] = 0.0;
	}
	k->pbPos[0] = pb->pos.x;
	k->pbPos[1] = pb->pos.y;
	k->pbPos[2] = pb->pos.z;
	k->screen = pb->distanceToScreen_PREV;
	k->rectW = pb->rect.w;
	k->rectH = pb->rect.h;
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
		{
			const double C0[3] = {C[0], C[1], C[2]};
			const double t0 = t;
			int iter;

			// Pulled in front of the nearest surface, again until free (at most 3 times).
			for (iter = 0; (iter < 3) && !isFree; iter++)
			{
				double v[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
				const double len = ExtCam_Norm(v);
				double keep = t * len - EXT_CAM_MARGIN;

				keep = (keep < EXT_CAM_MIN_DIST) ? EXT_CAM_MIN_DIST : keep;
				if (keep >= len)
				{
					break;
				}
				for (k = 0; k < 3; k++)
				{
					C[k] = A[k] + keep * v[k];
				}
				s_extCam.pulled++;
				s_extCam.pullFrame = gGT->timer;
				isFree = ExtCam_RayFree(A, C, &t);
			}
			// Still a surface in between (the road of a concave curve right behind the kart):
			// the camera lifted along the kart's up, then the retail camera, whichever is free.
			if (!isFree)
			{
				double Cu[3];
				double tu;

				for (k = 0; k < 3; k++)
				{
					Cu[k] = A[k] + 0.6 * (C0[k] - A[k]) + EXT_CAM_RAISE * U[k];
				}
				if (ExtCam_RayFree(A, Cu, &tu))
				{
					memcpy(C, Cu, sizeof(Cu));
					isFree = 1;
					s_extCam.raised++;
					s_extCam.pullFrame = gGT->timer;
				}
				else
				{
					const double Cr[3] = {(double)camPos->x, (double)camPos->y, (double)camPos->z};
					double tr;

					if (ExtCam_RayFree(A, Cr, &tr))
					{
						memcpy(C, Cr, sizeof(Cr));
						isFree = 1;
						s_extCam.retail++;
						s_extCam.pullFrame = gGT->timer;
					}
				}
			}
			if (!isFree && (s_extCam.blockedLogged < 64u))
			{
				s_extCam.blockedLogged++;
				Platform_Log("[CTR ExtCam] blocked frame %d: kart %.0f %.0f %.0f up %.2f %.2f %.2f, camera wanted %.0f %.0f %.0f (first surface at %.2f of the ray), "
				             "now %.0f %.0f %.0f\n",
				             gGT->timer, P[0], P[1], P[2], U[0], U[1], U[2], C0[0], C0[1], C0[2], t0, C[0], C[1], C[2]);
			}
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
			s_extCam.extFrame = gGT->timer;
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
	// The measure line's reading of a push buffer: the view as
	// PushBuffer_SetMatrixVP builds it (the transpose of the camera matrix, the
	// rows of y times 0x360 / 0x600, t = R * -pos / 4096), a camera of the
	// retail kind (yaw and pitch from eye minus look point, as the follow
	// camera's ratan2) - the look point lies in the middle of the picture, a
	// point above it higher up (y < 0), a point behind the eye has w < 0, the
	// up points up the world; and the occlusion segment leaves out the last
	// units before the kart.
	{
		static const int eyes[3][3] = {{100, 50, -300}, {-2000, 900, 400}, {0, 0, 0}};
		static const int looks[3][3] = {{120, 60, 0}, {-1800, 800, 300}, {-300, 10, 40}};
		int c;

		for (c = 0; c < 3; c++)
		{
			const int dx = eyes[c][0] - looks[c][0], dy = eyes[c][1] - looks[c][1], dz = eyes[c][2] - looks[c][2];
			SVec3 rot;
			MATRIX cam, vp;
			double up[3], look[3], above[3], behind[3], o1[3], o2[3], o3[3];
			int r, k;
			s64 tr;

			rot.y = (s16)ratan2(dx, dz);
			rot.x = (s16)(0x800 - ratan2(dy, (int)sqrt((double)dx * dx + (double)dz * dz)));
			rot.z = 0;
			ConvertRotToMatrix(&cam, &rot);
			for (r = 0; r < 3; r++)
			{
				cam.t[r] = eyes[c][r];
				tr = 0;
				for (k = 0; k < 3; k++)
				{
					vp.m[r][k] = cam.m[k][r];
					tr += (s64)cam.m[k][r] * (s64)(-eyes[c][k]);
				}
				vp.t[r] = (s32)(tr >> 12);
			}
			vp.t[1] = vp.t[1] * 0x360 / 0x600;
			for (k = 0; k < 3; k++)
			{
				vp.m[1][k] = (s16)(vp.m[1][k] * 0x360 / 0x600);
				look[k] = (double)looks[c][k];
				above[k] = look[k] + ((k == 1) ? 50.0 : 0.0);
				behind[k] = 2.0 * (double)eyes[c][k] - look[k];
			}
			ExtCam_MeasureUp(&cam, up);
			ExtCam_MeasureProject(&vp, 0x100, 512, 216, look, o1);
			ExtCam_MeasureProject(&vp, 0x100, 512, 216, above, o2);
			ExtCam_MeasureProject(&vp, 0x100, 512, 216, behind, o3);
			*n += 5;
			if (up[1] < 0.9)
			{
				(*failures)++;
				printf("ext selftest cam: measure case %d: the camera's up %.3f %.3f %.3f does not point up\n", c, up[0], up[1], up[2]);
			}
			// within the rounding of the game's angles (ratan2 and the 4.12 matrix: about a degree)
			if ((fabs(o1[0]) > 0.03) || (fabs(o1[1]) > 0.03) || (o1[2] <= 0.0))
			{
				(*failures)++;
				printf("ext selftest cam: measure case %d: the look point at %.4f %.4f w %.1f, not in the middle\n", c, o1[0], o1[1], o1[2]);
			}
			if ((o2[1] >= o1[1] - 0.05) || (fabs(o2[0] - o1[0]) > 0.03) || (o2[2] <= 0.0))
			{
				(*failures)++;
				printf("ext selftest cam: measure case %d: the point above the look point at %.4f %.4f w %.1f\n", c, o2[0], o2[1], o2[2]);
			}
			if (o3[2] >= 0.0)
			{
				(*failures)++;
				printf("ext selftest cam: measure case %d: the point behind the eye has w %.1f\n", c, o3[2]);
			}
			// the depth is the distance along the view: eye to look point
			if (fabs(o1[2] - sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz)) > 2.0)
			{
				(*failures)++;
				printf("ext selftest cam: measure case %d: depth %.1f of the look point is not its distance\n", c, o1[2]);
			}
		}
		{
			const double eye[3] = {0.0, 0.0, 0.0}, kart[3] = {0.0, 0.0, 20.0};
			double at, len;

			(*n)++;
			// no collision is built in the selftest: a segment shorter than the part left out is free at once
			if (ExtCam_MeasureOccluded(eye, kart, 26.0, &at, &len) || (fabs(len - 20.0) > 1e-9) || (at != -1.0))
			{
				(*failures)++;
				printf("ext selftest cam: measure: a segment of 20 within the 26 left out is not free\n");
			}
		}
	}
	return *failures - before;
}
