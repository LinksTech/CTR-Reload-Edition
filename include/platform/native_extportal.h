#ifndef NATIVE_EXTPORTAL_H
#define NATIVE_EXTPORTAL_H

// THE EXTERNAL TRACK'S TELEPORT PORTALS (platform/native_extportal.c; --ext-track,
// see platform/engine_exttrack.c). Without --ext-track, and for a track whose
// exttrack.json has no section "portals", none of this does anything.
//
// The section "portals" (written by the converter from the track's portal
// list): {"version": 1, "items": [{"index", "target", "enabled", "forward",
// "center": [x, y, z], "axes": [[x], [y], [z]], "half": [hx, hy, hz]}, ...]}
// in world units; axes are the portal's local x, y, z as unit vectors (4096 =
// 1.0), the thin local y being the gate's normal; half = the box's half
// extents along them.
//
// THE RULE (measured by the converter on the source's time-trial ghost): a kart enters
// portal k when its centre lies in k's box while it moves along k's local -y;
// it leaves at k's target, at the target's centre, moving along the target's
// local +y. A portal reached along +y is no entry (the pairs do not loop).
//
// STATE OF NIGHT RUN 1: reader, geometry and selftest only. Nothing calls the
// entry points from the game's frame yet (the move of a driver touches the
// anti-gravity physics and the camera of other parts and waits for them).

enum
{
	EXTPORTAL_MAX = 32,
	EXTPORTAL_UNIT = 4096,
};

typedef struct
{
	int center[3];
	int axes[3][3]; // rows: local x, y, z (unit * 4096)
	int half[3];
	int target;
	int enabled;
	int forward;
} ExtPortal;

struct Level;

// Reads the section "portals" of the active track (once per level load). A
// section that fails its check gives no portal and one log line why.
void ExtPortal_OnLevelLoaded(struct Level *lev);

// How many portals the track has (0 = none or refused).
int ExtPortal_Count(void);
const ExtPortal *ExtPortal_Get(int k);

// The portal a point (world units) moving with vel enters: inside its box and
// moving along its local -y (dot < 0); -1 none. A disabled portal or one
// without a valid target is never entered.
int ExtPortal_Entered(const int pos[3], const int vel[3]);

// Where a kart that entered portal k comes out: the target's centre and the
// target's local +y as the new direction of travel at the given speed (units
// per the caller's step), and its heading in game angles (0..4095, ratan2 of
// x and z as the game's yaw). 1 = done, 0 = no such portal.
int ExtPortal_Exit(int k, int speed, int outPos[3], int outVel[3], int *outYaw);

int ExtPortal_Selftest(void);

#endif
