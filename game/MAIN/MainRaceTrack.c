#include <common.h>

void MainRaceTrack_StartLoad(s16 levelID)
{
	// clear backup,
	// keep music,
	// destroy "most" fx, let menu fx play to end
	howl_StopAudio(1, 0, 0);

	ElimBG_Deactivate(sdata->gGT);

#ifdef CTR_NATIVE
	// The container switch between two cup races: the funnel below has already
	// assigned the slot, the container only comes now, when the flag fully covers
	// the picture and the old level is no longer drawn (MM_NativeMenu.c).
	MM_NativeTracks_StartLoad(levelID);
#endif

	LOAD_LevelFile(levelID);
	return;
}

void MainRaceTrack_RequestLoad(s16 levelID)
{
	// THE ONE FUNNEL for runtime IDs (2026-09-28): a container track
	// arrives here with its own ID (65..99) and leaves as the donor slot
	// - Lev_ID_To_Load, the loader and every retail table never see
	// a number outside the retail band. Every other ID passes through
	// unchanged. Translation happens ONLY here; a second translator would be
	// the same fact in two places.
	levelID = MM_NativeTracks_TranslateLevel(levelID);

	// Turn off HUD
	sdata->gGT->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK;

	if (RaceFlag_IsFullyOffScreen())
	{
		RaceFlag_BeginTransition(1);
	}
	RaceFlag_ResetTextAnim();

	sdata->Loading.stage = LOAD_REQUESTED;
	sdata->Loading.Lev_ID_To_Load = levelID;
	return;
}
