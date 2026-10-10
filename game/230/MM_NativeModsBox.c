#include <common.h>

#ifdef CTR_NATIVE

// THE MODS BOX OF THE ARCADE TRACK SELECT AND CUP SELECT (one player, on the
// disc and in NITRO-PIT - NativeMods_MenuOffered, platform/native_mods.c).
//
// NO COPY OF A RETAIL PROC. The retail screens keep their retail proc, which
// is called unchanged from a proc of this file: MM_NativeModsBox_Hook swaps
// the proc pointer every frame between exactly these pairs -
// MM_TrackSelect_MenuProc <-> MM_NativeModsBox_TrackProc and, for both cup
// screens, MM_CupSelect_MenuProc / MM_NativeCupSelect_MenuProc <->
// MM_NativeModsBox_CupProc (which calls the one it took the place of). The
// NITRO-PIT hooks swap only from an original, so MM_NativeModsBox_Unhook puts
// the originals back before them and MM_NativeModsBox_Hook comes after them
// (NativeMenuLock_Tick). Not offered: the original stands, nothing here is
// drawn. The track screen of NITRO-PIT is a copy of its own
// (MM_NativeTrackSelect.c), and its pointer is what the widescreen rows ask
// for (NativeUiDecl_CustomTrackSelect) - so it is never swapped: the copy
// calls the box itself ([K9] there).
//
// THE TRACK SELECT. The MODS box stands directly below the lap box
// (MM_NativeTrackSelect.c, block THE TIME TRIAL AND THE MODS BOX): the lap
// box's frame, font, width (MM_TRACK_SELECT_LAP_MENU_WIDTH)
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
// NITRO RACE: the same box in the same place, below the lap box, with the
// same keys - the copy of the track select calls it ([K9] there).
//
// THE CUP SELECT has no free place for a box at 4:3: the four cups and their
// shadows reach down to row 204 of 216. The MODS entry there is one line of
// FONT_SMALL, "MODS: " and the status, centred below the cups in the free
// strip; DOWN on a cup of the lower row moves the cursor onto it (a row whose
// DOWN stays on itself: RECTMENU leaves that key silent there,
// D230.rowsCupSelect and the rows of NITRO CUP alike), it then gets the
// pulsing bar of the cups, UP or TRIANGLE/SQUARE gives the cursor back to
// that cup, CROSS (or CIRCLE) opens the MODS page.
//
// THE MODS PAGE (game/native_mods_page.c) stands alone while it is open, like
// the GRAPHICS page: the screen's proc is not called, the page reads the keys.
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

// The cup proc the box stands in for: MM_CupSelect_MenuProc or, in NITRO CUP,
// MM_NativeCupSelect_MenuProc.
global_variable void (*s_nativeModsCupInner)(struct RectMenu *) = MM_CupSelect_MenuProc;

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

// The cursor in the MODS entry: the way out (UP below the screen's place,
// DOWN above it) and back go out (1), CROSS opens the page, every other key
// is swallowed. The input is cleared in every case, so the screen's proc and
// RECTMENU see nothing of it.
internal int MM_NativeModsBox_EntryKeys(u32 wayOut)
{
	const u32 tapped = MM_NativeModsBox_Tapped();
	int out = 0;

	if ((tapped & (BTN_CROSS_one | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		NativeModsPage_Open();
	}
	else if ((tapped & wayOut) != 0)
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

// The page while it is open, in place of the screen's proc. 1 when it ran.
int MM_NativeModsBox_PageFrame(void)
{
	if (!NativeModsPage_IsOpen())
	{
		return 0;
	}

	NativeModsPage_Frame();
	RECTMENU_ClearInput();
	return 1;
}

// ---------------------------------------------------------------------------
// THE TRACK SELECT
// ---------------------------------------------------------------------------

// Below the lap box (MM_NativeTrackSelect.c, block THE TIME TRIAL AND THE MODS
// BOX: frame y 173..201, shadow to 207).
internal void MM_NativeModsBox_DrawTrackBox(int focused)
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

	MM_NativeTrackSelect_DrawBox(&s_nativeModsMenu, focused);
}

void MM_NativeModsBox_TrackProc(struct RectMenu *menu)
{
	Color pulse;

	if (MM_NativeModsBox_PageFrame())
	{
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
			if (MM_NativeModsBox_EntryKeys(BTN_UP))
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
		MM_NativeModsBox_DrawTrackBox(s_nativeModsFocus);
	}
}

// ---------------------------------------------------------------------------
// NITRO RACE (called by the copy of the track select, MM_NativeTrackSelect.c)
// ---------------------------------------------------------------------------

int MM_NativeModsBox_NitroOffered(void)
{
	return NativeMods_MenuOffered();
}

// The cursor in the box: 1 when it goes back to the last lap row.
int MM_NativeModsBox_NitroKeys(void)
{
	return MM_NativeModsBox_EntryKeys(BTN_UP);
}

void MM_NativeModsBox_DrawNitroBox(int focused)
{
	MM_NativeModsBox_DrawTrackBox(focused);
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

// A cup of the lower row: its DOWN stays on itself.
internal int MM_NativeModsBox_LowerCup(const struct RectMenu *menu)
{
	const s16 row = menu->rowSelected;

	return (row >= 0) && (menu->rows[row].rowOnPressDown == (char)row);
}

void MM_NativeModsBox_CupProc(struct RectMenu *menu)
{
	void (*inner)(struct RectMenu *) = s_nativeModsCupInner;
	Color pulse;

	// A cup was confirmed (RECTMENU_ProcessInput): the way out of the screen.
	if (menu->funcState == RECTMENU_FUNC_STATE_INPUT)
	{
		inner(menu);
		return;
	}

	if (MM_NativeModsBox_PageFrame())
	{
		return;
	}

	if (D230.cupSelectTransition.state != IN_MENU)
	{
		s_nativeModsFocus = 0;
	}
	else if (s_nativeModsFocus)
	{
		if (MM_NativeModsBox_EntryKeys(BTN_UP))
		{
			s_nativeModsFocus = 0;
			menu->rowSelected = s_nativeModsCupRow;
		}
	}
	else if ((MM_NativeModsBox_Tapped() == BTN_DOWN) && MM_NativeModsBox_LowerCup(menu))
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
		inner(menu);
	}
	else
	{
		pulse = sdata->menuRowHighlight_Normal;
		ColorCode_SetPacked(&sdata->menuRowHighlight_Normal, MM_NATIVE_MODE_STILL_MARK);
		inner(menu);
		sdata->menuRowHighlight_Normal = pulse;
	}

	if (D230.cupSelectTransition.state == IN_MENU)
	{
		MM_NativeModsBox_DrawCupLine();
	}
}

// ---------------------------------------------------------------------------
// THE HOOK, every frame around the NITRO-PIT hooks (NativeMenuLock_Tick):
// Unhook before them puts the originals back, Hook after them takes the place
// of an original where the box is offered.
// ---------------------------------------------------------------------------
void MM_NativeModsBox_Unhook(void)
{
	if (D230.menuTrackSelect.funcPtr == MM_NativeModsBox_TrackProc)
	{
		D230.menuTrackSelect.funcPtr = MM_TrackSelect_MenuProc;
	}

	if (D230.menuCupSelect.funcPtr == MM_NativeModsBox_CupProc)
	{
		D230.menuCupSelect.funcPtr = s_nativeModsCupInner;
	}
}

void MM_NativeModsBox_Hook(void)
{
	if (!NativeMods_MenuOffered())
	{
		return;
	}

	if (D230.menuTrackSelect.funcPtr == MM_TrackSelect_MenuProc)
	{
		D230.menuTrackSelect.funcPtr = MM_NativeModsBox_TrackProc;
	}

	if ((D230.menuCupSelect.funcPtr == MM_CupSelect_MenuProc) || (D230.menuCupSelect.funcPtr == MM_NativeCupSelect_MenuProc))
	{
		s_nativeModsCupInner = D230.menuCupSelect.funcPtr;
		D230.menuCupSelect.funcPtr = MM_NativeModsBox_CupProc;
	}
}

#endif
