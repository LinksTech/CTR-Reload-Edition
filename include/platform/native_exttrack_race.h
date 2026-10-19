#ifndef NATIVE_EXTTRACK_RACE_H
#define NATIVE_EXTTRACK_RACE_H

// THE EXTERNAL TRACK'S RACE LOGIC (platform/native_exttrack_race.c; --ext-track,
// see platform/engine_exttrack.c): start grid, checkpoints and laps, respawn.
// The section "race" of exttrack.json replaces the donor level's
// DriverSpawn[8] and checkpoint table and clears its underwater bit; the game
// logic reads them unchanged. Logs for D4/D6/D7 and the one trigger of
// --dev-ext-respawn. Without --ext-track none of these is reached.

struct Level;

// From the dispatcher, once per level load of the active external track.
void ExtRace_OnLevelLoaded(struct Level *lev);
// From the dispatcher, once per logic frame of the active external track
// (after the game logic of the frame, game/MAIN/MainMain.c).
void ExtRace_OnLogicFrame(void);
// --ext-selftest race: prints "ext selftest race passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtRace_Selftest(void);

#endif
