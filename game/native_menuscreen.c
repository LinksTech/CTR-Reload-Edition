#include <common.h>

#include <platform/native_chars.h>

#ifdef CTR_NATIVE

// MENU SCREENS THAT MOVE THEMSELVES.
//
// Two screens move something in the wide picture that the UI mapper cannot
// anchor correctly from the geometry of a frame alone: the driver select
// its 3D windows, which lie in the world push buffer, and the high score its
// pages, which glide over the whole picture when paging. Both are computed here from
// what the menu itself stores in D230; nothing is
// written, and the menu files stay untouched. This file stands in
// game_unity.h behind the menu overlay, so that it can use its constants
// instead of copying them.

// THE DRIVER SELECT: 3D WINDOW, FRAME AND NAME ARE ONE ELEMENT.
//
// MM_Characters_DrawWindows (MM_Characters.c:193-259) writes each driver his
// window into pushBuffer[i].rect, in the 512 columns in which the menus are
// authored. The window however is a WORLD push buffer, and since the wide
// canvas that one is in canvas columns (native_view.c:595-618). The 3D picture
// therefore stays at its 4:3 position, while the UI mapper moves the
// frame, which lies in the UI table, to an anchor - measured in
// all four setups.
//
// Here it is decided ONCE per window where it belongs: the edge rule of the
// menus (native_view.c, CTR_UI_AnchorForBoxEdge) applied to the frame including
// drop shadow, in its position in THIS frame. The same shift is given to
// the draw area of the 3D window (PushBuffer_SetDrawEnv_Normal) and to every
// UI primitive of the frame and of the name (NativeGpu_ApplyUIMapping). Because both
// places ask the same function, they cannot drift apart.

// The drop shadow of the frame (RECTMENU_DrawInnerRect type 9,
// MM_Characters.c:1214) sticks out on the right and at the bottom: measured 77..435 x 30..128
// for the window 77..423 x 30..122. It belongs to the
// element, also for the edge distance.
#define NATIVE_MENU_WINDOW_SHADOW_X 12
#define NATIVE_MENU_WINDOW_SHADOW_Y 6

// How far around the frame a UI primitive still counts as part of the window. The
// outline (RECTMENU_DrawOuterRect_HighLevel) and the inner selection frames
// lie on the rectangle or inside it; four points cover the line width.
#define NATIVE_MENU_WINDOW_REACH 4

// How far a high score page at rest may reach over the 4:3 edge and still
// count as this page: the title band is an area over the whole
// width and is not asked, everything else lies in 32..480.
#define NATIVE_MENU_PAGE_SLACK 32

// Is window `index` a menu window of the driver select in this frame?
//
// Three things must hold: a player menu is in the picture, the driver select
// is the active menu, and the push buffer is not the full canvas. The
// third is the sign the driver select sets itself: HideDrivers gives every
// push buffer the whole canvas again through PushBuffer_Init (MM_Characters.c:586).
int NativeMenuWindow_Active(int index)
{
	struct GameTracker *gGT;
	const struct PushBuffer *pb;

	if (!CTR_UI_MenuMode())
	{
		return 0;
	}

	gGT = sdata->gGT;

	if ((index < 0) || (index >= 4) || (index >= (int)gGT->numPlyrNextGame))
	{
		return 0;
	}

	if (sdata->ptrActiveMenu != &D230.menuCharacterSelect)
	{
		return 0;
	}

	if ((D230.activeCharacterSelectWindowPos == NULL) || (D230.characterSelectTransitionMeta == NULL))
	{
		return 0;
	}

	pb = &gGT->pushBuffer[index];

	if ((pb->rect.x == 0) && (pb->rect.w == CTR_Canvas_ActiveWidth()))
	{
		return 0;
	}

	return 1;
}

// The frame including shadow, in the 512 space, where it stands in this frame - so
// with the offset of the fly-in and fly-out and WITHOUT the clamp at column 512 that
// only the 3D rectangle gets (MM_Characters.c:215-259). The frame is drawn
// unclamped (MM_Characters.c:1176-1186), and everything follows
// it.
int NativeMenuWindow_Box(int index, int *outBox)
{
	const SVec2 *pos;
	const struct TransitionMeta *meta;
	int x;
	int y;

	if (!NativeMenuWindow_Active(index) || (outBox == NULL))
	{
		return 0;
	}

	pos = &D230.activeCharacterSelectWindowPos[index];
	meta = &D230.characterSelectTransitionMeta[index + MM_CHARACTER_SELECT_DRIVER_WINDOW_TRANSITION_FIRST];

	x = (int)pos->x + (int)meta->currX;
	y = (int)pos->y + (int)meta->currY;

	outBox[0] = x;
	outBox[1] = y;
	outBox[2] = x + (int)D230.characterSelectWindowWidth + NATIVE_MENU_WINDOW_SHADOW_X;
	outBox[3] = y + (int)D230.characterSelectWindowHeight + NATIVE_MENU_WINDOW_SHADOW_Y;
	return 1;
}

// The core: the window itself and the line width all around, WITHOUT shadow. In 2P
// the windows stand 12 points apart (30..244 and 256..470), and the
// shadow of the left one reaches to 256 - into the frame of the right one. A primitive
// that lies in the core of a window belongs to it, even if it also lies in the
// shadow of the neighbour.
int NativeMenuWindow_CoreRegion(int index, int *outRegion)
{
	int box[4];

	if (!NativeMenuWindow_Box(index, box) || (outRegion == NULL))
	{
		return 0;
	}

	outRegion[0] = box[0] - NATIVE_MENU_WINDOW_REACH;
	outRegion[1] = box[1] - NATIVE_MENU_WINDOW_REACH;
	outRegion[2] = box[2] - NATIVE_MENU_WINDOW_SHADOW_X + NATIVE_MENU_WINDOW_REACH;
	outRegion[3] = box[3] - NATIVE_MENU_WINDOW_SHADOW_Y + NATIVE_MENU_WINDOW_REACH;
	return 1;
}

// The rectangle in which a UI primitive counts as part of the frame of this window: the
// frame including shadow and the line width all around.
int NativeMenuWindow_FrameRegion(int index, int *outRegion)
{
	int box[4];

	if (!NativeMenuWindow_Box(index, box) || (outRegion == NULL))
	{
		return 0;
	}

	outRegion[0] = box[0] - NATIVE_MENU_WINDOW_REACH;
	outRegion[1] = box[1] - NATIVE_MENU_WINDOW_REACH;
	outRegion[2] = box[2] + NATIVE_MENU_WINDOW_REACH;
	outRegion[3] = box[3] + NATIVE_MENU_WINDOW_REACH;
	return 1;
}

// Where the name of the driver stands: the same calculation as MM_Characters.c:1107-1138,
// and the width from the font itself. Exact, not generous: in 2P
// both names stand side by side at the same height, and in 4P a name can be wider
// than its window (CRASH BANDICOOT in FONT_SMALL, computed 15 x 13 = 195
// points, window 160). A band around the window would have caught the neighbour in 2P and
// too little in 4P.
int NativeMenuWindow_NameBand(int index, int *outBand)
{
	struct GameTracker *gGT;
	int box[4];
	int numPlyr;
	int fontType;
	int nameYOffset;
	int nameY;
	int centreX;
	int width;
	int characterID;
	const char *customName;

	if (!NativeMenuWindow_Box(index, box) || (outBand == NULL))
	{
		return 0;
	}

	gGT = sdata->gGT;
	numPlyr = (int)gGT->numPlyrNextGame;
	fontType = (numPlyr >= 3) ? FONT_SMALL : FONT_CREDITS;
	nameYOffset = (numPlyr >= 3) ? 4 : 0;

	if ((numPlyr == 4) && (index > 1))
	{
		nameY = box[1] + nameYOffset + MM_CHARACTER_SELECT_4P_NAME_BOTTOM_OFFSET;
	}
	else
	{
		nameY = box[1] + (int)D230.characterSelectNameTextY + nameYOffset;
	}

	characterID = (int)data.characterIDs[index];
	if ((characterID < 0) || (characterID >= (int)(sizeof(data.MetaDataCharacters) / sizeof(data.MetaDataCharacters[0]))))
	{
		return 0;
	}

	// The custom grid shows the name of a roster entry in this window: measure that one.
	customName = MM_NativeCharGrid_SeatName(index);
	if (customName != NULL)
	{
		width = DecalFont_GetLineWidth((char *)customName, (s16)fontType);
	}
	else
	{
		width = DecalFont_GetLineWidth(sdata->lngStrings[data.MetaDataCharacters[characterID].name_LNG_long], (s16)fontType);
	}

	centreX = box[0] + (int)((u32)D230.characterSelectWindowWidth >> 1);

	outBand[0] = centreX - (width / 2) - NATIVE_MENU_WINDOW_REACH;
	outBand[1] = nameY - 1;
	outBand[2] = centreX + (width / 2) + NATIVE_MENU_WINDOW_REACH;
	outBand[3] = nameY + (int)data.font_charPixHeight[fontType] + 1;
	return 1;
}

// The shift of this window in canvas columns. 0 if it is none.
int NativeMenuWindow_Shift(const struct CTR_UIViewParameters *view, int index)
{
	int box[4];

	if ((view == NULL) || !NativeMenuWindow_Box(index, box))
	{
		return 0;
	}

	return CTR_UI_AnchorShift(view, CTR_UI_AnchorForBoxEdge(box[0], box[2], view->virtualWidth));
}

// For PushBuffer_SetDrawEnv_Normal: if this push buffer is a driver window,
// by how much does its draw area shift? The canvas is read there
// the same way as in the UI mapper (CTR_UI_BuildSafeArea), so the
// 3D picture gets the same number as its frame.
int NativeMenuWindow_ShiftFor(const struct PushBuffer *pb)
{
	struct CTR_UIViewParameters view;
	int index;

	if ((pb == NULL) || !CTR_UI_MenuMode())
	{
		return 0;
	}

	for (index = 0; index < 4; index++)
	{
		if (pb == &sdata->gGT->pushBuffer[index])
		{
			break;
		}
	}

	if ((index >= 4) || !CTR_UI_BuildSafeArea(&view, CTR_CANVAS_REFERENCE_WIDTH, CTR_Canvas_ActiveHeight()))
	{
		return 0;
	}

	return NativeMenuWindow_Shift(&view, index);
}

// THE HIGH SCORE PAGES ITS PAGES OVER THE WHOLE PICTURE.
//
// On a track change MM_HighScore_MenuProc draws two pages, 512
// points apart, and pushes both on in 8 steps of 64 points each
// (MM_HighScore.c:386-432). Each page has a left column at the left edge and
// a right one at the right edge. With the edge rule applied to the position IN THIS FRAME
// each column changes its anchor while gliding, and at different
// times than the column next to it - measured at 43:18 the page fell apart for
// some frames into pieces that overtook each other.
//
// That is why during paging the resting position is looked up for every box here:
// the box minus the offset of the page it belongs to. The two
// offsets lie 512 apart, and a page fills 32..480, so exactly
// one fits. The anchor comes from the resting position.
//
// AND THE OFFSET OF THE PAGE GROWS WITH THE CANVAS. In the wide picture a
// page is as wide as the canvas - left column at the left, right one at the right
// edge. If two such pages glide only 512 points apart, the
// right column of one overtakes the left one of the other (measured).
// With the offset times W/512 two whole pages glide rigidly side by side, and
// what is outside at the end of paging at 4:3 is outside here too. At 4:3
// the addition is 0.
//
// 1 if paging is in progress and the box belongs to one of the pages;
// then outOffset holds the offset of its page in the 512 space.
int NativeMenuHighScore_RestBox(const int *box, int *outRest, int *outOffset)
{
	const int trackFrame = (int)D230.highScoreTransition.trackFrame;
	const int move = (int)D230.highScoreTransition.activeHorizontalMove;
	int offsets[2];
	int k;

	if ((box == NULL) || (outRest == NULL) || !CTR_UI_MenuMode() || (sdata->ptrActiveMenu != &D230.menuHighScores) || (trackFrame == 0) ||
	    (move == 0))
	{
		return 0;
	}

	offsets[0] = (MM_HIGHSCORE_SLIDE_TRANSITION_FRAMES - trackFrame) * move * MM_HIGHSCORE_TRACK_SLIDE_STEP_X;
	offsets[1] = trackFrame * -MM_HIGHSCORE_TRACK_SLIDE_STEP_X * move;

	for (k = 0; k < 2; k++)
	{
		const int x0 = box[0] - offsets[k];
		const int x1 = box[2] - offsets[k];

		if ((x0 >= -NATIVE_MENU_PAGE_SLACK) && (x1 <= (CTR_CANVAS_REFERENCE_WIDTH + NATIVE_MENU_PAGE_SLACK)))
		{
			outRest[0] = x0;
			outRest[1] = box[1];
			outRest[2] = x1;
			outRest[3] = box[3];
			if (outOffset != NULL)
			{
				*outOffset = offsets[k];
			}
			return 1;
		}
	}

	return 0;
}

// MULTIPLAYER IS LOCKED.
//
// Multiplayer is not part of the scope of the project. Every path of the
// player menu to more than one player is
// therefore locked: VS. and BATTLE in the main menu, 2P in the player count of
// arcade and arcade cup, and 2P, 3P, 4P behind VS. and BATTLE.
//
// Locking uses the game's own mechanism, the lock bit
// MENU_ROW_LOCKED in stringIndex. RECTMENU_DrawSelf draws such a row
// in the lock style, grey (RECTMENU.c:783-788), and RECTMENU_ProcessInput accepts
// no cross on it, it only plays the lock sound (RECTMENU.c:1036-1071). The
// row stays visible and in its place, the cursor can stand on it, and
// no other row moves up.
//
// WHY IN EVERY CALL AND NOT ONCE. Two places rewrite the rows:
// - MM_ToggleRows_PlayerCount (MM_MenuFlow.c:257-286) clears on every call
//   of MM_MenuProc_Main the bit of the player count rows and sets it only where
//   pads are missing. With two pads it unlocks 2P. MM_MenuProc_Main runs in
//   every frame before input and drawing (RECTMENU.c:1139, 1150, 1194).
// - OVR230_ResetRuntimeState (D230.c:859-869) resets all of D230 to the
//   initial state, including the procs - on every return from a race.
// The main menu box therefore gets, instead of MM_MenuProc_Main, a proc that
// calls MM_MenuProc_Main and locks AFTERWARDS. It is hooked in anew every frame,
// at the very top of RECTMENU_ProcessState (NativeMenuLock_Tick), so before the
// first call after every reset.
//
// THE SECOND WAY PAST THE LOCK. RECTMENU_ProcessInput checks the bit on
// the row the cursor stood on BEFORE the key press, and takes over the
// new row only afterwards (RECTMENU.c:1040-1052, "glitch into 3P VS with only 2
// controllers, by pressing DOWN+X same frame"). With direction and cross in the same
// frame you would get from ARCADE to VS. or from 1P to 2P. The procs of the three
// boxes therefore do not even let a choice that lands on a locked row
// through to the game; the cursor then stands on the locked
// row, as after a cross on it.
//
// The menu files stay untouched. Command line and debug menu go past
// these boxes and stay as they are.

// THE RACE TYPE BOX UNDER ARCADE: SINGLE / CUP / VS. / BATTLE / NITRO-PIT.
//
// Retail has two rows (D230.c:105-110), and their meaning lies in the index:
// MM_MenuProc_SingleCup only takes row < MM_RACE_TYPE_SELECTABLE_ROWS (2)
// (MM_MenuFlow.c:450) and reads row != 0 as cup (:456). The new rows
// therefore stand AT THE BACK, on 2 to 4. SINGLE and CUP keep their index, and
// the retail proc never sees the new ones - they are answered by
// NativeMenuLock_ProcRaceType.
//
// VS. AND BATTLE are locked like all of multiplayer. If
// they are unlocked, their chain becomes one level deeper each than from the old main menu:
// VS. then needs a SINGLE/CUP box of its own WITH title (main menu
// -> RACE TYPE -> SINGLE/CUP -> 2P/3P/4P, 14..209 like the arcade path today;
// without title 27..222, too high), BATTLE main menu -> RACE TYPE -> 2P/3P/4P
// (34..197). The proc then has to clear ARCADE_MODE, so that
// MM_MenuProc_SingleCup takes the VS. path (MM_MenuFlow.c:465-477).
//
// NITRO-PIT REPLACES THE BOX instead of hanging one below it: the same box
// D230.menuRaceType gets the title NITRO-PIT and the rows NITRO RACE / NITRO
// CUP / TIME TRIAL / CRYSTAL / CTR. A second box in the chain would be a
// fifth level (the four of the arcade path occupy 14..209 of 216) and would get
// neither the D230 reset nor the quick-state relocation. NITRO-PIT is one
// player: NITRO RACE and NITRO CUP skip the PLAYERS box and
// open the difficulty directly - exactly what the 1P row does
// (MM_MenuFlow.c:306-314). The chain there is three levels deep.
//
// LOCKED, grey with lock sound: NITRO RACE, as long as no container offers a race;
// NITRO CUP, as long as cups.txt carries no readable cup (why is said by
// the log); CRYSTAL, as long as no container offers Crystal
// (MM_NativeCrystal.c); CTR likewise with CTR (MM_NativeCtr.c); TIME
// TRIAL (Beta 2) always. If the cursor stands
// on a grey row, a line below the box says the reason
// (NativeMenuReason_NoteDrawn). NITRO-PIT itself is never grey in the arcade path.
//
// WHY THE WIDTH IS RIGHT: see MM_NativeMenu_String. No row has more than
// ten characters (NITRO RACE and TIME TRIAL exactly ten).
enum
{
	NATIVE_RACE_TYPE_ROW_VS = MM_RACE_TYPE_SELECTABLE_ROWS,
	NATIVE_RACE_TYPE_ROW_BATTLE = MM_RACE_TYPE_SELECTABLE_ROWS + 1,
	NATIVE_RACE_TYPE_ROW_NITRO_PIT = MM_RACE_TYPE_SELECTABLE_ROWS + 2,

	NATIVE_PIT_ROW_RACE = 0,
	NATIVE_PIT_ROW_CUP = 1,
	NATIVE_PIT_ROW_TIME_TRIAL = 2,
	NATIVE_PIT_ROW_CRYSTAL = 3,
	NATIVE_PIT_ROW_CTR = 4,
};

// stringIndex, then the target row for up, down, left, right. The texts
// are set anew every frame by NativeMenuRaceType_Apply, including the lock bit.
global_variable struct MenuRow s_nativeRaceTypeRows[] = {
    {0x15E, 0, 1, 0, 0},
    {0x15F, 0, 2, 1, 1},
    {LNG_VS, 1, 3, 2, 2},
    {LNG_BATTLE, 2, 4, 3, 3},
    {MM_NATIVE_LNG_NITRO_PIT, 3, 4, 4, 4},
    {RECTMENU_STRING_NONE},
};

global_variable struct MenuRow s_nativePitRows[] = {
    {MM_NATIVE_LNG_NITRO_RACE, 0, 1, 0, 0},
    {MM_NATIVE_LNG_NITRO_CUP, 0, 2, 1, 1},
    {LNG_TIME_TRIAL, 1, 3, 2, 2},
    {MM_NATIVE_LNG_CRYSTAL, 2, 4, 3, 3},
    {MM_NATIVE_LNG_CTR, 3, 4, 4, 4},
    {RECTMENU_STRING_NONE},
};

// Is the box currently standing as NITRO-PIT? And a change the proc
// requests: RECTMENU writes rowSelected back AFTER the proc
// (RECTMENU.c:1032, :1071), so only the next frame puts rows and
// cursor together - the cursor never points past the end of the rows.
global_variable int s_nativePitOpen = 0;

enum
{
	NATIVE_PIT_REQUEST_NONE = 0,
	NATIVE_PIT_REQUEST_OPEN = 1,
	NATIVE_PIT_REQUEST_CLOSE = 2,
};

global_variable int s_nativePitRequest = NATIVE_PIT_REQUEST_NONE;

// The reason for a grey row, or NULL. English like all game texts;
// cups.txt appears only in the log.
internal const char *NativeMenuRaceType_Reason(int row)
{
	if (!s_nativePitOpen)
	{
		return NULL;
	}

	switch (row)
	{
	case NATIVE_PIT_ROW_RACE:
		return (NativeTrack_CountRaceOffered() <= 0) ? "NO CUSTOM TRACKS" : NULL;
	case NATIVE_PIT_ROW_CUP:
		return (NativeCup_Count() <= 0) ? "NO CUSTOM CUPS" : NULL;
	case NATIVE_PIT_ROW_CRYSTAL:
		return (NativeTrack_CountOffered(NATIVE_TRACK_MODE_CRYSTAL) <= 0) ? "NO CRYSTAL TRACKS" : NULL;
	// TIME TRIAL comes in Beta 2.
	case NATIVE_PIT_ROW_TIME_TRIAL:
		return "COMING IN BETA 2";
	// CTR is released for Beta 0 - white as soon as a
	// container offers CTR (MM_NativeCtr.c).
	case NATIVE_PIT_ROW_CTR:
		return (NativeTrack_CountOffered(NATIVE_TRACK_MODE_CTR) <= 0) ? "NO CTR TRACKS" : NULL;
	default:
		return NULL;
	}
}

// Every frame, because OVR230_ResetRuntimeState resets the box including the row pointer
// (block MULTIPLAYER IS LOCKED, above), and because ARCADE_MODE
// is only settled with the choice in the main menu.
internal void NativeMenuRaceType_Apply(void)
{
	struct RectMenu *menu = &D230.menuRaceType;
	const int arcade = ((sdata->gGT->gameMode1 & ARCADE_MODE) != 0);
	int i;

	if (s_nativePitRequest == NATIVE_PIT_REQUEST_OPEN)
	{
		s_nativePitOpen = 1;
		menu->rowSelected = NATIVE_PIT_ROW_RACE;
	}
	else if (s_nativePitRequest == NATIVE_PIT_REQUEST_CLOSE)
	{
		s_nativePitOpen = 0;
		menu->rowSelected = NATIVE_RACE_TYPE_ROW_NITRO_PIT;
	}

	s_nativePitRequest = NATIVE_PIT_REQUEST_NONE;

	if (s_nativePitOpen)
	{
		s_nativePitRows[NATIVE_PIT_ROW_RACE].stringIndex = MM_NATIVE_LNG_NITRO_RACE;
		s_nativePitRows[NATIVE_PIT_ROW_CUP].stringIndex = MM_NATIVE_LNG_NITRO_CUP;
		s_nativePitRows[NATIVE_PIT_ROW_TIME_TRIAL].stringIndex = LNG_TIME_TRIAL;
		s_nativePitRows[NATIVE_PIT_ROW_CRYSTAL].stringIndex = MM_NATIVE_LNG_CRYSTAL;
		s_nativePitRows[NATIVE_PIT_ROW_CTR].stringIndex = MM_NATIVE_LNG_CTR;

		for (i = NATIVE_PIT_ROW_RACE; i <= NATIVE_PIT_ROW_CTR; i++)
		{
			if (NativeMenuRaceType_Reason(i) != NULL)
			{
				s_nativePitRows[i].stringIndex |= MENU_ROW_LOCKED;
			}
		}

		menu->stringIndexTitle = MM_NATIVE_LNG_NITRO_PIT;
		menu->rows = &s_nativePitRows[0];
		return;
	}

	s_nativeRaceTypeRows[0].stringIndex = D230.rowsRaceType[0].stringIndex;
	s_nativeRaceTypeRows[1].stringIndex = D230.rowsRaceType[1].stringIndex;
	s_nativeRaceTypeRows[NATIVE_RACE_TYPE_ROW_VS].stringIndex = LNG_VS;
	s_nativeRaceTypeRows[NATIVE_RACE_TYPE_ROW_VS].stringIndex |= MENU_ROW_LOCKED;
	s_nativeRaceTypeRows[NATIVE_RACE_TYPE_ROW_BATTLE].stringIndex = LNG_BATTLE;
	s_nativeRaceTypeRows[NATIVE_RACE_TYPE_ROW_BATTLE].stringIndex |= MENU_ROW_LOCKED;
	s_nativeRaceTypeRows[NATIVE_RACE_TYPE_ROW_NITRO_PIT].stringIndex = MM_NATIVE_LNG_NITRO_PIT;

	// The box now only comes through ARCADE; any other path does not get
	// NITRO-PIT.
	if (!arcade)
	{
		s_nativeRaceTypeRows[NATIVE_RACE_TYPE_ROW_NITRO_PIT].stringIndex |= MENU_ROW_LOCKED;
	}

	menu->stringIndexTitle = LNG_RACE_TYPE;
	menu->rows = &s_nativeRaceTypeRows[0];
}

// THE REASON BELOW THE BOX. From the end of RECTMENU_DrawSelf (RECTMENU.c),
// with the finished frame: one line of FONT_SMALL, centred below the frame and
// its shadow (6), in the lock style of the grey rows (0x17, RECTMENU.c:96).
// Only for the open box on whose row the cursor stands. Below the open NITRO-PIT box (frame to 199,
// shadow to 205) it lies at 207..214. In widescreen it holds the right
// edge like the box (native_uidecl.c, "menu-reason-line").
#define NATIVE_MENU_REASON_GAP 8
#define NATIVE_MENU_REASON_STYLE 0x17

global_variable int s_nativeMenuReasonDrawn = 0;

// The reason for a grey row of a box from the OPTIONS branch (further
// below), or NULL.
internal const char *NativeMenuOptions_BoxReason(const struct RectMenu *box);

void NativeMenuReason_NoteDrawn(const struct RectMenu *box, const RECT *frame)
{
	const char *reason = NULL;

	if ((box == NULL) || (frame == NULL) || ((box->state & ONLY_DRAW_TITLE) != 0) || (box->rowSelected < 0))
	{
		return;
	}

	if (box == &D230.menuRaceType)
	{
		reason = NativeMenuRaceType_Reason(box->rowSelected);
	}
	else
	{
		reason = NativeMenuOptions_BoxReason(box);
	}

	if (reason == NULL)
	{
		return;
	}

	DecalFont_DrawLine((char *)reason, (s16)(frame->x + (frame->w / 2)), (s16)(frame->y + frame->h + NATIVE_MENU_REASON_GAP),
	                   FONT_SMALL, (s16)(JUSTIFY_CENTER | NATIVE_MENU_REASON_STYLE));
	s_nativeMenuReasonDrawn = 1;
}

// For the widescreen row: has a reason been drawn in this frame?
// Reset at the start of every menu frame (NativeMenuLock_Tick).
int NativeMenuReason_OnScreen(void)
{
	return s_nativeMenuReasonDrawn;
}

// THE MAIN MENU: SIX ROWS.
//
// ADVENTURE, TIME TRIAL, ARCADE, HIGH SCORE, OPTIONS, EXIT GAME - always exactly
// these six, also with SCRAPBOOK unlocked. VS. and BATTLE stand under
// ARCADE in the race type box, SCRAPBOOK in the OPTIONS box. The retail fields in
// D230 (rowsMainMenuBasic / rowsMainMenuWithScrapbook) stay as they are;
// the box gets this list of its own. MM_MenuProc_Main chooses by the TEXT
// of the row, not by its index (MM_MenuFlow.c:156), so ADVENTURE,
// TIME TRIAL, ARCADE and HIGH SCORE work unchanged here. But with
// SCRAPBOOK unlocked it sets its own list in every call
// (MM_MenuFlow.c:49-52); that is why the swap happens AFTER it, in NativeMenuLock_Apply.
//
// SPACE: without title the box is centred on y 108 (D230.c:46-49), 20 per
// row, six rows occupy 44..171 like retail. The width stays: OPTIONS has
// 7 characters, EXIT GAME 9, the widest rows stay TIME TRIAL and HIGH
// SCORE with 10.
//
// MM_MenuProc_Main does not know OPTIONS and EXIT GAME. Before its
// choice it clears the mode bits, sets ONLY_DRAW_TITLE (MM_MenuFlow.c:139-145) and
// would then find no branch. NativeMenuMain_Answer answers both, and
// MM_MenuProc_Main never sees them.
global_variable struct MenuRow s_nativeMainRows[] = {
    {LNG_ADVENTURE, 0, 1, 0, 0},
    {LNG_TIME_TRIAL, 0, 2, 1, 1},
    {LNG_ARCADE, 1, 3, 2, 2},
    {LNG_HIGH_SCORE, 2, 4, 3, 3},
    {LNG_OPTIONS, 3, 5, 4, 4},
    {MM_NATIVE_LNG_EXIT_GAME, 4, 5, 5, 5},
    {RECTMENU_STRING_NONE},
};

// THE CONFIRMATION BEFORE QUITTING, in the retail format of the pause: title QUIT,
// rows YES and NO from data.rowsQuit, in all eight languages
// (zGlobal_DATA.c:4216-4235), the cursor on NO as in MainFreeze.c:930. The
// retail proc MainFreeze_MenuPtrQuit belongs to the pause (it loads the
// main menu level), therefore one of our own. The box hangs below the
// main menu like NEW/LOAD below ADVENTURE (D230.c:121-135) and returns in
// the same way (MM_MenuFlow.c:486-490).
internal void NativeMenuMain_ProcQuit(struct RectMenu *menu);

global_variable struct RectMenu s_nativeQuitMenu = {
    .stringIndexTitle = LNG_QUIT,
    .state = CENTER_ON_X,
    .funcPtr = NativeMenuMain_ProcQuit,
};

// A child box gets its proc only on a key press (RECTMENU.c:1025,
// 1056): YES, NO or back (-1).
internal void NativeMenuMain_ProcQuit(struct RectMenu *menu)
{
	if (menu->rowSelected != 0)
	{
		menu->ptrPrevBox_InHierarchy->state &= ~(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY);
		return;
	}

	// The same exit as Ctrl+Q: log line, then exit(0), cleaned up through
	// atexit (native_platform.c). Does not come back.
	Platform_QuitGame("EXIT GAME in the main menu");
}

// Further below, in the block MULTIPLAYER IS LOCKED.
internal int NativeMenuLock_ChoiceIsLocked(const struct RectMenu *menu);

// THE OPTIONS BOX: CHEATS /
// GRAPHICS, plus SCRAPBOOK when unlocked. It hangs below the
// main menu like NEW/LOAD below ADVENTURE (D230.c:121-135) and returns in
// the same way (MM_MenuFlow.c:486-490). Two or three rows below
// the collapsed main box: far below 216.
//
// No row for the options screen of the pause (MainFreeze_MenuPtrOptions): it
// is built for the pause and does not belong in the main menu. Sound settings
// will later get a page of their own
// next to GRAPHICS.
global_variable struct MenuRow s_nativeOptionsRows[] = {
    {MM_NATIVE_LNG_CHEATS, 0, 1, 0, 0},
    {MM_NATIVE_LNG_GRAPHICS, 0, 1, 1, 1},
    {RECTMENU_STRING_NONE},
};

// SCRAPBOOK: only once it is unlocked, and then between
// CHEATS and GRAPHICS - hidden, not grey, as in the retail main menu
// (MM_MenuFlow.c:49-52). It is a reward.
global_variable struct MenuRow s_nativeOptionsRowsScrapbook[] = {
    {MM_NATIVE_LNG_CHEATS, 0, 1, 0, 0},
    {LNG_SCRAPBOOK, 0, 2, 1, 1},
    {MM_NATIVE_LNG_GRAPHICS, 1, 2, 2, 2},
    {RECTMENU_STRING_NONE},
};

// SCRAPBOOK FROM THE OPTIONS BOX AND BACK. It is started as from the
// retail main menu (route 5, MM_Title.c:215-229: level SCRAPBOOK). Its end
// reloads the main menu level (MM_Scrapbook.c:214-238), and its stage 4
// begins with the title intro and a closed chain
// (MM_JumpTo_Title_FirstTime, MM_MenuFlow.c:556-583) - in the original you land in the
// main menu. From here it should go back into this box: the marker tells
// NativeMenuOptions_AfterScrapbook so in the first menu frame of the new level.
// Two stages, because the main menu level still runs for a few frames after the choice
// (fade-out): first the scrapbook level, then the main menu again.
enum
{
	NATIVE_SCRAPBOOK_NONE = 0,
	NATIVE_SCRAPBOOK_CHOSEN = 1,
	NATIVE_SCRAPBOOK_PLAYING = 2,
};

global_variable int s_nativeScrapbookFromOptions = NATIVE_SCRAPBOOK_NONE;

// --unlock-scrapbook (main.c, only with --dev).
int g_cfg_unlockScrapbook = 0;

internal void NativeMenuOptions_Proc(struct RectMenu *menu);

// The CHEATS box, further below.
global_variable struct RectMenu s_nativeCheatsMenu;

// Without title like NEW/LOAD: the collapsed row above it is already called
// OPTIONS.
global_variable struct RectMenu s_nativeOptionsMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .state = CENTER_ON_X,
    .rows = &s_nativeOptionsRows[0],
    .funcPtr = NativeMenuOptions_Proc,
};

// The reason for a grey row of the OPTIONS box, or NULL.
internal const char *NativeMenuOptions_Reason(s16 lng)
{
	// GRAPHICS is open since Beta 0 (game/native_graphics.c).
	switch (lng)
	{
	default:
		return NULL;
	}
}

internal void NativeMenuOptions_Apply(void)
{
	struct MenuRow *rows;
	int i;

	if (g_cfg_unlockScrapbook && !CHECK_ADV_BIT(sdata->gameProgress.unlocks, GAME_UNLOCK_BIT_SCRAPBOOK))
	{
		UNLOCK_ADV_BIT(sdata->gameProgress.unlocks, GAME_UNLOCK_BIT_SCRAPBOOK);
		Platform_Log("[CTR Menu] --unlock-scrapbook: SCRAPBOOK unlocked for this session\n");
	}

	rows = CHECK_ADV_BIT(sdata->gameProgress.unlocks, GAME_UNLOCK_BIT_SCRAPBOOK) ? &s_nativeOptionsRowsScrapbook[0]
	                                                                               : &s_nativeOptionsRows[0];

	for (i = 0; rows[i].stringIndex != RECTMENU_STRING_NONE; i++)
	{
		const s16 lng = (s16)(rows[i].stringIndex & MENU_ROW_LNG_MASK);

		rows[i].stringIndex = lng;

		if (NativeMenuOptions_Reason(lng) != NULL)
		{
			rows[i].stringIndex |= MENU_ROW_LOCKED;
		}
	}

	s_nativeOptionsMenu.rows = rows;
}

// In the first menu frame after the scrapbook, if it came from the OPTIONS box:
// the chain main menu (OPTIONS) -> OPTIONS box (SCRAPBOOK) stands
// open again. NativeMenuLock_Tick runs before ptrDesiredMenu is evaluated
// (RECTMENU.c:1101, :1104-1130), which then opens the leaf, this box.
// MM_MenuProc_Main skips the title intro as soon as the chain is open
// (MM_MenuFlow.c:103-106).
internal void NativeMenuOptions_AfterScrapbook(void)
{
	struct RectMenu *mainMenu = &D230.menuMainMenu;
	int i;

	if ((s_nativeScrapbookFromOptions == NATIVE_SCRAPBOOK_CHOSEN) && (sdata->gGT->levelID == SCRAPBOOK))
	{
		s_nativeScrapbookFromOptions = NATIVE_SCRAPBOOK_PLAYING;
	}

	if ((s_nativeScrapbookFromOptions != NATIVE_SCRAPBOOK_PLAYING) || (sdata->gGT->levelID != MAIN_MENU_LEVEL) ||
	    ((sdata->ptrDesiredMenu != mainMenu) && (sdata->ptrActiveMenu != mainMenu)))
	{
		return;
	}

	s_nativeScrapbookFromOptions = NATIVE_SCRAPBOOK_NONE;

	for (i = 0; s_nativeMainRows[i].stringIndex != RECTMENU_STRING_NONE; i++)
	{
		if ((s_nativeMainRows[i].stringIndex & MENU_ROW_LNG_MASK) == LNG_OPTIONS)
		{
			mainMenu->rowSelected = (s16)i;
		}
	}

	for (i = 0; s_nativeOptionsRowsScrapbook[i].stringIndex != RECTMENU_STRING_NONE; i++)
	{
		if ((s_nativeOptionsRowsScrapbook[i].stringIndex & MENU_ROW_LNG_MASK) == LNG_SCRAPBOOK)
		{
			s_nativeOptionsMenu.rowSelected = (s16)i;
		}
	}

	mainMenu->ptrNextBox_InHierarchy = &s_nativeOptionsMenu;
	mainMenu->state |= ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
	sdata->ptrDesiredMenu = mainMenu;

	Platform_Log("[CTR Menu] OPTIONS: SCRAPBOOK ended - back to the OPTIONS box\n");
}

internal const char *NativeMenuOptions_BoxReason(const struct RectMenu *box)
{
	if ((box != &s_nativeOptionsMenu) || (box->rows == NULL))
	{
		return NULL;
	}

	return NativeMenuOptions_Reason((s16)(box->rows[box->rowSelected].stringIndex & MENU_ROW_LNG_MASK));
}

// A child box gets its proc only on a key press.
internal void NativeMenuOptions_Proc(struct RectMenu *menu)
{
	s16 lng;

	if (NativeMenuLock_ChoiceIsLocked(menu))
	{
		return;
	}

	if (menu->rowSelected < 0)
	{
		menu->ptrPrevBox_InHierarchy->state &= ~(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY);
		return;
	}

	lng = (s16)(menu->rows[menu->rowSelected].stringIndex & MENU_ROW_LNG_MASK);

	if (lng == LNG_SCRAPBOOK)
	{
		// Like the retail row (MM_MenuFlow.c:245-253), plus the marker for
		// the way back.
		D230.desiredMenuIndex = MM_EXIT_ROUTE_SCRAPBOOK;
		D230.titleMenuState = TITLE_MENU_STATE_EXITING;
		menu->state |= ONLY_DRAW_TITLE;
		s_nativeScrapbookFromOptions = NATIVE_SCRAPBOOK_CHOSEN;
		Platform_Log("[CTR Menu] OPTIONS: SCRAPBOOK - the retail scrapbook, back to the OPTIONS box afterwards\n");
		return;
	}

	if (lng == MM_NATIVE_LNG_GRAPHICS)
	{
		NativeGraphics_Open(menu);
		return;
	}

	if (lng == MM_NATIVE_LNG_CHEATS)
	{
		menu->ptrNextBox_InHierarchy = &s_nativeCheatsMenu;
		menu->state |= ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
		Platform_Log("[CTR Menu] OPTIONS: CHEATS%s\n", Platform_SettingsLocked() ? " - greyed out, --settings-defaults keeps every cheat off" : "");
	}
}

// THE CHEATS BOX.
//
// The twelve cheats that only apply for the session: bits in gGT->gameMode2
// (namespace_Main.h:199-217) that never go onto the memory card (RaceConfig.c:23-35
// saves no gameMode2). The same bits are set by the retail codes with L1+R1 in the
// main menu (MM_CheatCodes.c) and by the debug page CHEATS. Here individually on and
// off. On the change to ADVENTURE and TIME TRIAL retail only clears WUMPA,
// MASK, TURBO, ENGINE and BOMBS (MM_MenuFlow.c:160, :180) - that stays so.
//
// NOT HERE: the unlock codes (drivers, tracks, SCRAPBOOK). They set
// bits in gameProgress.unlocks, and those go permanently onto the card with the next saved
// game state (SelectProfile.c:640-648). The retail codes
// keep working. Spyro 2 (D O A R) hangs the native game
// (MainKillGame.c:43-45).
//
// MEASUREMENT RUNS: under --settings-defaults all cheats are off and the rows
// grey; a bit that gets set anyway (retail code,
// debug page) is cleared by NativeMenuCheats_Enforce in the next menu frame, which
// says so in the log. Every [CTR Race] line names the cheat state.
//
// Small font, 8 points per row (RECTMENU.c:536-543): twelve rows in the
// big font (20 per row) did not fit in 216. Even so the box reaches
// below the collapsed OPTIONS and CHEATS down to row 214; there is no room for a
// reason line below it. It is locked only under
// --settings-defaults, and the log says so.
struct NativeMenuCheat
{
	const char *name;
	u32 bit;
};

// At most nine characters per name: with " OFF" thirteen characters of small font
// (13 points per character, space 7), not wider than TIME TRIAL in the
// big font (171). RECTMENU_GetWidth counts every row of the chain
// (RECTMENU.c:602-657) - a longer text would make the whole chain wider, at
// 4:3 beyond the right edge (measured with "SUPER ENGINE: OFF").
global_variable const struct NativeMenuCheat s_nativeCheats[] = {
    {"MAX WUMPA", CHEAT_WUMPA},
    {"MASKS", CHEAT_MASK},
    {"TURBOS", CHEAT_TURBO},
    {"BOMBS", CHEAT_BOMBS},
    {"INVISIBLE", CHEAT_INVISIBLE},
    {"ENGINE", CHEAT_ENGINE},
    {"ICY TRACK", CHEAT_ICY},
    {"TURBOPADS", CHEAT_TURBOPAD},
    {"ONE LAP", CHEAT_ONELAP},
    {"HARD BOTS", CHEAT_SUPERHARD},
    {"HARD ADV", CHEAT_ADV},
    {"TURBO CNT", CHEAT_TURBOCOUNT},
};

#define NATIVE_CHEAT_COUNT ((int)(sizeof(s_nativeCheats) / sizeof(s_nativeCheats[0])))

global_variable struct MenuRow s_nativeCheatRows[NATIVE_CHEAT_COUNT + 1];
global_variable char s_nativeCheatText[NATIVE_CHEAT_COUNT][24];

internal void NativeMenuCheats_Proc(struct RectMenu *menu);

global_variable struct RectMenu s_nativeCheatsMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .state = CENTER_ON_X | USE_SMALL_FONT,
    .rows = &s_nativeCheatRows[0],
    .funcPtr = NativeMenuCheats_Proc,
};

// The text of a cheat row ("MAX WUMPA ON"), or NULL. From
// MM_NativeMenu_String, when drawing and when measuring the width.
char *NativeMenuCheats_String(s16 index)
{
	const int i = (int)index - MM_NATIVE_LNG_CHEAT0;

	if ((i < 0) || (i >= NATIVE_CHEAT_COUNT))
	{
		return NULL;
	}

	snprintf(s_nativeCheatText[i], sizeof(s_nativeCheatText[i]), "%s %s", s_nativeCheats[i].name,
	         ((sdata->gGT->gameMode2 & s_nativeCheats[i].bit) != 0) ? "ON" : "OFF");
	return s_nativeCheatText[i];
}

// The cheat state as text for the log: "none" or the names.
const char *NativeMenuCheats_Describe(void)
{
	local_persist char text[256];
	int i;
	int n = 0;

	text[0] = 0;

	for (i = 0; i < NATIVE_CHEAT_COUNT; i++)
	{
		if ((sdata->gGT->gameMode2 & s_nativeCheats[i].bit) != 0)
		{
			n += snprintf(text + n, sizeof(text) - (size_t)n, "%s%s", (n > 0) ? "+" : "", s_nativeCheats[i].name);
		}
	}

	return (n > 0) ? text : "none";
}

internal void NativeMenuCheats_Apply(void)
{
	int i;

	for (i = 0; i < NATIVE_CHEAT_COUNT; i++)
	{
		s_nativeCheatRows[i].stringIndex = (s16)(MM_NATIVE_LNG_CHEAT0 + i);
		s_nativeCheatRows[i].rowOnPressUp = (char)((i > 0) ? (i - 1) : 0);
		s_nativeCheatRows[i].rowOnPressDown = (char)((i < (NATIVE_CHEAT_COUNT - 1)) ? (i + 1) : i);
		s_nativeCheatRows[i].rowOnPressLeft = (char)i;
		s_nativeCheatRows[i].rowOnPressRight = (char)i;

		if (Platform_SettingsLocked())
		{
			s_nativeCheatRows[i].stringIndex |= MENU_ROW_LOCKED;
		}
	}

	s_nativeCheatRows[NATIVE_CHEAT_COUNT].stringIndex = RECTMENU_STRING_NONE;
}

// Under --settings-defaults every cheat is off, including one from a
// retail code or the debug page.
internal void NativeMenuCheats_Enforce(void)
{
	const u32 set = sdata->gGT->gameMode2 & CHEAT_ALL;

	if (Platform_SettingsLocked() && (set != 0))
	{
		Platform_Log("[CTR Cheats] --settings-defaults: %s switched off (gameMode2 bits 0x%08x)\n", NativeMenuCheats_Describe(),
		             (unsigned)set);
		sdata->gGT->gameMode2 &= ~CHEAT_ALL;
	}
}

internal void NativeMenuCheats_Proc(struct RectMenu *menu)
{
	const s16 row = menu->rowSelected;

	if (NativeMenuLock_ChoiceIsLocked(menu))
	{
		return;
	}

	if (row < 0)
	{
		menu->ptrPrevBox_InHierarchy->state &= ~(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY);
		return;
	}

	if (row < NATIVE_CHEAT_COUNT)
	{
		sdata->gGT->gameMode2 ^= s_nativeCheats[row].bit;
		Platform_Log("[CTR Cheats] %s %s - now: %s\n", s_nativeCheats[row].name,
		             ((sdata->gGT->gameMode2 & s_nativeCheats[row].bit) != 0) ? "on" : "off", NativeMenuCheats_Describe());
	}
}

// 1 if this row in the main menu is ours and was answered here.
internal int NativeMenuMain_Answer(struct RectMenu *menu)
{
	s16 lng;

	if ((menu->funcState != RECTMENU_FUNC_STATE_INPUT) || (menu->rowSelected < 0) || (menu->rows == NULL))
	{
		return 0;
	}

	lng = (s16)(menu->rows[menu->rowSelected].stringIndex & MENU_ROW_LNG_MASK);

	if (lng == MM_NATIVE_LNG_EXIT_GAME)
	{
		s_nativeQuitMenu.rows = &data.rowsQuit[0];
		s_nativeQuitMenu.rowSelected = 1;

		menu->ptrNextBox_InHierarchy = &s_nativeQuitMenu;
		menu->state |= ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
		return 1;
	}

	if (lng == LNG_OPTIONS)
	{
		menu->ptrNextBox_InHierarchy = &s_nativeOptionsMenu;
		menu->state |= ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
		return 1;
	}

	return 0;
}

// Sets the list of the main menu, after MM_MenuProc_Main and in every frame.
internal void NativeMenuMain_Apply(void)
{
	struct RectMenu *menu = &D230.menuMainMenu;
	struct MenuRow *rows;

	if ((menu->rows != &D230.rowsMainMenuBasic[0]) && (menu->rows != &D230.rowsMainMenuWithScrapbook[0]) &&
	    (menu->rows != &s_nativeMainRows[0]))
	{
		return;
	}

	rows = &s_nativeMainRows[0];

	menu->rows = rows;

	NativeMenuOptions_Apply();
	NativeMenuCheats_Apply();
	NativeMenuCheats_Enforce();
}

// Locks every row with this text in a row list.
internal void NativeMenuLock_RowsWithString(struct MenuRow *rows, s16 lng)
{
	if (rows == NULL)
	{
		return;
	}

	for (/**/; rows->stringIndex != RECTMENU_STRING_NONE; rows++)
	{
		if ((rows->stringIndex & MENU_ROW_LNG_MASK) == lng)
		{
			rows->stringIndex |= MENU_ROW_LOCKED;
		}
	}
}

// Locks every row from `first` on in a player count list. These rows state
// their player count through the index (MM_MenuProc_1p2p: rowSelected + 1,
// MM_MenuProc_2p3p4p: rowSelected + 2), not through the text.
internal void NativeMenuLock_RowsFrom(struct MenuRow *rows, int first, int count)
{
	int i;

	for (i = first; i < count; i++)
	{
		rows[i].stringIndex |= MENU_ROW_LOCKED;
	}
}

internal void NativeMenuLock_Apply(void)
{
	NativeMenuMain_Apply();

	NativeMenuLock_RowsWithString(&D230.rowsMainMenuBasic[0], LNG_VS);
	NativeMenuLock_RowsWithString(&D230.rowsMainMenuBasic[0], LNG_BATTLE);
	NativeMenuLock_RowsWithString(&D230.rowsMainMenuWithScrapbook[0], LNG_VS);
	NativeMenuLock_RowsWithString(&D230.rowsMainMenuWithScrapbook[0], LNG_BATTLE);

	// The list the box is currently showing.
	NativeMenuLock_RowsWithString(D230.menuMainMenu.rows, LNG_VS);
	NativeMenuLock_RowsWithString(D230.menuMainMenu.rows, LNG_BATTLE);

	NativeMenuLock_RowsFrom(&D230.rowsPlayers1P2P[0], 1, MM_PLAYER_1P2P_SELECTABLE_ROWS);
	NativeMenuLock_RowsFrom(&D230.rowsPlayers2P3P4P[0], 0, MM_PLAYER_2P3P4P_SELECTABLE_ROWS);

	NativeMenuRaceType_Apply();

	// After MM_ToggleRows_Difficulty (in MM_MenuProc_Main, which NativeMenuLock_ProcMain
	// runs before this call): in the custom cup all difficulties are open.
	MM_NativeCup_OpenDifficulty();
}

// 1 if the box is about to answer a choice that stands on a
// locked row.
internal int NativeMenuLock_ChoiceIsLocked(const struct RectMenu *menu)
{
	return (menu->funcState == RECTMENU_FUNC_STATE_INPUT) && (menu->rowSelected >= 0) && (menu->rows != NULL) &&
	       ((menu->rows[menu->rowSelected].stringIndex & MENU_ROW_LOCKED) != 0);
}

internal void NativeMenuLock_ProcMain(struct RectMenu *menu)
{
	// A new choice in the main menu opens the race type box again as RACE
	// TYPE, not as NITRO-PIT from an earlier visit.
	if ((menu->funcState == RECTMENU_FUNC_STATE_INPUT) && (menu->rowSelected >= 0))
	{
		s_nativePitOpen = 0;
		s_nativePitRequest = NATIVE_PIT_REQUEST_NONE;

		// Any other choice than ARCADE drops the custom pick: it belongs to
		// the arcade path it was made on (platform/native_chars.c).
		if ((menu->rows != NULL) && ((menu->rows[menu->rowSelected].stringIndex & MENU_ROW_LNG_MASK) != LNG_ARCADE))
		{
			NativeChar_SetPick(-1);
		}
	}

	if (!NativeMenuLock_ChoiceIsLocked(menu) && !NativeMenuMain_Answer(menu))
	{
		MM_MenuProc_Main(menu);
	}

	NativeMenuLock_Apply();
}

internal void NativeMenuLock_Proc1p2p(struct RectMenu *menu)
{
	if (!NativeMenuLock_ChoiceIsLocked(menu))
	{
		MM_MenuProc_1p2p(menu);
	}
}

internal void NativeMenuLock_Proc2p3p4p(struct RectMenu *menu)
{
	if (!NativeMenuLock_ChoiceIsLocked(menu))
	{
		MM_MenuProc_2p3p4p(menu);
	}
}

// The race type box. SINGLE, CUP and the way back out of RACE TYPE go to the
// retail proc; NITRO-PIT and its rows are answered here.
//
// NITRO RACE IS A SINGLE RACE, so for this one call the retail proc
// is given the row of SINGLE: cup off, collapse the box,
// driver select fade-in (MM_MenuFlow.c:450-468). NITRO CUP likewise with the
// row of CUP; after the driver select then comes the cup screen, which
// MM_NativeCupSelect_Hook swaps for the copy. After that the box points
// straight at DIFFICULTY instead of PLAYERS, with one player - what 1P does
// (MM_MenuFlow.c:306-314). Back from DIFFICULTY the retail proc expands
// this box again (MM_MenuFlow.c:416-419), and it still stands as
// NITRO-PIT.
//
// Triangle in the NITRO-PIT box does NOT go to the retail proc: that would collapse
// back into the main menu (MM_MenuFlow.c:444-447). It requests RACE TYPE
// back, with the cursor on NITRO-PIT.
internal void NativeMenuLock_ProcRaceType(struct RectMenu *menu)
{
	const s16 row = menu->rowSelected;

	if (NativeMenuLock_ChoiceIsLocked(menu))
	{
		return;
	}

	if (menu->funcState == RECTMENU_FUNC_STATE_INPUT)
	{
		if (s_nativePitOpen)
		{
			// CTR goes the same way as NITRO RACE - a
			// single race with opponents, so with DIFFICULTY -, only with the
			// CTR list (MM_NativeCtr.c).
			if ((row == NATIVE_PIT_ROW_RACE) || (row == NATIVE_PIT_ROW_CUP) || (row == NATIVE_PIT_ROW_CTR))
			{
				const int cup = (row == NATIVE_PIT_ROW_CUP);

				menu->rowSelected = (s16)(cup ? 1 : 0);
				MM_MenuProc_SingleCup(menu);
				menu->rowSelected = row;

				sdata->gGT->numPlyrNextGame = 1;
				menu->ptrNextBox_InHierarchy = &D230.menuDifficulty;

				MM_NativeTrackSelect_SetChosen(cup ? 0 : ((row == NATIVE_PIT_ROW_CTR) ? MM_NATIVE_CHOSEN_CTR : MM_NATIVE_CHOSEN_RACE));
				MM_NativeCupSelect_SetChosen(cup);
				return;
			}

			// CRYSTAL: one player, without opponents, so without
			// DIFFICULTY - the retail proc gets, as with NITRO RACE, the row
			// of SINGLE (cup off), then the box goes out to the driver select,
			// as MM_MenuProc_Difficulty does it (MM_MenuFlow.c). After that the
			// track choice with the CRYSTAL list. Back from the driver select
			// this box stands as NITRO-PIT again (RECTMENU.c: the
			// deepest box of the chain loses ONLY_DRAW_TITLE).
			if (row == NATIVE_PIT_ROW_CRYSTAL)
			{
				menu->rowSelected = 0;
				MM_MenuProc_SingleCup(menu);
				menu->rowSelected = row;

				menu->state &= ~DRAW_NEXT_MENU_IN_HIERARCHY;
				menu->ptrNextBox_InHierarchy = NULL;
				sdata->gGT->numPlyrNextGame = 1;

				D230.titleMenuState = TITLE_MENU_STATE_EXITING;
				D230.desiredMenuIndex = MM_EXIT_ROUTE_CHARACTER_SELECT;

				MM_NativeTrackSelect_SetChosen(MM_NATIVE_CHOSEN_CRYSTAL);
				MM_NativeCupSelect_SetChosen(0);
				return;
			}

			if (row == -1)
			{
				s_nativePitRequest = NATIVE_PIT_REQUEST_CLOSE;
			}

			return;
		}

		if (row == NATIVE_RACE_TYPE_ROW_NITRO_PIT)
		{
			s_nativePitRequest = NATIVE_PIT_REQUEST_OPEN;
			return;
		}

		// SINGLE, CUP or back into the main menu (-1)
		MM_NativeTrackSelect_SetChosen(0);
		MM_NativeCupSelect_SetChosen(0);
	}

	MM_MenuProc_SingleCup(menu);
}

// All drivers unlocked, without changing the save data (native_memcard_adapter.c).
void NativeUnlock_ApplyToGame(void);

// THE SPYRO 2 CODE (L1+R1, down circle triangle right; D230.c:241). On the
// PS1 it started the Spyro 2 demo from the same disc; on the PC it does not
// exist, and MainKillGame_LaunchSpyro2 natively ended in an endless loop
// (MainKillGame.c:43-45), after it had already torn down music, sound and graphics
// - the game stood still. The entry in the cheat table therefore gets this proc in
// every menu frame: a log line, nothing else. MainKillGame.c
// stays as it is. Every frame, because OVR230_ResetRuntimeState resets D230.
internal void NativeMenuCheats_Spyro2(void)
{
	Platform_Log("[CTR Cheats] Spyro 2 code (L1+R1 down circle triangle right): the PC has no Spyro 2 demo - ignored\n");
}

internal void NativeMenuCheats_HookSpyro2(void)
{
	int i;

	for (i = 0; i < MM_CHEAT_COUNT; i++)
	{
		if (D230.cheats[i].handler == MainKillGame_LaunchSpyro2)
		{
			D230.cheats[i].handler = NativeMenuCheats_Spyro2;
		}
	}
}

// THE WAY BACK TO NITRO-PIT: the NITRO-PIT row in the end box
// of a container challenge (MM_NativeCrystal.c) loads the menu like QUIT and
// requests this. In the first menu frame the chain then stands as it stood after
// ARCADE -> NITRO-PIT: ARCADE chosen in the main menu - the same thing that
// MM_MenuProc_Main does for it (MM_MenuFlow.c, branch LNG_ARCADE) -, below it
// the race type box as NITRO-PIT, the cursor on CRYSTAL. With
// DRAW_NEXT_MENU_IN_HIERARCHY the title skips its intro
// (MM_MenuFlow.c, TITLE_INTRO_SKIP_FRAME).
internal void NativeMenuPit_Return(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct RectMenu *mainMenu = &D230.menuMainMenu;
	struct RectMenu *raceType = &D230.menuRaceType;
	int chosen;
	int i;

	if (((gGT->gameMode1 & MAIN_MENU) == 0) || (sdata->ptrActiveMenu != mainMenu) || (mainMenu->rows == NULL))
	{
		return;
	}

	// Which challenge requested it - the cursor stands there.
	chosen = MM_NativeCrystal_TakePitReturn();
	if (chosen == 0)
	{
		return;
	}

	for (i = 0; mainMenu->rows[i].stringIndex != RECTMENU_STRING_NONE; i++)
	{
		if ((mainMenu->rows[i].stringIndex & ~MENU_ROW_LOCKED) == LNG_ARCADE)
		{
			break;
		}
	}

	if (mainMenu->rows[i].stringIndex == RECTMENU_STRING_NONE)
	{
		Platform_LogWarn("[CTR Crystal] back to NITRO-PIT: the main menu has no ARCADE row - staying at the title\n");
		return;
	}

	gGT->gameMode1 &= ~(BATTLE_MODE | ADVENTURE_MODE | TIME_TRIAL | ADVENTURE_ARENA | ARCADE_MODE | ADVENTURE_CUP);
	gGT->gameMode2 &= ~(CUP_ANY_KIND);
	gGT->numLaps = ((gGT->gameMode2 & CHEAT_ONELAP) != 0) ? MM_ONE_LAP_CHEAT_COUNT : MM_DEFAULT_LAP_COUNT;
	gGT->gameMode1 |= ARCADE_MODE;

	mainMenu->rowSelected = (s16)i;
	mainMenu->state |= ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY;
	mainMenu->ptrNextBox_InHierarchy = raceType;

	s_nativePitOpen = 1;
	s_nativePitRequest = NATIVE_PIT_REQUEST_NONE;
	raceType->rowSelected = (chosen == MM_NATIVE_CHOSEN_CTR) ? NATIVE_PIT_ROW_CTR : NATIVE_PIT_ROW_CRYSTAL;
	raceType->state &= ~(ONLY_DRAW_TITLE | DRAW_NEXT_MENU_IN_HIERARCHY);
	raceType->ptrNextBox_InHierarchy = NULL;

	Platform_Log("[CTR Menu] back to NITRO-PIT - ARCADE -> NITRO-PIT, cursor on %s\n", (chosen == MM_NATIVE_CHOSEN_CTR) ? "CTR" : "CRYSTAL");
}

// From the top of RECTMENU_ProcessState, in every frame before the proc of the
// active box.
void NativeMenuLock_Tick(void)
{
	s_nativeMenuReasonDrawn = 0;

	// First: take the crystal bit of a container challenge out of the menu,
	// and build a requested return to NITRO-PIT.
	MM_NativeCrystal_MenuTick();
	MM_NativeCtr_MenuTick();
	NativeMenuPit_Return();

	NativeUnlock_ApplyToGame();
	NativeMenuCheats_HookSpyro2();

	NativeMenuOptions_AfterScrapbook();
	NativeGraphics_Tick();

	if (D230.menuMainMenu.funcPtr == MM_MenuProc_Main)
	{
		D230.menuMainMenu.funcPtr = NativeMenuLock_ProcMain;
	}

	if (D230.menuPlayers1P2P.funcPtr == MM_MenuProc_1p2p)
	{
		D230.menuPlayers1P2P.funcPtr = NativeMenuLock_Proc1p2p;
	}

	if (D230.menuPlayers2P3P4P.funcPtr == MM_MenuProc_2p3p4p)
	{
		D230.menuPlayers2P3P4P.funcPtr = NativeMenuLock_Proc2p3p4p;
	}

	if (D230.menuRaceType.funcPtr == MM_MenuProc_SingleCup)
	{
		D230.menuRaceType.funcPtr = NativeMenuLock_ProcRaceType;
	}

	// The track screen: the copy for CUSTOM or the original.
	MM_NativeTrackSelect_Hook();

	// The cup screen: the copy for CUSTOM CUP or the original.
	MM_NativeCupSelect_Hook();

	NativeMenuLock_Apply();
}

#endif
