#include <common.h>

// ===========================================================================
// GRAPHICS PAGE IN THE OPTIONS BOX (2026-09-30, Beta 0).
//
// Exactly four rows, in English:
//   DISPLAY MODE   FULLSCREEN / WINDOWED          at once (between two frames)
//   ASPECT RATIO   AUTO / 4:3 / 16:9 / 21:9       at the next load of a level
//   RESOLUTION     NATIVE / 1X ... 8X             at once
//   ANTI-ALIASING  OFF / 2X / 4X                  at the next frame end
// Up/down chooses the row, left/right the value, triangle goes back.
//
// A SCREEN OF ITS OWN, NOT A BOX OF THE CHAIN. "DISPLAY MODE  FULLSCREEN" is
// 312 points wide in FONT_SMALL; the chain allows 219 (otherwise the
// shadow runs over the 4:3 edge, and the 16:9 zone pushes the chain to the right
// instead of making room, native_view.c:2244-2370). The model is the
// options screen of the pause (MainFreeze.c:549-749): title, separator line, rows with
// the value on the right, highlight bar, background.
//
// THE WAY IN is the one of HIGH SCORE: the title flies out
// (TITLE_MENU_STATE_EXITING), after that this screen stands. MM_Title.c only knows
// the routes 0..5; with this route of its own its switch does nothing, and
// NativeGraphics_Tick takes over as soon as the title is out. The way back is
// MM_JumpTo_Title_Returning as from HIGH SCORE: the chain is still there
// (main menu -> OPTIONS), so the cursor lands on GRAPHICS again.
//
// SAVED: every change goes at once into ctr-settings.cfg
// (platform/native_platform.c, Platform_Graphics*). Under --settings-defaults
// it only acts in the session. Command-line switches apply to their session
// and never go into the file.
// ===========================================================================

void Platform_Log(const char *format, ...);
int Platform_IsFullscreen(void);
int Platform_GetAspectSetting(void);
int Platform_GetAspectPending(void);
int Platform_GetResolutionScaleSetting(void);
int Platform_GetResolutionNativePosition(void);
int Platform_GetResolutionScaleMax(void);
int Platform_GetMsaaRequested(void);
int Platform_SettingsLocked(void);
void Platform_GraphicsSetFullscreen(int on);
void Platform_GraphicsSetAspect(int setting);
void Platform_GraphicsSetResolution(int scale);
void Platform_GraphicsSetMsaa(int samples);

// A title route that MM_Title.c does not know (0..5 are retail).
#define NATIVE_GRAPHICS_ROUTE 0x40

#define NATIVE_GRAPHICS_ROWS         4
#define NATIVE_GRAPHICS_ROW_Y        58
#define NATIVE_GRAPHICS_ROW_PITCH    18
#define NATIVE_GRAPHICS_LABEL_X      76
#define NATIVE_GRAPHICS_VALUE_X      436
#define NATIVE_GRAPHICS_HINT_Y       138
// Fullscreen changes between two frames; until the window reports the new state,
// the row accepts no second change (like DebugMenu.c:381).
#define NATIVE_GRAPHICS_MODE_COOLDOWN 45

enum
{
	NATIVE_GRAPHICS_DISPLAY = 0,
	NATIVE_GRAPHICS_ASPECT,
	NATIVE_GRAPHICS_RESOLUTION,
	NATIVE_GRAPHICS_AA,
};

global_variable const char *const s_nativeGraphicsLabels[NATIVE_GRAPHICS_ROWS] = {"DISPLAY MODE", "ASPECT RATIO", "RESOLUTION", "ANTI-ALIASING"};
global_variable const char *const s_nativeGraphicsAspects[4] = {"AUTO", "4:3", "16:9", "21:9"};

global_variable int s_nativeGraphicsRow = 0;
global_variable int s_nativeGraphicsCooldown = 0;

internal void NativeGraphics_Proc(struct RectMenu *menu);

global_variable struct RectMenu s_nativeGraphicsMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .state = DISABLE_INPUT_ALLOW_FUNCPTRS,
    .funcPtr = NativeGraphics_Proc,
};

// For the menu anchors in widescreen (native_uidecl.c): centred like the
// options screen of the pause.
int NativeGraphics_Active(void)
{
	return sdata->ptrActiveMenu == &s_nativeGraphicsMenu;
}

// From the OPTIONS box (native_menuscreen.c, NativeMenuOptions_Proc).
void NativeGraphics_Open(struct RectMenu *optionsBox)
{
	D230.desiredMenuIndex = NATIVE_GRAPHICS_ROUTE;
	D230.titleMenuState = TITLE_MENU_STATE_EXITING;
	optionsBox->state |= ONLY_DRAW_TITLE;
	s_nativeGraphicsRow = 0;
	Platform_Log("[CTR Menu] OPTIONS: GRAPHICS - the title flies out, then the GRAPHICS page\n");
}

// Per frame from NativeMenuLock_Tick: once the title has flown out, the page stands.
void NativeGraphics_Tick(void)
{
	if (s_nativeGraphicsCooldown > 0)
	{
		s_nativeGraphicsCooldown--;
	}

	if ((D230.desiredMenuIndex == NATIVE_GRAPHICS_ROUTE) && (D230.titleMenuState == TITLE_MENU_STATE_EXITING) &&
	    (D230.titleMenuTransitionFrame > D230.titleMenuTransitionDurationFrames) && (sdata->ptrActiveMenu != &s_nativeGraphicsMenu) &&
	    (sdata->ptrDesiredMenu != &s_nativeGraphicsMenu))
	{
		sdata->ptrDesiredMenu = &s_nativeGraphicsMenu;
	}
}

internal const char *NativeGraphics_Value(int row, char *text, int size)
{
	int value;

	switch (row)
	{
	case NATIVE_GRAPHICS_DISPLAY:
		return Platform_IsFullscreen() ? "FULLSCREEN" : "WINDOWED";

	case NATIVE_GRAPHICS_ASPECT:
		value = Platform_GetAspectSetting();
		return s_nativeGraphicsAspects[((value >= 0) && (value < 4)) ? value : 0];

	case NATIVE_GRAPHICS_RESOLUTION:
		value = Platform_GetResolutionScaleSetting();
		if (value == Platform_GetResolutionNativePosition())
		{
			return "NATIVE";
		}
		snprintf(text, size, "%dX", value);
		return text;

	default:
		value = Platform_GetMsaaRequested();
		return (value >= 4) ? "4X" : ((value >= 2) ? "2X" : "OFF");
	}
}

// Left (-1) or right (+1) on a row. No row runs past the
// end. 0 if nothing changes.
internal int NativeGraphics_Change(int row, int dir)
{
	int value;

	switch (row)
	{
	case NATIVE_GRAPHICS_DISPLAY:
		if (s_nativeGraphicsCooldown > 0)
		{
			return 0;
		}
		// FULLSCREEN is on the left, WINDOWED on the right
		value = (dir < 0) ? 1 : 0;
		if (value == Platform_IsFullscreen())
		{
			return 0;
		}
		Platform_GraphicsSetFullscreen(value);
		s_nativeGraphicsCooldown = NATIVE_GRAPHICS_MODE_COOLDOWN;
		return 1;

	case NATIVE_GRAPHICS_ASPECT:
		value = Platform_GetAspectSetting() + dir;
		if ((value < 0) || (value > 3))
		{
			return 0;
		}
		Platform_GraphicsSetAspect(value);
		return 1;

	case NATIVE_GRAPHICS_RESOLUTION:
	{
		// NATIVE is left of 1X, 8X on the far right
		const int native = Platform_GetResolutionNativePosition();
		int order = Platform_GetResolutionScaleSetting();

		order = (order == native) ? 0 : order;
		order += dir;
		if ((order < 0) || (order > Platform_GetResolutionScaleMax()))
		{
			return 0;
		}
		Platform_GraphicsSetResolution((order == 0) ? native : order);
		return 1;
	}

	default:
	{
		static const int levels[3] = {1, 2, 4};
		int i = 0;

		value = Platform_GetMsaaRequested();
		while ((i < 2) && (levels[i] < value))
		{
			i++;
		}
		i += dir;
		if ((i < 0) || (i > 2))
		{
			return 0;
		}
		Platform_GraphicsSetMsaa(levels[i]);
		return 1;
	}
	}
}

internal void NativeGraphics_Draw(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	char text[16];
	Color color;
	int row;
	int hintY = NATIVE_GRAPHICS_HINT_Y;

	// Front to back, like MainFreeze.c:548: text, bar, line, background.
	DecalFont_DrawLine("GRAPHICS", 256, 26, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	for (row = 0; row < NATIVE_GRAPHICS_ROWS; row++)
	{
		const int y = NATIVE_GRAPHICS_ROW_Y + (row * NATIVE_GRAPHICS_ROW_PITCH);

		DecalFont_DrawLine((char *)s_nativeGraphicsLabels[row], NATIVE_GRAPHICS_LABEL_X, y, FONT_SMALL, ORANGE);
		DecalFont_DrawLine((char *)NativeGraphics_Value(row, text, sizeof(text)), NATIVE_GRAPHICS_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | WHITE));
	}

	if (Platform_GetAspectPending() >= 0)
	{
		DecalFont_DrawLine("ASPECT: APPLIES AT NEXT RACE", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += 10;
	}

	if (Platform_SettingsLocked())
	{
		DecalFont_DrawLine("NOT SAVED IN THIS RUN", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
	}

	{
		RECT cursor = {.x = 74, .y = NATIVE_GRAPHICS_ROW_Y + (s_nativeGraphicsRow * NATIVE_GRAPHICS_ROW_PITCH) - 3, .w = 364, .h = 14};

		CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
	}

	{
		RECT separator = {.x = 66, .y = 43, .w = 380, .h = 2};

		ColorCode_SetPacked(&color, sdata->battleSetup_Color_UI_1);
		RECTMENU_DrawOuterRect_Edge(&separator, color, 0x20, ot);
	}

	{
		RECT background = {.x = 56, .y = 20, .w = 400, .h = 145};

		RECTMENU_DrawInnerRect(&background, 4, ot);
	}
}

// DISABLE_INPUT_ALLOW_FUNCPTRS: the proc runs every frame, reads the keys
// itself and clears them (like MM_NativeTrackSelect.c:554-645).
internal void NativeGraphics_Proc(struct RectMenu *menu)
{
	const int tapped = sdata->buttonTapPerPlayer[0] & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_TRIANGLE | BTN_SQUARE_one);

	(void)menu;

	if (tapped != 0)
	{
		if ((tapped & BTN_UP) != 0)
		{
			s_nativeGraphicsRow = (s_nativeGraphicsRow + NATIVE_GRAPHICS_ROWS - 1) % NATIVE_GRAPHICS_ROWS;
			OtherFX_Play(0, 1);
		}
		else if ((tapped & BTN_DOWN) != 0)
		{
			s_nativeGraphicsRow = (s_nativeGraphicsRow + 1) % NATIVE_GRAPHICS_ROWS;
			OtherFX_Play(0, 1);
		}
		else if ((tapped & (BTN_LEFT | BTN_RIGHT)) != 0)
		{
			OtherFX_Play(NativeGraphics_Change(s_nativeGraphicsRow, ((tapped & BTN_LEFT) != 0) ? -1 : 1) ? 0 : 5, 1);
		}
		else
		{
			// "go back" sound, back as from HIGH SCORE (MM_HighScore.c:252)
			OtherFX_Play(2, 1);
			Platform_Log("[CTR Menu] GRAPHICS: back to the OPTIONS box\n");
			MM_JumpTo_Title_Returning();
			RECTMENU_ClearInput();
			return;
		}

		RECTMENU_ClearInput();
	}

	NativeGraphics_Draw();
}
