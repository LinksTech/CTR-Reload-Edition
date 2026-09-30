#include <common.h>

enum PlayLevelConstants
{
	PLAYLEVEL_DRIVER_COUNT = 8,
	PLAYLEVEL_FINISHLINE_NEAR_DISTANCE = 1200,
	PLAYLEVEL_FINISHLINE_FAR_DISTANCE = 32000,
	PLAYLEVEL_REVERSE_DISTANCE_MAX = 1000,
	PLAYLEVEL_REVERSE_CROSSING_PENALTY = 600,
	PLAYLEVEL_UNSORTED_RANK = -1,
	PLAYLEVEL_FIRST_PLACE_RANK = 0,
	PLAYLEVEL_LOWEST_LAP_SENTINEL = -10,
	PLAYLEVEL_DISTANCE_SENTINEL = 0x3fffffff,
	PLAYLEVEL_FINAL_LAP_SOUND = 0x66,
	PLAYLEVEL_FINAL_LAP_TEXT_FRAMES = CTR_SECONDS_TO_FRAMES(3),
	PLAYLEVEL_CONFETTI_PARTICLES = 250,
	PLAYLEVEL_SINGLE_WINNER_COUNT = 1,
	PLAYLEVEL_FIRST_WINNER_INDEX = 0,
	PLAYLEVEL_WINNER_FADE_CURRENT = 0x1fff,
	PLAYLEVEL_WINNER_FADE_DESIRED = 0x1000,
	PLAYLEVEL_WINNER_FADE_STEP = 0xff78,
	PLAYLEVEL_PASS_VOICELINE_DELAY = 0x4b00,
	PLAYLEVEL_PASS_VOICELINE = 8,
	PLAYLEVEL_VOICELINE_FLAGS = 0x10,
	PLAYLEVEL_BLASTED_DAMAGE = 2,
};

#if defined(CTR_NATIVE)
// How often the rank report prints, in vblanks. Zero is off, which is the
// default: this writes a line per driver and would drown a log it was not
// asked for. --rank-report [N] turns it on, N defaulting to one second.
//
// Here rather than in a settings module because the thing it reports on is
// here, and a switch that lives away from what it switches is a switch that
// gets left on.
int g_cfg_rankReport = 0;
static int s_rankReportLastVBlank = 0;

void Platform_Log(const char *format, ...);
int Platform_GetVBlankCount(void);
#endif


void PlayLevel_UpdateLapStats(void)
{
	int bestDriverIndex;
	u8 lapCounter;
	int effectiveLapIndex;
	struct Driver *farthestHuman;
	struct Driver *currDriver;
	int distToFinish_prev;
	int distToFinish_curr;
	int minDistance;
	int bestLapIndex;
	int driverIndex;
	int finishedHumanCount;
	int currRank;
	struct GameTracker *gGT = sdata->gGT;

	finishedHumanCount = 0;
	currRank = 0;

	// driver pointer,
	// unlike other "rank" index variables
	farthestHuman = NULL;

	// find farthest-ahead human
	for (int raceOrderIndex = 0; raceOrderIndex < PLAYLEVEL_DRIVER_COUNT; raceOrderIndex++)
	{
		currDriver = gGT->driversInRaceOrder[raceOrderIndex];

		if ((currDriver != 0) && ((currDriver->actionsFlagSet & ACTION_BOT) == 0))
		{
			farthestHuman = currDriver;
			break;
		}
	}

	for (driverIndex = 0; driverIndex < PLAYLEVEL_DRIVER_COUNT; driverIndex++)
	{
		gGT->driversInRaceOrder[driverIndex] = NULL;

		currDriver = gGT->drivers[driverIndex];

		if (currDriver == NULL)
		{
			continue;
		}

		// before and after
		distToFinish_prev = currDriver->distanceToFinish_curr;
		VehLap_UpdateProgress(currDriver);
		distToFinish_curr = currDriver->distanceToFinish_curr;

		int drivenBackwards = currDriver->distanceDrivenBackwards + (distToFinish_curr - distToFinish_prev);

		// clamp minimum
		if (drivenBackwards < 0)
		{
			drivenBackwards = 0;
		}

		// clamp to max
		else if (drivenBackwards > PLAYLEVEL_REVERSE_DISTANCE_MAX)
		{
			drivenBackwards = PLAYLEVEL_REVERSE_DISTANCE_MAX;
		}

		// update distance driven backwards
		currDriver->distanceDrivenBackwards = drivenBackwards;

		// === Natty Video ===
		// https://www.youtube.com/watch?v=lDaT2rY6GKI

		// Part A: Start-line -> 32000 distToFinish
		// Part B: 32000 distToFinish -> 1200 distToFinish
		// Part C: 1200 distToFinish -> Finish-line

		if (
		    // crossed finishline (forwards)
		    (distToFinish_prev < PLAYLEVEL_FINISHLINE_NEAR_DISTANCE) && (distToFinish_curr > PLAYLEVEL_FINISHLINE_FAR_DISTANCE))
		{
			// Set racer's distance driven backwards to zero
			currDriver->distanceDrivenBackwards = 0;

			if ((currDriver->actionsFlagSet & ACTION_BEHIND_START_LINE) != 0)
			{
				currDriver->actionsFlagSet &= ~ACTION_BEHIND_START_LINE;

				goto UpdateFinishedDriverRank;
			}

			// update checkpoint with distToFinish
			currDriver->distanceToFinish_checkpoint = distToFinish_curr;

#if defined(CTR_NATIVE)
			// NITRO-PIT -> CRYSTAL ON A TRACK WITH A DRIVING PATH (2026-09-29).
			//
			// Retail arenas have no restart points, so a
			// crystal challenge never gets here (MainFrame_RenderFrame.c calls this
			// function only with a table). A container that declares Race and Crystal
			// has one - and without this switch the player would be at the finish after
			// gGT->numLaps passes, turned into a bot, and
			// MainGameEnd_Initialize would run past the crystal and the clock.
			//
			// ONLY the lap is skipped: lap time (UI_SaveLapTime,
			// gGT->lapTime), lapTime, lapIndex, the lap sound, FINAL LAP and
			// the finish. What the pass does before that stays - the
			// backwards counter, ACTION_BEHIND_START_LINE and the checkpoint
			// just above, on which COLL.c hangs the shortcut check and with it
			// lastValid for the reset. Likewise the progress per
			// frame (VehLap_UpdateProgress, the checkpoint in the branch below) and the
			// ranking. lapIndex stays 0 as in a retail arena; it is not shown
			// in the crystal HUD (UI_RenderFrame_CrystChall instead of
			// UI_RenderFrame_Racing), and so HOWL never sees a final lap.
			// gGT->numLaps is not touched.
			//
			// So the challenge can only end by its own rules: all
			// crystals (UI_RenderFrame.c) or the clock (UI_DrawLimitClock).
			// Both set ACTION_RACE_FINISHED and END_OF_RACE in the same move,
			// and below the function returns on END_OF_RACE before the end check.
			// Outside a container challenge IsCustom is never true
			// (MM_NativeCrystal.c) - arcade, adventure and time trial run
			// bit for bit as in retail.
			if (MM_NativeCrystal_IsCustom())
			{
				goto UpdateFinishedDriverRank;
			}
#endif

			// If finished last lap, clamp
			if (gGT->numLaps < (currDriver->lapIndex + 1))
			{
				lapCounter = currDriver->lapIndex;
			}

			// if this is not final lap
			else
			{
				if (
				    // If you're in Arcade, or
				    // If you're in Adventure, or
				    // If you're in Time Trial
				    ((gGT->gameMode1 & GAME_MODE_SAVE_LAP_TIME_MASK) != 0) &&

				    // player of any kind
				    (currDriver->instSelf->thread->modelIndex == DYNAMIC_PLAYER))
				{
					UI_SaveLapTime(currDriver->lapIndex, gGT->elapsedEventTime - currDriver->lapTime, currDriver->driverID);

					gGT->lapTime[currDriver->lapIndex] = gGT->elapsedEventTime - currDriver->lapTime;
				}

				// time on the clock
				currDriver->lapTime = gGT->elapsedEventTime;

				// lap counter = lap counter + 1
				currDriver->lapIndex++;

				// if farthest-ahead human
				if (currDriver == farthestHuman)
				{
					OtherFX_Play(PLAYLEVEL_FINAL_LAP_SOUND, 1);
					Voiceline_ClearTimeStamp();
				}

				lapCounter = currDriver->lapIndex;

				// If Final Lap
				if (lapCounter == (gGT->numLaps - 1))
				{
					if ((currDriver->actionsFlagSet & ACTION_BOT) == 0)
					{
						// frames, so the animation lasts 3 seconds
						sdata->finalLapTextTimer[driverIndex] = PLAYLEVEL_FINAL_LAP_TEXT_FRAMES;
					}
				}
			}

			// If did not just finish race
			if (lapCounter != gGT->numLaps)
			{
				goto UpdateFinishedDriverRank;
			}

			// === If did just finish race ===

			if ((currDriver->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
			{
				currDriver->actionsFlagSet |= ACTION_RACE_FINISHED;

				// === Run on first frame that race ends ===

				// if total event hasn't finished (gGT->gameMode1)
				if ((gGT->gameMode1 & END_OF_RACE) == 0)
				{
					// set driver placement rank, based on
					// how many drivers have finished the race
					currDriver->driverRank = sdata->numPlayersFinishedRace;
					sdata->numPlayersFinishedRace++;
				}

				// you have no weapon
				currDriver->heldItemID = HELD_ITEM_NONE;

				if ((currDriver->actionsFlagSet & ACTION_BOT) == 0)
				{
					// If this racer is in first place
					if (currDriver->driverRank == PLAYLEVEL_FIRST_PLACE_RANK)
					{
						// amount of confetti particles
						gGT->confetti.numParticles_max = PLAYLEVEL_CONFETTI_PARTICLES;
						gGT->confetti.vanishRate = PLAYLEVEL_CONFETTI_PARTICLES;

						// one person won,
						// one person gets confetti
						gGT->numWinners = PLAYLEVEL_SINGLE_WINNER_COUNT;

						u8 driverID = currDriver->driverID;

						// add driver ID to array of confetti winners
						gGT->winnerIndex[PLAYLEVEL_FIRST_WINNER_INDEX] = driverID;

						// edit window variables for confetti
						gGT->pushBuffer[driverID].fadeFromBlack_currentValue = PLAYLEVEL_WINNER_FADE_CURRENT;
						gGT->pushBuffer[driverID].fadeFromBlack_desiredResult = PLAYLEVEL_WINNER_FADE_DESIRED;
						gGT->pushBuffer[driverID].fade_step = PLAYLEVEL_WINNER_FADE_STEP;
					}
					if (currDriver->noItemTimer != 0)
					{
						currDriver->noItemTimer = 0;
						currDriver->heldItemID = HELD_ITEM_NONE;
					}

					// turn driver into robotcar
					BOTS_Driver_Convert(currDriver);
				}
				goto UpdateFinishedDriverRank;
			}
		}

		// if player did not just finish a lap (correctly)
		else
		{
			if (
			    // crossed startline backwards
			    (distToFinish_curr < PLAYLEVEL_FINISHLINE_NEAR_DISTANCE) && (distToFinish_prev > PLAYLEVEL_FINISHLINE_FAR_DISTANCE))
			{
				// automatic backwards penalty
				currDriver->distanceDrivenBackwards = PLAYLEVEL_REVERSE_CROSSING_PENALTY;
				currDriver->actionsFlagSet |= ACTION_BEHIND_START_LINE;
			}

			// if player did not JUST cross finish backwards
			else
			{
				u32 trackLen = gGT->level1->ptr_restart_points[0].distToFinish;

				if (
				    // if player did not EVER cross finish backwards
				    ((currDriver->actionsFlagSet & ACTION_BEHIND_START_LINE) == 0) &&

				    (
				        // if distance driven this frame is less than...
				        (currDriver->distanceToFinish_checkpoint - distToFinish_curr) <=

				        // level's distance to finish
				        ((trackLen >> 2) << 3)))
				{
					// save distance for next frame
					currDriver->distanceToFinish_checkpoint = distToFinish_curr;
				}
			}

		UpdateFinishedDriverRank:
			if ((currDriver->actionsFlagSet & ACTION_RACE_FINISHED) == 0)
			{
				// set rank to "unsorted"
				currDriver->driverRank = PLAYLEVEL_UNSORTED_RANK;

				// skip next 5 lines of code
				continue;
			}
		}

		// === Driver Finished Race ===

		if (currDriver->instSelf->thread->modelIndex == DYNAMIC_PLAYER)
		{
			// count humans to finish race
			finishedHumanCount = finishedHumanCount + 1;
		}

		// increase your rank in the race, someone passed you
		int newRank = currDriver->driverRank + 1;

		// get human in last
		if (currRank < newRank)
		{
			currRank = newRank;
		}
	}

	// sort all drivers that have NOT finished race
	for (; currRank < PLAYLEVEL_DRIVER_COUNT; currRank++)
	{
		// set "min" distance to max
		minDistance = PLAYLEVEL_DISTANCE_SENTINEL;

		// set "highest" lap to min
		bestDriverIndex = PLAYLEVEL_UNSORTED_RANK;

		// lap index
		bestLapIndex = PLAYLEVEL_LOWEST_LAP_SENTINEL;

		// look for "next" farthest driver,
		// out of all unsorted drivers remaining
		for (driverIndex = 0; driverIndex < PLAYLEVEL_DRIVER_COUNT; driverIndex++)
		{
			// get current driver
			currDriver = gGT->drivers[driverIndex];

			if (currDriver == NULL)
			{
				continue;
			}

			if (currDriver->driverRank != PLAYLEVEL_UNSORTED_RANK)
			{
				continue;
			}

			// driver lap index
			effectiveLapIndex = currDriver->lapIndex;

			if ((currDriver->actionsFlagSet & ACTION_BEHIND_START_LINE) != 0)
			{
				effectiveLapIndex -= 1;
			}

			if (
			    // new highest lap
			    (effectiveLapIndex > bestLapIndex) ||

			    // OR

			    (
			        // same lap
			        (bestLapIndex == effectiveLapIndex) &&

			        // AND

			        // new lowest distance (max progress)
			        ((s32)currDriver->distanceToFinish_curr < minDistance)))
			{
				// set new min distToFinish (max progress)
				minDistance = currDriver->distanceToFinish_curr;

				// highest lap
				bestLapIndex = effectiveLapIndex;

				// index of driver closest to finish
				bestDriverIndex = driverIndex;
			}
		}

		if (bestDriverIndex != PLAYLEVEL_UNSORTED_RANK)
		{
			// If traffic lights run out
			if (gGT->trafficLightsTimer < 1)
			{
				gGT->drivers[bestDriverIndex]->driverRank = currRank;
			}

			// if traffic lights >= 1
			else
			{
				// set every driver position rank,
				// to the order that they spawn on the starting line
				gGT->drivers[bestDriverIndex]->driverRank = sdata->kartSpawnOrderArray[bestDriverIndex];
				gGT->humanPlayerPositions[bestDriverIndex] = sdata->kartSpawnOrderArray[bestDriverIndex];
			}
		}
	}

	for (driverIndex = 0; driverIndex < PLAYLEVEL_DRIVER_COUNT; driverIndex++)
	{
		// get pointer to each player structure
		currDriver = gGT->drivers[driverIndex];

		if (currDriver == NULL)
		{
			continue;
		}

		// should be impossible to be -1 here
		if (currDriver->driverRank > PLAYLEVEL_UNSORTED_RANK)
		{
			gGT->driversInRaceOrder[currDriver->driverRank] = currDriver;
		}
	}

#if defined(CTR_NATIVE)
	// EVERY INPUT THE RANK IS MADE OF, IN ONE LINE PER DRIVER.
	//
	// The rank comes out of exactly four things and nothing else: whether the
	// driver slot is filled at all, which lap the driver is on, how far along
	// that lap, and whether the countdown has finished. A frozen position is one
	// of those four standing still - and which one is not a thing to reason
	// about from the outside when it can simply be read.
	//
	// distToFinish is where it usually breaks. It is written by
	// VehLap_UpdateProgress, which returns without writing anything when the
	// driver has no valid quad under it or the level publishes no checkpoint
	// table - and then every driver keeps the number it had. The sort then picks
	// the lowest, ties resolve to the lowest driver index, and driver 0 is first
	// for the rest of the race no matter what anybody does.
	//
	// So the four columns say it outright: cp is what VehLap was given, dist is
	// what it wrote, lap and rank are what came out. cp = -1 on every line means
	// the progress step is not running; equal dist on every line means it ran
	// once and then stopped.
	//
	// Reading only. Off unless asked for.
	if (g_cfg_rankReport > 0)
	{
		const int vblank = Platform_GetVBlankCount();

		if ((vblank - s_rankReportLastVBlank) >= g_cfg_rankReport)
		{
			s_rankReportLastVBlank = vblank;

			Platform_Log("[CTR Rank] vblank %d: %d checkpoint node(s), traffic lights %d, %d human(s)\n", vblank,
			             (gGT->level1 != NULL) ? (int)gGT->level1->cnt_restart_points : -1, (int)gGT->trafficLightsTimer, (int)gGT->numPlyrCurrGame);

			for (driverIndex = 0; driverIndex < PLAYLEVEL_DRIVER_COUNT; driverIndex++)
			{
				struct Driver *d = gGT->drivers[driverIndex];

				if (d == NULL)
				{
					Platform_Log("[CTR Rank]   driver %d: empty slot\n", driverIndex);
					continue;
				}

				// cp is asked of the field the driver's own kind is read from,
				// which is not the same field for both.
				//
				// This column printed lastValid->checkpointIndex for everybody
				// on its first outing. A bot never writes that field, so every
				// bot line said 71 and the whole first reading of this log was
				// wrong - the bots looked frozen on the finish line when they
				// were driving normally, and it cost a run and a wrong
				// diagnosis. An instrument that reports a field its subject does
				// not use is worse than one that reports nothing.
				const int checkpointNow = ((d->actionsFlagSet & ACTION_BOT) != 0)
				                              ? (int)d->botData.ai_quadblock_checkpointIndex
				                              : ((d->lastValid != NULL) ? (int)d->lastValid->checkpointIndex : -1);

				Platform_Log("[CTR Rank]   driver %d: rank %d, lap %d, dist %d, cp %d, %s%s%s\n", driverIndex, (int)d->driverRank, (int)d->lapIndex,
				             (int)d->distanceToFinish_curr, checkpointNow,
				             ((d->actionsFlagSet & ACTION_BOT) != 0) ? "bot" : "human",
				             ((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0) ? ", finished" : "",
				             ((d->actionsFlagSet & ACTION_BEHIND_START_LINE) != 0) ? ", behind line" : "");

				// WHERE A BOT'S CHECKPOINT COMES FROM, WHICH IS NOT WHERE A
				// HUMAN'S COMES FROM.
				//
				// A human gets it off the quad under the kart. A bot gets it
				// out of the nav frame it is currently steering to, through the
				// field the decompilation calls goBackCount - which the walk in
				// BOTS.c compares against checkpoint.currentIndex, so whatever
				// its name says, it holds a checkpoint index.
				//
				// The first run showed every bot frozen on one checkpoint while
				// the human's swept the whole track. Two different things do
				// that and they need opposite fixes: the frame pointer standing
				// still, or the frame pointer walking while the field read out
				// of it is the wrong one. frame says which - a moving index
				// beside a standing cp is the second, a standing index is the
				// first.
				if ((d->actionsFlagSet & ACTION_BOT) != 0)
				{
					const int botPath = (int)d->botData.botPath;
					const struct NavFrame *frame = d->botData.botNavFrame;
					const struct NavFrame *first =
					    ((botPath >= 0) && (botPath < (int)len(sdata->NavPath_ptrNavFrameArray))) ? sdata->NavPath_ptrNavFrameArray[botPath] : NULL;

					Platform_Log("[CTR Rank]     bot %d: path %d, frame %d, goBack %d, flags 0x%x, currIndex %d\n", driverIndex, botPath,
					             ((frame != NULL) && (first != NULL)) ? (int)(frame - first) : -1, (frame != NULL) ? (int)frame->goBackCount : -1,
					             (unsigned)d->botData.botFlags, (int)d->checkpoint.currentIndex);
				}
			}
		}
	}
#endif

	for (driverIndex = 0; driverIndex < gGT->numPlyrCurrGame; driverIndex++)
	{
		// pointer to each player structure
		currDriver = gGT->drivers[driverIndex];

		if (currDriver == NULL)
		{
			continue;
		}

		int currRank = currDriver->driverRank;

		if ((PLAYLEVEL_UNSORTED_RANK < currRank) && (PLAYLEVEL_PASS_VOICELINE_DELAY < gGT->elapsedEventTime) &&
		    ((s8)gGT->humanPlayerPositions[driverIndex] < currRank))
		{
			int characterID = data.characterIDs[gGT->driversInRaceOrder[currRank - 1]->driverID];

			// Make driver talk
			Voiceline_RequestPlay(PLAYLEVEL_PASS_VOICELINE, characterID, PLAYLEVEL_VOICELINE_FLAGS);
		}
		gGT->humanPlayerPositions[driverIndex] = currRank;
	}

	// If already finished race
	if ((gGT->gameMode1 & END_OF_RACE) != 0)
	{
		return;
	}

	int humanPlayerCount = gGT->numPlyrCurrGame;

	// Check if race should end
	if ((
	        // 1P game, with 1 human finished
	        (humanPlayerCount == 1) && (finishedHumanCount > 0)

	            ) ||

	    (
	        // Multiplayer VS, all finished except one
	        (humanPlayerCount > 1) && ((gGT->gameMode1 & ARCADE_MODE) == 0) && (finishedHumanCount >= (humanPlayerCount - 1))) ||

	    (
	        // Arcade mode, all humans finished
	        ((gGT->gameMode1 & ARCADE_MODE) != 0) && (humanPlayerCount <= finishedHumanCount)))
	{
		// End race for all drivers
		for (currRank = 0; currRank < PLAYLEVEL_DRIVER_COUNT; currRank++)
		{
			// Get address of each player structure
			currDriver = gGT->drivers[currRank];

			if (currDriver == NULL)
			{
				continue;
			}

			if ((currDriver->actionsFlagSet & ACTION_RACE_FINISHED) != 0)
			{
				continue;
			}

			currDriver->actionsFlagSet |= ACTION_RACE_FINISHED;

			// remove weapon
			currDriver->heldItemID = HELD_ITEM_NONE;

			// skip AIs
			if ((currDriver->actionsFlagSet & ACTION_BOT) != 0)
			{
				continue;
			}

			// === VS Mode ===

			// Make the player Blasted
			VehPickState_NewState(currDriver, PLAYLEVEL_BLASTED_DAMAGE, currDriver, 0);

			// Reduce counters for AttackingPlayer and AttackedByPlayer
			currDriver->numTimesAttackedByPlayer[currDriver->driverID]--;
			currDriver->numTimesAttackingPlayer[currDriver->driverID]--;
		}

		MainGameEnd_Initialize();
	}
}
