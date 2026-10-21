#ifndef NATIVE_EXTCHASECAM_GLUE_H
#define NATIVE_EXTCHASECAM_GLUE_H

// THE PORTED CHASE CAMERA IN THE RACE (platform/native_extchasecam_glue.c;
// --ext-track only, and only on a track whose physics.source asks for the port
// and whose start world holds the camera's objects).
//
// The port of the source's chase camera (platform/native_extchasecam.c) runs on
// its own copy of the start world (ExtZp_WorldLoad). Every follow frame of seat 0
// the glue writes the kart as the camera reads it (the camera's kart record: its
// position, orientation and velocity; the kart state block: ground, maglev, air,
// slide, speed; the clock's frame time), calls the camera's per-frame update and
// hands back the eye, the view direction, the up and the field of view in
// Reload's world (y up, world units). The camera's ground ray and wall sweep are
// answered by Reload's collision. On every other track nothing here runs.

#include <platform/native_extzonemem.h>

typedef struct ExtChaseGlueIn
{
	int dtMs;            // the frame time Reload's logic used (gGT->elapsedTimeMS)
	int frame;           // gGT->timer (step log only)
	double pos[3];       // the kart's position (world units, posCurr / 256)
	double vel[3];       // the kart's velocity (world units per second)
	double fwd[3], up[3];// the kart's forward and up (unit length, Reload axes)
	int ground;          // touching the ground
	int maglev;          // in an anti-gravity zone
	int slide;           // drifting
	int freeze;          // the kart is held (mask grab): the camera keeps its view
} ExtChaseGlueIn;

typedef struct ExtChaseGlueOut
{
	double eye[3];       // world units
	double fwd[3];       // unit
	double up[3];        // unit, across fwd
	double fovDeg;       // horizontal field of view
} ExtChaseGlueOut;

// 1 = the ported camera is ready for the current track (checked once per level;
// the reason is logged when it is not).
int ExtChaseGlue_Ready(const void *level);
// One frame of the ported camera; 1 = out holds the picture.
int ExtChaseGlue_Step(const void *level, const ExtChaseGlueIn *in, ExtChaseGlueOut *out);
// A teleport of seat 0: the camera starts afresh at the kart in the next step.
void ExtChaseGlue_OnTeleport(void);
// Selftest (from the chase camera's selftest): the kart orientation's quaternion
// against the port's own quaternion-to-matrix function, the velocity mapping.
void ExtChaseGlue_Selftest(int *n, int *failures);

#endif
