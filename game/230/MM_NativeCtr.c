#include <common.h>

#ifdef CTR_NATIVE

#include "platform/native_assets.h"

// NITRO-PIT -> CTR: THE CTR CHALLENGE FOR CONTAINERS. The CTR row is white
// as soon as a container offers CTR,
// otherwise grey with "NO CTR TRACKS" (native_menuscreen.c).
//
// An ordinary arcade race with bots (NITRO RACE) plus TOKEN_RACE in gameMode2
// and this marker. ADVENTURE_MODE stays off: token and
// trophy bits, bot strength by save state, loss counter and the way back into
// the hub depend on it - for a container all on the donor seat (DINGO CANYON).
//
// What retail has for it and is used here: the letters C, T, R on the
// track (RB_CtrLetter.c), their flight into the HUD (UI_RenderFrame.c), the
// letter fall in the end screen (222.c) and the win rule "place 1 and three
// letters" (222.c). The switches for it are in INSTANCE.c, UI_Instance.c,
// UI_RenderFrame.c, UI_RaceFlow.c and 222.c, each under CTR_NATIVE and only with
// MM_NativeCtr_IsCustom().
//
// WHAT IS DIFFERENT FOR CONTAINERS: no token, nothing in advProgress, no
// prevLEV; at the end YOU WIN or TRY AGAIN, then the box RETRY / NITRO-PIT as
// with Crystal (MM_NativeCrystal.c), the way back opens NITRO-PIT with the
// cursor on CTR. Three laps fixed, like retail - the lap box is dropped.

global_variable int s_nativeCtrCustom = 0;
global_variable int s_nativeCtrEndLogged = 0;

int MM_NativeCtr_IsCustom(void)
{
	const struct GameTracker *gGT = sdata->gGT;

	// Only on a container track: a debug jump out of the race keeps
	// TOKEN_RACE and the marker.
	return s_nativeCtrCustom && ((gGT->gameMode2 & TOKEN_RACE) != 0) && ((gGT->gameMode1 & ARCADE_MODE) != 0) &&
	       ((gGT->gameMode1 & ADVENTURE_MODE) == 0) && NativeTrack_ActiveForLevel(gGT->levelID);
}

// From MM_NativeTrackSelect_MenuProc, when the container of a CTR row is
// loaded and QueueLoadTrack is about to request the level. The bit as with
// Crystal through Loading.OnBegin, here in the mask of gameMode2
// (MainMain.c: (old | add) & ~rem - clearing beats setting).
void MM_NativeCtr_Arm(void)
{
	struct GameTracker *gGT = sdata->gGT;

	sdata->Loading.OnBegin.RemBitsConfig8 &= ~(u32)TOKEN_RACE;
	sdata->Loading.OnBegin.AddBitsConfig8 |= (u32)TOKEN_RACE;
	gGT->numLaps = MM_DEFAULT_LAP_COUNT;

	s_nativeCtrCustom = 1;
	s_nativeCtrEndLogged = 0;

	Platform_Log("[CTR Ctr] NITRO-PIT -> CTR: '%s' - CTR Challenge, %d laps, win = 1st place and the letters C, T, R, no token, nothing saved\n",
	             NativeTrack_LoadedName(), (int)gGT->numLaps);
}

// From NativeMenuLock_Tick, only in the menu level (see MM_NativeCrystal_MenuTick,
// the same reason): TOKEN_RACE from a container challenge removed, otherwise the
// next NITRO RACE drives with letters.
void MM_NativeCtr_MenuTick(void)
{
	struct GameTracker *gGT = sdata->gGT;

	if (!s_nativeCtrCustom || ((gGT->gameMode1 & MAIN_MENU) == 0))
	{
		return;
	}

	if ((gGT->gameMode2 & TOKEN_RACE) != 0)
	{
		gGT->gameMode2 &= ~TOKEN_RACE;
		s_nativeCtrCustom = 0;
		Platform_Log("[CTR Ctr] back in the menu - TOKEN_RACE cleared\n");
	}
	else if ((sdata->Loading.OnBegin.AddBitsConfig8 & TOKEN_RACE) == 0)
	{
		// As with Crystal: NITRO-PIT in the end box takes the bit away through the
		// clear mask, then only the marker remains.
		s_nativeCtrCustom = 0;
		Platform_Log("[CTR Ctr] back in the menu - challenge marker cleared\n");
	}
}

// From 222.c, in every frame of the end screen of a container challenge.
void MM_NativeCtr_LogEnd(int won)
{
	const struct GameTracker *gGT = sdata->gGT;
	const struct Driver *d = gGT->drivers[0];

	if (s_nativeCtrEndLogged)
	{
		return;
	}

	s_nativeCtrEndLogged = 1;
	Platform_Log("[CTR Ctr] end: %s - place %d, %d of 3 letter(s) - no token, nothing saved\n", won ? "YOU WIN" : "TRY AGAIN",
	             (d != NULL) ? (int)d->driverRank + 1 : -1, (d != NULL) ? (int)d->PickupLetterHUD.numCollected : -1);
}

// THE END BOX: RETRY like retail (UI_RaceEnd_MenuProc), NITRO-PIT back into
// the box, cursor on CTR.
internal void MM_NativeCtr_EndMenuProc(struct RectMenu *menu)
{
	if ((menu->funcState != RECTMENU_FUNC_STATE_INPUT) || (menu->rowSelected < 0) ||
	    (menu->rows[menu->rowSelected].stringIndex != MM_NATIVE_LNG_NITRO_PIT))
	{
		// RETRY leaves TOKEN_RACE standing (LOAD_RESTART), the letters
		// come anew with the level.
		s_nativeCtrEndLogged = 0;
		UI_RaceEnd_MenuProc(menu);
		return;
	}

	RECTMENU_Hide(menu);
	sdata->framesSinceRaceEnded = 0;

	GhostTape_Destroy();
	sdata->Loading.OnBegin.RemBitsConfig8 |= TOKEN_RACE;
	MM_NativeChallenge_RequestPitReturn(MM_NATIVE_CHOSEN_CTR);
	sdata->mainMenuState = MAIN_MENU_TITLE;

	Platform_Log("[CTR Ctr] NITRO-PIT chosen at the end - back to the NITRO-PIT box\n");
	MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL);
}

global_variable struct MenuRow s_nativeCtrEndRows[3] = {
    {LNG_RETRY, 0, 1, 0, 0},
    {MM_NATIVE_LNG_NITRO_PIT, 0, 1, 1, 1},
    {RECTMENU_STRING_NONE, 0, 0, 0, 0},
};

// Position and style like the box of the Crystal Challenge.
global_variable struct RectMenu s_nativeCtrEndMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .posX_curr = 0x100,
    .posY_curr = 0xB4,
    .unk1 = 0,
    .state = RECTMENU_STATE_CENTERED,
    .rows = s_nativeCtrEndRows,
    .funcPtr = MM_NativeCtr_EndMenuProc,
    .drawStyle = 4,
};

struct RectMenu *MM_NativeCtr_EndMenu(void)
{
	return &s_nativeCtrEndMenu;
}

// --ctr-grab <n> (only with --dev): measuring probe like --crystal-grab. From the 90th frame on,
// every 60th frame the next letter still lying there, through its own
// collision path (RB_CtrLetter_LInC -> RB_CtrLetter_ThCollide). Counting,
// HUD flight and win rule stay game code. It does NOT set place 1.
int g_cfg_ctrGrab = 0;

void MM_NativeCtr_ProbeFrame(void)
{
	static int s_frames = 0;
	static int s_taken = 0;
	static int s_lastElapsed = 0;
	struct GameTracker *gGT = sdata->gGT;
	struct Driver *d = gGT->drivers[0];
	struct ScratchpadStruct sps;
	u32 i;

	if ((g_cfg_ctrGrab <= 0) || !MM_NativeCtr_IsCustom() || ((gGT->gameMode1 & PAUSE_ALL) != 0) || (d == NULL) || (d->instSelf == NULL) ||
	    (gGT->level1 == NULL))
	{
		if (!MM_NativeCtr_IsCustom())
		{
			s_frames = 0;
			s_taken = 0;
			s_lastElapsed = 0;
		}
		return;
	}

	// RETRY starts the clock from the beginning - and the probe too.
	if (gGT->elapsedEventTime < s_lastElapsed)
	{
		s_frames = 0;
		s_taken = 0;
	}
	s_lastElapsed = gGT->elapsedEventTime;

	s_frames++;
	if ((s_frames < 90) || ((s_frames % 60) != 0) || (s_taken >= g_cfg_ctrGrab))
	{
		return;
	}

	for (i = 0; i < gGT->level1->numInstances; i++)
	{
		struct InstDef *def = &gGT->level1->ptrInstDefs[i];
		struct Instance *inst = def->ptrInstance;

		if ((inst == NULL) || (def->model == NULL) || ((u32)(def->model->id - STATIC_C) >= 3u) || (inst->scale.x == 0))
		{
			continue;
		}

		memset(&sps, 0, sizeof(sps));
		sps.Input1.modelID = DYNAMIC_PLAYER;
		if (RB_CtrLetter_LInC(inst, d->instSelf->thread, &sps))
		{
			s_taken++;
			Platform_Log("[CTR Probe] ctr-grab: letter %d (model 0x%x) taken through RB_CtrLetter_LInC (frame %d)\n", s_taken, (int)def->model->id,
			             s_frames);
		}
		return;
	}
}

#endif
