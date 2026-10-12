#include <platform/native_controls.h>

#include <platform.h>
#include <platform/native_log.h>

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// KEYBOARD CONTROLS - see include/platform/native_controls.h.
//
// One table says what each action is, which pad button it presses and which
// key it has out of the box. The layout in use is an array of one SDL scancode
// per action. It is read from ctr-controls.cfg at the start (a missing file is
// written with the stock layout), changed only by NativeControls_Assign /
// NativeControls_ResetDefaults, and every change saves the whole file.
//
// A layout is VALID when no two actions CONFLICT: the same key and the same
// group, or the same key and at least one of the two belongs to both groups.
// A race action and a menu action may share a key (stock: C is gas and confirm)
// because they are never wanted at the same time - NativeControls_ReadKeyboard
// lets the race meaning win during a race and the menu meaning elsewhere. The
// file is a player's hand work, so every way it can be wrong ends in the stock
// key for that action and a line in the log - never in a crash or an abort.

#define CONTROLS_PATH       "ctr-controls.cfg"
#define CONTROLS_LINE_BYTES 256
#define CONTROLS_LABEL_MAX  15

#define CONTROLS_GROUP_RACE 0
#define CONTROLS_GROUP_MENU 1
#define CONTROLS_GROUP_BOTH 2

#define CONTROLS_PAD_SELECT   0x0001
#define CONTROLS_PAD_L3       0x0002
#define CONTROLS_PAD_R3       0x0004
#define CONTROLS_PAD_START    0x0008
#define CONTROLS_PAD_UP       0x0010
#define CONTROLS_PAD_RIGHT    0x0020
#define CONTROLS_PAD_DOWN     0x0040
#define CONTROLS_PAD_LEFT     0x0080
#define CONTROLS_PAD_L2       0x0100
#define CONTROLS_PAD_R2       0x0200
#define CONTROLS_PAD_L1       0x0400
#define CONTROLS_PAD_R1       0x0800
#define CONTROLS_PAD_TRIANGLE 0x1000
#define CONTROLS_PAD_CIRCLE   0x2000
#define CONTROLS_PAD_CROSS    0x4000
#define CONTROLS_PAD_SQUARE   0x8000

struct NativeControlsActionInfo
{
	const char *fileKey;
	const char *label;
	u16 padBit;
	int group;
	int stockKey;
};

global_variable const struct NativeControlsActionInfo s_controlsActions[NATIVE_CONTROLS_ACTION_COUNT] = {
    {"gas", "GAS", CONTROLS_PAD_CROSS, CONTROLS_GROUP_RACE, SDL_SCANCODE_C},
    {"brake", "BRAKE", CONTROLS_PAD_SQUARE, CONTROLS_GROUP_RACE, SDL_SCANCODE_X},
    {"steer_left", "STEER LEFT", CONTROLS_PAD_LEFT, CONTROLS_GROUP_RACE, SDL_SCANCODE_LEFT},
    {"steer_right", "STEER RIGHT", CONTROLS_PAD_RIGHT, CONTROLS_GROUP_RACE, SDL_SCANCODE_RIGHT},
    {"jump", "JUMP AND DRIFT", CONTROLS_PAD_R1, CONTROLS_GROUP_RACE, SDL_SCANCODE_RSHIFT},
    {"jump2", "JUMP 2", CONTROLS_PAD_L1, CONTROLS_GROUP_RACE, SDL_SCANCODE_LSHIFT},
    {"item", "ITEM", CONTROLS_PAD_CIRCLE, CONTROLS_GROUP_RACE, SDL_SCANCODE_V},
    {"rear_view", "REAR VIEW", CONTROLS_PAD_R2, CONTROLS_GROUP_RACE, SDL_SCANCODE_RCTRL},
    {"camera", "CAMERA", CONTROLS_PAD_L2, CONTROLS_GROUP_RACE, SDL_SCANCODE_LCTRL},
    {"start", "START", CONTROLS_PAD_START, CONTROLS_GROUP_BOTH, SDL_SCANCODE_RETURN},
    {"select", "SELECT", CONTROLS_PAD_SELECT, CONTROLS_GROUP_BOTH, SDL_SCANCODE_SPACE},
    {"menu_up", "MENU UP", CONTROLS_PAD_UP, CONTROLS_GROUP_MENU, SDL_SCANCODE_UP},
    {"menu_down", "MENU DOWN", CONTROLS_PAD_DOWN, CONTROLS_GROUP_MENU, SDL_SCANCODE_DOWN},
    {"menu_left", "MENU LEFT", CONTROLS_PAD_LEFT, CONTROLS_GROUP_MENU, SDL_SCANCODE_LEFT},
    {"menu_right", "MENU RIGHT", CONTROLS_PAD_RIGHT, CONTROLS_GROUP_MENU, SDL_SCANCODE_RIGHT},
    {"confirm", "CONFIRM", CONTROLS_PAD_CROSS, CONTROLS_GROUP_MENU, SDL_SCANCODE_C},
    {"back", "BACK", CONTROLS_PAD_TRIANGLE, CONTROLS_GROUP_MENU, SDL_SCANCODE_Z},
    {"stick_l", "L3", CONTROLS_PAD_L3, CONTROLS_GROUP_BOTH, SDL_SCANCODE_LEFTBRACKET},
    {"stick_r", "R3", CONTROLS_PAD_R3, CONTROLS_GROUP_BOTH, SDL_SCANCODE_RIGHTBRACKET},
};

// The layout in use. It starts as the stock layout so that the keyboard works
// before (and without) NativeControls_Load.
global_variable int s_controlsKeys[NATIVE_CONTROLS_ACTION_COUNT] = {
    SDL_SCANCODE_C,    SDL_SCANCODE_X,    SDL_SCANCODE_LEFT,        SDL_SCANCODE_RIGHT, SDL_SCANCODE_RSHIFT,
    SDL_SCANCODE_LSHIFT, SDL_SCANCODE_V,  SDL_SCANCODE_RCTRL,       SDL_SCANCODE_LCTRL, SDL_SCANCODE_RETURN,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_UP,  SDL_SCANCODE_DOWN,        SDL_SCANCODE_LEFT,  SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_C,    SDL_SCANCODE_Z,    SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
};

// --settings-defaults (or the selftest's locked case): a change applies to the
// session only and nothing is written.
global_variable int s_controlsLocked;

// What the last load counted (the selftest reads these).
global_variable int s_controlsLastRead;
global_variable int s_controlsLastSkipped;

// Key capture (the CONTROLS page waits for the next key press).
global_variable int s_controlsCapturing;
global_variable int s_controlsWaitRelease;
global_variable bool s_controlsPrevDown[SDL_SCANCODE_COUNT];

internal int NativeControls_ValidAction(int action)
{
	return (action >= 0) && (action < NATIVE_CONTROLS_ACTION_COUNT);
}

internal int NativeControls_Conflict(int a, int b, int keyA, int keyB)
{
	if (keyA != keyB)
	{
		return 0;
	}
	return (s_controlsActions[a].group == s_controlsActions[b].group) || (s_controlsActions[a].group == CONTROLS_GROUP_BOTH) ||
	       (s_controlsActions[b].group == CONTROLS_GROUP_BOTH);
}

// The first conflicting pair (a < b) of a layout, or 0.
internal int NativeControls_FindConflict(const int *keys, int *outA, int *outB)
{
	for (int a = 0; a < NATIVE_CONTROLS_ACTION_COUNT; a++)
	{
		for (int b = a + 1; b < NATIVE_CONTROLS_ACTION_COUNT; b++)
		{
			if (NativeControls_Conflict(a, b, keys[a], keys[b]))
			{
				*outA = a;
				*outB = b;
				return 1;
			}
		}
	}
	return 0;
}

internal void NativeControls_SetStock(void)
{
	for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		s_controlsKeys[action] = s_controlsActions[action].stockKey;
	}
}

internal const char *NativeControls_SdlName(int scancode)
{
	const char *name = SDL_GetScancodeName((SDL_Scancode)scancode);
	return (name != NULL) ? name : "";
}

int NativeControls_Key(int action)
{
	return NativeControls_ValidAction(action) ? s_controlsKeys[action] : 0;
}

const char *NativeControls_ActionLabel(int action)
{
	return NativeControls_ValidAction(action) ? s_controlsActions[action].label : "";
}

int NativeControls_KeyAllowed(int scancode)
{
	if ((scancode >= SDL_SCANCODE_A) && (scancode <= SDL_SCANCODE_SLASH))
	{
		// A-Z, 1-0, Return, Backspace, Tab, Space and the punctuation keys. Escape
		// cancels a capture; the non-US hash key has no name on every layout.
		return (scancode != SDL_SCANCODE_ESCAPE) && (scancode != SDL_SCANCODE_NONUSHASH);
	}
	if ((scancode == SDL_SCANCODE_CAPSLOCK) || ((scancode >= SDL_SCANCODE_INSERT) && (scancode <= SDL_SCANCODE_UP)))
	{
		// Insert, Home, Page Up, Delete, End, Page Down and the arrows.
		return 1;
	}
	if ((scancode >= SDL_SCANCODE_KP_DIVIDE) && (scancode <= SDL_SCANCODE_KP_PERIOD))
	{
		return 1;
	}
	return (scancode == SDL_SCANCODE_LCTRL) || (scancode == SDL_SCANCODE_LSHIFT) || (scancode == SDL_SCANCODE_RCTRL) ||
	       (scancode == SDL_SCANCODE_RSHIFT);
}

// FONT_SMALL cannot draw punctuation, so these keys are spelled out.
internal const char *NativeControls_WordFor(int scancode)
{
	switch (scancode)
	{
	case SDL_SCANCODE_LEFTBRACKET: return "LBRACKET";
	case SDL_SCANCODE_RIGHTBRACKET: return "RBRACKET";
	case SDL_SCANCODE_SEMICOLON: return "SEMICOLON";
	case SDL_SCANCODE_APOSTROPHE: return "APOSTROPHE";
	case SDL_SCANCODE_COMMA: return "COMMA";
	case SDL_SCANCODE_PERIOD: return "PERIOD";
	case SDL_SCANCODE_SLASH: return "SLASH";
	case SDL_SCANCODE_BACKSLASH: return "BACKSLASH";
	case SDL_SCANCODE_GRAVE: return "GRAVE";
	case SDL_SCANCODE_MINUS: return "MINUS";
	case SDL_SCANCODE_EQUALS: return "EQUALS";
	case SDL_SCANCODE_KP_DIVIDE: return "KEYPAD DIVIDE";
	case SDL_SCANCODE_KP_MULTIPLY: return "KEYPAD MULTIPLY";
	case SDL_SCANCODE_KP_MINUS: return "KEYPAD MINUS";
	case SDL_SCANCODE_KP_PLUS: return "KEYPAD PLUS";
	case SDL_SCANCODE_KP_ENTER: return "KEYPAD ENTER";
	case SDL_SCANCODE_KP_PERIOD: return "KEYPAD PERIOD";
	default: return NULL;
	}
}

void NativeControls_KeyLabel(int scancode, char *text, int size)
{
	char built[64];
	int length = 0;
	const char *word;

	if ((text == NULL) || (size <= 0))
	{
		return;
	}

	word = NativeControls_WordFor(scancode);
	if (word != NULL)
	{
		snprintf(built, sizeof(built), "%s", word);
	}
	else
	{
		// SDL's own name ("Left Shift", "Keypad 3", "Page Up"), upper case and
		// only what FONT_SMALL draws: letters, digits, spaces.
		const char *name = (scancode > 0) ? NativeControls_SdlName(scancode) : "";

		for (const char *c = name; (*c != '\0') && (length < (int)sizeof(built) - 1); c++)
		{
			if ((*c >= 'a') && (*c <= 'z'))
			{
				built[length++] = (char)(*c - 'a' + 'A');
			}
			else if (((*c >= 'A') && (*c <= 'Z')) || ((*c >= '0') && (*c <= '9')) || (*c == ' '))
			{
				built[length++] = *c;
			}
		}
		built[length] = '\0';
	}

	if (built[0] == '\0')
	{
		snprintf(built, sizeof(built), "NONE");
	}
	if (strlen(built) > CONTROLS_LABEL_MAX)
	{
		built[CONTROLS_LABEL_MAX] = '\0';
	}
	snprintf(text, (size_t)size, "%s", built);
}

// THE PAD WORD.
//
// 0xffff is nothing held. A key that is down clears its action's button. In a
// race the menu meaning of a key that a race action also has is dropped, and
// outside a race the race meaning of a key that a menu action also has - so a
// player who puts gas on Up still gets the d-pad up on the menus.
internal u16 NativeControls_ReadFrom(const int *layout, const bool *keys, int raceContext)
{
	u16 buttons = 0xffff;
	int dropGroup = raceContext ? CONTROLS_GROUP_MENU : CONTROLS_GROUP_RACE;
	int otherGroup = raceContext ? CONTROLS_GROUP_RACE : CONTROLS_GROUP_MENU;

	for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		int key = layout[action];

		if ((key <= 0) || (key >= SDL_SCANCODE_COUNT) || !keys[key])
		{
			continue;
		}

		if (s_controlsActions[action].group == dropGroup)
		{
			int shared = 0;
			for (int other = 0; other < NATIVE_CONTROLS_ACTION_COUNT; other++)
			{
				if ((s_controlsActions[other].group == otherGroup) && (layout[other] == key))
				{
					shared = 1;
					break;
				}
			}
			if (shared)
			{
				continue;
			}
		}

		buttons = (u16)(buttons & ~s_controlsActions[action].padBit);
	}

	return buttons;
}

internal int NativeControls_AnyKeyDown(const bool *keys)
{
	for (int key = 1; key < SDL_SCANCODE_COUNT; key++)
	{
		if (keys[key])
		{
			return 1;
		}
	}
	return 0;
}

u16 NativeControls_ReadKeyboard(const bool *keys, int raceContext)
{
	if (keys == NULL)
	{
		return 0xffff;
	}

	// While a key is being captured, and after it until every key is up again,
	// the pad gets nothing - the key that was just chosen must not also act.
	if (s_controlsCapturing)
	{
		return 0xffff;
	}
	if (s_controlsWaitRelease)
	{
		if (NativeControls_AnyKeyDown(keys))
		{
			return 0xffff;
		}
		s_controlsWaitRelease = 0;
	}

	return NativeControls_ReadFrom(s_controlsKeys, keys, raceContext);
}

// FILE.

internal int NativeControls_SaveTo(const char *path)
{
	char tmpPath[512];
	FILE *file;

	snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
	file = fopen(tmpPath, "wb");
	if (file == NULL)
	{
		Platform_LogWarn("[CTR Controls] cannot write '%s'\n", tmpPath);
		return 0;
	}

	fprintf(file, "# CTR Reload keyboard controls. One line per action: <action> <key>.\n");
	fprintf(file, "# Key names as SDL writes them (C, Left Shift, Return, Up, [ ...). Delete this file for the stock layout.\n");
	fprintf(file, "# Race actions and menu actions may share a key; during a race the race meaning wins.\n");
	for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		fprintf(file, "%s %s\n", s_controlsActions[action].fileKey, NativeControls_SdlName(s_controlsKeys[action]));
	}

	if (fclose(file) != 0)
	{
		Platform_LogWarn("[CTR Controls] cannot write '%s'\n", tmpPath);
		remove(tmpPath);
		return 0;
	}

	// rename does not replace an existing file on Windows.
	remove(path);
	if (rename(tmpPath, path) != 0)
	{
		Platform_LogWarn("[CTR Controls] cannot replace '%s'\n", path);
		remove(tmpPath);
		return 0;
	}
	return 1;
}

internal void NativeControls_Save(void)
{
	if (s_controlsLocked)
	{
		return;
	}
	if (NativeControls_SaveTo(CONTROLS_PATH))
	{
		Platform_Log("[CTR Controls] saved\n");
	}
}

internal void NativeControls_Trim(char *text)
{
	int length = (int)strlen(text);
	while ((length > 0) && ((text[length - 1] == '\n') || (text[length - 1] == '\r') || (text[length - 1] == ' ') || (text[length - 1] == '\t')))
	{
		text[--length] = '\0';
	}
}

// The action named in a file line (any case), or -1.
internal int NativeControls_ActionByName(const char *name)
{
	for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		const char *a = s_controlsActions[action].fileKey;
		const char *b = name;
		while ((*a != '\0') && (*b != '\0') && ((((*b >= 'A') && (*b <= 'Z')) ? (*b | 0x20) : *b) == *a))
		{
			a++;
			b++;
		}
		if ((*a == '\0') && (*b == '\0'))
		{
			return action;
		}
	}
	return -1;
}

internal void NativeControls_LoadFrom(const char *path, int locked)
{
	char line[CONTROLS_LINE_BYTES];
	int lineOf[NATIVE_CONTROLS_ACTION_COUNT];
	int fromFile[NATIVE_CONTROLS_ACTION_COUNT];
	int lineNumber = 0;
	int read = 0;
	int skipped = 0;
	FILE *file;

	s_controlsLocked = locked;
	s_controlsLastRead = 0;
	s_controlsLastSkipped = 0;
	NativeControls_SetStock();

	if (locked)
	{
		Platform_Log("[CTR Controls] --settings-defaults: stock layout, ctr-controls.cfg neither read nor written\n");
		return;
	}

	file = fopen(path, "rb");
	if (file == NULL)
	{
		if (NativeControls_SaveTo(path))
		{
			Platform_Log("[CTR Controls] no ctr-controls.cfg - written with the stock layout\n");
		}
		return;
	}

	memset(lineOf, 0, sizeof(lineOf));
	memset(fromFile, 0, sizeof(fromFile));

	while (fgets(line, sizeof(line), file) != NULL)
	{
		char *name = line;
		char *value;
		int action;
		int key;

		lineNumber++;
		if ((strchr(line, '\n') == NULL) && !feof(file))
		{
			// A line longer than the buffer: drop the rest of it.
			int c;
			while (((c = fgetc(file)) != EOF) && (c != '\n'))
			{
			}
		}

		NativeControls_Trim(line);
		while ((*name == ' ') || (*name == '\t'))
		{
			name++;
		}
		if ((*name == '\0') || (*name == '#'))
		{
			continue;
		}

		value = name;
		while ((*value != '\0') && (*value != ' ') && (*value != '\t'))
		{
			value++;
		}
		if (*value != '\0')
		{
			*value++ = '\0';
			while ((*value == ' ') || (*value == '\t'))
			{
				value++;
			}
		}

		action = NativeControls_ActionByName(name);
		if (action < 0)
		{
			Platform_LogWarn("[CTR Controls] ctr-controls.cfg line %d: '%s' names no action, skipped\n", lineNumber, name);
			skipped++;
			continue;
		}

		key = (value[0] != '\0') ? (int)SDL_GetScancodeFromName(value) : (int)SDL_SCANCODE_UNKNOWN;
		if ((key == SDL_SCANCODE_UNKNOWN) || !NativeControls_KeyAllowed(key))
		{
			Platform_LogWarn("[CTR Controls] ctr-controls.cfg line %d: key '%s' for %s is not usable, stock key %s kept\n", lineNumber, value,
			                 s_controlsActions[action].fileKey, NativeControls_SdlName(s_controlsActions[action].stockKey));
			skipped++;
			continue;
		}

		if (fromFile[action])
		{
			Platform_LogWarn("[CTR Controls] ctr-controls.cfg line %d: %s given twice, the later line wins\n", lineNumber,
			                 s_controlsActions[action].fileKey);
			skipped++;
		}
		else
		{
			read++;
		}
		fromFile[action] = 1;
		lineOf[action] = lineNumber;
		s_controlsKeys[action] = key;
	}
	fclose(file);

	// Conflicts: of a pair, the action that came later in the file goes back to
	// its stock key (an action not in the file counts as earlier than any line).
	// Every round puts one more action on its stock key for good, so the loop
	// ends; the stock layout itself is valid.
	for (int round = 0; round <= NATIVE_CONTROLS_ACTION_COUNT; round++)
	{
		int a;
		int b;
		int loser;
		int winner;

		if (!NativeControls_FindConflict(s_controlsKeys, &a, &b))
		{
			break;
		}
		loser = (lineOf[b] >= lineOf[a]) ? b : a;
		winner = (loser == a) ? b : a;
		if (s_controlsKeys[loser] == s_controlsActions[loser].stockKey)
		{
			// Already at home: the other one has to give way.
			int swap = loser;
			loser = winner;
			winner = swap;
		}
		Platform_LogWarn("[CTR Controls] ctr-controls.cfg: %s and %s both on %s - %s back to stock key %s\n", s_controlsActions[winner].fileKey,
		                 s_controlsActions[loser].fileKey, NativeControls_SdlName(s_controlsKeys[loser]), s_controlsActions[loser].fileKey,
		                 NativeControls_SdlName(s_controlsActions[loser].stockKey));
		s_controlsKeys[loser] = s_controlsActions[loser].stockKey;
		if (fromFile[loser])
		{
			fromFile[loser] = 0;
			read--;
		}
		skipped++;
	}
	{
		int a;
		int b;
		if (NativeControls_FindConflict(s_controlsKeys, &a, &b))
		{
			Platform_LogWarn("[CTR Controls] ctr-controls.cfg: conflicts remain, the whole layout goes back to stock\n");
			NativeControls_SetStock();
			skipped += read;
			read = 0;
		}
	}

	s_controlsLastRead = read;
	s_controlsLastSkipped = skipped;
	Platform_Log("[CTR Controls] ctr-controls.cfg: %d action(s) read, %d skipped or replaced\n", read, skipped);
}

void NativeControls_Load(void)
{
	NativeControls_LoadFrom(CONTROLS_PATH, Platform_SettingsLocked());
}

// CAPTURE.
//
// The page calls Begin, then Poll once a frame. A key that is already down
// when the capture begins (the one that opened it) does not count; the first
// key that goes down afterwards is the answer. The work is done on a keyboard
// state handed in, so the selftest can feed a fake one.

internal void NativeControls_BeginCaptureOn(const bool *keys)
{
	s_controlsCapturing = 1;
	for (int key = 0; key < SDL_SCANCODE_COUNT; key++)
	{
		s_controlsPrevDown[key] = (keys != NULL) && keys[key];
	}
}

internal int NativeControls_PollCaptureOn(const bool *keys)
{
	int found = 0;

	if (!s_controlsCapturing)
	{
		return NATIVE_CONTROLS_CAPTURE_CANCEL;
	}
	if (keys == NULL)
	{
		return NATIVE_CONTROLS_CAPTURE_WAITING;
	}

	for (int key = 1; key < SDL_SCANCODE_COUNT; key++)
	{
		if (keys[key] && !s_controlsPrevDown[key] && (found == 0))
		{
			found = key;
		}
		s_controlsPrevDown[key] = keys[key];
	}

	if (found == 0)
	{
		return NATIVE_CONTROLS_CAPTURE_WAITING;
	}
	if (found == SDL_SCANCODE_ESCAPE)
	{
		s_controlsCapturing = 0;
		s_controlsWaitRelease = 1;
		return NATIVE_CONTROLS_CAPTURE_CANCEL;
	}
	if (!NativeControls_KeyAllowed(found))
	{
		return NATIVE_CONTROLS_CAPTURE_RESERVED;
	}
	s_controlsCapturing = 0;
	s_controlsWaitRelease = 1;
	return found;
}

void NativeControls_BeginCapture(void)
{
	NativeControls_BeginCaptureOn(SDL_GetKeyboardState(NULL));
}

int NativeControls_PollCapture(void)
{
	return NativeControls_PollCaptureOn(SDL_GetKeyboardState(NULL));
}

int NativeControls_Capturing(void)
{
	return s_controlsCapturing;
}

// CHANGES.

int NativeControls_Assign(int action, int scancode, int *swappedWith)
{
	int before[NATIVE_CONTROLS_ACTION_COUNT];
	int old;
	int first = -1;
	int a;
	int b;

	if (swappedWith != NULL)
	{
		*swappedWith = -1;
	}
	if (!NativeControls_ValidAction(action) || !NativeControls_KeyAllowed(scancode))
	{
		return 0;
	}
	if (s_controlsKeys[action] == scancode)
	{
		return 0;
	}

	memcpy(before, s_controlsKeys, sizeof(before));
	old = s_controlsKeys[action];

	for (int other = 0; other < NATIVE_CONTROLS_ACTION_COUNT; other++)
	{
		if ((other != action) && NativeControls_Conflict(action, other, scancode, before[other]))
		{
			s_controlsKeys[other] = old;
			if (first < 0)
			{
				first = other;
			}
		}
	}
	s_controlsKeys[action] = scancode;

	// The old key can meet a new conflict: a "both" action displaced onto it
	// clashes with a menu action that sat there next to the race action. The one
	// that was not displaced takes the key the action now holds. A few rounds
	// settle it; a layout that still clashes is never kept.
	for (int round = 0; round < 8; round++)
	{
		if (!NativeControls_FindConflict(s_controlsKeys, &a, &b) || (a == action) || (b == action))
		{
			break;
		}
		s_controlsKeys[(before[a] == old) ? a : b] = scancode;
	}
	if (NativeControls_FindConflict(s_controlsKeys, &a, &b))
	{
		memcpy(s_controlsKeys, before, sizeof(before));
		return 0;
	}

	if (swappedWith != NULL)
	{
		*swappedWith = first;
	}
	if (first >= 0)
	{
		Platform_Log("[CTR Controls] %s: %s -> %s (swapped with %s)\n", s_controlsActions[action].fileKey, NativeControls_SdlName(old),
		             NativeControls_SdlName(scancode), s_controlsActions[first].fileKey);
	}
	else
	{
		Platform_Log("[CTR Controls] %s: %s -> %s\n", s_controlsActions[action].fileKey, NativeControls_SdlName(old),
		             NativeControls_SdlName(scancode));
	}
	NativeControls_Save();
	return 1;
}

void NativeControls_ResetDefaults(void)
{
	NativeControls_SetStock();
	Platform_Log("[CTR Controls] stock layout restored\n");
	NativeControls_Save();
}

// SELFTEST (ctest controls_selftest, main.c --controls-selftest <dir>).
//
// Everything runs inside dir and on fake keyboard states; no window, no SDL_Init.

global_variable int s_controlsChecks;
global_variable int s_controlsFailures;

#define CONTROLS_CHECK(condition, what)                                                                                                    \
	do                                                                                                                                   \
	{                                                                                                                                    \
		s_controlsChecks++;                                                                                                              \
		if (!(condition))                                                                                                                \
		{                                                                                                                                \
			s_controlsFailures++;                                                                                                        \
			printf("controls selftest: FAIL %s (line %d)\n", what, __LINE__);                                                            \
		}                                                                                                                                \
	} while (0)

internal void NativeControls_TestMakeDir(const char *dir)
{
	char partial[512];
	size_t length = strlen(dir);

	if (length >= sizeof(partial))
	{
		return;
	}
	for (size_t i = 1; i <= length; i++)
	{
		if ((dir[i] == '/') || (dir[i] == '\\') || (dir[i] == '\0'))
		{
			memcpy(partial, dir, i);
			partial[i] = '\0';
#if defined(_WIN32)
			_mkdir(partial);
#else
			mkdir(partial, 0777);
#endif
		}
	}
}

internal void NativeControls_TestWriteFile(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	if (file != NULL)
	{
		fputs(text, file);
		fclose(file);
	}
}

internal int NativeControls_TestFileExists(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (file != NULL)
	{
		fclose(file);
		return 1;
	}
	return 0;
}

// The fixed table this module replaced, copied literally.
internal u16 NativeControls_TestOldTable(const bool *keys)
{
	u16 buttons = 0xffff;

	if (keys[SDL_SCANCODE_X]) buttons &= (u16)~0x8000;
	if (keys[SDL_SCANCODE_V]) buttons &= (u16)~0x2000;
	if (keys[SDL_SCANCODE_Z]) buttons &= (u16)~0x1000;
	if (keys[SDL_SCANCODE_C]) buttons &= (u16)~0x4000;
	if (keys[SDL_SCANCODE_LSHIFT]) buttons &= (u16)~0x400;
	if (keys[SDL_SCANCODE_LCTRL]) buttons &= (u16)~0x100;
	if (keys[SDL_SCANCODE_LEFTBRACKET]) buttons &= (u16)~0x2;
	if (keys[SDL_SCANCODE_RSHIFT]) buttons &= (u16)~0x800;
	if (keys[SDL_SCANCODE_RCTRL]) buttons &= (u16)~0x200;
	if (keys[SDL_SCANCODE_RIGHTBRACKET]) buttons &= (u16)~0x4;
	if (keys[SDL_SCANCODE_UP]) buttons &= (u16)~0x10;
	if (keys[SDL_SCANCODE_DOWN]) buttons &= (u16)~0x40;
	if (keys[SDL_SCANCODE_LEFT]) buttons &= (u16)~0x80;
	if (keys[SDL_SCANCODE_RIGHT]) buttons &= (u16)~0x20;
	if (keys[SDL_SCANCODE_SPACE]) buttons &= (u16)~0x1;
	if (keys[SDL_SCANCODE_RETURN]) buttons &= (u16)~0x8;
	return buttons;
}

global_variable unsigned int s_controlsTestSeed = 12345u;

internal unsigned int NativeControls_TestRandom(void)
{
	s_controlsTestSeed = (s_controlsTestSeed * 1664525u) + 1013904223u;
	return s_controlsTestSeed >> 8;
}

internal int NativeControls_TestIsStock(void)
{
	for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		if (s_controlsKeys[action] != s_controlsActions[action].stockKey)
		{
			return 0;
		}
	}
	return 1;
}

internal int NativeControls_TestValid(void)
{
	int a;
	int b;
	return !NativeControls_FindConflict(s_controlsKeys, &a, &b);
}

int NativeControls_SelfTest(const char *dir)
{
	char path[600];
	char text[2048];
	bool keys[SDL_SCANCODE_COUNT];
	int allowed[SDL_SCANCODE_COUNT];
	int allowedCount = 0;
	int swapped;
	FILE *file;

	s_controlsChecks = 0;
	s_controlsFailures = 0;
	if ((dir == NULL) || (dir[0] == '\0'))
	{
		printf("controls selftest FAILED: no directory\n");
		return 1;
	}
	NativeControls_TestMakeDir(dir);
	snprintf(path, sizeof(path), "%s/ctr-controls.cfg", dir);

	for (int key = 1; key < SDL_SCANCODE_COUNT; key++)
	{
		if (NativeControls_KeyAllowed(key))
		{
			allowed[allowedCount++] = key;
		}
	}
	CONTROLS_CHECK(allowedCount > 60, "allowed keys exist");
	CONTROLS_CHECK(NativeControls_KeyAllowed(SDL_SCANCODE_ESCAPE) == 0, "escape not allowed");
	CONTROLS_CHECK(NativeControls_KeyAllowed(SDL_SCANCODE_F5) == 0, "F5 not allowed");
	CONTROLS_CHECK(NativeControls_KeyAllowed(SDL_SCANCODE_LALT) == 0, "left alt not allowed");
	CONTROLS_CHECK(NativeControls_KeyAllowed(SDL_SCANCODE_LGUI) == 0, "gui not allowed");
	CONTROLS_CHECK(NativeControls_KeyAllowed(SDL_SCANCODE_KP_ENTER) == 1, "keypad enter allowed");
	CONTROLS_CHECK(NativeControls_KeyAllowed(SDL_SCANCODE_0) == 1, "0 allowed");
	CONTROLS_CHECK(NativeControls_KeyAllowed(0) == 0, "scancode 0 not allowed");

	// Names must work without SDL_Init.
	CONTROLS_CHECK(SDL_GetScancodeFromName("Left Shift") == SDL_SCANCODE_LSHIFT, "name lookup without init");
	CONTROLS_CHECK(SDL_GetScancodeFromName("c") == SDL_SCANCODE_C, "name lookup is case-insensitive");

	// Labels.
	NativeControls_KeyLabel(SDL_SCANCODE_LEFTBRACKET, text, sizeof(text));
	CONTROLS_CHECK(strcmp(text, "LBRACKET") == 0, "label [");
	NativeControls_KeyLabel(SDL_SCANCODE_LSHIFT, text, sizeof(text));
	CONTROLS_CHECK(strcmp(text, "LEFT SHIFT") == 0, "label left shift");
	NativeControls_KeyLabel(SDL_SCANCODE_KP_MULTIPLY, text, sizeof(text));
	CONTROLS_CHECK(strcmp(text, "KEYPAD MULTIPLY") == 0, "label keypad multiply");
	NativeControls_KeyLabel(SDL_SCANCODE_KP_3, text, sizeof(text));
	CONTROLS_CHECK(strcmp(text, "KEYPAD 3") == 0, "label keypad 3");
	NativeControls_KeyLabel(0, text, sizeof(text));
	CONTROLS_CHECK(strcmp(text, "NONE") == 0, "label none");
	for (int i = 0; i < allowedCount; i++)
	{
		int ok;
		NativeControls_KeyLabel(allowed[i], text, sizeof(text));
		ok = (text[0] != '\0') && (strlen(text) <= CONTROLS_LABEL_MAX);
		for (const char *c = text; *c != '\0'; c++)
		{
			ok = ok && (((*c >= 'A') && (*c <= 'Z')) || ((*c >= '0') && (*c <= '9')) || (*c == ' '));
		}
		CONTROLS_CHECK(ok, "label of an allowed key is drawable");
	}
	CONTROLS_CHECK(strcmp(NativeControls_ActionLabel(NATIVE_CONTROLS_JUMP), "JUMP AND DRIFT") == 0, "action label");

	// 1. The stock layout equals the old fixed table, in both contexts.
	NativeControls_SetStock();
	CONTROLS_CHECK(NativeControls_TestValid(), "stock layout valid");
	for (int context = 0; context <= 1; context++)
	{
		for (int i = 0; i < allowedCount; i++)
		{
			memset(keys, 0, sizeof(keys));
			keys[allowed[i]] = 1;
			CONTROLS_CHECK(NativeControls_ReadFrom(s_controlsKeys, keys, context) == NativeControls_TestOldTable(keys), "stock single key");
		}
		for (int n = 0; n < 200; n++)
		{
			int count = 1 + (int)(NativeControls_TestRandom() % 4);
			memset(keys, 0, sizeof(keys));
			for (int k = 0; k < count; k++)
			{
				keys[allowed[NativeControls_TestRandom() % (unsigned int)allowedCount]] = 1;
			}
			CONTROLS_CHECK(NativeControls_ReadFrom(s_controlsKeys, keys, context) == NativeControls_TestOldTable(keys), "stock combination");
		}
	}

	// 2. A missing file is created with the stock layout.
	remove(path);
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(NativeControls_TestFileExists(path), "missing file created");
	CONTROLS_CHECK(NativeControls_TestIsStock(), "missing file gives stock");
	for (int i = 0; i < 5; i++)
	{
		s_controlsKeys[i] = SDL_SCANCODE_A + i;
	}
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(NativeControls_TestIsStock(), "written file parses back to stock");
	CONTROLS_CHECK((s_controlsLastRead == NATIVE_CONTROLS_ACTION_COUNT) && (s_controlsLastSkipped == 0), "written file: all read, none skipped");
	memset(text, 0, sizeof(text));
	file = fopen(path, "rb");
	if (file != NULL)
	{
		(void)fread(text, 1, sizeof(text) - 1, file);
		fclose(file);
	}
	for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
	{
		char needle[64];
		snprintf(needle, sizeof(needle), "\n%s ", s_controlsActions[action].fileKey);
		CONTROLS_CHECK(strstr(text, needle) != NULL, "every action has a line");
	}

	// 3. A file with every action changed.
	{
		static const int custom[NATIVE_CONTROLS_ACTION_COUNT] = {SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_E,
		                                                         SDL_SCANCODE_Q, SDL_SCANCODE_F, SDL_SCANCODE_R, SDL_SCANCODE_G, SDL_SCANCODE_1,
		                                                         SDL_SCANCODE_2, SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L,
		                                                         SDL_SCANCODE_O, SDL_SCANCODE_U, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6};
		int length = 0;
		text[0] = '\0';
		for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
		{
			length += snprintf(text + length, sizeof(text) - (size_t)length, "%s %s\n", s_controlsActions[action].fileKey,
			                   SDL_GetScancodeName((SDL_Scancode)custom[action]));
		}
		NativeControls_TestWriteFile(path, text);
		NativeControls_LoadFrom(path, 0);
		for (int action = 0; action < NATIVE_CONTROLS_ACTION_COUNT; action++)
		{
			CONTROLS_CHECK(s_controlsKeys[action] == custom[action], "custom layout read exactly");
		}
		CONTROLS_CHECK((s_controlsLastRead == NATIVE_CONTROLS_ACTION_COUNT) && (s_controlsLastSkipped == 0), "custom layout counts");
	}

	// 4. A file with everything wrong: unknown action, unknown key, reserved
	// keys, empty value, garbage, CRLF, upper-case action name, key with a space.
	NativeControls_TestWriteFile(path,
	                             "# test\r\n"
	                             "\r\n"
	                             "turbo W\r\n"
	                             "gas Banana\r\n"
	                             "brake Escape\r\n"
	                             "jump F5\r\n"
	                             "jump2\r\n"
	                             "item   W  \r\n"
	                             "garbage\r\n"
	                             "REAR_VIEW Left Shift\r\n"
	                             "camera Left Alt\r\n");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_C, "unknown key keeps stock");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_BRAKE] == SDL_SCANCODE_X, "escape keeps stock");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_JUMP] == SDL_SCANCODE_RSHIFT, "F5 keeps stock");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_JUMP2] == SDL_SCANCODE_LSHIFT, "empty value keeps stock");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_ITEM] == SDL_SCANCODE_W, "padded line read with CRLF");
	// rear_view on Left Shift clashes with jump2's stock key; rear_view came later.
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_REAR_VIEW] == SDL_SCANCODE_RCTRL, "later clashing line back to stock");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_CAMERA] == SDL_SCANCODE_LCTRL, "alt keeps stock");
	CONTROLS_CHECK(NativeControls_TestValid(), "wrong file ends valid");
	CONTROLS_CHECK(s_controlsLastRead == 1, "wrong file: one action read");
	CONTROLS_CHECK(s_controlsLastSkipped == 8, "wrong file: skipped count");

	// 5. Duplicates and conflicts.
	NativeControls_TestWriteFile(path, "gas C\nbrake C\n");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_C, "conflict: earlier line keeps its key");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_BRAKE] == SDL_SCANCODE_X, "conflict: later line goes back to stock");
	CONTROLS_CHECK(NativeControls_TestValid(), "conflict ends valid");
	// brake first, gas later: gas's stock key is C itself, so brake has to give way.
	NativeControls_TestWriteFile(path, "brake C\ngas C\n");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_BRAKE] == SDL_SCANCODE_X, "conflict with a stock owner: the other gives way");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_C, "conflict with a stock owner: gas on stock");
	CONTROLS_CHECK(NativeControls_TestValid(), "conflict with a stock owner ends valid");
	NativeControls_TestWriteFile(path, "gas Up\n");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK((s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_UP) && (s_controlsKeys[NATIVE_CONTROLS_MENU_UP] == SDL_SCANCODE_UP),
	               "race and menu may share a key");
	CONTROLS_CHECK(NativeControls_TestValid(), "shared key layout valid");
	NativeControls_TestWriteFile(path, "gas W\ngas Q\n");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_Q, "given twice: later line wins");
	CONTROLS_CHECK((s_controlsLastRead == 1) && (s_controlsLastSkipped == 1), "given twice: counts");
	NativeControls_TestWriteFile(path, "start C\n");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(NativeControls_TestValid(), "both-group clash ends valid");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_START] == SDL_SCANCODE_RETURN, "both-group clash: start back to stock");
	NativeControls_TestWriteFile(path, "");
	NativeControls_LoadFrom(path, 0);
	CONTROLS_CHECK(NativeControls_TestIsStock(), "empty file gives stock");

	// 6. Assign and swap.
	swapped = 99;
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_GAS, SDL_SCANCODE_X, &swapped) == 1, "assign gas to X");
	CONTROLS_CHECK(swapped == NATIVE_CONTROLS_BRAKE, "swapped with brake");
	CONTROLS_CHECK((s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_X) && (s_controlsKeys[NATIVE_CONTROLS_BRAKE] == SDL_SCANCODE_C),
	               "gas and brake traded");
	CONTROLS_CHECK(NativeControls_TestValid(), "valid after swap");
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_GAS, SDL_SCANCODE_X, &swapped) == 0, "same key changes nothing");
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_GAS, SDL_SCANCODE_ESCAPE, &swapped) == 0, "escape refused");
	CONTROLS_CHECK(swapped == -1, "refusal sets swapped to -1");
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_GAS, SDL_SCANCODE_F5, &swapped) == 0, "F5 refused");
	CONTROLS_CHECK(NativeControls_Assign(-1, SDL_SCANCODE_W, &swapped) == 0, "bad action refused");
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_ACTION_COUNT, SDL_SCANCODE_W, &swapped) == 0, "bad action refused (high)");
	NativeControls_ResetDefaults();
	CONTROLS_CHECK(NativeControls_TestIsStock(), "reset gives stock");
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_START, SDL_SCANCODE_C, &swapped) == 1, "assign start to C");
	CONTROLS_CHECK((s_controlsKeys[NATIVE_CONTROLS_GAS] == SDL_SCANCODE_RETURN) && (s_controlsKeys[NATIVE_CONTROLS_CONFIRM] == SDL_SCANCODE_RETURN),
	               "gas and confirm both take return");
	CONTROLS_CHECK(NativeControls_TestValid(), "valid after both-group swap");
	NativeControls_ResetDefaults();
	CONTROLS_CHECK(NativeControls_Assign(NATIVE_CONTROLS_GAS, SDL_SCANCODE_RETURN, &swapped) == 1, "assign gas onto start's key");
	CONTROLS_CHECK(NativeControls_TestValid(), "valid after gas onto start's key");
	CONTROLS_CHECK(s_controlsKeys[NATIVE_CONTROLS_START] == SDL_SCANCODE_C, "start took gas's old key");
	NativeControls_ResetDefaults();
	{
		int changed = 0;
		for (int n = 0; n < 500; n++)
		{
			int action = (int)(NativeControls_TestRandom() % NATIVE_CONTROLS_ACTION_COUNT);
			int key = allowed[NativeControls_TestRandom() % (unsigned int)allowedCount];
			changed += NativeControls_Assign(action, key, &swapped);
			CONTROLS_CHECK(NativeControls_TestValid(), "layout valid after random assign");
		}
		CONTROLS_CHECK(changed > 250, "random assigns mostly accepted");

		// 8. Save and load round trip of that layout.
		{
			int saved[NATIVE_CONTROLS_ACTION_COUNT];
			memcpy(saved, s_controlsKeys, sizeof(saved));
			CONTROLS_CHECK(NativeControls_SaveTo(path) == 1, "save");
			NativeControls_SetStock();
			NativeControls_LoadFrom(path, 0);
			CONTROLS_CHECK(memcmp(saved, s_controlsKeys, sizeof(saved)) == 0, "save and load round trip");
			CONTROLS_CHECK((s_controlsLastRead == NATIVE_CONTROLS_ACTION_COUNT) && (s_controlsLastSkipped == 0), "round trip counts");
		}
	}

	// 8b. Locked: stock, the file neither read nor written (Assign included).
	{
		char lockedPath[620];
		snprintf(lockedPath, sizeof(lockedPath), "%s/locked.cfg", dir);
		remove(lockedPath);
		NativeControls_LoadFrom(lockedPath, 1);
		CONTROLS_CHECK(NativeControls_TestIsStock(), "locked gives stock");
		CONTROLS_CHECK(!NativeControls_TestFileExists(lockedPath), "locked does not write the file");
		NativeControls_TestWriteFile(lockedPath, "gas W\n");
		NativeControls_LoadFrom(lockedPath, 1);
		CONTROLS_CHECK(NativeControls_TestIsStock(), "locked does not read the file");
		remove(lockedPath);
		s_controlsLocked = 0;
	}

	// 7. Context: gas on Up.
	NativeControls_SetStock();
	s_controlsKeys[NATIVE_CONTROLS_GAS] = SDL_SCANCODE_UP;
	CONTROLS_CHECK(NativeControls_TestValid(), "gas on up valid");
	memset(keys, 0, sizeof(keys));
	keys[SDL_SCANCODE_UP] = 1;
	CONTROLS_CHECK(NativeControls_ReadFrom(s_controlsKeys, keys, 1) == (u16)~CONTROLS_PAD_CROSS, "race: up is cross only");
	CONTROLS_CHECK(NativeControls_ReadFrom(s_controlsKeys, keys, 0) == (u16)~CONTROLS_PAD_UP, "menu: up is d-pad up only");
	memset(keys, 0, sizeof(keys));
	keys[SDL_SCANCODE_DOWN] = 1;
	CONTROLS_CHECK(NativeControls_ReadFrom(s_controlsKeys, keys, 1) == (u16)~CONTROLS_PAD_DOWN, "race: down still d-pad down");
	CONTROLS_CHECK(NativeControls_ReadFrom(s_controlsKeys, keys, 0) == (u16)~CONTROLS_PAD_DOWN, "menu: down is d-pad down");

	// 9. Capture on a fake keyboard.
	NativeControls_SetStock();
	s_controlsCapturing = 0;
	s_controlsWaitRelease = 0;
	memset(keys, 0, sizeof(keys));
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == NATIVE_CONTROLS_CAPTURE_CANCEL, "poll without begin cancels");
	keys[SDL_SCANCODE_RETURN] = 1; // held while the capture opens
	NativeControls_BeginCaptureOn(keys);
	CONTROLS_CHECK(s_controlsCapturing == 1, "capturing");
	CONTROLS_CHECK(NativeControls_ReadKeyboard(keys, 0) == 0xffff, "pad empty while capturing");
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == NATIVE_CONTROLS_CAPTURE_WAITING, "held key does not count");
	keys[SDL_SCANCODE_F5] = 1;
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == NATIVE_CONTROLS_CAPTURE_RESERVED, "F5 reserved");
	CONTROLS_CHECK(s_controlsCapturing == 1, "reserved keeps capturing");
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == NATIVE_CONTROLS_CAPTURE_WAITING, "same F5 not reported twice");
	keys[SDL_SCANCODE_F5] = 0;
	keys[SDL_SCANCODE_W] = 1;
	keys[SDL_SCANCODE_A] = 1;
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == SDL_SCANCODE_A, "lowest new key wins");
	CONTROLS_CHECK(s_controlsCapturing == 0, "capture ended");
	CONTROLS_CHECK(NativeControls_ReadKeyboard(keys, 0) == 0xffff, "pad empty until keys released");
	keys[SDL_SCANCODE_W] = 0;
	keys[SDL_SCANCODE_A] = 0;
	CONTROLS_CHECK(NativeControls_ReadKeyboard(keys, 0) == 0xffff, "return still held, still empty");
	keys[SDL_SCANCODE_RETURN] = 0;
	CONTROLS_CHECK(NativeControls_ReadKeyboard(keys, 0) == 0xffff, "all released: nothing held");
	keys[SDL_SCANCODE_C] = 1;
	CONTROLS_CHECK(NativeControls_ReadKeyboard(keys, 1) == (u16)~CONTROLS_PAD_CROSS, "pad works again after release");
	memset(keys, 0, sizeof(keys));
	NativeControls_BeginCaptureOn(keys);
	keys[SDL_SCANCODE_ESCAPE] = 1;
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == NATIVE_CONTROLS_CAPTURE_CANCEL, "escape cancels");
	CONTROLS_CHECK(s_controlsCapturing == 0, "escape ends capture");
	CONTROLS_CHECK(NativeControls_ReadKeyboard(keys, 0) == 0xffff, "escape still held, pad empty");
	memset(keys, 0, sizeof(keys));
	(void)NativeControls_ReadKeyboard(keys, 0);
	NativeControls_BeginCaptureOn(keys);
	keys[SDL_SCANCODE_LEFTBRACKET] = 1;
	CONTROLS_CHECK(NativeControls_PollCaptureOn(keys) == SDL_SCANCODE_LEFTBRACKET, "bracket taken");

	// Leave the module as the game starts it.
	memset(keys, 0, sizeof(keys));
	(void)NativeControls_ReadKeyboard(keys, 0);
	s_controlsCapturing = 0;
	s_controlsWaitRelease = 0;
	NativeControls_SetStock();
	s_controlsLocked = 0;

	if (s_controlsFailures == 0)
	{
		printf("controls selftest passed: %d checks, 0 failures\n", s_controlsChecks);
		return 0;
	}
	printf("controls selftest FAILED: %d checks, %d failures\n", s_controlsChecks, s_controlsFailures);
	return 1;
}
