#ifndef NATIVE_HOVER_H
#define NATIVE_HOVER_H

// THE HOVER ON SCREEN (platform/native_hover.c; round H1 (the hover),
// step 4). A seat whose .rldchar has the vehicle class hover (CHRI byte 0x27,
// NativeChar_SeatVehicleClass) is DRAWN higher by the native engine: its hi-res
// body, figure and wheels (the route's world matrix, after
// Engine_InstanceWorldMatrix in Engine_HiresCharRoute) are moved up by
//   lift = height x (1 + bob % / 100 x sin(2 pi x (tick mod period) / period))
// in world units, height = the HEIGHT in kit metres x 64 (model units per
// metre, RLDHIRES_UNITS_PER_METRE) x inst->scale.y / 4096 - it scales with the
// driver like its body and its own wheels. The start height is the radius of
// the original kart's wheel: 16 model units (ENG_HC_STD_RADIUS) = 0.25 kit
// metres = 12.80 world units at the retail driver scale 0xccc
// (VehBirth.c VEH_BIRTH_DRIVER_INSTANCE_SCALE); the bob 10 % of it (1.28 world
// units) over 30 ticks (about one second).
//
// PURELY VISUAL. Nothing here writes game state: inst->matrix.t, posCurr,
// collision and the camera stay where the game has them, and so do the shadow
// (posCurr / quadBlockHeight), the skid marks and the exhaust (inst->matrix.t,
// the CHRI look points) - the hover is physically on the ground. The class is
// read only here and in the engine's level load; no game file reads it.
// Without the engine (--no-engine, the PS1 path) nothing of this runs, and a
// hover seat whose hi-res character is not drawn (refused, the classic CMDL
// model) is drawn on the ground like a kart (the level load says so).
//
// THE SHAPE KEYS (include/platform/engine.h THE SHAPE KEYS), by name, when the
// seat's hi-res character has them: lean_left / lean_right follow the steering
// (d->wheelRotation, positive = left, 64 = full; include/namespace_Vehicle.h
// "Steering left ranges between 0 to 64"), nose_up the gas (CROSS held without
// SQUARE: the gas branch of VehPhysProc.c "if (square == 0)"), nose_down the
// brake (SQUARE held: the brake branch), the pad read as the kart's own input
// reads it (VehPhysProc.c: gamepad[driverID], nothing in the end-of-race
// menu); a CPU driver (ACTION_BOT) has no pad, so its nose keys stay 0. Each
// weight goes towards target x strength by at most NATIVE_HOVER_RAMP per tick.
// A missing key is simply not there (Engine_HiresShapeKeySet answers 0).
// PRECEDENCE: the coupling writes only for a seat of class hover, only these
// four names. For seat 0 it holds (writes nothing, takes over what is shown)
// while the debug menu's SHAPE KEYS page is open, and for the whole run when
// --dev-shape-keys or --dev-shape-chain is given: there the page / the switch
// wins. The glide binding (platform/native_glide.c) writes other names (the
// wings chain, flap, dive, pull_up), so the two never write the same key.
//
// THE PAGE "HOVER" of the debug menu (game/DebugMenu.c): on, height, bob,
// period, lean, nose, DEFAULTS. Runtime only, nothing is stored.

#define NATIVE_HOVER_SEATS 8

// The coupled keys, in this order everywhere (names in NativeHover_KeyName).
enum
{
	NATIVE_HOVER_KEY_LEAN_LEFT = 0,
	NATIVE_HOVER_KEY_LEAN_RIGHT,
	NATIVE_HOVER_KEY_NOSE_UP,
	NATIVE_HOVER_KEY_NOSE_DOWN,
	NATIVE_HOVER_KEYS,
};

// The values of the page (integers, like the glide page).
typedef struct
{
	int on;          // 1 = lift and keys (start 1); 0 = no lift, the keys go back to 0
	int heightMm;    // the height in kit millimetres (start 250 = 0.25 m)
	int bobPct;      // the bob amplitude in percent of the height (start 10)
	int bobFrames;   // the bob period in logic ticks (start 30)
	int lean;        // lean strength in thousandths (start 1000 = 1.0)
	int nose;        // nose strength in thousandths (start 1000)
} NativeHoverTune;

#define NATIVE_HOVER_HEIGHT_MM 250       // 16 model units / 64 per metre: the original kart's wheel radius
#define NATIVE_HOVER_BOB_PCT 10
#define NATIVE_HOVER_BOB_FRAMES 30
#define NATIVE_HOVER_STRENGTH 1000
// The page's ranges (a value is clamped into them before use).
#define NATIVE_HOVER_HEIGHT_MM_MAX 1000
#define NATIVE_HOVER_BOB_PCT_MAX 100
#define NATIVE_HOVER_BOB_FRAMES_MIN 2
#define NATIVE_HOVER_BOB_FRAMES_MAX 600
#define NATIVE_HOVER_STRENGTH_MAX 2000
// A key weight moves by at most this per tick (0 -> 1 in 4 ticks).
#define NATIVE_HOVER_RAMP 0.25f

// One seat's state (the module keeps eight; the selftest makes its own).
typedef struct
{
	int vehicle;                    // the class seen at the level load (RLDCHAR_VEHICLE_*); anything but hover = no lift, no keys
	u32 phase;                      // logic ticks since the level load (the bob)
	float w[NATIVE_HOVER_KEYS];     // the coupled weights as drawn (after the ramps)
} NativeHoverSeat;

// What the coupling reads of a tick (filled from the driver and its pad).
typedef struct
{
	int wheelRotation; // d->wheelRotation: + left, - right, 64 = full
	int gas;           // 1 = CROSS held without SQUARE (a human, not at the end of the race)
	int brake;         // 1 = SQUARE held
} NativeHoverIn;

// THE PURE PARTS (ctest hover_selftest, platform/native_hover_selftest.c).
void NativeHover_TuneDefaults(NativeHoverTune *t);
void NativeHover_TuneClamp(NativeHoverTune *t);
const char *NativeHover_KeyName(int key); // "lean_left" ...; "" outside 0..3
// The height in world units at a driver scale (scaleY = inst->scale.y), no bob.
double NativeHover_HeightWE(const NativeHoverTune *t, int scaleY);
// The lift in world units: exactly 0.0 unless vehicleClass is hover and the tune is on.
double NativeHover_Lift(int vehicleClass, const NativeHoverTune *t, int scaleY, u32 phase);
// One step from cur towards target by at most step (target clamped to 0..1).
float NativeHover_Ramp(float cur, float target, float step);
// The four targets of a tick (0..1, strength applied).
void NativeHover_Targets(const NativeHoverTune *t, const NativeHoverIn *in, float target[NATIVE_HOVER_KEYS]);
// One tick of a seat's keys: 1 = s->w ramped and copied into out (to be
// written); 0 = not a hover - nothing changed, nothing to write.
int NativeHover_KeyStep(NativeHoverSeat *s, const NativeHoverTune *t, const NativeHoverIn *in, float out[NATIVE_HOVER_KEYS]);

// THE MODULE (the engine and the page call these).
NativeHoverTune *NativeHover_Tunables(void);
void NativeHover_LevelReset(void); // every seat a kart, phase 0, weights 0 (every level load, also without the engine)
// The engine's level load, per seat whose hi-res character is ready: the class,
// the file (for the log line), whether the character is drawn by its hi-res
// route (0 = the classic model: no lift, logged).
void NativeHover_LevelSeat(int seat, int vehicleClass, const char *file, int hires);
// The engine's seat tick (once per logic tick, before the key chain steps):
// the bob phase and, with keys, the coupling.
void NativeHover_EngineTick(int seat, const struct Driver *d, int hasKeys);
// The route: the lift of this seat's draw (0.0 for every seat not of class hover).
double NativeHover_SeatLift(int seat, int scaleY);
int NativeHover_SeatVehicle(int seat); // the class seen at the level load (kart for an unknown seat)
void NativeHover_Report(void);     // the line at exit (only in a run that saw a hover seat)

// game/DebugMenu.c: 1 while the debug menu is open on its SHAPE KEYS page.
int DebugMenu_ShapePreviewOpen(void);

int NativeHover_SelfTest(void);

#endif
