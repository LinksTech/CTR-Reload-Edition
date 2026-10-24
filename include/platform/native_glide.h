#ifndef NATIVE_GLIDE_H
#define NATIVE_GLIDE_H

// THE GLIDE ABILITY (TEST) (platform/native_glide.c; round "Dummy Glider",
// phase 1b, step C). Player 1 (seat 0, a human driver) may open wings in the
// air and glide: R3 (the right stick's click; keyboard "]") pressed while the
// kart is in the air opens the wings, R3 held glides, R3 released or the
// ground closes them. Only with the switch "GLIDE ABILITY (TEST)" of the debug
// menu (or --dev-glide); off by default, and off it changes nothing.
//
// WHAT GLIDING CHANGES: the gravity of seat 0, nothing else. The game asks
// two hooks, each of which hands its value back unchanged for every driver
// that is not gliding at that tick (the CPU drivers never reach them - their
// threads run BOTS_ThTick_Drive, not the driver function table):
//   NativeGlide_Gravity   game/Vehicle/VehPhysForce.c, VehPhysForce_OnGravity:
//                         the gravity after the MODS share and the low-gravity
//                         block, before the scaling by the tick's ms.
//   NativeGlide_LandingBoost  game/COLL.c, the air time (jump_LandingBoost):
//                         it stands still in every tick in which the glide
//                         gravity applied, and counts on after closing while
//                         the kart is still in the air (gliding does not
//                         count as air time for the hang-time turbo).
// Drift start, hop and landing are the game's own: R1/L1 are not read here.
//
// GLIDING: gravity x factor. The factor is the glide value (start 0.30), and
// the stick moves it: forward (stickLY towards 0x00, keyboard arrow up) towards
// the dive value (1.00), back (towards 0xFF, arrow down) towards the pull-up
// value (0.20). The stick share follows the stick by at most 1/6 per tick
// (keyboard and d-pad jump to the end in one tick), with a dead zone
// 0x70..0x90. The forward speed is not touched.
//
// THE WINGS (the hi-res driver's shape keys, include/platform/engine.h THE
// SHAPE KEYS; a driver without them simply has none to move): the chain
// wings_folded (Basis) -> wings_fold_33 -> wings_fold_66 -> wings_open, opened
// over "open" ticks (6) with one flap (flap 0 -> 1 -> 0 over 6 ticks) when it
// is fully open; dive and pull_up follow the stick share, on top of
// wings_open. Closing: over "close on release" ticks (4) when R3 is let go or
// gliding ends otherwise (mask grab, a hit, the finish), over "close on
// landing" ticks (2) at the ground; dive, pull_up and a running flap fall to 0
// over the same ticks as the chain.
//
// No state survives a level load (nor a change of seat 0's driver, nor the
// switch going off or on). Everything here is integer and runs once per logic
// tick from the hook, so a run with --deterministic is the same run again.

struct Driver;
struct GameTracker;

// The start values (debug menu page GLIDE ABILITY (TEST)); factors in
// thousandths of the gravity, durations in logic ticks.
#define NATIVE_GLIDE_GLIDE_DEFAULT 300
#define NATIVE_GLIDE_DIVE_DEFAULT 1000
#define NATIVE_GLIDE_PULL_UP_DEFAULT 200
#define NATIVE_GLIDE_OPEN_DEFAULT 6
#define NATIVE_GLIDE_CLOSE_RELEASE_DEFAULT 4
#define NATIVE_GLIDE_CLOSE_LAND_DEFAULT 2
#define NATIVE_GLIDE_FRAMES_MIN 1
#define NATIVE_GLIDE_FRAMES_MAX 20
#define NATIVE_GLIDE_FACTOR_MIN 0
#define NATIVE_GLIDE_FACTOR_MAX 1500

// Fixed (not tunables): the flap's ticks (half up, half down), the stick
// share's step per tick, the dead zone of stickLY.
#define NATIVE_GLIDE_FLAP_TICKS 6
#define NATIVE_GLIDE_STICK_STEP 167
#define NATIVE_GLIDE_STICK_DEAD_LOW 0x70
#define NATIVE_GLIDE_STICK_DEAD_HIGH 0x90

typedef struct
{
	int glide;        // thousandths of the gravity while gliding, stick at rest
	int dive;         // ... with the stick fully forward
	int pullUp;       // ... with the stick fully back
	int openFrames;   // ticks of the chain folded -> open
	int closeRelease; // ticks of open -> folded when R3 is let go (or gliding ends otherwise)
	int closeLand;    // ticks of open -> folded at the ground
} NativeGlideTune;

#define NATIVE_GLIDE_FOLDED 0
#define NATIVE_GLIDE_OPENING 1
#define NATIVE_GLIDE_OPEN 2
#define NATIVE_GLIDE_CLOSING 3

// What a step did (the log; one per step at most).
#define NATIVE_GLIDE_EV_NONE 0
#define NATIVE_GLIDE_EV_OPEN 1          // R3 pressed in the air: the wings start to open
#define NATIVE_GLIDE_EV_OPENED 2        // the chain reached wings_open (the flap starts)
#define NATIVE_GLIDE_EV_CLOSE_RELEASE 3 // R3 let go: closing over closeRelease
#define NATIVE_GLIDE_EV_CLOSE_LAND 4    // the ground: closing over closeLand
#define NATIVE_GLIDE_EV_CLOSE_END 5     // gliding ended otherwise (not eligible): closing over closeRelease
#define NATIVE_GLIDE_EV_FOLDED 6        // the chain is back at the Basis

// THE STATE MACHINE (pure: numbers in, numbers out; the selftest drives it).
typedef struct
{
	int state;     // NATIVE_GLIDE_*
	int prog;      // the chain's way 0..1000 (engine position = 3 x prog / 1000)
	int heldPrev;  // R3 held at the step before (the press is the edge)
	int fwd;       // the stick share forward 0..1000, following the stick
	int back;      // the stick share back 0..1000
	int flapStep;  // -1 no flap, else 0..NATIVE_GLIDE_FLAP_TICKS
	int closeRate; // thousandths of the way per tick while closing
	int closeFrom; // prog when the closing started
	int latchDive; // the key weights when the closing started (thousandths)
	int latchPull;
	int latchFlap;
} NativeGlideState;

typedef struct
{
	int eligible; // the ability is on for this driver and it may glide now (seat 0, human, KS_NORMAL / KS_DRIFTING, not END_OF_RACE)
	int airborne; // ACTION_TOUCH_GROUND clear
	int r3Held;   // BTN_R3 held
	int stickLY;  // 0x00 forward .. 0x80 rest .. 0xFF back
} NativeGlideIn;

typedef struct
{
	int gliding;  // 1 = the glide gravity applies at this tick (OPENING or OPEN after the step)
	int factor;   // thousandths of the gravity while gliding; 1000 otherwise
	int pressed;  // the R3 edge of this step (whether in the air or not)
	int event;    // NATIVE_GLIDE_EV_*
	int chain;    // the drawn chain way 0..1000
	int flap;     // drawn key weights, thousandths
	int dive;
	int pullUp;
} NativeGlideOut;

void NativeGlide_TuneDefaults(NativeGlideTune *t);
void NativeGlide_TuneClamp(NativeGlideTune *t);
void NativeGlide_StateReset(NativeGlideState *s);
int NativeGlide_StickForward(int stickLY);           // the share 0..1000 the stick asks forward (before the step limit)
int NativeGlide_StickBack(int stickLY);              // ... back
int NativeGlide_Rate(int frames);                    // thousandths per tick of a way over frames ticks: ceil(1000 / frames), frames clamped 1..20
int NativeGlide_ScaleGravity(int gravityY, int factor); // gravityY x factor / 1000 (towards 0)
int NativeGlide_FlapWeight(int flapStep);            // thousandths at that step of the flap (0 for -1)
void NativeGlide_Step(NativeGlideState *s, const NativeGlideTune *t, const NativeGlideIn *in, NativeGlideOut *out);

// THE SWITCH AND THE TUNABLES (debug menu; --dev-glide). The setter also
// clears the state (wings folded, nothing held).
int NativeGlide_Ability(void);
void NativeGlide_SetAbility(int on);
NativeGlideTune *NativeGlide_Tunables(void); // the menu steps it; clamped at every use

// THE HOOKS (game code). Each returns the value it was given for every driver
// that does not glide at this tick.
int NativeGlide_Gravity(const struct Driver *d, int gravityY);
int NativeGlide_LandingBoost(const struct Driver *d, int before, int added);

// THE WINGS ON SCREEN (platform/engine_hires_char.c, once per logic tick of a
// seat whose hi-res character has shape keys): the chain and flap / dive /
// pull_up of seat 0 as the state says. When the hook stopped stepping (seat 0
// became a bot at the finish) the wings close on screen alone, over "close on
// release". With the switch off nothing is written (a last write puts the
// keys back to 0 when it goes off).
void NativeGlide_EngineTick(int seat, const struct Driver *d);

// THE DEV SWITCHES (main.c; all only with --dev, all off by default).
//   --dev-glide            the ability on from the start
//   --dev-glide-log        one line per tick of seat 0 while the wings are not
//                          folded or something happened ("[CTR Glide] tick")
//   --dev-glide-script <l> scripted manoeuvres for --dev-kart-steer (below)
//   --dev-glide-dump <p>   the drawn wing, kart and wheel vertices of seat 0 at
//                          every glide landing (C-N1; the format in the round's
//                          notes, DUMP-FORMAT.md): <p>-map.bin at the load,
//                          <p>-<timer>.bin per tick
extern int g_cfg_glideLog;
#define NATIVE_GLIDE_SCRIPT_MAX 32
#define NATIVE_GLIDE_ARG_MAX 512
extern char g_cfg_glideScript[NATIVE_GLIDE_ARG_MAX];
extern char g_cfg_glideDump[NATIVE_GLIDE_ARG_MAX];

// THE SCRIPT (--dev-glide-script): a comma list of manoeuvres, played in turn
// and from the start again, each after 60 ticks on the ground:
//   h      hop (R1 tapped, let go at the 3rd air tick), glide, own steering
//   dl dr  hop, R1 held until 20 ticks after the landing, LEFT / RIGHT held from
//          the 3rd air tick until then: a landing into a drift
//   DL DR  the same with the stick forward (UP) in the air: dive 1 when closing
//   sl sr  hop, R1 let go, LEFT / RIGHT held from the 3rd air tick until 20
//          ticks after the landing: a landing with steering
//   pu     hop, glide with the stick back (DOWN)
//   ol or  off the track: LEFT / RIGHT held on the ground (at most 45 ticks)
//          until the kart is in the air, then held on while it glides
// R3 is held from the 3rd air tick of every flight until the ground; a flight
// the script did not start (a ramp, an edge) glides the same way and uses up
// no manoeuvre. Everything goes through the pad word of --dev-kart-steer
// (R1, R3 and the d-pad), so R3 and stickLY take the real input path.
#define NATIVE_GLIDE_MAN_H 0
#define NATIVE_GLIDE_MAN_DL 1
#define NATIVE_GLIDE_MAN_DR 2
#define NATIVE_GLIDE_MAN_DIVE_L 3
#define NATIVE_GLIDE_MAN_DIVE_R 4
#define NATIVE_GLIDE_MAN_SL 5
#define NATIVE_GLIDE_MAN_SR 6
#define NATIVE_GLIDE_MAN_PU 7
#define NATIVE_GLIDE_MAN_OL 8
#define NATIVE_GLIDE_MAN_OR 9
#define NATIVE_GLIDE_MAN_COUNT 10
#define NATIVE_GLIDE_SCRIPT_GROUND_TICKS 60
#define NATIVE_GLIDE_SCRIPT_R3_TICK 3
#define NATIVE_GLIDE_SCRIPT_AFTER_TICKS 20
#define NATIVE_GLIDE_SCRIPT_HOP_WAIT 15
#define NATIVE_GLIDE_SCRIPT_OFF_TICKS 45

const char *NativeGlide_ManName(int man);
// 1 = the list parses (1..NATIVE_GLIDE_SCRIPT_MAX known names); codes may be NULL.
int NativeGlide_ScriptParse(const char *text, int *codes, int *count, char *why, int whySize);

// The script's player (pure). Phases: WAIT (on the ground, counting), HOP (R1
// pressed, waiting for the air), OFF (steering off the track), FLIGHT, AFTER
// (the ticks after the landing), NATURAL (a flight the script did not start).
#define NATIVE_GLIDE_PH_WAIT 0
#define NATIVE_GLIDE_PH_HOP 1
#define NATIVE_GLIDE_PH_OFF 2
#define NATIVE_GLIDE_PH_FLIGHT 3
#define NATIVE_GLIDE_PH_AFTER 4
#define NATIVE_GLIDE_PH_NATURAL 5

typedef struct
{
	int codes[NATIVE_GLIDE_SCRIPT_MAX];
	int count;
	int next;    // the index of the next manoeuvre
	int current; // the manoeuvre running, -1 none
	int phase;   // NATIVE_GLIDE_PH_*
	int ticks;   // ticks in the phase
	int air;     // air ticks of the flight
	int started; // manoeuvres started
} NativeGlideScript;

typedef struct
{
	int racing;   // the kart-steer drives (a race, seat 0 human); 0 resets the player to WAIT
	int airborne; // ACTION_TOUCH_GROUND clear
	int grabbed;  // KS_MASK_GRABBED
} NativeGlideScriptIn;

// The pad bits (RAW_BTN_* of include/namespace_Gamepad.h) the script presses
// and the ones it takes away from the kart-steer's own word.
typedef struct
{
	unsigned int press;   // held this tick
	unsigned int release; // not held this tick (the steerer's LEFT/RIGHT under a forced side)
	int event;            // 0 none, 1 a manoeuvre started, 2 its flight started, 3 it landed, 4 it ended, 5 it gave up (no flight)
} NativeGlideScriptOut;

void NativeGlide_ScriptReset(NativeGlideScript *p);
void NativeGlide_ScriptStep(NativeGlideScript *p, const NativeGlideScriptIn *in, NativeGlideScriptOut *out);

// From DebugMenu_KartSteerTick (game/DebugMenu.c): the steerer's pad word
// (0 = held) with the script's buttons; the word unchanged without
// --dev-glide-script.
unsigned short NativeGlide_DevPadWord(const struct GameTracker *gGT, const struct Driver *d, unsigned short word);

// --dev-glide-dump: 1 = write the dump of this tick for seat 0 (the engine's
// route calls it once per drawn tick; info gets the glide words of the
// header, NATIVE_GLIDE_DUMP_WORDS of them).
#define NATIVE_GLIDE_DUMP_WORDS 12
int NativeGlide_DumpWanted(const struct Driver *d, int info[NATIVE_GLIDE_DUMP_WORDS]);

// The pure parts (ctest glide_selftest, platform/native_glide_selftest.c).
int NativeGlide_SelfTest(void);

#endif
