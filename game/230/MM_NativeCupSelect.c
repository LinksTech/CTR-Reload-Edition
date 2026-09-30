#include <common.h>

#ifdef CTR_NATIVE

#include "platform/native_assets.h"

// NITRO-PIT -> NITRO CUP: THE CUP SCREEN FOR THE CUPS FROM cups.txt
// (run-time level IDs).
//
// A COPY of MM_CupSelect_MenuProc (MM_CupSelect.c:15-177), no change
// to it - like the NITRO-PIT track screen (MM_NativeTrackSelect.c): the
// retail cup screen is the model, the copy lives in its own file, and the
// retail file stays bit-identical.
//
// HOOKED IN through D230.menuCupSelect.funcPtr and .rows, every frame in
// MM_NativeCupSelect_Hook (from NativeMenuLock_Tick, at the very top of
// RECTMENU_ProcessState). The box is the same, so the same widescreen
// row applies to it ("menu-cup-select", native_uidecl.c).
// MM_CupSelect_Init and the driver select before it stay retail
// (MM_Characters.c:657-663).
//
// WHAT IS DIFFERENT, and only that (marked [C1] to [C6] in the code):
//
//   [C1] The boxes come from cups.txt (NativeCup_Get), at most four. The
//        rows are our own, with neighbours only between existing boxes.
//   [C2] The name comes from cups.txt, shortened to the box width.
//   [C3] Instead of the four track icons the four track names: a container
//        has no icon. Two above the cup name, two below it, in the
//        order of the icons.
//   [C4] No stars. They show retail win bits, and a custom cup
//        writes none (the switch in UI_CupStandings.c).
//   [C5] A grey cup (container missing, rejected or without ID) stands grey,
//        its row carries the lock bit: RECTMENU accepts no cross and
//        plays the lock sound (RECTMENU_ProcessInput). Where the track is missing,
//        the keyword stands in red (MISSING, NEEDS NEWER, DAMAGED, NO ID, NO RACE
//        MODE ...) - the log gives the full reason.
//   [C6] The start: cup.cupID is the index in cups.txt (0..3, never 4 - the
//        purple special cases ask cupID == 4), trackIndex and points as in
//        retail, and currLEV is the run-time ID of the first track. The rest
//        is done by QueueLoadTrack as in the original, the funnel translates.

// How many characters fit in a row of the box: 180 wide, 6 indent on
// each side, leaves 168. FONT_CREDITS is 14 wide, FONT_SMALL 13
// (data.font_charPixWidth): 12 characters in both.
#define MM_NATIVE_CUP_LINE_CHARS 12

// [C3] The four track rows, relative to the content corner like the icons. The
// name stands as in the original at +27 (0x44 - 0x29) and is 17 high; FONT_SMALL
// is 8 high. The box reaches from -4 to +74.
static const s16 s_nativeCupTrackLineY[NATIVE_CUP_TRACKS] = {1, 12, 50, 61};

// Whether NITRO CUP is chosen in the race type box. Set and cleared only there
// (NativeMenuLock_ProcRaceType), like the marker of the track screen.
global_variable int s_nativeCupChosen = 0;

// [C1] The rows: stringIndex (0 or the lock bit), up, down, left,
// right - like D230.rowsCupSelect (D230.c:153).
global_variable struct MenuRow s_nativeCupRows[NATIVE_CUP_MAX + 1];

// The running cup: set at the start [C6], cleared at the title screen
// (MM_NativeTracks_Disarm).
global_variable int s_nativeCupRunning = 0;
global_variable int s_nativeCupIndex = -1;

// The points before the last race, for "+N" in the log line.
global_variable int s_nativeCupPointsBefore[MM_CUP_SELECT_DRIVER_SLOT_COUNT];

global_variable char s_nativeCupLine[MM_NATIVE_LINE_MAX + 1];

void MM_NativeCupSelect_SetChosen(int chosen)
{
	if (chosen != s_nativeCupChosen)
	{
		Platform_Log("[CTR Menu] NITRO-PIT: NITRO CUP %s\n", chosen ? "chosen - the cup screen shows cups.txt" : "off - the cup screen shows the disc");
	}

	s_nativeCupChosen = chosen;
}

// The hook condition: NITRO CUP chosen, arcade, cup, no other mode.
internal int MM_NativeCupSelect_Applies(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	return (s_nativeCupChosen != 0) && ((gGT->gameMode1 & ARCADE_MODE) != 0) &&
	       ((gGT->gameMode1 & (TIME_TRIAL | BATTLE_MODE | ADVENTURE_MODE)) == 0) && ((gGT->gameMode2 & CUP_ANY_KIND) != 0);
}

// A cup can be chosen if cups.txt found it good AND each of its
// four tracks offers a race NOW. The second counts because a container
// can only be rejected when it loads (memory figures, hash) - then its
// cup turns grey, instead of falling onto the bare donor slot the next time.
internal int MM_NativeCupSelect_Playable(const struct NativeCup *cup)
{
	int t;

	if ((cup == NULL) || !cup->ok)
	{
		return 0;
	}

	for (t = 0; t < NATIVE_CUP_TRACKS; t++)
	{
		if (!NativeTrack_OffersRace(cup->index[t]))
		{
			return 0;
		}
	}

	return 1;
}

// [C1] Boxes in the arrangement of the original: 0 1 on top, 2 3 below.
internal void MM_NativeCupSelect_BuildRows(void)
{
	const int count = NativeCup_Count();
	int i;

	for (i = 0; i < count; i++)
	{
		const struct NativeCup *cup = NativeCup_Get(i);
		struct MenuRow *row = &s_nativeCupRows[i];

		row->stringIndex = (s16)(MM_NativeCupSelect_Playable(cup) ? 0 : MENU_ROW_LOCKED);
		row->rowOnPressUp = (char)((i >= 2) ? (i - 2) : i);
		row->rowOnPressDown = (char)(((i + 2) < count) ? (i + 2) : i);
		row->rowOnPressLeft = (char)(((i & 1) != 0) ? (i - 1) : i);
		row->rowOnPressRight = (char)((((i & 1) == 0) && ((i + 1) < count)) ? (i + 1) : i);
	}

	s_nativeCupRows[count].stringIndex = RECTMENU_STRING_NONE;

	if (D230.menuCupSelect.rowSelected >= count)
	{
		D230.menuCupSelect.rowSelected = 0;
	}
}

// EVERY FRAME, for the same reason as on the track screen: OVR230_ResetRuntimeState
// resets all of D230 on every return from a race, including proc and
// rows. Swapping happens only between exactly these two procs.
void MM_NativeCupSelect_Hook(void)
{
	if (MM_NativeCupSelect_Applies())
	{
		if (D230.menuCupSelect.funcPtr == MM_CupSelect_MenuProc)
		{
			D230.menuCupSelect.funcPtr = MM_NativeCupSelect_MenuProc;
		}

		if (D230.menuCupSelect.funcPtr == MM_NativeCupSelect_MenuProc)
		{
			MM_NativeCupSelect_BuildRows();
			D230.menuCupSelect.rows = &s_nativeCupRows[0];
		}
	}
	else if (D230.menuCupSelect.funcPtr == MM_NativeCupSelect_MenuProc)
	{
		D230.menuCupSelect.funcPtr = MM_CupSelect_MenuProc;
		D230.menuCupSelect.rows = &D230.rowsCupSelect[0];
	}
}

// After MM_ToggleRows_Difficulty, which sets the cup lock anew every frame
// (from NativeMenuLock_Apply). By design: in a
// custom cup all three are open, as in the NITRO RACE single race.
void MM_NativeCup_OpenDifficulty(void)
{
	int i;

	if (!MM_NativeCupSelect_Applies())
	{
		return;
	}

	for (i = 0; i < MM_DIFFICULTY_COUNT; i++)
	{
		D230.rowsDifficulty[i].stringIndex &= (s16)~MENU_ROW_LOCKED;
	}
}

// The driver name for the log, short as in the standings.
internal const char *MM_NativeCup_DriverName(int driverID)
{
	const int character = data.characterIDs[driverID];

	return sdata->lngStrings[data.MetaDataCharacters[character].name_LNG_short];
}

internal void MM_NativeCup_LogRace(int trackIndex)
{
	const struct NativeCup *cup = NativeCup_Get(s_nativeCupIndex);

	if ((cup == NULL) || (trackIndex < 0) || (trackIndex >= NATIVE_CUP_TRACKS))
	{
		return;
	}

	Platform_Log("[CTR Cup] race %d of %d in '%s': level id %d '%s' at vblank %d\n", trackIndex + 1, NATIVE_CUP_TRACKS, cup->name,
	             cup->levelID[trackIndex], cup->file[trackIndex], Platform_GetVBlankCount());
}

// [C6]
internal void MM_NativeCup_Start(int index)
{
	const struct NativeCup *cup = NativeCup_Get(index);
	const struct GameTracker *gGT = sdata->gGT;
	int i;

	s_nativeCupRunning = 1;
	s_nativeCupIndex = index;

	for (i = 0; i < MM_CUP_SELECT_DRIVER_SLOT_COUNT; i++)
	{
		s_nativeCupPointsBefore[i] = 0;
	}

	Platform_Log("[CTR Cup] '%s' (line %d of %s) starts: %d races, %d lap(s) each, difficulty %d, driver %d\n", cup->name, cup->line, NATIVE_CUP_FILE,
	             NATIVE_CUP_TRACKS, (int)gGT->numLaps, (int)gGT->arcadeDifficulty, (int)data.characterIDs[0]);
	MM_NativeCup_LogRace(0);
}

int MM_NativeCup_Running(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	return (s_nativeCupRunning != 0) && (NativeCup_Get(s_nativeCupIndex) != NULL) && ((gGT->gameMode1 & ARCADE_MODE) != 0) &&
	       ((gGT->gameMode2 & CUP_ANY_KIND) != 0);
}

// The full name for the log.
internal const char *MM_NativeCup_FullName(void)
{
	const struct NativeCup *cup = NativeCup_Get(s_nativeCupIndex);

	return (cup != NULL) ? cup->name : "?";
}

// The name for the titles (standings, race start), shortened like the
// track names there (MM_NativeTracks_NameForLevel).
global_variable char s_nativeCupTitle[MM_NATIVE_LINE_MAX + 1];

const char *MM_NativeCup_Name(void)
{
	MM_NativeTracks_Shorten(MM_NativeCup_FullName(), &s_nativeCupTitle[0], MM_NATIVE_LINE_MAX);
	return &s_nativeCupTitle[0];
}

// The run-time ID of race trackIndex (0..3) - in place of
// data.ArcadeCups[cupID].CupTrack[trackIndex].trackID. The funnel turns it
// into the slot.
s16 MM_NativeCup_LevelFor(int trackIndex)
{
	const struct NativeCup *cup = NativeCup_Get(s_nativeCupIndex);

	if ((cup == NULL) || (trackIndex < 0) || (trackIndex >= NATIVE_CUP_TRACKS))
	{
		Platform_LogWarn("[CTR Cup] no custom cup behind race %d - the funnel falls back to the bare donor seat\n", trackIndex + 1);
		return (s16)NATIVE_TRACK_LEVELID_FIRST;
	}

	MM_NativeCup_LogRace(trackIndex);
	return (s16)cup->levelID[trackIndex];
}

// After the points of a race are awarded (UI_CupStandings.c): in
// finishing order, per driver "+ this race = total".
void MM_NativeCup_LogPoints(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	char text[512];
	int at = 0;
	int i;

	for (i = 0; i < MM_CUP_SELECT_DRIVER_SLOT_COUNT; i++)
	{
		const struct Driver *d = gGT->driversInRaceOrder[i];
		int id;

		if (d == NULL)
		{
			continue;
		}

		id = d->driverID;
		at += snprintf(&text[at], sizeof(text) - (size_t)at, "%s%d. %s%s +%d = %d", (at != 0) ? ", " : "", i + 1, MM_NativeCup_DriverName(id),
		               (id == 0) ? " (P1)" : "", gGT->cup.points[id] - s_nativeCupPointsBefore[id], gGT->cup.points[id]);

		if (at >= (int)sizeof(text))
		{
			break;
		}
	}

	Platform_Log("[CTR Cup] after race %d of %d on '%s' at vblank %d: %s\n", gGT->cup.trackIndex + 1, NATIVE_CUP_TRACKS,
	             NativeTrack_ActiveForLevel(gGT->levelID) ? NativeTrack_LoadedName() : "?", Platform_GetVBlankCount(), text);

	for (i = 0; i < MM_CUP_SELECT_DRIVER_SLOT_COUNT; i++)
	{
		s_nativeCupPointsBefore[i] = gGT->cup.points[i];
	}
}

// The final standings, after UI_CupStandings_FinalizeCupRanks and before the points
// are cleared.
void MM_NativeCup_LogFinal(void)
{
	const struct GameTracker *gGT = sdata->gGT;
	char text[512];
	int at = 0;
	int i;

	for (i = 0; i < MM_CUP_SELECT_DRIVER_SLOT_COUNT; i++)
	{
		const int id = data.cupPositionPerPlayer[i];

		if ((id < 0) || (id >= MM_CUP_SELECT_DRIVER_SLOT_COUNT) || (gGT->drivers[id] == NULL))
		{
			continue;
		}

		at += snprintf(&text[at], sizeof(text) - (size_t)at, "%s%d. %s%s %d", (at != 0) ? ", " : "", i + 1, MM_NativeCup_DriverName(id),
		               (id == 0) ? " (P1)" : "", gGT->cup.points[id]);

		if (at >= (int)sizeof(text))
		{
			break;
		}
	}

	Platform_Log("[CTR Cup] final standings of '%s' at vblank %d: %s\n", MM_NativeCup_FullName(), Platform_GetVBlankCount(), text);
}

void MM_NativeCup_Stop(const char *why)
{
	if (s_nativeCupRunning)
	{
		Platform_Log("[CTR Cup] '%s' ends - %s, at vblank %d\n", MM_NativeCup_FullName(), why, Platform_GetVBlankCount());
	}

	s_nativeCupRunning = 0;
	s_nativeCupIndex = -1;
}

// [C3] The name of a track in the box, or the reason why it is missing. Since
// format 4.1 the same keyword as in the wheel (NativeTrack_WhyNoRace): NEEDS
// NEWER, DAMAGED, NO RACE MODE ... - at most 12 characters, that is how wide the row is.
internal const char *MM_NativeCupSelect_TrackLine(const struct NativeCup *cup, int t, int *missing)
{
	const struct NativeTrackEntry *entry = NativeTrack_Get(cup->index[t]);
	struct NativeTrackRefusalText refusal;

	*missing = 1;

	if (entry == NULL)
	{
		return "MISSING";
	}

	if (NativeTrack_WhyNoRace(cup->index[t], &refusal) != NULL)
	{
		return refusal.tag;
	}

	*missing = 0;
	MM_NativeTracks_Shorten(entry->name, &s_nativeCupLine[0], MM_NATIVE_CUP_LINE_CHARS);
	return &s_nativeCupLine[0];
}

void MM_NativeCupSelect_MenuProc(struct RectMenu *menu)
{
	struct GameTracker *gGT = sdata->gGT;
	const int count = NativeCup_Count();

	if (menu->funcState == RECTMENU_FUNC_STATE_INPUT)
	{
		// [C5] Direction and cross in the same frame: RECTMENU checked the lock bit
		// on the old row (RECTMENU_ProcessInput). A grey cup
		// does not start this way either.
		if ((menu->rowSelected >= 0) && (menu->rowSelected < count) && ((s_nativeCupRows[menu->rowSelected].stringIndex & MENU_ROW_LOCKED) != 0))
		{
			return;
		}

		D230.cupSelectTransition.startAfterExit = (menu->rowSelected != -1);
		D230.cupSelectTransition.state = EXITING_MENU;
		D230.menuCupSelect.state &= ~(EXECUTE_FUNCPTR);
		D230.menuCupSelect.state |= DISABLE_INPUT_ALLOW_FUNCPTRS;
		return;
	}

	s16 elapsedFrames = D230.cupSelectTransition.frame;

	// The first frame after MM_CupSelect_Init: what the screen shows, and for
	// a grey cup, why.
	if ((D230.cupSelectTransition.state == ENTERING_MENU) && (elapsedFrames == MM_CUP_SELECT_INITIAL_TRANSITION_FRAMES))
	{
		Platform_Log("[CTR Menu] CUSTOM CUP screen: %d cup(s) from %s\n", count, NATIVE_CUP_FILE);

		for (int i = 0; i < count; i++)
		{
			const struct NativeCup *cup = NativeCup_Get(i);

			if (MM_NativeCupSelect_Playable(cup))
			{
				Platform_Log("[CTR Menu]   box %d: '%s'\n", i + 1, cup->name);
			}
			else if (cup->ok)
			{
				Platform_Log("[CTR Menu]   box %d: '%s' greyed out, cannot be chosen - a track was refused when it loaded\n", i + 1, cup->name);
			}
			else
			{
				Platform_Log("[CTR Menu]   box %d: '%s' greyed out, cannot be chosen - %s\n", i + 1, cup->name, cup->problem);
			}
		}
	}

	if (D230.cupSelectTransition.state != IN_MENU)
	{
		if (D230.cupSelectTransition.state == ENTERING_MENU)
		{
			MM_TransitionInOut(D230.transitionMeta_cupSel, elapsedFrames, MM_CUP_SELECT_LERP_FRAMES);

			if (elapsedFrames == 0)
			{
				D230.cupSelectTransition.state = IN_MENU;
				D230.menuCupSelect.state &= ~(DISABLE_INPUT_ALLOW_FUNCPTRS);
				D230.menuCupSelect.state |= EXECUTE_FUNCPTR;
			}
			else
			{
				elapsedFrames--;
			}
		}
		else if (D230.cupSelectTransition.state == EXITING_MENU)
		{
			MM_TransitionInOut(D230.transitionMeta_cupSel, elapsedFrames, MM_CUP_SELECT_LERP_FRAMES);

			elapsedFrames++;

			if (MM_CUP_SELECT_TRANSITION_OUT_DONE_FRAME < elapsedFrames)
			{
				if (D230.cupSelectTransition.startAfterExit != 0)
				{
					// [C6]
					const struct NativeCup *cup = NativeCup_Get(menu->rowSelected);

					if (!MM_NativeCupSelect_Playable(cup))
					{
						Platform_LogWarn("[CTR Cup] cup %d cannot start - back to the driver select\n", (int)menu->rowSelected);
						sdata->ptrDesiredMenu = &D230.menuCharacterSelect;
						MM_Characters_RestoreIDs();
						return;
					}

					gGT->cup.cupID = menu->rowSelected;
					gGT->cup.trackIndex = 0;

					for (s32 driverIndex = 0; driverIndex < MM_CUP_SELECT_DRIVER_SLOT_COUNT; driverIndex++)
					{
						gGT->cup.points[driverIndex] = 0;
					}

					MM_NativeCup_Start(menu->rowSelected);

					sdata->ptrDesiredMenu = &data.menuQueueLoadTrack;
					gGT->currLEV = (s16)cup->levelID[0];
					return;
				}

				sdata->ptrDesiredMenu = &D230.menuCharacterSelect;

				MM_Characters_RestoreIDs();
				return;
			}
		}
	}

	D230.cupSelectTransition.frame = elapsedFrames;

	DecalFont_DrawLine(sdata->lngStrings[LNG_SELECT_CUP_RACE], D230.transitionMeta_cupSel[MM_CUP_SELECT_TITLE_META_INDEX].currX + MM_CUP_SELECT_TITLE_X_OFFSET,
	                   D230.transitionMeta_cupSel[MM_CUP_SELECT_TITLE_META_INDEX].currY + MM_CUP_SELECT_TITLE_Y_OFFSET, FONT_BIG, MM_CUP_SELECT_TEXT_COLOR);

	for (int cupIndex = 0; cupIndex < count; cupIndex++)
	{
		const struct NativeCup *cup = NativeCup_Get(cupIndex);

		// [C5]
		u32 txtColor = MM_NativeCupSelect_Playable(cup) ? MM_CUP_SELECT_TEXT_COLOR : (JUSTIFY_CENTER | GRAY);

		if (cupIndex == menu->rowSelected)
		{
			if ((sdata->frameCounter & MM_CUP_SELECT_FLASH_FRAME_BIT) == 0)
			{
				txtColor |= MM_CUP_SELECT_FLASH_COLOR_BIT;
			}
		}

		int startX = (s16)D230.transitionMeta_cupSel[cupIndex].currX + (cupIndex & 1) * MM_CUP_SELECT_COLUMN_WIDTH;
		int startY = (s16)D230.transitionMeta_cupSel[cupIndex].currY + (cupIndex >> 1) * MM_CUP_SELECT_ROW_HEIGHT;

		// [C2]
		MM_NativeTracks_Shorten(cup->name, &s_nativeCupLine[0], MM_NATIVE_CUP_LINE_CHARS);
		DecalFont_DrawLine(&s_nativeCupLine[0], startX + MM_CUP_SELECT_NAME_X_OFFSET, startY + MM_CUP_SELECT_NAME_Y_OFFSET, FONT_CREDITS, txtColor);

		startX = startX + MM_CUP_SELECT_CONTENT_X_OFFSET;
		startY = startY + MM_CUP_SELECT_CONTENT_Y_OFFSET;

		// [C4] no stars.

		// [C3] the four track names, centered in the box.
		for (int trackIndex = 0; trackIndex < NATIVE_CUP_TRACKS; trackIndex++)
		{
			int missing;
			const char *line = MM_NativeCupSelect_TrackLine(cup, trackIndex, &missing);
			const u32 lineColor = missing ? (JUSTIFY_CENTER | RED) : (MM_NativeCupSelect_Playable(cup) ? (JUSTIFY_CENTER | ORANGE) : (JUSTIFY_CENTER | GRAY));

			DecalFont_DrawLine((char *)line, startX + MM_CUP_SELECT_BACKGROUND_X_OFFSET + (MM_CUP_SELECT_BACKGROUND_WIDTH / 2),
			                   startY + s_nativeCupTrackLineY[trackIndex], FONT_SMALL, lineColor);
		}

		RECT cupBox;

		if (cupIndex == menu->rowSelected)
		{
			cupBox.x = startX + MM_CUP_SELECT_HIGHLIGHT_X_OFFSET;
			cupBox.y = startY + MM_CUP_SELECT_HIGHLIGHT_Y_OFFSET;
			cupBox.w = MM_CUP_SELECT_HIGHLIGHT_WIDTH;
			cupBox.h = MM_CUP_SELECT_HIGHLIGHT_HEIGHT;

			CTR_Box_DrawClearBox(&cupBox, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, gGT->backBuffer->otMem.uiOT);
		}

		cupBox.x = startX + MM_CUP_SELECT_BACKGROUND_X_OFFSET;
		cupBox.y = startY + MM_CUP_SELECT_BACKGROUND_Y_OFFSET;
		cupBox.w = MM_CUP_SELECT_BACKGROUND_WIDTH;
		cupBox.h = MM_CUP_SELECT_BACKGROUND_HEIGHT;

		RECTMENU_DrawInnerRect(&cupBox, 0, gGT->backBuffer->otMem.uiOT);
	}
}

#endif
