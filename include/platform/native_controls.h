#ifndef PLATFORM_NATIVE_CONTROLS_H
#define PLATFORM_NATIVE_CONTROLS_H

// KEYBOARD CONTROLS: which key presses which button of the pad, read from
// ctr-controls.cfg next to the exe (platform/native_controls.c) and changed on
// the CONTROLS page of the OPTIONS box (game/native_controls_page.c). The
// gamepad keeps its own fixed layout (native_input.c); this is the keyboard only.
//
// Two groups of actions share the pad buttons: the race group drives, the menu
// group moves through menus. Both are read at once, so a key used by only one
// group works everywhere, as before. Where a key is bound in both groups, the
// race meaning wins during a race and the menu meaning everywhere else
// (NativeControlsPage_RaceContext). START, SELECT and the stick clicks belong
// to both groups. With the stock layout every key gives the same button in both
// groups, so nothing changes against the fixed table this replaces.

enum NativeControlsAction
{
	// race group
	NATIVE_CONTROLS_GAS = 0,     // cross
	NATIVE_CONTROLS_BRAKE,       // square
	NATIVE_CONTROLS_STEER_LEFT,  // d-pad left
	NATIVE_CONTROLS_STEER_RIGHT, // d-pad right
	NATIVE_CONTROLS_JUMP,        // R1, jump and drift
	NATIVE_CONTROLS_JUMP2,       // L1, the second jump button
	NATIVE_CONTROLS_ITEM,        // circle
	NATIVE_CONTROLS_REAR_VIEW,   // R2, held
	NATIVE_CONTROLS_CAMERA,      // L2, near / far camera

	// both groups
	NATIVE_CONTROLS_START,  // start
	NATIVE_CONTROLS_SELECT, // select

	// menu group
	NATIVE_CONTROLS_MENU_UP,    // d-pad up
	NATIVE_CONTROLS_MENU_DOWN,  // d-pad down
	NATIVE_CONTROLS_MENU_LEFT,  // d-pad left
	NATIVE_CONTROLS_MENU_RIGHT, // d-pad right
	NATIVE_CONTROLS_CONFIRM,    // cross
	NATIVE_CONTROLS_BACK,       // triangle

	// both groups, only in the file (no row on the page)
	NATIVE_CONTROLS_STICK_L, // L3
	NATIVE_CONTROLS_STICK_R, // R3

	NATIVE_CONTROLS_ACTION_COUNT
};

// The rows of the CONTROLS page: the actions before this one.
#define NATIVE_CONTROLS_PAGE_ACTIONS NATIVE_CONTROLS_STICK_L

// NativeControls_PollCapture: nothing pressed yet, and a key the page does not
// take (Escape cancels; F1..F12, Alt and the Windows keys belong to the
// program). A taken key is returned as its SDL scancode (> 0).
#define NATIVE_CONTROLS_CAPTURE_WAITING  (-1)
#define NATIVE_CONTROLS_CAPTURE_CANCEL   0
#define NATIVE_CONTROLS_CAPTURE_RESERVED (-2)

// Reads ctr-controls.cfg, or writes it with the stock layout if it is missing.
// Under --settings-defaults the stock layout applies and the file is neither
// read nor written.
void NativeControls_Load(void);

// The pad word (active low, 0xffff = nothing held) of the keyboard for this
// frame. keys is SDL_GetKeyboardState. raceContext: 1 during a race (race
// meaning wins), 0 everywhere else.
u16 NativeControls_ReadKeyboard(const bool *keys, int raceContext);

// The key of an action (SDL scancode) and its name on the page, upper case and
// drawable in FONT_SMALL (e.g. "LEFT SHIFT", "LBRACKET").
int NativeControls_Key(int action);
void NativeControls_KeyLabel(int scancode, char *text, int size);
const char *NativeControls_ActionLabel(int action);

// Is this scancode one the page may give to an action?
int NativeControls_KeyAllowed(int scancode);

// Key capture for the page: Begin, then Poll once per frame until it returns
// a scancode or CANCEL. While capturing, and afterwards until every key is
// released, the keyboard gives the pad nothing.
void NativeControls_BeginCapture(void);
int NativeControls_PollCapture(void);
int NativeControls_Capturing(void);

// Gives the key to the action and saves. A conflicting action that held the
// key gets the action's old key (a swap); *swappedWith is that action, or -1.
// Returns 1 if anything changed.
int NativeControls_Assign(int action, int scancode, int *swappedWith);

// The stock layout, saved.
void NativeControls_ResetDefaults(void);

// ctest controls_selftest: parsing, fallbacks, saving, swaps, in dir.
int NativeControls_SelfTest(const char *dir);

// game/native_controls_page.c: is a race running (not paused, no menu box
// open, not in the main menu)?
int NativeControlsPage_RaceContext(void);

#endif
