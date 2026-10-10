#include <common.h>

#ifdef CTR_NATIVE

// THE MODS BOX OF THE ARCADE TRACK SELECT AND CUP SELECT (one player, disc
// tracks - NativeMods_MenuOffered, platform/native_mods.c).
//
// NO COPY OF A RETAIL PROC: both screens keep their retail proc, which is
// called unchanged from a proc of this file. MM_NativeModsBox_Hook (from
// NativeMenuLock_Tick, before the NITRO-PIT hooks) swaps the proc pointer
// every frame between exactly these pairs, as the NITRO-PIT copies do:
// MM_TrackSelect_MenuProc <-> MM_NativeModsBox_TrackProc and
// MM_CupSelect_MenuProc <-> MM_NativeModsBox_CupProc. Not offered (NITRO-PIT,
// two or more players, time trial, battle, adventure): the retail proc stands,
// and nothing here is drawn.
//
// THE TRACK SELECT. The MODS box stands directly below the lap box, in the
// place and the style of the MODE box of NITRO RACE (MM_NativeTrackSelect.c,
// block THE MODE BOX): same frame, font, width (MM_TRACK_SELECT_LAP_MENU_WIDTH)
// and transition, title MODS, one row with the status line
// (NativeMods_StatusText). It is there while the lap box is open. DOWN on the
// last lap row moves the cursor into it (the row RECTMENU leaves silent,
// D230.rowsLapSel), UP or TRIANGLE/SQUARE moves it back to that lap row, CROSS
// (or CIRCLE) opens the MODS page. While the cursor is in the MODS box the
// keys never reach the retail proc, and its highlight is held still for that
// call (MM_NATIVE_MODE_STILL_MARK), as the box without the cursor shows it.
// As long as the cursor never enters the box, the track select runs as before:
// the step is taken only with the keys RECTMENU itself would read
// (MM_NativeTrackSelect_ModeStep).
//
// THE CUP SELECT has no free place for a box at 4:3: the four cups and their
// shadows reach down to row 204 of 216. The MODS entry there is one line of
// FONT_SMALL, "MODS: " and the status, centred below the cups in the free
// strip; DOWN on a cup of the lower row moves the cursor onto it (RECTMENU
// leaves that key silent there, D230.rowsCupSelect), it then gets the pulsing
// bar of the cups, UP or TRIANGLE/SQUARE gives the cursor back to that cup,
// CROSS (or CIRCLE) opens the MODS page.
//
// THE MODS PAGE (game/native_mods_page.c) stands alone while it is open, like
// the GRAPHICS page: the retail proc is not called, the page reads the keys.
// Closed with TRIANGLE/SQUARE, the cursor is in the MODS entry again and the
// status line says what was chosen.

const char *NativeMods_StatusText(void); // platform/native_mods.c
int NativeMods_MenuOffered(void);
void NativeModsPage_Open(void); // game/native_mods_page.c
int NativeModsPage_IsOpen(void);
void NativeModsPage_Frame(void);

#define MM_NATIVE_MODS_CUP_LINE_Y 206
#define MM_NATIVE_MODS_CUP_BAR_PAD 6

global_variable struct MenuRow s_nativeModsRows[2] = {
    {.stringIndex = MM_NATIVE_LNG_MODS_STATUS, .rowOnPressUp = 0, .rowOnPressDown = 0, .rowOnPressLeft = 0, .rowOnPressRight = 0},
    {.stringIndex = RECTMENU_STRING_NONE},
};

global_variable struct RectMenu s_nativeModsMenu = {
    .stringIndexTitle = MM_NATIVE_LNG_MODS,
    .state = USE_SMALL_FONT | CENTER_ON_X,
    .rows = &s_nativeModsRows[0],
};

// The cursor is in the MODS entry (1) or in the screen's own place (0).
global_variable int s_nativeModsFocus = 0;

// The cup the cursor came from.
global_variable s16 s_nativeModsCupRow = 2;

global_variable char s_nativeModsCupLine[24];

// The keys the step looks at, as RECTMENU reads them: player one, no L1/R1
// held, one direction or one confirm or back key. 0 for anything else.
internal u32 MM_NativeModsBox_Tapped(void)
{
	if ((sdata->buttonHeldPerPlayer[0] & (BTN_L1 | BTN_R1)) != 0)
	{
		return 0;
	}

	return sdata->buttonTapPerPlayer[0] & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_CROSS_one | BTN_CIRCLE | BTN_TRIANGLE | BTN_SQUARE_one);
}

// The cursor in the MODS entry: UP and back go out (1), CROSS opens the page,
// every other key is swallowed. The input is cleared in every case, so the
// retail proc and RECTMENU see nothing of it.
internal int MM_NativeModsBox_EntryKeys(void)
{
	const u32 tapped = MM_NativeModsBox_Tapped();
	int out = 0;

	if ((tapped & (BTN_CROSS_one | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		NativeModsPage_Open();
	}
	else if ((tapped & BTN_UP) != 0)
	{
		OtherFX_Play(0, 1);
		out = 1;
	}
	else if ((tapped & (BTN_TRIANGLE | BTN_SQUARE_one)) != 0)
	{
		OtherFX_Play(2, 1);
		out = 1;
	}

	RECTMENU_ClearInput();
	return out;
}

// ---------------------------------------------------------------------------
// THE TRACK SELECT
// ---------------------------------------------------------------------------

// Below the lap box, as MM_NativeTrackSelect_DrawModeBox places the MODE box.
internal void MM_NativeModsBox_DrawTrackBox(void)
{
	const struct RectMenuStyle *style = &g_rectMenuStyleRetail;
	const struct RectMenu *laps = &D230.menuLapSel;
	s16 lapHeight = 0;
	int lapFrameBottom;

	RECTMENU_GetHeight(&D230.menuLapSel, &lapHeight, 0);
	lapFrameBottom = (int)laps->posY_curr - style->frameOffsetY + lapHeight + style->frameExtraH - (int)((laps->state & 0xff) >> 7);

	s_nativeModsMenu.posX_curr = laps->posX_curr;
	s_nativeModsMenu.posY_curr = (u16)(lapFrameBottom + style->shadowHWide + MM_NATIVE_MODE_AIR + style->frameOffsetY);
	s_nativeModsMenu.rowSelected = 0;

	MM_NativeTrackSelect_DrawBox(&s_nativeModsMenu, s_nativeModsFocus);
}

void MM_NativeModsBox_TrackProc(struct RectMenu *menu)
{
	Color pulse;

	if (NativeModsPage_IsOpen())
	{
		NativeModsPage_Frame();
		RECTMENU_ClearInput();
		return;
	}

	// A closed lap box has no MODS box; the next one opens on LAPS.
	if (D230.trackSelect.lapBoxOpen == 0)
	{
		s_nativeModsFocus = 0;
	}
	else if (D230.trackSelect.transition.state == IN_MENU)
	{
		if (s_nativeModsFocus)
		{
			if (MM_NativeModsBox_EntryKeys())
			{
				// Back onto the last lap row, the row the cursor came from.
				s_nativeModsFocus = 0;
				sdata->uselessLapRowCopy = MM_NativeTrackSelect_LastRow(&D230.menuLapSel);
			}
		}
		else if ((sdata->activeSubMenu == &D230.menuLapSel) && (MM_NativeModsBox_Tapped() == BTN_DOWN) &&
		         (sdata->uselessLapRowCopy == MM_NativeTrackSelect_LastRow(&D230.menuLapSel)))
		{
			s_nativeModsFocus = 1;
			OtherFX_Play(0, 1);
			RECTMENU_ClearInput();
		}
	}

	if (NativeModsPage_IsOpen())
	{
		return;
	}

	if (!s_nativeModsFocus)
	{
		MM_TrackSelect_MenuProc(menu);
	}
	else
	{
		pulse = sdata->menuRowHighlight_Normal;
		ColorCode_SetPacked(&sdata->menuRowHighlight_Normal, MM_NATIVE_MODE_STILL_MARK);
		MM_TrackSelect_MenuProc(menu);
		sdata->menuRowHighlight_Normal = pulse;
	}

	if (D230.trackSelect.lapBoxOpen != 0)
	{
		MM_NativeModsBox_DrawTrackBox();
	}
}

// ---------------------------------------------------------------------------
// THE CUP SELECT
// ---------------------------------------------------------------------------

// The line below the cups; with the cursor the bar of the cups behind it.
internal void MM_NativeModsBox_DrawCupLine(void)
{
	struct GameTracker *gGT = sdata->gGT;
	int width;

	snprintf(s_nativeModsCupLine, sizeof(s_nativeModsCupLine), "MODS: %s", NativeMods_StatusText());
	width = DecalFont_GetLineWidth(s_nativeModsCupLine, FONT_SMALL);

	DecalFont_DrawLine(s_nativeModsCupLine, 256, MM_NATIVE_MODS_CUP_LINE_Y, FONT_SMALL, MM_CUP_SELECT_TEXT_COLOR);

	if (s_nativeModsFocus)
	{
		RECT bar = {.x = (s16)(256 - (width / 2) - MM_NATIVE_MODS_CUP_BAR_PAD), .y = MM_NATIVE_MODS_CUP_LINE_Y - 2,
		            .w = (s16)(width + (2 * MM_NATIVE_MODS_CUP_BAR_PAD)), .h = 12};

		CTR_Box_DrawClearBox(&bar, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, gGT->backBuffer->otMem.uiOT);
	}
}

void MM_NativeModsBox_CupProc(struct RectMenu *menu)
{
	Color pulse;

	// A cup was confirmed (RECTMENU_ProcessInput): the retail way out.
	if (menu->funcState == RECTMENU_FUNC_STATE_INPUT)
	{
		MM_CupSelect_MenuProc(menu);
		return;
	}

	if (NativeModsPage_IsOpen())
	{
		NativeModsPage_Frame();
		RECTMENU_ClearInput();
		return;
	}

	if (D230.cupSelectTransition.state != IN_MENU)
	{
		s_nativeModsFocus = 0;
	}
	else if (s_nativeModsFocus)
	{
		if (MM_NativeModsBox_EntryKeys())
		{
			s_nativeModsFocus = 0;
			menu->rowSelected = s_nativeModsCupRow;
		}
	}
	else if ((MM_NativeModsBox_Tapped() == BTN_DOWN) && ((menu->rowSelected == 2) || (menu->rowSelected == 3)))
	{
		s_nativeModsFocus = 1;
		s_nativeModsCupRow = menu->rowSelected;
		OtherFX_Play(0, 1);
		RECTMENU_ClearInput();
	}

	if (NativeModsPage_IsOpen())
	{
		return;
	}

	if (!s_nativeModsFocus)
	{
		MM_CupSelect_MenuProc(menu);
	}
	else
	{
		pulse = sdata->menuRowHighlight_Normal;
		ColorCode_SetPacked(&sdata->menuRowHighlight_Normal, MM_NATIVE_MODE_STILL_MARK);
		MM_CupSelect_MenuProc(menu);
		sdata->menuRowHighlight_Normal = pulse;
	}

	if (D230.cupSelectTransition.state == IN_MENU)
	{
		MM_NativeModsBox_DrawCupLine();
	}
}

// ---------------------------------------------------------------------------
// THE HOOK, every frame before the NITRO-PIT hooks: they swap only from the
// retail proc, so ours goes back to retail first wherever the box is not
// offered.
// ---------------------------------------------------------------------------
void MM_NativeModsBox_Hook(void)
{
	const int offered = NativeMods_MenuOffered();

	if (offered && (D230.menuTrackSelect.funcPtr == MM_TrackSelect_MenuProc))
	{
		D230.menuTrackSelect.funcPtr = MM_NativeModsBox_TrackProc;
	}
	else if (!offered && (D230.menuTrackSelect.funcPtr == MM_NativeModsBox_TrackProc))
	{
		D230.menuTrackSelect.funcPtr = MM_TrackSelect_MenuProc;
	}

	if (offered && (D230.menuCupSelect.funcPtr == MM_CupSelect_MenuProc))
	{
		D230.menuCupSelect.funcPtr = MM_NativeModsBox_CupProc;
	}
	else if (!offered && (D230.menuCupSelect.funcPtr == MM_NativeModsBox_CupProc))
	{
		D230.menuCupSelect.funcPtr = MM_CupSelect_MenuProc;
	}
}

#endif
