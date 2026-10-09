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
#define NATIVE_WHEELS_TREAD_GROOVE_FREE 24

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

internal double NativeWheels_AxleAngle(const float *p)
{
	double a = atan2((double)p[2], (double)p[1]);

	if (a < 0.0)
	{
		a += NATIVE_WHEELS_TWO_PI;
	}
	return a;
}

// The merged angle a point belongs to (within the merge distance, across
// 2 pi as well), -1 for none.
internal int NativeWheels_AngleSlot(const double *angle, u32 merged, double a)
{
	u32 i;

	for (i = 0; i < merged; i++)
	{
		double d = fabs(a - angle[i]);

		if (d > (NATIVE_WHEELS_TWO_PI * 0.5))
		{
			d = NATIVE_WHEELS_TWO_PI - d;
		}
		if (d <= NATIVE_WHEELS_TREAD_MERGE)
		{
			return (int)i;
		}
	}
	return -1;
}

// The plausible range of a result: 4 to 32 treads, else the default.
internal void NativeWheels_TreadsTake(struct NativeWheelTreads *out, int treads, double strength)
{
	if ((treads < NATIVE_WHEELS_TREADS_PLAUSIBLE_MIN) || (treads > NATIVE_WHEELS_TREADS_PLAUSIBLE_MAX))
	{
		out->treads = NATIVE_WHEELS_TREADS_DEFAULT;
		out->estimated = 0;
		out->strength = 0.0;
		out->implausible = treads;
		return;
	}
	out->treads = treads;
	out->estimated = 1;
	out->strength = strength;
}

void NativeWheels_EstimateTreads(const float *positions, size_t strideBytes, u32 count, const u16 *indices, u32 indexCount, u32 indexBase,
                                 struct NativeWheelTreads *out)
{
	static double angle[NATIVE_WHEELS_TREAD_POINTS];
	static u8 covered[NATIVE_WHEELS_TREAD_POINTS];
	static int slot[NATIVE_WHEELS_TREAD_POINTS];
	const u8 *base = (const u8 *)positions;
	double rmax = 0.0;
	u32 outer = 0;
	u32 merged = 0;
	u32 open = 0;
	int alternating = 1;
	u32 i;

	memset(out, 0, sizeof(*out));
	out->treads = NATIVE_WHEELS_TREADS_DEFAULT;
	out->open = -1;
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
			angle[outer++] = NativeWheels_AxleAngle(p);
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

	// THE GAPS between neighbouring outer angles: covered when an edge of a
	// triangle joins two outermost points of those two angles (the tyre runs
	// on at full radius - a lug top, a smooth tyre), open when none does (a
	// groove, the gap beside a spike or a loose part). Only with the triangles.
	if (indices != NULL)
	{
		memset(covered, 0, merged);
		for (i = 0; i < count; i++)
		{
			const float *p = (const float *)(const void *)(base + ((size_t)i * strideBytes));

			slot[i] = (NativeWheels_AxleRadius(p) >= (rmax * (1.0 - NATIVE_WHEELS_TREAD_BAND))) ? NativeWheels_AngleSlot(angle, merged, NativeWheels_AxleAngle(p))
			                                                                                     : -1;
		}
		for (i = 0; (i + 2u) < indexCount; i += 3u)
		{
			int e;

			for (e = 0; e < 3; e++)
			{
				const u32 a = (u32)indices[i + (u32)e] - indexBase;
				const u32 b = (u32)indices[i + (u32)((e + 1) % 3)] - indexBase;
				int sa;
				int sb;

				if ((a >= count) || (b >= count))
				{
					continue;
				}
				sa = slot[a];
				sb = slot[b];
				if ((sa < 0) || (sb < 0))
				{
					continue;
				}
				if ((u32)sb == (((u32)sa + 1u) % merged))
				{
					covered[sa] = 1;
				}
				else if ((u32)sa == (((u32)sb + 1u) % merged))
				{
					covered[sb] = 1;
				}
			}
		}
		for (i = 0; i < merged; i++)
		{
			open += covered[i] ? 0u : 1u;
			if (covered[i] == covered[(i + 1u) % merged])
			{
				alternating = 0;
			}
		}
		out->open = (int)open;
	}

	// A ring of evenly spaced angles. With the triangles: no open gap is a
	// smooth tyre (its count below 16 - a coarse polygon -, else the default),
	// every gap open are spikes (their count), every second gap open are lugs
	// whose tops fill half the pitch (half the count); anything else the
	// default. Without them: the count below 16, else the default.
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
			if ((out->open > 0) && (open == merged))
			{
				NativeWheels_TreadsTake(out, (int)merged, 1.0);
			}
			else if ((out->open > 0) && alternating && ((merged & 1u) == 0u))
			{
				NativeWheels_TreadsTake(out, (int)(merged / 2u), 1.0);
			}
			else if ((out->open <= 0) && (merged < (u32)NATIVE_WHEELS_TREAD_RING_MAX))
			{
				NativeWheels_TreadsTake(out, (int)merged, 1.0);
			}
			return;
		}
	}

	// Any other set: the smallest n-fold periodicity that is strong enough.
	// Above NATIVE_WHEELS_TREAD_GROOVE_FREE treads only with open gaps for at
	// least half of them (a smooth tyre with one extra point - a valve, a seam
	// - is periodic in its own facets, not in a tread); with the triangles, no
	// open gap at all is no tread either.
	{
		int n;

		if (out->open == 0)
		{
			return;
		}
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
				if ((n > NATIVE_WHEELS_TREAD_GROOVE_FREE) && ((out->open < 0) || ((u32)out->open < ((u32)n / 2u))))
				{
					out->implausible = n;
					return;
				}
				NativeWheels_TreadsTake(out, n, strength);
				return;
			}
		}
	}
}

// THE SELF-TEST of the stroboscope. Made-up tyres of radius 16 (model units):
// a profile round the wheel - (angle in degrees, radius) points, a lug top at
// 16, a groove floor at 14 - extruded from x -6 to x +6, two triangles per
// step, so neighbouring points of the profile share an edge on each side:
//   lugs 8        8 lug tops of 16 degrees, walls down to the grooves: 8
//   lugs 12       12 lug tops with bevels (4 angles each): 12
//   half 12       12 lug tops of half the pitch (15 degrees, 24 even angles,
//                 every second gap a groove): 12
//   smooth 32     an even ring of 32: the default 8
//   seam 32       the ring of 32 with one more point on the rim: the default 8
//   valve 32      the ring of 32 and a loose triangle with one point on the
//                 rim: the default 8 (32 would need 16 open gaps)
//   ring 10       an even ring of 10 (a coarse polygon): 10
//   spikes 40     40 spikes: 40 is past 32, the default 8
//   lugs 3        3 lugs: below 4, the default 8
//   none          no point: the default 8
// and two ticks of a made-up driver at radius 16, wheelSize 0x0ccc, 32 ms:
// speed 3000 forward and backward with 8 treads (true step 0.916 rad, more
// than half the pitch 0.785) are drawn clamped with the sign kept, speed 200
// as it is, and treads 0 draws the true step. Leaves the pose table empty and
// the counters as they were.
#define NATIVE_WHEELS_TEST_PROFILE 160
#define NATIVE_WHEELS_TEST_POINTS ((NATIVE_WHEELS_TEST_PROFILE * 2) + 3)
#define NATIVE_WHEELS_TEST_INDICES ((NATIVE_WHEELS_TEST_PROFILE * 6) + 3)

struct NativeWheelsTestMesh
{
	float p[NATIVE_WHEELS_TEST_POINTS][3];
	u16 index[NATIVE_WHEELS_TEST_INDICES];
	u32 points;
	u32 indices;
	double profile[NATIVE_WHEELS_TEST_PROFILE][2]; // degrees, radius
	u32 profileCount;
};

internal void NativeWheels_TestPoint(struct NativeWheelsTestMesh *m, double degrees, double radius)
{
	if (m->profileCount < NATIVE_WHEELS_TEST_PROFILE)
	{
		m->profile[m->profileCount][0] = degrees;
		m->profile[m->profileCount][1] = radius;
		m->profileCount++;
	}
}

// The profile extruded: point k on +x is 2k, on -x 2k + 1; step k to k + 1 two
// triangles.
internal void NativeWheels_TestExtrude(struct NativeWheelsTestMesh *m)
{
	u32 k;

	m->points = 0;
	m->indices = 0;
	for (k = 0; k < m->profileCount; k++)
	{
		const double a = (m->profile[k][0] * NATIVE_WHEELS_TWO_PI) / 360.0;
		int side;

		for (side = 0; side < 2; side++)
		{
			m->p[m->points][0] = (side == 0) ? 6.0f : -6.0f;
			m->p[m->points][1] = (float)(m->profile[k][1] * cos(a));
			m->p[m->points][2] = (float)(m->profile[k][1] * sin(a));
			m->points++;
		}
	}
	for (k = 0; k < m->profileCount; k++)
	{
		const u16 a = (u16)(2u * k);
		const u16 b = (u16)(2u * ((k + 1u) % m->profileCount));

		m->index[m->indices++] = a;
		m->index[m->indices++] = b;
		m->index[m->indices++] = (u16)(a + 1u);
		m->index[m->indices++] = b;
		m->index[m->indices++] = (u16)(b + 1u);
		m->index[m->indices++] = (u16)(a + 1u);
	}
}

// count lugs, each of the given top angles (degrees about its middle) at 16,
// the groove floor at 14 from the last top angle to the next lug's first.
internal void NativeWheels_TestLugs(struct NativeWheelsTestMesh *m, int lugs, const double *top, int topCount)
{
	int l;
	int t;

	m->profileCount = 0;
	for (l = 0; l < lugs; l++)
	{
		const double middle = (360.0 * (double)l) / (double)lugs;

		for (t = 0; t < topCount; t++)
		{
			NativeWheels_TestPoint(m, middle + top[t], 16.0);
		}
		NativeWheels_TestPoint(m, middle + top[topCount - 1], 14.0);
		NativeWheels_TestPoint(m, middle + (360.0 / (double)lugs) + top[0], 14.0);
	}
	NativeWheels_TestExtrude(m);
}

internal void NativeWheels_TestRingMesh(struct NativeWheelsTestMesh *m, int steps, int seam)
{
	int k;

	m->profileCount = 0;
	for (k = 0; k < steps; k++)
	{
		NativeWheels_TestPoint(m, (360.0 * (double)k) / (double)steps, 16.0);
		if (seam && (k == 0))
		{
			NativeWheels_TestPoint(m, 180.0 / (double)steps, 16.0);
		}
	}
	NativeWheels_TestExtrude(m);
}

int NativeWheels_StrobeSelfTest(char *line, size_t size)
{
	enum
	{
		CASES = 10
	};
	static struct NativeWheelsTestMesh m;
	static struct Driver driver;
	static struct Instance inst;
	static const char *const names[CASES] = {"lugs 8", "lugs 12", "half 12", "smooth 32", "seam 32", "valve 32", "ring 10", "spikes 40", "lugs 3", "none"};
	static const int expect[CASES] = {8, 12, 12, 8, 8, 8, 10, 8, 8, 8};
	static const int expectEstimated[CASES] = {1, 1, 1, 0, 0, 0, 1, 0, 0, 0};
	static const double top8[2] = {-8.0, 8.0};
	static const double top12[4] = {-6.0, -3.0, 3.0, 6.0};
	static const double half12[2] = {-7.5, 7.5};
	static const double spike[1] = {0.0};
	static const double top3[2] = {-20.0, 20.0};
	static const s16 speeds[4] = {3000, -3000, 200, 3000};
	static const int treads[4] = {8, 8, 8, 0};
	struct NativeWheelTreads t[CASES];
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
	char *at = line;
	size_t left = size;
	int written;

	// The meshes.
	for (c = 0; c < CASES; c++)
	{
		memset(&m, 0, sizeof(m));
		switch (c)
		{
		case 0:
			NativeWheels_TestLugs(&m, 8, top8, 2);
			break;
		case 1:
			NativeWheels_TestLugs(&m, 12, top12, 4);
			break;
		case 2:
			NativeWheels_TestLugs(&m, 12, half12, 2);
			break;
		case 3:
			NativeWheels_TestRingMesh(&m, 32, 0);
			break;
		case 4:
			NativeWheels_TestRingMesh(&m, 32, 1);
			break;
		case 5:
			// The ring and a loose triangle: one point on the rim between two
			// facets, two below it.
			NativeWheels_TestRingMesh(&m, 32, 0);
			{
				static const double valve[3][2] = {{5.625, 16.0}, {4.0, 15.0}, {7.0, 15.0}};
				int v;

				for (v = 0; v < 3; v++)
				{
					const double a = (valve[v][0] * NATIVE_WHEELS_TWO_PI) / 360.0;

					m.p[m.points][0] = 6.0f;
					m.p[m.points][1] = (float)(valve[v][1] * cos(a));
					m.p[m.points][2] = (float)(valve[v][1] * sin(a));
					m.index[m.indices++] = (u16)m.points;
					m.points++;
				}
			}
			break;
		case 6:
			NativeWheels_TestRingMesh(&m, 10, 0);
			break;
		case 7:
			NativeWheels_TestLugs(&m, 40, spike, 1);
			break;
		case 8:
			NativeWheels_TestLugs(&m, 3, top3, 2);
			break;
		default:
			break;
		}
		if (c == (CASES - 1))
		{
			NativeWheels_EstimateTreads(NULL, sizeof(m.p[0]), 0, NULL, 0, 0, &t[c]);
		}
		else
		{
			NativeWheels_EstimateTreads(&m.p[0][0], sizeof(m.p[0]), m.points, m.index, m.indices, 0, &t[c]);
		}
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

	written = snprintf(at, left, "treads");
	for (c = 0; (c < CASES) && (written > 0) && ((size_t)written < left); c++)
	{
		at += written;
		left -= (size_t)written;
		written = snprintf(at, left, "%s %s -> %d (%s)", (c == 0) ? "" : ",", names[c], t[c].treads, t[c].estimated ? "estimated" : "default");
	}
	if ((written > 0) && ((size_t)written < left))
	{
		at += written;
		left -= (size_t)written;
		snprintf(at, left,
		         ", estimates %d of %d; clamp 8 treads %.4f rad (half pitch %.4f): forward step %.4f drawn %.4f, backward step %.4f drawn %.4f, "
		         "slow step %.4f drawn %.4f, no clamp step %.4f drawn %.4f, clamp %s",
		         estimatesHeld, CASES, NativeWheels_StrobeClamp(8), halfPitch8, step[0], drawn[0], step[1], drawn[1], step[2], drawn[2], step[3], drawn[3],
		         clampHeld ? "held" : "off");
	}
	return (estimatesHeld == CASES) && clampHeld;
}
