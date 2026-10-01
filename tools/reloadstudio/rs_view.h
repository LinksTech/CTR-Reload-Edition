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
//   RsView_SetCrashBox(view, <Crash box>);           size outline about the model
//   RsView_SetWheels(view, 0 | 1);                   the game's kart wheels on the dummy
//   RsView_SetPose(view, 0..2);  RsView_SetYaw(view, degrees);
//
// Under the model the view always draws the reference dummy of make-char
// (tools/rldpack_dummy.inc): the retail kart at Crash's size with his seat,
// his steering wheel and the kart wheels the game draws. make-char fits every
// model onto it, so model and dummy share one origin; what the model leaves
// open, the dummy shows. While there is no model, the dummy is drawn alone
// with the window text above it; a message of RsView_Clear (or a preview that
// cannot be read) is shown instead of the picture.
//
// Dragging with the left mouse button turns the model; the parent then gets
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

// Notification code (HIWORD of WM_COMMAND's wParam): the user turned the model.
#define RS_VIEW_N_YAW 0x0101

// The yaw a new view starts with: a three-quarter view from the front.
#define RS_VIEW_DEFAULT_YAW 35

// Registers the window class. TRUE if it is registered (also when it already was).
BOOL RsView_Register(HINSTANCE instance);

// Reads a preview file (format "RLDPV1", written by rldpack make-char --preview
// and described there in tools/rldpack_char.inc).
// A missing or damaged file leaves an empty view with a message saying why.
// Pose and yaw stay as they are.
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
// dummy (where the game draws its wheel sprites), off = the dummy without
// them. A new view has them on. The framing does not change with them.
void RsView_SetWheels(HWND view, int on);

// The size of Crash with his kart ("@value crash-box"), in TENTHS of a game
// unit (the line carries one decimal). It is drawn as a dashed outline about
// the model, labelled "Crash size", hidden where the model is in front of it.
// Values are clamped to 16 bits of game units. A box with no extent on one
// axis (e.g. all zero) removes the outline.
void RsView_SetCrashBox(HWND view, int x0, int y0, int z0, int x1, int y1, int z1);

// Drops the model and shows message instead (NULL or "": the window text).
void RsView_Clear(HWND view, const wchar_t *message);

// TRUE while a model is loaded; the number of triangles of a pose (0 without a model).
BOOL RsView_Loaded(HWND view);
int  RsView_TriangleCount(HWND view, int pose);

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

#endif
