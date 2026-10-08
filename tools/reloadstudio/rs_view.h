// rs_view.h - the 3D preview of a character model (window class "RsModelView")
//
// The character page shows what rldpack make-char built, the way the game
// draws it: rldpack writes the converted model with `--preview <file>` and
// this control paints that file. It checks nothing itself - like the rest of
// Reload Studio it only shows what rldpack computed.
//
// Usage:
//   RsView_Register(instance);                       once, before the first window
//   view = CreateWindowW(L"RsModelView", L"<text while empty>",
//                        WS_CHILD | WS_VISIBLE, ..., page, (HMENU)id, instance, NULL);
//   RsView_LoadPreview(view, path);                  after every check
//   RsView_SetCrashBox(view, <Crash box>);           the size of the dummy's driver
//   RsView_SetWheels(view, 0 | 1);                   the game's kart wheels under the model
//   RsView_SetPose(view, 0..2);  RsView_SetYaw(view, degrees);
//
// Beside the model, on the same floor and in the same scale, the view always
// draws the reference dummy, labelled "Crash size": the kart of make-char
// (tools/rldpack_dummy.inc) - the retail kart at Crash's size with his seat,
// his steering wheel and the kart wheels the game draws - and on it a plain
// driver figure as tall and as wide as Crash with his kart. make-char fits
// every model onto that kart (--fit crash), so the two compare directly.
// While there is no model, the dummy is drawn alone with the window text
// above it; a message of RsView_Clear (or a preview that cannot be read) is
// shown instead of the picture.
//
// Dragging with the left mouse button turns model and dummy, each about its
// own origin (both by the same angle); the parent then gets
// WM_COMMAND with HIWORD(wParam) = RS_VIEW_N_YAW, LOWORD(wParam) = the control
// ID and lParam = the control. Same input gives the same pixels: the drawing
// uses only integers (see rs_view.c), so screenshots can be compared.

#ifndef RS_VIEW_H
#define RS_VIEW_H

#include "reloadstudio.h"

#define RS_VIEW_CLASS L"RsModelView"

// The poses of the preview file, in its order.
enum RsViewPose {
    RS_VIEW_POSE_NEUTRAL = 0,   // animation 0, frame 10: no steering
    RS_VIEW_POSE_FRAME0,        // animation 0, frame 0: full lock steering left
    RS_VIEW_POSE_FRAME20,       // animation 0, frame 20: full lock steering right
    RS_VIEW_POSE_COUNT
};

// The measured points of the reference dummy (tools/rldpack_dummy.inc) that
// the dummy's driver of the view is anchored at, in TENTHS of a game unit
// (degrees for the turn). They are copies: Rs_DummyCopiesCheck (rs_rldpack.c,
// which carries rldpack) compares them with the RLDDUM_* values; the first
// RsView_Register writes a line into the automation log when one differs.
#define RS_VIEW_DUMMY_SEAT_Y 102          // RLDDUM_SEAT_Y 10.2
#define RS_VIEW_DUMMY_WHEEL_X 0           // RLDDUM_WHEEL_CENTER (0, 38, 24)
#define RS_VIEW_DUMMY_WHEEL_Y 380
#define RS_VIEW_DUMMY_WHEEL_Z 240
#define RS_VIEW_DUMMY_WHEEL_RADIUS 105    // RLDDUM_WHEEL_RADIUS 10.5
#define RS_VIEW_DUMMY_TURN_WHEEL 30       // RLDDUM_TURN_WHEEL 30 (degrees)
#define RS_VIEW_DUMMY_CRASH_X0 (-338)     // RLDDUM_CRASH_BOX (-33.8, 5.6, -54.1, 34.3, 88.3, 58.3)
#define RS_VIEW_DUMMY_CRASH_Y0 56
#define RS_VIEW_DUMMY_CRASH_Z0 (-541)
#define RS_VIEW_DUMMY_CRASH_X1 343
#define RS_VIEW_DUMMY_CRASH_Y1 883
#define RS_VIEW_DUMMY_CRASH_Z1 583

// Notification code (HIWORD of WM_COMMAND's wParam): the user turned the model.
#define RS_VIEW_N_YAW 0x0101

// The yaw a new view starts with: a three-quarter view from the front.
#define RS_VIEW_DEFAULT_YAW 35

// Registers the window class. TRUE if it is registered (also when it already was).
BOOL RsView_Register(HINSTANCE instance);

// Reads a preview file (format "RLDPV2", written by rldpack make-char --preview
// and described there in tools/rldpack_char.inc: positions in 1/16 game units;
// the older "RLDPV1" with whole game units is read as well).
// A missing or damaged file leaves an empty view with a message saying why.
// Pose and yaw stay as they are.
// Preview feature: only with --enable-preview-features (g_rsPreviewFeatures)
// the file may go on with the native model ("RLDPN1", make-char
// --native-model on; THE NATIVE MODEL IN THE PREVIEW in
// tools/rldpack_native.inc), which the view then draws, textured, in place of
// the model (rs_view.c, THE NATIVE MODEL). Without the switch such a file is
// refused as before ("bytes follow after the last pose").
void RsView_LoadPreview(HWND view, const wchar_t *path);

// The same from memory (the bytes are copied). TRUE if the model is shown.
BOOL RsView_LoadPreviewData(HWND view, const void *data, size_t bytes);

// Which pose to draw: RS_VIEW_POSE_* (other values are clamped to 0..2).
void RsView_SetPose(HWND view, int pose);

// Turn about the vertical axis in degrees (any integer, taken modulo 360).
// 0 looks at the front of the kart; positive turns its nose to the right of
// the picture.
void RsView_SetYaw(HWND view, int degrees);
int  RsView_GetYaw(HWND view);   // 0..359

// The kart wheels the game draws for this driver: on = the four wheels of the
// dummy under the model (where the game draws its wheel sprites), off = the
// model alone. The dummy beside it always has them. A new view has them on.
// The framing does not change with them.
void RsView_SetWheels(HWND view, int on);

// The size of Crash with his kart ("@value crash-box"), in TENTHS of a game
// unit (the line carries one decimal). The dummy's driver reaches its top
// and its sides. Values are clamped to 16 bits of game units. A box with no
// extent on one axis (e.g. all zero) means "not known": the driver then takes
// the measured box of tools/rldpack_dummy.inc (RLDDUM_CRASH_BOX).
void RsView_SetCrashBox(HWND view, int x0, int y0, int z0, int x1, int y1, int z1);

// Drops the model and shows message instead (NULL or "": the window text).
void RsView_Clear(HWND view, const wchar_t *message);

// TRUE while a model is loaded; the number of triangles of a pose (0 without a model).
BOOL RsView_Loaded(HWND view);
int  RsView_TriangleCount(HWND view, int pose);

// Preview features (only called with --enable-preview-features). Without
// them nothing below is loaded and the view draws exactly as described above.
//
// A set of extra poses of the same model (rldpack char-poses --preview,
// "RLDPS1\0\0", u32 count, per pose u32 triangles + the triangle records of
// RLDPV1). Shown instead of the built model while index >= 0. Loading takes
// the set into the framing; switching between poses never zooms.
BOOL RsView_LoadPoseSet(HWND view, const wchar_t *path);
int  RsView_PoseSetCount(HWND view);
void RsView_ShowPoseSet(HWND view, int index);     // -1 = the built model again (RsView_SetPose applies)
void RsView_DropPoseSet(HWND view);

// A wheel model (rldpack char-wheel --preview, "RLDPW1\0\0", u32 triangles +
// the records of RLDPV1, centred on its axle, sized like the game's wheel).
// Drawn at the dummy's four wheel points instead of the game's wheels, only
// while RsView_SetWheels is on; the dummy beside the model keeps the game's
// wheels.
BOOL RsView_LoadWheelModel(HWND view, const wchar_t *path);
void RsView_DropWheelModel(HWND view);
void RsView_SetWheelScale(HWND view, int percent);  // 50..200, integer scaling
void RsView_SetWheelTurn(HWND view, int spinDegrees, int steerDegrees);  // spin about the axle, steer the front pair
void RsView_SetWheelAnimation(HWND view, int on);   // own WM_TIMER on the view; never on by itself
//
// The wheel file (1/16 game units): axle along X through the origin, the rim
// (outer side) toward +X - as on the driver's left. The -X pair is drawn
// mirrored. Spin: + rolls forward (the top toward +Z), any integer taken
// modulo 360. Steer: + steers left (the front edge toward +X), clamped to
// +-RS_VIEW_WHEEL_STEER_MAX, the front pair only. The animation turns both by
// fixed steps per tick of the view's own timer (not by the clock); it stops
// while the view is hidden or minimized or has no wheel model. Without a
// wheel model the angles change nothing.
// Loading and the scale may change the framing; pose, spin and steering never.
#define RS_VIEW_WHEEL_STEER_MAX 22   // the game's full lock, 22.5 degrees (game/DrawTires.c, wheelRotation)

// The reference dummy (RldDum_Mesh of tools/rldpack_dummy.inc), defined in
// rs_rldpack.c - the translation unit that carries rldpack. Positions in 1/16
// game units (rounded), 3 per position; triangles as 3 position indices,
// wound outward, each with a colour 0xRRGGBB. wheels: with the four kart
// wheels the game draws. pose: RS_VIEW_POSE_*; the steering wheel is turned as
// in that frame (RldDum_PoseFrame, RldDum_ApplyPose), the rest stays.
// Returns the triangle count and *positionCount; 0 (and *positionCount 0)
// when a buffer is too small. position or triangle NULL: only the counts.
int Rs_DummyMesh(int wheels, int pose, int *position, int positionMax, int *triangle, unsigned int *color,
                 int triangleMax, int *positionCount);

// The four wheel points of the dummy (RLDDUM_TIRE_* of tools/rldpack_dummy.inc),
// defined in rs_rldpack.c: centres in 1/16 game units (rounded), order front
// left, front right, rear left, rear right; *halfSize the half size of the
// game's wheel in 1/16 game units.
void Rs_DummyTires(int center[4][3], int *halfSize);

// rs_rldpack.c: 1 if every RS_VIEW_DUMMY_* above equals its RLDDUM_* value
// (rounded to tenths); else 0 and why names the first that differs.
int Rs_DummyCopiesCheck(wchar_t *why, int whyCap);

#endif
