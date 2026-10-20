#ifndef NATIVE_EXTTRACK_RACE_H
#define NATIVE_EXTTRACK_RACE_H

// THE EXTERNAL TRACK'S RACE LOGIC (platform/native_exttrack_race.c; --ext-track,
// see platform/engine_exttrack.c): start grid, checkpoints and laps, respawn.
// The section "race" of exttrack.json replaces the donor level's
// DriverSpawn[8] and checkpoint table and clears its underwater bit; the game
// logic reads them unchanged. Optional: a respawn node per node and a kill
// height. Logs for D4/D6/D7 and the triggers of --dev-ext-respawn. Without
// --ext-track none of these is reached (ExtRace_RespawnNode answers with the
// index it was given).

struct Level;
struct Driver;

// From the dispatcher, once per level load of the active external track.
void ExtRace_OnLevelLoaded(struct Level *lev);
// From the dispatcher, once per logic frame of the active external track
// (after the game logic of the frame, game/MAIN/MainMain.c).
void ExtRace_OnLogicFrame(void);
// --ext-selftest race: prints "ext selftest race passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtRace_Selftest(void);

// game/Vehicle/VehStuckProc.c: the node a kart is put back at when its last
// ground names node index (the data's "respawn"; default the node itself).
// Outside the external track's table: index unchanged.
int ExtRace_RespawnNode(const struct Level *lev, int index);
// game/Vehicle/VehStuckProc.c, right after the respawn teleport of a human kart:
// logs where it was placed and sets the physics state for the new place.
void ExtRace_OnRespawned(struct Driver *d);
// main.c, --dev-ext-respawn <item>[,<item>...] (item: race frame 0..1000000
// or node cpN, up to 8): the check before any window (1 = valid) and the take.
int ExtRace_RespawnListValid(const char *text);
void ExtRace_SetRespawnList(const char *text);

#endif
