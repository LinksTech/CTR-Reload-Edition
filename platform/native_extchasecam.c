// THE SOURCE GAME'S CHASE CAMERA, PORTED STEP BY STEP (include/platform/native_extchasecam.h).
//
// One C function per function of the source's chase camera (per-frame update,
// timers, velocity mean, look direction and pivot frame, camera up, eye with
// ground and wall correction, look point, view matrix), in the memory form of
// the zone port (include/platform/native_extzonemem.h, the conventions in
// include/platform/native_extzonephys.h). Every function is checked bit for bit
// against the original in the offline bench (night run 2). The sphere test of
// the eye against the track and the special cameras are queries to Reload.
// Nothing here runs unless Reload's side calls it.

#include <platform/native_extchasecam.h>

#include "native_extchasecam_k1.c" // per-frame update, kart world matrix, view matrix
#include "native_extchasecam_k2.c" // the camera flow per frame, timers, velocity mean, camera up, parameter block
#include "native_extchasecam_k3.c" // look direction and pivot frame, matrix inverse
#include "native_extchasecam_k4.c" // the eye: local, world with ground/wall correction, smoothing; look point
