#include <common.h>

#ifdef CTR_NATIVE

#include "platform/native_assets.h"

// NITRO-PIT -> CRYSTAL: THE CRYSTAL CHALLENGE FOR CONTAINERS.
//
// THE SAME PATH AS THE DEBUG JUMP. The debug menu has no crystal mode of its
// own, just the probe s_crystalProbe in DebugMenu_JumpToLevel
// (DebugMenu.c): ARCADE_MODE stays, CRYSTAL_CHALLENGE moves from the clear
// mask into the set mask of Loading.OnBegin, then MainRaceTrack_RequestLoad.
// Everything else is game code that both paths share and neither copies:
//   - INSTANCE_LevInitAll counts the crystals from the LEV
//     (gGT->numCrystalsInLEV, INSTANCE.c "If crystal challenge");
//   - the time limit is gGT->originalEventTime = TITLE_INITIAL_EVENT_TIME
//     (3:00). The debug jump sets it itself, the menu path through
//     QueueLoadTrack_MenuProc (QueueLoadTrack.c). The retail table per level,
//     D232.battleCrystalEventTime, is read only by the warp pad (AH_WarpPad.c) -
//     neither path passes there, so no switch is needed;
//   - HUD, clock, collecting and the win check (UI_RenderFrame_CrystChall,
//     UI_DrawLimitClock, RB_Crystal.c) and the end screen (221.c).
// MM_NativeCrystal_ModeBits sets the bits, in ONE place for both.
//
// WHAT IS DIFFERENT FOR CONTAINERS, and only that (221.c, CTR_NATIVE branch):
//   - no purple token, nothing in advProgress, no ADVENTURE_ARENA, no
//     jump to prevLEV (here that would be the menu level with the adventure bit);
//   - after YOU WIN or TRY AGAIN a box of its own: RETRY, NITRO-PIT. The
//     NITRO-PIT row loads the menu and opens the NITRO-PIT box with the
//     cursor on CRYSTAL (NativeMenuLock_Tick).
// Retail adventure stays bit for bit as it was: MM_NativeCrystal_IsCustom
// is never true there (ADVENTURE_MODE, and the marker is only set here).

// Set when a container challenge starts, cleared in the menu as soon as the
// bit comes back from there (MM_NativeCrystal_MenuTick).
global_variable int s_nativeCrystalCustom = 0;

// The return to NITRO-PIT, requested by the NITRO-PIT row in the end box:
// 0 none, otherwise the row as MM_NATIVE_CHOSEN_CRYSTAL or _CTR.
global_variable int s_nativeCrystalPitReturn = 0;

// For both challenges (Crystal here, CTR in MM_NativeCtr.c).
void MM_NativeChallenge_RequestPitReturn(int chosen)
{
	s_nativeCrystalPitReturn = chosen;
}

// Log the end once, not in every frame.
global_variable int s_nativeCrystalEndLogged = 0;

// THE BITS, in one place for the debug jump and for NITRO-PIT. The bit
// has to come out of the CLEAR MASK, not only go into the set mask:
// MainMain.c computes (old | add) & ~rem, clearing beats setting.
void MM_NativeCrystal_ModeBits(u32 *addBits, u32 *remBits)
{
	*remBits &= ~(u32)CRYSTAL_CHALLENGE;
	*addBits |= (u32)CRYSTAL_CHALLENGE;
}

int MM_NativeCrystal_IsCustom(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	// Only on a container track: a debug jump to a disc track would otherwise
	// keep a stale marker.
	return s_nativeCrystalCustom && ((gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0) && ((gGT->gameMode1 & ADVENTURE_MODE) == 0) &&
	       NativeTrack_ActiveForLevel(gGT->levelID);
}

// From MM_NativeTrackSelect_MenuProc, when the container of a CRYSTAL row
// is loaded and QueueLoadTrack is about to request the level.
void MM_NativeCrystal_Arm(void)
{
	u32 addBits = 0;
	u32 remBits = sdata->Loading.OnBegin.RemBitsConfig0;

	MM_NativeCrystal_ModeBits(&addBits, &remBits);
	sdata->Loading.OnBegin.AddBitsConfig0 |= addBits;
	sdata->Loading.OnBegin.RemBitsConfig0 = remBits;

	s_nativeCrystalCustom = 1;
	s_nativeCrystalEndLogged = 0;

	Platform_Log("[CTR Crystal] NITRO-PIT -> CRYSTAL: '%s' - Crystal Challenge, time limit %d:%02d (the game's default, as the debug jump), "
	             "crystals counted from the LEV at load, no token, nothing saved\n",
	             NativeTrack_LoadedName(), (int)(TITLE_INITIAL_EVENT_TIME / 0xe100), (int)((TITLE_INITIAL_EVENT_TIME % 0xe100) / 960));
}

// From NativeMenuLock_Tick: if the bit comes back to the menu from a container
// challenge (end box, pause QUIT, CHANGE LEVEL), it is removed here
// - otherwise the next race runs as a challenge. The marker waits until the
// bit has really been there: between the start and the load this hook keeps
// running, and at that point the bit is only in Loading.OnBegin.
//
// ONLY IN THE MENU LEVEL (MAIN_MENU). NativeMenuLock_Tick runs for EVERY
// active box, including the end box in the race; without the bit the
// next frame there picked the arcade end screen (222.c) instead of 221.c - and
// that crashed.
void MM_NativeCrystal_MenuTick(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!s_nativeCrystalCustom || ((gGT->gameMode1 & MAIN_MENU) == 0))
	{
		return;
	}

	if ((gGT->gameMode1 & CRYSTAL_CHALLENGE) != 0)
	{
		gGT->gameMode1 &= ~CRYSTAL_CHALLENGE;
		s_nativeCrystalCustom = 0;
		Platform_Log("[CTR Crystal] back in the menu - CRYSTAL_CHALLENGE cleared\n");
	}
	else if ((sdata->Loading.OnBegin.AddBitsConfig0 & CRYSTAL_CHALLENGE) == 0)
	{
		// The NITRO-PIT end box already removes the bit through the clear mask;
		// then it never arrives here. If it is not pending to be set either
		// (Arm, before the load), no challenge is on its way any more.
		s_nativeCrystalCustom = 0;
		Platform_Log("[CTR Crystal] back in the menu - challenge marker cleared\n");
	}
}

// From DebugMenu.c: the crystal probe on the TRACK page with a container.
// The debug jump sets the bits itself (MM_NativeCrystal_ModeBits); here only
// the marker, otherwise the end screen would take the retail path with token and
// save data on the donor slot.
void MM_NativeCrystal_MarkDebug(void)
{
	s_nativeCrystalCustom = 1;
	s_nativeCrystalEndLogged = 0;
	Platform_Log("[CTR Crystal] debug TRACK page with the crystal probe: '%s' runs as a container challenge - no token, nothing saved\n",
	             NativeTrack_LoadedName());
}

int MM_NativeCrystal_TakePitReturn(void)
{
	const int wanted = s_nativeCrystalPitReturn;

	s_nativeCrystalPitReturn = 0;
	return wanted;
}

// From 221.c, in every frame of the end screen of a container challenge.
void MM_NativeCrystal_LogEnd(int didWin)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d = gGT->drivers[0];
	int left;

	if (s_nativeCrystalEndLogged)
	{
		return;
	}

	s_nativeCrystalEndLogged = 1;
	left = gGT->originalEventTime - gGT->elapsedEventTime;
	if (left < 0)
	{
		left = 0;
	}

	Platform_Log("[CTR Crystal] end: %s - %d of %d crystal(s), %d:%02d left - no token, nothing saved\n", didWin ? "YOU WIN" : "TIME UP, TRY AGAIN",
	             (d != NULL) ? (int)d->numCrystals : -1, (int)gGT->numCrystalsInLEV, left / 0xe100, (left % 0xe100) / 960);
}

// THE END BOX: RETRY as retail (UI_RaceEnd_MenuProc), NITRO-PIT instead of
// EXIT TO MAP.
internal void MM_NativeCrystal_EndMenuProc(struct RectMenu *menu)
{
	s16 option;

	if ((menu->funcState != RECTMENU_FUNC_STATE_INPUT) || (menu->rowSelected < 0))
	{
		UI_RaceEnd_MenuProc(menu);
		return;
	}

	option = menu->rows[menu->rowSelected].stringIndex;
	if (option != MM_NATIVE_LNG_NITRO_PIT)
	{
		// RETRY: the next end goes to the log again (as MM_NativeCtr.c).
		s_nativeCrystalEndLogged = 0;
		UI_RaceEnd_MenuProc(menu);
		return;
	}

	RECTMENU_Hide(menu);
	sdata->framesSinceRaceEnded = 0;

	// As QUIT in the retail proc: ghost gone, load the menu level, title. Plus the
	// request for NativeMenuLock_Tick and the bit into the clear mask.
	GhostTape_Destroy();
	sdata->Loading.OnBegin.RemBitsConfig0 |= CRYSTAL_CHALLENGE;
	MM_NativeChallenge_RequestPitReturn(MM_NATIVE_CHOSEN_CRYSTAL);
	sdata->mainMenuState = MAIN_MENU_TITLE;

	Platform_Log("[CTR Crystal] NITRO-PIT chosen at the end - back to the NITRO-PIT box\n");
	MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
}

global_variable struct MenuRow s_nativeCrystalEndRows[3] = {
    {LNG_RETRY, 0, 1, 0, 0},
    {MM_NATIVE_LNG_NITRO_PIT, 0, 1, 1, 1},
    {RECTMENU_STRING_NONE, 0, 0, 0, 0},
};

// Position and style as menu221 (221.c).
global_variable struct RectMenu s_nativeCrystalEndMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .posX_curr = 0x100,
    .posY_curr = 0xB4,
    .unk1 = 0,
    .state = RECTMENU_STATE_CENTERED,
    .rows = s_nativeCrystalEndRows,
    .funcPtr = MM_NativeCrystal_EndMenuProc,
    .drawStyle = 4,
};

struct RectMenu *MM_NativeCrystal_EndMenu(void)
{
	return &s_nativeCrystalEndMenu;
}

// --crystal-grab <n> (only with --dev): a probe for acceptance tests, no help
// in the game. Without drive paths no autopilot drives, and the measurement
// run does not drive by hand. So from frame 90 on, every 20th frame it takes the next
// crystal of the LEV still lying there - through its own collision path
// (RB_Crystal_LInC -> RB_Crystal_ThCollide), like a kart driving through it.
// Counting, HUD, win check and end screen stay game code. n = 0 means
// off, n >= crystal count means all.
int g_cfg_crystalGrab = 0;

void MM_NativeCrystal_ProbeFrame(void)
{
	static int s_frames = 0;
	static int s_taken = 0;
	static int s_lastElapsed = 0;
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = gGT->drivers[0];
	struct ScratchpadStruct sps;
	u32 i;

	if ((g_cfg_crystalGrab <= 0) || !MM_NativeCrystal_IsCustom() || (d == NULL) || (d->instSelf == NULL) || (gGT->level1 == NULL))
	{
		s_frames = 0;
		s_taken = 0;
		s_lastElapsed = 0;
		return;
	}

	// RETRY starts the clock over - and the probe too.
	if (gGT->elapsedEventTime < s_lastElapsed)
	{
		s_frames = 0;
		s_taken = 0;
	}
	s_lastElapsed = gGT->elapsedEventTime;

	s_frames++;
	if ((s_frames < 90) || ((s_frames % 20) != 0) || (s_taken >= g_cfg_crystalGrab))
	{
		return;
	}

	for (i = 0; i < gGT->level1->numInstances; i++)
	{
		struct InstDef *def = &gGT->level1->ptrInstDefs[i];
		struct Instance *inst = def->ptrInstance;

		if ((inst == NULL) || (def->model == NULL) || (def->model->id != STATIC_CRYSTAL) || (inst->scale.x == 0))
		{
			continue;
		}

		memset(&sps, 0, sizeof(sps));
		sps.Input1.modelID = DYNAMIC_PLAYER;
		if (RB_Crystal_LInC(inst, d->instSelf->thread, &sps))
		{
			s_taken++;
			Platform_Log("[CTR Probe] crystal-grab: crystal %d of %d taken through RB_Crystal_LInC (frame %d)\n", s_taken, (int)gGT->numCrystalsInLEV,
			             s_frames);
		}
		return;
	}
}

#endif
