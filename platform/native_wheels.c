// ===========================================================================
// THE WHEEL POSES - FLOAT, IN A HOST TABLE, ONLY READING THE GAME.
//
// The pose calculator of the native wheels (the render plan, C.7.1, step 3a). Per seat
// one entry in a static table, keyed on the Driver pointer of the seat and the
// instance it drives (a new driver or instance starts the entry anew), moved on
// once per logic tick from the render layer's pull (platform/
// native_render_layer.c, NativeRenderLayer_Pull, which sees the new tick on
// gGT->timer). Only while the native probe is on; without it nothing here runs.
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
//   Ghosts would need the position instead (they write no speed, plan C.7.1);
//   the probe binds only the seats of a race, never a ghost.
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
		const double theta = side * pose->roll;
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

void NativeWheels_Pull(int seat, const struct Driver *driver, const struct Instance *inst, int newTick, u32 timer, int elapsedTimeMS, int counted)
{
	struct NativeWheelEntry *e;
	struct NativeWheelPose *pose;

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
	pose->steer = (double)((int)driver->wheelRotation * 4) * NATIVE_WHEELS_ANGLE;

	// The roll step of this tick (see the head of the file). No wheel, no roll.
	pose->rollStep = 0.0;
	if ((pose->ticks > 0) && (pose->wheelSize != 0))
	{
		pose->rollStep = ((double)pose->speed * (double)elapsedTimeMS) / (32.0 * (double)pose->wheelSize);
	}
	pose->roll = fmod(pose->roll + pose->rollStep, NATIVE_WHEELS_TWO_PI);
	if (pose->roll < 0.0)
	{
		pose->roll += NATIVE_WHEELS_TWO_PI;
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
