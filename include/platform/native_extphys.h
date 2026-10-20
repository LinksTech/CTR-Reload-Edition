#ifndef NATIVE_EXTPHYS_H
#define NATIVE_EXTPHYS_H

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
// Z-e: speed <-> velocity in the surface frame of AxisAngle1 (right, normal,
// forward at heading 0, the convention of VehPhysForce_RotAxisAngle); 1 = in a
// zone (converted), 0 = untouched.
int ExtPhys_ZoneLocalToWorld(const struct Driver *d, const s32 local[3], Vec3 *world);
int ExtPhys_ZoneWorldToLocal(const struct Driver *d, const Vec3 *world, s32 local[3]);
// The heading (0..4095, the convention of d->angle) towards a point dx/dy/dz
// away: in a zone taken in the surface frame (as d->angle is there), else
// untouched (the caller keeps its ratan2(dx, dz)); 1 = in a zone (*heading set).
int ExtPhys_ZoneHeadingTo(const struct Driver *d, int dx, int dy, int dz, int *heading);

// --ext-selftest phys: prints "ext selftest phys passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtPhys_Selftest(void);

#endif
