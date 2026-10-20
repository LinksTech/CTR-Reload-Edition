#ifndef NATIVE_EXTOBSTACLE_H
#define NATIVE_EXTOBSTACLE_H

// THE EXTERNAL TRACK'S MOVING OBSTACLES (platform/native_extobstacle.c;
// --ext-track, see platform/engine_exttrack.c). A track may bring them in the
// section "obstacles" of exttrack.json:
//
//   "obstacles": {"version": 1, "items": [{
//       "name": "...", "kind": "animated_boxes", "clock": "timer/30", "period": 40.0,
//       "damage": 3, "kart_radius": 25,
//       "boxes": [{"anim": A, "center": [x,y,z], "half": [hx,hy,hz], "axes": [[..],[..],[..]], "phantom": "..."}],
//       "exit_plane": {"point": [x,y,z], "normal": [x,y,z], "anim": A, "ref": [x,y,z]},
//       "nodes": ["uvna_..."]}]}
//
// Each box is given in the rest pose of its part and moves with the node
// animation `anim` (an entry of the section "node_anims": the same keyed 3x4
// matrices the engine draws the part with, ExtTrack_MatAt), on the same clock
// (the logic timer / 30). A kart whose middle (posCurr + 25 up) enters a box
// grown by kart_radius is hit once per entry with the retail hazard hit of a
// vehicle on a fixed path (game/231/RB_Minecart.c): RB_Hazard_HurtDriver(driver,
// damage, NULL, 0). Without the section (every other track) and without
// --ext-track nothing of this runs.
//
// Log (always with the section; rare lines):
//   [CTR ExtObstacle] <name>: N box(es), M node(s) (K drawn by the track), period P s, damage D
//   [CTR ExtObstacle] exit <name> frame F timer T loop L.LLL s view V nodes drawable n/m
//   [CTR ExtObstacle] hit <name> seat S frame F timer T box B (phantom) kartState A -> B result R
//   [CTR ExtObstacle] hit+1 <name> seat S frame F+1 timer T+1 kartState K botDamage 0|1 (-1 for a player)
//   [CTR ExtObstacle] place seat 0 frame F timer T at X Y Z (--dev-ext-obstacle-place)
// view = the exit point lies in the view of the drawn camera of seat 0 (in
// front, NDC within +-1); nodes drawable = the obstacle's nodes registered by the
// track's drawing whose box passes the drawing's own view test this frame.

struct Level;

// --dev-ext-obstacle-place <timer> (main.c; measuring only, -1 = off): at the
// first logic frame with gGT->timer >= the value, seat 0 is put onto the path
// of the first obstacle, at the place its front part reaches 1 s later, stopped.
extern int g_cfg_extObstaclePlace;

// From the dispatcher: level load (reads the section), restart (clears the
// hit state), after the game logic of a frame (moves the boxes, hits, logs).
void ExtObstacle_OnLevelLoaded(void);
void ExtObstacle_OnRestart(void);
void ExtObstacle_OnLogicFrame(void);

// --ext-selftest obstacle: prints "ext selftest obstacle passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtObstacle_Selftest(void);

#endif
