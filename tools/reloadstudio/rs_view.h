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
//   RsView_SetReference(view, <retail kart box>);    fixed retail size, see below
//   RsView_SetPose(view, 0..2);  RsView_SetYaw(view, degrees);
//
// The window text is shown while there is no model (as in the message list),
// unless RsView_Clear passed its own message.
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

// The retail kart for size comparison, as a box in the game units of the model
// (a retail kart is 112 x 56 units with its bottom at 6, tools/rldpack_char.inc).
// Not the model's own "@value kart-box": the model already shows its own kart.
// It is drawn as a grey, simplified kart next to the model, labelled "Retail
// kart"; its outline is exactly this box. Values are clamped to 16 bits. A box
// with no extent on one axis (e.g. all zero) removes the reference.
void RsView_SetReference(HWND view, int x0, int y0, int z0, int x1, int y1, int z1);

// Drops the model and shows message instead (NULL or "": the window text).
void RsView_Clear(HWND view, const wchar_t *message);

// TRUE while a model is loaded; the number of triangles of a pose (0 without a model).
BOOL RsView_Loaded(HWND view);
int  RsView_TriangleCount(HWND view, int pose);

#endif
