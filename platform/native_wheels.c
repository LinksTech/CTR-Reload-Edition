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
//   2 pi / treads (the count NativeWheels_EstimateTreads worked out at load);
//   the step of the way (rollStep) stays for the sign check and the report.
//   One tick is one change of the drawn pose (a frame without logic keeps the
//   pose), so the tread never moves by half a pitch or more between two
//   pictures and never seems to stand or to turn backwards. The probe and the
//   test wheel are drawn without a clamp (treads 0), as before.
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

// THE TREAD ESTIMATE (see the header). At most 2 x 2048 points (WHLS holds at
// most 2048); more are not looked at.
#define NATIVE_WHEELS_TREAD_POINTS 4096
#define NATIVE_WHEELS_TREAD_MERGE ((0.5 * NATIVE_WHEELS_TWO_PI) / 360.0)
#define NATIVE_WHEELS_TREAD_RING 1.25
#define NATIVE_WHEELS_TREAD_RING_MAX 16

internal int NativeWheels_CompareAngle(const void *a, const void *b)
{
	const double x = *(const double *)a;
	const double y = *(const double *)b;

	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

internal double NativeWheels_AxleRadius(const float *p)
{
	return sqrt(((double)p[1] * (double)p[1]) + ((double)p[2] * (double)p[2]));
}

void NativeWheels_EstimateTreads(const float *positions, size_t strideBytes, u32 count, struct NativeWheelTreads *out)
{
	static double angle[NATIVE_WHEELS_TREAD_POINTS];
	const u8 *base = (const u8 *)positions;
	double rmax = 0.0;
	u32 outer = 0;
	u32 merged = 0;
	u32 i;

	memset(out, 0, sizeof(*out));
	out->treads = NATIVE_WHEELS_TREADS_DEFAULT;
	if ((positions == NULL) || (count == 0u))
	{
		return;
	}
	if (count > NATIVE_WHEELS_TREAD_POINTS)
	{
		count = NATIVE_WHEELS_TREAD_POINTS;
	}

	// The largest radius about the axle (X).
	for (i = 0; i < count; i++)
	{
		const double r = NativeWheels_AxleRadius((const float *)(const void *)(base + ((size_t)i * strideBytes)));

		rmax = (r > rmax) ? r : rmax;
	}
	if (rmax < 1e-6)
	{
		return;
	}

	// The angles of the outermost points, sorted and merged.
	for (i = 0; i < count; i++)
	{
		const float *p = (const float *)(const void *)(base + ((size_t)i * strideBytes));

		if (NativeWheels_AxleRadius(p) >= (rmax * (1.0 - NATIVE_WHEELS_TREAD_BAND)))
		{
			double a = atan2((double)p[2], (double)p[1]);

			if (a < 0.0)
			{
				a += NATIVE_WHEELS_TWO_PI;
			}
			angle[outer++] = a;
		}
	}
	out->outer = outer;
	qsort(angle, outer, sizeof(angle[0]), NativeWheels_CompareAngle);
	for (i = 0; i < outer; i++)
	{
		if ((merged == 0u) || ((angle[i] - angle[merged - 1u]) > NATIVE_WHEELS_TREAD_MERGE))
		{
			angle[merged++] = angle[i];
		}
	}
	// Across 2 pi: the last angles merge into the first.
	while ((merged > 1u) && (((angle[0] + NATIVE_WHEELS_TWO_PI) - angle[merged - 1u]) <= NATIVE_WHEELS_TREAD_MERGE))
	{
		merged--;
	}
	out->angles = merged;
	if (merged < (u32)NATIVE_WHEELS_TREADS_MIN)
	{
		return;
	}

	// A ring of evenly spaced angles: its count below 16, else a smooth tyre.
	{
		double gapMin = 1e9;
		double gapMax = 0.0;

		for (i = 0; i < merged; i++)
		{
			const double gap = ((i + 1u) < merged) ? (angle[i + 1u] - angle[i]) : ((angle[0] + NATIVE_WHEELS_TWO_PI) - angle[i]);

			gapMin = (gap < gapMin) ? gap : gapMin;
			gapMax = (gap > gapMax) ? gap : gapMax;
		}
		if (gapMax <= (gapMin * NATIVE_WHEELS_TREAD_RING))
		{
			if (merged < (u32)NATIVE_WHEELS_TREAD_RING_MAX)
			{
				out->treads = (int)merged;
				out->estimated = 1;
				out->strength = 1.0;
			}
			return;
		}
	}

	// Any other set: the smallest n-fold periodicity that is strong enough.
	{
		int n;

		for (n = NATIVE_WHEELS_TREADS_MIN; n <= NATIVE_WHEELS_TREADS_MAX; n++)
		{
			double c = 0.0;
			double s = 0.0;
			double strength;

			for (i = 0; i < merged; i++)
			{
				c += cos((double)n * angle[i]);
				s += sin((double)n * angle[i]);
			}
			strength = sqrt((c * c) + (s * s)) / (double)merged;
			if (strength >= NATIVE_WHEELS_TREAD_STRENGTH)
			{
				out->treads = n;
				out->estimated = 1;
				out->strength = strength;
				return;
			}
		}
	}
}

// THE SELF-TEST of the stroboscope. Made-up wheels of radius 16 (model units,
// both sides x +-6; a groove ring at 14 and a hub at 8 that never count):
//   lugs 8     8 lug tops of 16 degrees: 8
//   lugs 12    12 lug tops with bevels (4 angles each, +-3 and +-6 degrees): 12
//   smooth 32  an even ring of 32: the default 8
//   ring 10    an even ring of 10 (a coarse polygon or 10 spikes): 10
//   none       no point: the default 8
// and two ticks of a made-up driver at radius 16, wheelSize 0x0ccc, 32 ms:
// speed 3000 forward and backward with 8 treads (true step 0.916 rad, more
// than half the pitch 0.785) are drawn clamped with the sign kept, speed 200
// as it is, and treads 0 draws the true step. Leaves the pose table empty and
// the counters as they were.
#define NATIVE_WHEELS_TEST_POINTS 512

internal u32 NativeWheels_TestRing(float (*p)[3], u32 at, double radius, int steps, const double *offsets, int offsetCount)
{
	int k;
	int o;
	int side;

	for (k = 0; k < steps; k++)
	{
		for (o = 0; o < offsetCount; o++)
		{
			const double a = ((NATIVE_WHEELS_TWO_PI * (double)k) / (double)steps) + ((offsets[o] * NATIVE_WHEELS_TWO_PI) / 360.0);

			for (side = 0; side < 2; side++)
			{
				if (at < NATIVE_WHEELS_TEST_POINTS)
				{
					p[at][0] = (side == 0) ? 6.0f : -6.0f;
					p[at][1] = (float)(radius * cos(a));
					p[at][2] = (float)(radius * sin(a));
					at++;
				}
			}
		}
	}
	return at;
}

int NativeWheels_StrobeSelfTest(char *line, size_t size)
{
	static float p[NATIVE_WHEELS_TEST_POINTS][3];
	static const double lug8[2] = {-8.0, 8.0};
	static const double lug12[4] = {-6.0, -3.0, 3.0, 6.0};
	static const double one[1] = {0.0};
	static const double groove[1] = {22.5};
	static struct Driver driver;
	static struct Instance inst;
	static const int expect[5] = {8, 12, 8, 10, 8};
	static const int expectEstimated[5] = {1, 1, 0, 1, 0};
	static const s16 speeds[4] = {3000, -3000, 200, 3000};
	static const int treads[4] = {8, 8, 8, 0};
	struct NativeWheelTreads t[5];
	struct NativeWheelOwn own;
	double drawn[4];
	double step[4];
	const unsigned long long ticksBefore = s_nwStrobeTicks;
	const unsigned long long clampedBefore = s_nwStrobeClamped;
	const double stepMaxBefore = s_nwStrobeStepMax;
	const double drawnMaxBefore = s_nwStrobeDrawnMax;
	const double halfPitch8 = (0.5 * NATIVE_WHEELS_TWO_PI) / 8.0;
	int estimatesHeld = 0;
	int clampHeld = 1;
	int c;
	u32 n;

	// The meshes.
	n = NativeWheels_TestRing(p, 0, 16.0, 8, lug8, 2);
	n = NativeWheels_TestRing(p, n, 14.0, 8, groove, 1);
	n = NativeWheels_TestRing(p, n, 8.0, 24, one, 1);
	NativeWheels_EstimateTreads(&p[0][0], sizeof(p[0]), n, &t[0]);
	n = NativeWheels_TestRing(p, 0, 16.0, 12, lug12, 4);
	n = NativeWheels_TestRing(p, n, 14.0, 12, one, 1);
	NativeWheels_EstimateTreads(&p[0][0], sizeof(p[0]), n, &t[1]);
	n = NativeWheels_TestRing(p, 0, 16.0, 32, one, 1);
	n = NativeWheels_TestRing(p, n, 8.0, 32, one, 1);
	NativeWheels_EstimateTreads(&p[0][0], sizeof(p[0]), n, &t[2]);
	n = NativeWheels_TestRing(p, 0, 16.0, 10, one, 1);
	NativeWheels_EstimateTreads(&p[0][0], sizeof(p[0]), n, &t[3]);
	NativeWheels_EstimateTreads(NULL, sizeof(p[0]), 0, &t[4]);
	for (c = 0; c < 5; c++)
	{
		estimatesHeld += ((t[c].treads == expect[c]) && (t[c].estimated == expectEstimated[c])) ? 1 : 0;
	}

	// The clamp, on the seat table.
	memset(&driver, 0, sizeof(driver));
	memset(&inst, 0, sizeof(inst));
	driver.wheelSize = 0x0ccc;
	for (c = 0; c < 4; c++)
	{
		const struct NativeWheelPose *pose;

		NativeWheels_Forget();
		driver.speedApprox = speeds[c];
		own.radius[0] = 16.0;
		own.radius[1] = 16.0;
		own.treads[0] = treads[c];
		own.treads[1] = treads[c];
		own.rearOwn = 0;
		NativeWheels_Pull(0, &driver, &inst, 1, 1u, 32, 0, &own);
		NativeWheels_Pull(0, &driver, &inst, 1, 2u, 32, 0, &own);
		pose = NativeWheels_PoseOf(&inst);
		step[c] = 0.0;
		drawn[c] = 0.0;
		if (pose == NULL)
		{
			clampHeld = 0;
			continue;
		}
		step[c] = pose->rollStep;
		drawn[c] = pose->drawStep;
		if (treads[c] == 0)
		{
			clampHeld = clampHeld && (pose->drawStep == pose->rollStep);
			continue;
		}
		// Below half a pitch, the sign kept; as it is when small, else the clamp.
		clampHeld = clampHeld && (fabs(pose->drawStep) < halfPitch8) && ((pose->drawStep > 0.0) == (pose->rollStep > 0.0));
		if (fabs(pose->rollStep) <= NativeWheels_StrobeClamp(treads[c]))
		{
			clampHeld = clampHeld && (pose->drawStep == pose->rollStep);
		}
		else
		{
			clampHeld = clampHeld && (fabs(fabs(pose->drawStep) - NativeWheels_StrobeClamp(treads[c])) < 1e-12);
		}
	}
	NativeWheels_Forget();
	clampHeld = clampHeld && (fabs(step[0]) > halfPitch8) && (fabs(step[1]) > halfPitch8) && (fabs(step[2]) < halfPitch8);
	s_nwStrobeTicks = ticksBefore;
	s_nwStrobeClamped = clampedBefore;
	s_nwStrobeStepMax = stepMaxBefore;
	s_nwStrobeDrawnMax = drawnMaxBefore;

	snprintf(line, size,
	         "treads lugs 8 -> %d (%s, %.2f), lugs 12 -> %d (%s, %.2f), smooth 32 -> %d (%s), ring 10 -> %d (%s), none -> %d (%s), estimates %d of 5; "
	         "clamp 8 treads %.4f rad (half pitch %.4f): forward step %.4f drawn %.4f, backward step %.4f drawn %.4f, slow step %.4f drawn %.4f, "
	         "no clamp step %.4f drawn %.4f, clamp %s",
	         t[0].treads, t[0].estimated ? "estimated" : "default", t[0].strength, t[1].treads, t[1].estimated ? "estimated" : "default", t[1].strength,
	         t[2].treads, t[2].estimated ? "estimated" : "default", t[3].treads, t[3].estimated ? "estimated" : "default", t[4].treads,
	         t[4].estimated ? "estimated" : "default", estimatesHeld, NativeWheels_StrobeClamp(8), halfPitch8, step[0], drawn[0], step[1], drawn[1], step[2],
	         drawn[2], step[3], drawn[3], clampHeld ? "held" : "off");
	return (estimatesHeld == 5) && clampHeld;
}
