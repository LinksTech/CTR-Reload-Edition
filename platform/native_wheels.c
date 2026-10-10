// ===========================================================================
// THE WHEEL POSES - FLOAT, IN A HOST TABLE, ONLY READING THE GAME.
//
// The pose calculator of the native wheels (the render plan, C.7.1, step 3a). Per seat
// one entry in a static table, keyed on the Driver pointer of the seat and the
// instance it drives (a new driver or instance starts the entry anew), moved on
// once per logic tick from the render layer's pull (platform/
// native_render_layer.c, NativeRenderLayer_Pull, which sees the new tick on
// gGT->timer). Only while the native probe is on or a custom character is
// drawn natively; without either nothing here runs.
//
// WHAT IT TAKES, all read from the driver of the seat in the tick:
// - ROLLING. The game has none: the retail wheels are 17 sprites of views
//   (game/DrawTires.c). The roll phase is integrated here from the signed speed
//   along the kart's forward axis, driver->speedApprox (include/
//   namespace_Vehicle.h:1159). game/Vehicle/VehPhysCrash.c:87-92 writes it in
//   every tick from the movement vector of the tick (VehPhysGeneral.c:850): the
//   length of the movement without its part along the up axis, negative when that
//   rest points against the forward column of matrixMovingDir. Bots set it from
//   their own speed (game/BOTS.c:2621). Its unit is the movement of one tick of
//   32 ms in posCurr units (1/256 world unit): the step of a tick is
//   velocity * elapsedTimeMS / 32 (game/COLL.c:2323-2326, 2430-2432), and
//   speedApprox is the length of that velocity (VehPhysCrash.c:59, 85-87, the
//   shift by 8 undoes the 8 fraction bits of VehCalc_FastSqrt's result). So the
//   way of a tick is speedApprox * elapsedTimeMS / 32 / 256 world units, and the
//   roll step is that way over the radius (wheelSize / 256, see
//   NativeWheels_Radius): speedApprox * elapsedTimeMS / (32 * wheelSize) radians.
//   The own wheel of a custom character (WHLS 2) rolls over its own radius in
//   world units, ownRadius * wheelSize / 4096 (NativeWheels_OwnScale): the way
//   over that, speedApprox * elapsedTimeMS / (8192 * ownRadius * wheelSize /
//   4096) radians - the same as above for a radius of 16 model units, the
//   retail size, so the wheel neither slips nor spins on the road.
//   Ghosts would need the position instead (they write no speed, plan C.7.1);
//   the probe binds only the seats of a race, never a ghost.
// - THE STROBOSCOPE of an own wheel (render plan A3): the roll drawn in a tick
//   is that step clamped to NATIVE_WHEELS_STROBE_FRACTION of a tread pitch,
//   2 pi / treads - the count the file stores for the mesh (make-char found
//   it in the geometry), or NATIVE_WHEELS_TREADS_SAFE when it stores none
//   (the clamp below half the pitch of every count up to 32); the step of
//   the way (rollStep) stays for the sign check and the report. One tick is
//   one change of the drawn pose, and one picture: game/MAIN/MainMain.c runs
//   MainFrame_GameLogic (one gGT->timer step, game/MAIN/MainFrame.c:222) and
//   then MainFrame_RenderFrame once per loop, the pull sees the new tick
//   there, the wheels are drawn from the pose as it is (no interpolation
//   between ticks), a frame without logic keeps the pose, and every frame
//   is presented (FIFO, platform/native_gfx_vk.c). So the tread never moves
//   by half a pitch or more between two pictures and never seems to stand or
//   to turn backwards. The probe and the test wheel are drawn without a
//   clamp (treads 0), as before.
// - STEERING, front wheels only: driver->wheelRotation << 2 in the angle unit of
//   the game (4096 a full turn), taken 1:1 as the yaw of the axle - as
//   DrawTiresSolid_BuildWheelLocalPairs turns the rim vector (DrawTires.c:372-376:
//   wheel 0 rim x - cos, z + sin; wheel 1 x + cos, z - sin).
// - WOBBLE, as DrawTires.c:378-388 in float: angle hazardTimer << 5, for an odd
//   hazardTimer shift 6 and the angle doubled, else shift 9; the rim of wheel 0
//   gets the angle, wheel 2 + 0x400, wheel 1 + 0x800, wheel 3 + 0xc00, and its y
//   and z grow by cos and sin of that angle over 2^shift of the rim's length.
// - Kart space only: the middles (DrawTires.c:338-366) and the turn into the
//   world are applied where the item is drawn, with the matrices of the body
//   (platform/native_render_layer.c, NativeRenderLayer_FillNativeWheels).
//
// THE RULES. Static host memory only, never MEMPACK, primMem, otMem or anything
// a quick state or a checkpoint holds. Nothing of the game is written. No
// coprocessor, no random numbers, no clock: sin, cos and sqrt of the C library on
// values of the tick, so the same run gives the same table.
// ===========================================================================

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <common.h>
#include <macros.h>

#include "platform/native_wheels.h"

int g_cfg_nativeWheelReport = 0;

#define NATIVE_WHEELS_TWO_PI 6.283185307179586

// One angle unit of the game in radians (4096 to the full turn).
#define NATIVE_WHEELS_ANGLE (NATIVE_WHEELS_TWO_PI / 4096.0)

struct NativeWheelEntry
{
	const struct Driver *driver; // only compared, never followed outside the pull
	const struct Instance *inst;
	s32 lastPos[3]; // posCurr of the last tick
	int haveLastPos;
	struct NativeWheelPose pose;
};

internal struct NativeWheelEntry s_nwSeats[NATIVE_WHEELS_SEATS];

// The counters, of the counted seat only (the seat the probe takes).
internal unsigned long long s_nwTicks = 0;
internal unsigned long long s_nwStarts = 0;
internal unsigned long long s_nwSigns[NATIVE_WHEEL_SIGNS];

// The stroboscope counters, of every seat with a clamp.
internal unsigned long long s_nwStrobeTicks = 0;
internal unsigned long long s_nwStrobeClamped = 0;
internal double s_nwStrobeStepMax = 0.0;
internal double s_nwStrobeDrawnMax = 0.0;

const char *NativeWheels_SignName(int sign)
{
	static const char *const names[NATIVE_WHEEL_SIGNS] = {"none", "still", "agree", "disagree", "zero"};

	if ((sign < 0) || (sign >= NATIVE_WHEEL_SIGNS))
	{
		return "none";
	}
	return names[sign];
}

void NativeWheels_Forget(void)
{
	memset(s_nwSeats, 0, sizeof(s_nwSeats));
}

void NativeWheels_LocalMiddle(int wheel, double out[3])
{
	out[0] = ((wheel & 1) != 0) ? -(double)0x90 : (double)0x90;
	out[1] = (double)0x40;
	out[2] = (wheel < 2) ? (double)0xc7 : -(double)0x60;
}

double NativeWheels_Radius(const struct NativeWheelPose *pose)
{
	return (double)pose->wheelSize / 256.0;
}

double NativeWheels_HalfWidth(const struct NativeWheelPose *pose)
{
	return NativeWheels_Radius(pose) * 0.5;
}

double NativeWheels_OwnScale(const struct NativeWheelPose *pose)
{
	return (double)pose->wheelSize / 4096.0;
}

double NativeWheels_RollRadius(const struct NativeWheelPose *pose)
{
	if (pose->ownRadius > 0.0)
	{
		return pose->ownRadius * NativeWheels_OwnScale(pose);
	}
	return NativeWheels_Radius(pose);
}

internal void NativeWheels_Normalize(double v[3])
{
	const double len = sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));

	if (len < 1e-12)
	{
		return;
	}
	v[0] /= len;
	v[1] /= len;
	v[2] /= len;
}

internal void NativeWheels_Cross(const double a[3], const double b[3], double out[3])
{
	out[0] = (a[1] * b[2]) - (a[2] * b[1]);
	out[1] = (a[2] * b[0]) - (a[0] * b[2]);
	out[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

// The four wheel frames of a pose from its steering, wobble and roll.
//
// THE AXLE. The rim vector of DrawTires.c points inwards (wheel 0: x - 0x1000);
// the axle here points outwards, its negative: wheel 0 (cos s, 0, -sin s), wheel
// 1 (-cos s, 0, sin s), the rear wheels (+-1, 0, 0) - a yaw by s about +y for
// both front wheels. The wobble adds (0, cos a, sin a) / 2^shift to the inward
// rim, so it is subtracted here.
//
// THE RIM PLANE. front = axle x up (normalized), up' = front x axle: for the
// left wheels (axle +x) that is z front and y up, for the right wheels (axle -x)
// z back and y up - the left frame turned by 180 degrees about y, a rotation
// and never a mirror (the determinant is +1 for every wheel).
//
// ROLLING turns the rim plane about the axle by theta: up'' = up' cos + front'
// sin, front'' = -up' sin + front' cos, so the top of the wheel moves towards
// front'. Forward rolling moves the top to the kart's front: theta = +roll for
// the left wheels, -roll for the right ones (their front' points back).
internal void NativeWheels_Frames(struct NativeWheelPose *pose)
{
	static const double up[3] = {0.0, 1.0, 0.0};
	const double s = pose->steer;
	int hazardShift = 9;
	int hazardAngle = (int)pose->hazardTimer * 32;
	int wheel;

	if ((pose->hazardTimer & 1) != 0)
	{
		hazardShift = 6;
		hazardAngle *= 2;
	}

	for (wheel = 0; wheel < NATIVE_WHEELS_COUNT; wheel++)
	{
		// DrawTires.c:385-388: wheel 0 +0, wheel 2 +0x400, wheel 1 +0x800, wheel 3 +0xc00.
		static const int phase[NATIVE_WHEELS_COUNT] = {0x000, 0x800, 0x400, 0xc00};
		const double a = (double)(hazardAngle + phase[wheel]) * NATIVE_WHEELS_ANGLE;
		const double wobble = 1.0 / (double)(1 << hazardShift);
		const double side = ((wheel & 1) != 0) ? -1.0 : 1.0;
		const double theta = side * ((wheel < 2) ? pose->roll : pose->rollRear);
		double axle[3];
		double front[3];
		double rimUp[3];
		int r;

		if (wheel < 2)
		{
			axle[0] = side * cos(s);
			axle[1] = 0.0;
			axle[2] = -side * sin(s);
		}
		else
		{
			axle[0] = side;
			axle[1] = 0.0;
			axle[2] = 0.0;
		}
		axle[1] -= cos(a) * wobble;
		axle[2] -= sin(a) * wobble;
		NativeWheels_Normalize(axle);

		NativeWheels_Cross(axle, up, front);
		NativeWheels_Normalize(front);
		NativeWheels_Cross(front, axle, rimUp);

		for (r = 0; r < 3; r++)
		{
			pose->axis[wheel][r][0] = axle[r];
			pose->axis[wheel][r][1] = (rimUp[r] * cos(theta)) + (front[r] * sin(theta));
			pose->axis[wheel][r][2] = (-rimUp[r] * sin(theta)) + (front[r] * cos(theta));
		}
	}
}

void NativeWheels_Pull(int seat, const struct Driver *driver, const struct Instance *inst, int newTick, u32 timer, int elapsedTimeMS, int counted,
                       const struct NativeWheelOwn *own)
{
	struct NativeWheelEntry *e;
	struct NativeWheelPose *pose;
	int axle;

	if ((seat < 0) || (seat >= NATIVE_WHEELS_SEATS))
	{
		return;
	}
	e = &s_nwSeats[seat];

	if ((driver == NULL) || (inst == NULL))
	{
		memset(e, 0, sizeof(*e));
		return;
	}

	pose = &e->pose;

	// Another driver or instance on the seat (a load, a restart): start anew,
	// roll phase 0 and no way for the first tick.
	if (!pose->valid || (e->driver != driver) || (e->inst != inst))
	{
		memset(e, 0, sizeof(*e));
		e->driver = driver;
		e->inst = inst;
		pose->valid = 1;
		if (counted)
		{
			s_nwStarts++;
		}
	}
	else if (!newTick)
	{
		// The same tick again (a frame without logic): the pose stays.
		return;
	}

	pose->timer = timer;
	pose->speed = driver->speedApprox;
	pose->wheelRotation = driver->wheelRotation;
	pose->hazardTimer = driver->hazardTimer;
	pose->wheelSize = driver->wheelSize;
	pose->ownRadius = ((own != NULL) && (own->radius[0] > 0.0)) ? own->radius[0] : 0.0;
	pose->ownRadiusRear = ((own != NULL) && (own->radius[1] > 0.0)) ? own->radius[1] : pose->ownRadius;
	pose->steer = (double)((int)driver->wheelRotation * 4) * NATIVE_WHEELS_ANGLE;

	// The roll step of this tick per axle (see the head of the file), and the
	// drawn step: the step of the way, clamped below half a tread pitch for an
	// own wheel (THE STROBOSCOPE at the head of the file). No wheel, no roll.
	// Without an own rear wheel the rear axle takes the front's values, so its
	// roll is the front's.
	for (axle = 0; axle < 2; axle++)
	{
		const double radius = (axle == 0) ? pose->ownRadius : pose->ownRadiusRear;
		const int treads = ((own != NULL) && (own->treads[axle] > 0)) ? own->treads[axle] : 0;
		const int counts = (axle == 0) || ((own != NULL) && own->rearOwn);
		double step = 0.0;
		double drawn;
		double *roll = (axle == 0) ? &pose->roll : &pose->rollRear;

		if ((pose->ticks > 0) && (pose->wheelSize != 0))
		{
			if (radius > 0.0)
			{
				step = ((double)pose->speed * (double)elapsedTimeMS) / (8192.0 * (radius * NativeWheels_OwnScale(pose)));
			}
			else
			{
				step = ((double)pose->speed * (double)elapsedTimeMS) / (32.0 * (double)pose->wheelSize);
			}
		}
		drawn = step;
		if (treads > 0)
		{
			const double clamp = NativeWheels_StrobeClamp(treads);
			const double pitch = NATIVE_WHEELS_TWO_PI / (double)treads;

			if (drawn > clamp)
			{
				drawn = clamp;
			}
			else if (drawn < -clamp)
			{
				drawn = -clamp;
			}
			if ((step != 0.0) && counts)
			{
				const double stepPitch = fabs(step) / pitch;
				const double drawnPitch = fabs(drawn) / pitch;

				s_nwStrobeTicks++;
				s_nwStrobeClamped += (drawn != step) ? 1u : 0u;
				s_nwStrobeStepMax = (stepPitch > s_nwStrobeStepMax) ? stepPitch : s_nwStrobeStepMax;
				s_nwStrobeDrawnMax = (drawnPitch > s_nwStrobeDrawnMax) ? drawnPitch : s_nwStrobeDrawnMax;
			}
		}
		*roll = fmod(*roll + drawn, NATIVE_WHEELS_TWO_PI);
		if (*roll < 0.0)
		{
			*roll += NATIVE_WHEELS_TWO_PI;
		}
		if (axle == 0)
		{
			pose->rollStep = step;
			pose->treads = treads;
			pose->drawStep = drawn;
		}
		else
		{
			pose->rollStepRear = step;
			pose->treadsRear = treads;
			pose->drawStepRear = drawn;
		}
	}

	// The way along the forward axis (the third column of the instance matrix,
	// model +z in the world) since the last tick, from posCurr (8 fraction bits).
	pose->moveKnown = 0;
	pose->move = 0.0;
	if (e->haveLastPos)
	{
		const double dx = (double)(driver->posCurr.x - e->lastPos[0]) / 256.0;
		const double dy = (double)(driver->posCurr.y - e->lastPos[1]) / 256.0;
		const double dz = (double)(driver->posCurr.z - e->lastPos[2]) / 256.0;

		pose->move = ((dx * (double)inst->matrix.m[0][2]) + (dy * (double)inst->matrix.m[1][2]) + (dz * (double)inst->matrix.m[2][2])) / 4096.0;
		pose->moveKnown = 1;
	}
	e->lastPos[0] = driver->posCurr.x;
	e->lastPos[1] = driver->posCurr.y;
	e->lastPos[2] = driver->posCurr.z;
	e->haveLastPos = 1;

	NativeWheels_Frames(pose);

	// The sign class of this tick (see enum NativeWheelSign): a roll step of
	// exactly 0 is a class of its own, so it counts neither for nor against the
	// sign, whichever way the kart moved.
	if (!pose->moveKnown)
	{
		pose->sign = NATIVE_WHEEL_SIGN_NONE;
	}
	else if (fabs(pose->move) < NATIVE_WHEELS_MOVING)
	{
		pose->sign = NATIVE_WHEEL_SIGN_STILL;
	}
	else if (pose->rollStep == 0.0)
	{
		pose->sign = NATIVE_WHEEL_SIGN_ZERO;
	}
	else if ((pose->rollStep > 0.0) == (pose->move > 0.0))
	{
		pose->sign = NATIVE_WHEEL_SIGN_AGREE;
	}
	else
	{
		pose->sign = NATIVE_WHEEL_SIGN_DISAGREE;
	}

	if (counted && (pose->ticks > 0))
	{
		s_nwTicks++;
		s_nwSigns[pose->sign]++;
	}
	pose->ticks++;
}

const struct NativeWheelPose *NativeWheels_PoseOf(const struct Instance *inst)
{
	int seat;

	if (inst == NULL)
	{
		return NULL;
	}

	for (seat = 0; seat < NATIVE_WHEELS_SEATS; seat++)
	{
		if (s_nwSeats[seat].pose.valid && (s_nwSeats[seat].inst == inst))
		{
			return &s_nwSeats[seat].pose;
		}
	}
	return NULL;
}

void NativeWheels_Counts(unsigned long long *ticks, unsigned long long *starts, unsigned long long signs[NATIVE_WHEEL_SIGNS])
{
	int sign;

	*ticks = s_nwTicks;
	*starts = s_nwStarts;
	for (sign = 0; sign < NATIVE_WHEEL_SIGNS; sign++)
	{
		signs[sign] = s_nwSigns[sign];
	}
}

double NativeWheels_StrobeClamp(int treads)
{
	if (treads <= 0)
	{
		return 0.0;
	}
	return (NATIVE_WHEELS_STROBE_FRACTION * NATIVE_WHEELS_TWO_PI) / (double)treads;
}

void NativeWheels_StrobeCounts(unsigned long long *ticks, unsigned long long *clamped, double *stepMax, double *drawnMax)
{
	*ticks = s_nwStrobeTicks;
	*clamped = s_nwStrobeClamped;
	*stepMax = s_nwStrobeStepMax;
	*drawnMax = s_nwStrobeDrawnMax;
}

// THE SELF-TEST of the stroboscope, on the seat table with a made-up driver
// (own wheel radius 16 model units, wheelSize 0x0ccc, 32 ms ticks):
//   safe     the safe clamp (NATIVE_WHEELS_TREADS_SAFE) is below half the
//            pitch of every tread count 4..32; and at every speed from 100
//            to 12000 forward and backward (12000 is past any kart's: the
//            true step is then 3.7 rad) the drawn step moves a tread of each
//            of those counts by a part of its pitch strictly between 0 and
//            1/2 in the direction of travel - never standing, never back
//   stored   a count of the file (10) clamps to 0.45 of its own pitch in
//            place of the safe clamp
//   ticks    speed 3000 forward and backward (true step 0.916 rad) drawn
//            clamped with the sign kept, speed 200 as it is, treads 0 the
//            true step
// Leaves the pose table empty and the counters as they were.
internal int NativeWheels_TestStep(struct Driver *driver, struct Instance *inst, s16 speed, int treads, double *step, double *drawn)
{
	struct NativeWheelOwn own;
	const struct NativeWheelPose *pose;

	NativeWheels_Forget();
	driver->speedApprox = speed;
	own.radius[0] = 16.0;
	own.radius[1] = 16.0;
	own.treads[0] = treads;
	own.treads[1] = treads;
	own.rearOwn = 0;
	NativeWheels_Pull(0, driver, inst, 1, 1u, 32, 0, &own);
	NativeWheels_Pull(0, driver, inst, 1, 2u, 32, 0, &own);
	pose = NativeWheels_PoseOf(inst);
	if (pose == NULL)
	{
		*step = 0.0;
		*drawn = 0.0;
		return 0;
	}
	*step = pose->rollStep;
	*drawn = pose->drawStep;
	return (pose->drawStepRear == pose->drawStep);
}

int NativeWheels_StrobeSelfTest(char *line, size_t size)
{
	static struct Driver driver;
	static struct Instance inst;
	static const s16 speeds[4] = {3000, -3000, 200, 3000};
	static const int treads[4] = {NATIVE_WHEELS_TREADS_SAFE, NATIVE_WHEELS_TREADS_SAFE, NATIVE_WHEELS_TREADS_SAFE, 0};
	const unsigned long long ticksBefore = s_nwStrobeTicks;
	const unsigned long long clampedBefore = s_nwStrobeClamped;
	const double stepMaxBefore = s_nwStrobeStepMax;
	const double drawnMaxBefore = s_nwStrobeDrawnMax;
	const double safe = NativeWheels_StrobeClamp(NATIVE_WHEELS_TREADS_SAFE);
	double drawn[4];
	double step[4];
	double stored[2];
	double partMax = 0.0;
	double partMin = 1.0;
	double stepTop = 0.0;
	int safeHeld = 1;
	int storedHeld;
	int clampHeld = 1;
	int counts = 0;
	int n;
	int c;

	memset(&driver, 0, sizeof(driver));
	memset(&inst, 0, sizeof(inst));
	driver.wheelSize = 0x0ccc;

	// The safe clamp against every count, and every speed.
	for (n = 4; n <= 32; n++)
	{
		safeHeld = safeHeld && (safe < ((0.5 * NATIVE_WHEELS_TWO_PI) / (double)n));
		counts++;
	}
	for (c = -120; c <= 120; c++)
	{
		double s;
		double d;

		if (c == 0)
		{
			continue;
		}
		safeHeld = NativeWheels_TestStep(&driver, &inst, (s16)(c * 100), NATIVE_WHEELS_TREADS_SAFE, &s, &d) && safeHeld;
		stepTop = (fabs(s) > stepTop) ? fabs(s) : stepTop;
		for (n = 4; n <= 32; n++)
		{
			// The move of the tread in its pitches, signed in the direction of travel.
			const double part = ((c > 0) ? d : -d) / (NATIVE_WHEELS_TWO_PI / (double)n);

			safeHeld = safeHeld && (part > 0.0) && (part < 0.5);
			partMax = (part > partMax) ? part : partMax;
			partMin = (part < partMin) ? part : partMin;
		}
	}

	// A stored count in place of the safe clamp.
	storedHeld = NativeWheels_TestStep(&driver, &inst, 3000, 10, &stored[0], &stored[1]) &&
	             (fabs(stored[1] - ((NATIVE_WHEELS_STROBE_FRACTION * NATIVE_WHEELS_TWO_PI) / 10.0)) < 1e-12) && (stored[1] > safe);

	// The ticks.
	for (c = 0; c < 4; c++)
	{
		clampHeld = NativeWheels_TestStep(&driver, &inst, speeds[c], treads[c], &step[c], &drawn[c]) && clampHeld;
		if (treads[c] == 0)
		{
			clampHeld = clampHeld && (drawn[c] == step[c]);
		}
		else if (fabs(step[c]) <= NativeWheels_StrobeClamp(treads[c]))
		{
			clampHeld = clampHeld && (drawn[c] == step[c]);
		}
		else
		{
			clampHeld = clampHeld && (fabs(fabs(drawn[c]) - NativeWheels_StrobeClamp(treads[c])) < 1e-12) && ((drawn[c] > 0.0) == (step[c] > 0.0));
		}
	}
	clampHeld = clampHeld && (fabs(step[0]) > safe) && (fabs(step[1]) > safe) && (fabs(step[2]) < safe);
	NativeWheels_Forget();
	s_nwStrobeTicks = ticksBefore;
	s_nwStrobeClamped = clampedBefore;
	s_nwStrobeStepMax = stepMaxBefore;
	s_nwStrobeDrawnMax = drawnMaxBefore;

	snprintf(line, size,
	         "safe clamp %d treads %.4f rad, below half the pitch of %d counts 4..32 and at 240 speeds the tread moves %.4f..%.4f of its pitch "
	         "(top step %.4f rad), safe %s; stored 10 treads drawn %.4f rad, stored %s; forward step %.4f drawn %.4f, backward step %.4f drawn %.4f, "
	         "slow step %.4f drawn %.4f, no clamp step %.4f drawn %.4f, clamp %s",
	         NATIVE_WHEELS_TREADS_SAFE, safe, counts, partMin, partMax, stepTop, safeHeld ? "held" : "off", stored[1], storedHeld ? "held" : "off", step[0],
	         drawn[0], step[1], drawn[1], step[2], drawn[2], step[3], drawn[3], clampHeld ? "held" : "off");
	return safeHeld && storedHeld && clampHeld;
}
