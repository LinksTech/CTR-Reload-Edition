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
// "follow_up": "zones"  the source game's chase camera (fix round 2,
//                   platform/native_extcam_module.c, included below): it runs
//                   beside the retail camera and is shown from the first frame
//                   of an anti-gravity zone, blended in and out over 1 s
//                   (details at the head of that file). Writes the push
//                   buffer's position, rotation and projection distance only
//                   and puts the retail values back before the next camera
//                   tick; the GTE registers are put back. On every track
//                   without zones, and outside its frames, nothing is written:
//                   the retail camera exactly. (The helpers below stay from
//                   fix round 1 and night run 1.)
// Log: one summary at exit.
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
// gate F2's reference point), leaving out its last H / 2;
// at = that hit's distance from the eye (-1 none), len = eye to the kart's
// middle. Once per level "measure on" with H,
// at exit the number of lines and how often the eye moved between the logic and
// the drawing (the push buffer's position of F against matrix_Camera.t).

#define EXT_CAM_KART_H 66.2     // the kart's height H in world units (PLAN 3a of fix round 1)
#define EXT_CAM_UP_RATE 0.15    // the up turns by this share of its angle to the zone's up per frame ...
#define EXT_CAM_UP_MAX 5.0      // ... at most this many degrees
#define EXT_CAM_FWD_RATE 0.5    // the course: this share per frame
#define EXT_CAM_IN 12           // frames of the blend in
#define EXT_CAM_OUT 12          // frames of the blend out ...
#define EXT_CAM_OUT_END 40      // ... over by this many frames after the last zone frame
#define EXT_CAM_LAST_FRAME 44   // never written later than this after the last zone frame (gate F2f: 45)
#define EXT_CAM_TURN_MAX 7.0    // degrees the camera turns at most per frame (gate F2b: 8)
#define EXT_CAM_TURN_CHECK 7.8  // the drawn up's step allowed after the rotation is made
#define EXT_CAM_HANDOVER 7.0    // back to retail when the retail camera is within this of the frame before
#define EXT_CAM_MIN_EYE 60.0    // the eye stays this far from the kart's middle
#define EXT_CAM_GROW 0.04       // a pulled-in eye goes back out by this share per frame
#define EXT_CAM_FRAME_Y 0.30    // the kart's middle sits this far below the picture's middle (-1..1) ...
#define EXT_CAM_FRAME_BOX 0.42  // ... and never outside this box when the step allows (gate F2a: 0.5; chief: 0.45)
#define EXT_CAM_ORIENT_OUT 38   // after the zone the view turns towards the retail one from this frame on

global_variable struct
{
	const void *level1; // the level the state belongs to
	int intro;      // 1 = no fly-in
	int end;        // 1 = spin at the end
	int follow;     // 1 = zones
	int introLogged;
	int reportOn;
	// the zone camera
	int active;        // 1 = in use (from a zone frame to the hand-over to retail)
	int lastZoneFrame; // gGT->timer of the last frame in a zone, -1 none on this level
	int haveFrame;
	double up[3];      // the soft up
	double fwd[3];     // the soft course across it
	double weight;     // 0..1 blend with the retail camera
	double sigma;      // the eye's share of the rig's distance (pulled in < 1)
	u32 frames, pulled, raised, blocked, limited, halved, lateHandover, idles, framingWide, framingOut;
	double sigmaMin;
	// notes of game/CAM.c (this file's state only): the frame (gGT->timer) in
	// which CAM_FollowDriver_Normal ran for seat 0 and the one before; the rig's
	// eye of that frame; the frame this camera set the push buffer / pulled the
	// camera in; -1 never
	int followFrame, followEnd, prevFollowFrame;
	int rigFrame;
	double rig[3];
	int extFrame;
	int pullFrame;
} s_extCam = {.lastZoneFrame = -1, .followFrame = -1, .prevFollowFrame = -1, .rigFrame = -1, .extFrame = -1, .pullFrame = -1};

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
// every call; a new level - another level1 - starts from the retail camera).
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
		s_extCam.active = 0;
		s_extCam.weight = 0.0;
		s_extCam.haveFrame = 0;
		s_extCam.lastZoneFrame = -1;
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
// it ran for seat 0 (and whether the camera turns away) and the frame before.
// Writes this file's state only.
void ExtCam_NoteFollow(const struct GameTracker *gGT, const struct Driver *d, int turningAway)
{
	if ((gGT == NULL) || (d == NULL) || (d->driverID != 0))
	{
		return;
	}
	if (s_extCam.followFrame != gGT->timer)
	{
		s_extCam.prevFollowFrame = s_extCam.followFrame;
		s_extCam.followFrame = gGT->timer;
	}
	s_extCam.followEnd = turningAway;
}

// game/CAM.c, CAM_FollowDriver_Normal: the retail eye of seat 0 right after it
// is built (distance, height and course; before the mask-grab height, the
// blasted lerp and the world-height rules). Writes this file's state only.
void ExtCam_NoteRig(const struct GameTracker *gGT, const struct Driver *d, const Vec3 *pos)
{
	if ((gGT == NULL) || (d == NULL) || (pos == NULL) || (d->driverID != 0))
	{
		return;
	}
	s_extCam.rigFrame = gGT->timer;
	s_extCam.rig[0] = (double)pos->x;
	s_extCam.rig[1] = (double)pos->y;
	s_extCam.rig[2] = (double)pos->z;
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
		// the reference point of gate F2: the kart's middle
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

// --------------------------------------------------------------------------
// THE ZONE CAMERA (follow_up "zones"; fix round 1, gate F2, variant R)

// Turns unit vector v about unit axis a by ang radians (Rodrigues).
internal void ExtCam_Turn(const double v[3], const double a[3], double ang, double out[3])
{
	const double c = cos(ang), s = sin(ang), d = ExtCam_Dot(a, v);
	double x[3];
	int k;

	ExtCam_Cross(a, v, x);
	for (k = 0; k < 3; k++)
	{
		out[k] = v[k] * c + x[k] * s + a[k] * d * (1.0 - c);
	}
}

// cur (unit) towards target (unit): by frac of the angle between them, at most
// maxDeg degrees, at least the whole angle when it is below 0.3 degrees.
internal void ExtCam_TurnToward(double cur[3], const double target[3], double frac, double maxDeg)
{
	double axis[3];
	double ang = ExtCam_AngleDeg(cur, target);
	double step;

	if (ang < 1e-9)
	{
		return;
	}
	ExtCam_Cross(cur, target, axis);
	if (ExtCam_Norm(axis) < 1e-9)
	{
		// opposite: any axis across cur
		const double t[3] = {(fabs(cur[0]) < 0.9) ? 1.0 : 0.0, (fabs(cur[0]) < 0.9) ? 0.0 : 1.0, 0.0};

		ExtCam_Cross(cur, t, axis);
		ExtCam_Norm(axis);
	}
	step = (ang < 0.3) ? ang : ang * frac;
	step = (step > maxDeg) ? maxDeg : step;
	ExtCam_Turn(cur, axis, step * 3.14159265358979323846 / 180.0, cur);
	ExtCam_Norm(cur);
}

// A camera's orientation: its view direction f and its up u (unit, u across f).
typedef struct
{
	double f[3];
	double u[3];
} ExtCamOrient;

internal int ExtCam_OrientMake(ExtCamOrient *o, const double f[3], const double up[3])
{
	double d;
	int k;

	memmove(o->f, f, sizeof(o->f)); // f may be o->f itself
	if (ExtCam_Norm(o->f) < 1e-9)
	{
		return 0;
	}
	d = ExtCam_Dot(up, o->f);
	for (k = 0; k < 3; k++)
	{
		o->u[k] = up[k] - d * o->f[k];
	}
	return ExtCam_Norm(o->u) > 1e-6;
}

// The orientation of a camera matrix as ConvertRotToMatrix makes it (and as
// the measure line reads it): view = the third column, up = minus the second.
internal void ExtCam_OrientOfMatrix(const MATRIX *m, ExtCamOrient *o)
{
	o->f[0] = (double)m->m[0][2];
	o->f[1] = (double)m->m[1][2];
	o->f[2] = (double)m->m[2][2];
	ExtCam_Norm(o->f);
	ExtCam_MeasureUp(m, o->u);
}

// The turn from a to b: *ang (radians) about axis (unit). The frames
// [f, u, f x u] of both, R = B A^T.
internal void ExtCam_OrientDelta(const ExtCamOrient *a, const ExtCamOrient *b, double axis[3], double *ang)
{
	double sa[3], sb[3], r[3][3], c;
	int i, j;

	ExtCam_Cross(a->f, a->u, sa);
	ExtCam_Cross(b->f, b->u, sb);
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
		{
			r[i][j] = b->f[i] * a->f[j] + b->u[i] * a->u[j] + sb[i] * sa[j];
		}
	}
	c = 0.5 * (r[0][0] + r[1][1] + r[2][2] - 1.0);
	c = (c > 1.0) ? 1.0 : ((c < -1.0) ? -1.0 : c);
	*ang = acos(c);
	axis[0] = r[2][1] - r[1][2];
	axis[1] = r[0][2] - r[2][0];
	axis[2] = r[1][0] - r[0][1];
	if (ExtCam_Norm(axis) < 1e-9)
	{
		if (*ang < 1e-6)
		{
			axis[0] = 0.0;
			axis[1] = 1.0;
			axis[2] = 0.0;
			*ang = 0.0;
			return;
		}
		// a half turn: the axis from the symmetric part (the column of R + I with the largest length)
		for (j = 0; j < 3; j++)
		{
			double col[3] = {r[0][j] + ((j == 0) ? 1.0 : 0.0), r[1][j] + ((j == 1) ? 1.0 : 0.0), r[2][j] + ((j == 2) ? 1.0 : 0.0)};

			if (ExtCam_Norm(col) > 0.5)
			{
				memcpy(axis, col, sizeof(col));
				return;
			}
		}
	}
}

// The roll for yaw/pitch rot that brings the DRAWN up (minus the second column
// of ConvertRotToMatrix, as the measure line reads it) closest to want: the
// roll turns about the view, so the drawn up of roll t is cos t u0 + sin t u90
// with u0 / u90 the drawn ups of roll 0 and a quarter turn - no sign rules
// (ExtCam_RollFor's guess of rows or columns and of the sign was wrong in some
// views: selftest of the zone camera).
internal s16 ExtCam_RollDrawn(SVec3 rot, const double want[3])
{
	MATRIX m;
	double u0[3], u90[3];
	double a;

	rot.z = 0;
	ConvertRotToMatrix(&m, &rot);
	ExtCam_MeasureUp(&m, u0);
	rot.z = 1024;
	ConvertRotToMatrix(&m, &rot);
	ExtCam_MeasureUp(&m, u90);
	a = atan2(ExtCam_Dot(want, u90), ExtCam_Dot(want, u0));
	return (s16)((int)lround(a * 4096.0 / (2.0 * 3.14159265358979323846)) & 0xfff);
}

// The push buffer's rotation for an orientation, as the follow camera makes it:
// yaw and pitch of ratan2 from the view direction backwards, the roll from up.
internal SVec3 ExtCam_RotOf(const ExtCamOrient *o)
{
	const int dx = (int)lround(-o->f[0] * 4096.0), dy = (int)lround(-o->f[1] * 4096.0), dz = (int)lround(-o->f[2] * 4096.0);
	SVec3 rot;

	rot.y = (s16)ratan2(dx, dz);
	rot.x = (s16)(0x800 - ratan2(dy, (int)sqrt((double)dx * dx + (double)dz * dz)));
	rot.z = 0;
	rot.z = ExtCam_RollDrawn(rot, o->u);
	return rot;
}

// THE STEP LIMIT (F2b): from the orientation drawn in the frame before (prev,
// read from its matrix), towards want: the turn is cut at EXT_CAM_TURN_MAX, the
// rotation made of it, and the up OF THAT ROTATION (ConvertRotToMatrix, minus
// the second column - what the measure line reads) checked against prev's up;
// over EXT_CAM_TURN_CHECK the step is halved, at most 4 times, then prev's own
// rotation. *cut = 1 when the step was cut, *halvings = how often it was halved.
// Returns the rotation.
internal SVec3 ExtCam_LimitedRotMax(const ExtCamOrient *prev, const ExtCamOrient *want, double maxDeg, int *cut, int *halvings, double *drawnStep)
{
	ExtCamOrient o;
	double axis[3], ang, step;
	int tries;

	ExtCam_OrientDelta(prev, want, axis, &ang);
	step = ang;
	*cut = 0;
	*halvings = 0;
	if (step > maxDeg * 3.14159265358979323846 / 180.0)
	{
		step = maxDeg * 3.14159265358979323846 / 180.0;
		*cut = 1;
	}
	for (tries = 0; tries <= 4; tries++)
	{
		MATRIX m;
		ExtCamOrient drawn;
		SVec3 rot;

		ExtCam_Turn(prev->f, axis, step, o.f);
		ExtCam_Turn(prev->u, axis, step, o.u);
		ExtCam_OrientMake(&o, o.f, o.u);
		rot = ExtCam_RotOf(&o);
		ConvertRotToMatrix(&m, &rot);
		ExtCam_OrientOfMatrix(&m, &drawn);
		*drawnStep = ExtCam_AngleDeg(drawn.u, prev->u);
		if (*drawnStep <= EXT_CAM_TURN_CHECK)
		{
			return rot;
		}
		step *= 0.5;
		*cut = 1;
		(*halvings)++;
	}
	return ExtCam_RotOf(prev);
}

internal SVec3 ExtCam_LimitedRot(const ExtCamOrient *prev, const ExtCamOrient *want, int *cut, int *halvings, double *drawnStep)
{
	return ExtCam_LimitedRotMax(prev, want, EXT_CAM_TURN_MAX, cut, halvings, drawnStep);
}

// The view matrix of rotation rot at eye as PushBuffer_SetMatrixVP builds it
// (the transpose of ConvertRotToMatrix, t = R * -eye / 4096, the rows of y
// times 0x360 / 0x600), the eye in whole units as the push buffer holds it.
internal void ExtCam_ViewOf(const SVec3 *rot, const double eye[3], MATRIX *vp)
{
	MATRIX cam;
	s32 e[3];
	int r, k;

	ConvertRotToMatrix(&cam, rot);
	for (k = 0; k < 3; k++)
	{
		e[k] = (s32)(s16)lround(eye[k]);
	}
	for (r = 0; r < 3; r++)
	{
		s64 tr = 0;

		for (k = 0; k < 3; k++)
		{
			vp->m[r][k] = cam.m[k][r];
			tr += (s64)cam.m[k][r] * (s64)(-e[k]);
		}
		vp->t[r] = (s32)(tr >> 12);
	}
	vp->t[1] = vp->t[1] * 0x360 / 0x600;
	for (k = 0; k < 3; k++)
	{
		vp->m[1][k] = (s16)(vp->m[1][k] * 0x360 / 0x600);
	}
}

// The view from eye that puts point mid at (0, yWant) of the picture (y down;
// screen = distanceToScreen, rectH = the view's height; the rows count 9/16):
// the direction to mid turned towards up by the angle of yWant; the camera's up
// = up across the view (or alt when the view runs along up).
internal int ExtCam_AimAt(const double eye[3], const double mid[3], const double up[3], const double alt[3], int screen, int rectH, double yWant,
                          ExtCamOrient *out)
{
	double a[3] = {mid[0] - eye[0], mid[1] - eye[1], mid[2] - eye[2]};
	double axis[3], f[3];
	const double half = (double)(((rectH >> 1) > 0) ? (rectH >> 1) : 1);
	const double sy = (double)screen * (double)0x360 / (double)0x600 / half;
	const double theta = atan(yWant / ((sy > 1e-6) ? sy : 1.0));

	if (ExtCam_Norm(a) < 1e-6)
	{
		return 0;
	}
	ExtCam_Cross(a, up, axis);
	if (ExtCam_Norm(axis) < 1e-6)
	{
		ExtCam_Cross(a, alt, axis);
		if (ExtCam_Norm(axis) < 1e-6)
		{
			return 0;
		}
	}
	ExtCam_Turn(a, axis, theta, f);
	if (!ExtCam_OrientMake(out, f, up))
	{
		return ExtCam_OrientMake(out, f, alt);
	}
	return 1;
}

// Where point p lies in the picture of rotation rot at eye (the measure line's projection).
internal void ExtCam_InPicture(const SVec3 *rot, const double eye[3], const struct PushBuffer *pb, const double p[3], double out[3])
{
	MATRIX vp;

	ExtCam_ViewOf(rot, eye, &vp);
	ExtCam_MeasureProject(&vp, pb->distanceToScreen_PREV, pb->rect.w, pb->rect.h, p, out);
}

// The kart's course in its surface as the retail camera reads it: the moving
// direction (matrixMovingDir: d->angle on AxisAngle1_normalVec) turned by
// rotCurr.w about that normal - the retail camera's yaw is rotCurr.w + angle.
internal void ExtCam_KartCourse(const struct Driver *d, double out[3])
{
	const double f[3] = {d->matrixMovingDir.m[0][2] / 4096.0, d->matrixMovingDir.m[1][2] / 4096.0, d->matrixMovingDir.m[2][2] / 4096.0};
	double n[3] = {d->matrixMovingDir.m[0][1] / 4096.0, d->matrixMovingDir.m[1][1] / 4096.0, d->matrixMovingDir.m[2][1] / 4096.0};

	ExtCam_Norm(n);
	ExtCam_Turn(f, n, (double)d->rotCurr.w * 2.0 * 3.14159265358979323846 / 4096.0, out);
	ExtCam_Norm(out);
}

// The retail rig's frame: the course K of yaw rotCurr.w + angle on flat ground
// (VehPhysForce_RotAxisAngle with the normal straight up: (sin a, 0, cos a)),
// world up, Q = K x up; laid onto (fwd, up, fwd x up). With up = (0, 1, 0) and
// fwd = K it maps every vector onto itself.
internal void ExtCam_MapRig(const double k[3], const double fwd[3], const double up[3], const double v[3], double out[3])
{
	const double y[3] = {0.0, 1.0, 0.0};
	double q[3], qh[3];
	double a, b, c;
	int i;

	ExtCam_Cross(k, y, q);
	ExtCam_Cross(fwd, up, qh);
	a = ExtCam_Dot(v, k);
	b = ExtCam_Dot(v, y);
	c = ExtCam_Dot(v, q);
	for (i = 0; i < 3; i++)
	{
		out[i] = a * fwd[i] + b * up[i] + c * qh[i];
	}
}

// THE ZONE CAMERA: the Reload camera module of night run 1 (its own file, built
// on the helpers above; it defines ExtCam_Follow and ExtCamMod_Selftest).
#include "native_extcam_module.c"

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
	// THE ZONE CAMERA: the rig's map is the identity with world up and the
	// retail course; with a tilted frame it keeps lengths and heights along up;
	// the soft up turns by at most its step; an orientation survives the
	// rotation the push buffer gets (within the game's rounding); and the step
	// limit holds on the drawn up for random turns.
	{
		u32 seed = 12345u;
		int c;

		for (c = 0; c < 40; c++)
		{
			const double yaw = (double)c * 0.37, tilt = (double)(c % 7) * 0.45;
			const double k[3] = {sin(yaw), 0.0, cos(yaw)};
			const double y[3] = {0.0, 1.0, 0.0};
			double v[3], out[3], up[3], fwd[3], axis[3];
			int i;

			for (i = 0; i < 3; i++)
			{
				seed = seed * 1664525u + 1013904223u;
				v[i] = (double)((seed >> 8) % 2001) - 1000.0;
			}
			ExtCam_MapRig(k, k, y, v, out);
			(*n)++;
			if ((fabs(out[0] - v[0]) > 1e-9) || (fabs(out[1] - v[1]) > 1e-9) || (fabs(out[2] - v[2]) > 1e-9))
			{
				(*failures)++;
				printf("ext selftest cam: zone rig case %d: world up and the retail course do not map onto themselves\n", c);
			}
			// tilted about the course's side: up and fwd turn together
			ExtCam_Cross(k, y, axis);
			ExtCam_Norm(axis);
			ExtCam_Turn(y, axis, tilt, up);
			ExtCam_Turn(k, axis, tilt, fwd);
			ExtCam_MapRig(k, fwd, up, v, out);
			(*n)++;
			if ((fabs(sqrt(ExtCam_Dot(out, out)) - sqrt(ExtCam_Dot(v, v))) > 1e-6) || (fabs(ExtCam_Dot(out, up) - v[1]) > 1e-6))
			{
				(*failures)++;
				printf("ext selftest cam: zone rig case %d: the tilted map changes a length or the height along up\n", c);
			}
		}
		{
			double cur[3] = {0.0, 1.0, 0.0};
			const double target[3] = {1.0, 0.0, 0.0};

			ExtCam_TurnToward(cur, target, EXT_CAM_UP_RATE, EXT_CAM_UP_MAX);
			(*n)++;
			if (fabs(ExtCam_AngleDeg(cur, target) - (90.0 - EXT_CAM_UP_MAX)) > 1e-6)
			{
				(*failures)++;
				printf("ext selftest cam: the soft up turned %.3f deg, not %.1f\n", 90.0 - ExtCam_AngleDeg(cur, target), EXT_CAM_UP_MAX);
			}
		}
		for (c = 0; c < 300; c++)
		{
			SVec3 r0, r1, rot;
			MATRIX m;
			ExtCamOrient prev, want, back, drawn;
			int cut, halvings;
			double step;

			seed = seed * 1664525u + 1013904223u;
			r0.x = (s16)(0x800 - 0x300 + (int)((seed >> 8) % 0x600));
			seed = seed * 1664525u + 1013904223u;
			r0.y = (s16)((seed >> 8) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			r0.z = (s16)((seed >> 8) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			r1.x = (s16)(0x800 - 0x300 + (int)((seed >> 8) % 0x600));
			seed = seed * 1664525u + 1013904223u;
			// the second one near the first in half the cases (small turns), anywhere in the others
			r1.y = (s16)(((c & 1) ? (r0.y + (int)((seed >> 8) % 120) - 60) : (int)(seed >> 8)) & 0xfff);
			seed = seed * 1664525u + 1013904223u;
			r1.z = (s16)(((c & 1) ? (r0.z + (int)((seed >> 8) % 120) - 60) : (int)(seed >> 8)) & 0xfff);
			if (c & 1)
			{
				r1.x = (s16)(r0.x + (r1.x - 0x800) / 16);
			}
			ConvertRotToMatrix(&m, &r0);
			ExtCam_OrientOfMatrix(&m, &prev);
			ConvertRotToMatrix(&m, &r1);
			ExtCam_OrientOfMatrix(&m, &want);
			// the orientation through the push buffer's rotation and back
			rot = ExtCam_RotOf(&want);
			ConvertRotToMatrix(&m, &rot);
			ExtCam_OrientOfMatrix(&m, &back);
			(*n)++;
			if ((ExtCam_AngleDeg(back.f, want.f) > 1.0) || (ExtCam_AngleDeg(back.u, want.u) > 1.0))
			{
				(*failures)++;
				printf("ext selftest cam: zone orientation case %d: through the rotation %.2f / %.2f deg off\n", c, ExtCam_AngleDeg(back.f, want.f),
				       ExtCam_AngleDeg(back.u, want.u));
			}
			rot = ExtCam_LimitedRot(&prev, &want, &cut, &halvings, &step);
			ConvertRotToMatrix(&m, &rot);
			ExtCam_OrientOfMatrix(&m, &drawn);
			(*n)++;
			if (ExtCam_AngleDeg(drawn.u, prev.u) > EXT_CAM_TURN_CHECK)
			{
				(*failures)++;
				printf("ext selftest cam: step limit case %d: the drawn up turned %.2f deg\n", c, ExtCam_AngleDeg(drawn.u, prev.u));
			}
		}
	}
	// THE AIM (attempt 2): the view of ExtCam_AimAt puts the kart's middle at (0, EXT_CAM_FRAME_Y) of the
	// picture drawn with the push buffer's rotation (within the rounding), for eyes all round and ups tilted
	{
		struct PushBuffer tpb;
		int c;

		memset(&tpb, 0, sizeof(tpb));
		tpb.distanceToScreen_PREV = 0x100;
		tpb.rect.w = 1216;
		tpb.rect.h = 216;
		for (c = 0; c < 48; c++)
		{
			const double yaw = (double)c * 0.53, tilt = (double)(c % 6) * 0.5;
			const double mid[3] = {1000.0, 2000.0, -500.0};
			double up[3] = {sin(tilt) * cos(yaw * 0.7), cos(tilt), sin(tilt) * sin(yaw * 0.7)};
			const double alt[3] = {1.0, 0.0, 0.0};
			double eye[3], pic[3], back[3];
			ExtCamOrient o;
			SVec3 rot;
			int k;

			ExtCam_Norm(up);
			// an eye behind and above the middle in the frame of up
			{
				double side[3], fw[3];
				const double ref[3] = {cos(yaw), 0.0, sin(yaw)};

				ExtCam_Cross(up, ref, side);
				ExtCam_Norm(side);
				ExtCam_Cross(side, up, fw);
				for (k = 0; k < 3; k++)
				{
					eye[k] = mid[k] - (150.0 + 10.0 * (double)(c % 5)) * fw[k] + 70.0 * up[k];
				}
			}
			for (k = 0; k < 3; k++)
			{
				eye[k] = (double)lround(eye[k]);
			}
			(*n)++;
			if (!ExtCam_AimAt(eye, mid, up, alt, tpb.distanceToScreen_PREV, tpb.rect.h, EXT_CAM_FRAME_Y, &o))
			{
				(*failures)++;
				printf("ext selftest cam: aim case %d: no view\n", c);
				continue;
			}
			rot = ExtCam_RotOf(&o);
			ExtCam_InPicture(&rot, eye, &tpb, mid, pic);
			(void)back;
			(*n)++;
			if ((pic[2] <= 0.0) || (fabs(pic[0]) > 0.03) || (fabs(pic[1] - EXT_CAM_FRAME_Y) > 0.03))
			{
				(*failures)++;
				printf("ext selftest cam: aim case %d: the middle at %.3f %.3f w %.1f, not 0 %.2f\n", c, pic[0], pic[1], pic[2], EXT_CAM_FRAME_Y);
			}
		}
	}
	ExtCamMod_Selftest(n, failures);
	return *failures - before;
}
