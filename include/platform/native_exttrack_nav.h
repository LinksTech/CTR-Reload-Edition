#ifndef NATIVE_EXTTRACK_NAV_H
#define NATIVE_EXTTRACK_NAV_H

// THE EXTERNAL TRACK'S CPU PATHS (platform/native_exttrack_nav.c; --ext-track,
// see platform/engine_exttrack.c). SKELETON (round of 2026-10-19): every
// function is an empty stub that says so in the log once; without --ext-track
// none of them is reached.

struct Level;

// From the dispatcher, once per level load of the active external track.
void ExtNav_OnLevelLoaded(struct Level *lev);
// --ext-selftest nav: prints "ext selftest nav passed: N checks, 0 failures"
// (or FAILED); returns 0 / 1.
int ExtNav_Selftest(void);

#endif
