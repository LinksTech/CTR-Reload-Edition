#include <common.h>
#include <platform/native_mods.h>

// ===========================================================================
// MODS PAGE (the big view of the MODS box of the track select and cup select).
//
// Main page "MODS", below the separator line the small line "FEATURE PREVIEW"
// (white like the values, FONT_SMALL), then the rows in three groups, each
// under a small white heading, in English:
//   DRIVERS  CPU DRIVERS     DEFAULT / ALL RANDOM / ONLY SELECTED
//                            (grey while BOSS CHALLENGE is on: it sits out)
//            SELECT DRIVERS  <n> SELECTED   (grey unless CPU DRIVERS is ONLY
//                            SELECTED and no boss; no arrow: FONT_SMALL draws
//                            ">" as a dash)
//            DIFFICULTY      EASY / NORMAL / HARD / EXTREME
//            BOSS CHALLENGE  OFF / RANDOM / ROO / PAPU / JOE / PINSTRIPE / OXIDE
//   RULES    ITEMS           DEFAULT / CHAOS / CUSTOM
//            ITEM WEIGHTS    <k> OF <n> ON  (grey unless ITEMS is CUSTOM)
//            RETRO FUELED    OFF / ON
//            RETRO PARTS     <k> OF 4 ON  (grey unless RETRO FUELED is ON)
//   WORLD    GRAVITY         20 % .. 200 %, steps of 10
//            GRIP            0 % .. 100 %
//            NIGHT           0 % .. 90 %
// (the play mods, platform/native_mods.h). Fourteen lines do not fit the page:
// it shows NATIVE_MODS_PAGE_MAIN_VISIBLE of them and scrolls so that the
// cursor stays in view, with the heading of its group when it is the group's
// first row; a bar right of the rows shows where the view is.
//
// Keys: up/down choose the row (around the ends, headings are skipped),
// left/right the value (no wrap; a percentage to the next multiple of 10),
// cross opens SELECT DRIVERS / ITEM WEIGHTS / RETRO PARTS (only when not
// grey), takes a
// choice to the next one (around the end) and a percentage back to its
// normal value; a grey row takes no key; triangle goes back.
//
// Sub page "SELECT DRIVERS": at most six rows at a time, every driver - the
// fifteen retail ones first, then the custom ones - with ON / OFF; the list
// scrolls so the cursor stays visible. Left sets OFF, right sets ON, cross
// toggles, triangle goes back to the main page.
//
// Sub page "ITEM WEIGHTS": the same list of the race items (the items of the
// four race sets, NativeMods_RaceItems) with OFF / LOW / NORMAL / HIGH (the
// weights 0 / 1 / 2 / 4); left and right step, cross goes round. Below the
// list what the weights come to: every item OFF is DEFAULT, one item alone is
// the only item of every box.
//
// Sub page "RETRO FUELED": its four parts (SUPER PADS, LONG SUPER PADS, USF
// STACKING, KEEP RESERVES) with ON / OFF, keys as on SELECT DRIVERS; below
// them one line of what the part under the cursor does, and "ALL PARTS OFF:
// AS OFF" when none is on.
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
// ("BOSS CHALLENGE" and "SELECT DRIVERS", 182, 76 to 258) stays 35 left of
// its longest value ("47 SELECTED" at most - 15 retail and 32 roster files -,
// 143, from 293; "PINSTRIPE" 117); "CPU DRIVERS" (143, to 219) 48 left of
// "ONLY SELECTED" (169, from 267). A list name is cut to what ends
// NATIVE_MODS_PAGE_GAP left of the widest value, measured
// (DecalFont_GetLineWidth).
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

// The rows of the main page, in cursor order.
enum
{
	NATIVE_MODS_PAGE_ROW_CPU = 0,
	NATIVE_MODS_PAGE_ROW_DRIVERS,
	NATIVE_MODS_PAGE_ROW_DIFFICULTY,
	NATIVE_MODS_PAGE_ROW_BOSS,
	NATIVE_MODS_PAGE_ROW_ITEMS,
	NATIVE_MODS_PAGE_ROW_WEIGHTS,
	NATIVE_MODS_PAGE_ROW_RETRO,
	NATIVE_MODS_PAGE_ROW_PARTS,
	NATIVE_MODS_PAGE_ROW_GRAVITY,
	NATIVE_MODS_PAGE_ROW_GRIP,
	NATIVE_MODS_PAGE_ROW_NIGHT,
	NATIVE_MODS_PAGE_ROWS
};

// The sub pages.
#define NATIVE_MODS_PAGE_SUB_NONE    0
#define NATIVE_MODS_PAGE_SUB_DRIVERS 1
#define NATIVE_MODS_PAGE_SUB_WEIGHTS 2
#define NATIVE_MODS_PAGE_SUB_RETRO   3

#define NATIVE_MODS_PAGE_LINES        14
#define NATIVE_MODS_PAGE_MAIN_VISIBLE 7
#define NATIVE_MODS_PAGE_MAIN_ROW_Y   62
#define NATIVE_MODS_PAGE_MAIN_PITCH   15
#define NATIVE_MODS_PAGE_HEAD_X       66
#define NATIVE_MODS_PAGE_BAR_X        446
#define NATIVE_MODS_PAGE_VISIBLE      6
#define NATIVE_MODS_PAGE_NAME_MAX     40
#define NATIVE_MODS_PAGE_ROW_Y        58
#define NATIVE_MODS_PAGE_PREVIEW_Y    49
#define NATIVE_MODS_PAGE_ROW_PITCH    18
#define NATIVE_MODS_PAGE_LABEL_X      76
#define NATIVE_MODS_PAGE_VALUE_X      436
#define NATIVE_MODS_PAGE_GAP          16
#define NATIVE_MODS_PAGE_HINT_PITCH   10
#define NATIVE_MODS_PAGE_BACK_Y       20

global_variable int s_nativeModsPageOpen = 0;
global_variable int s_nativeModsPageSub = NATIVE_MODS_PAGE_SUB_NONE;
global_variable int s_nativeModsPageRow = 0;
global_variable int s_nativeModsPageMainScroll = 0;
global_variable int s_nativeModsPageCursor = 0;
global_variable int s_nativeModsPageScroll = 0;

// The lines of the main page: a heading (row -1) or a row.
global_variable const struct
{
	const char *text;
	int row;
} s_nativeModsPageLines[NATIVE_MODS_PAGE_LINES] = {
    {"DRIVERS", -1},
    {"CPU DRIVERS", NATIVE_MODS_PAGE_ROW_CPU},
    {"SELECT DRIVERS", NATIVE_MODS_PAGE_ROW_DRIVERS},
    {"DIFFICULTY", NATIVE_MODS_PAGE_ROW_DIFFICULTY},
    {"BOSS CHALLENGE", NATIVE_MODS_PAGE_ROW_BOSS},
    {"RULES", -1},
    {"ITEMS", NATIVE_MODS_PAGE_ROW_ITEMS},
    {"ITEM WEIGHTS", NATIVE_MODS_PAGE_ROW_WEIGHTS},
    {"RETRO FUELED", NATIVE_MODS_PAGE_ROW_RETRO},
    {"RETRO PARTS", NATIVE_MODS_PAGE_ROW_PARTS},
    {"WORLD", -1},
    {"GRAVITY", NATIVE_MODS_PAGE_ROW_GRAVITY},
    {"GRIP", NATIVE_MODS_PAGE_ROW_GRIP},
    {"NIGHT", NATIVE_MODS_PAGE_ROW_NIGHT},
};

global_variable const char *const s_nativeModsPageValues[3] = {"DEFAULT", "ALL RANDOM", "ONLY SELECTED"};
global_variable const char *const s_nativeModsPageLevels[4] = {"OFF", "LOW", "NORMAL", "HIGH"};

// The play mod of a row (native_mods.h), -1 for the CPU rows and the rows of
// a sub page.
internal int NativeModsPage_Mod(int row)
{
	switch (row)
	{
	case NATIVE_MODS_PAGE_ROW_DIFFICULTY:
		return NATIVE_MODS_DIFFICULTY;
	case NATIVE_MODS_PAGE_ROW_BOSS:
		return NATIVE_MODS_BOSS;
	case NATIVE_MODS_PAGE_ROW_ITEMS:
		return NATIVE_MODS_ITEMS;
	case NATIVE_MODS_PAGE_ROW_RETRO:
		return NATIVE_MODS_RETRO;
	case NATIVE_MODS_PAGE_ROW_GRAVITY:
		return NATIVE_MODS_GRAVITY;
	case NATIVE_MODS_PAGE_ROW_GRIP:
		return NATIVE_MODS_GRIP;
	case NATIVE_MODS_PAGE_ROW_NIGHT:
		return NATIVE_MODS_NIGHT;
	default:
		return -1;
	}
}

internal int NativeModsPage_Value(void)
{
	const int value = NativeMods_CpuDrivers();

	return ((value >= NATIVE_MODS_PAGE_DEFAULT) && (value <= NATIVE_MODS_PAGE_SELECTED)) ? value : NATIVE_MODS_PAGE_DEFAULT;
}

internal int NativeModsPage_Boss(void)
{
	return NativeMods_Value(NATIVE_MODS_BOSS) != NATIVE_MODS_BOSS_OFF;
}

// A grey row shows its value and takes no key.
internal int NativeModsPage_Grey(int row)
{
	switch (row)
	{
	case NATIVE_MODS_PAGE_ROW_CPU:
		return NativeModsPage_Boss();
	case NATIVE_MODS_PAGE_ROW_DRIVERS:
		return NativeModsPage_Boss() || (NativeModsPage_Value() != NATIVE_MODS_PAGE_SELECTED);
	case NATIVE_MODS_PAGE_ROW_WEIGHTS:
		return NativeMods_Value(NATIVE_MODS_ITEMS) != NATIVE_MODS_ITEMS_CUSTOM;
	case NATIVE_MODS_PAGE_ROW_PARTS:
		return NativeMods_Value(NATIVE_MODS_RETRO) == 0;
	default:
		return 0;
	}
}

// RETRO PARTS: how many are on.
internal int NativeModsPage_PartsOn(void)
{
	int on = 0;
	int k;

	for (k = 0; k < NATIVE_MODS_RETRO_PARTS; k++)
	{
		on += NativeMods_RetroPart(k);
	}

	return on;
}

// ITEM WEIGHTS: the race items, and how many of them are not OFF.
internal int NativeModsPage_Items(u8 items[NATIVE_MODS_ITEM_IDS], int *on)
{
	const int count = NativeMods_RaceItems(items);
	int k;

	*on = 0;
	for (k = 0; k < count; k++)
	{
		*on += (NativeMods_ItemLevel(items[k]) != NATIVE_MODS_WEIGHT_OFF);
	}

	return count;
}

// Separator line, the scroll bar of a list that does not fit, and background,
// the same calls and numbers as NativeGraphics_Draw. firstHintY: where the
// first hint stands; hintY: below the last hint drawn. The bar: its top and
// height, and the share in view (first line, lines shown, all lines); none
// when all of them are shown.
internal void NativeModsPage_DrawFrame(u32 *ot, int cursorY, int firstHintY, int hintY, int barY, int barH, int first, int shown, int all)
{
	Color color;

	if (cursorY >= 0)
	{
		RECT cursor = {.x = 74, .y = cursorY - 3, .w = 364, .h = 14};

		CTR_Box_DrawClearBox(&cursor, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot);
	}

	ColorCode_SetPacked(&color, sdata->battleSetup_Color_UI_1);

	// The thumb solid in the colour of the separator line, on a dark track.
	if ((barH > 0) && (all > shown))
	{
		RECT thumb = {.x = NATIVE_MODS_PAGE_BAR_X, .y = (s16)(barY + ((barH * first) / all)), .w = 3, .h = (s16)((barH * shown) / all)};
		RECT track = {.x = NATIVE_MODS_PAGE_BAR_X, .y = (s16)barY, .w = 3, .h = (s16)barH};
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
		RECT background = {.x = 56, .y = NATIVE_MODS_PAGE_BACK_Y, .w = 400, .h = bottom - NATIVE_MODS_PAGE_BACK_Y};

		RECTMENU_DrawInnerRect(&background, 4, ot);
	}
}

// The value a row shows.
internal const char *NativeModsPage_Shown(int row, char text[24])
{
	const int mod = NativeModsPage_Mod(row);

	if (mod >= 0)
	{
		return NativeMods_ValueText(mod);
	}

	if (row == NATIVE_MODS_PAGE_ROW_CPU)
	{
		return s_nativeModsPageValues[NativeModsPage_Value()];
	}

	if (row == NATIVE_MODS_PAGE_ROW_DRIVERS)
	{
		snprintf(text, 24, "%d SELECTED", NativeMods_SelectedCount());
		return text;
	}

	if (row == NATIVE_MODS_PAGE_ROW_PARTS)
	{
		snprintf(text, 24, "%d OF %d ON", NativeModsPage_PartsOn(), NATIVE_MODS_RETRO_PARTS);
		return text;
	}

	{
		u8 items[NATIVE_MODS_ITEM_IDS];
		int on;
		const int count = NativeModsPage_Items(items, &on);

		snprintf(text, 24, "%d OF %d ON", on, count);
		return text;
	}
}

internal int NativeModsPage_LineOfRow(int row)
{
	int line;

	for (line = 0; line < NATIVE_MODS_PAGE_LINES; line++)
	{
		if (s_nativeModsPageLines[line].row == row)
		{
			return line;
		}
	}

	return 0;
}

// The view of the main page: the cursor's line inside it, and the heading
// above it too when the cursor is on the first row of a group.
internal void NativeModsPage_FixMainScroll(void)
{
	const int line = NativeModsPage_LineOfRow(s_nativeModsPageRow);
	const int top = ((line > 0) && (s_nativeModsPageLines[line - 1].row < 0)) ? (line - 1) : line;

	if (top < s_nativeModsPageMainScroll)
	{
		s_nativeModsPageMainScroll = top;
	}
	if (line >= (s_nativeModsPageMainScroll + NATIVE_MODS_PAGE_MAIN_VISIBLE))
	{
		s_nativeModsPageMainScroll = line - (NATIVE_MODS_PAGE_MAIN_VISIBLE - 1);
	}
	if (s_nativeModsPageMainScroll > (NATIVE_MODS_PAGE_LINES - NATIVE_MODS_PAGE_MAIN_VISIBLE))
	{
		s_nativeModsPageMainScroll = NATIVE_MODS_PAGE_LINES - NATIVE_MODS_PAGE_MAIN_VISIBLE;
	}
	if (s_nativeModsPageMainScroll < 0)
	{
		s_nativeModsPageMainScroll = 0;
	}
}

internal void NativeModsPage_DrawMain(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	const int firstHintY = NATIVE_MODS_PAGE_MAIN_ROW_Y + (NATIVE_MODS_PAGE_MAIN_VISIBLE * NATIVE_MODS_PAGE_MAIN_PITCH) + 6;
	int hintY = firstHintY;
	int cursorY = -1;
	int i;

	DecalFont_DrawLine("MODS", 256, 26, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
	DecalFont_DrawLine("FEATURE PREVIEW", 256, NATIVE_MODS_PAGE_PREVIEW_Y, FONT_SMALL, (JUSTIFY_CENTER | WHITE));

	NativeModsPage_FixMainScroll();

	for (i = 0; i < NATIVE_MODS_PAGE_MAIN_VISIBLE; i++)
	{
		const int line = s_nativeModsPageMainScroll + i;
		const int row = s_nativeModsPageLines[line].row;
		const int y = NATIVE_MODS_PAGE_MAIN_ROW_Y + (i * NATIVE_MODS_PAGE_MAIN_PITCH);
		char text[24];
		int grey;

		if (row < 0)
		{
			DecalFont_DrawLine((char *)s_nativeModsPageLines[line].text, NATIVE_MODS_PAGE_HEAD_X, y, FONT_SMALL, WHITE);
			continue;
		}

		grey = NativeModsPage_Grey(row);
		DecalFont_DrawLine((char *)s_nativeModsPageLines[line].text, NATIVE_MODS_PAGE_LABEL_X, y, FONT_SMALL, grey ? GRAY : ORANGE);
		DecalFont_DrawLine((char *)NativeModsPage_Shown(row, text), NATIVE_MODS_PAGE_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | (grey ? GRAY : WHITE)));

		if (row == s_nativeModsPageRow)
		{
			cursorY = y;
		}
	}

	// BOSS CHALLENGE leaves the CPU seats to the boss (platform/native_mods.c).
	if (NativeModsPage_Boss())
	{
		DecalFont_DrawLine("ONLY YOU AND THE BOSS", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}
	// ONLY SELECTED with nothing ticked is DEFAULT.
	else if ((NativeModsPage_Value() == NATIVE_MODS_PAGE_SELECTED) && (NativeMods_SelectedCount() == 0))
	{
		DecalFont_DrawLine("NONE SELECTED: AS DEFAULT", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	// CUSTOM with every item OFF is DEFAULT.
	if (NativeMods_ItemsAllOff())
	{
		DecalFont_DrawLine("ALL ITEMS OFF: AS DEFAULT", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	// RETRO FUELED with every part OFF is OFF.
	if ((NativeMods_Value(NATIVE_MODS_RETRO) != 0) && (NativeModsPage_PartsOn() == 0))
	{
		DecalFont_DrawLine("ALL PARTS OFF: AS OFF", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	if (Platform_SettingsLocked())
	{
		DecalFont_DrawLine("NOT SAVED IN THIS RUN", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	NativeModsPage_DrawFrame(ot, cursorY, firstHintY, hintY, NATIVE_MODS_PAGE_MAIN_ROW_Y - 3, NATIVE_MODS_PAGE_MAIN_VISIBLE * NATIVE_MODS_PAGE_MAIN_PITCH,
	                         s_nativeModsPageMainScroll, NATIVE_MODS_PAGE_MAIN_VISIBLE, NATIVE_MODS_PAGE_LINES);
}

// The name of a list row, cut until it ends NATIVE_MODS_PAGE_GAP left of the
// widest value (OFF on SELECT DRIVERS and RETRO FUELED, NORMAL on ITEM
// WEIGHTS): never into the value.
internal void NativeModsPage_FitName(const char *source, const char *widest, char name[NATIVE_MODS_PAGE_NAME_MAX + 1])
{
	const int room = NATIVE_MODS_PAGE_VALUE_X - DecalFont_GetLineWidth((char *)widest, FONT_SMALL) - NATIVE_MODS_PAGE_GAP - NATIVE_MODS_PAGE_LABEL_X;
	size_t length;

	strncpy(name, (source != NULL) ? source : "", NATIVE_MODS_PAGE_NAME_MAX);
	name[NATIVE_MODS_PAGE_NAME_MAX] = '\0';

	length = strlen(name);
	while ((length > 0) && (DecalFont_GetLineWidth(name, FONT_SMALL) > room))
	{
		name[--length] = '\0';
	}
}

// The count of the open list.
internal int NativeModsPage_SubCount(void)
{
	if (s_nativeModsPageSub == NATIVE_MODS_PAGE_SUB_WEIGHTS)
	{
		u8 items[NATIVE_MODS_ITEM_IDS];
		int on;

		return NativeModsPage_Items(items, &on);
	}

	if (s_nativeModsPageSub == NATIVE_MODS_PAGE_SUB_RETRO)
	{
		return NATIVE_MODS_RETRO_PARTS;
	}

	return NativeMods_DriverCount();
}

internal void NativeModsPage_DrawSub(void)
{
	struct GameTracker *gGT = sdata->gGT;
	u32 *ot = gGT->backBuffer->otMem.uiOT;
	const int weights = (s_nativeModsPageSub == NATIVE_MODS_PAGE_SUB_WEIGHTS);
	const int retro = (s_nativeModsPageSub == NATIVE_MODS_PAGE_SUB_RETRO);
	u8 items[NATIVE_MODS_ITEM_IDS];
	int on = 0;
	const int count = NativeModsPage_SubCount();
	const int firstHintY = NATIVE_MODS_PAGE_ROW_Y + (NATIVE_MODS_PAGE_VISIBLE * NATIVE_MODS_PAGE_ROW_PITCH) + 8;
	int hintY = firstHintY;
	int last = s_nativeModsPageScroll + NATIVE_MODS_PAGE_VISIBLE;
	int i;

	if (weights)
	{
		NativeModsPage_Items(items, &on);
	}

	DecalFont_DrawLine(weights ? "ITEM WEIGHTS" : (retro ? "RETRO FUELED" : "SELECT DRIVERS"), 256, 26, FONT_BIG, (JUSTIFY_CENTER | ORANGE));

	if (last > count)
	{
		last = count;
	}

	for (i = s_nativeModsPageScroll; i < last; i++)
	{
		const int y = NATIVE_MODS_PAGE_ROW_Y + ((i - s_nativeModsPageScroll) * NATIVE_MODS_PAGE_ROW_PITCH);
		char name[NATIVE_MODS_PAGE_NAME_MAX + 1];

		if (weights)
		{
			NativeModsPage_FitName(NativeMods_ItemName(items[i]), "NORMAL", name);
			DecalFont_DrawLine(name, NATIVE_MODS_PAGE_LABEL_X, y, FONT_SMALL, ORANGE);
			DecalFont_DrawLine((char *)s_nativeModsPageLevels[NativeMods_ItemLevel(items[i])], NATIVE_MODS_PAGE_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | WHITE));
		}
		else if (retro)
		{
			NativeModsPage_FitName(NativeMods_RetroPartName(i), "OFF", name);
			DecalFont_DrawLine(name, NATIVE_MODS_PAGE_LABEL_X, y, FONT_SMALL, ORANGE);
			DecalFont_DrawLine(NativeMods_RetroPart(i) ? "ON" : "OFF", NATIVE_MODS_PAGE_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | WHITE));
		}
		else
		{
			NativeModsPage_FitName(NativeMods_DriverName(i), "OFF", name);
			DecalFont_DrawLine(name, NATIVE_MODS_PAGE_LABEL_X, y, FONT_SMALL, ORANGE);
			DecalFont_DrawLine(NativeMods_DriverSelected(i) ? "ON" : "OFF", NATIVE_MODS_PAGE_VALUE_X, y, FONT_SMALL, (JUSTIFY_RIGHT | WHITE));
		}
	}

	if (count > NATIVE_MODS_PAGE_VISIBLE)
	{
		char text[24];

		snprintf(text, sizeof(text), "%d-%d OF %d", s_nativeModsPageScroll + 1, last, count);
		DecalFont_DrawLine(text, 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	// What the part under the cursor does; every part OFF is OFF.
	if (retro)
	{
		DecalFont_DrawLine((char *)NativeMods_RetroPartHint(s_nativeModsPageCursor), 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
		if (NativeModsPage_PartsOn() == 0)
		{
			DecalFont_DrawLine("ALL PARTS OFF: AS OFF", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
			hintY += NATIVE_MODS_PAGE_HINT_PITCH;
		}
	}

	// What the weights come to (platform/native_mods.c).
	if (weights && (on == 0))
	{
		DecalFont_DrawLine("ALL OFF: AS DEFAULT", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}
	else if (weights && (on == 1))
	{
		char text[40];
		int alone = 0;

		for (i = 0; i < count; i++)
		{
			if (NativeMods_ItemLevel(items[i]) != NATIVE_MODS_WEIGHT_OFF)
			{
				alone = i;
			}
		}
		snprintf(text, sizeof(text), "EVERY BOX: %s", NativeMods_ItemName(items[alone]));
		DecalFont_DrawLine(text, 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	if (Platform_SettingsLocked())
	{
		DecalFont_DrawLine("NOT SAVED IN THIS RUN", 256, hintY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
		hintY += NATIVE_MODS_PAGE_HINT_PITCH;
	}

	NativeModsPage_DrawFrame(ot, NATIVE_MODS_PAGE_ROW_Y + ((s_nativeModsPageCursor - s_nativeModsPageScroll) * NATIVE_MODS_PAGE_ROW_PITCH), firstHintY,
	                         hintY, 0, 0, 0, 0, 0);
}

// Left (-1) or right (+1) on a row; the value does not run past its ends. 0
// if nothing changes.
internal int NativeModsPage_Change(int row, int dir)
{
	const int mod = NativeModsPage_Mod(row);
	const int value = NativeModsPage_Value() + dir;

	if (NativeModsPage_Grey(row))
	{
		return 0;
	}

	if (mod >= 0)
	{
		return NativeMods_Step(mod, dir);
	}

	if ((row != NATIVE_MODS_PAGE_ROW_CPU) || (value < NATIVE_MODS_PAGE_DEFAULT) || (value > NATIVE_MODS_PAGE_SELECTED))
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

internal void NativeModsPage_OpenSub(int sub)
{
	s_nativeModsPageSub = sub;
	s_nativeModsPageCursor = 0;
	s_nativeModsPageScroll = 0;
	OtherFX_Play(1, 1);
	Platform_Log("[CTR Menu] MODS: %s\n",
	             (sub == NATIVE_MODS_PAGE_SUB_WEIGHTS) ? "ITEM WEIGHTS" : ((sub == NATIVE_MODS_PAGE_SUB_RETRO) ? "RETRO FUELED" : "SELECT DRIVERS"));
}

// Cross on a row: a sub page opens (2), a choice goes to its next value
// (around the end), a percentage back to its normal value (1); 0 if nothing
// happens.
internal int NativeModsPage_Cross(int row)
{
	const int mod = NativeModsPage_Mod(row);

	if (NativeModsPage_Grey(row))
	{
		return 0;
	}

	if (row == NATIVE_MODS_PAGE_ROW_DRIVERS)
	{
		NativeModsPage_OpenSub(NATIVE_MODS_PAGE_SUB_DRIVERS);
		return 2;
	}

	if (row == NATIVE_MODS_PAGE_ROW_WEIGHTS)
	{
		NativeModsPage_OpenSub(NATIVE_MODS_PAGE_SUB_WEIGHTS);
		return 2;
	}

	if (row == NATIVE_MODS_PAGE_ROW_PARTS)
	{
		NativeModsPage_OpenSub(NATIVE_MODS_PAGE_SUB_RETRO);
		return 2;
	}

	if (row == NATIVE_MODS_PAGE_ROW_CPU)
	{
		NativeMods_SetCpuDrivers((NativeModsPage_Value() + 1) % (NATIVE_MODS_PAGE_SELECTED + 1));
		return 1;
	}

	if ((mod == NATIVE_MODS_GRAVITY) || (mod == NATIVE_MODS_GRIP) || (mod == NATIVE_MODS_NIGHT))
	{
		if (NativeMods_Value(mod) == NativeMods_NormalValue(mod))
		{
			return 0;
		}
		NativeMods_SetValue(mod, NativeMods_NormalValue(mod));
		return 1;
	}

	if (!NativeMods_Step(mod, 1))
	{
		NativeMods_SetValue(mod, 0);
	}
	return 1;
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
		const int done = NativeModsPage_Cross(s_nativeModsPageRow);

		if (done != 2)
		{
			OtherFX_Play(done ? 0 : 5, 1);
		}
	}
	else
	{
		OtherFX_Play(2, 1);
		Platform_Log("[CTR Menu] MODS: the page closes\n");
		s_nativeModsPageOpen = 0;
		return 1;
	}

	NativeModsPage_FixMainScroll();
	return 0;
}

// ITEM WEIGHTS: left and right step without wrap, cross goes round.
internal void NativeModsPage_KeysWeight(int tapped)
{
	u8 items[NATIVE_MODS_ITEM_IDS];
	int on;
	int item;
	int level;
	int next;

	NativeModsPage_Items(items, &on);
	item = items[s_nativeModsPageCursor];
	level = NativeMods_ItemLevel(item);

	if ((tapped & BTN_LEFT) != 0)
	{
		next = (level > NATIVE_MODS_WEIGHT_OFF) ? (level - 1) : level;
	}
	else if ((tapped & BTN_RIGHT) != 0)
	{
		next = (level < NATIVE_MODS_WEIGHT_HIGH) ? (level + 1) : level;
	}
	else
	{
		next = (level + 1) % (NATIVE_MODS_WEIGHT_HIGH + 1);
	}

	if (next != level)
	{
		NativeMods_SetItemLevel(item, next);
		OtherFX_Play(0, 1);
	}
	else
	{
		OtherFX_Play(5, 1);
	}
}

internal void NativeModsPage_KeysSub(int tapped)
{
	const int weights = (s_nativeModsPageSub == NATIVE_MODS_PAGE_SUB_WEIGHTS);
	const int retro = (s_nativeModsPageSub == NATIVE_MODS_PAGE_SUB_RETRO);
	const int count = NativeModsPage_SubCount();

	if ((tapped & (BTN_TRIANGLE | BTN_SQUARE_one)) != 0)
	{
		s_nativeModsPageRow = weights ? NATIVE_MODS_PAGE_ROW_WEIGHTS : (retro ? NATIVE_MODS_PAGE_ROW_PARTS : NATIVE_MODS_PAGE_ROW_DRIVERS);
		s_nativeModsPageSub = NATIVE_MODS_PAGE_SUB_NONE;
		NativeModsPage_FixMainScroll();
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
	else if (weights)
	{
		NativeModsPage_KeysWeight(tapped);
	}
	else
	{
		const int on = (retro ? NativeMods_RetroPart(s_nativeModsPageCursor) : NativeMods_DriverSelected(s_nativeModsPageCursor)) ? 1 : 0;
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

		if ((next != on) && retro)
		{
			NativeMods_SetRetroPart(s_nativeModsPageCursor, next);
			OtherFX_Play(0, 1);
		}
		else if (next != on)
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
	s_nativeModsPageSub = NATIVE_MODS_PAGE_SUB_NONE;
	s_nativeModsPageRow = 0;
	s_nativeModsPageMainScroll = 0;
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
		if (s_nativeModsPageSub != NATIVE_MODS_PAGE_SUB_NONE)
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

	if (s_nativeModsPageSub != NATIVE_MODS_PAGE_SUB_NONE)
	{
		NativeModsPage_FixScroll(NativeModsPage_SubCount());
		NativeModsPage_DrawSub();
	}
	else
	{
		NativeModsPage_DrawMain();
	}
}
