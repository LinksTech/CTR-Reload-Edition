#include <common.h>
#include <platform/native_controls.h>

// ===========================================================================
// CONTROLS PAGE IN THE OPTIONS BOX (keyboard rebinding), and the race context.
//
// One line a pad action of the keyboard (platform/native_controls.h), in three
// groups under small white headings, in English:
//   RACE   GAS, BRAKE, STEER LEFT, STEER RIGHT, JUMP AND DRIFT, JUMP 2, ITEM,
//          REAR VIEW, CAMERA
//   MENU   MENU UP, MENU DOWN, MENU LEFT, MENU RIGHT, CONFIRM, BACK
//   BOTH   START, SELECT
// and last the row RESET TO DEFAULT (no value). The label is orange at x 76,
// the key name white and right-aligned at x 436 (NativeControls_KeyLabel). The
// enum of the header runs race, START, SELECT, menu; the page shows race, menu,
// both, so a table maps a line to its action.
//
// Keys: up/down choose the row (around the ends, headings are skipped), cross
// on an action starts the key capture (the value shows PRESS A KEY, the next
// key becomes the action's key, ESC cancels), cross on RESET TO DEFAULT puts the
// stock layout back, triangle or square goes back. Left and right do nothing.
// While a key is captured every pad tap is dropped; a key the page does not
// take (NATIVE_CONTROLS_CAPTURE_RESERVED) says so and the capture goes on. A
// key that another action held makes a swap (the other action gets the old key
// of this one), which the hint names. A change is saved by
// NativeControls_Assign itself; under --settings-defaults (Platform_SettingsLocked)
// it only holds for the session, which the last hint says.
//
// LAYOUT, like the MODS page (native_mods_page.c): title FONT_BIG at y 26,
// separator at y 43, background x 56..456. The lines do not fit (3 headings and
// 18 rows), so NATIVE_CONTROLS_PAGE_VISIBLE = 7 of them stand at pitch 15 from
// y 62 (the last at y 152, its text ends near 162). The hints start 6 below the
// last pitch, at y 173, one pitch of 10 each; the background ends 27 below the
// first hint (y 200), room for two hints, and moves down by 10 for a third.
// That is the same 20..200 frame as the MODS page, so it sits in the same
// part of the screen. The view scrolls so that the cursor row stays visible,
// with its heading when it is the first row of a group; the bar right of the
// rows (x 446, like MODS) shows where the view is.
//
// THE WAY IN is the one of the GRAPHICS page (native_graphics.c): own title
// route (MM_Title.c only knows 0..5), the title flies out, after that this
// screen stands; the way back is MM_JumpTo_Title_Returning, the cursor lands on
// CONTROLS again. One centred panel in widescreen ("menu-controls",
// game/native_uidecl.c).
//
// Widths in FONT_SMALL (13 a character, 7 for ':' and '.'): the longest label
// JUMP AND DRIFT is 182 (76 to 258), the longest key name 15 characters, 195
// (from 241). A value that would come closer than NATIVE_CONTROLS_PAGE_GAP to
// the end of its own label is cut from the right (DecalFont_GetLineWidth) so it
// never runs into the label. Only characters FONT_SMALL draws are used (no
// brackets, no ">").
// ===========================================================================

int Platform_SettingsLocked(void);
void Platform_Log(const char *format, ...);

// A title route that MM_Title.c does not know (0..5 are retail; GRAPHICS is 0x40).
#define NATIVE_CONTROLS_ROUTE 0x41

#define NATIVE_CONTROLS_PAGE_VISIBLE     7
#define NATIVE_CONTROLS_PAGE_ROW_Y       62
#define NATIVE_CONTROLS_PAGE_PITCH       15
#define NATIVE_CONTROLS_PAGE_HEAD_X      66
#define NATIVE_CONTROLS_PAGE_LABEL_X     76
#define NATIVE_CONTROLS_PAGE_VALUE_X     436
#define NATIVE_CONTROLS_PAGE_BAR_X       446
#define NATIVE_CONTROLS_PAGE_GAP         10
#define NATIVE_CONTROLS_PAGE_HINT_PITCH  10
#define NATIVE_CONTROLS_PAGE_BACK_Y      20
#define NATIVE_CONTROLS_PAGE_NOTE_FRAMES 90
#define NATIVE_CONTROLS_PAGE_SWAP_FRAMES 120

// A line of the page: a heading, an action, or the reset row.
#define NATIVE_CONTROLS_LINE_HEADING (-1)
#define NATIVE_CONTROLS_LINE_RESET   (-2)

global_variable const struct
{
	const char *text;
	int action;
} s_nativeControlsPageLines[] = {
    {"RACE", NATIVE_CONTROLS_LINE_HEADING},
    {NULL, NATIVE_CONTROLS_GAS},
    {NULL, NATIVE_CONTROLS_BRAKE},
    {NULL, NATIVE_CONTROLS_STEER_LEFT},
    {NULL, NATIVE_CONTROLS_STEER_RIGHT},
    {NULL, NATIVE_CONTROLS_JUMP},
    {NULL, NATIVE_CONTROLS_JUMP2},
    {NULL, NATIVE_CONTROLS_ITEM},
    {NULL, NATIVE_CONTROLS_REAR_VIEW},
    {NULL, NATIVE_CONTROLS_CAMERA},
    {"MENU", NATIVE_CONTROLS_LINE_HEADING},
    {NULL, NATIVE_CONTROLS_MENU_UP},
    {NULL, NATIVE_CONTROLS_MENU_DOWN},
    {NULL, NATIVE_CONTROLS_MENU_LEFT},
    {NULL, NATIVE_CONTROLS_MENU_RIGHT},
    {NULL, NATIVE_CONTROLS_CONFIRM},
    {NULL, NATIVE_CONTROLS_BACK},
    {"BOTH", NATIVE_CONTROLS_LINE_HEADING},
    {NULL, NATIVE_CONTROLS_START},
    {NULL, NATIVE_CONTROLS_SELECT},
    {"RESET TO DEFAULT", NATIVE_CONTROLS_LINE_RESET},
};

#define NATIVE_CONTROLS_PAGE_LINES ((int)(sizeof(s_nativeControlsPageLines) / sizeof(s_nativeControlsPageLines[0])))

global_variable int s_nativeControlsPageLine = 1;    // the cursor: a line that is not a heading
global_variable int s_nativeControlsPageScroll = 0;  // the first line in view
global_variable int s_nativeControlsPageAction = -1; // the action being captured, or -1
global_variable int s_nativeControlsPageReserved = 0;
global_variable int s_nativeControlsPageNoteFrames = 0;
global_variable char s_nativeControlsPageNote[48];

internal void NativeControlsPage_Proc(struct RectMenu *menu);

global_variable struct RectMenu s_nativeControlsMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .state = DISABLE_INPUT_ALLOW_FUNCPTRS,
    .funcPtr = NativeControlsPage_Proc,
};

// For the menu anchors in widescreen (native_uidecl.c).
int NativeControlsPage_Active(void)
{
	return sdata->ptrActiveMenu == &s_nativeControlsMenu;
}

// From the OPTIONS box (native_menuscreen.c, NativeMenuOptions_Proc).
void NativeControlsPage_Open(struct RectMenu *optionsBox)
{
	D230.desiredMenuIndex = NATIVE_CONTROLS_ROUTE;
	D230.titleMenuState = TITLE_MENU_STATE_EXITING;
	optionsBox->state |= ONLY_DRAW_TITLE;
	s_nativeControlsPageLine = 1;
	s_nativeControlsPageScroll = 0;
	s_nativeControlsPageAction = -1;
	s_nativeControlsPageReserved = 0;
	s_nativeControlsPageNoteFrames = 0;
	Platform_Log("[CTR Menu] OPTIONS: CONTROLS - the title flies out, then the CONTROLS page\n");
}

// Per frame from NativeMenuLock_Tick: once the title has flown out, the page stands.
void NativeControlsPage_Tick(void)
{
	if ((D230.desiredMenuIndex == NATIVE_CONTROLS_ROUTE) && (D230.titleMenuState == TITLE_MENU_STATE_EXITING) &&
	    (D230.titleMenuTransitionFrame > D230.titleMenuTransitionDurationFrames) && (sdata->ptrActiveMenu != &s_nativeControlsMenu) &&
	    (sdata->ptrDesiredMenu != &s_nativeControlsMenu))
	{
		sdata->ptrDesiredMenu = &s_nativeControlsMenu;
	}
}

// For the keyboard (platform/native_controls.c), every frame: is the player
// driving? Cheap and NULL-safe: it may run before the game state exists. Each
// flag that makes it 0 means the pad is a menu pad then:
//   MAIN_MENU      the main menu level (title, boxes, pages like this one)
//   PAUSE_ALL      the pause menu is up (also covered by ptrActiveMenu)
//   GAME_CUTSCENE  a cutscene, the buttons skip or do nothing
//   END_OF_RACE    after the finish the buttons continue through the menus
//   START_OF_RACE  the camera fly-in before the race, the buttons skip it;
//                  the flag drops when the fly-in is over (CAM.c) and the
//                  traffic light counts after that, so during the countdown
//                  it is clear and holding gas gives CROSS (the race meaning)
//   LOADING        a level is being loaded, there is no race yet
// and sdata->ptrActiveMenu != NULL: any menu box is open (pause, track end
// menus, pages), where the menu meaning wins.
int NativeControlsPage_RaceContext(void)
{
	const struct GameTracker *gGT;

	if (sdata == NULL)
	{
		return 0;
	}

	gGT = sdata->gGT;
	if (gGT == NULL)
	{
		return 0;
	}

	if ((gGT->gameMode1 & (MAIN_MENU | PAUSE_ALL | GAME_CUTSCENE | END_OF_RACE | START_OF_RACE | LOADING)) != 0)
	{
		return 0;
	}

	return (sdata->ptrActiveMenu == NULL) ? 1 : 0;
}

// The value text of an action row, cut to leave the gap after its label.
internal void NativeControlsPage_Value(int action, const char *label, char *text, int size)
{
	int room;
	size_t length;

	text[0] = '\0';

	if (action == s_nativeControlsPageAction)
	{
		snprintf(text, size, "PRESS A KEY");
	}
	else
	{
		NativeControls_KeyLabel(NativeControls_Key(action), text, size);
	}
	text[size - 1] = '\0';

	room = NATIVE_CONTROLS_PAGE_VALUE_X - (NATIVE_CONTROLS_PAGE_LABEL_X + DecalFont_GetLineWidth((char *)label, FONT_SMALL) + NATIVE_CONTROLS_PAGE_GAP);
	length = strlen(text);
	while ((length > 0) && (DecalFont_GetLineWidth(text, FONT_SMALL) > room))
	{
		text[--length] = '\0';
	}
}

// Up (-1) or down (+1) from the cursor line, around the ends, headings skipped.
internal int NativeControlsPage_NextLine(int line, int dir)
{
	int i;

	for (i = 0; i < NATIVE_CONTROLS_PAGE_LINES; i++)
	{
		line = (line + NATIVE_CONTROLS_PAGE_LINES + dir) % NATIVE_CONTROLS_PAGE_LINES;
		if (s_nativeControlsPageLines[line].action != NATIVE_CONTROLS_LINE_HEADING)
		{
			break;
		}
	}
	return line;
}

// The view: the cursor line inside it, and the heading above it too when the
// cursor is on the first row of a group.
internal void NativeControlsPage_FixScroll(void)
{
	const int line = s_nativeControlsPageLine;
	const int top = ((line > 0) && (s_nativeControlsPageLines[line - 1].action == NATIVE_CONTROLS_LINE_HEADING)) ? (line - 1) : line;

	if (top < s_nativeControlsPageScroll)
	{
		s_nativeControlsPageScroll = top;
	}
	if (line >= (s_nativeControlsPageScroll + NATIVE_CONTROLS_PAGE_VISIBLE))
	{
		s_nativeControlsPageScroll = line - (NATIVE_CONTROLS_PAGE_VISIBLE - 1);
	}
	if (s_nativeControlsPageScroll > (NATIVE_CONTROLS_PAGE_LINES - NATIVE_CONTROLS_PAGE_VISIBLE))
	{
		s_nativeControlsPageScroll = NATIVE_CONTROLS_PAGE_LINES - NATIVE_CONTROLS_PAGE_VISIBLE;
	}
	if (s_nativeControlsPageScroll < 0)
	{
		s_nativeControlsPageScroll = 0;
	}
}

internal void NativeControlsPage_SetNote(int frames, const char *format, const char *argument)
{
	snprintf(s_nativeControlsPageNote, sizeof(s_nativeControlsPageNote), format, (argument != NULL) ? argument : "");
	s_nativeControlsPageNoteFrames = frames;
}

internal void NativeControlsPage_Draw(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	const int firstHintY = NATIVE_CONTROLS_PAGE_ROW_Y + (NATIVE_CONTROLS_PAGE_VISIBLE * NATIVE_CONTROLS_PAGE_PITCH) + 6;
	int hintY = firstHintY;
	int cursorY = -1;
	Color color;
	int i;

	DecalFont_DrawLine("CONTROLS", 256, 26, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	NativeControlsPage_FixScroll();

	for (i = 0; i < NATIVE_CONTROLS_PAGE_VISIBLE; i++)
	{
		const int line = s_nativeControlsPageScroll + i;
		const int action = s_nativeControlsPageLines[line].action;
		const int y = NATIVE_CONTROLS_PAGE_ROW_Y + (i * NATIVE_CONTROLS_PAGE_PITCH);

		if (action == NATIVE_CONTROLS_LINE_HEADING)
		{
			DecalFont_DrawLine((char *)s_nativeControlsPageLines[line].text, NATIVE_CONTROLS_PAGE_HEAD_X, y, FONT_SMALL, WHITE);
			continue;
		}

		if (action == NATIVE_CONTROLS_LINE_RESET)
		{
			DecalFont_DrawLine((char *)s_nativeControlsPageLines[line].text, NATIVE_CONTROLS_PAGE_LABEL_X, y, FONT_SMALL, ORANGE);
		}
		else
		{
			const char *label = NativeControls_ActionLabel(action);
			char text[24];

			if (label == NULL)
			{
				label = "";
			}
			NativeControlsPage_Value(action, label, text, sizeof(text));
			DecalFont_DrawLine((char *)label, NATIVE_CONTROLS_PAGE_LABEL_X, y, FONT_SMALL, ORANGE);
			DecalFont_DrawLine(text, NATIVE_CONTROLS_PAGE_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | WHITE));
		}

		if (line == s_nativeControlsPageLine)
		{
			cursorY = y;
		}
	}

	// One status hint: the capture (or why it goes on), or the note of the last change.
	if (s_nativeControlsPageAction >= 0)
	{
		DecalFont_DrawLine(s_nativeControlsPageReserved ? "KEY RESERVED - PRESS ANOTHER" : "ESC CANCELS", 256, hintY, FONT_SMALL,
		                   (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_CONTROLS_PAGE_HINT_PITCH;
	}
	else if (s_nativeControlsPageNoteFrames > 0)
	{
		DecalFont_DrawLine(s_nativeControlsPageNote, 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_CONTROLS_PAGE_HINT_PITCH;
	}

	if (Platform_SettingsLocked())
	{
		DecalFont_DrawLine("NOT SAVED IN THIS RUN", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_CONTROLS_PAGE_HINT_PITCH;
	}

	// Front to back, like NativeGraphics_Draw: text, bar, line, background.
	if (cursorY >= 0)
	{
		RECT cursor = {.x = 74, .y = cursorY - 3, .w = 364, .h = 14};

		CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
	}

	ColorCode_SetPacked(&color, sdata->battleSetup_Color_UI_1);

	{
		const int barY = NATIVE_CONTROLS_PAGE_ROW_Y - 3;
		const int barH = NATIVE_CONTROLS_PAGE_VISIBLE * NATIVE_CONTROLS_PAGE_PITCH;
		RECT thumb = {.x = NATIVE_CONTROLS_PAGE_BAR_X,
		              .y = (s16)(barY + ((barH * s_nativeControlsPageScroll) / NATIVE_CONTROLS_PAGE_LINES)),
		              .w = 3,
		              .h = (s16)((barH * NATIVE_CONTROLS_PAGE_VISIBLE) / NATIVE_CONTROLS_PAGE_LINES)};
		RECT track = {.x = NATIVE_CONTROLS_PAGE_BAR_X, .y = (s16)barY, .w = 3, .h = (s16)barH};
		Color trackColor;

		ColorCode_SetPacked(&trackColor, MakeColorPacked(0x30, 0x30, 0x30));
		CTR_Box_DrawSolidBox(&thumb, color, ot);
		CTR_Box_DrawSolidBox(&track, trackColor, ot);
	}

	{
		RECT separator = {.x = 66, .y = 43, .w = 380, .h = 2};

		RECTMENU_DrawOuterRect_Edge(&separator, color, 0x20, ot);
	}

	{
		const int minBottom = firstHintY + 27;
		const int bottom = (hintY + 7 > minBottom) ? (hintY + 7) : minBottom;
		RECT background = {.x = 56, .y = NATIVE_CONTROLS_PAGE_BACK_Y, .w = 400, .h = bottom - NATIVE_CONTROLS_PAGE_BACK_Y};

		RECTMENU_DrawInnerRect(&background, 4, ot);
	}
}

// While a key is captured: the pad is dead, the keyboard decides.
internal void NativeControlsPage_Capture(void)
{
	const int action = s_nativeControlsPageAction;
	int result;

	if (!NativeControls_Capturing())
	{
		// ended from outside
		s_nativeControlsPageAction = -1;
		return;
	}

	result = NativeControls_PollCapture();

	if (result == NATIVE_CONTROLS_CAPTURE_WAITING)
	{
		return;
	}

	if (result == NATIVE_CONTROLS_CAPTURE_RESERVED)
	{
		s_nativeControlsPageReserved = 1;
		return;
	}

	s_nativeControlsPageAction = -1;
	s_nativeControlsPageReserved = 0;

	if (result == NATIVE_CONTROLS_CAPTURE_CANCEL)
	{
		OtherFX_Play(2, 1);
		return;
	}

	{
		int swapped = -1;

		if (NativeControls_Assign(action, result, &swapped))
		{
			OtherFX_Play(1, 1);
			if (swapped >= 0)
			{
				NativeControlsPage_SetNote(NATIVE_CONTROLS_PAGE_SWAP_FRAMES, "SWAPPED WITH %s", NativeControls_ActionLabel(swapped));
			}
		}
		else
		{
			OtherFX_Play(0, 1);
		}
	}
}

// DISABLE_INPUT_ALLOW_FUNCPTRS: the proc runs every frame, reads the keys
// itself and clears them (like NativeGraphics_Proc).
internal void NativeControlsPage_Proc(struct RectMenu *menu)
{
	(void)menu;

	if (s_nativeControlsPageNoteFrames > 0)
	{
		s_nativeControlsPageNoteFrames--;
	}

	if (s_nativeControlsPageAction >= 0)
	{
		// every pad tap is dropped while a key is wanted
		RECTMENU_ClearInput();
		NativeControlsPage_Capture();
	}
	else
	{
		const int tapped = sdata->buttonTapPerPlayer[0] & (BTN_UP | BTN_DOWN | BTN_CROSS_one | BTN_TRIANGLE | BTN_SQUARE_one);

		if (tapped != 0)
		{
			if ((tapped & BTN_UP) != 0)
			{
				s_nativeControlsPageLine = NativeControlsPage_NextLine(s_nativeControlsPageLine, -1);
				OtherFX_Play(0, 1);
			}
			else if ((tapped & BTN_DOWN) != 0)
			{
				s_nativeControlsPageLine = NativeControlsPage_NextLine(s_nativeControlsPageLine, 1);
				OtherFX_Play(0, 1);
			}
			else if ((tapped & BTN_CROSS_one) != 0)
			{
				const int action = s_nativeControlsPageLines[s_nativeControlsPageLine].action;

				if (action == NATIVE_CONTROLS_LINE_RESET)
				{
					NativeControls_ResetDefaults();
					OtherFX_Play(1, 1);
					NativeControlsPage_SetNote(NATIVE_CONTROLS_PAGE_NOTE_FRAMES, "STOCK LAYOUT RESTORED", NULL);
				}
				else if (action >= 0)
				{
					NativeControls_BeginCapture();
					s_nativeControlsPageAction = action;
					s_nativeControlsPageReserved = 0;
					s_nativeControlsPageNoteFrames = 0;
					OtherFX_Play(1, 1);
				}
			}
			else
			{
				// "go back" sound, back as from GRAPHICS
				OtherFX_Play(2, 1);
				Platform_Log("[CTR Menu] CONTROLS: back to the OPTIONS box\n");
				MM_JumpTo_Title_Returning();
				RECTMENU_ClearInput();
				return;
			}

			RECTMENU_ClearInput();
		}
	}

	NativeControlsPage_Draw();
}
