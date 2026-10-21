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

// The vertex array a record's u16 indices point into (game/COLL.c's vertex
// loader, VehStuckProc's quad centre): always mesh->ptrVertexArray, except for
// the external collision built with vertex pages (collision.vertex_pages, a
// track with more than 65535 vertices), whose records count from their page's
// first vertex.
struct QuadBlock;
struct LevVertex;
struct LevVertex *COLL_RecordVertexArray(struct mesh_info *mesh, const struct QuadBlock *quad);

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

// THE LINE QUERY (for measuring and for the CPU drivers): which record's
// triangle a line meets, exactly (double arithmetic on the record's own corners,
// the triangles the game's tests read; the game's stored planes lie up to ~2
// units off them). Both faces count. From `from` (world units) along dir (any
// length, normalised inside) for t in [tMin, tMax] units (tMin may be
// negative: a line through `from`); `classes` = a mask of EXT_COLL_LINE_*; the
// hit with the smallest |t| (nearestAbs = 1) or the smallest t (0). Read only:
// no scratchpad, no game state. 1 = hit (out set), 0 = none or nothing built.
enum
{
	EXT_COLL_LINE_GROUND = 1,  // ground records (anti-gravity zones included); no wall, pad or kill record
	EXT_COLL_LINE_WALL = 2,
	EXT_COLL_LINE_KILL = 4,
	EXT_COLL_LINE_TRIGGER = 8, // turbo pads
	EXT_COLL_LINE_ALL = 15,
};
typedef struct
{
	double pos[3];    // the point on the triangle, world units
	double t;         // signed distance from `from` along the unit dir
	double normal[3]; // the triangle's front normal, unit length: the side the game's one-sided test is hit from (ground: the driving side)
	int front;        // 1 = the line meets the front face first along +dir (dot(dir, normal) < 0)
	int cls;          // the record's EXT_COLL_LINE_* bit
	int zone;         // its anti-gravity zone, 0 none
	int terrain;      // terrain_type (pads: the trigger bits)
	int recordIndex;  // its index in the external collision (blockID)
	const struct QuadBlock *record;
} ExtCollLineHit;
int ExtColl_LineQuery(const double from[3], const double dir[3], double tMin, double tMax, int classes, int nearestAbs, ExtCollLineHit *out);

// --ext-selftest coll: prints "ext selftest coll passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtColl_Selftest(void);

#endif
