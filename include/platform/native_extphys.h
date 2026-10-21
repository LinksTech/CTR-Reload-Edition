#ifndef NATIVE_EXTPHYS_H
#define NATIVE_EXTPHYS_H

#include <platform/native_extcoll.h> // ExtCollHit (ExtPhys_GroundProbe)

// THE EXTERNAL TRACK'S PHYSICS VALUES (platform/native_extphys.c; --ext-track,
// see platform/engine_exttrack.c). Owner: the collision/physics part.
//
// A track may bring its own values in the section "physics" of exttrack.json:
//
//   "physics": {
//     "version": 1,
//     "gravity_percent": 100,                  // 25..400, the share of the game's gravity
//     "terrain": {"HARDPACK": {"speed": 256, "accel": 256, "counter_steer": 256,
//                              "turn_lean": 256, "friction": 256,
//                              "turn_angle": 256, "turn_response": 256}},
//     "turbo_pad": {"reserves": 960, "fire_level": 256}
//   }
//
// THE NEUTRAL RULE: a value equal to the game's own (gravity 100 %, a terrain
// field equal to its row of the terrain table, the retail turbo pad) is kept
// as "not set". Without the section, with a neutral section and without
// --ext-track every hook below hands its input back unchanged, without any
// arithmetic - the same code path as before, bit for bit.

struct Terrain;

// From the dispatcher ExtTrack_OnLevelLoaded, once per level load of the
// active external track: reads the section, logs one line.
void ExtPhys_OnLevelLoaded(void);

// game/Vehicle/VehPhysForce.c VehPhysForce_OnGravity: the gravity of a frame
// before it is scaled by the frame time; unchanged unless the track sets
// gravity_percent.
int ExtPhys_ScaleGravity(int gravity);
// The same for a driver with the player physics (VehPhysForce_OnGravity): the
// track's share, then the share of the gravity volume (physics.gravity_volumes:
// boxes of any turn) that holds the driver; one log line per entering and
// leaving a volume:
//   [CTR ExtPhys] seat S enter gravity volume V (name, P%) frame F pos X Y Z | seat S leave gravity volume V (name) frame F
// Without volumes exactly ExtPhys_ScaleGravity.
struct Driver;
int ExtPhys_ScaleGravityFor(const struct Driver *d, int gravity);

// game/Vehicle/VehAfterColl.c VehAfterColl_GetTerrain: the row the game picked
// for a terrain type, or the track's copy of it with its own values.
struct Terrain *ExtPhys_Terrain(unsigned char terrainType, struct Terrain *row);

// game/Vehicle/VehPhysForce.c VehPhysForce_CollideDrivers: the reserves and
// the fire level of a (normal) turbo pad.
int ExtPhys_PadReserves(int reserves);
int ExtPhys_PadFireLevel(int fireLevel);

// THE BOOST LOG (gate A3), only with --ext-track on its level slot.
// game/Vehicle/VehPhysForce.c VehPhysForce_CollideDrivers: a driver got a pad
// boost this frame (turbo or super pad step flag).
struct Driver;
void ExtPhys_NotePadBoost(const struct Driver *d);
// From the dispatcher ExtTrack_OnLogicFrame after the game logic of a frame:
// the frame count (as the drive log's tick) and the boost lines
//   [CTR ExtPhys] boost seat S frame F pad P pos X Y Z speed V0 -> peak V1 at +K frame(s), normal max VMAX
void ExtPhys_OnLogicFrame(void);

// THE ANTI-GRAVITY ZONES (B2). A zone is a surface: the ground records of the
// collision nodes with "zone": n, listed in physics.zones. Every function is
// inert (returns 0 / leaves its output at world up) unless the track lists
// zones, the collision has zone records and the track holds the level.
//   [CTR ExtZone] seat S enter zone Z frame F lap L | seat S leave zone Z frame F lap L (why)
//   [CTR ExtZone] seat S frame F zone Z ground G tilt A up X Y Z pos X Y Z      (every frame in a zone; A degrees, %.1f)
//   [CTR ExtZone] seat S after zone Z: gravity 0 -4096 0, ground at +N frame(s)
struct QuadBlock;
// Z-a: game/COLL.c COLL_FIXED_PlayerSearch, after the ground search.
void ExtPhys_AfterGroundSearch(const struct Driver *d);
// The same rule for a bot (game/BOTS.c, M4): its ground record (NULL in the
// air) and the ground normal.
void ExtPhys_BotGround(const struct Driver *bot, const struct QuadBlock *ground, const SVec3 *normal);
// Respawn (M3): put the driver into zone `zone` (the record's zone,
// ExtColl_ProbeSurface) with this up, its normals set to it; zone 0 = world.
void ExtPhys_SeedDriver(struct Driver *d, const SVec3 *up, int zone);
// The driver's zone id (0 none); its up (1 in a zone, else 0 and world up);
// its forward (the drawn frame's forward; returns 1 in a zone).
int ExtPhys_DriverZone(const struct Driver *d);
int ExtPhys_DriverUp(const struct Driver *d, SVec3 *up);
int ExtPhys_DriverForward(const struct Driver *d, SVec3 *fwd);
// Z-b: game/Vehicle/VehPhysForce.c VehPhysForce_OnGravity - the frame's gravity
// (times the frame time, negative = down) as a world vector against the zone's
// up; 1 = in a zone (out set), 0 = the game's own world -Y.
int ExtPhys_ZoneGravity(const struct Driver *d, int gravity, s16 out[3]);
// Fixup round 2: with the section
//   "maglev_forces": {"gravity": 9.81, "downforce": 4.5, "downforce_air": 11,
//     "downforce_air_time": 0.1, "lat_friction": 55, "lat_friction_hi_turn": 60,
//     "hi_turn_cos": 0.966976, "lat_friction_air": 60}      (m/s^2, s; all keys or none)
// the zone gravity of ExtPhys_ZoneGravity has the source game's size (downforce
// * gravity, in the air downforce_air, its gravity volume's share in the air),
// and ExtPhys_ZoneSideGrip gives the sideways velocity of a frame the source
// game's way. game/Vehicle/VehPhysForce.c VehPhysForce_OnGravity: x, y, z the
// velocity in matrixMovingDir before the gravity, gravityX the gravity's side
// part; 1 = in a zone with maglev_forces and kartState KS_NORMAL (*outX = the side
// velocity the game then keeps instead of its own side friction), 0 = untouched.
int ExtPhys_ZoneSideGrip(const struct Driver *d, int x, int y, int z, int gravityX, int *outX);
// "maglev_boxes_win": 1 (with maglev_volumes): while a driver is inside the
// maglev box that switched its zone, the box decides and the surface does not
// switch it back; out of every box of that kind the surface rule holds again.
// Without the key (0) the frame's surface has the last word as before.
//   [CTR ExtZone] seat S out of the on|off box frame F
// Z-e: speed <-> velocity in the surface frame of AxisAngle1 (right, normal,
// forward at heading 0, the convention of VehPhysForce_RotAxisAngle); 1 = in a
// zone (converted), 0 = untouched.
int ExtPhys_ZoneLocalToWorld(const struct Driver *d, const s32 local[3], Vec3 *world);
int ExtPhys_ZoneWorldToLocal(const struct Driver *d, const Vec3 *world, s32 local[3]);
// The heading (0..4095, the convention of d->angle) towards a point dx/dy/dz
// away: in a zone taken in the surface frame (as d->angle is there), else
// untouched (the caller keeps its ratan2(dx, dz)); 1 = in a zone (*heading set).
int ExtPhys_ZoneHeadingTo(const struct Driver *d, int dx, int dy, int dz, int *heading);

// Night run 1 (M1): the anti-gravity state of a driver of the player physics,
// for the telemetry - reads only, writes nothing. 1 = the zones are in force
// for the driver; 0 = not (out holds the neutral state: no zone, world down,
// world up, -1 for every index).
typedef struct
{
	int maglev;         // in a zone (the source game's maglev state)
	int zone;           // its id, 0 none
	int ground;         // stands on a record (as ExtPhys_AfterGroundSearch)
	int groundZone;     // the zone id of that record, 0 none (a zone record = the source game's maglev surface, Z9)
	int terrain;        // the record's terrain type, -1 none
	int trackPercent;   // physics.gravity_percent (100 neutral)
	int volume;         // the gravity volume the gravity was last scaled by, -1 none
	int volumePercent;  // its share (100 none)
	int zonePercent;    // the zone's gravity_percent (100 outside)
	int maggravBox;     // the maggrav box, -1 none
	int spline;         // its spline, -1 none
	int node;           // the segment on it, -1 none
	int gravFromSpline; // this frame's zone gravity came from the spline
	int gravFrame;      // the frame of the zone gravity read (log frame), 0: none this frame (the game's world -Y)
	SVec3 gravDir;      // the direction of this frame's gravity (4096, "down")
	SVec3 alignUp;      // the up the kart is turned to (Reload: the zone's up; world up outside)
} ExtPhysProbe;
int ExtPhys_ProbeState(const struct Driver *d, ExtPhysProbe *out);

// THE GROUND PROBE OF THE PLAYER PHYSICS for any driver (fixup round 1: the CPU
// drivers on the external nav paths, game/BOTS.c): the window of
// COLL_FIXED_PlayerSearch (EXT_PHYS_PROBE_TOP over pos to EXT_PHYS_PROBE_BOTTOM
// under it) along the driver's up - its zone's up (ExtPhys_DriverUp) in a zone,
// else upHint (NULL = world up) - with the game's ray test and the player's
// flags (ExtColl_ProbeSurface: GROUND | COLLISION_SURFACE, NO_COLLISION_RESPONSE
// ignored, the leaf test COLL_FIXED_BSPLEAF_TestQuadblocks). pos in the
// driver's 1/256 units. 1 = ground hit (*hit set); *upUsed = the probe's up.
// Reads only; 0 while the external collision is not built.
enum
{
	EXT_PHYS_PROBE_TOP = 0x80,    // COLL_FIXED_PLAYER_SEARCH_TOP_Y_OFFSET (game/COLL.c)
	EXT_PHYS_PROBE_BOTTOM = 0x100, // COLL_FIXED_PLAYER_SEARCH_BOTTOM_Y_OFFSET
	// A CPU driver sits this far over the probed ground along its up: the game's
	// stored planes lie up to ~2.1 off the exact triangles (ExtColl check) and the
	// hit point is rounded to whole units - 4 keeps the kart on the driving side.
	EXT_PHYS_BOT_LIFT = 4,
	// A CPU driver never takes ground more than this far over its own height
	// along its up (one tick of a steep climb; a face of an overhang or a bridge
	// inside the probe's window above the kart is no floor for it).
	EXT_PHYS_BOT_STEP = 64,
	// How far under the kart the exact fallback looks for ground in the open
	// (the kart high in the air over a dip: the ground it falls to).
	EXT_PHYS_BOT_FALL_REACH = 2048,
};
int ExtPhys_GroundProbe(const struct Driver *d, const s32 pos[3], const SVec3 *upHint, ExtCollHit *hit, SVec3 *upUsed);
// The ground of a CPU driver (game/BOTS.c, the external nav paths): the player's
// probe above; a hit more than `above` over pos (along the probe's up) is no
// ground. Without a ground hit the exact line on the same collision
// records (ExtColl_LineQuery, ground class, both faces) along the probe's up,
// from `above` over pos down to `below` under it, the nearest face, taken when
// the kart lies at most `behindMax` behind it (hit->normal = its front normal,
// 4096 = 1.0). 1 = ground (*fallback 1 when the line found it). Open track:
// EXT_PHYS_BOT_STEP over, EXT_PHYS_PROBE_BOTTOM under, EXT_PHYS_BOT_BEHIND_MAX;
// a zone frame: EXT_PHYS_ZONE_REACH each way and behind (the AI line dives under
// a tube's floor where the tube ends - the kart stays on the floor up to its edge).
enum
{
	EXT_PHYS_BOT_BEHIND_MAX = 32,
	EXT_PHYS_ZONE_REACH = 256,
};
int ExtPhys_BotGroundAt(const struct Driver *d, const s32 pos[3], const SVec3 *upHint, int below, int above, int behindMax, ExtCollHit *hit, SVec3 *upUsed,
                        int *fallback);

// --ext-selftest phys: prints "ext selftest phys passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtPhys_Selftest(void);

#endif
