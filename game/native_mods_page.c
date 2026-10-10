#include <common.h>

// ===========================================================================
// MODS PAGE (the big view of the MODS box of the track select and cup select).
//
// Main page "MODS", two rows in English:
//   CPU DRIVERS      DEFAULT / ALL RANDOM / ONLY SELECTED
//   SELECT DRIVERS   <n> SELECTED   (grey unless CPU DRIVERS is ONLY SELECTED;
//                                    no arrow: FONT_SMALL draws ">" as a dash)
// Up/down chooses the row (around the ends), left/right the value, cross opens
// SELECT DRIVERS (only when it is not grey), triangle goes back.
//
// Sub page "SELECT DRIVERS": at most six rows at a time, every driver - the
// fifteen retail ones first, then the custom ones - with ON / OFF; the list
// scrolls so the cursor stays visible. Left sets OFF, right sets ON, cross
// toggles, triangle goes back to the main page.
//
// The look is the one of the GRAPHICS page (native_graphics.c): title, separator
// line, rows with the value on the right, highlight bar, background; same
// numbers, same front-to-back order (text, bar, line, background), and the
// same widescreen row: the whole page is one panel, centred
// ("menu-mods", game/native_uidecl.c). The page is drawn by this file only;
// the owner calls NativeModsPage_Open once and then NativeModsPage_Frame every
// frame while NativeModsPage_IsOpen answers 1. The keys are read here and
// cleared after a handled key (like NativeGraphics_Proc).
//
// Widths in FONT_SMALL (13 a character, 7 for ':' and '.'): the longest label
// ("SELECT DRIVERS", 182, 76 to 258) stays 35 left of its value ("47
// SELECTED" at most - 15 retail and 32 roster files -, 143, from 293); "CPU
// DRIVERS" (143, to 219) 48 left of "ONLY SELECTED" (169, from 267). A driver name is cut to what ends
// NATIVE_MODS_PAGE_GAP left of OFF, measured (DecalFont_GetLineWidth).
// ===========================================================================

int NativeMods_CpuDrivers(void);
void NativeMods_SetCpuDrivers(int value);
int NativeMods_DriverCount(void);
const char *NativeMods_DriverName(int index);
int NativeMods_DriverSelected(int index);
void NativeMods_SetDriverSelected(int index, int on);
int NativeMods_SelectedCount(void);
int Platform_SettingsLocked(void);
void Platform_Log(const char *format, ...);

// The values of native_mods.h, as numbers (the constants are not redefined).
#define NATIVE_MODS_PAGE_DEFAULT  0
#define NATIVE_MODS_PAGE_ALL      1
#define NATIVE_MODS_PAGE_SELECTED 2

#define NATIVE_MODS_PAGE_ROWS        2
#define NATIVE_MODS_PAGE_ROW_DRIVERS 1
#define NATIVE_MODS_PAGE_VISIBLE     6
#define NATIVE_MODS_PAGE_NAME_MAX    40
#define NATIVE_MODS_PAGE_ROW_Y       58
#define NATIVE_MODS_PAGE_ROW_PITCH   18
#define NATIVE_MODS_PAGE_LABEL_X     76
#define NATIVE_MODS_PAGE_VALUE_X     436
#define NATIVE_MODS_PAGE_GAP         16
#define NATIVE_MODS_PAGE_HINT_PITCH  10
#define NATIVE_MODS_PAGE_BACK_Y      20

global_variable int s_nativeModsPageOpen = 0;
global_variable int s_nativeModsPageSub = 0;
global_variable int s_nativeModsPageRow = 0;
global_variable int s_nativeModsPageCursor = 0;
global_variable int s_nativeModsPageScroll = 0;

global_variable const char *const s_nativeModsPageLabels[NATIVE_MODS_PAGE_ROWS] = {"CPU DRIVERS", "SELECT DRIVERS"};
global_variable const char *const s_nativeModsPageValues[3] = {"DEFAULT", "ALL RANDOM", "ONLY SELECTED"};

// Separator line and background, the same calls and numbers as
// NativeGraphics_Draw. firstHintY: where the first hint stands; hintY: below
// the last hint drawn.
internal void NativeModsPage_DrawFrame(u32 *ot, int cursorY, int firstHintY, int hintY)
{
	Color color;

	if (cursorY >= 0)
	{
		RECT cursor = {.x = 74, .y = cursorY - 3, .w = 364, .h = 14};

		CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
	}

	{
		RECT separator = {.x = 66, .y = 43, .w = 380, .h = 2};

		ColorCode_SetPacked(&color, sdata->battleSetup_Color_UI_1);
		RECTMENU_DrawOuterRect_Edge(&separator, color, 0x20, ot);
	}

	{
		const int minBottom = firstHintY + 27;
		const int bottom = (hintY + 7 > minBottom) ? (hintY + 7) : minBottom;
		RECT background = {.x = 56, .y = NATIVE_MODS_PAGE_BACK_Y, .w = 400, .h = bottom - NATIVE_MODS_PAGE_BACK_Y};

		RECTMENU_DrawInnerRect(&background, 4, ot);
	}
}

internal int NativeModsPage_Value(void)
{
	const int value = NativeMods_CpuDrivers();

	return ((value >= NATIVE_MODS_PAGE_DEFAULT) && (value <= NATIVE_MODS_PAGE_SELECTED)) ? value : NATIVE_MODS_PAGE_DEFAULT;
}

internal void NativeModsPage_DrawMain(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	char text[24];
	const int value = NativeModsPage_Value();
	const int firstHintY = NATIVE_MODS_PAGE_ROW_Y + (NATIVE_MODS_PAGE_ROWS * NATIVE_MODS_PAGE_ROW_PITCH) + 8;
	int hintY = firstHintY;
	int row;

	DecalFont_DrawLine("MODS", 256, 26, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	snprintf(text, sizeof(text), "%d SELECTED", NativeMods_SelectedCount());

	for (row = 0; row < NATIVE_MODS_PAGE_ROWS; row++)
	{
		const int y = NATIVE_MODS_PAGE_ROW_Y + (row * NATIVE_MODS_PAGE_ROW_PITCH);
		const int grey = (row == NATIVE_MODS_PAGE_ROW_DRIVERS) && (value != NATIVE_MODS_PAGE_SELECTED);

		DecalFont_DrawLine((char *)s_nativeModsPageLabels[row], NATIVE_MODS_PAGE_LABEL_X, y, FONT_SMALL, grey ? GRAY : ORANGE);
		DecalFont_DrawLine((row == 0) ? (char *)s_nativeModsPageValues[value] : text, NATIVE_MODS_PAGE_VALUE_X, y, FONT_SMALL,
		                   (JUSTIFY_RIGHT | (grey ? GRAY : WHITE)));
	}

	// ONLY SELECTED with nothing ticked is DEFAULT (platform/native_mods.c).
	if ((value == NATIVE_MODS_PAGE_SELECTED) && (NativeMods_SelectedCount() == 0))
	{
		DecalFont_DrawLine("NONE SELECTED: AS DEFAULT", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	if (Platform_SettingsLocked())
	{
		DecalFont_DrawLine("NOT SAVED IN THIS RUN", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	NativeModsPage_DrawFrame(ot, NATIVE_MODS_PAGE_ROW_Y + (s_nativeModsPageRow * NATIVE_MODS_PAGE_ROW_PITCH), firstHintY, hintY);
}

// The name of a list row, cut until it ends NATIVE_MODS_PAGE_GAP left of the
// widest value (OFF): never into the value.
internal void NativeModsPage_FitName(const char *source, char name[NATIVE_MODS_PAGE_NAME_MAX + 1])
{
	const int room = NATIVE_MODS_PAGE_VALUE_X - DecalFont_GetLineWidth("OFF", FONT_SMALL) - NATIVE_MODS_PAGE_GAP - NATIVE_MODS_PAGE_LABEL_X;
	size_t length;

	strncpy(name, (source != NULL) ? source : "", NATIVE_MODS_PAGE_NAME_MAX);
	name[NATIVE_MODS_PAGE_NAME_MAX] = '\0';

	length = strlen(name);
	while ((length > 0) && (DecalFont_GetLineWidth(name, FONT_SMALL) > room))
	{
		name[--length] = '\0';
	}
}

internal void NativeModsPage_DrawSub(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	const int count = NativeMods_DriverCount();
	const int firstHintY = NATIVE_MODS_PAGE_ROW_Y + (NATIVE_MODS_PAGE_VISIBLE * NATIVE_MODS_PAGE_ROW_PITCH) + 8;
	int hintY = firstHintY;
	int last = s_nativeModsPageScroll + NATIVE_MODS_PAGE_VISIBLE;
	int i;

	DecalFont_DrawLine("SELECT DRIVERS", 256, 26, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	if (last > count)
	{
		last = count;
	}

	for (i = s_nativeModsPageScroll; i < last; i++)
	{
		const int y = NATIVE_MODS_PAGE_ROW_Y + ((i - s_nativeModsPageScroll) * NATIVE_MODS_PAGE_ROW_PITCH);
		char name[NATIVE_MODS_PAGE_NAME_MAX + 1];

		NativeModsPage_FitName(NativeMods_DriverName(i), name);
		DecalFont_DrawLine(name, NATIVE_MODS_PAGE_LABEL_X, y, FONT_SMALL, ORANGE);
		DecalFont_DrawLine(NativeMods_DriverSelected(i) ? "ON" : "OFF", NATIVE_MODS_PAGE_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | WHITE));
	}

	if (count > NATIVE_MODS_PAGE_VISIBLE)
	{
		char text[24];

		snprintf(text, sizeof(text), "%d-%d OF %d", s_nativeModsPageScroll + 1, last, count);
		DecalFont_DrawLine(text, 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	if (Platform_SettingsLocked())
	{
		DecalFont_DrawLine("NOT SAVED IN THIS RUN", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	NativeModsPage_DrawFrame(ot, NATIVE_MODS_PAGE_ROW_Y + ((s_nativeModsPageCursor - s_nativeModsPageScroll) * NATIVE_MODS_PAGE_ROW_PITCH), firstHintY,
	                         hintY);
}

// Left (-1) or right (+1) on CPU DRIVERS; the value does not run past its
// ends. 0 if nothing changes.
internal int NativeModsPage_Change(int row, int dir)
{
	const int value = NativeModsPage_Value() + dir;

	if ((row != 0) || (value < NATIVE_MODS_PAGE_DEFAULT) || (value > NATIVE_MODS_PAGE_SELECTED))
	{
		return 0;
	}

	NativeMods_SetCpuDrivers(value);
	return 1;
}

// Keeps the cursor inside the list and in view.
internal void NativeModsPage_FixScroll(int count)
{
	if (count <= 0)
	{
		s_nativeModsPageCursor = 0;
		s_nativeModsPageScroll = 0;
		return;
	}
	if (s_nativeModsPageCursor >= count)
	{
		s_nativeModsPageCursor = count - 1;
	}
	if (s_nativeModsPageCursor < 0)
	{
		s_nativeModsPageCursor = 0;
	}
	if (s_nativeModsPageCursor < s_nativeModsPageScroll)
	{
		s_nativeModsPageScroll = s_nativeModsPageCursor;
	}
	if (s_nativeModsPageCursor >= s_nativeModsPageScroll + NATIVE_MODS_PAGE_VISIBLE)
	{
		s_nativeModsPageScroll = s_nativeModsPageCursor - (NATIVE_MODS_PAGE_VISIBLE - 1);
	}
	if (s_nativeModsPageScroll < 0)
	{
		s_nativeModsPageScroll = 0;
	}
}

// Returns 1 if the page was closed by the key.
internal int NativeModsPage_KeysMain(int tapped)
{
	if ((tapped & BTN_UP) != 0)
	{
		s_nativeModsPageRow = (s_nativeModsPageRow + NATIVE_MODS_PAGE_ROWS - 1) % NATIVE_MODS_PAGE_ROWS;
		OtherFX_Play(0, 1);
	}
	else if ((tapped & BTN_DOWN) != 0)
	{
		s_nativeModsPageRow = (s_nativeModsPageRow + 1) % NATIVE_MODS_PAGE_ROWS;
		OtherFX_Play(0, 1);
	}
	else if ((tapped & (BTN_LEFT | BTN_RIGHT)) != 0)
	{
		OtherFX_Play(NativeModsPage_Change(s_nativeModsPageRow, ((tapped & BTN_LEFT) != 0) ? -1 : 1) ? 0 : 5, 1);
	}
	else if ((tapped & (BTN_CROSS_one | BTN_CIRCLE)) != 0)
	{
		if (s_nativeModsPageRow == NATIVE_MODS_PAGE_ROW_DRIVERS)
		{
			if (NativeModsPage_Value() == NATIVE_MODS_PAGE_SELECTED)
			{
				s_nativeModsPageSub = 1;
				s_nativeModsPageCursor = 0;
				s_nativeModsPageScroll = 0;
				OtherFX_Play(1, 1);
				Platform_Log("[CTR Menu] MODS: SELECT DRIVERS\n");
			}
			else
			{
				OtherFX_Play(5, 1);
			}
		}
	}
	else
	{
		OtherFX_Play(2, 1);
		Platform_Log("[CTR Menu] MODS: the page closes\n");
		s_nativeModsPageOpen = 0;
		return 1;
	}

	return 0;
}

internal void NativeModsPage_KeysSub(int tapped)
{
	const int count = NativeMods_DriverCount();

	if ((tapped & (BTN_TRIANGLE | BTN_SQUARE_one)) != 0)
	{
		s_nativeModsPageSub = 0;
		s_nativeModsPageRow = NATIVE_MODS_PAGE_ROW_DRIVERS;
		OtherFX_Play(2, 1);
		return;
	}

	if (count <= 0)
	{
		return;
	}

	if ((tapped & BTN_UP) != 0)
	{
		s_nativeModsPageCursor = (s_nativeModsPageCursor + count - 1) % count;
		OtherFX_Play(0, 1);
	}
	else if ((tapped & BTN_DOWN) != 0)
	{
		s_nativeModsPageCursor = (s_nativeModsPageCursor + 1) % count;
		OtherFX_Play(0, 1);
	}
	else
	{
		const int on = NativeMods_DriverSelected(s_nativeModsPageCursor) ? 1 : 0;
		int next;

		if ((tapped & BTN_LEFT) != 0)
		{
			next = 0;
		}
		else if ((tapped & BTN_RIGHT) != 0)
		{
			next = 1;
		}
		else
		{
			next = !on;
		}

		if (next != on)
		{
			NativeMods_SetDriverSelected(s_nativeModsPageCursor, next);
			OtherFX_Play(0, 1);
		}
		else
		{
			OtherFX_Play(5, 1);
		}
	}

	NativeModsPage_FixScroll(count);
}

void NativeModsPage_Open(void)
{
	s_nativeModsPageOpen = 1;
	s_nativeModsPageSub = 0;
	s_nativeModsPageRow = 0;
	s_nativeModsPageCursor = 0;
	s_nativeModsPageScroll = 0;
	Platform_Log("[CTR Menu] MODS: the page opens\n");
}

int NativeModsPage_IsOpen(void)
{
	return s_nativeModsPageOpen;
}

void NativeModsPage_Frame(void)
{
	int tapped;

	if (!s_nativeModsPageOpen)
	{
		return;
	}

	tapped = sdata->buttonTapPerPlayer[0] & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_CROSS_one | BTN_CIRCLE | BTN_TRIANGLE | BTN_SQUARE_one);

	if (tapped != 0)
	{
		if (s_nativeModsPageSub)
		{
			NativeModsPage_KeysSub(tapped);
		}
		else if (NativeModsPage_KeysMain(tapped))
		{
			RECTMENU_ClearInput();
			return;
		}

		RECTMENU_ClearInput();
	}

	if (s_nativeModsPageSub)
	{
		NativeModsPage_FixScroll(NativeMods_DriverCount());
		NativeModsPage_DrawSub();
	}
	else
	{
		NativeModsPage_DrawMain();
	}
}
