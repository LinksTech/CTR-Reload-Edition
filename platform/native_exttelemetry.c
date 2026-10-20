// THE EXTERNAL TRACK'S TELEMETRY (--dev-ext-telemetry; measuring only).
//
// WHAT. One "[CTR ExtTel]" line per logic frame for the kart of seat 0, from the
// dispatcher after the game logic of the frame (platform/engine_exttrack.c,
// ExtTrack_OnLogicFrame), only while the external track holds the loaded level.
// It fills what the other measuring lines leave out, so that a drive can be fed
// into an offline model of the anti-gravity behaviour frame by frame:
//   --dev-ext-cam-log  the drawn camera of a frame (eye, up, the kart in the
//                      picture, occlusion) - one frame late, see there
//   --dev-ext-kart-log the distance to the ground along the nav frame's up
//   [CTR ExtZone]      zone entries/exits and, in a zone, tilt and the zone's up
//   --dev-drive-log    position, checkpoint and lap every n frames
// Missing there and written here, every frame: the velocity, the kart's drawn
// rotation matrix, the ground contact and normal, the zone and its up also
// outside the zone lines, the kart state, and the camera the logic of this frame
// computed (the push buffer's pos/rot) beside the matrix last built from it.
//
// FORMAT (one line, world units; matrices and unit vectors in 4096 = 1):
//   [CTR ExtTel] f T pos X Y Z vel VX VY VZ step SX SY SZ speed S SA ks K grab G
//     gnd N norm NX NY NZ kup UX UY UZ mat M00 M01 M02 M10 M11 M12 M20 M21 M22
//     zone Z zup ZX ZY ZZ cam PX PY PZ rot RX RY RZ eye EX EY EZ cmat C00 .. C22
// T = gGT->timer (the frame number of the camera line); pos = posCurr / 256;
// vel = velocity / 256 (the game's velocity vector); step = posCurr - posPrev
// (/ 256, the move of this frame); speed = speed and speedApprox; ks = kartState;
// grab = mask grabbed; gnd = ACTION_TOUCH_GROUND; norm = normalVecUP (the ground
// normal the physics uses); kup = AxisAngle2_normalVec (the drawn kart's up);
// mat = the kart instance's rotation matrix (instSelf->matrix.m, rows);
// zone = ExtPhys_DriverZone (0 none) and zup its up (ExtPhys_DriverUp; world up
// outside a zone); cam = pushBuffer[0].pos, rot = pushBuffer[0].rot (what the
// camera of this frame's logic set); eye / cmat = matrix_Camera.t / .m - the
// matrix last built for drawing, i.e. of the frame before (as --dev-ext-cam-log).
//
// A log only: it reads the driver, the instance and the push buffer and writes
// nothing of the game, draws no random number and touches no GTE register.
// Without the switch (main.c) it returns at once.

int g_cfg_extTelemetry = 0;

global_variable int s_extTelLines;
global_variable int s_extTelReported;

internal void ExtTel_AtExit(void)
{
	Platform_Log("[CTR ExtTel] at exit: %d line(s)\n", s_extTelLines);
}

// The line of one frame into out.
internal int ExtTel_Format(char *out, size_t size, int frame, const struct Driver *d, const MATRIX *kartMat, int zone, const SVec3 *zup,
                           const struct PushBuffer *pb)
{
	const int ground = ((d->actionsFlagSet & ACTION_TOUCH_GROUND) != 0) ? 1 : 0;
	const int grab = (d->kartState == KS_MASK_GRABBED) ? 1 : 0;

	return snprintf(out, size,
	                "[CTR ExtTel] f %d pos %.2f %.2f %.2f vel %.2f %.2f %.2f step %.2f %.2f %.2f speed %d %d ks %d grab %d gnd %d norm %d %d %d "
	                "kup %d %d %d mat %d %d %d %d %d %d %d %d %d zone %d zup %d %d %d cam %d %d %d rot %d %d %d eye %d %d %d "
	                "cmat %d %d %d %d %d %d %d %d %d\n",
	                frame, d->posCurr.x / 256.0, d->posCurr.y / 256.0, d->posCurr.z / 256.0, d->velocity.x / 256.0, d->velocity.y / 256.0,
	                d->velocity.z / 256.0, (d->posCurr.x - d->posPrev.x) / 256.0, (d->posCurr.y - d->posPrev.y) / 256.0,
	                (d->posCurr.z - d->posPrev.z) / 256.0, (int)d->speed, (int)d->speedApprox, (int)d->kartState, grab, ground, (int)d->normalVecUP.x,
	                (int)d->normalVecUP.y, (int)d->normalVecUP.z, (int)d->AxisAngle2_normalVec.x, (int)d->AxisAngle2_normalVec.y,
	                (int)d->AxisAngle2_normalVec.z, (int)kartMat->m[0][0], (int)kartMat->m[0][1], (int)kartMat->m[0][2], (int)kartMat->m[1][0],
	                (int)kartMat->m[1][1], (int)kartMat->m[1][2], (int)kartMat->m[2][0], (int)kartMat->m[2][1], (int)kartMat->m[2][2], zone, (int)zup->x,
	                (int)zup->y, (int)zup->z, (int)pb->pos.x, (int)pb->pos.y, (int)pb->pos.z, (int)pb->rot.x, (int)pb->rot.y, (int)pb->rot.z,
	                (int)pb->matrix_Camera.t[0], (int)pb->matrix_Camera.t[1], (int)pb->matrix_Camera.t[2], (int)pb->matrix_Camera.m[0][0],
	                (int)pb->matrix_Camera.m[0][1], (int)pb->matrix_Camera.m[0][2], (int)pb->matrix_Camera.m[1][0], (int)pb->matrix_Camera.m[1][1],
	                (int)pb->matrix_Camera.m[1][2], (int)pb->matrix_Camera.m[2][0], (int)pb->matrix_Camera.m[2][1], (int)pb->matrix_Camera.m[2][2]);
}

// From the dispatcher once per logic frame (after the game logic).
void ExtTel_OnLogicFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d;
	SVec3 zup;
	int zone;
	char line[768];

	if (!g_cfg_extTelemetry)
	{
		return;
	}
	if ((gGT == NULL) || ((gGT->gameMode1 & (MAIN_MENU | LOADING)) != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		return;
	}
	d = gGT->drivers[0];
	if ((d == NULL) || (d->instSelf == NULL))
	{
		return;
	}
	if (!s_extTelReported)
	{
		s_extTelReported = 1;
		Platform_AtExitReport(ExtTel_AtExit);
	}
	zone = ExtPhys_DriverZone(d);
	zup.x = 0;
	zup.y = 4096;
	zup.z = 0;
	if (zone != 0)
	{
		(void)ExtPhys_DriverUp(d, &zup);
	}
	ExtTel_Format(line, sizeof(line), (int)gGT->timer, d, &d->instSelf->matrix, zone, &zup, &gGT->pushBuffer[0]);
	Platform_Log("%s", line);
	s_extTelLines++;
}
