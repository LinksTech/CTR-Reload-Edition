#ifndef NATIVE_WHEELS_H
#define NATIVE_WHEELS_H

// THE WHEEL POSES (platform/native_wheels.c): a float pose of the four kart
// wheels per seat, kept in a host table beside the game and moved on once per
// logic tick. Only reads the game; only runs while the native probe is on
// (--native-preview --native-probe) or a custom character is drawn natively
// (--native-preview, a bound seat). See the file for the rules and the sources.

#include <stddef.h>

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
	double ownRadius;  // the radius of the seat's own front wheel (WHLS 2 or 3), model units; 0 = the retail size
	int moveKnown;     // 1 = move holds the way of this tick
	double move;       // way along the kart's forward axis in this tick, world units
	double rollStep;   // roll phase of the way of this tick, radians (+ = rolling forward)
	int treads;        // the tread count of the own wheel the drawing is clamped to; 0 = no clamp
	double drawStep;   // roll phase drawn in this tick: rollStep, clamped below half a tread pitch
	double roll;       // drawn roll phase, radians, 0 to 2 pi (the sum of drawStep)
	// The rear wheels: the same as the four above for an own rear wheel (WHLS 3
	// with a rear mesh of its own radius and treads); else equal to them.
	double ownRadiusRear;
	double rollStepRear;
	int treadsRear;
	double drawStepRear;
	double rollRear;
	double steer;      // steering angle of the front wheels, radians, from wheelRotation << 2
	int sign;          // enum NativeWheelSign of this tick
	// Per wheel, kart space (x, y, z of the model; z front), unit vectors: the
	// axle pointing outwards (steering and wobble included), and the two axes of
	// the rim plane after rolling. The columns of the wheel's rotation.
	double axis[NATIVE_WHEELS_COUNT][3][3]; // [wheel][row][column]: columns axle, rim up, rim front
};

// THE OWN WHEELS of a seat (WHLS 2 or 3), for NativeWheels_Pull: per axle
// (0 front, 1 rear) the radius of the mesh in model units and the tread count
// of the stroboscope clamp (0 = no clamp). rearOwn: the rear wheels have a
// mesh of their own (WHLS 3), counted apart in the stroboscope counters; else
// the rear values are the front ones.
struct NativeWheelOwn
{
	double radius[2];
	int treads[2];
	int rearOwn;
};

// Once per pull, for every seat 0..NATIVE_WHEELS_SEATS - 1: the driver and
// instance of the seat (NULL when the seat is not ready), whether a new tick
// started (gGT->timer changed) and the tick's values. Writes only the table.
// counted: 1 for the seat the probe takes (--native-probe-seat), whose ticks
// alone go into the counters of NativeWheels_Counts. own: the own wheels of a
// custom character drawn natively on the seat - each axle rolls over its
// radius, and its drawn roll is clamped by its tread count (see THE
// STROBOSCOPE); NULL for every other seat (the roll then follows the retail
// size and is drawn as it is).
void NativeWheels_Pull(int seat, const struct Driver *driver, const struct Instance *inst, int newTick, u32 timer, int elapsedTimeMS, int counted,
                       const struct NativeWheelOwn *own);

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

// THE OWN WHEEL (WHLS 2) of a custom character: its mesh is in model units and
// is drawn at the scale wheelSize / 4096 - the size retail gives its wheel
// (DrawTires.c:370, 493-511: radius wheelSize / 256 world units = 16 model units
// times wheelSize / 4096), which is the body's scale at rest (inst->scale and
// wheelSize are both 0xccc, game/Vehicle/VehBirth.c:23, 31). Squash and stretch
// move the middles (inst->scale) and leave the mesh as it is, as for the retail
// wheels. OwnScale: that scale (0 without a wheel); RollRadius: the radius the
// roll of the pose was integrated with, world units (the own radius times
// OwnScale, else NativeWheels_Radius).
double NativeWheels_OwnScale(const struct NativeWheelPose *pose);
double NativeWheels_RollRadius(const struct NativeWheelPose *pose);

// THE STROBOSCOPE (render plan A3). At top speed an own wheel turns by more
// than half a tread pitch per tick, and its tread seems to stand or to turn
// backwards. The drawn roll step is therefore clamped to NATIVE_WHEELS_STROBE_FRACTION
// of the pitch 2 pi / treads, its sign kept; the way, the sign check and the
// report keep the true step (rollStep). Drawing only: the pose table is all
// this changes. treads is the count the file stores for the mesh (WHLS 3,
// worked out by make-char from the geometry, include/rldtread.inc); without
// one NATIVE_WHEELS_TREADS_SAFE: 0.30 of 1/32 of a turn is below half the
// pitch of every tread of up to 32 (RLDCHAR_WHEEL_TREADS_MAX), so no tread
// a file can describe ever seems to stand or to turn backwards - only a
// wheel with fewer treads looks slower at top speed. The game never guesses
// a count of its own. 0.30 and not just under 1/2: a step near half a pitch
// is nearly as close to the step backwards (the fraction minus 1) as to its
// own, and the tread seems to flicker in place; at 0.30 it clearly rolls
// forward, at top speed 0.30 / 0.45 = 2/3 as fast as at 0.45.
#define NATIVE_WHEELS_STROBE_FRACTION 0.30
#define NATIVE_WHEELS_TREADS_SAFE 32

// The clamp of the drawn roll step for a tread count, radians per tick; 0 for
// treads 0 (no clamp).
double NativeWheels_StrobeClamp(int treads);

// The stroboscope counters of every seat with a clamp (an own wheel): ticks
// with a roll step, ticks clamped, and the largest true and drawn steps in
// tread pitches (the drawn one stays at NATIVE_WHEELS_STROBE_FRACTION or below).
void NativeWheels_StrobeCounts(unsigned long long *ticks, unsigned long long *clamped, double *stepMax, double *drawnMax);

// --native-depth-selftest, a line of its own: the safe clamp against every
// tread count 4..32 (at most NATIVE_WHEELS_STROBE_FRACTION of a pitch, a
// forward step stays forward), a stored count taking the place of the safe one, and the clamp on made-up
// ticks. 1 = passed; line gets the report.
int NativeWheels_StrobeSelfTest(char *line, size_t size);

// Counters for the exit report, of the counted seat only: ticks with a pose
// after the first of an entry, entries started, and the ticks per sign class
// (index enum NativeWheelSign).
void NativeWheels_Counts(unsigned long long *ticks, unsigned long long *starts, unsigned long long signs[NATIVE_WHEEL_SIGNS]);

#endif
