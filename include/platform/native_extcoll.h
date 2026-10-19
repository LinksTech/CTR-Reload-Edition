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

// --ext-selftest coll: prints "ext selftest coll passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtColl_Selftest(void);

#endif
