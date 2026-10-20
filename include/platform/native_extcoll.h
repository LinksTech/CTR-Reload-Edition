#ifndef NATIVE_EXTCOLL_H
#define NATIVE_EXTCOLL_H

// THE EXTERNAL TRACK'S COLLISION (platform/native_extcoll.c; --ext-track, see
// platform/engine_exttrack.c). Owner: the collision part of the external track.
// SKELETON (round of 2026-10-19): every function is an empty stub that says so
// in the log once; without --ext-track none of them is reached.

struct Level;
struct mesh_info;

// From the dispatcher ExtTrack_OnLevelLoaded, once per level load of the
// active external track (gGT->level1 is set).
void ExtColl_OnLevelLoaded(struct Level *lev);

// The world mesh every collision reader asks for. Today (stub, and always
// without --ext-track): level->ptr_mesh_info, the same pointer as before.
struct mesh_info *COLL_WorldMesh(struct Level *level);

// After the game logic of a frame (dispatcher ExtTrack_OnLogicFrame - the call
// is M1's line): read-only log of seat 0 on the external track's collision
// (ground frames per terrain, wall contacts, the lowest height), every 300
// frames and at exit. Returns at once unless the collision is built and the
// track holds the level.
void ExtColl_OnLogicFrame(void);

// The turbo pads of the collision (records of class trigger: retail pad flags
// 0xA040, terrain_type = the trigger bits): how many pad numbers, a pad's box
// (1 = it has records), and the pad whose box (grown by margin) holds a point
// (-1 none). 0 / -1 while nothing is built.
struct BoundingBox;
int ExtColl_PadCount(void);
int ExtColl_PadBox(int pad, struct BoundingBox *box, int *records, int *bits);
int ExtColl_PadAt(int x, int y, int z, int margin);

// THE ANTI-GRAVITY ZONES (B2): the zone of a record (0 = none or not a record
// of the external collision), how many records lie in zones, and a ray in any
// direction with the game's own ray test (COLL_SearchBSP_CallbackQUADBLK on the
// external mesh, an own scratchpad): from `from` along dir (unit vector, 4096 =
// 1.0) for len units; it hits faces whose normal points against dir (ground,
// walls; not kill planes, not pads). 1 = hit with the point, the game's plane
// normal, the record, its zone and terrain.
struct QuadBlock;
typedef struct
{
	SVec3 pos;
	SVec3 normal;
	const struct QuadBlock *record;
	int zone;
	int terrain;
} ExtCollHit;
int ExtColl_RecordZone(const struct QuadBlock *q);
int ExtColl_ZoneRecords(void);
int ExtColl_ProbeSurface(const SVec3 *from, const SVec3 *dir, int len, ExtCollHit *out);

// --ext-selftest coll: prints "ext selftest coll passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtColl_Selftest(void);

#endif
