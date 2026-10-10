#include <common.h>

#ifdef CTR_NATIVE

#include "platform/native_assets.h"
#include "platform/native_preview.h"

// NITRO-PIT -> NITRO RACE / CRYSTAL / CTR: THE TRACK SCREEN FOR THE
// CONTAINERS FROM tracks/.
//
// A COPY of MM_TrackSelect_MenuProc (MM_TrackSelect.c:392-941), no
// change to it. The retail menu files stay word-for-word vanilla; a hook
// in the original would break that, so the copy lives in its own file and is
// hooked in through the proc pointer.
//
// HOOKED IN through D230.menuTrackSelect.funcPtr, every frame in
// MM_NativeTrackSelect_Hook (from NativeMenuLock_Tick, at the very top of
// RECTMENU_ProcessState). The box carries DISABLE_INPUT_ALLOW_FUNCPTRS
// (D230.c:145-151): the proc IS the whole screen. MM_TrackSelect_Init
// and everything before it stays retail - the driver select calls it as always
// (MM_Characters.c:686-687).
//
// SHARED, NOT COPIED: the constants MM_TRACK_SELECT_* and
// MM_TRACK_VIDEO_* from MM_TrackSelect.c. That file stands directly before this one
// in the unity build (game_unity.h), so its enumeration is visible here. Plus
// MM_TrackSelect_Video_State, boolTrackOpen, MM_TransitionInOut, the
// lap box D230.menuLapSel and the rows and names of the containers from
// MM_NativeMenu.c.
//
// WHAT IS DIFFERENT, and only that (marked [K1] to [K9] in the code):
//
//   [K1] The rows come from tracks/. A marker of its own remembers the
//        position; sdata->trackSelBackup stays the one of the disc.
//   [K2] The name comes from the container and is shortened to the row.
//   [K3] Time trial, battle and cup are missing. The hook condition does not let them
//        in (MM_NativeTrackSelect_Applies), so their branches are not
//        here - stars, ghost data, weapon choice, ghost choice.
//   [K4] A container the reader rejected stands grey in the list.
//        A cross on it plays the lock sound (as RECTMENU.c:1062-1066).
//   [K5] The right column: title and preview window in the place the
//        disc takes with a map. The map comes from the container (icons 3
//        and 4 of its LEV, texels from its VRM) and lies in a measured
//        free VRAM strip (block THE MINIMAP). In the window
//        the recorded preview from tracks/vorschau/ runs, otherwise it shows
//        NO PREVIEW (MM_NativeTrackSelect_Preview).
//   [K6] The start: MM_NativeTracks_LoadRow, then QueueLoadTrack like the
//        original - that is how the old NITRO-PIT screen worked. Laps,
//        difficulty, driver and player count are set by the menu, and
//        QueueLoadTrack_MenuProc (QueueLoadTrack.c) leaves them alone.
//        NOT through DebugMenu_JumpToLevel: that fixes laps at 3 and
//        the player count at 1.
//   [K7] CRYSTAL: the same copy with the list of the containers
//        that declare Crystal (MM_NativeTracks_ListMode). A cross starts
//        without the lap box, and at the start MM_NativeCrystal_Arm sets the
//        bit the way the debug jump does (MM_NativeCrystal.c).
//   [K8] CTR: the same copy with the list of the containers that declare
//        CTR. Three laps fixed, no lap box; at the start
//        MM_NativeCtr_Arm sets TOKEN_RACE (MM_NativeCtr.c).
//   [K9] TIME TRIAL: a row of its own in the NITRO-PIT box; the same copy
//        with the list of NITRO RACE and the lap box, and at the start
//        MM_NativeTimeTrial_Arm (MM_NativeTimeTrial.c). NITRO RACE: the MODS
//        box directly below the lap box (MM_NativeModsBox.c), where the
//        MODE box with RACE and TIME TRIAL stood until 0.7.5. DOWN on the
//        last lap row moves the cursor into it, UP or TRIANGLE/SQUARE back
//        onto that row, CROSS opens the MODS page, which then stands alone
//        in place of this proc. Only CROSS (or CIRCLE) in the lap box starts,
//        and TRIANGLE/SQUARE there closes the lap box, as before. As long as
//        the cursor never enters the MODS box, the lap box runs exactly as
//        before. Block THE TIME TRIAL AND THE MODS BOX.

// How many characters of a name fit in a row of the wheel: the row is
// MM_TRACK_SELECT_ROW_W (256) wide, the name starts 8 inside, a character in
// FONT_BIG is 17 wide. 14 x 17 = 238 ends at 246. That is also the length of the
// longest names on the disc (SLIDE COLISEUM, HOT AIR SKYWAY).
#define MM_NATIVE_WHEEL_NAME_CHARS 14

// The reason for a grey row in the preview window (format 4.1): up to four
// lines of FONT_SMALL with at most 12 characters, with pitch 12 instead of the
// glyph height 8 - the window is 75 high.
#define MM_NATIVE_REFUSAL_TOP   12
#define MM_NATIVE_REFUSAL_PITCH 12

// Which NITRO-PIT row is chosen (MM_NATIVE_CHOSEN_*, 0 = none). Set and
// cleared only in the race type box (NativeMenuLock_ProcRaceType): NITRO RACE,
// TIME TRIAL, CRYSTAL and CTR set it; NITRO CUP, SINGLE, CUP and the way back
// clear it.
//
// The release at the title screen (MM_NativeTracks_Disarm) leaves it standing,
// on purpose: after a NITRO-PIT race CHANGE LEVEL leads back here
// and not to the disc. The container is released then, and the
// start loads it again [K6].
global_variable int s_nativeTrackSelectChosen = 0;

// [K1] The row the screen last stood on.
global_variable s16 s_nativeTrackSelectBackup = 0;

void MM_NativeTrackSelect_SetChosen(int chosen)
{
	if (chosen != s_nativeTrackSelectChosen)
	{
		Platform_Log("[CTR Menu] NITRO-PIT: %s %s\n",
		             (chosen == MM_NATIVE_CHOSEN_CRYSTAL)    ? "CRYSTAL"
		             : (chosen == MM_NATIVE_CHOSEN_CTR)      ? "CTR"
		             : (chosen == MM_NATIVE_CHOSEN_TIME_TRIAL) ? "TIME TRIAL"
		                                                     : "NITRO RACE",
		             chosen ? "chosen - track select shows tracks/" : "off - track select shows the disc");
	}

	s_nativeTrackSelectChosen = chosen;
	if (chosen)
	{
		MM_NativeTracks_SetListMode((chosen == MM_NATIVE_CHOSEN_CRYSTAL) ? NATIVE_TRACK_MODE_CRYSTAL
		                            : ((chosen == MM_NATIVE_CHOSEN_CTR) ? NATIVE_TRACK_MODE_CTR : NATIVE_TRACK_MODE_RACE));
	}
}

// The marker for code outside this file (MM_NATIVE_CHOSEN_*, 0 = none). The
// driver select asks it before the track select has set CRYSTAL_CHALLENGE or
// TOKEN_RACE: the custom characters are offered only where the marker is
// neither CRYSTAL nor CTR (NativeChar_ModeAllowed, platform/native_chars.c).
int MM_NativeTrackSelect_Chosen(void)
{
	return s_nativeTrackSelectChosen;
}

internal int MM_NativeTrackSelect_Crystal(void)
{
	return s_nativeTrackSelectChosen == MM_NATIVE_CHOSEN_CRYSTAL;
}

internal int MM_NativeTrackSelect_Ctr(void)
{
	return s_nativeTrackSelectChosen == MM_NATIVE_CHOSEN_CTR;
}

// The hook condition [K3]: a NITRO-PIT row chosen, arcade, no cup, no other
// mode. Adventure, time trial and battle come through the main menu, and
// MM_MenuProc_Main clears ARCADE_MODE on every choice there (MM_MenuFlow.c:140).
internal int MM_NativeTrackSelect_Applies(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	return (s_nativeTrackSelectChosen != 0) && ((gGT->gameMode1 & ARCADE_MODE) != 0) &&
	       ((gGT->gameMode1 & (TIME_TRIAL | BATTLE_MODE | ADVENTURE_MODE)) == 0) && ((gGT->gameMode2 & CUP_ANY_KIND) == 0);
}

// EVERY FRAME, because OVR230_ResetRuntimeState resets all of D230 on every
// return from a race, including the procs (native_menuscreen.c, block above
// NativeMenuLock_Tick). Swapping happens only between exactly these two procs.
void MM_NativeTrackSelect_Hook(void)
{
	if (MM_NativeTrackSelect_Applies())
	{
		if (D230.menuTrackSelect.funcPtr == MM_TrackSelect_MenuProc)
		{
			D230.menuTrackSelect.funcPtr = MM_NativeTrackSelect_MenuProc;
		}
	}
	else if (D230.menuTrackSelect.funcPtr == MM_NativeTrackSelect_MenuProc)
	{
		D230.menuTrackSelect.funcPtr = MM_TrackSelect_MenuProc;
	}
}

// ===========================================================================
//  [K5] THE MINIMAP.
// ===========================================================================
//
//  Retail draws the map from two icons of the menu icon set
//  (gGT->ptrIcons[mapTextureID] and +1), in six layers through UI_Map_DrawMap.
//  A container brings its map along as icons 3 and 4 of its LEV, the
//  texels in its VRM - and that is not loaded in the menu.
//  NativeTrack_ReadMinimap reads both from the file; here it gets a
//  place in the menu VRAM and two icons of its own whose layout points there.
//  Drawing uses the same lines as retail.
//
//  THE PLACE IS MEASURED, NOT ASSUMED.
//  A measurement exe recorded every write into VRAM - LoadImage,
//  MoveImage, Clear, GPU - over all menu screens, in 4:3, 16:9 and 43:18.
//  Apart from the clear at startup NOTHING writes the strip
//  x 0..511, y 264..295: above it lie the uploads from startup (y 216..263),
//  below it the second frame buffer (y 296..511), on the right the menu VRM.
//  So 32 rows are the limit for one map half.
//
//  LAYOUT OF THE STRIP. It lies in the texture pages with y base 256,
//  its rows there are v 8..39.
//    upper half      texels from x   0, page 0 (up to 128 halfwords wide)
//    lower half      texels from x 128, page 2 (up to 128 halfwords wide)
//    color tables    from x 256, row 264 (upper) and 265 (lower), each up to 256
//  A half must not go beyond its texture page: at 4 bit the
//  page is 64 halfwords wide (256 texels), at 8 bit 128.
//
//  WHAT DOES NOT FIT IS NOT DRAWN, and the log says so once with the
//  numbers.
//  No placeholder: a frame with "NO MAP" would say it again at every
//  glance.
#define MM_NATIVE_MAP_STRIP_Y 264
#define MM_NATIVE_MAP_STRIP_H 32
#define MM_NATIVE_MAP_PAGE_Y 256
#define MM_NATIVE_MAP_SLOT_W 128
#define MM_NATIVE_MAP_CLUT_X 256

global_variable const int s_nativeMapSlotX[2] = {0, 128};

enum
{
	MM_NATIVE_MAP_UNREAD = 0,
	MM_NATIVE_MAP_NONE = 1,
	MM_NATIVE_MAP_READY = 2,
};

struct MM_NativeMap
{
	int state;

	// For which file - the debug menu can re-read the folder, and
	// then a different container stands at the same row.
	char file[128];

	struct NativeTrackMinimap data;
	struct Icon icon[2];
	int clutRow[2];
};

// Read per row at the first look and then kept: reading fetches
// LEVD and VRMD from the file and checks their SHA-256, and that should not
// happen again at every step through the list.
global_variable struct MM_NativeMap *s_nativeMaps[MM_NATIVE_SELECT_MAX];

// Which row currently lies in the strip. -1 means: upload at the next draw
// - that is the state after every entry, because in between a race may have
// occupied the VRAM quite differently.
global_variable int s_nativeMapUploaded = -1;

// Gives the icons their layout in the strip. 0 with a reason if a half does not
// fit.
internal int MM_NativeTrackSelect_MapPlace(struct MM_NativeMap *map, const char **why)
{
	int k;

	for (k = 0; k < 2; k++)
	{
		const struct NativeTrackMinimapHalf *half = &map->data.half[k];
		const int per = (half->depth == 0) ? 4 : ((half->depth == 1) ? 2 : 1);
		const int pageW = 256 / per;
		const int maxW = (pageW < MM_NATIVE_MAP_SLOT_W) ? pageW : MM_NATIVE_MAP_SLOT_W;
		struct TextureLayout *t = &map->icon[k].texLayout;
		const int slotPage = s_nativeMapSlotX[k] / 64;
		const int dv = MM_NATIVE_MAP_STRIP_Y - MM_NATIVE_MAP_PAGE_Y - half->texelBaseV;

		if (half->h > MM_NATIVE_MAP_STRIP_H)
		{
			*why = "a map half is higher than the free VRAM strip (32 rows)";
			return 0;
		}

		if (half->w > maxW)
		{
			*why = "a map half is wider than its texture page slot";
			return 0;
		}

		memcpy(t, &half->layout[0], sizeof(*t));

		t->u0 = (u8)(t->u0 - half->texelBaseU);
		t->u1 = (u8)(t->u1 - half->texelBaseU);
		t->u2 = (u8)(t->u2 - half->texelBaseU);
		t->u3 = (u8)(t->u3 - half->texelBaseU);
		t->v0 = (u8)(t->v0 + dv);
		t->v1 = (u8)(t->v1 + dv);
		t->v2 = (u8)(t->v2 + dv);
		t->v3 = (u8)(t->v3 + dv);

		// Page and y base new, color depth and semi-transparency as in the LEV.
		t->tpage = (u16)((t->tpage & ~(u16)0x081f) | (u16)slotPage | (u16)0x10);

		// If both halves share a color table, the lower one gets no
		// row of its own.
		map->clutRow[k] = MM_NATIVE_MAP_STRIP_Y + k;

		if ((k == 1) && (half->clutW > 0) && (half->clutX == map->data.half[0].clutX) && (half->clutY == map->data.half[0].clutY))
		{
			map->clutRow[k] = MM_NATIVE_MAP_STRIP_Y;
		}

		t->clut = (u16)((map->clutRow[k] << 6) | (MM_NATIVE_MAP_CLUT_X >> 4));

		map->icon[k].global_IconArray_Index = 3 + k;
		memcpy(&map->icon[k].name[0], "custom-map", sizeof("custom-map"));
	}

	return 1;
}

// The map for a row, or NULL if there is none.
internal struct MM_NativeMap *MM_NativeTrackSelect_Map(int row)
{
	const int scan = MM_NativeTracks_RowIndex(row);
	const struct NativeTrackEntry *entry = NativeTrack_Get(scan);
	struct MM_NativeMap *map;

	if ((entry == NULL) || (row < 0) || (row >= MM_NATIVE_SELECT_MAX))
	{
		return NULL;
	}

	map = s_nativeMaps[row];

	if ((map != NULL) && (strcmp(map->file, entry->file) != 0))
	{
		NativeTrack_FreeMinimap(&map->data);
		memset(map, 0, sizeof(*map));

		if (s_nativeMapUploaded == row)
		{
			s_nativeMapUploaded = -1;
		}
	}

	if (map == NULL)
	{
		map = (struct MM_NativeMap *)calloc(1, sizeof(*map));

		if (map == NULL)
		{
			return NULL;
		}

		s_nativeMaps[row] = map;
	}

	if (map->state == MM_NATIVE_MAP_UNREAD)
	{
		const char *why = NULL;

		snprintf(map->file, sizeof(map->file), "%s", entry->file);

		if (!NativeTrack_ReadMinimap(scan, &map->data, &why))
		{
			map->state = MM_NATIVE_MAP_NONE;
			Platform_Log("[CTR Menu] CUSTOM minimap '%s': none - %s\n", entry->name, why);
		}
		else if (!MM_NativeTrackSelect_MapPlace(map, &why))
		{
			map->state = MM_NATIVE_MAP_NONE;
			Platform_Log("[CTR Menu] CUSTOM minimap '%s': not shown - %s (halves %dx%d and %dx%d VRAM halfwords, %d bit, from %s, read in %u us)\n",
			             entry->name, why, map->data.half[0].w, map->data.half[0].h, map->data.half[1].w, map->data.half[1].h,
			             (map->data.half[0].depth == 0) ? 4 : ((map->data.half[0].depth == 1) ? 8 : 15),
			             map->data.scaled ? "the LEV, scaled in the game" : "the LEV", map->data.microseconds);
			NativeTrack_FreeMinimap(&map->data);
		}
		else
		{
			map->state = MM_NATIVE_MAP_READY;
			Platform_Log("[CTR Menu] CUSTOM minimap '%s': halves %dx%d and %dx%d VRAM halfwords, %d bit, color tables %d+%d, from %s, read in %u us - "
			             "placed in the strip x 0..511, y %d..%d\n",
			             entry->name, map->data.half[0].w, map->data.half[0].h, map->data.half[1].w, map->data.half[1].h,
			             (map->data.half[0].depth == 0) ? 4 : ((map->data.half[0].depth == 1) ? 8 : 15), map->data.half[0].clutW,
			             (map->clutRow[1] == map->clutRow[0]) ? 0 : map->data.half[1].clutW,
			             map->data.scaled ? "the LEV, scaled in the game" : "the LEV", map->data.microseconds, MM_NATIVE_MAP_STRIP_Y,
			             MM_NATIVE_MAP_STRIP_Y + MM_NATIVE_MAP_STRIP_H - 1);
		}
	}

	return (map->state == MM_NATIVE_MAP_READY) ? map : NULL;
}

// Puts the map into the strip if a different one currently lies there.
// LoadImage2 makes it visible immediately (NativeRenderer_UpdateVRAM), like the
// upload of the preview video (native_str.c:803).
internal void MM_NativeTrackSelect_MapUpload(struct MM_NativeMap *map, int row)
{
	int k;

	if (s_nativeMapUploaded == row)
	{
		return;
	}

	for (k = 0; k < 2; k++)
	{
		const struct NativeTrackMinimapHalf *half = &map->data.half[k];
		RECT texels;

		texels.x = (s16)s_nativeMapSlotX[k];
		texels.y = MM_NATIVE_MAP_STRIP_Y;
		texels.w = (s16)half->w;
		texels.h = (s16)half->h;
		LoadImage2(&texels, half->texels);

		if (half->clutW > 0)
		{
			RECT clut;

			clut.x = MM_NATIVE_MAP_CLUT_X;
			clut.y = (s16)map->clutRow[k];
			clut.w = (s16)half->clutW;
			clut.h = 1;
			LoadImage2(&clut, (void *)&half->clut[0]);
		}
	}

	s_nativeMapUploaded = row;
}

// ===========================================================================
//  [K9] THE TIME TRIAL AND THE MODS BOX.
// ===========================================================================
//
//  TIME TRIAL is the row of the NITRO-PIT box that sets
//  MM_NATIVE_CHOSEN_TIME_TRIAL: the list of NITRO RACE, the lap box, and
//  MM_NativeTrackSelect_Mode answers TIME TRIAL - the start arms it
//  (MM_NativeTimeTrial_Arm). CRYSTAL and CTR start without the lap box ([K7],
//  [K8]); there and in NITRO RACE the mode is RACE.
//
//  THE MODS BOX (MM_NativeModsBox.c, NativeMods_MenuOffered) only in NITRO
//  RACE - a time trial has no CPU seats.
//
//  THE PLACE. The lap box of the disc (D230.menuLapSel, D230.c:594-603):
//  small font, centred on x 0x18C, rows from y 0x7c, drawn with the width
//  MM_TRACK_SELECT_LAP_MENU_WIDTH on trackSelect_lapMenuTransition. RECTMENU
//  gives its frame the height of its content plus frameExtraH, minus one
//  for the small font (RECTMENU.c:817-830): title 8 + 6, three rows of 8,
//  so 38 + 8 - 1 = 45, frame y 120..164, shadow to 170 (RECTMENU.c:433-451).
//  The MODS box takes the same x, the same width and the same transition,
//  and its frame begins below that shadow with MM_NATIVE_MODE_AIR rows of
//  air: title and one row, 22 + 8 - 1 = 29, frame y 173..201, shadow to 207
//  - as on the disc. The preview window above ends at 120 with its shadow,
//  the wheel on the left at x 288; title and map are not drawn while the lap
//  box is open.
//
//  THE BOX WITHOUT THE CURSOR keeps its marked row: RECTMENU draws it as
//  always, only with the highlight colour held still and dimmer for this
//  one call (MM_NATIVE_MODE_STILL_MARK). The pulsing bar
//  (MainFrame_RenderFrame.c:450) stays the sign of the cursor.
//
//  INPUT. The lap box keeps RECTMENU_ProcessInput unchanged; it alone
//  starts. Only the step into the MODS box is taken before it, with what
//  RECTMENU itself would read (RECTMENU.c:913-952): player one, no L1/R1
//  held, UP before DOWN, no confirm or back key in the same frame, and the
//  lap box already the active box - a box entered anew clears the input in
//  its first frame (RECTMENU.c:900-911). Down on the last lap row stays where
//  it is in RECTMENU (D230.rowsLapSel, D230.c:592), silent, so that key is
//  free. The step plays the cursor sound of RECTMENU (RECTMENU.c:960). In the
//  MODS box the keys go to MM_NativeModsBox_NitroKeys: UP or
//  TRIANGLE/SQUARE back to the last lap row - the row the cursor came from -,
//  CROSS opens the MODS page.
#define MM_NATIVE_MODE_AIR 2

#define MM_NATIVE_MODE_STILL_MARK MakeColorPacked(0x40, 0x20, 0)

// Which box holds the cursor. LAPS at every opening of the lap box.
enum
{
	MM_NATIVE_FOCUS_LAPS = 0,
	MM_NATIVE_FOCUS_MODS = 1,
};

global_variable int s_nativeLapFocus = MM_NATIVE_FOCUS_LAPS;

int MM_NativeTrackSelect_Mode(void)
{
	return (s_nativeTrackSelectChosen == MM_NATIVE_CHOSEN_TIME_TRIAL) ? MM_NATIVE_MODE_TIME_TRIAL : MM_NATIVE_MODE_RACE;
}

internal s16 MM_NativeTrackSelect_LastRow(const struct RectMenu *box)
{
	s16 last = 0;

	while (box->rows[last + 1].stringIndex != RECTMENU_STRING_NONE)
	{
		last++;
	}

	return last;
}

// Back from the MODS box to the last lap row. The lap box becomes the
// active box again (RECTMENU.c:902-910), so its next frame does not clear
// the input as a box entered anew would.
internal void MM_NativeTrackSelect_BackToLaps(void)
{
	s_nativeLapFocus = MM_NATIVE_FOCUS_LAPS;
	D230.menuLapSel.rowSelected = MM_NativeTrackSelect_LastRow(&D230.menuLapSel);
	sdata->activeSubMenu = &D230.menuLapSel;
}

// The step into the MODS box, before RECTMENU_ProcessInput sees the key.
internal void MM_NativeTrackSelect_ModsStep(void)
{
	const u32 button = sdata->buttonTapPerPlayer[0];

	// A box entered anew clears the input in its first frame (RECTMENU.c:902-910).
	if ((s_nativeLapFocus != MM_NATIVE_FOCUS_LAPS) || (sdata->activeSubMenu != &D230.menuLapSel))
	{
		return;
	}

	if (((button & RECTMENU_INPUT_MENU) == 0) || ((sdata->buttonHeldPerPlayer[0] & (BTN_L1 | BTN_R1)) != 0) ||
	    ((button & (BTN_CROSS_one | BTN_CIRCLE | BTN_TRIANGLE | BTN_SQUARE_one)) != 0))
	{
		return;
	}

	if (((button & BTN_UP) != 0) || ((button & BTN_DOWN) == 0) || (D230.menuLapSel.rowSelected != MM_NativeTrackSelect_LastRow(&D230.menuLapSel)))
	{
		return;
	}

	s_nativeLapFocus = MM_NATIVE_FOCUS_MODS;
	OtherFX_Play(0, 1);
	RECTMENU_ClearInput();
}

// The lap box or the MODS box. The one with the cursor exactly as RECTMENU
// draws it; the other with the still mark.
internal void MM_NativeTrackSelect_DrawBox(struct RectMenu *box, int focused)
{
	const int posX = D230.trackTransitions.named.trackSelect_lapMenuTransition.currX;
	const s16 posY = D230.trackTransitions.named.trackSelect_lapMenuTransition.currY;
	Color pulse;

	if (focused)
	{
		RECTMENU_DrawSelf(box, posX, posY, MM_TRACK_SELECT_LAP_MENU_WIDTH);
		return;
	}

	pulse = sdata->menuRowHighlight_Normal;
	ColorCode_SetPacked(&sdata->menuRowHighlight_Normal, MM_NATIVE_MODE_STILL_MARK);
	RECTMENU_DrawSelf(box, posX, posY, MM_TRACK_SELECT_LAP_MENU_WIDTH);
	sdata->menuRowHighlight_Normal = pulse;
}

// [K1] The first frame after MM_TrackSelect_Init.
//
// Init sets the pointer to sdata->trackSelBackup and searches
// D230.arcadeTracks for an open track. Both belong to the disc. Here
// the containers and our own marker take their place.
internal void MM_NativeTrackSelect_Enter(struct RectMenu *menu)
{
	const int count = MM_NativeTracks_BuildRows();

	if (s_nativeTrackSelectBackup >= count)
	{
		s_nativeTrackSelectBackup = 0;
	}

	menu->rowSelected = s_nativeTrackSelectBackup;
	D230.trackSelect.currentTrack = s_nativeTrackSelectBackup;

	// [K5] Put the map into the strip anew at the first draw.
	s_nativeMapUploaded = -1;

	// [K9]
	s_nativeLapFocus = MM_NATIVE_FOCUS_LAPS;

	Platform_Log("[CTR Menu] CUSTOM track select: %d container(s), cursor on %d\n", count, (int)s_nativeTrackSelectBackup);
}

// [K6] What the menu has set, at the moment of the hand-over to QueueLoadTrack.
// The counter-evidence in the race is the [CTR Race] line (DebugMenu.c).
internal void MM_NativeTrackSelect_LogStart(int row)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct NativeTrackEntry *entry = NativeTrack_Get(MM_NativeTracks_RowIndex(row));

	// currLEV carries the run-time ID here; LoadRow has just chosen the slot.
	Platform_Log("[CTR Menu] CUSTOM start: '%s' with level id %d on seat %d, driver %d, difficulty %d, laps %d, players %d - via QueueLoadTrack\n",
	             (entry != NULL) ? entry->name : "?", (int)gGT->currLEV, NativeTrack_LoadedDonorLevel(), (int)data.characterIDs[0],
	             (int)gGT->arcadeDifficulty, (int)gGT->numLaps, (int)gGT->numPlyrNextGame);
}

// [K5] PREVIEW. The same timing as the STR video of the
// disc: MM_TrackSelect_Video_State waits 21 frames after every row change,
// then START_STREAM. The frames come from
// tracks/vorschau/<container>.rldprev (platform/native_preview.c), to the
// place in VRAM where retail puts its video (icon 0x3f,
// MM_TrackSelect.c:186-190), and are drawn with the same 16-bit quad
// (MM_TrackSelect_Video_DrawNativePreview). 15 frames/s: every
// second menu frame a new one. Without a valid preview - no file, foreign
// version or a different LEVD hash - the window shows NO PREVIEW.
// Drawn BEFORE the opaque box, like the reason for a grey row.
global_variable int s_nativePreviewScan = -1;
global_variable int s_nativePreviewOk = 0;
global_variable int s_nativePreviewTick = 0;

internal void MM_NativeTrackSelect_Preview(RECT *previewRect, int scan)
{
	struct GameTracker *gGT = sdata->gGT;

	if (D230.trackSelect.videoStateCurr == MM_TRACK_VIDEO_ICON)
	{
		if (s_nativePreviewScan != -1)
		{
			NativePreview_Close();
			s_nativePreviewScan = -1;
		}
		return;
	}

	if (s_nativePreviewScan != scan)
	{
		s_nativePreviewScan = scan;
		s_nativePreviewOk = NativePreview_Open(scan);
		s_nativePreviewTick = 0;
	}

	if (s_nativePreviewOk)
	{
		u16 tpage = gGT->ptrIcons[MM_TRACK_VIDEO_ICON_INDEX]->texLayout.tpage;
		u8 u0 = gGT->ptrIcons[MM_TRACK_VIDEO_ICON_INDEX]->texLayout.u0;
		u8 v0 = gGT->ptrIcons[MM_TRACK_VIDEO_ICON_INDEX]->texLayout.v0;
		int srcX = (u16)u0 + (tpage & 0xf) * 0x40 + MM_TRACK_VIDEO_FRAME_SRC_OFFSET_X;
		int srcY = (u16)v0 + (tpage & 0x10) * 0x10 + (s16)(((u32)tpage & 0x800) >> 2) + MM_TRACK_VIDEO_FRAME_SRC_OFFSET_Y;

		if (NativePreview_Upload(s_nativePreviewTick / 2, srcX, srcY))
		{
			s_nativePreviewTick++;
			D230.trackSelect.videoStateCurr = MM_TRACK_VIDEO_PLAYING;
			MM_TrackSelect_Video_DrawNativePreview(previewRect, srcX, srcY);
			return;
		}

		s_nativePreviewOk = 0;
	}

	DecalFont_DrawLine("NO PREVIEW", previewRect->x + (MM_TRACK_VIDEO_WIDTH / 2), previewRect->y + (MM_TRACK_VIDEO_HEIGHT / 2) - 4, FONT_SMALL,
	                   (JUSTIFY_CENTER | WHITE));
}

void MM_NativeTrackSelect_MenuProc(struct RectMenu *menu)
{
	struct GameTracker *gGT = sdata->gGT;
	s16 elapsedFrames = D230.trackSelect.transition.frame;

	// [K9] The MODS page stands alone while it is open.
	if (MM_NativeModsBox_PageFrame())
	{
		return;
	}

	// [K1] Init has set exactly this state (MM_TrackSelect.c:354, :360),
	// and only the first frame after it sees it: after that ENTERING_MENU counts
	// down.
	if ((D230.trackSelect.transition.state == ENTERING_MENU) && (elapsedFrames == MM_TRACK_SELECT_TRANSITION_FRAMES))
	{
		MM_NativeTrackSelect_Enter(menu);
	}

	// WITHOUT CONTAINERS THERE IS NO WHEEL. The search for the next open
	// row would run forever. The NITRO-PIT row is already locked then
	// (NativeMenuLock_Apply); this only catches what gets here anyway.
	if (MM_NativeTracks_Count() <= 0)
	{
		Platform_LogWarn("[CTR Menu] CUSTOM track select without a container - back to character select\n");
		sdata->ptrDesiredMenu = &D230.menuCharacterSelect;
		MM_Characters_RestoreIDs();
		return;
	}

	// if you are not in track selection menu
	if (D230.trackSelect.transition.state != IN_MENU)
	{
		// if transitioning in
		if (D230.trackSelect.transition.state == ENTERING_MENU)
		{
			// make error message posY appear
			// near bottom of screen
			sdata->errorMessagePosIndex = 1;

			MM_TransitionInOut(D230.trackTransitions.transitionMeta_trackSel, elapsedFrames, MM_TRACK_SELECT_SLIDE_FRAMES);

			// ran out of frames
			if (elapsedFrames == 0)
			{
				// menu is now in focus
				D230.trackSelect.transition.state = IN_MENU;
			}
			else
			{
				elapsedFrames--;
			}
		}
		// transitioning out
		else if (D230.trackSelect.transition.state == EXITING_MENU)
		{
			MM_TransitionInOut(D230.trackTransitions.transitionMeta_trackSel, elapsedFrames, MM_TRACK_SELECT_SLIDE_FRAMES);
			elapsedFrames++;

			if (elapsedFrames > MM_TRACK_SELECT_TRANSITION_FRAMES)
			{
				sdata->errorMessagePosIndex = 0;

				// if track has not been chosen
				if (D230.trackSelect.transition.startAfterExit == 0)
				{
					// return to character selection
					sdata->ptrDesiredMenu = &D230.menuCharacterSelect;
					MM_Characters_RestoreIDs();
					return;
				}

				// [K6] The container is only loaded now, when the track is
				// really chosen. If that fails, it goes back to the
				// driver select, and the reason is in the log.
				if (!MM_NativeTracks_LoadRowFor(MM_NativeTracks_RowIndex((int)menu->rowSelected), MM_NativeTracks_ListMode()))
				{
					sdata->ptrDesiredMenu = &D230.menuCharacterSelect;
					MM_Characters_RestoreIDs();
					return;
				}

				MM_NativeTrackSelect_LogStart((int)menu->rowSelected);

				// [K7] CRYSTAL: the bit as the debug jump sets it (MM_NativeCrystal.c).
				if (MM_NativeTrackSelect_Crystal())
				{
					MM_NativeCrystal_Arm();
				}

				// [K8] CTR: TOKEN_RACE and three laps (MM_NativeCtr.c).
				if (MM_NativeTrackSelect_Ctr())
				{
					MM_NativeCtr_Arm();
				}

				// [K9] TIME TRIAL from the NITRO-PIT box (MM_NativeTimeTrial.c).
				if (MM_NativeTrackSelect_Mode() == MM_NATIVE_MODE_TIME_TRIAL)
				{
					Platform_Log("[CTR Menu] CUSTOM start: mode TIME TRIAL - MM_NativeTimeTrial_Arm\n");
					MM_NativeTimeTrial_Arm();
				}

				// passthrough Menu for the function
				// QueueLoadTrack
				sdata->ptrDesiredMenu = &data.menuQueueLoadTrack;

				// make error message posY appear
				// near middle of screen
				sdata->errorMessagePosIndex = 0;

				return;
			}
		}
	}
	D230.trackSelect.transition.frame = elapsedFrames;

	// [K1]
	struct MainMenu_LevelRow *selectMenu = MM_NativeTracks_Rows();
	s16 numTracks = (s16)MM_NativeTracks_Count();

	s16 currTrack = menu->rowSelected;
	s_nativeTrackSelectBackup = currTrack;

	// if lap selection menu is closed
	if (D230.trackSelect.lapBoxOpen == 0)
	{
		int importantButton = sdata->buttonTapPerPlayer[0] & MM_TRACK_SELECT_INPUT;

		// [K9] Every opening of the lap box begins in LAPS.
		s_nativeLapFocus = MM_NATIVE_FOCUS_LAPS;

		if (
		    // if not changing levels
		    (D230.trackSelect.trackChangeFrames == 0) &&

		    // only check buttons if IN_MENU
		    (D230.trackSelect.transition.state == IN_MENU) &&

		    // desired button pressed
		    (importantButton != 0))
		{
			switch (importantButton)
			{
			case BTN_UP:

				// look for unlocked track
				do
				{
					currTrack--;

					// if index is negative
					if (currTrack < 0)
					{
						// set to the last track
						currTrack = numTracks - 1;
					}

				} while (!MM_TrackSelect_boolTrackOpen(&selectMenu[currTrack]));

				D230.trackSelect.currentTrack = currTrack;
				D230.trackSelect.trackChangeFrames = MM_TRACK_SELECT_TRACK_CHANGE_FRAMES;
				D230.trackSelect.trackChangeDirection = 1;

				OtherFX_Play(0, 1);
				break;

			case BTN_DOWN:

				// look for unlocked track
				do
				{
					currTrack++;

					// if you go beyond max number of tracks
					if (currTrack >= numTracks)
					{
						// set to the first track
						currTrack = 0;
					}

				} while (!MM_TrackSelect_boolTrackOpen(&selectMenu[currTrack]));

				D230.trackSelect.currentTrack = currTrack;
				D230.trackSelect.trackChangeFrames = MM_TRACK_SELECT_TRACK_CHANGE_FRAMES;
				D230.trackSelect.trackChangeDirection = -1;

				OtherFX_Play(0, 1);
				break;

			case BTN_CROSS_one:
			case BTN_CIRCLE:

				// [K4]
				if (!MM_NativeTracks_RowOk(currTrack))
				{
					OtherFX_Play(5, 1);
					break;
				}

				// "enter/confirm" sound
				OtherFX_Play(1, 1);

				// [K7] CRYSTAL has no laps, [K8] CTR drives a fixed three like
				// retail: go right away, like the lap box with "start"
				// (lapSelTransitionState 1).
				if (MM_NativeTrackSelect_Crystal() || MM_NativeTrackSelect_Ctr())
				{
					D230.trackSelect.transition.state = EXITING_MENU;
					D230.trackSelect.transition.startAfterExit = 1;
					break;
				}

				// [K3] Only arcade gets in, so always the lap choice.
				D230.trackSelect.lapBoxOpen = D230.trackSelect.transition.state;
				break;

			case BTN_TRIANGLE:
			case BTN_SQUARE_one:

				// "go back" sound
				OtherFX_Play(2, 1);

				D230.trackSelect.transition.startAfterExit = 0;
				D230.trackSelect.transition.state = EXITING_MENU;
				break;
			default:
				break;
			}

			// clear gamepad input (for menus)
			RECTMENU_ClearInput();
		}
	}

	// if lap selection menu is open
	else
	{
		s16 lapSelTransitionState = 0;

		// copy LapRow from 8d920 to temp variable b55ae
		D230.menuLapSel.rowSelected = sdata->uselessLapRowCopy;

		// [K9] The MODS box only where it is offered (NITRO RACE); elsewhere
		// the cursor never leaves LAPS.
		const int modsOffered = MM_NativeModsBox_NitroOffered();

		if (!modsOffered)
		{
			s_nativeLapFocus = MM_NATIVE_FOCUS_LAPS;
		}

		// If you're in track selection menu
		if (D230.trackSelect.transition.state == IN_MENU)
		{
			// [K9]
			if (modsOffered)
			{
				MM_NativeTrackSelect_ModsStep();
			}

			if (s_nativeLapFocus == MM_NATIVE_FOCUS_LAPS)
			{
				lapSelTransitionState = RECTMENU_ProcessInput(&D230.menuLapSel);
			}
			else if (MM_NativeModsBox_NitroKeys())
			{
				// Neither starts nor closes: back onto the last lap row.
				MM_NativeTrackSelect_BackToLaps();
			}
		}

		// [K9] With the cursor in LAPS the same call as before.
		MM_NativeTrackSelect_DrawBox(&D230.menuLapSel, s_nativeLapFocus == MM_NATIVE_FOCUS_LAPS);

		if (modsOffered)
		{
			MM_NativeModsBox_DrawNitroBox(s_nativeLapFocus == MM_NATIVE_FOCUS_MODS);
		}

		// put LapRow back into 8d920
		sdata->uselessLapRowCopy = D230.menuLapSel.rowSelected;

		// get lap count
		gGT->numLaps = D230.lapCountByRow[D230.menuLapSel.rowSelected].lapCount;

		// if it is time to start the race
		if (lapSelTransitionState == 1)
		{
			// try to start the race
			D230.trackSelect.transition.state = EXITING_MENU;

			// if this is 1 (which it is), the race starts,
			// otherwise, you go back to character selection
			D230.trackSelect.transition.startAfterExit = lapSelTransitionState;
		}

		// If it is not time to start the race
		else
		{
			if (lapSelTransitionState == -1)
			{
				// close lap selection menu
				D230.trackSelect.lapBoxOpen = 0;
			}
		}

		// If "One Lap Race" Cheat is enabled
		if ((gGT->gameMode2 & CHEAT_ONELAP) != 0)
		{
			// Set number of Laps to 1
			gGT->numLaps = 1;
		}
	}

	// decrease frame from track list motion
	int trackChangeFrames = D230.trackSelect.trackChangeFrames + -1;
	if ((0 < D230.trackSelect.trackChangeFrames) && (D230.trackSelect.trackChangeFrames = trackChangeFrames, trackChangeFrames == 0))
	{
		menu->rowSelected = D230.trackSelect.currentTrack;
	}

	// not transitioning
	b32 resetPreviewVideo = false;

	// if you are transitioning out of level selection
	if ((D230.trackSelect.trackChangeFrames != 0) || (D230.trackSelect.transition.state == EXITING_MENU))
	{
		// transitioning,
		// which means stop drawing track video,
		// just draw icon
		resetPreviewVideo = true;
	}

	MM_TrackSelect_Video_State(resetPreviewVideo);

	// The run-time ID of the row (65..99, NativeTrack_LevelForIndex), or -1
	// for a row without an ID. QueueLoadTrack only passes it through; it becomes the
	// donor slot only at the funnel MainRaceTrack_RequestLoad
	// (MM_NativeTracks_TranslateLevel). currLEV is nowhere read as a
	// table index - that is why the number may lie outside the
	// retail band here.
	gGT->currLEV = selectMenu[menu->rowSelected].levID;
	s32 scanTrack = (int)menu->rowSelected + -1;

	for (s32 hiddenRowIndex = 0; hiddenRowIndex < MM_TRACK_SELECT_CENTER_ROW; hiddenRowIndex++)
	{
		b32 trackOpen;

		do
		{
			currTrack = (s16)scanTrack;
			if (scanTrack < 0)
			{
				currTrack = numTracks - 1;
			}

			trackOpen = MM_TrackSelect_boolTrackOpen(&selectMenu[currTrack]);

			scanTrack = currTrack - 1;
		} while (!trackOpen);
	}

	s32 rowIndex = 0;
	s32 rowPhase = 0;

	// loop through tracks in track list
	do
	{
		// This part actually "moves" the rows,
		// when pressing the Up and Down buttons on D-Pad
		u32 rowAngle = ((rowPhase >> 0x10) + -MM_TRACK_SELECT_CENTER_ROW) * MM_TRACK_SELECT_ROW_ANGLE_STEP;
		if (0 < D230.trackSelect.trackChangeFrames)
		{
			rowAngle = rowAngle + (((MM_TRACK_SELECT_TRACK_CHANGE_FRAMES - D230.trackSelect.trackChangeFrames) * MM_TRACK_SELECT_ROW_ANGLE_STEP) /
			                       MM_TRACK_SELECT_TRACK_CHANGE_FRAMES) *
			                          (int)D230.trackSelect.trackChangeDirection;
		}

		RECT rowRect;
		rowRect.w = MM_TRACK_SELECT_ROW_W;
		rowRect.h = MM_TRACK_SELECT_ROW_H;

		// posX of track list
		s32 rowX = (u32)D230.trackTransitions.named.trackSelect_rowListTransition.currX +
		           (MATH_Cos(rowAngle) * MM_TRACK_SELECT_ROW_X_RADIUS >> MM_TRACK_SELECT_ROW_X_SHIFT) + MM_TRACK_SELECT_ROW_X_OFFSET;

		// posY of track list
		s32 rowBaseY = (u32)D230.trackTransitions.named.trackSelect_rowListTransition.currY +
		               (MATH_Sin(rowAngle) * MM_TRACK_SELECT_ROW_Y_RADIUS >> MM_TRACK_SELECT_ROW_Y_SHIFT);

		s16 rowY = (s16)rowBaseY + MM_TRACK_SELECT_ROW_Y_OFFSET;
		rowRect.x = (s16)rowX;
		rowRect.y = rowY;

		// [K2] [K4]
		DecalFont_DrawLine(MM_NativeTracks_RowName(currTrack, MM_NATIVE_WHEEL_NAME_CHARS), (rowX + MM_TRACK_SELECT_ROW_NAME_X_OFFSET),
		                   (rowBaseY + MM_TRACK_SELECT_ROW_NAME_Y_OFFSET), FONT_BIG, MM_NativeTracks_RowOk(currTrack) ? ORANGE : GRAY);

		if ((D230.trackSelect.trackChangeFrames == 0) && ((s16)rowIndex == MM_TRACK_SELECT_CENTER_ROW))
		{
			RECT highlightRect;
			struct NativeTrackRefusalText refusal;

			// Format 4.1: a grey row says WHY - small and red below the
			// name, in the place where the disc writes "GHOST DATA EXISTS"
			// (MM_TrackSelect.c:807-809). The full reason stands in the
			// preview window.
			if (NativeTrack_WhyNotOffered(MM_NativeTracks_RowIndex(currTrack), MM_NativeTracks_ListMode(), &refusal) != NULL)
			{
				DecalFont_DrawLine((char *)refusal.tag, (rowX + MM_TRACK_SELECT_ROW_NAME_X_OFFSET + MM_TRACK_SELECT_GHOST_TEXT_FROM_NAME_X),
				                   (rowBaseY + MM_TRACK_SELECT_GHOST_TEXT_Y_OFFSET), FONT_SMALL, (JUSTIFY_CENTER | RED));
			}

			highlightRect.x = rowRect.x + MM_TRACK_SELECT_HIGHLIGHT_INSET_X;
			highlightRect.y = rowRect.y + MM_TRACK_SELECT_HIGHLIGHT_INSET_Y;
			highlightRect.w = rowRect.w - MM_TRACK_SELECT_HIGHLIGHT_W_SHRINK;
			highlightRect.h = rowRect.h - MM_TRACK_SELECT_HIGHLIGHT_H_SHRINK;

			CTR_Box_DrawClearBox(&highlightRect, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, gGT->backBuffer->otMem.uiOT);
		}

		// Draw 2D Menu rectangle background
		RECTMENU_DrawInnerRect(&rowRect, 0, gGT->backBuffer->otMem.uiOT);

		b32 trackOpen;

		do
		{
			currTrack++;

			if (numTracks <= currTrack)
			{
				currTrack = 0;
			}
			trackOpen = MM_TrackSelect_boolTrackOpen(&selectMenu[currTrack]);

		} while (!trackOpen);

		rowIndex = rowIndex + 1;
		rowPhase = rowIndex * MM_TRACK_SELECT_ROW_PHASE_UNIT;
		if (MM_TRACK_SELECT_VISIBLE_ROWS <= rowIndex)
		{
			RECT previewRect;
			previewRect.w = MM_TRACK_VIDEO_WIDTH;
			previewRect.h = MM_TRACK_VIDEO_HEIGHT;

			// posX of "SELECT LEVEL"
			previewRect.x = D230.trackTransitions.named.trackSelect_previewTransition.currX + MM_TRACK_SELECT_PREVIEW_X;

			// [K5] The place WITH a map, although there is none yet. Without a map
			// the disc puts the window at y 92..167, and with the lap choice open
			// its box stands there - on the disc the two never
			// meet, because all 18 arcade tracks have a map.
			previewRect.y = D230.trackTransitions.named.trackSelect_previewTransition.currY + MM_TRACK_SELECT_PREVIEW_Y_WITH_MAP;

			// If the lap selection menu is closed
			if (D230.trackSelect.lapBoxOpen == 0)
			{
				DecalFont_DrawLine(sdata->lngStrings[LNG_SELECT_LEVEL_SELECT],
				                   (D230.trackTransitions.named.trackSelect_titleTransition.currX + MM_TRACK_SELECT_TITLE_X),
				                   (D230.trackTransitions.named.trackSelect_titleTransition.currY + (u32)previewRect.y), FONT_BIG, (JUSTIFY_CENTER | ORANGE));

				DecalFont_DrawLine(sdata->lngStrings[LNG_LEVEL], (D230.trackTransitions.named.trackSelect_titleTransition.currX + MM_TRACK_SELECT_TITLE_X),
				                   (D230.trackTransitions.named.trackSelect_titleTransition.currY + (u32)previewRect.y + MM_TRACK_SELECT_LEVEL_TEXT_Y_STEP),
				                   FONT_BIG, (JUSTIFY_CENTER | ORANGE));
			}

			// next, draw the map icon, below "SELECT LEVEL",
			// exactly 0x22 (34) pixels below the text
			previewRect.y += MM_TRACK_SELECT_TITLE_TO_MAP_Y;

			// [K5] The map from the container instead of the menu icon set,
			// otherwise the same lines as MM_TrackSelect.c:876-933.
			struct MM_NativeMap *nativeMap = MM_NativeTrackSelect_Map((int)menu->rowSelected);

			if ((nativeMap != NULL) &&

			    // If lap selection menu is closed
			    (D230.trackSelect.lapBoxOpen == 0))
			{
				MM_NativeTrackSelect_MapUpload(nativeMap, (int)menu->rowSelected);

				struct Icon *iconMap0 = &nativeMap->icon[0];
				struct Icon *iconMap1 = &nativeMap->icon[1];

				// icon data
				u8 mapTopV2 = iconMap0->texLayout.v2;
				u8 mapTopV0 = iconMap0->texLayout.v0;
				u8 mapBottomV2 = iconMap1->texLayout.v2;
				u8 mapBottomV0 = iconMap1->texLayout.v0;

				s32 mapWidth = (s32)iconMap0->texLayout.u1 - (s32)iconMap0->texLayout.u0;
				s32 mapHeight = ((s32)mapTopV2 - (s32)mapTopV0) + (s32)mapBottomV2 - (s32)mapBottomV0;

				// draw six track minimaps on menu
				// map 1 is the regular color, which is white
				// map 2 is blue and shifted 2px to the left
				// map 3 is blue and shifted 2px to the right
				// map 4 is blue and shifted 1px downwards
				// map 5 is blue and shifted 1px upwards
				// map 6 is black and shifted 6px downwards and 12px to the right
				for (s32 mapLayer = 0; mapLayer < MM_TRACK_SELECT_MAP_LAYER_COUNT; mapLayer++)
				{
					UI_Map_DrawMap(
					    // top half
					    iconMap0,

					    // bottom half
					    iconMap1,

					    // X
					    D230.drawMapOffset[mapLayer].offsetX + previewRect.x +
					        (D230.trackTransitions.named.trackSelect_lapMenuTransition.currX -
					         D230.trackTransitions.named.trackSelect_previewTransition.currX) +
					        (MM_TRACK_VIDEO_WIDTH >> 1) + (mapWidth >> 1),

					    // Y
					    D230.drawMapOffset[mapLayer].offsetY + previewRect.y +
					        (D230.trackTransitions.named.trackSelect_lapMenuTransition.currY -
					         D230.trackTransitions.named.trackSelect_previewTransition.currY) +
					        MM_TRACK_SELECT_MAP_CENTER_Y_OFFSET + (MM_TRACK_SELECT_MAP_BOX_H >> 1) + (mapHeight >> 1),

					    // pointer to PrimMem struct
					    &gGT->backBuffer->primMem,

					    // pointer to OT mem
					    gGT->pushBuffer_UI.ptrOT,

					    // 1 = draw map with regular color (white) - used for the main layer of the minimap in the track select screen
					    // 2 = draw map blue - used for the outline of the minimap in the track select screen
					    // 3 = draw map black - used for the shadow of the minimap in the track select screen
					    D230.drawMapOffset[mapLayer].type);
				}
			}

			// [K5] Preview: the recorded frame sequence or NO PREVIEW
			// (MM_NativeTrackSelect_Preview), then the same box that
			// MM_TrackSelect_Video_Draw draws last
			// (MM_TrackSelect.c:322).
			//
			// Format 4.1: if the cursor is on a grey row, the reason stands
			// here - "NEEDS A NEWER VERSION OF CTR RELOAD" and the others
			// of the container format. Output BEFORE the box: it is
			// opaque, and what comes after it would lie below it.
			if ((D230.trackSelect.lapBoxOpen == 0) && (D230.trackSelect.trackChangeFrames == 0))
			{
				struct NativeTrackRefusalText refusal;

				int scan = MM_NativeTracks_RowIndex((int)menu->rowSelected);

				if (NativeTrack_WhyNotOffered(scan, MM_NativeTracks_ListMode(), &refusal) == NULL)
				{
					MM_NativeTrackSelect_Preview(&previewRect, scan);
				}
				else
				{
					int line;

					for (line = 0; line < refusal.lineCount; line++)
					{
						DecalFont_DrawLine((char *)refusal.lines[line], previewRect.x + (MM_TRACK_VIDEO_WIDTH / 2),
						                   previewRect.y + MM_NATIVE_REFUSAL_TOP + (line * MM_NATIVE_REFUSAL_PITCH), FONT_SMALL,
						                   (line == 0) ? (JUSTIFY_CENTER | RED) : (JUSTIFY_CENTER | WHITE));
					}
				}
			}

			RECTMENU_DrawInnerRect(&previewRect, 1, gGT->backBuffer->otMem.uiOT);

			return;
		}
	} while (true);
}

#endif
