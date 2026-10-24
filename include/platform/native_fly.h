#ifndef NATIVE_FLY_H
#define NATIVE_FLY_H

// FREE FLY (platform/native_fly.c; round "Free Fly simple"). A mod of the
// MODS page (NATIVE_MODS_FREEFLY, include/platform/native_mods.h): player 1
// (seat 0, a human driver) whose .rldchar has the vehicle class hover or plane
// (NativeChar_SeatVehicleClass) flies with R1 instead of hopping and drifting:
//   R1 pressed on the ground   take off
//   R1 held                    climb (climb rate WE per tick, start 25)
//   R1 released                hold the height (no gravity, vertical speed 0)
//   R1 pressed twice within    the flight ends: the game's gravity until the
//   double-tap ticks (10)      ground
//   the ground                 the flight ends; a new R1 press takes off again
// For this driver R1 and L1 never hop, drift, powerslide or give a drift
// turbo, and the air time (the hang-time turbo) stands still in every tick of
// a flight. Steering, gas and brake are the game's own in the air (the forward
// speed is not touched).
//
// THE CEILING: min(ground below + band, highest track point + margin). The
// ground below is the game's own ray (COLL_SearchBSP_CallbackQUADBLK, as the
// bots ask it) with the scratchpad and the GTE registers saved and restored;
// over nothing the last valid ground. The highest track point is computed at
// the level load from the loaded level's quadblocks (GROUND, not
// NO_COLLISION_RESPONSE, not KILL_PLANE; all 9 vertices). Climbing stops at
// the ceiling; above it (the ground fell away) the kart sinks to it at most at
// the game's terminal velocity per tick.
//
// THE SCOPE GATE (NativeFly_InScope): the mod active for this race AND
// driverID 0 AND not ACTION_BOT AND vehicle class hover or plane. For every
// other driver each hook hands its input back at once (the CPU drivers never
// reach them: BOTS_ThTick_Drive is their tick, not the driver function table).
//
// THE HOOKS (game code):
//   NativeFly_KartButtons  game/Vehicle/VehPhysProc.c, where the kart reads
//                          its pad (PhysLinear) and the drift start on landing:
//                          R1 and L1 taken out of the local copy (never the
//                          shared pad struct)
//   NativeFly_Gravity      game/Vehicle/VehPhysForce.c, VehPhysForce_OnGravity
//                          after the glide hook: steps the state machine once
//                          per logic tick; 0 while flying
//   NativeFly_Velocity     the end of VehPhysForce_OnGravity, after the local
//                          velocity is rotated back: the world Y velocity of a
//                          flying tick (climb rate, 0, or what the ceiling
//                          leaves)
//   NativeFly_AirTimeStill platform/native_glide.c NativeGlide_LandingBoost
//                          (game/COLL.c): 1 = the air time stands still

struct Driver;
struct GameTracker;
struct Level;

// The start values (debug menu page FREE FLY); runtime only, not saved.
#define NATIVE_FLY_CLIMB_DEFAULT 25    // WE per tick
#define NATIVE_FLY_TAP_DEFAULT 10      // ticks between the two presses of a double tap
#define NATIVE_FLY_BAND_DEFAULT 1500   // WE above the ground below
#define NATIVE_FLY_MARGIN_DEFAULT 500  // WE above the highest track point
#define NATIVE_FLY_CLIMB_MIN 5
#define NATIVE_FLY_CLIMB_MAX 80
#define NATIVE_FLY_TAP_MIN 2
#define NATIVE_FLY_TAP_MAX 30
#define NATIVE_FLY_BAND_MIN 100
#define NATIVE_FLY_BAND_MAX 1850       // + NATIVE_FLY_CAM_HEIGHT stays below NATIVE_FLY_PVS_LIMIT
#define NATIVE_FLY_MARGIN_MIN 0
#define NATIVE_FLY_MARGIN_MAX 2000

// Fixed: the highest camera height above the kart of the game's camera table
// (vertDistance 0x40 / 0x60 / 0xA0, game/zGlobal_DATA.c:983-1017), the reach
// of the camera's ground search (0x800 below it, game/CAM.c:632-641), the
// highest ceiling (s16 positions in the collision, game/COLL.c:1294-1306), the
// ray of the ground below (from 0x40 above the kart to 0x1000 below it) and
// the sink per tick above the ceiling (the game's const_TerminalVelocity,
// 20480 = 80 WE per tick, game/zGlobal_DATA.c:7125-7148).
#define NATIVE_FLY_CAM_HEIGHT 0xA0
#define NATIVE_FLY_PVS_LIMIT 0x800
#define NATIVE_FLY_CEILING_MAX 32000
#define NATIVE_FLY_RAY_UP 0x40
#define NATIVE_FLY_RAY_DOWN 0x1000
#define NATIVE_FLY_SINK_MAX 20480

typedef struct
{
	int climbRate; // WE per tick while R1 is held
	int doubleTap; // ticks: a press at most this many ticks after the one before ends the flight
	int band;      // WE above the ground below
	int margin;    // WE above the highest track point
} NativeFlyTune;

void NativeFly_TuneDefaults(NativeFlyTune *t);
void NativeFly_TuneClamp(NativeFlyTune *t);

// THE SCOPE RULE (pure): 1 only for the mod active, driverID 0, not a bot and
// vehicle class 1 (hover) or 2 (plane). NativeFly_InScope asks it.
int NativeFly_ScopeRule(int modActive, int driverID, int bot, int vehicle);

// THE STATE MACHINE (pure).
#define NATIVE_FLY_GROUND 0 // no flight (on the ground, or in the air without one)
#define NATIVE_FLY_CLIMB 1  // flying, R1 held: climbing
#define NATIVE_FLY_HOLD 2   // flying, R1 released: holding the height
#define NATIVE_FLY_FALL 3   // after a double tap: the game's gravity until the ground

#define NATIVE_FLY_EV_NONE 0
#define NATIVE_FLY_EV_TAKEOFF 1    // R1 pressed on the ground
#define NATIVE_FLY_EV_DOUBLE_TAP 2 // the second press within doubleTap ticks: CLIMB/HOLD -> FALL
#define NATIVE_FLY_EV_LAND 3       // the ground after at least one air tick: -> GROUND
#define NATIVE_FLY_EV_END 4        // not eligible any more (mask grab, a hit, the finish): -> GROUND
#define NATIVE_FLY_EV_ABORT 5      // the take-off never left the ground: -> GROUND

#define NATIVE_FLY_TAP_NONE 1000000 // sinceTap with no press to count from

typedef struct
{
	int state;    // NATIVE_FLY_*
	int heldPrev; // R1 held at the step before (a press is the edge)
	int sinceTap; // ticks since the last press of this flight (NATIVE_FLY_TAP_NONE none)
	int airSeen;  // the flight had an air tick
} NativeFlyState;

typedef struct
{
	int eligible; // in scope and may fly now (KS_NORMAL, not END_OF_RACE)
	int onGround; // ACTION_TOUCH_GROUND
	int r1Held;   // BTN_R1 held on the pad (read only)
} NativeFlyIn;

typedef struct
{
	int event;    // NATIVE_FLY_EV_*
	int pressed;  // the R1 edge of this step
	int flying;   // CLIMB or HOLD after the step: gravity 0 and the Y velocity written
	int climbing; // CLIMB after the step
} NativeFlyOut;

void NativeFly_StateReset(NativeFlyState *s);
// One logic tick. Rules, in this order: sinceTap counts up (up to
// NATIVE_FLY_TAP_NONE); not eligible -> GROUND (EV_END if it was flying);
// GROUND: a press on the ground -> CLIMB (EV_TAKEOFF, sinceTap 0, airSeen 0);
// CLIMB/HOLD: the ground after an air tick -> GROUND (EV_LAND); else an air
// tick sets airSeen, a press with sinceTap <= doubleTap -> FALL (EV_DOUBLE_TAP),
// another press sets sinceTap 0; still flying: CLIMB while held, HOLD
// otherwise, and HOLD on the ground without an air tick -> GROUND (EV_ABORT);
// FALL: an air tick sets airSeen, the ground -> GROUND (EV_LAND after an air
// tick, EV_ABORT before one); presses do nothing.
void NativeFly_Step(NativeFlyState *s, const NativeFlyTune *t, const NativeFlyIn *in, NativeFlyOut *out);

// THE CEILING (pure). Ground: hit 1 = groundY is this tick's ground below,
// else the last valid one stays (valid 0 = none yet: then the kart's own Y).
// All heights in WE.
typedef struct
{
	int valid;
	int groundY;
} NativeFlyGround;

void NativeFly_GroundNote(NativeFlyGround *g, int hit, int groundY, int kartY);
// min(groundY + band, cap), at most NATIVE_FLY_CEILING_MAX.
int NativeFly_Ceiling(int groundY, int band, int cap);
// The highest track point + margin, at most NATIVE_FLY_CEILING_MAX.
int NativeFly_Cap(int highestY, int margin);
// The world Y velocity of a flying tick (1/256 WE per 32 ms): climbRate WE per
// tick while climbing, else 0; never above what moves the kart to the ceiling
// in this tick (posY and ceiling in WE << 8, ms the tick's elapsedTimeMS, 1..),
// and above the ceiling down to it, at most NATIVE_FLY_SINK_MAX per tick.
// *capped 1 when the ceiling took something off.
int NativeFly_VelocityY(int climbing, int climbRate, int posY, int ceiling, int ms, int *capped);
// The highest Y of the quads of a level that count as track (GROUND, not
// NO_COLLISION_RESPONSE, not KILL_PLANE; all 9 vertices); flags[i] and the
// 9 vertex Y of quad i at ys[9 * i]. 0 = no such quad (*highest untouched).
int NativeFly_HighestOf(int count, const unsigned short *flags, const short *ys, int *highest);

// THE SCRIPT (--dev-fly-script, dev only; pure): a comma list played in turn
// after NATIVE_FLY_SCRIPT_GROUND_TICKS ticks on the ground, and again from the
// start after the last one:
//   c<n>  R1 held n ticks (take off on the ground, climb in the air)
//   h<n>  R1 released n ticks (hold)
//   d     a double tap: released, pressed, released, pressed, released (5 ticks)
//   l     released until the ground (at most NATIVE_FLY_SCRIPT_LAND_MAX ticks)
// n is 1..999. Everything goes through the pad word of --dev-kart-steer.
#define NATIVE_FLY_SCRIPT_MAX 32
#define NATIVE_FLY_ARG_MAX 512
#define NATIVE_FLY_SCRIPT_GROUND_TICKS 60
#define NATIVE_FLY_SCRIPT_LAND_MAX 600
#define NATIVE_FLY_SCRIPT_TAP_TICKS 5

#define NATIVE_FLY_MAN_CLIMB 0
#define NATIVE_FLY_MAN_HOLD 1
#define NATIVE_FLY_MAN_DOUBLE 2
#define NATIVE_FLY_MAN_LAND 3

#define NATIVE_FLY_PH_WAIT 0 // on the ground, counting
#define NATIVE_FLY_PH_RUN 1  // playing the list

typedef struct
{
	int codes[NATIVE_FLY_SCRIPT_MAX];
	int args[NATIVE_FLY_SCRIPT_MAX];
	int count;
	int index;   // the manoeuvre playing
	int ticks;   // ticks in the phase or the manoeuvre
	int phase;   // NATIVE_FLY_PH_*
	int rounds;  // times the list was started
} NativeFlyScript;

typedef struct
{
	int racing;   // the kart-steer drives (a race, seat 0 human); 0 resets to WAIT
	int onGround; // ACTION_TOUCH_GROUND
	int grabbed;  // KS_MASK_GRABBED
} NativeFlyScriptIn;

typedef struct
{
	int r1;    // 1 = R1 held this tick
	int event; // 0 none, 1 the list started, 2 a manoeuvre started, 3 'l' landed, 4 'l' gave up, 5 reset
} NativeFlyScriptOut;

// 1 = the list parses (1..NATIVE_FLY_SCRIPT_MAX manoeuvres); codes/args may be NULL.
int NativeFly_ScriptParse(const char *text, int *codes, int *args, int *count, char *why, int whySize);
void NativeFly_ScriptReset(NativeFlyScript *p); // keeps codes/args/count
void NativeFly_ScriptStep(NativeFlyScript *p, const NativeFlyScriptIn *in, NativeFlyScriptOut *out);

// THE MODULE (game side).
NativeFlyTune *NativeFly_Tunables(void); // the debug page steps it; clamped at every use
int NativeFly_InScope(const struct Driver *d);
unsigned int NativeFly_KartButtons(const struct Driver *d, unsigned int buttons, int tapped);
int NativeFly_Gravity(const struct Driver *d, int gravityY);
void NativeFly_Velocity(const struct Driver *d, int *velocityY);
int NativeFly_AirTimeStill(const struct Driver *d);
// From DebugMenu_KartSteerTick (game/DebugMenu.c): the steerer's pad word
// (0 = held) with the script's R1; the word unchanged without --dev-fly-script.
unsigned short NativeFly_DevPadWord(const struct GameTracker *gGT, const struct Driver *d, unsigned short word);

// THE DEV SWITCHES (main.c; only with --dev).
//   --dev-fly-log         one line per flight event ("[CTR Fly] tick ...")
//   --dev-fly-script <l>  the script above, for --dev-kart-steer
extern int g_cfg_flyLog;
extern char g_cfg_flyScript[NATIVE_FLY_ARG_MAX];

// The pure parts (ctest fly_selftest, platform/native_fly_selftest.c).
int NativeFly_SelfTest(void);

#endif
