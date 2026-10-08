#ifndef NATIVE_WHEELS_H
#define NATIVE_WHEELS_H

// THE WHEEL POSES (platform/native_wheels.c): a float pose of the four kart
// wheels per seat, kept in a host table beside the game and moved on once per
// logic tick. Only reads the game; only runs while the native probe is on
// (--native-preview --native-probe). See the file for the rules and the sources.

#include <macros.h>

struct Driver;
struct Instance;

// --native-wheel-report (only with --dev, only with the probe on): one line per
// tick with the float wheel middles of the probe seat, its roll phase and its
// speed, and one with the middles of the retail wheels of that seat, which stay
// on for the probe seat under this switch. Never in ctr-settings.cfg.
extern int g_cfg_nativeWheelReport;

#define NATIVE_WHEELS_SEATS 8

// The wheels in the order of game/DrawTires.c (wheelLocal[0..3]): front +X,
// front -X, rear +X, rear -X (+X is left seen from behind).
#define NATIVE_WHEELS_COUNT 4

// THE SIGN CHECK of the report (render plan D.4, 3a: the roll phase follows the
// direction of travel). A tick counts as one with motion when the kart moved at
// least NATIVE_WHEELS_MOVING world units along its forward axis (a quarter unit
// is 64 of the 1/256 steps of posCurr). Every tick of a seat gets one class:
#define NATIVE_WHEELS_MOVING 0.25
enum NativeWheelSign
{
	NATIVE_WHEEL_SIGN_NONE,     // no way known for this tick (the first tick of an entry): not counted
	NATIVE_WHEEL_SIGN_STILL,    // |move| below NATIVE_WHEELS_MOVING: not counted
	NATIVE_WHEEL_SIGN_AGREE,    // moving, roll step not 0 and of the sign of move
	NATIVE_WHEEL_SIGN_DISAGREE, // moving, roll step not 0 and of the other sign
	NATIVE_WHEEL_SIGN_ZERO,     // moving, roll step exactly 0 (no wheel, speed 0): neither of the two above
	NATIVE_WHEEL_SIGNS
};

// The word of a class in the report line: none, still, agree, disagree, zero.
const char *NativeWheels_SignName(int sign);

struct NativeWheelPose
{
	int valid;
	u32 timer;         // gGT->timer of the tick the pose belongs to
	u32 ticks;         // ticks integrated since the entry was (re)started
	s16 speed;         // driver->speedApprox of that tick
	s16 wheelRotation; // driver->wheelRotation of that tick
	s16 hazardTimer;   // driver->hazardTimer of that tick
	u16 wheelSize;     // driver->wheelSize (0: no wheel)
	int moveKnown;     // 1 = move holds the way of this tick
	double move;       // way along the kart's forward axis in this tick, world units
	double rollStep;   // roll phase added in this tick, radians (+ = rolling forward)
	double roll;       // roll phase, radians, 0 to 2 pi
	double steer;      // steering angle of the front wheels, radians, from wheelRotation << 2
	int sign;          // enum NativeWheelSign of this tick
	// Per wheel, kart space (x, y, z of the model; z front), unit vectors: the
	// axle pointing outwards (steering and wobble included), and the two axes of
	// the rim plane after rolling. The columns of the wheel's rotation.
	double axis[NATIVE_WHEELS_COUNT][3][3]; // [wheel][row][column]: columns axle, rim up, rim front
};

// Once per pull, for every seat 0..NATIVE_WHEELS_SEATS - 1: the driver and
// instance of the seat (NULL when the seat is not ready), whether a new tick
// started (gGT->timer changed) and the tick's values. Writes only the table.
// counted: 1 for the seat the probe takes (--native-probe-seat), whose ticks
// alone go into the counters of NativeWheels_Counts.
void NativeWheels_Pull(int seat, const struct Driver *driver, const struct Instance *inst, int newTick, u32 timer, int elapsedTimeMS, int counted);

// Empties the table (checkpoint restore, the probe let go): every seat starts
// anew at its next pull.
void NativeWheels_Forget(void);

// The pose of the seat whose instance this is, NULL when there is none.
const struct NativeWheelPose *NativeWheels_PoseOf(const struct Instance *inst);

// The wheel middle in the units of game/DrawTires.c's local wheel points
// (DrawTires.c:338-366) before inst->scale: x +-0x90, y 0x40, z 0xc7 front and
// -0x60 rear.
void NativeWheels_LocalMiddle(int wheel, double out[3]);

// Radius and half width of the native wheel in world units: the radius is the
// half extent of the retail wheel quad, wheelSize / 64 in the four-times units of
// DrawTires.c:493-511, so wheelSize / 256; the half width is half the radius
// (a choice, retail has no width).
double NativeWheels_Radius(const struct NativeWheelPose *pose);
double NativeWheels_HalfWidth(const struct NativeWheelPose *pose);

// Counters for the exit report, of the counted seat only: ticks with a pose
// after the first of an entry, entries started, and the ticks per sign class
// (index enum NativeWheelSign).
void NativeWheels_Counts(unsigned long long *ticks, unsigned long long *starts, unsigned long long signs[NATIVE_WHEEL_SIGNS]);

#endif
