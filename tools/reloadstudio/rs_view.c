// rs_view.c - the 3D preview of a character model (window class "RsModelView")
//
// See rs_view.h for the interface. The control paints the preview file that
// rldpack make-char writes with --preview: the model after the conversion,
// read back by the renderer's rule, so the author sees what the game will
// draw - not the PLY he put in.
//
// WHY A CPU RASTERIZER
//
// Reload Studio is plain Win32 + GDI and has no 3D device. A few hundred
// triangles in a window of a few hundred pixels are drawn on the CPU in far
// less than a frame: a 32-bit DIB section, a depth buffer, Gouraud shading
// from the corner colours, one BitBlt. No driver, no shader compiler, and the
// same pixels on every machine.
//
// DETERMINISM
//
// Automation compares screenshots, so the same input has to give the same
// pixels. Everything from the file to the pixel is integer arithmetic:
// sine and cosine come from a table of whole degrees (Q14), positions are
// carried in 1/16 game units and 1/16 pixels, divisions are C divisions
// (truncating, defined by the standard), the square root is an integer one.
// Ties in the depth test keep the triangle drawn first, and the drawing order
// is fixed (floor, model in file order, the game's wheels under the model,
// dummy kart, dummy driver). The dummy kart comes from rldpack's own numbers
// in doubles (Rs_DummyMesh, rs_rldpack.c), computed without library functions
// and rounded to 1/16 units there; the dummy driver is built here from
// integers. Only the text comes from GDI, which is the same for the same
// machine and font settings. Several threads fill the picture, each a band
// of rows at a time (THE DRAW LIST); every pixel still sees the triangles
// in the one drawing order, so the threads change no pixel.
//
// THE AXES
//
// The preview carries model coordinates in 1/16 game units ("RLDPV2"; the
// older "RLDPV1" in whole game units is still read and taken x16), as rldpack
// builds them (the file format is described at "--preview" in
// tools/rldpack_char.inc:
// "+Y up, +Z forward, +X the driver's left"; RldMk_Axes maps the PLY axes by a
// proper rotation; the records are "X, Z (forward), Y (up)"): the ground is
// y = 0; with --fit crash the kart's bottom stands at y = 5.6 like the
// dummy's (RLDDUM_KART_BOTTOM), and the poses turn the driver about the seat
// of the reference kart (RLDDUM_SEAT, tools/rldpack_dummy.inc). The system is
// right-handed. The view keeps that: y is drawn upwards, so the
// driver stands upright, and the projection below contains no mirror - a
// triangle seen from the side of its right-hand normal runs counter-clockwise
// on screen.
//
// THE COLOURS
//
// make-char builds untextured triangles (G3, b->drawBytes in rldpack_char.inc
// counts RLDCHAR_DRAW_BYTES_G3 per triangle). For those
// the game passes the palette colour unchanged: RenderBucket_LoadPrimColors
// moves the three corner colours into the GTE colour FIFO and only applies a
// depth cue (DPCT) when the instance has an alphaScale (game/RenderBucket/
// RenderBucket_QueueExecute.c:3022-3034), and the G3 packet takes them as they
// are (:3067-3073). No lighting, no 2x factor (that only exists for textured
// primitives, where 128 means "texture as it is"). So the factor here is 1:1:
// corner byte 255 is pixel 255. The PS1 GPU then dithers to 15 bits; the view
// does not imitate that. The view shows the brightest case: in the race the
// ground under the kart sets alphaScale (game/COLL.c): 0.25 + luma/128 of the
// colour below luma 96, at most 75 % darker - the page says so below the
// preview.
//
// THE BACK FACES
//
// The game draws a triangle only from the side its cull bits let through
// (game/RenderBucket/RenderBucket_QueueExecute.c:2884-2903). The preview keeps
// that: the corners of each triangle run counter-clockwise seen from the side
// the game draws (right-hand normal towards that viewer), and bit 0 of the pad
// byte marks a triangle without cull bit, drawn from both sides. rldpack writes
// the same pad on all three corners, so corner 0 decides. The view draws a
// culled triangle only when its corners run counter-clockwise on screen.
//
// THE PICTURE
//
// Camera slightly from above (RS_VIEW_PITCH at the start), perspective;
// the user tilts, zooms and pans it (THE CAMERA in rs_view.h, RsView_Scene).
// Two things stand
// side by side on one floor at y = 0, in the same scale: on the left the
// model, on the right the dummy - Crash's kart with a driver of Crash's size.
// Each turns about its own origin (the point the game turns a kart about),
// both by the same yaw, and the two stay side by side across the picture:
// so both are always seen from the same side and at the same distance, and
// their sizes compare like for like. Turning them about a common centre would
// move one behind the other - the far one would look smaller and could be
// hidden. Their distance is the one their widest turn needs plus
// RS_VIEW_GAP, so that they never touch at any yaw.
// The framing depends on all three poses, the model with the game's wheels,
// the dummy in all three poses and the floor, which stays inside the
// picture - not on the yaw, the pose or the wheels: turning, changing the
// pose or switching the wheels never zooms. While there is no model the
// dummy stands alone in the middle. Without the reference (RsView_SetCrashRef
// 0) the model stands alone. Background and floor have the view's own
// colours (dark or light, RsView_SetBackground), not the colour scheme's.
//
// THE DUMMY
//
// The reference dummy of make-char (tools/rldpack_dummy.inc, RldDum_Mesh):
// the retail kart at Crash's size with his seat, his steering wheel (turned
// as in the pose) and the four wheels the game draws - Crash always has them.
// make-char fits every model onto this kart (--fit crash), so a kart model
// beside it has the same length. On it sits a driver (RsView_DriverMesh): a
// plain figure of boxes - hips on the seat, legs forward, hands on the
// steering wheel - that reaches the top and the sides of Crash's box
// (RsView_SetCrashBox, "@value crash-box" of rldpack). Only that box, the
// seat and the steering wheel are measured; the figure's proportions are
// not, it shows his size and nothing else. Its hands follow the steering
// wheel in the turned poses; its body stays upright. The dummy is grey, one
// colour per triangle, shaded by a fixed light per face, so its shape stays
// readable; "Crash size" stands above it.
//
// THE GAME'S WHEELS UNDER THE MODEL
//
// The game draws its four wheels for every driver whose wheels are on - it
// places them by the kart, not by the model. With RsView_SetWheels on, the
// view draws them under the model at the dummy's wheel points (the part of
// the dummy mesh that its wheels add, RsView_FindTires). They NEVER cover
// the model: the model is drawn first and marks its pixels
// (RsViewTarget.mask), the wheels are drawn only into pixels the model left
// free. So every pixel of the model is exactly as without them (also a
// model's own wheels inside them).
//
// PREVIEW FEATURES
//
// The page loads more into the view: with --enable-preview-features a pose
// set (RLDPS1, drawn instead of the model's pose while one is chosen), and
// always the wheel model of the card Wheels (RLDPW3, textured, drawn at the
// four wheel points of Rs_DummyTires instead of the game's wheels - unlike
// them by depth against the model, as the game draws an author's wheels in
// the native model's item; the right pair mirrored, the centres raised by the
// radius a larger wheel grew, moved by the axle offsets of WHLS v3; a rear
// wheel model at the rear pair). The dummy beside it keeps the game's wheels.
// The wheel turns by whole degrees from the same sine table; its animation
// runs on the view's own timer in fixed steps per tick. Nothing loaded:
// nothing of it is used, and the picture is the same as without them.
//
// THE NATIVE MODEL (preview feature, renderer step 5b; open to everyone)
//
// With make-char --native-model on, rldpack appends the native model (CNET
// with its CTXT textures in full size and its materials: "RLDPN3", or the
// older "RLDPN2", described at THE NATIVE MODEL IN THE PREVIEW in
// tools/rldpack_native.inc) to the classic data of --preview; rs_tex.c reads
// it and builds the game's mip levels (include/rldmip.inc). The view accepts
// that block with and without --enable-preview-features; it then draws the
// native model in place of the game's own model (in the same frame; THE
// ANIMATIONS below), with
// "Native model" above it - unless a material blends: the game then draws
// the classic model, and so does the view. Per pixel the view does what the
// game's "nr" program does (rs_tex.h): u, v and the corner colour
// perspective correct, all in integers (the weights of the corners in Q16,
// each value times the corner depth, divided by the interpolated depth);
// the texel of the triangle's mip level, nearest, wrapped per CTXT flags;
// texel x corner colour x material colour (RsTex_Shade); a mask material
// leaves out what lies below one half of alpha. Back faces are culled (CNET
// winds counter-clockwise from outside). No light, like the model of the
// game. Without the block the view parses, frames and draws exactly as
// before. An author's wheel (RLDPW3 of char-wheel) is read and drawn the
// same way.
//
// THE ANIMATIONS (rs_view.h)
//
// The classic model carries its 47 frames in "RLDPC1" behind RLDPV2
// (RsView_ParseFrames turns them into 47 poses of RLDPV2 records), the
// native model its 47 poses and the end poses in RLDPN3 (rs_tex.c). The frame
// set (anim, frame) picks the pose of each (RsView_FilePose; an older file
// shows the nearest turn frame). The end poses are the game's: neutral +
// weight x (end pose - neutral) on the native model only, in integers
// (RsView_NativePos, built once per end pose and weight). The dummy's
// steering wheel has three poses: the nearest turn frame. The framing holds
// every pose and both end poses, so neither a frame nor the playing ever
// zooms. The playing runs on the view's own timer, a fixed step per tick,
// like the wheel animation: the same ticks give the same pictures.

#include "reloadstudio.h"
#include "rs_view.h"
#include "rs_tex.h"
#include <string.h>
#include <wchar.h>

#define RS_VIEW_MAGIC_BYTES 8
#define RS_VIEW_CORNER_BYTES 10                         // s16 x, y, z; u8 r, g, b, pad
#define RS_VIEW_TRI_BYTES (3 * RS_VIEW_CORNER_BYTES)
#define RS_VIEW_FILE_MAX (64u * 1024u * 1024u)          // far above any model the packer accepts
#define RS_VIEW_PITCH 20                                // degrees the camera looks down
#define RS_VIEW_SUB 16                                  // positions in 1/16 units and pixels
#define RS_VIEW_DEPTH_SHIFT 20                          // depth values: camera distance / depth in Q20
#define RS_VIEW_COORD_MAX (1LL << 18)                   // 1/16 pixels; see RsView_Triangle
#define RS_VIEW_DUMMY_POS_MAX 2048                      // far above the dummy (RLDDUM_MESH_TRIANGLES_MAX 600)
#define RS_VIEW_DUMMY_TRI_MAX 2048
#define RS_VIEW_MESSAGE_CAP 512
// Game units between model and dummy: half the dummy kart's width (56.2).
#define RS_VIEW_GAP 28
#define RS_VIEW_DRIVER_PARTS 16                         // boxes of the dummy driver (RsView_DriverMesh)
#define RS_VIEW_DRIVER_BODY 0xA0A0A0u
#define RS_VIEW_DRIVER_HEAD 0xB4B4B4u
#define RS_VIEW_SET_POSES_MAX 16                        // poses of a pose set (RLDPS1)
#define RS_VIEW_TIMER_WHEEL 1                           // the view's own timer: the wheel animation
#define RS_VIEW_TIMER_ANIM 2                            // and the playing of the animations
#define RS_VIEW_WHEEL_TICK_MS 33
#define RS_VIEW_WHEEL_SPIN_STEP 12                      // degrees of spin per tick
#define RS_VIEW_WHEEL_STEER_STEP 2                      // degrees of steering per tick
#define RS_VIEW_PITCH_MIN (-10)                         // THE CAMERA (rs_view.h)
#define RS_VIEW_PITCH_MAX 89
#define RS_VIEW_ZOOM_MIN 50
#define RS_VIEW_ZOOM_MAX 800
#define RS_VIEW_BAR_BUTTONS 8                           // the bar: six fixed views, Reset, View
#define RS_VIEW_BAR_VIEW 7                              // the button "View" (the menu)
#define RS_VIEW_BAR_RESET 6
#define RS_VIEW_BENCH_MAX 100000                        // pictures of one RsView_Bench
// The race camera (RS_VIEW_PRESET_RACE): the game's near camera of one player
// behind the kart at full speed (game/CAM.c, CAM_FollowDriver_Normal with
// NearCam4x3 of game/zGlobal_DATA.c, distance 224 at full speed, kart on flat
// ground): the eye 211.6 world units behind the kart's origin and 98.2 above
// it, the point it looks at 41.4 ahead and 107.1 above; world units x 1.25031
// = model units (the race scale 0xccc of game/Vehicle/VehBirth.c). Here in
// 1/16 units of the model, the kart's nose toward +z: the eye and the point
// it looks at. The camera looks 2 degrees up. Its focal length is two thirds of
// the picture's height (game/native_view.c: distance to screen 0x100 on a
// 512 x 216 canvas whose rows count 9/16), at most half the width, so that a
// narrow picture still shows 90 degrees across.
#define RS_VIEW_RACE_EYE_Y 1965
#define RS_VIEW_RACE_EYE_Z (-4233)
#define RS_VIEW_RACE_AT_Y 2143
#define RS_VIEW_RACE_AT_Z 828
#define RS_VIEW_RACE_PITCH (-2)
// The exhaust of the game (game/Vehicle/VehTurbo.c): the turbo flames of the
// retail kart, 1/16 units (x mirrored). The page gives the smoke points.
#define RS_VIEW_FLAME_X 288
#define RS_VIEW_FLAME_Y 768
#define RS_VIEW_FLAME_Z (-832)
// The shadow (game/Vehicle/VehGroundShadow.c draws it subtractive, 31/128 of
// its texture): at most this much darker in its middle, fading to nothing
// over the outer part of the quad (Q16 of half the quad). The exact shape of
// the game's shadow texture is game data and not copied.
#define RS_VIEW_SHADOW_DARK 60
#define RS_VIEW_SHADOW_SOFT 13107                       // 0.4 of half the quad

// Crash with his kart in tenths of game units while the caller has given no
// box (RS_VIEW_DUMMY_CRASH_*, rs_view.h).
static const int s_rsViewCrashDefault[6] = {
    RS_VIEW_DUMMY_CRASH_X0, RS_VIEW_DUMMY_CRASH_Y0, RS_VIEW_DUMMY_CRASH_Z0,
    RS_VIEW_DUMMY_CRASH_X1, RS_VIEW_DUMMY_CRASH_Y1, RS_VIEW_DUMMY_CRASH_Z1,
};

static const unsigned char s_rsViewMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'V', '1', 0, 0 };
static const unsigned char s_rsViewMagic2[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'V', '2', 0, 0 };
static const unsigned char s_rsViewFramesMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'C', '1', 0, 0 };
static const unsigned char s_rsViewSetMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'S', '1', 0, 0 };

// sin(0..90 degrees) * 16384, rounded half up. A table instead of sin(), so that
// no runtime library can round differently.
static const short s_rsViewSin[91] = {
        0,   286,   572,   857,  1143,  1428,  1713,  1997,  2280,  2563,
     2845,  3126,  3406,  3686,  3964,  4240,  4516,  4790,  5063,  5334,
     5604,  5872,  6138,  6402,  6664,  6924,  7182,  7438,  7692,  7943,
     8192,  8438,  8682,  8923,  9162,  9397,  9630,  9860, 10087, 10311,
    10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365,
    12551, 12733, 12911, 13085, 13255, 13421, 13583, 13741, 13894, 14044,
    14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191, 15296,
    15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083,
    16135, 16182, 16225, 16262, 16294, 16322, 16344, 16362, 16374, 16382,
    16384,
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct RsViewPoseData {
    const unsigned char *tris;   // into RsView.data, RS_VIEW_TRI_BYTES each
    int count;
    int sub;                     // 1/16 units per file unit: RS_VIEW_SUB (whole units) or 1 (RLDPV2)
};

// A solid of the dummy, built once (RsView_SolidsBuild): positions in 1/16
// units, triangles as position indices, the shaded colour of each triangle.
struct RsViewSolid {
    int *pos;
    int *tri;
    unsigned int *rgb;
    int positions, triangles;
};
// RsView.solids: the dummy kart with its wheels, without them, its driver;
// RS_VIEW_POSE_COUNT of each.
enum { RS_VIEW_SOLID_KART = 0, RS_VIEW_SOLID_BARE = RS_VIEW_POSE_COUNT, RS_VIEW_SOLID_DRIVER = 2 * RS_VIEW_POSE_COUNT,
       RS_VIEW_SOLIDS = 3 * RS_VIEW_POSE_COUNT };

struct RsView {
    // The model. data owns the bytes of the file; loaded = 0: none.
    unsigned char *data;
    struct RsViewPoseData poses[RS_VIEW_FRAMES];   // poseCount of them, in the file's order (RsView_FilePose)
    int poseCount;             // RS_VIEW_FRAMES (RLDPC1), or RS_VIEW_POSE_COUNT (RLDPV2/RLDPV1 alone)
    unsigned char *frames;     // RLDPC1: its 47 frames as RLDPV2 records (RsView_ParseFrames), owned
    struct RsTexNative native;    // RLDPN3/RLDPN2 (rs_tex.c): drawn in place of the model while triangles > 0, no blend
    int loaded;
    long long radius2;         // largest x*x + z*z over all poses (whole game units, rounded up)
    int ymin, ymax;            // over all poses (whole game units, rounded outwards)
    wchar_t message[RS_VIEW_MESSAGE_CAP];

    // Room for the dummy (Rs_DummyMesh with its wheels) while extents are
    // measured; the pictures take it from solids (RsView_SolidsBuild):
    // positions in 1/16 units, triangles, a colour per triangle. tireFirst..
    // tireEnd: its positions that the wheels add (the game's wheels under the
    // model); both 0 when they cannot be told apart.
    int *dumPos;
    int *dumTri;
    unsigned int *dumColor;
    int tireFirst, tireEnd;
    int wheels;                // the game's kart wheels under the model

    // Extents for the framing, over all poses. Across the picture: per whole
    // degree of yaw the lowest and highest x after the turn, in 1/16 units
    // times 16384 (RsView_SpanAdd) - the model with the game's wheels, and the
    // dummy with its driver. Height in whole game units, outwards.
    long long *modelLo, *modelHi;   // 360 each
    long long *dumLo, *dumHi;       // 360 each
    long long dumRadius2;           // largest x*x + z*z of the dummy, whole units
    long long tireRadius2;          // the same of the game's wheels
    int dumYmin, dumYmax;
    int tireYmin, tireYmax;

    // Crash with his kart (tenths of game units): the size of the dummy's driver.
    int hasCrash;
    int crash[6];              // x0 y0 z0 x1 y1 z1, lo <= hi

    int yaw;                   // 0..359

    // THE CAMERA (rs_view.h): pitch in degrees down, zoom in percent of the
    // framing's focal length, pan in pixels; preset RS_VIEW_PRESET_* while
    // the camera is one of them, -1 after a change; race: the camera of
    // RS_VIEW_PRESET_RACE (own distance and focal length) until a reset or
    // another fixed view. modelKey: the model of RsView_SetModelKey.
    int pitch;
    int zoom;
    int panX, panY;
    int preset;
    int race;
    wchar_t modelKey[RS_VIEW_MESSAGE_CAP];
    // The display toggles (RsView_SetBackground and the rest).
    int light;
    int crashRef;
    int showShadow, showExhaust;
    int showNative;
    int pitchMin;              // the lowest pitch that keeps the eye above the ground (last RsView_Scene)
    int wheelDelta;            // mouse wheel units not yet used for a zoom step

    // The bar of the view (RsView_Bar*): the buttons laid out by the last
    // paint, the one under the mouse and the one pressed (-1 none).
    RECT barRect[RS_VIEW_BAR_BUTTONS];
    int barShown[RS_VIEW_BAR_BUTTONS];
    int barHot, barDown;
    int tracking;              // TrackMouseEvent asked for WM_MOUSELEAVE

    // Built once, not per picture (the same numbers every time): the dummy
    // per pose with and without its wheels, its driver per pose (again on a
    // new Crash box), each with the colour of every triangle already shaded.
    struct RsViewSolid *solids;
    int solidsValid;
    // The box of the native model per pose of the file (x0 y0 z0 x1 y1 z1,
    // 1/16 units), and of the end pose blended (RsView_NativeBlend).
    long long nativeBox[RS_TEX_POSES_MAX][6];
    // The end pose blended (RsView_NativeBlend): the positions of the native
    // model as the file has them per pose (s16 x, y, z per corner), for
    // blendEnd at blendWeight; blendEnd 0 = not built.
    unsigned char *blendPos;
    int blendEnd, blendWeight;
    long long blendBox[6];
    // THE DRAW LIST of the last picture and the threads that fill it.
    struct RsViewTri *tris;
    int *triRows;
    int triCap;
    struct RsViewPool *pool;

    // A pose set (RsView_LoadPoseSet): setData owns the bytes, setCount = 0:
    // none. setIndex >= 0 draws that pose instead of the model's. Extents
    // like radius2, ymin, ymax, over the whole set.
    unsigned char *setData;
    struct RsViewPoseData setPoses[RS_VIEW_SET_POSES_MAX];
    int setCount;
    int setIndex;
    long long setRadius2;
    int setYmin, setYmax;

    // A wheel model (RsView_LoadWheelModel): wheelData owns the bytes of
    // the file, wheel reads them (RsTex_ParseWheel); wheelCount = 0: none -
    // the game's wheels are drawn as before.
    // wheelR2: largest y*y + z*z, wheelHalfW: largest |x|, both of the file
    // (1/16 units, wheel-local). tire*: the dummy's wheel points (Rs_DummyTires).
    unsigned char *wheelData;
    struct RsTexWheel wheel;            // RLDPW3 (rs_tex.c), pointing into wheelData
    int wheelCount;                     // wheel.triangles, 0 = none
    long long wheelTicks;               // QueryPerformanceCounter ticks of the last RsView_DrawWheels
    int benching;                       // RsView_WheelBench renders: no timing line per picture
    long long wheelR2;
    int wheelHalfW;
    int wheelScale;            // percent, 50..200
    int wheelSpin;             // 0..359
    int wheelSteer;            // -RS_VIEW_WHEEL_STEER_MAX..RS_VIEW_WHEEL_STEER_MAX
    int wheelSteerStep;        // +-RS_VIEW_WHEEL_STEER_STEP while animated
    int wheelAnim;             // the animation is on (RsView_SetWheelAnimation)
    int wheelTimer;            // the timer runs (stopped while the view is hidden)
    long long wheelRadius2;    // the framing of the wheel model, like tireRadius2
    int wheelYmin, wheelYmax;
    int tireAt[4][3];          // FL FR RL RR, 1/16 units
    int tireHalf;              // the game's wheel: half its size (its radius), 1/16 units
    // THE REAR WHEEL AND THE AXLES (rs_view.h): the rear wheel model like
    // the wheel model (rearData owns the bytes, rearCount = 0: none), the
    // offsets per axle {dz, dy, dtrack} in 1/16 units, the flag "always draw".
    unsigned char *rearData;
    struct RsTexWheel rear;
    int rearCount;
    long long rearR2;
    int rearHalfW;
    int axle[2][3];
    int rearSize;              // the rear wheel model's size, RS_VIEW_REAR_SIZE_SAME = as the wheel model
    int wheelsAlways;

    // THE ANIMATIONS (rs_view.h): the frame set (anim, frame), the end pose
    // and its weight, the playing (RS_VIEW_PLAY_*, -1 none) with its step.
    int anim, frame;
    int endPose, endWeight;
    int playing;
    int playStep;
    int animTimer;             // the timer runs (stopped while the view is hidden)

    int dragging;              // the left button turns and tilts
    int dragX, dragY;
    int dragYaw, dragPitch;
    int downX, downY;          // where the left button went down (a click picks, RsView_PickBegin)
    int panning;               // the right (1) or middle (2) button moves the picture
    int panDownX, panDownY;    // where it went down
    int panFromX, panFromY;    // the pan then
    int panMoved;              // it moved 4 pixels or more: no click

    // The look (RsView_SetLook; the tab In-game look of the page): the
    // shadow quad on the floor under the model, the exhaust points over it.
    int lookShadow;            // 0 none, 1 drawn
    int lookQuad[4];           // x0 x1 z0 z1, 1/16 units
    int lookCount;             // exhaust points drawn, 0..2
    int lookPoint[2][3];       // 1/16 units
    int lookGrey;              // the points are the retail ones: grey
    int pick;                  // 1 or 2 while that point is picked by a click; 0 = none
    DWORD pickClickTime;       // message time of the click that ended a pick: its double click resets nothing
    int pickDone;              // the point the last pick was for
    int pickHit;               // it hit the model
    int pickAt[3];             // where, 1/16 units

    // The picture: a top-down 32-bit DIB selected into mem, and its depth buffer.
    HBITMAP dib;
    HDC mem;
    HGDIOBJ oldBmp;
    unsigned int *pixels;
    int *depth;
    unsigned char *mask;                // 1 = a pixel of the model (RsViewTarget.mask)
    int w, h;
    int dirty;                          // the picture no longer matches the state
    const struct RsPalette *drawnPal;   // palette the picture was drawn with
    int drawnScale;                     // Rs_Px(96) it was drawn with (margins, text)
};

// One projected corner: position in 1/16 pixels, depth (larger = nearer), colour.
struct RsViewVert {
    long long x, y;
    int z;
    int r, g, b;
    int a;                      // corner alpha (the native model and the wheels: C of rs_tex.h)
    long long u, v;             // texture coordinates, Q16 (only the native model's)
};

// Everything the projection needs; filled by RsView_Scene.
struct RsViewCam {
    int yawSin, yawCos;         // Q14
    int pitchSin, pitchCos;     // Q14
    long long ycq;              // height the camera looks at, 1/16 units
    long long dq;               // camera distance, 1/16 units
    long long dqNear;           // dq / 4 + 1: nearer corners are not drawn (RsView_Project)
    long long fq;               // focal length in 1/16 pixels
    long long cxq, cyq;         // screen centre in 1/16 pixels
};

struct RsViewScene {
    struct RsViewCam cam;
    long long offModel;         // sideways offsets after the turn, 1/16 units
    long long offDummy;
    long long floorX, floorZ;   // half width and half depth of the floor, 1/16 units
    long long floorY;           // its height, 1/16 units
    int pitch;                  // the pitch drawn (RsView.pitch, not below pitchMin)
    int pitchMin;               // the lowest pitch with the eye above the ground
    int panX, panY;             // the pan drawn (RsView.panX/Y, limited), pixels
    long long baseCx, baseCy;   // the screen centre without the pan, 1/16 pixels
    int dummy;                  // the dummy is drawn
};

// mask / maskMode: RS_VIEW_MASK_* - the model marks its pixels, the game's
// wheels under it leave them alone.
enum { RS_VIEW_MASK_NONE = 0, RS_VIEW_MASK_SET, RS_VIEW_MASK_SKIP };

struct RsViewTarget {
    unsigned int *pixels;
    int *depth;
    unsigned char *mask;
    int maskMode;
    int w, h;
    int y0, y1;                 // the band of rows filled: y0 <= y < y1 (THE DRAW LIST)
};

static struct RsView *RsView_Data(HWND view)
{
    return (struct RsView *)GetWindowLongPtrW(view, GWLP_USERDATA);
}

// ---------------------------------------------------------------------------
// Integer helpers
// ---------------------------------------------------------------------------

static int RsView_NormDeg(int deg)
{
    deg %= 360;
    return deg < 0 ? deg + 360 : deg;
}

static int RsView_Sin(int deg)
{
    deg = RsView_NormDeg(deg);
    if (deg <= 90)
        return s_rsViewSin[deg];
    if (deg <= 180)
        return s_rsViewSin[180 - deg];
    if (deg <= 270)
        return -s_rsViewSin[deg - 180];
    return -s_rsViewSin[360 - deg];
}

static int RsView_Cos(int deg)
{
    return RsView_Sin(deg + 90);
}

// floor(sqrt(n)), bit by bit.
static unsigned long long RsView_Isqrt(unsigned long long n)
{
    unsigned long long r = 0, bit = 1ULL << 62;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

// floor(v / 16) also for negative v (C division truncates towards zero);
// by shifts of non-negative values (no 64-bit division call).
static long long RsView_FloorDiv16(long long v)
{
    return v >= 0 ? v >> 4 : -((-v + 15) >> 4);
}

// Tenths of a game unit -> whole units, rounded down / up (also for negative t).
static int RsView_TenthsFloor(int t)
{
    return t >= 0 ? t / 10 : -((-t + 9) / 10);
}

static int RsView_TenthsCeil(int t)
{
    return -RsView_TenthsFloor(-t);
}

// Tenths of a game unit -> 1/16 units (truncating, as every division here).
static long long RsView_TenthsSub(int t)
{
    return (long long)t * RS_VIEW_SUB / 10;
}

// n / d rounded half away from zero, d > 0.
static long long RsView_DivRound(long long n, long long d)
{
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

static long long RsView_Min3(long long a, long long b, long long c)
{
    long long m = a < b ? a : b;
    return m < c ? m : c;
}

static long long RsView_Max3(long long a, long long b, long long c)
{
    long long m = a > b ? a : b;
    return m > c ? m : c;
}

static unsigned int RsView_ReadU32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int RsView_ReadS16(const unsigned char *p)
{
    int v = (int)p[0] | ((int)p[1] << 8);
    return v >= 0x8000 ? v - 0x10000 : v;
}

// COLORREF (0x00BBGGRR) -> DIB pixel (0x00RRGGBB).
static unsigned int RsView_Pixel(COLORREF c)
{
    return ((unsigned int)(c & 0xFF) << 16) | (unsigned int)(c & 0xFF00) | ((unsigned int)(c >> 16) & 0xFF);
}

// ---------------------------------------------------------------------------
// Extents across the picture, per yaw
// ---------------------------------------------------------------------------
//
// After the turn by yaw a point (x, z) lies at x * cos + z * sin across the
// picture (RsView_Project). A span keeps, for each of the 360 whole degrees
// the view can be turned to, the lowest and the highest of that over a set
// of points - exact for every yaw, so the layout never depends on the yaw.

struct RsViewTrig {
    int sin[360], cos[360];   // Q14
};

static void RsView_TrigFill(struct RsViewTrig *tr)
{
    int d;
    for (d = 0; d < 360; d++) {
        tr->sin[d] = RsView_Sin(d);
        tr->cos[d] = RsView_Cos(d);
    }
}

static void RsView_SpanReset(long long *lo, long long *hi)
{
    int d;
    for (d = 0; d < 360; d++) {
        lo[d] = 0x7FFFFFFFFFFFFFFFLL;
        hi[d] = -0x7FFFFFFFFFFFFFFFLL;
    }
}

// x, z in 1/16 units (below 2^21): the values stay below 2^36.
static void RsView_SpanAdd(const struct RsViewTrig *tr, long long *lo, long long *hi, long long x, long long z)
{
    int d;
    for (d = 0; d < 360; d++) {
        const long long at = x * tr->cos[d] + z * tr->sin[d];
        if (at < lo[d])
            lo[d] = at;
        if (at > hi[d])
            hi[d] = at;
    }
}

// A span value (1/16 units x 16384) -> whole game units, rounded outwards.
static long long RsView_SpanUnitsFloor(long long v)
{
    const long long q = (long long)RS_VIEW_SUB * 16384;
    return v >= 0 ? v / q : -((-v + q - 1) / q);
}

static long long RsView_SpanUnitsCeil(long long v)
{
    return -RsView_SpanUnitsFloor(-v);
}

// ---------------------------------------------------------------------------
// The preview file
// ---------------------------------------------------------------------------

static void RsView_DropModel(struct RsView *v)
{
    RsTex_FreeNative(&v->native);   // its levels; level 0 lies in data
    Rs_Free(v->data);
    v->data = NULL;
    Rs_Free(v->frames);
    v->frames = NULL;
    memset(v->poses, 0, sizeof(v->poses));
    v->poseCount = 0;
    memset(&v->native, 0, sizeof(v->native));
    Rs_Free(v->blendPos);
    v->blendPos = NULL;
    v->blendEnd = 0;
    v->loaded = 0;
    v->radius2 = 0;
    v->ymin = 0;
    v->ymax = 0;
}

// One corner of a pose into the extents of the framing: radius2 the largest
// x*x + z*z in whole game units (rounded up), ymin/ymax in whole game units
// (rounded outwards). sub: 1/16 units per file unit (RsViewPoseData.sub); for
// whole units (sub = RS_VIEW_SUB) the numbers are the file's own.
// The box of positions in the native layout (s16 x, y, z per corner,
// RsTexNative.pos), 1/16 units: x0 y0 z0 x1 y1 z1; all 0 without corners.
static void RsView_PosBox(const unsigned char *pos, int corners, long long box[6])
{
    int i, c;
    memset(box, 0, 6 * sizeof(long long));
    for (i = 0; i < corners; i++)
        for (c = 0; c < 3; c++) {
            const long long q = RsView_ReadS16(pos + (size_t)i * 6 + 2 * c);
            if (i == 0 || q < box[c])
                box[c] = q;
            if (i == 0 || q > box[3 + c])
                box[3 + c] = q;
        }
}

static void RsView_ExtentAdd(const unsigned char *q, int sub, long long *radius2, int *ymin, int *ymax, int *any)
{
    const long long x = (long long)RsView_ReadS16(q) * sub, z = (long long)RsView_ReadS16(q + 4) * sub;
    const long long y = (long long)RsView_ReadS16(q + 2) * sub;
    const long long r2 = (x * x + z * z + RS_VIEW_SUB * RS_VIEW_SUB - 1) / (RS_VIEW_SUB * RS_VIEW_SUB);
    const int lo = (int)RsView_FloorDiv16(y), hi = (int)-RsView_FloorDiv16(-y);
    if (r2 > *radius2)
        *radius2 = r2;
    if (!*any || lo < *ymin)
        *ymin = lo;
    if (!*any || hi > *ymax)
        *ymax = hi;
    *any = 1;
}

// RLDPC1 at *at (tools/rldpack_native.inc, format of the preview file): every
// frame of the classic model, indexed - per corner of each triangle of
// RLDPV2 (same order, already wound) a record, per frame every record's
// position (s16 x, y, z, 1/16 game units). Turned into 47 poses of RLDPV2
// records (colour and flags from the neutral pose), so that the frames
// draw, frame, pick like any pose. NULL = refused (why says why); else the
// records (Rs_Alloc, the caller's) and *at behind the block.
static unsigned char *RsView_ParseFrames(const unsigned char *data, size_t bytes, size_t *at, struct RsViewPoseData poses[RS_VIEW_FRAMES],
                                         wchar_t *why, int whyCap)
{
    const struct RsViewPoseData neutralPose = poses[RS_VIEW_POSE_NEUTRAL];   // poses is overwritten below
    const struct RsViewPoseData *neutral = &neutralPose;
    const unsigned char *head = data + *at, *index, *pos;
    unsigned int frames, records, tris;
    unsigned char *out;
    size_t rest = bytes - *at;
    int f, i, c;

    if (rest < RS_VIEW_MAGIC_BYTES + 12) {
        swprintf(why, whyCap, L"the frames of the classic model end in their head");
        return NULL;
    }
    frames = RsView_ReadU32(head + RS_VIEW_MAGIC_BYTES);
    records = RsView_ReadU32(head + RS_VIEW_MAGIC_BYTES + 4);
    tris = RsView_ReadU32(head + RS_VIEW_MAGIC_BYTES + 8);
    rest -= RS_VIEW_MAGIC_BYTES + 12;
    // Compared by division: a damaged count could wrap a product.
    if (frames != RS_VIEW_FRAMES || tris != (unsigned int)neutral->count || records < 1 || records > 65536u ||
        (size_t)tris > rest / 6 || (size_t)records > (rest - (size_t)tris * 6) / ((size_t)RS_VIEW_FRAMES * 6)) {
        swprintf(why, whyCap, L"the frames of the classic model claim %u frames of %u records for %u triangles (the model has %d)", frames,
                 records, tris, neutral->count);
        return NULL;
    }
    index = head + RS_VIEW_MAGIC_BYTES + 12;
    pos = index + (size_t)tris * 6;
    for (i = 0; i < 3 * (int)tris; i++)
        if ((unsigned int)(index[2 * i] | (index[2 * i + 1] << 8)) >= records) {
            swprintf(why, whyCap, L"corner %d of the frames of the classic model names record %u of %u", i,
                     (unsigned int)(index[2 * i] | (index[2 * i + 1] << 8)), records);
            return NULL;
        }
    out = (unsigned char *)Rs_Alloc((size_t)RS_VIEW_FRAMES * tris * RS_VIEW_TRI_BYTES + 1);
    for (f = 0; f < RS_VIEW_FRAMES; f++) {
        unsigned char *t = out + (size_t)f * tris * RS_VIEW_TRI_BYTES;
        const unsigned char *frame = pos + (size_t)f * records * 6;
        for (i = 0; i < (int)tris; i++)
            for (c = 0; c < 3; c++) {
                const unsigned char *ix = index + (size_t)i * 6 + (size_t)c * 2;
                unsigned char *q = t + (size_t)i * RS_VIEW_TRI_BYTES + (size_t)c * RS_VIEW_CORNER_BYTES;
                memcpy(q, frame + (size_t)(ix[0] | (ix[1] << 8)) * 6, 6);
                memcpy(q + 6, neutral->tris + (size_t)i * RS_VIEW_TRI_BYTES + (size_t)c * RS_VIEW_CORNER_BYTES + 6, 4);
            }
        poses[f].tris = t;
        poses[f].count = (int)tris;
        poses[f].sub = 1;
    }
    *at += RS_VIEW_MAGIC_BYTES + 12 + (size_t)tris * 6 + (size_t)RS_VIEW_FRAMES * records * 6;
    return out;
}

// Checks every length before it is used; takes data on success (then it belongs
// to v), leaves v untouched and why filled otherwise.
static int RsView_Parse(struct RsView *v, unsigned char *data, size_t bytes, wchar_t *why, int whyCap)
{
    struct RsViewPoseData poses[RS_VIEW_FRAMES];
    struct RsTexNative native;
    unsigned char *frames = NULL;
    size_t at;
    unsigned int count;
    int p, i, c, sub;
    long long radius2 = 0;
    int ymin = 0, ymax = 0, any = 0;

    if (bytes < RS_VIEW_MAGIC_BYTES + 4) {
        swprintf(why, whyCap, L"the file is too short (%u bytes)", (unsigned)bytes);
        return 0;
    }
    // RLDPV2: positions in 1/16 game units; RLDPV1 (older): in whole game units.
    if (memcmp(data, s_rsViewMagic2, RS_VIEW_MAGIC_BYTES) == 0)
        sub = 1;
    else if (memcmp(data, s_rsViewMagic, RS_VIEW_MAGIC_BYTES) == 0)
        sub = RS_VIEW_SUB;
    else {
        swprintf(why, whyCap, L"it is not a preview file (it does not start with RLDPV2 or RLDPV1)");
        return 0;
    }
    count = RsView_ReadU32(data + RS_VIEW_MAGIC_BYTES);
    if (count != RS_VIEW_POSE_COUNT) {
        swprintf(why, whyCap, L"it has %u poses instead of %d", count, RS_VIEW_POSE_COUNT);
        return 0;
    }
    at = RS_VIEW_MAGIC_BYTES + 4;
    for (p = 0; p < (int)count; p++) {
        unsigned int tris;
        if (bytes - at < 4) {
            swprintf(why, whyCap, L"it ends before pose %d", p);
            return 0;
        }
        tris = RsView_ReadU32(data + at);
        at += 4;
        // Compared by division: tris * 30 could wrap for a damaged count.
        if (tris > (bytes - at) / RS_VIEW_TRI_BYTES) {
            swprintf(why, whyCap, L"pose %d claims %u triangles, but the file ends before them", p, tris);
            return 0;
        }
        poses[p].tris = data + at;
        poses[p].count = (int)tris;
        poses[p].sub = sub;
        at += (size_t)tris * RS_VIEW_TRI_BYTES;
    }
    // Behind the poses every frame of the classic model (RLDPC1, behind
    // RLDPV2 only; RsView_ParseFrames turns it into 47 poses like those
    // above), then the native model (RLDPN3 or RLDPN2, read by rs_tex.c with
    // its mip levels; open to everyone); anything else is an error as it
    // always was. From here nothing fails: native and the frames go to v.
    if (sub == 1 && bytes - at >= RS_VIEW_MAGIC_BYTES && memcmp(data + at, s_rsViewFramesMagic, RS_VIEW_MAGIC_BYTES) == 0) {
        frames = RsView_ParseFrames(data, bytes, &at, poses, why, whyCap);
        if (!frames)
            return 0;
        count = RS_VIEW_FRAMES;
    }
    memset(&native, 0, sizeof(native));
    if (at != bytes && !RsTex_ParseNative(data, bytes, at, &native, why, whyCap)) {
        Rs_Free(frames);
        return 0;   // rs_tex keeps nothing of a refused block; v stays as it was
    }

    // Extent over all poses, for a framing that does not change with the pose.
    for (p = 0; p < (int)count; p++)
        for (i = 0; i < poses[p].count; i++)
            for (c = 0; c < 3; c++)
                RsView_ExtentAdd(poses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES + c * RS_VIEW_CORNER_BYTES, sub,
                                 &radius2, &ymin, &ymax, &any);
    // The native model in it (its corners are s16 x, y, z as those of RLDPV2),
    // its end poses too: a blend lies between neutral and the end pose, so
    // inside both extents.
    for (p = 0; p < native.poses + 2; p++) {
        const unsigned char *pos = p < native.poses ? native.pos[p] : native.end[p - native.poses];
        for (i = 0; pos && i < native.vertices; i++)
            RsView_ExtentAdd(pos + (size_t)i * 6, 1, &radius2, &ymin, &ymax, &any);
    }

    RsView_DropModel(v);
    v->data = data;
    v->frames = frames;
    memcpy(v->poses, poses, (size_t)count * sizeof(poses[0]));
    v->poseCount = (int)count;
    v->native = native;
    // The box of the native model per pose, for its label (not per picture).
    memset(v->nativeBox, 0, sizeof(v->nativeBox));
    for (p = 0; p < native.poses; p++)
        RsView_PosBox(native.pos[p], native.vertices, v->nativeBox[p]);
    v->loaded = 1;
    v->radius2 = radius2;
    v->ymin = ymin;
    v->ymax = ymax;
    v->message[0] = 0;
    return 1;
}

static void RsView_Changed(HWND view, struct RsView *v)
{
    v->dirty = 1;
    InvalidateRect(view, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Preview features: the pose set and the wheel model
// ---------------------------------------------------------------------------
//
// A pose set ("RLDPS1") carries the triangle records of RLDPV1 (RsView_Parse)
// in whole game units like an RLDPV1 model; a wheel model ("RLDPW3",
// tools/rldpack_wheel.inc, read by rs_tex.c) its texture, its material and
// corners with UV in 1/16 game units about its axle. Without them nothing below is used and the view draws as
// before.

// One block "u32 triangles + records" at *at; checks its length first.
static int RsView_ParseTris(const unsigned char *data, size_t bytes, size_t *at, struct RsViewPoseData *out)
{
    unsigned int tris;
    if (bytes - *at < 4)
        return 0;
    tris = RsView_ReadU32(data + *at);
    *at += 4;
    if (tris > (bytes - *at) / RS_VIEW_TRI_BYTES)
        return 0;
    out->tris = data + *at;
    out->count = (int)tris;
    out->sub = RS_VIEW_SUB;   // whole game units
    *at += (size_t)tris * RS_VIEW_TRI_BYTES;
    return 1;
}

static void RsView_DropSet(struct RsView *v)
{
    Rs_Free(v->setData);
    v->setData = NULL;
    memset(v->setPoses, 0, sizeof(v->setPoses));
    v->setCount = 0;
    v->setIndex = -1;
    v->setRadius2 = 0;
    v->setYmin = 0;
    v->setYmax = 0;
}

// A pose set; takes data on success, leaves v untouched otherwise.
static int RsView_ParseSet(struct RsView *v, unsigned char *data, size_t bytes, wchar_t *why, int whyCap)
{
    struct RsViewPoseData poses[RS_VIEW_SET_POSES_MAX];
    size_t at;
    unsigned int count;
    int p, i, c, ymin = 0, ymax = 0, any = 0;
    long long radius2 = 0;

    if (bytes < RS_VIEW_MAGIC_BYTES + 4 || memcmp(data, s_rsViewSetMagic, RS_VIEW_MAGIC_BYTES) != 0) {
        swprintf(why, whyCap, L"it is not a pose set (it does not start with RLDPS1)");
        return 0;
    }
    count = RsView_ReadU32(data + RS_VIEW_MAGIC_BYTES);
    if (count < 1 || count > RS_VIEW_SET_POSES_MAX) {
        swprintf(why, whyCap, L"it has %u poses, 1..%d are possible", count, RS_VIEW_SET_POSES_MAX);
        return 0;
    }
    at = RS_VIEW_MAGIC_BYTES + 4;
    for (p = 0; p < (int)count; p++)
        if (!RsView_ParseTris(data, bytes, &at, &poses[p])) {
            swprintf(why, whyCap, L"it ends inside pose %d", p);
            return 0;
        }
    if (at != bytes) {
        swprintf(why, whyCap, L"%u bytes follow after the last pose", (unsigned)(bytes - at));
        return 0;
    }
    for (p = 0; p < (int)count; p++)
        for (i = 0; i < poses[p].count; i++)
            for (c = 0; c < 3; c++) {
                const unsigned char *q = poses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES + c * RS_VIEW_CORNER_BYTES;
                const long long x = RsView_ReadS16(q), y = RsView_ReadS16(q + 2), z = RsView_ReadS16(q + 4);
                if (x * x + z * z > radius2)
                    radius2 = x * x + z * z;
                if (!any || y < ymin)
                    ymin = (int)y;
                if (!any || y > ymax)
                    ymax = (int)y;
                any = 1;
            }

    RsView_DropSet(v);
    v->setData = data;
    memcpy(v->setPoses, poses, (size_t)count * sizeof(poses[0]));
    v->setCount = (int)count;
    v->setRadius2 = radius2;
    v->setYmin = ymin;
    v->setYmax = ymax;
    return 1;
}

static void RsView_DropWheel(struct RsView *v)
{
    RsTex_FreeWheel(&v->wheel);
    memset(&v->wheel, 0, sizeof(v->wheel));
    Rs_Free(v->wheelData);
    v->wheelData = NULL;
    v->wheelCount = 0;
    v->wheelR2 = 0;
    v->wheelHalfW = 0;
    v->wheelRadius2 = 0;
    v->wheelYmin = 0;
    v->wheelYmax = 0;
}

static void RsView_DropRear(struct RsView *v)
{
    RsTex_FreeWheel(&v->rear);
    memset(&v->rear, 0, sizeof(v->rear));
    Rs_Free(v->rearData);
    v->rearData = NULL;
    v->rearCount = 0;
    v->rearR2 = 0;
    v->rearHalfW = 0;
}

// A wheel model (RLDPW3, read by rs_tex.c with its mip levels) - the wheel
// model (rear 0) or the rear wheel model (rear 1); takes data on success,
// leaves v untouched otherwise.
static int RsView_ParseWheel(struct RsView *v, int rear, unsigned char *data, size_t bytes, wchar_t *why, int whyCap)
{
    struct RsTexWheel w;
    long long r2 = 0;
    int i, c, halfW = 0;

    memset(&w, 0, sizeof(w));
    if (!RsTex_ParseWheel(data, bytes, &w, why, whyCap))
        return 0;
    for (i = 0; i < w.triangles; i++)
        for (c = 0; c < 3; c++) {
            struct RsTexCorner k;
            int xyz[3];
            long long ax, y, z;
            RsTex_WheelCorner(&w, i, c, xyz, &k);
            ax = xyz[0] < 0 ? -xyz[0] : xyz[0];
            y = xyz[1];
            z = xyz[2];
            if (y * y + z * z > r2)
                r2 = y * y + z * z;
            if (ax > halfW)
                halfW = (int)ax;
        }

    if (rear) {
        RsView_DropRear(v);
        v->rearData = data;
        v->rear = w;
        v->rearCount = w.triangles;
        v->rearR2 = r2;
        v->rearHalfW = halfW;
    } else {
        RsView_DropWheel(v);
        v->wheelData = data;
        v->wheel = w;
        v->wheelCount = w.triangles;
        v->wheelR2 = r2;
        v->wheelHalfW = halfW;
    }
    return 1;
}

// THE REAR WHEEL AND THE AXLES (rs_view.h): the model of wheel point w (0
// front left, 1 front right, 2 rear left, 3 rear right) - the rear wheel
// model at the rear pair while there is one - and the triangles of all four.
static const struct RsTexWheel *RsView_WheelOf(const struct RsView *v, int w)
{
    return w >= 2 && v->rearCount ? &v->rear : &v->wheel;
}

static int RsView_WheelTris(const struct RsView *v)
{
    return v->wheelCount ? 2 * v->wheelCount + 2 * (v->rearCount ? v->rearCount : v->wheelCount) : 0;
}

// The scale of the model at wheel point w, num / den: the wheel scale, and
// for the rear wheel model its size against the wheel model as well
// (RsView_SetRearWheelSize).
static void RsView_WheelScaleOf(const struct RsView *v, int w, long long *num, long long *den)
{
    if (w >= 2 && v->rearCount) {
        *num = (long long)v->wheelScale * v->rearSize;
        *den = 100LL * RS_VIEW_REAR_SIZE_SAME;
    } else {
        *num = v->wheelScale;
        *den = 100;
    }
}

// How far wheel point w rises at its scale (1/16 units): the bottom of the
// wheel stays where the game's wheel touches the ground, so the centre rises
// by the radius grown (renderer SPEC P2: ground contact as retail; WHLS v3:
// each wheel's centre at its own radius).
static long long RsView_WheelLift(const struct RsView *v, int w)
{
    long long num, den;
    RsView_WheelScaleOf(v, w, &num, &den);
    return (long long)v->tireHalf * (num - den) / den;
}

// The centre of wheel point w (1/16 units): the dummy's point, raised by
// the lift, moved by the offset of its axle (dz forward, dy up, dtrack
// outward: away from x = 0 on the side of the point) - the centre make-char
// writes into WHLS v3 (front = (36 + track / 2, r + dy, 49.75 + dz)).
static void RsView_WheelCentre(const struct RsView *v, int w, long long out[3])
{
    const int *o = v->axle[w < 2 ? RS_VIEW_AXLE_FRONT : RS_VIEW_AXLE_REAR];
    out[0] = (long long)v->tireAt[w][0] + (v->tireAt[w][0] < 0 ? -o[2] : o[2]);
    out[1] = (long long)v->tireAt[w][1] + RsView_WheelLift(v, w) + o[1];
    out[2] = (long long)v->tireAt[w][2] + o[0];
}

// The wheel models at their scale about a wheel point, 1/16 units: radius =
// how far they reach up and down, reach = across (the larger of the wheel
// model and the rear wheel model). Under every spin a wheel stays in a
// cylinder of its radius and half width; under every steering angle that
// stays in a circle of radius reach about the point.
static void RsView_WheelReach(const struct RsView *v, long long *radius, long long *reach)
{
    long long r = ((long long)RsView_Isqrt((unsigned long long)v->wheelR2) + 1) * v->wheelScale / 100 + 1;
    long long hw = (long long)v->wheelHalfW * v->wheelScale / 100 + 1;
    if (v->rearCount) {
        long long num, den, rr, rhw;
        RsView_WheelScaleOf(v, 2, &num, &den);
        rr = ((long long)RsView_Isqrt((unsigned long long)v->rearR2) + 1) * num / den + 1;
        rhw = (long long)v->rearHalfW * num / den + 1;
        r = rr > r ? rr : r;
        hw = rhw > hw ? rhw : hw;
    }
    *radius = r;
    *reach = (long long)RsView_Isqrt((unsigned long long)(r * r + hw * hw)) + 1;
}

// The framing of the wheel model (wheelRadius2, wheelYmin, wheelYmax in whole
// units, like tireRadius2), again on loading and on a new scale - never on a
// turn.
static void RsView_WheelExtent(struct RsView *v)
{
    long long r, reach, r2 = 0;
    int i, ymin = 0, ymax = 0;
    if (!v->wheelCount)
        return;
    RsView_WheelReach(v, &r, &reach);
    for (i = 0; i < 4; i++) {
        long long at[3];
        RsView_WheelCentre(v, i, at);
        const long long x = at[0], y = at[1], z = at[2];
        const long long ax = ((x < 0 ? -x : x) + reach) / RS_VIEW_SUB + 1;
        const long long az = ((z < 0 ? -z : z) + reach) / RS_VIEW_SUB + 1;
        const int ylo = (int)RsView_FloorDiv16(y - r), yhi = (int)-RsView_FloorDiv16(-(y + r));
        if (ax * ax + az * az > r2)
            r2 = ax * ax + az * az;
        if (ylo < ymin)
            ymin = ylo;
        if (yhi > ymax)
            ymax = yhi;
    }
    v->wheelRadius2 = r2;
    v->wheelYmin = ymin;
    v->wheelYmax = ymax;
}

// One corner (s16 x, y, z) into the z range of its x (RsView_ModelSpans).
static void RsView_ZRange(short *zLo, short *zHi, unsigned char *has, const unsigned char *q)
{
    const int x = RsView_ReadS16(q) + 32768;
    const short z = (short)RsView_ReadS16(q + 4);
    if (!has[x]) {
        has[x] = 1;
        zLo[x] = z;
        zHi[x] = z;
    } else if (z < zLo[x]) {
        zLo[x] = z;
    } else if (z > zHi[x]) {
        zHi[x] = z;
    }
}

// The model's span across the picture (all poses) with the game's wheels
// under it - whether they are shown or not, so that switching them never zooms.
static void RsView_ModelSpans(struct RsView *v)
{
    struct RsViewTrig tr;
    int p, i, c, positions = 0;

    RsView_TrigFill(&tr);
    RsView_SpanReset(v->modelLo, v->modelHi);
    // The poses of the file and of the native model: up to 49 poses of 30 000
    // triangles are millions of corners, each of 360 products in the span.
    // The span takes only the largest and smallest value per yaw, and for
    // one x the smallest and the largest z give them (x cos + z sin is
    // linear in z): so per x of the file (an s16) only those two go in -
    // the same span, exactly. RLDPV1 (whole units) the old way.
    {
        short *zLo = (short *)Rs_Alloc(65536 * sizeof(short));
        short *zHi = (short *)Rs_Alloc(65536 * sizeof(short));
        unsigned char *has = (unsigned char *)Rs_Alloc(65536);
        int x;
        for (p = 0; p < v->poseCount; p++) {
            for (i = 0; i < v->poses[p].count; i++) {
                const unsigned char *t = v->poses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES;
                for (c = 0; c < 3; c++) {
                    const unsigned char *q = t + c * RS_VIEW_CORNER_BYTES;
                    if (v->poses[p].sub != 1)
                        RsView_SpanAdd(&tr, v->modelLo, v->modelHi, (long long)RsView_ReadS16(q) * v->poses[p].sub,
                                       (long long)RsView_ReadS16(q + 4) * v->poses[p].sub);
                    else
                        RsView_ZRange(zLo, zHi, has, q);
                }
            }
        }
        for (p = 0; p < v->native.poses + 2; p++) {
            const unsigned char *pos = p < v->native.poses ? v->native.pos[p] : v->native.end[p - v->native.poses];
            for (i = 0; pos && i < v->native.vertices; i++)
                RsView_ZRange(zLo, zHi, has, pos + (size_t)i * 6);
        }
        for (x = 0; x < 65536; x++)
            if (has[x]) {
                RsView_SpanAdd(&tr, v->modelLo, v->modelHi, x - 32768, zLo[x]);
                if (zHi[x] != zLo[x])
                    RsView_SpanAdd(&tr, v->modelLo, v->modelHi, x - 32768, zHi[x]);
            }
        Rs_Free(zLo);
        Rs_Free(zHi);
        Rs_Free(has);
    }
    if (v->tireEnd > v->tireFirst &&
        Rs_DummyMesh(1, RS_VIEW_POSE_NEUTRAL, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor,
                     RS_VIEW_DUMMY_TRI_MAX, &positions) &&
        v->tireEnd <= positions)
        for (i = v->tireFirst; i < v->tireEnd; i++)
            RsView_SpanAdd(&tr, v->modelLo, v->modelHi, v->dumPos[3 * i], v->dumPos[3 * i + 2]);
    // A pose set and a wheel model, while loaded: the framing holds all of
    // them, so that switching the pose or turning the wheels never zooms.
    for (p = 0; p < v->setCount; p++) {
        for (i = 0; i < v->setPoses[p].count; i++) {
            const unsigned char *t = v->setPoses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES;
            for (c = 0; c < 3; c++) {
                const unsigned char *q = t + c * RS_VIEW_CORNER_BYTES;
                RsView_SpanAdd(&tr, v->modelLo, v->modelHi, (long long)RsView_ReadS16(q) * v->setPoses[p].sub,
                               (long long)RsView_ReadS16(q + 4) * v->setPoses[p].sub);
            }
        }
    }
    if (v->wheelCount) {
        long long r, reach;
        RsView_WheelReach(v, &r, &reach);
        for (i = 0; i < 4; i++) {
            long long at[3];
            RsView_WheelCentre(v, i, at);
            for (c = 0; c < 4; c++)
                RsView_SpanAdd(&tr, v->modelLo, v->modelHi, at[0] + ((c & 1) ? reach : -reach), at[2] + ((c & 2) ? reach : -reach));
        }
    }
    if (v->modelLo[0] > v->modelHi[0]) {
        // Nothing at all: a point at the origin.
        RsView_SpanReset(v->modelLo, v->modelHi);
        RsView_SpanAdd(&tr, v->modelLo, v->modelHi, 0, 0);
    }
}

// Takes data (Rs_Alloc) in any case.
static int RsView_Take(HWND view, struct RsView *v, unsigned char *data, size_t bytes)
{
    wchar_t why[256];
    if (!RsView_Parse(v, data, bytes, why, 256)) {
        Rs_Free(data);
        RsView_DropModel(v);
        swprintf(v->message, RS_VIEW_MESSAGE_CAP, L"The preview could not be shown: %ls - check the model again.", why);
        RsView_Changed(view, v);
        return 0;
    }
    RsView_ModelSpans(v);
    RsView_Changed(view, v);
    return 1;
}

// ---------------------------------------------------------------------------
// Projection and rasterizer
// ---------------------------------------------------------------------------

// A point in 1/16 game units -> screen. turn = 0 for the floor, which stays
// under the camera. Products stay far below 2^63: positions below 2^21, the
// Q14 factors below 2^15, the focal length below 2^24.
// v / 16384 as the C division (towards zero), by shifts: a 64-bit division
// is a library call in the 32-bit build, and this runs for every corner.
static long long RsView_Q14(long long v)
{
    return v >= 0 ? v >> 14 : -((-v) >> 14);
}

static void RsView_Project(const struct RsViewCam *cam, long long x, long long y, long long z,
                           long long off, int turn, struct RsViewVert *out)
{
    long long x1 = x, z1 = z, y2, z2, d;
    if (turn) {
        x1 = RsView_Q14(x * cam->yawCos + z * cam->yawSin);
        z1 = RsView_Q14(z * cam->yawCos - x * cam->yawSin);
    }
    x1 += off;
    y -= cam->ycq;
    y2 = RsView_Q14(y * cam->pitchCos - z1 * cam->pitchSin);
    z2 = RsView_Q14(y * cam->pitchSin + z1 * cam->pitchCos);
    d = cam->dq - z2;   // the framing keeps it above half the camera distance
    if (d < cam->dqNear) {
        // Nearer than a quarter of the distance (only the race camera can
        // come so near): far outside, so that RsView_Triangle skips the
        // triangle; the depth stays below 2^23.
        out->x = 2 * RS_VIEW_COORD_MAX;
        out->y = 2 * RS_VIEW_COORD_MAX;
        out->z = 0;
        return;
    }
    out->x = cam->cxq + x1 * cam->fq / d;
    out->y = cam->cyq - y2 * cam->fq / d;
    out->z = (int)((cam->dq << RS_VIEW_DEPTH_SHIFT) / d);
}

static int RsView_TopLeft(long long dx, long long dy)
{
    return dy < 0 || (dy == 0 && dx > 0);
}

// An exact quotient carried along a row: the value is q * den + r with
// 0 <= r < den (den > 0). Stepping by a fixed numerator adds that one's
// quotient and remainder, so every pixel gets the same integer as dividing
// there - without a division per pixel (a 64-bit division is a library call
// in the 32-bit build). Inside a triangle the numerators are >= 0, where
// this floor equals the truncating C division used before.
struct RsViewQuot {
    long long q, r;
};

static void RsView_QuotSet(struct RsViewQuot *o, long long n, long long den)
{
    long long q = n / den, r = n - q * den;
    if (r < 0) {
        q--;
        r += den;
    }
    o->q = q;
    o->r = r;
}

#define RS_VIEW_QUOT_STEP(o, d, den) \
    do { (o).q += (d).q; (o).r += (d).r; if ((o).r >= (den)) { (o).r -= (den); (o).q++; } } while (0)

// The pixel range and the edge functions of a triangle whose edge function
// (b - a) x (c - a) is positive (RsView_Fill and its kin). Edge i is the one
// opposite corner i; its value is the weight of corner i.
struct RsViewEdges {
    long long area;
    long long px0, px1, py0, py1;
    long long e0Row, e1Row, e2Row;
    long long e0dx, e1dx, e2dx, e0dy, e1dy, e2dy;
    int bias0, bias1, bias2;
};

// 0: no pixel of the picture.
static int RsView_EdgesSet(const struct RsViewTarget *t, const struct RsViewVert *a, const struct RsViewVert *b,
                           const struct RsViewVert *c, struct RsViewEdges *e)
{
    long long sx, sy;
    e->area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    // Pixel range: centres 16 * p + 8 inside the bounding box.
    e->px0 = -RsView_FloorDiv16(-(RsView_Min3(a->x, b->x, c->x) - 8));
    e->px1 = RsView_FloorDiv16(RsView_Max3(a->x, b->x, c->x) - 8);
    e->py0 = -RsView_FloorDiv16(-(RsView_Min3(a->y, b->y, c->y) - 8));
    e->py1 = RsView_FloorDiv16(RsView_Max3(a->y, b->y, c->y) - 8);
    if (e->px0 < 0)
        e->px0 = 0;
    if (e->py0 < t->y0)
        e->py0 = t->y0;
    if (e->px1 > t->w - 1)
        e->px1 = t->w - 1;
    if (e->py1 > t->y1 - 1)
        e->py1 = t->y1 - 1;
    if (e->px0 > e->px1 || e->py0 > e->py1 || e->area <= 0)
        return 0;
    e->e0dx = -(c->y - b->y) * RS_VIEW_SUB;
    e->e1dx = -(a->y - c->y) * RS_VIEW_SUB;
    e->e2dx = -(b->y - a->y) * RS_VIEW_SUB;
    e->e0dy = (c->x - b->x) * RS_VIEW_SUB;
    e->e1dy = (a->x - c->x) * RS_VIEW_SUB;
    e->e2dy = (b->x - a->x) * RS_VIEW_SUB;
    e->bias0 = RsView_TopLeft(c->x - b->x, c->y - b->y) ? 0 : -1;
    e->bias1 = RsView_TopLeft(a->x - c->x, a->y - c->y) ? 0 : -1;
    e->bias2 = RsView_TopLeft(b->x - a->x, b->y - a->y) ? 0 : -1;
    sx = e->px0 * RS_VIEW_SUB + 8;
    sy = e->py0 * RS_VIEW_SUB + 8;
    e->e0Row = (c->x - b->x) * (sy - b->y) - (c->y - b->y) * (sx - b->x);
    e->e1Row = (a->x - c->x) * (sy - c->y) - (a->y - c->y) * (sx - c->x);
    e->e2Row = (b->x - a->x) * (sy - a->y) - (b->y - a->y) * (sx - a->x);
    return 1;
}

// Fills a triangle whose edge function (b - a) x (c - a) is positive. Pixels are
// sampled at their centre; an edge pixel belongs to the triangle on its top or
// left edge (as Direct3D and OpenGL do), so that neighbouring triangles neither
// overlap nor leave a gap. Colour and depth are interpolated linearly in screen
// space - affine Gouraud like the PS1 GPU; the depth is 1/distance, which is
// linear in screen space. Every value is the weighted mean of the corners
// (sum of e_i x value_i / area, colours rounded), carried exactly along the
// row (RsViewQuot); e0 + e1 + e2 == area and all three >= 0 inside, so no
// sum overflows (area < 2^40, depth < 2^23; across the bounding box the
// products stay below 2^62).
static void RsView_Fill(const struct RsViewTarget *t, const struct RsViewVert *a,
                        const struct RsViewVert *b, const struct RsViewVert *c)
{
    struct RsViewEdges e;
    struct RsViewQuot dz, dr, dg, db, rz, rr, rg, rb, sz, sr, sg, sb;
    long long px, py, area, half;
    int flat;
    unsigned int flatRgb;

    if (!RsView_EdgesSet(t, a, b, c, &e))
        return;
    area = e.area;
    half = area / 2;
    // One colour on all three corners (the floor, the dummy): the mean is
    // that colour exactly ((area x c + half) / area == c).
    flat = a->r == b->r && a->r == c->r && a->g == b->g && a->g == c->g && a->b == b->b && a->b == c->b;
    flatRgb = ((unsigned int)a->r << 16) | ((unsigned int)a->g << 8) | (unsigned int)a->b;
    RsView_QuotSet(&dz, e.e0dx * a->z + e.e1dx * b->z + e.e2dx * c->z, area);
    RsView_QuotSet(&dr, e.e0dx * a->r + e.e1dx * b->r + e.e2dx * c->r, area);
    RsView_QuotSet(&dg, e.e0dx * a->g + e.e1dx * b->g + e.e2dx * c->g, area);
    RsView_QuotSet(&db, e.e0dx * a->b + e.e1dx * b->b + e.e2dx * c->b, area);
    // The first pixel of each row, stepped down the rows the same way.
    RsView_QuotSet(&rz, e.e0Row * a->z + e.e1Row * b->z + e.e2Row * c->z, area);
    RsView_QuotSet(&sz, e.e0dy * a->z + e.e1dy * b->z + e.e2dy * c->z, area);
    RsView_QuotSet(&rr, e.e0Row * a->r + e.e1Row * b->r + e.e2Row * c->r + half, area);
    RsView_QuotSet(&sr, e.e0dy * a->r + e.e1dy * b->r + e.e2dy * c->r, area);
    RsView_QuotSet(&rg, e.e0Row * a->g + e.e1Row * b->g + e.e2Row * c->g + half, area);
    RsView_QuotSet(&sg, e.e0dy * a->g + e.e1dy * b->g + e.e2dy * c->g, area);
    RsView_QuotSet(&rb, e.e0Row * a->b + e.e1Row * b->b + e.e2Row * c->b + half, area);
    RsView_QuotSet(&sb, e.e0dy * a->b + e.e1dy * b->b + e.e2dy * c->b, area);

    for (py = e.py0; py <= e.py1; py++) {
        long long e0 = e.e0Row, e1 = e.e1Row, e2 = e.e2Row;
        unsigned int *pix = t->pixels + (size_t)py * (size_t)t->w;
        int *dep = t->depth + (size_t)py * (size_t)t->w;
        unsigned char *own = t->mask + (size_t)py * (size_t)t->w;
        struct RsViewQuot qz = rz, qr = rr, qg = rg, qb = rb;
        for (px = e.px0; px <= e.px1; px++) {
            if (e0 + e.bias0 >= 0 && e1 + e.bias1 >= 0 && e2 + e.bias2 >= 0 &&
                !(t->maskMode == RS_VIEW_MASK_SKIP && own[px])) {
                const int z = (int)qz.q;
                if (z > dep[px]) {
                    dep[px] = z;
                    pix[px] = flat ? flatRgb
                                   : ((unsigned int)qr.q << 16) | ((unsigned int)qg.q << 8) | (unsigned int)qb.q;
                    if (t->maskMode == RS_VIEW_MASK_SET)
                        own[px] = 1;
                }
            }
            e0 += e.e0dx;
            e1 += e.e1dx;
            e2 += e.e2dx;
            RS_VIEW_QUOT_STEP(qz, dz, area);
            if (!flat) {
                RS_VIEW_QUOT_STEP(qr, dr, area);
                RS_VIEW_QUOT_STEP(qg, dg, area);
                RS_VIEW_QUOT_STEP(qb, db, area);
            }
        }
        e.e0Row += e.e0dy;
        e.e1Row += e.e1dy;
        e.e2Row += e.e2dy;
        RS_VIEW_QUOT_STEP(rz, sz, area);
        RS_VIEW_QUOT_STEP(rr, sr, area);
        RS_VIEW_QUOT_STEP(rg, sg, area);
        RS_VIEW_QUOT_STEP(rb, sb, area);
    }
}

// RsView_Fill for the native model and the wheel model: the game's "nr"
// program per pixel (rs_tex.h). The same edges and depth; u, v and the
// corner colour C (RGBA) perspective correct, as the GPU interpolates them
// (weights in Q16 times value x z, divided by the same sum of z); T = the
// texel of the triangle's mip level (RsTex_Texel; img NULL: untextured),
// then RsTex_Shade with the material m. A pixel the mask leaves out keeps
// the depth and colour below it, as in the game. Products stay below 2^63:
// weights 2^16, z below 2^23, u and v within +-2^22, colours 255. The
// weights and the depth are carried along the row exactly (RsViewQuot); u
// and v (and C where the corners differ) are divided per pixel.
static void RsView_FillTex(const struct RsViewTarget *t, const struct RsViewVert *a, const struct RsViewVert *b,
                           const struct RsViewVert *c, const struct RsTexImage *img, int level,
                           const struct RsTexMaterial *m)
{
    const long long auz = a->u * a->z, buz = b->u * b->z, cuz = c->u * c->z;
    const long long avz = a->v * a->z, bvz = b->v * b->z, cvz = c->v * c->z;
    const long long arz = (long long)a->r * a->z, brz = (long long)b->r * b->z, crz = (long long)c->r * c->z;
    const long long agz = (long long)a->g * a->z, bgz = (long long)b->g * b->z, cgz = (long long)c->g * c->z;
    const long long abz = (long long)a->b * a->z, bbz = (long long)b->b * b->z, cbz = (long long)c->b * c->z;
    const long long aaz = (long long)a->a * a->z, baz = (long long)b->a * b->z, caz = (long long)c->a * c->z;
    // One colour on all three corners (always without COL0, and every
    // wheel): the perspective mean is that colour, no division needed.
    const int flat = a->r == b->r && a->r == c->r && a->g == b->g && a->g == c->g && a->b == b->b && a->b == c->b &&
                     a->a == b->a && a->a == c->a;
    struct RsViewEdges e;
    struct RsViewQuot dz, dw0, dw1, dw2, rq[4], sq[4];
    long long px, py, area;
    int k;

    if (!RsView_EdgesSet(t, a, b, c, &e))
        return;
    area = e.area;
    RsView_QuotSet(&dz, e.e0dx * a->z + e.e1dx * b->z + e.e2dx * c->z, area);
    RsView_QuotSet(&dw0, e.e0dx << 16, area);
    RsView_QuotSet(&dw1, e.e1dx << 16, area);
    RsView_QuotSet(&dw2, e.e2dx << 16, area);
    // The first pixel of each row, stepped down the rows the same way.
    RsView_QuotSet(&rq[0], e.e0Row * a->z + e.e1Row * b->z + e.e2Row * c->z, area);
    RsView_QuotSet(&sq[0], e.e0dy * a->z + e.e1dy * b->z + e.e2dy * c->z, area);
    RsView_QuotSet(&rq[1], e.e0Row << 16, area);
    RsView_QuotSet(&sq[1], e.e0dy << 16, area);
    RsView_QuotSet(&rq[2], e.e1Row << 16, area);
    RsView_QuotSet(&sq[2], e.e1dy << 16, area);
    RsView_QuotSet(&rq[3], e.e2Row << 16, area);
    RsView_QuotSet(&sq[3], e.e2dy << 16, area);

    for (py = e.py0; py <= e.py1; py++) {
        long long e0 = e.e0Row, e1 = e.e1Row, e2 = e.e2Row;
        unsigned int *pix = t->pixels + (size_t)py * (size_t)t->w;
        int *dep = t->depth + (size_t)py * (size_t)t->w;
        unsigned char *own = t->mask + (size_t)py * (size_t)t->w;
        struct RsViewQuot qz = rq[0], qw0 = rq[1], qw1 = rq[2], qw2 = rq[3];
        for (px = e.px0; px <= e.px1; px++) {
            if (e0 + e.bias0 >= 0 && e1 + e.bias1 >= 0 && e2 + e.bias2 >= 0 &&
                !(t->maskMode == RS_VIEW_MASK_SKIP && own[px])) {
                const int z = (int)qz.q;
                if (z > dep[px]) {
                    const long long w0 = qw0.q, w1 = qw1.q, w2 = qw2.q;
                    const long long zs = w0 * a->z + w1 * b->z + w2 * c->z;
                    const unsigned char *texel = NULL;
                    int cr = a->r, cg = a->g, cb = a->b, ca = a->a, out;
                    if (zs > 0) {
                        if (img) {
                            const long long uq = (w0 * auz + w1 * buz + w2 * cuz) / zs;
                            const long long vq = (w0 * avz + w1 * bvz + w2 * cvz) / zs;
                            texel = RsTex_Texel(img, level, uq, vq);
                        }
                        if (!flat) {
                            const long long half = zs / 2;
                            cr = (int)((w0 * arz + w1 * brz + w2 * crz + half) / zs);
                            cg = (int)((w0 * agz + w1 * bgz + w2 * cgz + half) / zs);
                            cb = (int)((w0 * abz + w1 * bbz + w2 * cbz + half) / zs);
                            ca = (int)((w0 * aaz + w1 * baz + w2 * caz + half) / zs);
                        }
                    } else if (img) {
                        texel = RsTex_Texel(img, level, a->u, a->v);
                    }
                    out = RsTex_Shade(texel, cr, cg, cb, ca, m);
                    if (out != RS_TEX_DISCARD) {
                        dep[px] = z;
                        pix[px] = (unsigned int)out;
                        if (t->maskMode == RS_VIEW_MASK_SET)
                            own[px] = 1;
                    }
                }
            }
            e0 += e.e0dx;
            e1 += e.e1dx;
            e2 += e.e2dx;
            RS_VIEW_QUOT_STEP(qz, dz, area);
            RS_VIEW_QUOT_STEP(qw0, dw0, area);
            RS_VIEW_QUOT_STEP(qw1, dw1, area);
            RS_VIEW_QUOT_STEP(qw2, dw2, area);
        }
        e.e0Row += e.e0dy;
        e.e1Row += e.e1dy;
        e.e2Row += e.e2dy;
        for (k = 0; k < 4; k++)
            RS_VIEW_QUOT_STEP(rq[k], sq[k], area);
    }
}

// THE DRAW LIST. A picture is drawn in two steps: every triangle is first
// projected and prepared into a list in the fixed drawing order
// (RsView_TriPrep), then the list is filled band by band of rows
// (RsView_RasterBand). Each pixel belongs to one band and sees the
// triangles in list order, so the picture is the same whichever thread
// fills which band (RsView_ParallelFor) - and the same as one thread.
enum { RS_VIEW_TRI_SKIP = 0, RS_VIEW_TRI_FILL, RS_VIEW_TRI_TEX, RS_VIEW_TRI_SHADOW };

struct RsViewTri {
    struct RsViewVert v[3];                 // in the order whose edge function is positive
    const struct RsTexImage *img;           // RS_VIEW_TRI_TEX: the texture, NULL none
    const struct RsTexMaterial *mat;        // RS_VIEW_TRI_TEX: the material
    int level;                              // RS_VIEW_TRI_TEX: the mip level of the triangle
    int kind;                               // RS_VIEW_TRI_*
    int maskMode;                           // RS_VIEW_MASK_*
};

// One triangle into the list (o, with its rows in *rows: first and last row
// it may touch, or first > last when it draws nothing). cull: draw only the
// visible side. In pixel rows (y down) a triangle that runs
// counter-clockwise as seen (right-hand normal towards the viewer) has a
// negative edge function. kind RS_VIEW_TRI_TEX: the "nr" program with
// material mat and texture img (NULL: untextured), at the mip level of the
// triangle - the screen derivatives of u and v at its centroid
// (RsTex_TriangleSlopes, RsTex_Level; the GPU takes them per 2 x 2 pixels,
// which can pick the next level near a boundary). A corner far outside the window (only through a broken
// framing, or nearer than RsViewCam.dqNear) skips it; that keeps the edge
// functions below 2^40.
static void RsView_TriPrep(struct RsViewTri *o, int *rows, const struct RsViewVert *v, int cull, int kind, int maskMode,
                           const struct RsTexImage *img, const struct RsTexMaterial *mat)
{
    long long area;
    int i;

    o->kind = RS_VIEW_TRI_SKIP;
    rows[0] = 1;
    rows[1] = 0;
    for (i = 0; i < 3; i++)
        if (v[i].x < -RS_VIEW_COORD_MAX || v[i].x > RS_VIEW_COORD_MAX ||
            v[i].y < -RS_VIEW_COORD_MAX || v[i].y > RS_VIEW_COORD_MAX)
            return;
    area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
    if (area == 0)
        return;
    if (cull && area > 0)
        return;
    o->v[0] = v[0];
    o->v[1] = area < 0 ? v[2] : v[1];
    o->v[2] = area < 0 ? v[1] : v[2];
    o->kind = kind;
    o->maskMode = maskMode;
    o->img = img;
    o->mat = mat;
    o->level = 0;
    if (kind == RS_VIEW_TRI_TEX && img && img->levels > 1) {
        long long x[3], y[3], z[3], u[3], vv[3], slope[4];
        for (i = 0; i < 3; i++) {
            x[i] = v[i].x;
            y[i] = v[i].y;
            z[i] = v[i].z;
            u[i] = v[i].u;
            vv[i] = v[i].v;
        }
        RsTex_TriangleSlopes(x, y, z, u, vv, slope);
        o->level = RsTex_Level(img, slope[0], slope[1], slope[2], slope[3]);
    }
    rows[0] = (int)-RsView_FloorDiv16(-(RsView_Min3(v[0].y, v[1].y, v[2].y) - 8));
    rows[1] = (int)RsView_FloorDiv16(RsView_Max3(v[0].y, v[1].y, v[2].y) - 8);
}

// ---------------------------------------------------------------------------
// The scene
// ---------------------------------------------------------------------------

// Framing from the extents only (not from the yaw, the pose or the wheels).
// Across the picture the spans give, for every yaw, where model and dummy
// reach; the dummy stands so far to the right that the two keep RS_VIEW_GAP
// between them at the worst yaw, and the pair is centred. In depth both fit
// in a cylinder about their vertical axis, so any turn stays inside. topH:
// room kept free at the top for text. Without the dummy (RsView_SetCrashRef
// 0 while a model is shown) the model alone is framed.
//
// The framing is fitted at the start view's pitch (RS_VIEW_PITCH) and does
// not follow the camera: turning, tilting, changing the pose or switching
// the wheels never zooms. The camera then tilts by RsView.pitch about the
// middle of the framing (never below the pitch that keeps the eye above the
// ground), zooms by RsView.zoom on the focal length and pans the picture by
// RsView.panX/Y, as far as the middle of the model stays in the picture.
// Every point keeps at least two thirds of the camera distance at any pitch:
// the distance is three times the radius of everything about the middle.
// The race camera (RsView.race) takes its own distance, height and focal
// length instead (RS_VIEW_RACE_*), with the model's origin in the middle.
static void RsView_Scene(const struct RsView *v, int w, int h, int topH, struct RsViewScene *s)
{
    long long rModel = (long long)RsView_Isqrt((unsigned long long)v->radius2) + 1;
    long long rTire = (long long)RsView_Isqrt((unsigned long long)v->tireRadius2) + 1;
    long long rDummy = (long long)RsView_Isqrt((unsigned long long)v->dumRadius2) + 1;
    long long rMax = 0, halfW, ylo = 0, yhi = 0, ey, ey2, ez2, rScene, dist, dmin, hw, hh, fx, fy, fq;
    long long floorX, floorZ, left = 0, right = 0, sep = 0, lo, hi, gapNeed, centreY;
    const int margin = Rs_Px(12);
    const int ps = RsView_Sin(RS_VIEW_PITCH), pc = RsView_Cos(RS_VIEW_PITCH);
    const int dummy = !v->loaded || v->crashRef;
    int d;

    s->dummy = dummy;
    if (dummy) {
        ylo = v->dumYmin < 0 ? v->dumYmin : 0;
        yhi = v->dumYmax > 0 ? v->dumYmax : 0;
        rMax = rDummy;
        lo = v->dumLo[0];
        hi = v->dumHi[0];
        for (d = 1; d < 360; d++) {
            if (v->dumLo[d] < lo)
                lo = v->dumLo[d];
            if (v->dumHi[d] > hi)
                hi = v->dumHi[d];
        }
        left = RsView_SpanUnitsFloor(lo);
        right = RsView_SpanUnitsCeil(hi);
    }
    if (!v->loaded && v->wheelCount) {
        // The wheel model on the dummy alone (RsView_DrawDummy): within the
        // circle of its framing radius about the dummy's origin.
        const long long rWheel = (long long)RsView_Isqrt((unsigned long long)v->wheelRadius2) + 1;
        if (v->wheelYmin < ylo)
            ylo = v->wheelYmin;
        if (v->wheelYmax > yhi)
            yhi = v->wheelYmax;
        if (rWheel > rMax)
            rMax = rWheel;
        if (-rWheel < left)
            left = -rWheel;
        if (rWheel > right)
            right = rWheel;
    }
    if (v->loaded) {
        // The model's origin is 0 here; the dummy's lies sep to the right.
        if (v->ymin < ylo)
            ylo = v->ymin;
        if (v->ymax > yhi)
            yhi = v->ymax;
        if (v->tireYmin < ylo)
            ylo = v->tireYmin;
        if (v->tireYmax > yhi)
            yhi = v->tireYmax;
        if (v->setCount) {
            const long long rSet = (long long)RsView_Isqrt((unsigned long long)v->setRadius2) + 1;
            if (v->setYmin < ylo)
                ylo = v->setYmin;
            if (v->setYmax > yhi)
                yhi = v->setYmax;
            if (rSet > rModel)
                rModel = rSet;
        }
        if (v->wheelCount) {
            const long long rWheel = (long long)RsView_Isqrt((unsigned long long)v->wheelRadius2) + 1;
            if (v->wheelYmin < ylo)
                ylo = v->wheelYmin;
            if (v->wheelYmax > yhi)
                yhi = v->wheelYmax;
            if (rWheel > rTire)
                rTire = rWheel;
        }
        if (rTire > rModel)
            rModel = rTire;
        if (rModel > rMax)
            rMax = rModel;
        lo = v->modelLo[0];
        hi = v->modelHi[0];
        gapNeed = v->modelHi[0] - v->dumLo[0];
        for (d = 1; d < 360; d++) {
            if (v->modelHi[d] - v->dumLo[d] > gapNeed)
                gapNeed = v->modelHi[d] - v->dumLo[d];
            if (v->modelLo[d] < lo)
                lo = v->modelLo[d];
            if (v->modelHi[d] > hi)
                hi = v->modelHi[d];
        }
        left = RsView_SpanUnitsFloor(lo);
        if (dummy) {
            sep = RsView_SpanUnitsCeil(gapNeed) + RS_VIEW_GAP;
            right += sep;
        } else {
            right = RsView_SpanUnitsCeil(hi);
        }
    }
    s->offModel = -(left + right) * RS_VIEW_SUB / 2;
    s->offDummy = s->offModel + sep * RS_VIEW_SUB;
    halfW = (right - left + 1) / 2 + 1;
    ey = (yhi - ylo + 1) / 2 + 1;

    floorX = halfW + rMax / 16 + 1;
    floorZ = rMax + rMax / 16 + 1;
    s->floorX = floorX * RS_VIEW_SUB;
    s->floorZ = floorZ * RS_VIEW_SUB;
    s->floorY = ylo * RS_VIEW_SUB - 2;   // a hair below the lowest point, so that faces on the ground win

    // Camera distance three times the radius of everything incl. the floor:
    // a mild perspective, and every point keeps at least two thirds of the
    // distance (depth values below 1.5 in Q20).
    rScene = (long long)RsView_Isqrt((unsigned long long)(floorX * floorX + ey * ey + floorZ * floorZ)) + 1;
    dist = 3 * rScene;

    // Bounds after the camera tilt: |y| <= ey cos + r sin, |z| <= ey sin + r cos;
    // the nearest depth is dist - that z. Fit width and height at that depth.
    ey2 = (ey * pc + rMax * ps) / 16384 + 1;
    ez2 = (ey * ps + rMax * pc) / 16384 + 1;
    dmin = dist - ez2;
    hw = w / 2 - margin;
    hh = (h - topH) / 2 - margin;
    if (hw < 1)
        hw = 1;
    if (hh < 1)
        hh = 1;
    fx = hw * RS_VIEW_SUB * dmin / halfW;
    fy = hh * RS_VIEW_SUB * dmin / ey2;
    // The floor is not turned and reaches further than the objects: its four
    // corners (x +-floorX, y about -ey below the centre, z +-floorZ) stay
    // inside the picture as well.
    {
        int side;
        for (side = -1; side <= 1; side += 2) {
            const long long fz = side * floorZ;
            const long long fy2 = (-ey * pc - fz * ps) / 16384;
            const long long fz2 = (-ey * ps + fz * pc) / 16384 + 1;
            const long long d = dist - fz2;
            const long long ay = fy2 < 0 ? -fy2 + 1 : fy2 + 1;
            if (d > 0) {
                if (hw * RS_VIEW_SUB * d / floorX < fx)
                    fx = hw * RS_VIEW_SUB * d / floorX;
                if (hh * RS_VIEW_SUB * d / ay < fy)
                    fy = hh * RS_VIEW_SUB * d / ay;
            }
        }
    }

    s->cam.yawSin = RsView_Sin(v->yaw);
    s->cam.yawCos = RsView_Cos(v->yaw);
    if (v->race) {
        // Turned by half a turn the eye lies at +ez, the point it looks at
        // at -az; the camera turns about the point of that line over the
        // model's origin.
        const long long ez = -RS_VIEW_RACE_EYE_Z, az = RS_VIEW_RACE_AT_Z;
        const long long y0 = RS_VIEW_RACE_EYE_Y + (long long)(RS_VIEW_RACE_AT_Y - RS_VIEW_RACE_EYE_Y) * ez / (ez + az);
        const long long dy = y0 - RS_VIEW_RACE_EYE_Y;
        long long f = (long long)h * 2 / 3;
        if (f > w / 2)
            f = w / 2;
        s->offModel = 0;
        s->offDummy = v->loaded ? sep * RS_VIEW_SUB : 0;
        s->cam.ycq = y0;
        s->cam.dq = (long long)RsView_Isqrt((unsigned long long)(ez * ez + dy * dy));
        fq = f * RS_VIEW_SUB;
        s->baseCx = (long long)w * RS_VIEW_SUB / 2;
        s->baseCy = (long long)h * RS_VIEW_SUB / 2;
    } else {
        s->cam.ycq = (ylo + yhi) * RS_VIEW_SUB / 2;
        s->cam.dq = dist * RS_VIEW_SUB;
        fq = fx < fy ? fx : fy;
        s->baseCx = (long long)w * RS_VIEW_SUB / 2;
        s->baseCy = (long long)(h + topH) * RS_VIEW_SUB / 2;   // the room for text is at the top
    }
    s->cam.dqNear = s->cam.dq / 4 + 1;
    // The eye (the middle raised by dq sin(pitch)) stays above the ground.
    s->pitchMin = RS_VIEW_PITCH_MIN;
    while (s->pitchMin < RS_VIEW_PITCH_MAX && s->cam.ycq * 16384 + s->cam.dq * RsView_Sin(s->pitchMin) <= 0)
        s->pitchMin++;
    s->pitch = v->pitch < s->pitchMin ? s->pitchMin : v->pitch;
    s->cam.pitchSin = RsView_Sin(s->pitch);
    s->cam.pitchCos = RsView_Cos(s->pitch);
    s->cam.fq = fq * v->zoom / 100;
    if (s->cam.fq < 1)
        s->cam.fq = 1;

    // The pan keeps the middle of the model (or of the dummy alone) inside
    // the picture.
    centreY = v->loaded ? (long long)(v->ymin + v->ymax) * RS_VIEW_SUB / 2
                        : (long long)(v->dumYmin + v->dumYmax) * RS_VIEW_SUB / 2;
    s->cam.cxq = s->baseCx;
    s->cam.cyq = s->baseCy;
    {
        struct RsViewVert c;
        long long px = v->panX, py = v->panY;
        RsView_Project(&s->cam, 0, centreY, 0, v->loaded ? s->offModel : s->offDummy, 1, &c);
        if (c.x + px * RS_VIEW_SUB < 0)
            px = -RsView_FloorDiv16(c.x);
        if (c.x + px * RS_VIEW_SUB > (long long)(w - 1) * RS_VIEW_SUB)
            px = RsView_FloorDiv16((long long)(w - 1) * RS_VIEW_SUB - c.x);
        if (c.y + py * RS_VIEW_SUB < 0)
            py = -RsView_FloorDiv16(c.y);
        if (c.y + py * RS_VIEW_SUB > (long long)(h - 1) * RS_VIEW_SUB)
            py = RsView_FloorDiv16((long long)(h - 1) * RS_VIEW_SUB - c.y);
        s->panX = (int)px;
        s->panY = (int)py;
    }
    s->cam.cxq = s->baseCx + (long long)s->panX * RS_VIEW_SUB;
    s->cam.cyq = s->baseCy + (long long)s->panY * RS_VIEW_SUB;
}

// ---------------------------------------------------------------------------
// The dummy's driver
// ---------------------------------------------------------------------------
//
// A figure of boxes on the dummy kart, in tenths of game units (y up, z
// forward, x the driver's left), anchored at the dummy's measured points
// (RS_VIEW_DUMMY_*, rs_view.h): hips on the seat, hands on the steering
// ring, the head up to the top of Crash's box and the elbows out to its
// sides (RsView_SetCrashBox). The shapes in between are plain, not measured.

#define RS_VIEW_DRIVER_NECK_TOP 545     // the head starts here
#define RS_VIEW_DRIVER_HEAD_X 140       // the head: x +-140, z -110..130
#define RS_VIEW_DRIVER_HEAD_Z0 (-110)
#define RS_VIEW_DRIVER_HEAD_Z1 130
#define RS_VIEW_DRIVER_ARM 30           // half thickness of an arm
#define RS_VIEW_DRIVER_HAND 35          // half size of a hand
// A larger box keeps the driver this large: products in RsView_DummyShade
// stay below 2^50.
#define RS_VIEW_DRIVER_MAX 2400

// x0 y0 z0 x1 y1 z1. The first three are centred; the others stand on the
// driver's left (+x) and are mirrored to his right.
#define RS_VIEW_DRIVER_CENTRED 3
static const short s_rsViewDriverBoxes[6][6] = {
    { -90, RS_VIEW_DUMMY_SEAT_Y, -70, 90, 220, 90 },      // hips, on the seat
    { -105, 220, -75, 105, 500, 55 },                     // body
    { -35, 500, -30, 35, RS_VIEW_DRIVER_NECK_TOP, 30 },   // neck
    { 25, 105, 50, 85, 195, 320 },                        // thigh
    { 25, 90, 300, 85, 190, 440 },                        // shin
    { 20, 90, 440, 90, 240, 500 },                        // foot
};
static const short s_rsViewDriverShoulder[3] = { 135, 470, -10 };   // x mirrored
#define RS_VIEW_DRIVER_ELBOW_Y 330
#define RS_VIEW_DRIVER_ELBOW_Z 60

// The faces of a box whose corner k takes x from bit 0, y from bit 1 and z
// from bit 2: counter-clockwise seen from outside (-z, +z, -x, +x, -y, +y).
static const unsigned char s_rsViewBoxQuads[6][4] = {
    { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 },
};

struct RsViewDriver {
    int pos[RS_VIEW_DRIVER_PARTS * 8 * 3];      // 1/16 units
    int tri[RS_VIEW_DRIVER_PARTS * 12 * 3];
    unsigned int rgb[RS_VIEW_DRIVER_PARTS * 12];
    int positions, triangles;
};

// The 12 triangles of the 8 corners just added.
static void RsView_DriverFaces(struct RsViewDriver *d, unsigned int rgb)
{
    const int p = d->positions - 8;
    int f;
    for (f = 0; f < 6; f++) {
        const unsigned char *q = s_rsViewBoxQuads[f];
        int *t = d->tri + 3 * d->triangles;
        t[0] = p + q[0]; t[1] = p + q[1]; t[2] = p + q[2];
        t[3] = p + q[0]; t[4] = p + q[2]; t[5] = p + q[3];
        d->rgb[d->triangles] = rgb;
        d->rgb[d->triangles + 1] = rgb;
        d->triangles += 2;
    }
}

// An upright box from lo to hi, 1/16 units.
static void RsView_DriverBoxSub(struct RsViewDriver *d, const long long *lo, const long long *hi, unsigned int rgb)
{
    int k;
    if (d->positions + 8 > RS_VIEW_DRIVER_PARTS * 8)
        return;
    for (k = 0; k < 8; k++) {
        int *p = d->pos + 3 * (d->positions + k);
        p[0] = (int)((k & 1) ? hi[0] : lo[0]);
        p[1] = (int)((k & 2) ? hi[1] : lo[1]);
        p[2] = (int)((k & 4) ? hi[2] : lo[2]);
    }
    d->positions += 8;
    RsView_DriverFaces(d, rgb);
}

// The same in tenths of game units.
static void RsView_DriverBox(struct RsViewDriver *d, int x0, int y0, int z0, int x1, int y1, int z1, unsigned int rgb)
{
    const long long lo[3] = { RsView_TenthsSub(x0), RsView_TenthsSub(y0), RsView_TenthsSub(z0) };
    const long long hi[3] = { RsView_TenthsSub(x1), RsView_TenthsSub(y1), RsView_TenthsSub(z1) };
    RsView_DriverBoxSub(d, lo, hi, rgb);
}

// A beam of square section (half size half) from a to b, all in 1/16 units.
// Its sides u (level) and v = (b - a) x u make (u, v, b - a) right-handed,
// so the faces of RsView_DriverFaces face outward as for a box.
static void RsView_DriverBeam(struct RsViewDriver *d, const long long *a, const long long *b, long long half,
                              unsigned int rgb)
{
    const long long dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    long long ux = -dz, uz = dx, vx, vy, vz, len;
    int k;

    if (d->positions + 8 > RS_VIEW_DRIVER_PARTS * 8)
        return;
    len = (long long)RsView_Isqrt((unsigned long long)(ux * ux + uz * uz));
    if (len == 0) {
        ux = half;   // straight up or down
        uz = 0;
    } else {
        ux = ux * half / len;
        uz = uz * half / len;
    }
    vx = dy * uz;
    vy = dz * ux - dx * uz;
    vz = -dy * ux;
    len = (long long)RsView_Isqrt((unsigned long long)(vx * vx + vy * vy + vz * vz));
    if (len == 0)
        return;   // no length
    vx = vx * half / len;
    vy = vy * half / len;
    vz = vz * half / len;
    for (k = 0; k < 8; k++) {
        const long long *e = (k & 4) ? b : a;
        const long long su = (k & 1) ? 1 : -1, sv = (k & 2) ? 1 : -1;
        int *p = d->pos + 3 * (d->positions + k);
        p[0] = (int)(e[0] + su * ux + sv * vx);
        p[1] = (int)(e[1] + sv * vy);
        p[2] = (int)(e[2] + su * uz + sv * vz);
    }
    d->positions += 8;
    RsView_DriverFaces(d, rgb);
}

// The top of the driver's head, tenths of game units: the top of Crash's box.
static int RsView_DriverTop(const struct RsView *v)
{
    const int top = (v->hasCrash ? v->crash : s_rsViewCrashDefault)[4];
    if (top < RS_VIEW_DRIVER_NECK_TOP + 100)
        return RS_VIEW_DRIVER_NECK_TOP + 100;   // a box lower than the neck: still a head
    return top > RS_VIEW_DRIVER_MAX ? RS_VIEW_DRIVER_MAX : top;
}

// The driver in a pose (RS_VIEW_POSE_*): only the hands move, with the
// steering wheel (RldDum_Turn: about its column along +z, the top toward +X
// in frame 0); the body stays upright.
static void RsView_DriverMesh(const struct RsView *v, int pose, struct RsViewDriver *d)
{
    const int *box = v->hasCrash ? v->crash : s_rsViewCrashDefault;
    const int turn = pose == RS_VIEW_POSE_FRAME0 ? RS_VIEW_DUMMY_TURN_WHEEL
                   : pose == RS_VIEW_POSE_FRAME20 ? -RS_VIEW_DUMMY_TURN_WHEEL : 0;
    const int ts = RsView_Sin(turn), tc = RsView_Cos(turn);
    const int top = RsView_DriverTop(v);
    int i, side;

    d->positions = 0;
    d->triangles = 0;
    for (i = 0; i < 6; i++) {
        const short *b = s_rsViewDriverBoxes[i];
        RsView_DriverBox(d, b[0], b[1], b[2], b[3], b[4], b[5], RS_VIEW_DRIVER_BODY);
        if (i >= RS_VIEW_DRIVER_CENTRED)
            RsView_DriverBox(d, -b[3], b[1], b[2], -b[0], b[4], b[5], RS_VIEW_DRIVER_BODY);
    }
    RsView_DriverBox(d, -RS_VIEW_DRIVER_HEAD_X, RS_VIEW_DRIVER_NECK_TOP, RS_VIEW_DRIVER_HEAD_Z0,
                     RS_VIEW_DRIVER_HEAD_X, top, RS_VIEW_DRIVER_HEAD_Z1, RS_VIEW_DRIVER_HEAD);

    for (side = 1; side >= -1; side -= 2) {
        // The elbow reaches the side of the box, but never inside the shoulder.
        int ex = side > 0 ? box[3] - RS_VIEW_DRIVER_ARM : -(box[0] + RS_VIEW_DRIVER_ARM);
        const long long g = RsView_TenthsSub(side * RS_VIEW_DUMMY_WHEEL_RADIUS);   // the hands sit on the ring
        long long shoulder[3], elbow[3], hand[3];
        if (ex < s_rsViewDriverShoulder[0])
            ex = s_rsViewDriverShoulder[0];
        if (ex > RS_VIEW_DRIVER_MAX)
            ex = RS_VIEW_DRIVER_MAX;
        shoulder[0] = RsView_TenthsSub(side * s_rsViewDriverShoulder[0]);
        shoulder[1] = RsView_TenthsSub(s_rsViewDriverShoulder[1]);
        shoulder[2] = RsView_TenthsSub(s_rsViewDriverShoulder[2]);
        elbow[0] = RsView_TenthsSub(side * ex);
        elbow[1] = RsView_TenthsSub(RS_VIEW_DRIVER_ELBOW_Y);
        elbow[2] = RsView_TenthsSub(RS_VIEW_DRIVER_ELBOW_Z);
        hand[0] = RsView_TenthsSub(RS_VIEW_DUMMY_WHEEL_X) + g * tc / 16384;
        hand[1] = RsView_TenthsSub(RS_VIEW_DUMMY_WHEEL_Y) - g * ts / 16384;
        hand[2] = RsView_TenthsSub(RS_VIEW_DUMMY_WHEEL_Z);
        RsView_DriverBeam(d, shoulder, elbow, RsView_TenthsSub(RS_VIEW_DRIVER_ARM), RS_VIEW_DRIVER_BODY);
        RsView_DriverBeam(d, elbow, hand, RsView_TenthsSub(RS_VIEW_DRIVER_ARM), RS_VIEW_DRIVER_BODY);
        {
            const long long hh = RsView_TenthsSub(RS_VIEW_DRIVER_HAND);
            const long long lo[3] = { hand[0] - hh, hand[1] - hh, hand[2] - hh };
            const long long hi[3] = { hand[0] + hh, hand[1] + hh, hand[2] + hh };
            RsView_DriverBoxSub(d, lo, hi, RS_VIEW_DRIVER_BODY);
        }
    }
}

// The light on a face of the dummy, percent of its colour: a fixed light from
// above, in front and from the driver's left, in the dummy's coordinates (so
// it turns with it), 7 long: (2, 6, 3). Faces turned away keep 55 %.
static int RsView_DummyShade(const int *a, const int *b, const int *c)
{
    const long long ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const long long wx = c[0] - a[0], wy = c[1] - a[1], wz = c[2] - a[2];
    // The outward normal (the dummy is wound outward); coordinates below 2^12
    // in 1/16 units, so every product here stays below 2^50.
    const long long nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    const long long len = (long long)RsView_Isqrt((unsigned long long)(nx * nx + ny * ny + nz * nz));
    const long long dot = 2 * nx + 6 * ny + 3 * nz;
    if (len == 0)
        return 100;
    if (dot <= 0)
        return 55;
    return 55 + (int)(dot * 45 / (len * 7));
}

// One solid from a mesh: copied, every triangle's colour shaded by
// RsView_DummyShade (as the dummy always was, per face).
static void RsView_SolidSet(struct RsViewSolid *o, const int *pos, int positions, const int *tri,
                            const unsigned int *rgbs, int tris)
{
    int i;
    Rs_Free(o->pos);
    Rs_Free(o->tri);
    Rs_Free(o->rgb);
    o->pos = (int *)Rs_Alloc((size_t)(positions > 0 ? positions : 1) * 3 * sizeof(int));
    o->tri = (int *)Rs_Alloc((size_t)(tris > 0 ? tris : 1) * 3 * sizeof(int));
    o->rgb = (unsigned int *)Rs_Alloc((size_t)(tris > 0 ? tris : 1) * sizeof(unsigned int));
    o->positions = positions;
    o->triangles = 0;
    memcpy(o->pos, pos, (size_t)positions * 3 * sizeof(int));
    for (i = 0; i < tris; i++) {
        const int *t = tri + 3 * i;
        const unsigned int rgb = rgbs[i];
        int shade;
        if (t[0] < 0 || t[0] >= positions || t[1] < 0 || t[1] >= positions || t[2] < 0 || t[2] >= positions)
            break;   // cannot happen; never read past the buffer
        shade = RsView_DummyShade(pos + 3 * t[0], pos + 3 * t[1], pos + 3 * t[2]);
        o->tri[3 * i] = t[0];
        o->tri[3 * i + 1] = t[1];
        o->tri[3 * i + 2] = t[2];
        o->rgb[i] = ((((rgb >> 16) & 0xFF) * (unsigned int)shade / 100) << 16) |
                    ((((rgb >> 8) & 0xFF) * (unsigned int)shade / 100) << 8) | ((rgb & 0xFF) * (unsigned int)shade / 100);
        o->triangles++;
    }
}

static void RsView_SolidsFree(struct RsView *v)
{
    int i;
    if (!v->solids)
        return;
    for (i = 0; i < RS_VIEW_SOLIDS; i++) {
        Rs_Free(v->solids[i].pos);
        Rs_Free(v->solids[i].tri);
        Rs_Free(v->solids[i].rgb);
    }
    Rs_Free(v->solids);
    v->solids = NULL;
    v->solidsValid = 0;
}

// The dummy kart per pose with and without the game's wheels, the driver
// per pose: once, and again for a new Crash box (solidsValid = 0). The same
// numbers as built per picture before, so the same pixels.
static void RsView_SolidsBuild(struct RsView *v)
{
    struct RsViewDriver driver;
    int pose, positions, tris;
    if (v->solidsValid)
        return;
    if (!v->solids)
        v->solids = (struct RsViewSolid *)Rs_Alloc(RS_VIEW_SOLIDS * sizeof(struct RsViewSolid));
    for (pose = 0; pose < RS_VIEW_POSE_COUNT; pose++) {
        positions = 0;
        tris = Rs_DummyMesh(1, pose, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor, RS_VIEW_DUMMY_TRI_MAX,
                            &positions);
        RsView_SolidSet(&v->solids[RS_VIEW_SOLID_KART + pose], v->dumPos, positions, v->dumTri, v->dumColor, tris);
        positions = 0;
        tris = Rs_DummyMesh(0, pose, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor, RS_VIEW_DUMMY_TRI_MAX,
                            &positions);
        RsView_SolidSet(&v->solids[RS_VIEW_SOLID_BARE + pose], v->dumPos, positions, v->dumTri, v->dumColor, tris);
        RsView_DriverMesh(v, pose, &driver);
        RsView_SolidSet(&v->solids[RS_VIEW_SOLID_DRIVER + pose], driver.pos, driver.positions, driver.tri, driver.rgb,
                        driver.triangles);
    }
    v->solidsValid = 1;
}

// ---------------------------------------------------------------------------
// The threads of the view
// ---------------------------------------------------------------------------
//
// A few worker threads (one fewer than the processors, at most
// RS_VIEW_THREADS_MAX in all), started with the first picture and ended
// with the window. RsView_ParallelFor hands out a job in chunks of a range
// and takes part itself; it returns when every chunk is done. Which thread
// does which chunk changes nothing: every job writes only its own part
// (triangles of the list, rows of the picture).

#define RS_VIEW_THREADS_MAX 8
typedef void (*RsViewJobFn)(void *ctx, int first, int end);

struct RsViewWorker {
    struct RsViewPool *pool;
    HANDLE wake;                // auto-reset: a job is there (or quit)
    HANDLE thread;
};

struct RsViewPool {
    int workers;
    struct RsViewWorker worker[RS_VIEW_THREADS_MAX];
    HANDLE done;                // auto-reset: the last worker finished its part
    volatile LONG next;         // the next chunk
    volatile LONG busy;         // workers still in the job
    volatile LONG quit;
    RsViewJobFn fn;
    void *ctx;
    int count, chunk;
};

static void RsView_PoolWork(struct RsViewPool *p)
{
    for (;;) {
        const long long first = (long long)(InterlockedIncrement(&p->next) - 1) * p->chunk;
        if (first >= p->count)
            return;
        p->fn(p->ctx, (int)first, (int)(first + p->chunk < p->count ? first + p->chunk : p->count));
    }
}

static DWORD WINAPI RsView_PoolThread(LPVOID arg)
{
    struct RsViewWorker *w = (struct RsViewWorker *)arg;
    struct RsViewPool *p = w->pool;
    for (;;) {
        WaitForSingleObject(w->wake, INFINITE);
        if (p->quit)
            return 0;
        RsView_PoolWork(p);
        if (InterlockedDecrement(&p->busy) == 0)
            SetEvent(p->done);
    }
}

static struct RsViewPool *RsView_PoolStart(void)
{
    struct RsViewPool *p = (struct RsViewPool *)Rs_Alloc(sizeof(struct RsViewPool));
    SYSTEM_INFO si;
    int want, i;
    GetSystemInfo(&si);
    want = (int)si.dwNumberOfProcessors - 1;
    if (want > RS_VIEW_THREADS_MAX - 1)
        want = RS_VIEW_THREADS_MAX - 1;
    p->done = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (i = 0; i < want && p->done; i++) {
        struct RsViewWorker *w = &p->worker[p->workers];
        w->pool = p;
        w->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        w->thread = w->wake ? CreateThread(NULL, 0, RsView_PoolThread, w, 0, NULL) : NULL;
        if (!w->thread) {
            if (w->wake)
                CloseHandle(w->wake);
            w->wake = NULL;
            break;
        }
        p->workers++;
    }
    return p;
}

static void RsView_PoolStop(struct RsViewPool *p)
{
    int i;
    if (!p)
        return;
    p->quit = 1;
    for (i = 0; i < p->workers; i++)
        SetEvent(p->worker[i].wake);
    for (i = 0; i < p->workers; i++) {
        WaitForSingleObject(p->worker[i].thread, INFINITE);
        CloseHandle(p->worker[i].thread);
        CloseHandle(p->worker[i].wake);
    }
    if (p->done)
        CloseHandle(p->done);
    Rs_Free(p);
}

// fn over 0..count-1 in chunks; p NULL or no worker: here, in one call.
static void RsView_ParallelFor(struct RsViewPool *p, int count, int chunk, RsViewJobFn fn, void *ctx)
{
    int helpers, i;
    if (count <= 0)
        return;
    helpers = p ? (count + chunk - 1) / chunk - 1 : 0;
    if (p && helpers > p->workers)
        helpers = p->workers;
    if (helpers <= 0) {
        fn(ctx, 0, count);
        return;
    }
    p->fn = fn;
    p->ctx = ctx;
    p->count = count;
    p->chunk = chunk;
    p->next = 0;
    p->busy = helpers;
    for (i = 0; i < helpers; i++)
        SetEvent(p->worker[i].wake);   // a full barrier: the job is seen
    RsView_PoolWork(p);
    WaitForSingleObject(p->done, INFINITE);
}

// ---------------------------------------------------------------------------
// The triangles of a picture (THE DRAW LIST)
// ---------------------------------------------------------------------------

// What a part of the list is made of; RsView_GenJob fills tris[first + i].
enum { RS_VIEW_GEN_NATIVE = 0, RS_VIEW_GEN_CLASSIC, RS_VIEW_GEN_WHEELS, RS_VIEW_GEN_SOLID };

struct RsViewGen {
    struct RsView *v;
    const struct RsViewScene *s;
    struct RsViewTri *tris;
    int *rows;                          // 2 per triangle (RsView_TriPrep)
    int first;                          // where the part starts in the list
    int kind;                           // RS_VIEW_GEN_*
    int maskMode;
    long long off;                      // sideways after the turn (model or dummy)
    const struct RsViewPoseData *pose;  // RS_VIEW_GEN_CLASSIC
    const unsigned char *nativePos;     // RS_VIEW_GEN_NATIVE: RsView_NativePos
    const struct RsViewSolid *solid;    // RS_VIEW_GEN_SOLID: its triangles with all corners in lo..hi-1
    int lo, hi;
};

// The native model (rs_tex.h): its material, that material's texture,
// culled (CNET winds counter-clockwise from outside), the corner colour
// COL0 (RGBA) - the material and the texture come in per pixel.
static void RsView_GenNative(const struct RsViewGen *g, int first, int end)
{
    const struct RsTexNative *nm = &g->v->native;
    int tri, c;
    for (tri = first; tri < end; tri++) {
        const int m = RsTex_NativeMaterialOf(nm, tri);
        struct RsViewVert vert[3];
        for (c = 0; c < 3; c++) {
            struct RsTexCorner k;
            const unsigned char *q = g->nativePos + (size_t)RsTex_NativeVertex(nm, tri, c) * RS_TEX_NATIVE_POS3_BYTES;
            RsView_Project(&g->s->cam, RsView_ReadS16(q), RsView_ReadS16(q + 2), RsView_ReadS16(q + 4), g->off, 1, &vert[c]);
            RsTex_NativeCorner(nm, tri, c, &k);
            vert[c].u = k.u;
            vert[c].v = k.v;
            vert[c].r = k.r;
            vert[c].g = k.g;
            vert[c].b = k.b;
            vert[c].a = k.a;
        }
        RsView_TriPrep(&g->tris[g->first + tri], &g->rows[2 * (g->first + tri)], vert, 1, RS_VIEW_TRI_TEX, g->maskMode,
                       RsTex_NativeImage(nm, m), &nm->mat[m]);
    }
}

// The model of the preview file (or a pose of the pose set): Gouraud, pad
// bit 0 = no cull bit in the game, drawn from both sides.
static void RsView_GenClassic(const struct RsViewGen *g, int first, int end)
{
    const struct RsViewPoseData *pose = g->pose;
    int tri, c;
    for (tri = first; tri < end; tri++) {
        const unsigned char *p = pose->tris + (size_t)tri * RS_VIEW_TRI_BYTES;
        struct RsViewVert vert[3];
        for (c = 0; c < 3; c++) {
            const unsigned char *q = p + c * RS_VIEW_CORNER_BYTES;
            RsView_Project(&g->s->cam, (long long)RsView_ReadS16(q) * pose->sub, (long long)RsView_ReadS16(q + 2) * pose->sub,
                           (long long)RsView_ReadS16(q + 4) * pose->sub, g->off, 1, &vert[c]);
            vert[c].r = q[6];
            vert[c].g = q[7];
            vert[c].b = q[8];
        }
        RsView_TriPrep(&g->tris[g->first + tri], &g->rows[2 * (g->first + tri)], vert, !(p[9] & 1), RS_VIEW_TRI_FILL,
                       g->maskMode, NULL, NULL);
    }
}

// The wheel model at the four wheel points (instead of the game's wheels),
// the rear wheel model at the rear pair while there is one; index = the
// front pair's triangles (wheel x count + triangle), then the rear pair's.
// Each corner: spin about +X (rolling
// forward: the top goes to +Z), on the -X side mirrored (x -> -x, corners 1
// and 2 swapped, so the rim faces outwards, a tread runs mirrored and the
// culling stays right - the right wheels of WHLS version 2), scaled, the
// front pair steered about +Y (+ = left: the front edge toward +X), moved
// to its point, raised by the radius it grew (RsView_WheelLift) and moved
// by the offset of its axle (RsView_WheelCentre), moved sideways by off (the model's, or the dummy's while there is no model).
// Textured with its UVs and its material as the native model (rs_tex.h),
// the corner colour 255 as in the game. Unlike the game's wheels the wheels of the model
// meet it by depth, as the game draws them in the native model's item
// (pixel by pixel, renderer wheel concept 4).
static void RsView_GenWheels(const struct RsViewGen *g, int first, int end)
{
    const struct RsView *v = g->v;
    const long long ss = RsView_Sin(v->wheelSpin), sc = RsView_Cos(v->wheelSpin);
    const long long ps = RsView_Sin(v->wheelSteer), pc = RsView_Cos(v->wheelSteer);
    const int frontTris = 2 * v->wheelCount, rearCount = v->rearCount ? v->rearCount : v->wheelCount;
    int at, c;

    for (at = first; at < end; at++) {
        const int w = at < frontTris ? at / v->wheelCount : 2 + (at - frontTris) / rearCount;
        const int i = at < frontTris ? at % v->wheelCount : (at - frontTris) % rearCount;
        const struct RsTexWheel *wm = RsView_WheelOf(v, w);
        const struct RsTexImage *img = wm->textured ? &wm->tex : NULL;
        const int mirror = v->tireAt[w][0] < 0;
        const int front = w < 2;
        long long centre[3], num, den;
        struct RsViewVert vert[3];
        RsView_WheelCentre(v, w, centre);
        RsView_WheelScaleOf(v, w, &num, &den);
        for (c = 0; c < 3; c++) {
            struct RsTexCorner k;
            int xyz[3];
            long long x0, y0, z0, x;
            RsTex_WheelCorner(wm, i, mirror && c ? 3 - c : c, xyz, &k);
            x0 = xyz[0];
            y0 = xyz[1];
            z0 = xyz[2];
            x = mirror ? -x0 : x0;
            long long y = RsView_Q14(y0 * sc - z0 * ss);
            long long z = RsView_Q14(y0 * ss + z0 * sc);
            x = x * num / den;
            y = y * num / den;
            z = z * num / den;
            if (front) {
                const long long xs = RsView_Q14(x * pc + z * ps);
                z = RsView_Q14(z * pc - x * ps);
                x = xs;
            }
            RsView_Project(&g->s->cam, x + centre[0], y + centre[1], z + centre[2], g->off, 1, &vert[c]);
            vert[c].u = k.u;
            vert[c].v = k.v;
            vert[c].r = k.r;
            vert[c].g = k.g;
            vert[c].b = k.b;
            vert[c].a = k.a;
        }
        RsView_TriPrep(&g->tris[g->first + at], &g->rows[2 * (g->first + at)], vert, !RsTex_WheelTwoSided(wm, i),
                       RS_VIEW_TRI_TEX, g->maskMode, img, &wm->mat);
    }
}

// Closed solids of the dummy, shaded per face (RsView_SolidsBuild), without
// culling: the triangles whose corners all lie in the positions lo..hi-1,
// turned and then moved sideways by off; the others are skipped.
static void RsView_GenSolid(const struct RsViewGen *g, int first, int end)
{
    const struct RsViewSolid *o = g->solid;
    int i, k;
    for (i = first; i < end; i++) {
        struct RsViewVert vert[3];
        const unsigned int rgb = o->rgb[i];
        int inside = 1;
        for (k = 0; k < 3; k++) {
            const int at = o->tri[3 * i + k];
            if (at < g->lo || at >= g->hi)
                inside = 0;
        }
        if (!inside) {
            g->tris[g->first + i].kind = RS_VIEW_TRI_SKIP;
            g->rows[2 * (g->first + i)] = 1;
            g->rows[2 * (g->first + i) + 1] = 0;
            continue;
        }
        for (k = 0; k < 3; k++) {
            const int *corner = o->pos + 3 * o->tri[3 * i + k];
            RsView_Project(&g->s->cam, corner[0], corner[1], corner[2], g->off, 1, &vert[k]);
            vert[k].r = (int)((rgb >> 16) & 0xFF);
            vert[k].g = (int)((rgb >> 8) & 0xFF);
            vert[k].b = (int)(rgb & 0xFF);
        }
        RsView_TriPrep(&g->tris[g->first + i], &g->rows[2 * (g->first + i)], vert, 0, RS_VIEW_TRI_FILL, g->maskMode,
                       NULL, NULL);
    }
}

static void RsView_GenJob(void *ctx, int first, int end)
{
    const struct RsViewGen *g = (const struct RsViewGen *)ctx;
    switch (g->kind) {
    case RS_VIEW_GEN_NATIVE:
        RsView_GenNative(g, first, end);
        break;
    case RS_VIEW_GEN_CLASSIC:
        RsView_GenClassic(g, first, end);
        break;
    case RS_VIEW_GEN_WHEELS:
        RsView_GenWheels(g, first, end);
        break;
    default:
        RsView_GenSolid(g, first, end);
        break;
    }
}

// The wheel model is drawn: under the model while the game's wheels are on,
// or on the dummy while there is no model (the wheels can be seen before a
// body is loaded).
static int RsView_WheelsShown(const struct RsView *v)
{
    return v->wheelCount && ((v->loaded && v->wheels) || (!v->loaded && !v->message[0]));
}

// The view's own colours (RsView_SetBackground), the same in both colour
// schemes of the shell: [0] dark, [1] light. Background and floor as DIB
// pixels (0x00RRGGBB), the text and the bar as COLORREF.
static const unsigned int s_rsViewBg[2] = { 0x1E2023u, 0xE4E6E9u };
static const unsigned int s_rsViewFloor[2] = { 0x383C42u, 0xC4C8CEu };
static const COLORREF s_rsViewText[2] = { RGB(160, 166, 174), RGB(84, 90, 98) };
static const COLORREF s_rsViewBarFill[2] = { RGB(44, 48, 54), RGB(247, 248, 250) };
static const COLORREF s_rsViewBarEdge[2] = { RGB(84, 90, 98), RGB(160, 166, 174) };
static const COLORREF s_rsViewBarText[2] = { RGB(214, 218, 224), RGB(40, 44, 50) };
#define RS_VIEW_FLAME_RGB 0xFF8C1Au                     // the turbo flame markers

static void RsView_DrawText(struct RsView *v, const RECT *rc, const wchar_t *text, int font, UINT format)
{
    HGDIOBJ oldFont;
    RECT r = *rc;
    if (!text || !*text)
        return;
    SetBkMode(v->mem, TRANSPARENT);
    SetTextColor(v->mem, s_rsViewText[v->light ? 1 : 0]);
    oldFont = SelectObject(v->mem, Rs_Font(font));
    DrawTextW(v->mem, text, -1, &r, format | DT_NOPREFIX);
    SelectObject(v->mem, oldFont);
}

// Centred, wrapped text in the middle of the view.
static void RsView_CenterText(struct RsView *v, const wchar_t *text)
{
    RECT r = { 0, 0, v->w, v->h }, calc;
    HGDIOBJ oldFont;
    if (!text || !*text)
        return;
    InflateRect(&r, -Rs_Px(16), -Rs_Px(16));
    calc = r;
    oldFont = SelectObject(v->mem, Rs_Font(RS_FONT_BODY));
    DrawTextW(v->mem, text, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
    SelectObject(v->mem, oldFont);
    calc.right = r.right;
    calc.left = r.left;
    OffsetRect(&calc, 0, (r.bottom - r.top - (calc.bottom - calc.top)) / 2);
    RsView_DrawText(v, &calc, text, RS_FONT_BODY, DT_WORDBREAK | DT_CENTER);
}

// A label centred above the highest projected corner of a box given in 1/16
// units, turned and moved sideways by off.
static void RsView_BoxLabel(struct RsView *v, const struct RsViewScene *s, const long long *lo, const long long *hi,
                            long long off, int labelH, const wchar_t *text)
{
    struct RsViewVert c8;
    long long minX = 0, maxX = 0, minY = 0;
    RECT r;
    int k;
    for (k = 0; k < 8; k++) {
        RsView_Project(&s->cam, (k & 1) ? hi[0] : lo[0], (k & 2) ? hi[1] : lo[1], (k & 4) ? hi[2] : lo[2], off, 1,
                       &c8);
        if (k == 0 || c8.x < minX)
            minX = c8.x;
        if (k == 0 || c8.x > maxX)
            maxX = c8.x;
        if (k == 0 || c8.y < minY)
            minY = c8.y;
    }
    r.left = (int)((minX + maxX) / 2 / RS_VIEW_SUB) - Rs_Px(80);
    r.right = r.left + 2 * Rs_Px(80);
    r.bottom = (int)(minY / RS_VIEW_SUB) - Rs_Px(2);
    r.top = r.bottom - labelH;
    RsView_DrawText(v, &r, text, RS_FONT_SMALL, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
}

// THE ANIMATIONS (rs_view.h). The frame set as one number 0..46: the four
// animations one after the other (the order of the CMDL's animation table).
static const unsigned char s_rsViewAnimFirst[RS_VIEW_ANIM_COUNT] = { 0, 21, 28, 43 };
static const unsigned char s_rsViewAnimFrames[RS_VIEW_ANIM_COUNT] = { 21, 7, 15, 4 };
#define RS_VIEW_NEUTRAL_FRAME 10                        // animation 0, frame 10: straight on

static int RsView_GlobalFrame(const struct RsView *v)
{
    return s_rsViewAnimFirst[v->anim] + v->frame;
}

// The pose of a file that shows frame gf (0..46): a file of every frame has
// them in that order (RLDPC1, RLDPN3), a still model one pose; an older one
// only the turn frames 10, 0 and 20 (RS_VIEW_POSE_*): steering frames 0..4
// show frame 0, 16..20 frame 20, every other frame frame 10.
static int RsView_FilePose(int poses, int gf)
{
    if (poses >= RS_VIEW_FRAMES)
        return gf;
    if (poses == 1)
        return 0;   // a still model (RLDPN3 of one pose)
    if (gf <= 4)
        return RS_VIEW_POSE_FRAME0;
    if (gf >= 16 && gf <= 20)
        return RS_VIEW_POSE_FRAME20;
    return RS_VIEW_POSE_NEUTRAL;
}

// The pose of the dummy (its steering wheel has the three of RS_VIEW_POSE_*).
static int RsView_DummyPose(const struct RsView *v)
{
    return RsView_FilePose(RS_VIEW_POSE_COUNT, RsView_GlobalFrame(v));
}

// The triangles drawn for the model: a pose of the pose set while one is
// chosen, else the pose of the preview file for the frame set (the classic
// model has no end pose: the game's fallback stays in its frame).
static const struct RsViewPoseData *RsView_ShownPose(const struct RsView *v)
{
    if (v->setIndex >= 0 && v->setIndex < v->setCount)
        return &v->setPoses[v->setIndex];
    return &v->poses[RsView_FilePose(v->poseCount, RsView_GlobalFrame(v))];
}

// The native model is drawn in place of the model: there is one without a
// blend material, it is not switched off (RsView_SetShowNative) and no pose
// of a pose set is chosen.
static int RsView_NativeUsable(const struct RsView *v)
{
    // A blend material: the game refuses the native set and draws the classic model.
    return v->loaded && v->native.triangles > 0 && !(v->native.flags & RS_TEX_NATIVE_BLEND);
}

static int RsView_DrawsNative(const struct RsView *v)
{
    return RsView_NativeUsable(v) && v->showNative && !(v->setIndex >= 0 && v->setIndex < v->setCount);
}

// The end pose drawn: RS_VIEW_END_WIN or _LOSE while one is set with a
// weight, the native model is drawn and has that pose; else 0.
static int RsView_EndShown(const struct RsView *v)
{
    if (v->endPose < RS_VIEW_END_WIN || v->endPose > RS_VIEW_END_LOSE || v->endWeight <= 0 || !RsView_DrawsNative(v))
        return 0;
    return v->native.end[v->endPose - 1] ? v->endPose : 0;
}

// The positions of the native model drawn (s16 x, y, z per corner, as the
// file has them) and their box: the file's pose of the frame set, or - after
// the finish - neutral + weight x (end pose - neutral), per coordinate,
// rounded half away from zero (rs_view.h, the end poses). The blend is built
// once per end pose and weight, not per picture, on one thread.
static const unsigned char *RsView_NativePos(struct RsView *v, const long long **box)
{
    const int end = RsView_EndShown(v);
    int p;
    if (end) {
        if (v->blendEnd != end || v->blendWeight != v->endWeight) {
            const unsigned char *n = v->native.pos[v->native.neutral], *e = v->native.end[end - 1];
            size_t i;
            if (!v->blendPos)
                v->blendPos = (unsigned char *)Rs_Alloc((size_t)v->native.vertices * RS_TEX_NATIVE_POS3_BYTES);
            for (i = 0; i < (size_t)v->native.vertices * 3; i++) {
                const int a = RsView_ReadS16(n + 2 * i), b = RsView_ReadS16(e + 2 * i);
                const int m = a + (int)RsView_DivRound((long long)(b - a) * v->endWeight, 100);
                v->blendPos[2 * i] = (unsigned char)(m & 0xFF);
                v->blendPos[2 * i + 1] = (unsigned char)((m >> 8) & 0xFF);
            }
            RsView_PosBox(v->blendPos, v->native.vertices, v->blendBox);
            v->blendEnd = end;
            v->blendWeight = v->endWeight;
        }
        if (box)
            *box = v->blendBox;
        return v->blendPos;
    }
    p = RsView_FilePose(v->native.poses, RsView_GlobalFrame(v));
    if (box)
        *box = v->nativeBox[p];
    return v->native.pos[p];
}

// A marker of the look at pixel (x, y): a ring of radius Rs_Px(5) and a
// cross through it, two pixels wide - integers only. flame: a filled
// diamond instead (the turbo flames of the retail kart).
static void RsView_Marker(struct RsView *v, int x, int y, unsigned int rgb, int flame)
{
    const int r = Rs_Px(5), r2o = (r + 1) * (r + 1), r2i = (r - 1) * (r - 1);
    int dx, dy;
    for (dy = -r - 3; dy <= r + 3; dy++)
        for (dx = -r - 3; dx <= r + 3; dx++) {
            const int d2 = dx * dx + dy * dy;
            const int px = x + dx, py = y + dy;
            if (px < 0 || py < 0 || px >= v->w || py >= v->h)
                continue;
            if (flame ? (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= r
                      : ((d2 <= r2o && d2 >= r2i) || ((dx == 0 || dx == 1) && dy >= -r - 3 && dy <= r + 3) ||
                         ((dy == 0 || dy == 1) && dx >= -r - 3 && dx <= r + 3)))
                v->pixels[(size_t)py * (size_t)v->w + (size_t)px] = rgb;
        }
}

// The shadow of the look on the floor: u and v (Q16 over the quad, 0 at
// its first corner, 65536 at the opposite one) perspective correct as in
// RsView_FillTex; the floor's pixels inside get darker by up to
// RS_VIEW_SHADOW_DARK, fading to nothing towards the quad's edges
// (RS_VIEW_SHADOW_SOFT) - subtracted like the game's shadow. Only pixels of
// the floor (depth set; the floor is all that is drawn before it); the
// depth stays the floor's.
static void RsView_FillShadow(const struct RsViewTarget *t, const struct RsViewVert *a, const struct RsViewVert *b,
                              const struct RsViewVert *c)
{
    const long long auz = a->u * a->z, buz = b->u * b->z, cuz = c->u * c->z;
    const long long avz = a->v * a->z, bvz = b->v * b->z, cvz = c->v * c->z;
    struct RsViewEdges e;
    long long px, py;

    if (!RsView_EdgesSet(t, a, b, c, &e))
        return;
    for (py = e.py0; py <= e.py1; py++) {
        long long e0 = e.e0Row, e1 = e.e1Row, e2 = e.e2Row;
        unsigned int *pix = t->pixels + (size_t)py * (size_t)t->w;
        const int *dep = t->depth + (size_t)py * (size_t)t->w;
        for (px = e.px0; px <= e.px1; px++) {
            if (e0 + e.bias0 >= 0 && e1 + e.bias1 >= 0 && e2 + e.bias2 >= 0 && dep[px] > 0) {
                const long long w0 = (e0 << 16) / e.area, w1 = (e1 << 16) / e.area, w2 = (e2 << 16) / e.area;
                const long long zs = w0 * a->z + w1 * b->z + w2 * c->z;
                long long uq = zs > 0 ? (w0 * auz + w1 * buz + w2 * cuz) / zs : a->u;
                long long vq = zs > 0 ? (w0 * avz + w1 * bvz + w2 * cvz) / zs : a->v;
                long long fu, fv;
                unsigned int k, p = pix[px], r, g, bl;
                uq = uq < 32768 ? uq : 65536 - uq;   // to the nearer edge
                vq = vq < 32768 ? vq : 65536 - vq;
                fu = uq <= 0 ? 0 : (uq >= RS_VIEW_SHADOW_SOFT ? 256 : uq * 256 / RS_VIEW_SHADOW_SOFT);
                fv = vq <= 0 ? 0 : (vq >= RS_VIEW_SHADOW_SOFT ? 256 : vq * 256 / RS_VIEW_SHADOW_SOFT);
                k = (unsigned int)(RS_VIEW_SHADOW_DARK * fu * fv / 65536);
                r = (p >> 16) & 0xFF;
                g = (p >> 8) & 0xFF;
                bl = p & 0xFF;
                r = r > k ? r - k : 0;
                g = g > k ? g - k : 0;
                bl = bl > k ? bl - k : 0;
                pix[px] = (r << 16) | (g << 8) | bl;
            }
            e0 += e.e0dx;
            e1 += e.e1dx;
            e2 += e.e2dx;
        }
        e.e0Row += e.e0dy;
        e.e1Row += e.e1dy;
        e.e2Row += e.e2dy;
    }
}

// The rows of one band (RS_VIEW_BAND_ROWS) take part of the list in order.
#define RS_VIEW_BAND_ROWS 32
#define RS_VIEW_GEN_CHUNK 1024

struct RsViewRaster {
    struct RsViewTarget target;
    const struct RsViewTri *tris;
    const int *rows;
    int first, end;             // the part of the list
};

static void RsView_RasterJob(void *ctx, int firstBand, int endBand)
{
    const struct RsViewRaster *r = (const struct RsViewRaster *)ctx;
    struct RsViewTarget t = r->target;
    int band, i;
    for (band = firstBand; band < endBand; band++) {
        t.y0 = band * RS_VIEW_BAND_ROWS;
        t.y1 = t.y0 + RS_VIEW_BAND_ROWS < t.h ? t.y0 + RS_VIEW_BAND_ROWS : t.h;
        for (i = r->first; i < r->end; i++) {
            const struct RsViewTri *o;
            if (r->rows[2 * i] > r->rows[2 * i + 1] || r->rows[2 * i + 1] < t.y0 || r->rows[2 * i] >= t.y1)
                continue;
            o = &r->tris[i];
            t.maskMode = o->maskMode;
            if (o->kind == RS_VIEW_TRI_FILL)
                RsView_Fill(&t, &o->v[0], &o->v[1], &o->v[2]);
            else if (o->kind == RS_VIEW_TRI_TEX)
                RsView_FillTex(&t, &o->v[0], &o->v[1], &o->v[2], o->img, o->level, o->mat);
            else if (o->kind == RS_VIEW_TRI_SHADOW)
                RsView_FillShadow(&t, &o->v[0], &o->v[1], &o->v[2]);
        }
    }
}

// Part first..end-1 of the list into the picture, all bands.
static void RsView_Raster(struct RsView *v, const struct RsViewTarget *t, int first, int end)
{
    struct RsViewRaster r;
    if (end <= first)
        return;
    r.target = *t;
    r.tris = v->tris;
    r.rows = v->triRows;
    r.first = first;
    r.end = end;
    RsView_ParallelFor(v->pool, (t->h + RS_VIEW_BAND_ROWS - 1) / RS_VIEW_BAND_ROWS, 1, RsView_RasterJob, &r);
}

// A part of the list made by RsView_GenJob, the big ones on all threads.
static void RsView_Generate(struct RsView *v, struct RsViewGen *g, int count)
{
    if (count <= 0)
        return;
    g->v = v;
    g->tris = v->tris;
    g->rows = v->triRows;
    RsView_ParallelFor(v->pool, count, RS_VIEW_GEN_CHUNK, RsView_GenJob, g);
}

// Room in the list for count triangles.
static void RsView_TrisReserve(struct RsView *v, int count)
{
    if (count <= v->triCap)
        return;
    Rs_Free(v->tris);
    Rs_Free(v->triRows);
    v->triCap = count + count / 4 + 64;
    v->tris = (struct RsViewTri *)Rs_Alloc((size_t)v->triCap * sizeof(struct RsViewTri));
    v->triRows = (int *)Rs_Alloc((size_t)v->triCap * 2 * sizeof(int));
}

static void RsView_Render(HWND view, struct RsView *v)
{
    struct RsViewTarget t;
    struct RsViewScene s;
    struct RsViewGen g;
    const unsigned int bg = s_rsViewBg[v->light ? 1 : 0];
    const unsigned int floorRgb = s_rsViewFloor[v->light ? 1 : 0];
    const int labelH = Rs_Px(20);
    // Room at the top: the label above the dummy, and the window text while
    // the dummy stands alone.
    const int topH = v->loaded ? labelH : labelH + Rs_Px(36);
    const int native = RsView_DrawsNative(v);
    const int pose = RsView_DummyPose(v);
    const long long *nativeBox = NULL;
    const unsigned char *nativePos = v->loaded && v->native.triangles > 0 ? RsView_NativePos(v, &nativeBox) : NULL;
    const struct RsViewSolid *kart, *driver;
    size_t i, n = (size_t)v->w * (size_t)v->h;
    wchar_t text[RS_VIEW_MESSAGE_CAP];
    int k, count, modelFirst, modelEnd, wheelEnd, wheelCount = 0;
    // The native model (preview feature) is timed: its triangles and the
    // whole picture, into the automation log (D.4 5b measures the time per
    // preview picture). Nothing of it without a native model.
    LARGE_INTEGER renderStart, nativeStart, nativeEnd, wheelStart, wheelEndT, renderEnd, freq;
    int nativeTimed = 0;

    QueryPerformanceCounter(&renderStart);
    nativeStart = renderStart;
    nativeEnd = renderStart;
    GdiFlush();   // GDI must be done with the DIB before its bits are written
    for (i = 0; i < n; i++)
        v->pixels[i] = bg;
    memset(v->depth, 0, n * sizeof(int));   // depth 0 = infinitely far
    memset(v->mask, 0, n);
    v->dirty = 0;
    v->drawnPal = g_rsPal;
    v->drawnScale = Rs_Px(96);
    v->wheelTicks = 0;

    // A message instead of the picture (RsView_Clear, a preview that cannot be read).
    if (!v->loaded && v->message[0]) {
        RsView_CenterText(v, v->message);
        return;
    }
    if (v->w < 8 || v->h < 8)
        return;
    if (!v->pool)
        v->pool = RsView_PoolStart();

    t.pixels = v->pixels;
    t.depth = v->depth;
    t.mask = v->mask;
    t.maskMode = RS_VIEW_MASK_NONE;
    t.w = v->w;
    t.h = v->h;
    t.y0 = 0;
    t.y1 = v->h;
    RsView_Scene(v, v->w, v->h, topH, &s);
    v->pitchMin = s.pitchMin;
    RsView_SolidsBuild(v);
    kart = &v->solids[RS_VIEW_SOLID_KART + pose];
    driver = &v->solids[RS_VIEW_SOLID_DRIVER + pose];
    if (!v->loaded && RsView_WheelsShown(v))
        kart = &v->solids[RS_VIEW_SOLID_BARE + pose];   // the wheel model takes the place of the game's wheels

    // The list: floor, shadow, model, the wheels under it, the dummy.
    count = 4 + (native ? v->native.triangles : (v->loaded ? RsView_ShownPose(v)->count : 0)) + RsView_WheelTris(v) +
            2 * v->solids[RS_VIEW_SOLID_KART + pose].triangles + driver->triangles;
    RsView_TrisReserve(v, count);
    memset(&g, 0, sizeof(g));
    g.s = &s;

    // The floor: two triangles under everything, not turned with it.
    {
        struct RsViewVert q[4], tri[3];
        for (k = 0; k < 4; k++) {
            RsView_Project(&s.cam, (k & 1) ? s.floorX : -s.floorX, s.floorY, (k & 2) ? s.floorZ : -s.floorZ, 0, 0,
                           &q[k]);
            q[k].r = (int)((floorRgb >> 16) & 0xFF);
            q[k].g = (int)((floorRgb >> 8) & 0xFF);
            q[k].b = (int)(floorRgb & 0xFF);
        }
        tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[3];
        RsView_TriPrep(&v->tris[0], &v->triRows[0], tri, 0, RS_VIEW_TRI_FILL, RS_VIEW_MASK_NONE, NULL, NULL);
        tri[0] = q[0]; tri[1] = q[3]; tri[2] = q[2];
        RsView_TriPrep(&v->tris[1], &v->triRows[2], tri, 0, RS_VIEW_TRI_FILL, RS_VIEW_MASK_NONE, NULL, NULL);
    }
    modelFirst = 2;

    // The shadow of the look (RsView_SetLook), unless switched off
    // (RsView_SetOverlays): darker and soft at its edges on the floor under
    // the model, turned with it (both sides); the model's faces on the
    // ground still win.
    if (v->loaded && v->lookShadow && v->showShadow) {
        struct RsViewVert q[4], tri[3];
        for (k = 0; k < 4; k++) {
            RsView_Project(&s.cam, v->lookQuad[(k & 1) ? 1 : 0], s.floorY + 1, v->lookQuad[(k & 2) ? 3 : 2], s.offModel,
                           1, &q[k]);
            q[k].u = (k & 1) ? 65536 : 0;
            q[k].v = (k & 2) ? 65536 : 0;
        }
        tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[3];
        RsView_TriPrep(&v->tris[2], &v->triRows[4], tri, 0, RS_VIEW_TRI_SHADOW, RS_VIEW_MASK_NONE, NULL, NULL);
        tri[0] = q[0]; tri[1] = q[3]; tri[2] = q[2];
        RsView_TriPrep(&v->tris[3], &v->triRows[6], tri, 0, RS_VIEW_TRI_SHADOW, RS_VIEW_MASK_NONE, NULL, NULL);
        modelFirst = 4;
    }

    // The model, in file order; it marks its pixels. The native model
    // (preview feature) in its place while there is one, it is shown
    // (RsView_SetShowNative) and no pose of a pose set is chosen.
    g.first = modelFirst;
    g.maskMode = RS_VIEW_MASK_SET;
    g.off = s.offModel;
    if (native) {
        QueryPerformanceCounter(&nativeStart);
        nativeTimed = 1;
        g.kind = RS_VIEW_GEN_NATIVE;
        g.nativePos = nativePos;
        RsView_Generate(v, &g, v->native.triangles);
        modelEnd = modelFirst + v->native.triangles;
    } else if (v->loaded) {
        g.kind = RS_VIEW_GEN_CLASSIC;
        g.pose = RsView_ShownPose(v);
        RsView_Generate(v, &g, g.pose->count);
        modelEnd = modelFirst + g.pose->count;
    } else {
        modelEnd = modelFirst;
    }
    RsView_Raster(v, &t, 0, modelEnd);
    if (nativeTimed)
        QueryPerformanceCounter(&nativeEnd);

    // The game's wheels under the model (only into pixels the model left
    // free), or the wheel model by depth, as the game draws it (under the
    // model, or on the dummy alone); timed for RsView_WheelBench. The classic
    // look of a model that has a native one is the game's fallback, which
    // keeps the game's wheels: the wheel model goes with the native look only.
    QueryPerformanceCounter(&wheelStart);
    g.first = modelEnd;
    wheelEnd = modelEnd;
    if (v->loaded && v->wheels && v->wheelCount && (native || !RsView_NativeUsable(v))) {
        g.kind = RS_VIEW_GEN_WHEELS;
        g.maskMode = RS_VIEW_MASK_NONE;
        g.off = s.offModel;
        wheelCount = RsView_WheelTris(v);
    } else if (v->loaded && v->wheels && v->tireEnd > v->tireFirst && v->tireEnd <= kart->positions) {
        g.kind = RS_VIEW_GEN_SOLID;
        g.maskMode = RS_VIEW_MASK_SKIP;
        g.off = s.offModel;
        g.solid = kart;
        g.lo = v->tireFirst;
        g.hi = v->tireEnd;
        wheelCount = kart->triangles;
    } else if (!v->loaded && s.dummy && RsView_WheelsShown(v)) {
        g.kind = RS_VIEW_GEN_WHEELS;
        g.maskMode = RS_VIEW_MASK_NONE;
        g.off = s.offDummy;
        wheelCount = RsView_WheelTris(v);
    }
    RsView_Generate(v, &g, wheelCount);
    wheelEnd += wheelCount;
    RsView_Raster(v, &t, modelEnd, wheelEnd);
    QueryPerformanceCounter(&wheelEndT);
    if (v->wheelCount)
        v->wheelTicks = wheelEndT.QuadPart - wheelStart.QuadPart;

    // The dummy beside it: kart and driver in the pose of the view.
    if (s.dummy) {
        g.kind = RS_VIEW_GEN_SOLID;
        g.maskMode = RS_VIEW_MASK_NONE;
        g.off = s.offDummy;
        g.first = wheelEnd;
        g.solid = kart;
        g.lo = 0;
        g.hi = kart->positions;
        RsView_Generate(v, &g, kart->triangles);
        g.first = wheelEnd + kart->triangles;
        g.solid = driver;
        g.hi = driver->positions;
        RsView_Generate(v, &g, driver->triangles);
        RsView_Raster(v, &t, wheelEnd, wheelEnd + kart->triangles + driver->triangles);
    }

    // The exhaust points of the look, always on top, unless switched off
    // (RsView_SetOverlays): a ring with a cross, point 1 in the accent
    // colour, point 2 in the note colour; the retail smoke points grey, and
    // with them the retail turbo flames (game/Vehicle/VehTurbo.c), which the
    // game draws a little lower and further forward. With custom points the
    // game puts smoke and flames on them.
    for (k = 0; v->loaded && v->showExhaust && k < v->lookCount && k < 2; k++) {
        struct RsViewVert c;
        const unsigned int rgb = v->lookGrey ? RsView_Pixel(s_rsViewText[v->light ? 1 : 0])
                                             : RsView_Pixel(k == 0 ? RS_COL_ACCENT : RS_COL_NOTE);
        if (v->lookGrey) {
            RsView_Project(&s.cam, v->lookPoint[k][0] < 0 ? -RS_VIEW_FLAME_X : RS_VIEW_FLAME_X, RS_VIEW_FLAME_Y,
                           RS_VIEW_FLAME_Z, s.offModel, 1, &c);
            RsView_Marker(v, (int)(c.x >> 4), (int)(c.y >> 4), RS_VIEW_FLAME_RGB, 1);
        }
        RsView_Project(&s.cam, v->lookPoint[k][0], v->lookPoint[k][1], v->lookPoint[k][2], s.offModel, 1, &c);
        RsView_Marker(v, (int)(c.x >> 4), (int)(c.y >> 4), rgb, 0);
    }

    GdiFlush();   // the bits are done; GDI writes the text on top
    if (s.dummy) {
        // Above the dummy driver's head.
        const long long lo[3] = { RsView_TenthsSub(-RS_VIEW_DRIVER_HEAD_X), RsView_TenthsSub(RS_VIEW_DRIVER_NECK_TOP),
                                  RsView_TenthsSub(RS_VIEW_DRIVER_HEAD_Z0) };
        const long long hi[3] = { RsView_TenthsSub(RS_VIEW_DRIVER_HEAD_X), RsView_TenthsSub(RsView_DriverTop(v)),
                                  RsView_TenthsSub(RS_VIEW_DRIVER_HEAD_Z1) };
        RsView_BoxLabel(v, &s, lo, hi, s.offDummy, labelH, L"Crash size");
    }
    if (v->loaded && v->native.triangles > 0 && !(v->setIndex >= 0 && v->setIndex < v->setCount)) {
        // Above the model: its native box in the pose shown (RsView_Parse),
        // "Native model" or, switched to the classic one, "Classic model"; a
        // blend material: the game draws the classic one, and so does the view.
        RsView_BoxLabel(v, &s, nativeBox, nativeBox + 3, s.offModel, labelH,
                        native ? L"Native model"
                               : ((v->native.flags & RS_TEX_NATIVE_BLEND) ? L"Classic model (blend material)" : L"Classic model"));
    }
    if (!v->loaded) {
        // The dummy alone: the window text above it.
        RECT r = { 0, 0, v->w, Rs_Px(28) };
        InflateRect(&r, -Rs_Px(16), 0);
        OffsetRect(&r, 0, Rs_Px(8));
        GetWindowTextW(view, text, RS_VIEW_MESSAGE_CAP);
        text[RS_VIEW_MESSAGE_CAP - 1] = 0;
        RsView_DrawText(v, &r, text, RS_FONT_BODY, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (!native && RsView_ShownPose(v)->count == 0) {
        RsView_CenterText(v, L"This pose has no triangles.");
    }
    if (nativeTimed && Rs_Automating() && !v->benching) {
        QueryPerformanceCounter(&renderEnd);
        QueryPerformanceFrequency(&freq);
        Rs_AutoLog(L"  view: native model %d triangles, frame %d, %d x %d pixels: triangles %lld us, whole picture %lld us",
                   v->native.triangles, RsView_GlobalFrame(v), v->w, v->h,
                   (long long)((nativeEnd.QuadPart - nativeStart.QuadPart) * 1000000 / freq.QuadPart),
                   (long long)((renderEnd.QuadPart - renderStart.QuadPart) * 1000000 / freq.QuadPart));
    }
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

static void RsView_FreeFrame(struct RsView *v)
{
    if (v->mem) {
        SelectObject(v->mem, v->oldBmp);
        DeleteDC(v->mem);
    }
    if (v->dib)
        DeleteObject(v->dib);
    Rs_Free(v->depth);
    Rs_Free(v->mask);
    v->mem = NULL;
    v->dib = NULL;
    v->oldBmp = NULL;
    v->pixels = NULL;
    v->depth = NULL;
    v->mask = NULL;
    v->w = 0;
    v->h = 0;
}

static int RsView_EnsureFrame(struct RsView *v, int w, int h)
{
    BITMAPINFO bi;
    void *bits = NULL;

    if (v->dib && v->w == w && v->h == h)
        return 1;
    RsView_FreeFrame(v);
    if (w <= 0 || h <= 0)
        return 0;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down: row 0 is the top row
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    v->dib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!v->dib || !bits) {
        if (v->dib)
            DeleteObject(v->dib);
        v->dib = NULL;
        return 0;
    }
    v->mem = CreateCompatibleDC(NULL);
    if (!v->mem) {
        DeleteObject(v->dib);
        v->dib = NULL;
        return 0;
    }
    v->oldBmp = SelectObject(v->mem, v->dib);
    v->pixels = (unsigned int *)bits;
    v->depth = (int *)Rs_Alloc((size_t)w * (size_t)h * sizeof(int));
    v->mask = (unsigned char *)Rs_Alloc((size_t)w * (size_t)h);
    v->w = w;
    v->h = h;
    v->dirty = 1;
    return 1;
}

// Adds points in 1/16 units to a radius (whole units, squared, outwards) and
// a height range (whole units, outwards).
static void RsView_Extent(const int *pos, int from, int to, long long *r2, int *ymin, int *ymax)
{
    int i;
    for (i = from; i < to; i++) {
        const long long x = pos[3 * i], y = pos[3 * i + 1], z = pos[3 * i + 2];
        const long long ax = (x < 0 ? -x : x) / RS_VIEW_SUB + 1, az = (z < 0 ? -z : z) / RS_VIEW_SUB + 1;
        const int ylo = (int)RsView_FloorDiv16(y), yhi = (int)-RsView_FloorDiv16(-y);
        if (ax * ax + az * az > *r2)
            *r2 = ax * ax + az * az;
        if (ylo < *ymin)
            *ymin = ylo;
        if (yhi > *ymax)
            *ymax = yhi;
    }
}

// The game's wheels in the dummy mesh: the positions the mesh with wheels has
// more than the one without. Both are the same up to the first wheel position
// and again after the last (RldDum_Mesh adds the wheels as one block); if the
// two do not split like that, tireFirst = tireEnd = 0 and no wheels are drawn
// under the model.
static void RsView_FindTires(struct RsView *v)
{
    int *bare = (int *)Rs_Alloc((size_t)RS_VIEW_DUMMY_POS_MAX * 3 * sizeof(int));
    int with = 0, without = 0, first = 0, added, i;

    v->tireFirst = 0;
    v->tireEnd = 0;
    if (Rs_DummyMesh(0, RS_VIEW_POSE_NEUTRAL, bare, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor,
                     RS_VIEW_DUMMY_TRI_MAX, &without) &&
        Rs_DummyMesh(1, RS_VIEW_POSE_NEUTRAL, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor,
                     RS_VIEW_DUMMY_TRI_MAX, &with) &&
        with > without) {
        added = with - without;
        while (first < without && memcmp(bare + 3 * first, v->dumPos + 3 * first, 3 * sizeof(int)) == 0)
            first++;
        for (i = first; i < without; i++)
            if (memcmp(bare + 3 * i, v->dumPos + 3 * (i + added), 3 * sizeof(int)) != 0)
                break;
        if (i == without) {
            v->tireFirst = first;
            v->tireEnd = first + added;
        }
    }
    Rs_Free(bare);
}

// The extents of the dummy (kart with its wheels and driver, all poses) and
// of the game's wheels under the model, for a framing that does not change
// with the pose or the wheels. Again whenever the driver changes (the Crash
// box).
static void RsView_DummyExtent(struct RsView *v)
{
    struct RsViewTrig tr;
    struct RsViewDriver driver;
    long long r2 = 0;
    int ymin = 0, ymax = 0, pose, i, positions;

    RsView_TrigFill(&tr);
    RsView_SpanReset(v->dumLo, v->dumHi);
    RsView_SpanAdd(&tr, v->dumLo, v->dumHi, 0, 0);   // its origin, also when the mesh fails
    for (pose = 0; pose < RS_VIEW_POSE_COUNT; pose++) {
        if (Rs_DummyMesh(1, pose, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor, RS_VIEW_DUMMY_TRI_MAX,
                         &positions)) {
            RsView_Extent(v->dumPos, 0, positions, &r2, &ymin, &ymax);
            for (i = 0; i < positions; i++)
                RsView_SpanAdd(&tr, v->dumLo, v->dumHi, v->dumPos[3 * i], v->dumPos[3 * i + 2]);
        }
        RsView_DriverMesh(v, pose, &driver);
        RsView_Extent(driver.pos, 0, driver.positions, &r2, &ymin, &ymax);
        for (i = 0; i < driver.positions; i++)
            RsView_SpanAdd(&tr, v->dumLo, v->dumHi, driver.pos[3 * i], driver.pos[3 * i + 2]);
    }
    v->dumRadius2 = r2;
    v->dumYmin = ymin;
    v->dumYmax = ymax;

    // The wheels do not move with the pose.
    r2 = 0;
    ymin = 0;
    ymax = 0;
    if (v->tireEnd > v->tireFirst &&
        Rs_DummyMesh(1, RS_VIEW_POSE_NEUTRAL, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor,
                     RS_VIEW_DUMMY_TRI_MAX, &positions) &&
        v->tireEnd <= positions)
        RsView_Extent(v->dumPos, v->tireFirst, v->tireEnd, &r2, &ymin, &ymax);
    v->tireRadius2 = r2;
    v->tireYmin = ymin;
    v->tireYmax = ymax;
}

// Something to turn: a model, or the dummy alone (no message instead of it).
static int RsView_Turnable(const struct RsView *v)
{
    return v->loaded || !v->message[0];
}

// WM_COMMAND with code to the parent.
static void RsView_Notify(HWND view, int code)
{
    HWND parent = GetParent(view);
    if (parent)
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(view), code), (LPARAM)view);
}

static void RsView_SetYawFrom(HWND view, struct RsView *v, int degrees, int notify)
{
    const int yaw = RsView_NormDeg(degrees);
    if (yaw == v->yaw)
        return;
    v->yaw = yaw;
    v->preset = -1;
    RsView_Changed(view, v);
    if (notify) {
        RsView_Notify(view, RS_VIEW_N_YAW);
        RsView_Notify(view, RS_VIEW_N_CAMERA);
    }
}

// The room RsView_Render keeps at the top (the same for the pick and the camera).
static int RsView_TopRoom(const struct RsView *v)
{
    const int labelH = Rs_Px(20);
    return v->loaded ? labelH : labelH + Rs_Px(36);
}

// The camera as drawn: pitch not below the eye's limit, the pan as far as
// RsView_Scene lets it go - written back, so that RsView_GetCamera and the
// next drag start from what is seen. Needs the size of the picture.
static void RsView_ClampCamera(struct RsView *v)
{
    struct RsViewScene s;
    if (v->w < 8 || v->h < 8)
        return;
    RsView_Scene(v, v->w, v->h, RsView_TopRoom(v), &s);
    v->pitchMin = s.pitchMin;
    v->pitch = s.pitch;
    v->panX = s.panX;
    v->panY = s.panY;
}

// A change of the camera by the user: drawn again, the parent told
// (RS_VIEW_N_YAW too when the yaw changed).
static void RsView_UserCamera(HWND view, struct RsView *v, int oldYaw)
{
    RsView_ClampCamera(v);
    RsView_Changed(view, v);
    if (v->yaw != oldYaw)
        RsView_Notify(view, RS_VIEW_N_YAW);
    RsView_Notify(view, RS_VIEW_N_CAMERA);
}

// The zoom steps of the wheel: x 1.25 per notch from 100 %, rounded, between
// the limits - fixed numbers, so the same notches give the same zoom.
static const short s_rsViewZoomSteps[] = { 50, 64, 80, 100, 125, 156, 195, 244, 305, 381, 477, 596, 745, 800 };
#define RS_VIEW_ZOOM_STEPS ((int)(sizeof(s_rsViewZoomSteps) / sizeof(s_rsViewZoomSteps[0])))

static int RsView_ZoomStep(int zoom, int in)
{
    int i;
    if (in) {
        for (i = 0; i < RS_VIEW_ZOOM_STEPS; i++)
            if (s_rsViewZoomSteps[i] > zoom)
                return s_rsViewZoomSteps[i];
        return RS_VIEW_ZOOM_MAX;
    }
    for (i = RS_VIEW_ZOOM_STEPS - 1; i >= 0; i--)
        if (s_rsViewZoomSteps[i] < zoom)
            return s_rsViewZoomSteps[i];
    return RS_VIEW_ZOOM_MIN;
}

// A new zoom about the anchor (qx, qy), 1/16 pixels of the picture: the
// point there stays there - the screen centre c moves to
// q - (q - c) x new / old, so the pan grows and shrinks with the zoom (all
// integers; the pan is then limited by RsView_Scene as always).
static void RsView_ZoomAt(struct RsView *v, int zoom, long long qx, long long qy)
{
    struct RsViewScene s;
    if (zoom < RS_VIEW_ZOOM_MIN)
        zoom = RS_VIEW_ZOOM_MIN;
    if (zoom > RS_VIEW_ZOOM_MAX)
        zoom = RS_VIEW_ZOOM_MAX;
    if (zoom == v->zoom)
        return;
    if (v->w >= 8 && v->h >= 8) {
        long long cx, cy;
        RsView_Scene(v, v->w, v->h, RsView_TopRoom(v), &s);
        cx = qx - (qx - s.cam.cxq) * zoom / v->zoom;
        cy = qy - (qy - s.cam.cyq) * zoom / v->zoom;
        v->panX = (int)RsView_FloorDiv16(cx - s.baseCx + 8);
        v->panY = (int)RsView_FloorDiv16(cy - s.baseCy + 8);
    }
    v->zoom = zoom;
    v->preset = -1;
}

// The anchor of a zoom that names no point (RsView_SetCamera without a new
// pan): where the middle of the model is in the picture now, so that the
// model stays where it is; without a model the middle of the picture.
static void RsView_ModelAnchor(struct RsView *v, long long *qx, long long *qy)
{
    struct RsViewScene s;
    struct RsViewVert c;
    *qx = (long long)v->w * RS_VIEW_SUB / 2;
    *qy = (long long)v->h * RS_VIEW_SUB / 2;
    if (!v->loaded || v->w < 8 || v->h < 8)
        return;
    RsView_Scene(v, v->w, v->h, RsView_TopRoom(v), &s);
    RsView_Project(&s.cam, 0, (long long)(v->ymin + v->ymax) * RS_VIEW_SUB / 2, 0, s.offModel, 1, &c);
    if (c.x >= 0 && c.x < (long long)v->w * RS_VIEW_SUB && c.y >= 0 && c.y < (long long)v->h * RS_VIEW_SUB) {
        *qx = c.x;
        *qy = c.y;
    }
}

// ---------------------------------------------------------------------------
// The bar and the menu of the view
// ---------------------------------------------------------------------------
//
// A row of small buttons along the bottom of the picture, drawn by the view
// over the picture (GDI, after the picture is copied - the picture itself
// is not drawn again for a hovered button): the fixed views and Reset from
// the left, as many as fit, and "View" at the right with the menu of
// everything (the same menu as a right click). Nothing of the page's layout
// changes.

static const wchar_t *const s_rsViewBarTexts[RS_VIEW_BAR_BUTTONS] = {
    L"Front", L"Side", L"Back", L"Top", L"3/4", L"Race", L"Reset", L"View",
};
static const wchar_t *const s_rsViewPresetTexts[RS_VIEW_PRESET_COUNT] = {
    L"Front", L"Side", L"Back", L"Top", L"3/4 (start view)", L"Race camera",
};
enum {
    RS_VIEW_CMD_PRESET = 100,   // + RS_VIEW_PRESET_*
    RS_VIEW_CMD_RESET = 110,
    RS_VIEW_CMD_DARK = 120,
    RS_VIEW_CMD_LIGHT,
    RS_VIEW_CMD_CRASH = 130,
    RS_VIEW_CMD_SHADOW,
    RS_VIEW_CMD_EXHAUST,
    RS_VIEW_CMD_NATIVE = 140,
    RS_VIEW_CMD_CLASSIC,
};

// Lays the buttons out for the picture's size (font of the scale); none
// while there is nothing to turn or no room for "View".
static void RsView_BarLayout(struct RsView *v)
{
    const int pad = Rs_Px(7), gap = Rs_Px(3), height = Rs_Px(20), edge = Rs_Px(6);
    int widths[RS_VIEW_BAR_BUTTONS], i, x, right, top, bottom;
    HGDIOBJ oldFont;

    for (i = 0; i < RS_VIEW_BAR_BUTTONS; i++)
        v->barShown[i] = 0;
    if (!v->mem || !RsView_Turnable(v))
        return;
    oldFont = SelectObject(v->mem, Rs_Font(RS_FONT_SMALL));
    for (i = 0; i < RS_VIEW_BAR_BUTTONS; i++) {
        SIZE size;
        size.cx = 0;
        GetTextExtentPoint32W(v->mem, s_rsViewBarTexts[i], (int)wcslen(s_rsViewBarTexts[i]), &size);
        widths[i] = size.cx + 2 * pad;
    }
    SelectObject(v->mem, oldFont);
    bottom = v->h - edge;
    top = bottom - height;
    if (top < edge || v->w < widths[RS_VIEW_BAR_VIEW] + 2 * edge)
        return;
    SetRect(&v->barRect[RS_VIEW_BAR_VIEW], v->w - edge - widths[RS_VIEW_BAR_VIEW], top, v->w - edge, bottom);
    v->barShown[RS_VIEW_BAR_VIEW] = 1;
    right = v->barRect[RS_VIEW_BAR_VIEW].left - 2 * gap;
    x = edge;
    for (i = 0; i < RS_VIEW_BAR_VIEW; i++) {
        if (x + widths[i] > right)
            break;
        SetRect(&v->barRect[i], x, top, x + widths[i], bottom);
        v->barShown[i] = 1;
        x += widths[i] + gap;
    }
}

// The button under client point (x, y), -1 none.
static int RsView_BarHit(const struct RsView *v, int x, int y)
{
    int i;
    for (i = 0; i < RS_VIEW_BAR_BUTTONS; i++) {
        const RECT *r = &v->barRect[i];
        if (v->barShown[i] && x >= r->left && x < r->right && y >= r->top && y < r->bottom)
            return i;
    }
    return -1;
}

static void RsView_BarDraw(struct RsView *v, HDC dc)
{
    const int k = v->light ? 1 : 0;
    HBRUSH fill = CreateSolidBrush(s_rsViewBarFill[k]);
    HBRUSH edge = CreateSolidBrush(s_rsViewBarEdge[k]);
    HBRUSH accent = CreateSolidBrush(RS_COL_ACCENT);
    HGDIOBJ oldFont = SelectObject(dc, Rs_Font(RS_FONT_SMALL));
    int i;
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < RS_VIEW_BAR_BUTTONS; i++) {
        RECT r = v->barRect[i];
        const int on = i < RS_VIEW_PRESET_COUNT && v->preset == i;
        if (!v->barShown[i])
            continue;
        FillRect(dc, &r, fill);
        FrameRect(dc, &r, (on || v->barHot == i || v->barDown == i) ? accent : edge);
        SetTextColor(dc, on ? RS_COL_ACCENT : s_rsViewBarText[k]);
        DrawTextW(dc, s_rsViewBarTexts[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
    DeleteObject(fill);
    DeleteObject(edge);
    DeleteObject(accent);
}

static void RsView_PaintTo(HWND view, struct RsView *v, HDC dc)
{
    RECT rc;
    GetClientRect(view, &rc);
    if (v && RsView_EnsureFrame(v, rc.right, rc.bottom)) {
        int saved, i;
        // Drawn again only when something changed - also the colour scheme
        // or the scale, which the shell switches without telling the
        // controls (a new dpi may keep the size in pixels).
        if (v->dirty || v->drawnPal != g_rsPal || v->drawnScale != Rs_Px(96))
            RsView_Render(view, v);
        RsView_BarLayout(v);
        saved = SaveDC(dc);
        for (i = 0; i < RS_VIEW_BAR_BUTTONS; i++)
            if (v->barShown[i])
                ExcludeClipRect(dc, v->barRect[i].left, v->barRect[i].top, v->barRect[i].right, v->barRect[i].bottom);
        BitBlt(dc, 0, 0, v->w, v->h, v->mem, 0, 0, SRCCOPY);
        RestoreDC(dc, saved);
        RsView_BarDraw(v, dc);
    } else if (rc.right > 0 && rc.bottom > 0) {
        HBRUSH br = CreateSolidBrush(RS_COL_PAGE);
        FillRect(dc, &rc, br);
        DeleteObject(br);
    }
}

static void RsView_BarInvalidate(HWND view, struct RsView *v, int i)
{
    if (i >= 0 && i < RS_VIEW_BAR_BUTTONS && v->barShown[i])
        InvalidateRect(view, &v->barRect[i], FALSE);
}

// A fixed view chosen by the user (bar, menu).
static void RsView_UserPreset(HWND view, struct RsView *v, int preset)
{
    const int oldYaw = v->yaw;
    RsView_SetPreset(view, preset);
    RsView_UserCamera(view, v, oldYaw);
}

// The menu of the view at client point (x, y); up: it opens upwards (from
// the bar). Every display toggle and the camera; the choice is applied here.
static void RsView_Menu(HWND view, struct RsView *v, int x, int y, int up)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    int i, cmd;
    if (!m)
        return;
    for (i = 0; i < RS_VIEW_PRESET_COUNT; i++)
        AppendMenuW(m, MF_STRING | (v->preset == i ? MF_CHECKED : 0), RS_VIEW_CMD_PRESET + i, s_rsViewPresetTexts[i]);
    AppendMenuW(m, MF_STRING, RS_VIEW_CMD_RESET, L"Reset view\tDouble-click");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (v->light ? 0 : MF_CHECKED), RS_VIEW_CMD_DARK, L"Background: dark");
    AppendMenuW(m, MF_STRING | (v->light ? MF_CHECKED : 0), RS_VIEW_CMD_LIGHT, L"Background: light");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (v->crashRef ? MF_CHECKED : 0), RS_VIEW_CMD_CRASH, L"Crash size reference");
    AppendMenuW(m, MF_STRING | (v->showShadow ? MF_CHECKED : 0), RS_VIEW_CMD_SHADOW, L"Shadow");
    AppendMenuW(m, MF_STRING | (v->showExhaust ? MF_CHECKED : 0), RS_VIEW_CMD_EXHAUST, L"Exhaust");
    if (RsView_NativeUsable(v)) {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING | (v->showNative ? MF_CHECKED : 0), RS_VIEW_CMD_NATIVE, L"Model: native");
        AppendMenuW(m, MF_STRING | (v->showNative ? 0 : MF_CHECKED), RS_VIEW_CMD_CLASSIC, L"Model: classic");
    }
    pt.x = x;
    pt.y = y;
    ClientToScreen(view, &pt);
    cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | (up ? TPM_BOTTOMALIGN : TPM_TOPALIGN),
                              pt.x, pt.y, 0, view, NULL);
    DestroyMenu(m);
    if (cmd >= RS_VIEW_CMD_PRESET && cmd < RS_VIEW_CMD_PRESET + RS_VIEW_PRESET_COUNT) {
        RsView_UserPreset(view, v, cmd - RS_VIEW_CMD_PRESET);
        return;
    }
    switch (cmd) {
    case RS_VIEW_CMD_RESET:
        RsView_UserPreset(view, v, RS_VIEW_PRESET_THREE_QUARTER);
        return;
    case RS_VIEW_CMD_DARK:
    case RS_VIEW_CMD_LIGHT:
        RsView_SetBackground(view, cmd == RS_VIEW_CMD_LIGHT);
        break;
    case RS_VIEW_CMD_CRASH:
        RsView_SetCrashRef(view, !v->crashRef);
        break;
    case RS_VIEW_CMD_SHADOW:
        RsView_SetOverlays(view, !v->showShadow, v->showExhaust);
        break;
    case RS_VIEW_CMD_EXHAUST:
        RsView_SetOverlays(view, v->showShadow, !v->showExhaust);
        break;
    case RS_VIEW_CMD_NATIVE:
    case RS_VIEW_CMD_CLASSIC:
        RsView_SetShowNative(view, cmd == RS_VIEW_CMD_NATIVE);
        break;
    default:
        return;   // nothing chosen
    }
    RsView_Notify(view, RS_VIEW_N_TOGGLES);
}

// THE PICK of the look: the point of the model's surface under pixel (px,
// py) of the last picture, in model units (1/16), the pose shown. The ray
// of the pixel's centre through the inverse of RsView_Project, against every
// triangle of the model drawn (the native one when it is drawn), the nearest
// hit. 1 = a hit (out set), 0 = the pixel shows no model. Doubles only here:
// it picks a point, it draws nothing.
static int RsView_PickRay(struct RsView *v, int px, int py, int out[3])
{
    struct RsViewScene s;
    const int topH = RsView_TopRoom(v);
    const struct RsViewCam *c;
    double o[3], dir[3], best = -1.0;
    int k, tri, found = 0;

    if (!v->loaded || v->w < 8 || v->h < 8 || px < 0 || py < 0 || px >= v->w || py >= v->h)
        return 0;
    RsView_Scene(v, v->w, v->h, topH, &s);
    c = &s.cam;
    // P(d) for the camera depth d = 1 and d = 2: origin and direction (linear in d).
    for (k = 0; k < 2; k++) {
        const double d = (double)(k + 1);
        const double a = ((double)px * RS_VIEW_SUB + 8.0 - (double)c->cxq) / (double)c->fq;
        const double b = ((double)c->cyq - ((double)py * RS_VIEW_SUB + 8.0)) / (double)c->fq;
        const double x1 = a * d, y2 = b * d, z2 = (double)c->dq - d;
        const double y = (y2 * c->pitchCos + z2 * c->pitchSin) / 16384.0;
        const double z1 = (-y2 * c->pitchSin + z2 * c->pitchCos) / 16384.0;
        const double xr = x1 - (double)s.offModel;
        const double p[3] = { (xr * c->yawCos - z1 * c->yawSin) / 16384.0, y + (double)c->ycq, (xr * c->yawSin + z1 * c->yawCos) / 16384.0 };
        int a3;
        for (a3 = 0; a3 < 3; a3++) {
            if (k == 0)
                o[a3] = p[a3];
            else
                dir[a3] = p[a3] - o[a3];
        }
    }
    for (k = 0; k < 3; k++)
        o[k] -= dir[k];   // d = 0: the camera
    {
        const int native = RsView_DrawsNative(v);
        const unsigned char *nativePos = native ? RsView_NativePos(v, NULL) : NULL;
        const struct RsViewPoseData *pd = RsView_ShownPose(v);
        const int count = native ? v->native.triangles : pd->count;
        for (tri = 0; tri < count; tri++) {
            double q[3][3], e1[3], e2[3], h[3], sv[3], qv[3], det, f, u, w, t;
            int i;
            for (i = 0; i < 3; i++) {
                const unsigned char *p = native ? nativePos + (size_t)RsTex_NativeVertex(&v->native, tri, i) * RS_TEX_NATIVE_POS3_BYTES
                                                : pd->tris + (size_t)tri * RS_VIEW_TRI_BYTES + (size_t)i * RS_VIEW_CORNER_BYTES;
                const int sub = native ? 1 : pd->sub;
                q[i][0] = (double)RsView_ReadS16(p) * sub;
                q[i][1] = (double)RsView_ReadS16(p + 2) * sub;
                q[i][2] = (double)RsView_ReadS16(p + 4) * sub;
            }
            for (i = 0; i < 3; i++) {
                e1[i] = q[1][i] - q[0][i];
                e2[i] = q[2][i] - q[0][i];
            }
            h[0] = dir[1] * e2[2] - dir[2] * e2[1];
            h[1] = dir[2] * e2[0] - dir[0] * e2[2];
            h[2] = dir[0] * e2[1] - dir[1] * e2[0];
            det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
            if (det > -1e-9 && det < 1e-9)
                continue;
            f = 1.0 / det;
            for (i = 0; i < 3; i++)
                sv[i] = o[i] - q[0][i];
            u = f * (sv[0] * h[0] + sv[1] * h[1] + sv[2] * h[2]);
            if (u < 0.0 || u > 1.0)
                continue;
            qv[0] = sv[1] * e1[2] - sv[2] * e1[1];
            qv[1] = sv[2] * e1[0] - sv[0] * e1[2];
            qv[2] = sv[0] * e1[1] - sv[1] * e1[0];
            w = f * (dir[0] * qv[0] + dir[1] * qv[1] + dir[2] * qv[2]);
            if (w < 0.0 || u + w > 1.0)
                continue;
            t = f * (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]);
            if (t > 0.0 && (!found || t < best)) {
                best = t;
                found = 1;
            }
        }
    }
    if (!found)
        return 0;
    for (k = 0; k < 3; k++) {
        const double value = o[k] + best * dir[k];
        out[k] = (int)(value < 0.0 ? value - 0.5 : value + 0.5);
    }
    return 1;
}

// A pick ends: the point (x >= 0: the pixel clicked) and WM_COMMAND
// RS_VIEW_N_PICK to the parent - RsView_PickResult says whether it hit.
static void RsView_PickNotify(HWND view, struct RsView *v, int x, int y)
{
    HWND parent = GetParent(view);
    v->pickHit = x >= 0 && RsView_PickRay(v, x, y, v->pickAt);
    v->pickDone = v->pick ? v->pick : v->pickDone;
    v->pick = 0;
    if (parent)
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(view), RS_VIEW_N_PICK), (LPARAM)view);
}

// One tick of the wheel animation: fixed steps of spin and steering.
static void RsView_WheelStep(struct RsView *v)
{
    v->wheelSpin = RsView_NormDeg(v->wheelSpin + RS_VIEW_WHEEL_SPIN_STEP);
    if (v->wheelSteerStep == 0)
        v->wheelSteerStep = RS_VIEW_WHEEL_STEER_STEP;
    if (v->wheelSteer + v->wheelSteerStep > RS_VIEW_WHEEL_STEER_MAX ||
        v->wheelSteer + v->wheelSteerStep < -RS_VIEW_WHEEL_STEER_MAX)
        v->wheelSteerStep = -v->wheelSteerStep;
    v->wheelSteer += v->wheelSteerStep;
}

// The view's own timer for the wheel animation (another window than the
// page's timers, so no ID can clash).
static void RsView_WheelTimer(HWND view, struct RsView *v, int run)
{
    if (run && !v->wheelTimer)
        v->wheelTimer = SetTimer(view, RS_VIEW_TIMER_WHEEL, RS_VIEW_WHEEL_TICK_MS, NULL) != 0;
    else if (!run && v->wheelTimer) {
        KillTimer(view, RS_VIEW_TIMER_WHEEL);
        v->wheelTimer = 0;
    }
}

// THE ANIMATIONS: the frame and the end pose shown; a new picture only when
// something changed.
static void RsView_AnimShow(HWND view, struct RsView *v, int anim, int frame, int end, int weight)
{
    if (anim == v->anim && frame == v->frame && end == v->endPose && weight == v->endWeight)
        return;
    v->anim = anim;
    v->frame = frame;
    v->endPose = end;
    v->endWeight = weight;
    if (v->loaded)
        RsView_Changed(view, v);
}

// The view's own timer for the playing (like the wheel animation's).
static void RsView_AnimTimer(HWND view, struct RsView *v, int run)
{
    if (run && !v->animTimer)
        v->animTimer = SetTimer(view, RS_VIEW_TIMER_ANIM, RS_VIEW_ANIM_TICK_MS, NULL) != 0;
    else if (!run && v->animTimer) {
        KillTimer(view, RS_VIEW_TIMER_ANIM);
        v->animTimer = 0;
    }
}

static void RsView_PlayStop(HWND view, struct RsView *v)
{
    v->playing = -1;
    v->playStep = 0;
    RsView_AnimTimer(view, v, 0);
}

// One loop of the playing, tick by tick (rs_view.h): the length of a part
// and what tick t of it shows. Forth and back over n frames: 2 (n - 1) ticks.
#define RS_VIEW_END_TICKS 2                             // ticks per stage of an end pose
#define RS_VIEW_END_HOLD 30                             // ticks the end pose is held
#define RS_VIEW_END_REST 10                             // ticks of the frame before it comes again
#define RS_VIEW_END_LENGTH (RS_VIEW_END_TICKS * (2 * RS_VIEW_END_STAGES - 1) + RS_VIEW_END_HOLD + RS_VIEW_END_REST)

// Whether a part shows anything of its own in RS_VIEW_PLAY_ALL: an end pose
// only while the native model drawn has it.
static int RsView_PlayHas(const struct RsView *v, int what)
{
    if (what == RS_VIEW_PLAY_WIN || what == RS_VIEW_PLAY_LOSE)
        return RsView_DrawsNative(v) && v->native.end[what == RS_VIEW_PLAY_WIN ? RS_TEX_END_WIN : RS_TEX_END_LOSE] != NULL;
    return 1;
}

static int RsView_PlayLength(int what)
{
    switch (what) {
    case RS_VIEW_PLAY_STEER:
        return 2 * (s_rsViewAnimFrames[RS_VIEW_ANIM_STEER] - 1);
    case RS_VIEW_PLAY_CRASH:
        return s_rsViewAnimFrames[RS_VIEW_ANIM_CRASH];
    case RS_VIEW_PLAY_WIN:
    case RS_VIEW_PLAY_LOSE:
        return RS_VIEW_END_LENGTH;
    case RS_VIEW_PLAY_REVERSE:
        return 2 * (s_rsViewAnimFrames[RS_VIEW_ANIM_REVERSE] - 1);
    default:
        return 2 * (s_rsViewAnimFrames[RS_VIEW_ANIM_JUMP] - 1);
    }
}

// Tick t (0 .. RsView_PlayLength - 1) of a part: anim, frame, end pose, weight.
static void RsView_PlayAt(int what, int t, int out[4])
{
    out[0] = RS_VIEW_ANIM_STEER;
    out[1] = RS_VIEW_NEUTRAL_FRAME;
    out[2] = RS_VIEW_END_NONE;
    out[3] = 0;
    if (what == RS_VIEW_PLAY_STEER) {
        // 10 -> 0 -> 20 -> 10
        out[1] = t <= 10 ? 10 - t : (t <= 30 ? t - 10 : 50 - t);
    } else if (what == RS_VIEW_PLAY_CRASH) {
        out[0] = RS_VIEW_ANIM_CRASH;
        out[1] = t;
    } else if (what == RS_VIEW_PLAY_WIN || what == RS_VIEW_PLAY_LOSE) {
        // up stage 1..8, held, down 7..1, then the frame
        const int up = RS_VIEW_END_TICKS * RS_VIEW_END_STAGES, held = up + RS_VIEW_END_HOLD;
        const int down = held + RS_VIEW_END_TICKS * (RS_VIEW_END_STAGES - 1);
        const int stage = t < up ? t / RS_VIEW_END_TICKS + 1
                        : t < held ? RS_VIEW_END_STAGES
                        : t < down ? RS_VIEW_END_STAGES - 1 - (t - held) / RS_VIEW_END_TICKS : 0;
        out[2] = what == RS_VIEW_PLAY_WIN ? RS_VIEW_END_WIN : RS_VIEW_END_LOSE;
        out[3] = (int)RsView_DivRound((long long)stage * 100, RS_VIEW_END_STAGES);
    } else {
        const int anim = what == RS_VIEW_PLAY_REVERSE ? RS_VIEW_ANIM_REVERSE : RS_VIEW_ANIM_JUMP;
        const int n = s_rsViewAnimFrames[anim];
        out[0] = anim;
        out[1] = t < n ? t : 2 * (n - 1) - t;
    }
}

// The state of tick playStep of the playing.
static void RsView_PlayApply(HWND view, struct RsView *v)
{
    int st[4], what = v->playing, t;
    if (what < 0)
        return;
    if (what == RS_VIEW_PLAY_ALL) {
        int total = 0, k;
        for (k = 0; k < RS_VIEW_PLAY_ALL; k++)
            total += RsView_PlayHas(v, k) ? RsView_PlayLength(k) : 0;
        t = v->playStep % total;
        for (k = 0; k < RS_VIEW_PLAY_ALL - 1; k++) {
            const int len = RsView_PlayHas(v, k) ? RsView_PlayLength(k) : 0;
            if (t < len)
                break;
            t -= len;
        }
        what = k;
    } else {
        t = v->playStep % RsView_PlayLength(what);
    }
    RsView_PlayAt(what, t, st);
    RsView_AnimShow(view, v, st[0], st[1], st[2], st[3]);
}

static LRESULT CALLBACK RsView_Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct RsView *v = RsView_Data(hwnd);
    switch (msg) {
    case WM_CREATE:
        v = (struct RsView *)Rs_Alloc(sizeof(struct RsView));
        v->yaw = RS_VIEW_DEFAULT_YAW;
        v->pitch = RS_VIEW_DEFAULT_PITCH;
        v->zoom = RS_VIEW_DEFAULT_ZOOM;
        v->preset = RS_VIEW_PRESET_THREE_QUARTER;
        v->pitchMin = RS_VIEW_PITCH_MIN;
        v->crashRef = 1;
        v->showShadow = 1;
        v->showExhaust = 1;
        v->showNative = 1;
        v->barHot = -1;
        v->barDown = -1;
        v->wheels = 1;
        v->dirty = 1;
        v->dumPos = (int *)Rs_Alloc((size_t)RS_VIEW_DUMMY_POS_MAX * 3 * sizeof(int));
        v->dumTri = (int *)Rs_Alloc((size_t)RS_VIEW_DUMMY_TRI_MAX * 3 * sizeof(int));
        v->dumColor = (unsigned int *)Rs_Alloc((size_t)RS_VIEW_DUMMY_TRI_MAX * sizeof(unsigned int));
        v->modelLo = (long long *)Rs_Alloc(360 * sizeof(long long));
        v->modelHi = (long long *)Rs_Alloc(360 * sizeof(long long));
        v->dumLo = (long long *)Rs_Alloc(360 * sizeof(long long));
        v->dumHi = (long long *)Rs_Alloc(360 * sizeof(long long));
        RsView_FindTires(v);
        RsView_DummyExtent(v);
        v->setIndex = -1;
        v->wheelScale = 100;
        v->rearSize = RS_VIEW_REAR_SIZE_SAME;
        v->anim = RS_VIEW_ANIM_STEER;
        v->frame = RS_VIEW_NEUTRAL_FRAME;
        v->playing = -1;
        Rs_DummyTires(v->tireAt, &v->tireHalf);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        return 0;
    case WM_DESTROY:
        if (v) {
            RsView_WheelTimer(hwnd, v, 0);
            RsView_AnimTimer(hwnd, v, 0);
            RsView_FreeFrame(v);
            RsView_DropModel(v);
            RsView_DropSet(v);
            RsView_DropWheel(v);
            RsView_DropRear(v);
            RsView_SolidsFree(v);
            RsView_PoolStop(v->pool);
            Rs_Free(v->tris);
            Rs_Free(v->triRows);
            Rs_Free(v->dumPos);
            Rs_Free(v->dumTri);
            Rs_Free(v->dumColor);
            Rs_Free(v->modelLo);
            Rs_Free(v->modelHi);
            Rs_Free(v->dumLo);
            Rs_Free(v->dumHi);
            Rs_Free(v);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    case WM_SIZE:
        if (v)
            RsView_Changed(hwnd, v);
        return 0;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (v)
            RsView_Changed(hwnd, v);
        return r;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SHOWWINDOW:
        if (v && !wParam) {
            RsView_WheelTimer(hwnd, v, 0);
            RsView_AnimTimer(hwnd, v, 0);
        }
        break;
    case WM_TIMER:
        if (v && wParam == RS_VIEW_TIMER_ANIM) {
            // As the wheel animation: a fixed step per tick; hidden or
            // minimized the timer stops until the next paint.
            if (v->playing < 0 || !IsWindowVisible(hwnd) || IsIconic(GetAncestor(hwnd, GA_ROOT))) {
                RsView_AnimTimer(hwnd, v, 0);
                return 0;
            }
            v->playStep++;
            RsView_PlayApply(hwnd, v);
            return 0;
        }
        if (v && wParam == RS_VIEW_TIMER_WHEEL) {
            // Fixed steps per tick, not per time: hidden, minimized or
            // without a wheel model the timer stops (and the picture with
            // it) until the next paint starts it again.
            if (!v->wheelAnim || !v->wheelCount || !IsWindowVisible(hwnd) ||
                IsIconic(GetAncestor(hwnd, GA_ROOT))) {
                RsView_WheelTimer(hwnd, v, 0);
                return 0;
            }
            RsView_WheelStep(v);
            if (RsView_WheelsShown(v))
                RsView_Changed(hwnd, v);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc;
        if (v && v->wheelAnim && v->wheelCount && !v->wheelTimer)
            RsView_WheelTimer(hwnd, v, 1);   // shown again, or a wheel model came
        if (v && v->playing >= 0 && !v->animTimer)
            RsView_AnimTimer(hwnd, v, 1);    // shown again
        dc = BeginPaint(hwnd, &ps);
        RsView_PaintTo(hwnd, v, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        RsView_PaintTo(hwnd, v, (HDC)wParam);
        return 0;
    case WM_SETCURSOR:
        if (v && RsView_Turnable(v) && LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursor(NULL, RsView_BarHit(v, pt.x, pt.y) >= 0 ? IDC_HAND : IDC_SIZEALL));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        const int hit = v ? RsView_BarHit(v, x, y) : -1;
        if (!v || v->dragging || v->panning)
            return 0;
        if (hit >= 0) {
            // A button of the bar: it acts when the button comes up over it.
            v->barDown = hit;
            RsView_BarInvalidate(hwnd, v, hit);
            SetCapture(hwnd);
            return 0;
        }
        if (!RsView_Turnable(v))
            return 0;
        if (msg == WM_LBUTTONDBLCLK && !v->pick &&
            (DWORD)GetMessageTime() - v->pickClickTime > (DWORD)GetDoubleClickTime()) {
            // A double click resets the view (while picking, and as the second
            // click of the one that picked a point, it is a click).
            RsView_UserPreset(hwnd, v, RS_VIEW_PRESET_THREE_QUARTER);
            return 0;
        }
        v->dragging = 1;
        v->dragX = x;
        v->dragY = y;
        v->dragYaw = v->yaw;
        v->dragPitch = v->pitch;
        v->downX = x;
        v->downY = y;
        SetCapture(hwnd);
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        // Right or middle button: moves the picture; a right click without
        // a drag opens the menu (or ends a pick) when it comes up.
        if (v && !v->dragging && !v->panning && v->barDown < 0 && RsView_Turnable(v)) {
            v->panning = msg == WM_RBUTTONDOWN ? 1 : 2;
            v->panDownX = (short)LOWORD(lParam);
            v->panDownY = (short)HIWORD(lParam);
            v->panFromX = v->panX;
            v->panFromY = v->panY;
            v->panMoved = 0;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE: {
        const int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        if (!v)
            return 0;
        if (v->dragging && GetCapture() == hwnd) {
            // Half a turn per 360 pixels at 96 dpi, across for the yaw, up
            // and down for the pitch; integer, so the same drag gives the
            // same angles.
            const int oldYaw = v->yaw, oldPitch = v->pitch;
            int pitch = v->dragPitch + (y - v->dragY) * 180 / Rs_Px(360);
            const int lowest = v->pitchMin > RS_VIEW_PITCH_MIN ? v->pitchMin : RS_VIEW_PITCH_MIN;
            if (pitch < lowest)
                pitch = lowest;
            if (pitch > RS_VIEW_PITCH_MAX)
                pitch = RS_VIEW_PITCH_MAX;
            v->yaw = RsView_NormDeg(v->dragYaw + (x - v->dragX) * 180 / Rs_Px(360));
            v->pitch = pitch;
            if (v->yaw != oldYaw || v->pitch != oldPitch) {
                v->preset = -1;
                RsView_UserCamera(hwnd, v, oldYaw);
            }
            return 0;
        }
        if (v->panning && GetCapture() == hwnd) {
            const int dx = x - v->panDownX, dy = y - v->panDownY;
            const int oldX = v->panX, oldY = v->panY;
            if (dx * dx + dy * dy >= Rs_Px(4) * Rs_Px(4))
                v->panMoved = 1;
            if (v->panMoved) {
                v->panX = v->panFromX + dx;
                v->panY = v->panFromY + dy;
                RsView_ClampCamera(v);
                if (v->panX != oldX || v->panY != oldY) {
                    v->preset = -1;
                    RsView_UserCamera(hwnd, v, v->yaw);
                }
            }
            return 0;
        }
        if (v->barDown < 0) {
            // The button under the mouse lights up (only the bar is drawn again).
            const int hit = RsView_BarHit(v, x, y);
            if (hit != v->barHot) {
                RsView_BarInvalidate(hwnd, v, v->barHot);
                v->barHot = hit;
                RsView_BarInvalidate(hwnd, v, hit);
            }
            if (!v->tracking) {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                v->tracking = TrackMouseEvent(&tme) ? 1 : 0;
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (v) {
            v->tracking = 0;
            RsView_BarInvalidate(hwnd, v, v->barHot);
            v->barHot = -1;
        }
        return 0;
    case WM_LBUTTONUP:
        if (v && v->barDown >= 0) {
            const int down = v->barDown;
            const int hit = RsView_BarHit(v, (short)LOWORD(lParam), (short)HIWORD(lParam));
            v->barDown = -1;
            ReleaseCapture();
            RsView_BarInvalidate(hwnd, v, down);
            if (hit != down)
                return 0;
            if (down < RS_VIEW_PRESET_COUNT)
                RsView_UserPreset(hwnd, v, down);
            else if (down == RS_VIEW_BAR_RESET)
                RsView_UserPreset(hwnd, v, RS_VIEW_PRESET_THREE_QUARTER);
            else
                RsView_Menu(hwnd, v, v->barRect[down].left, v->barRect[down].top, 1);
            return 0;
        }
        if (v && v->dragging) {
            const int dx = (short)LOWORD(lParam) - v->downX, dy = (short)HIWORD(lParam) - v->downY;
            ReleaseCapture();
            // A click (less than 4 pixels at 96 dpi) while a point is picked:
            // the surface under it, the camera as before the click.
            if (v->pick && dx * dx + dy * dy < Rs_Px(4) * Rs_Px(4)) {
                const int oldYaw = v->yaw;
                if (v->yaw != v->dragYaw || v->pitch != v->dragPitch) {
                    v->yaw = v->dragYaw;
                    v->pitch = v->dragPitch;
                    RsView_UserCamera(hwnd, v, oldYaw);
                }
                v->pickClickTime = (DWORD)GetMessageTime();
                RsView_PickNotify(hwnd, v, v->downX, v->downY);
            }
        }
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (v && v->panning == (msg == WM_RBUTTONUP ? 1 : 2)) {
            const int click = !v->panMoved && msg == WM_RBUTTONUP;
            ReleaseCapture();
            if (click) {
                // A right click: ends a pick without a point, else the menu.
                if (v->pick) {
                    v->pick = 0;
                    RsView_PickNotify(hwnd, v, -1, -1);
                } else {
                    RsView_Menu(hwnd, v, (short)LOWORD(lParam), (short)HIWORD(lParam), 0);
                }
            }
        } else if (v && msg == WM_RBUTTONUP && v->pick) {
            // A right click ends a pick without a point.
            v->pick = 0;
            RsView_PickNotify(hwnd, v, -1, -1);
        }
        return 0;
    case WM_MOUSEWHEEL:
        // The wheel zooms in fixed steps about the point under the mouse:
        // that point of the picture stays under it (rs_shell.c hands the
        // wheel over while the mouse is over the view). The pixel's centre,
        // in 1/16 pixels.
        if (v && RsView_Turnable(v)) {
            const int oldZoom = v->zoom;
            POINT pt;
            long long qx, qy;
            pt.x = (short)LOWORD(lParam);
            pt.y = (short)HIWORD(lParam);
            ScreenToClient(hwnd, &pt);
            qx = (long long)pt.x * RS_VIEW_SUB + RS_VIEW_SUB / 2;
            qy = (long long)pt.y * RS_VIEW_SUB + RS_VIEW_SUB / 2;
            v->wheelDelta += (short)HIWORD(wParam);
            while (v->wheelDelta >= WHEEL_DELTA) {
                RsView_ZoomAt(v, RsView_ZoomStep(v->zoom, 1), qx, qy);
                RsView_ClampCamera(v);
                v->wheelDelta -= WHEEL_DELTA;
            }
            while (v->wheelDelta <= -WHEEL_DELTA) {
                RsView_ZoomAt(v, RsView_ZoomStep(v->zoom, 0), qx, qy);
                RsView_ClampCamera(v);
                v->wheelDelta += WHEEL_DELTA;
            }
            if (v->zoom != oldZoom)
                RsView_UserCamera(hwnd, v, v->yaw);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (v) {
            v->dragging = 0;
            v->panning = 0;
            if (v->barDown >= 0) {
                RsView_BarInvalidate(hwnd, v, v->barDown);
                v->barDown = -1;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------

BOOL RsView_Register(HINSTANCE instance)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;   // a double click resets the view
    wc.lpfnWndProc = RsView_Proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = RS_VIEW_CLASS;
    if (RegisterClassExW(&wc)) {
        // Once: the dummy's driver hangs on copies of rldpack's measures.
        wchar_t why[160];
        if (!Rs_DummyCopiesCheck(why, 160))
            Rs_AutoLog(L"  preview: dummy measures differ from rldpack: %ls", why);
        return TRUE;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void RsView_LoadPreview(HWND view, const wchar_t *path)
{
    struct RsView *v = RsView_Data(view);
    HANDLE f;
    LARGE_INTEGER size;
    unsigned char *data;
    DWORD want, done = 0;

    if (!v)
        return;
    // Shared for writing and deleting: rldpack may be writing the next one already.
    f = (path && *path) ? CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
                        : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = (path && *path) ? GetLastError() : ERROR_FILE_NOT_FOUND;
        RsView_DropModel(v);
        swprintf(v->message, RS_VIEW_MESSAGE_CAP,
                 L"The preview could not be shown: the preview file could not be opened (Windows error %lu) - check the model again.",
                 (unsigned long)e);
        RsView_Changed(view, v);
        return;
    }
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 0 || size.QuadPart > RS_VIEW_FILE_MAX) {
        CloseHandle(f);
        RsView_DropModel(v);
        swprintf(v->message, RS_VIEW_MESSAGE_CAP,
                 L"The preview could not be shown: the preview file is missing its size or is larger than %u MB - check the model again.",
                 RS_VIEW_FILE_MAX / (1024u * 1024u));
        RsView_Changed(view, v);
        return;
    }
    want = (DWORD)size.QuadPart;
    data = (unsigned char *)Rs_Alloc((size_t)want + 1);
    while (done < want) {
        DWORD part = 0;
        if (!ReadFile(f, data + done, want - done, &part, NULL) || part == 0)
            break;
        done += part;
    }
    CloseHandle(f);
    if (done != want) {
        Rs_Free(data);
        RsView_DropModel(v);
        swprintf(v->message, RS_VIEW_MESSAGE_CAP,
                 L"The preview could not be shown: the preview file could not be read completely - check the model again.");
        RsView_Changed(view, v);
        return;
    }
    RsView_Take(view, v, data, (size_t)want);
}

BOOL RsView_LoadPreviewData(HWND view, const void *data, size_t bytes)
{
    struct RsView *v = RsView_Data(view);
    unsigned char *copy;
    if (!v)
        return FALSE;
    if (!data || bytes > RS_VIEW_FILE_MAX) {
        RsView_DropModel(v);
        swprintf(v->message, RS_VIEW_MESSAGE_CAP,
                 L"The preview could not be shown: no preview data or more than %u MB - check the model again.",
                 RS_VIEW_FILE_MAX / (1024u * 1024u));
        RsView_Changed(view, v);
        return FALSE;
    }
    copy = (unsigned char *)Rs_Alloc(bytes + 1);
    memcpy(copy, data, bytes);
    return RsView_Take(view, v, copy, bytes) ? TRUE : FALSE;
}

// The turn frames of RS_VIEW_POSE_*.
static const unsigned char s_rsViewPoseFrame[RS_VIEW_POSE_COUNT] = { RS_VIEW_NEUTRAL_FRAME, 0, 20 };

void RsView_SetPose(HWND view, int pose)
{
    if (pose < 0)
        pose = 0;
    if (pose >= RS_VIEW_POSE_COUNT)
        pose = RS_VIEW_POSE_COUNT - 1;
    RsView_SetAnimFrame(view, RS_VIEW_ANIM_STEER, s_rsViewPoseFrame[pose]);
}

void RsView_SetYaw(HWND view, int degrees)
{
    struct RsView *v = RsView_Data(view);
    if (v)
        RsView_SetYawFrom(view, v, degrees, 0);
}

int RsView_GetYaw(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return v ? v->yaw : 0;
}

void RsView_SetWheels(HWND view, int on)
{
    struct RsView *v = RsView_Data(view);
    if (!v || v->wheels == (on != 0))
        return;
    v->wheels = on != 0;
    RsView_Changed(view, v);
}

void RsView_SetCrashBox(HWND view, int x0, int y0, int z0, int x1, int y1, int z1)
{
    struct RsView *v = RsView_Data(view);
    int box[6], a;
    if (!v)
        return;
    box[0] = x0; box[1] = y0; box[2] = z0;
    box[3] = x1; box[4] = y1; box[5] = z1;
    // Tenths of the s16 range of the model (the driver of the dummy stays
    // within RS_VIEW_DRIVER_MAX of it).
    for (a = 0; a < 6; a++) {
        if (box[a] < -327680)
            box[a] = -327680;
        if (box[a] > 327670)
            box[a] = 327670;
    }
    for (a = 0; a < 3; a++)
        if (box[a] > box[a + 3]) {
            int swap = box[a];
            box[a] = box[a + 3];
            box[a + 3] = swap;
        }
    v->hasCrash = box[0] < box[3] && box[1] < box[4] && box[2] < box[5];
    memcpy(v->crash, box, sizeof(box));
    v->solidsValid = 0;      // the driver is built again
    RsView_DummyExtent(v);   // the driver takes its size from the box
    RsView_Changed(view, v);
}

void RsView_Clear(HWND view, const wchar_t *message)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    RsView_DropModel(v);
    v->message[0] = 0;
    if (message) {
        wcsncpy(v->message, message, RS_VIEW_MESSAGE_CAP - 1);
        v->message[RS_VIEW_MESSAGE_CAP - 1] = 0;
    }
    RsView_Changed(view, v);
}

BOOL RsView_Loaded(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return (v && v->loaded) ? TRUE : FALSE;
}

int RsView_TriangleCount(HWND view, int pose)
{
    struct RsView *v = RsView_Data(view);
    if (!v || !v->loaded || pose < 0 || pose >= RS_VIEW_POSE_COUNT)
        return 0;
    return v->poses[RsView_FilePose(v->poseCount, s_rsViewPoseFrame[pose])].count;
}

// ---------------------------------------------------------------------------
// Preview features: a set of poses and a wheel model
// ---------------------------------------------------------------------------

// The whole file into memory (Rs_Alloc); NULL with why filled otherwise.
static unsigned char *RsView_ReadFile(const wchar_t *path, size_t *bytes, wchar_t *why, int whyCap)
{
    HANDLE f;
    LARGE_INTEGER size;
    unsigned char *data;
    DWORD want, done = 0;

    f = (path && *path) ? CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
                        : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) {
        swprintf(why, whyCap, L"the file could not be opened (Windows error %lu)",
                 (unsigned long)((path && *path) ? GetLastError() : ERROR_FILE_NOT_FOUND));
        return NULL;
    }
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 0 || size.QuadPart > RS_VIEW_FILE_MAX) {
        CloseHandle(f);
        swprintf(why, whyCap, L"the file is missing its size or is larger than %u MB",
                 RS_VIEW_FILE_MAX / (1024u * 1024u));
        return NULL;
    }
    want = (DWORD)size.QuadPart;
    data = (unsigned char *)Rs_Alloc((size_t)want + 1);
    while (done < want) {
        DWORD part = 0;
        if (!ReadFile(f, data + done, want - done, &part, NULL) || part == 0)
            break;
        done += part;
    }
    CloseHandle(f);
    if (done != want) {
        Rs_Free(data);
        swprintf(why, whyCap, L"the file could not be read completely");
        return NULL;
    }
    *bytes = (size_t)want;
    return data;
}

// The framing changed (a set or a wheel model came or went, a new wheel
// scale): the model's spans again, then a new picture.
static void RsView_Reframe(HWND view, struct RsView *v)
{
    if (v->loaded)
        RsView_ModelSpans(v);
    RsView_Changed(view, v);
}

// A damaged or missing set drops the one shown before (FALSE); the model
// stays. The pose set is shown from RsView_ShowPoseSet on.
BOOL RsView_LoadPoseSet(HWND view, const wchar_t *path)
{
    struct RsView *v = RsView_Data(view);
    wchar_t why[256];
    unsigned char *data;
    size_t bytes = 0;
    if (!v)
        return FALSE;
    data = RsView_ReadFile(path, &bytes, why, 256);
    if (!data || !RsView_ParseSet(v, data, bytes, why, 256)) {
        Rs_Free(data);
        RsView_DropSet(v);
        Rs_AutoLog(L"  preview: the pose set could not be shown: %ls", why);
        RsView_Reframe(view, v);
        return FALSE;
    }
    RsView_Reframe(view, v);
    return TRUE;
}

int RsView_PoseSetCount(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return v ? v->setCount : 0;
}

// Never zooms: the framing holds the whole set since it was loaded.
void RsView_ShowPoseSet(HWND view, int index)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    if (index < 0 || index >= v->setCount)
        index = -1;
    if (index == v->setIndex)
        return;
    v->setIndex = index;
    RsView_Changed(view, v);
}

void RsView_DropPoseSet(HWND view)
{
    struct RsView *v = RsView_Data(view);
    if (!v || !v->setData)
        return;
    RsView_DropSet(v);
    RsView_Reframe(view, v);
}

// A damaged or missing file drops the wheel model shown before (FALSE): the
// game's wheels come back.
BOOL RsView_LoadWheelModel(HWND view, const wchar_t *path)
{
    struct RsView *v = RsView_Data(view);
    wchar_t why[256];
    unsigned char *data;
    size_t bytes = 0;
    if (!v)
        return FALSE;
    data = RsView_ReadFile(path, &bytes, why, 256);
    if (!data || !RsView_ParseWheel(v, 0, data, bytes, why, 256)) {
        Rs_Free(data);
        RsView_DropWheel(v);
        Rs_AutoLog(L"  preview: the wheel model could not be shown: %ls", why);
        RsView_Reframe(view, v);
        return FALSE;
    }
    RsView_WheelExtent(v);
    RsView_Reframe(view, v);
    return TRUE;
}

void RsView_DropWheelModel(HWND view)
{
    struct RsView *v = RsView_Data(view);
    if (!v || !v->wheelData)
        return;
    RsView_DropWheel(v);
    RsView_Reframe(view, v);
}

// A new size is a new wheel: the framing follows it (turning never zooms).
void RsView_SetWheelScale(HWND view, int percent)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    if (percent < 50)
        percent = 50;
    if (percent > 200)
        percent = 200;
    if (percent == v->wheelScale)
        return;
    v->wheelScale = percent;
    if (v->wheelCount) {
        RsView_WheelExtent(v);
        RsView_Reframe(view, v);
    }
}

void RsView_SetWheelTurn(HWND view, int spinDegrees, int steerDegrees)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    spinDegrees = RsView_NormDeg(spinDegrees);
    if (steerDegrees < -RS_VIEW_WHEEL_STEER_MAX)
        steerDegrees = -RS_VIEW_WHEEL_STEER_MAX;
    if (steerDegrees > RS_VIEW_WHEEL_STEER_MAX)
        steerDegrees = RS_VIEW_WHEEL_STEER_MAX;
    if (spinDegrees == v->wheelSpin && steerDegrees == v->wheelSteer)
        return;
    v->wheelSpin = spinDegrees;
    v->wheelSteer = steerDegrees;
    if (v->wheelCount)
        RsView_Changed(view, v);
}

void RsView_SetWheelAnimation(HWND view, int on)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    v->wheelAnim = on != 0;
    RsView_WheelTimer(view, v, v->wheelAnim && v->wheelCount && IsWindowVisible(view));
}

// See rs_view.h: the picture `frames` times, each one tick of the animation
// further, timed with QueryPerformanceCounter; spin and steering back after.
int RsView_WheelBench(HWND view, int frames, int *wholeAvgUs, int *wholeMaxUs, int *wheelsAvgUs, int *wheelsMaxUs)
{
    struct RsView *v = RsView_Data(view);
    LARGE_INTEGER freq, a, b;
    long long whole = 0, wheels = 0, wholeMax = 0, wheelsMax = 0;
    int spin, steer, step, i;
    RECT rc;

    if (!v || frames < 1 || !v->wheelCount)
        return 0;
    GetClientRect(view, &rc);
    if (!RsView_EnsureFrame(v, rc.right, rc.bottom))
        return 0;
    spin = v->wheelSpin;
    steer = v->wheelSteer;
    step = v->wheelSteerStep;
    QueryPerformanceFrequency(&freq);
    v->benching = 1;
    for (i = 0; i < frames; i++) {
        RsView_WheelStep(v);
        v->wheelTicks = 0;
        QueryPerformanceCounter(&a);
        RsView_Render(view, v);
        QueryPerformanceCounter(&b);
        whole += b.QuadPart - a.QuadPart;
        wheels += v->wheelTicks;
        if (b.QuadPart - a.QuadPart > wholeMax)
            wholeMax = b.QuadPart - a.QuadPart;
        if (v->wheelTicks > wheelsMax)
            wheelsMax = v->wheelTicks;
    }
    v->benching = 0;
    v->wheelSpin = spin;
    v->wheelSteer = steer;
    v->wheelSteerStep = step;
    RsView_Changed(view, v);
    *wholeAvgUs = (int)(whole * 1000000 / freq.QuadPart / frames);
    *wholeMaxUs = (int)(wholeMax * 1000000 / freq.QuadPart);
    *wheelsAvgUs = (int)(wheels * 1000000 / freq.QuadPart / frames);
    *wheelsMaxUs = (int)(wheelsMax * 1000000 / freq.QuadPart);
    return frames;
}

int RsView_WheelPixels(HWND view, int *w, int *h)
{
    struct RsView *v = RsView_Data(view);
    *w = v ? v->w : 0;
    *h = v ? v->h : 0;
    return v && RsView_WheelsShown(v);
}

// ---------------------------------------------------------------------------
// The look (the tab In-game look of the page Character)
// ---------------------------------------------------------------------------

void RsView_SetLook(HWND view, int shadow, const int quad[4], int count, const int point[2][3], int grey)
{
    struct RsView *v = RsView_Data(view);
    int k, a;
    if (!v)
        return;
    v->lookShadow = shadow && quad != NULL;
    for (k = 0; k < 4; k++)
        v->lookQuad[k] = (quad != NULL) ? quad[k] : 0;
    v->lookCount = (point != NULL) ? (count < 0 ? 0 : (count > 2 ? 2 : count)) : 0;
    for (k = 0; k < 2; k++)
        for (a = 0; a < 3; a++)
            v->lookPoint[k][a] = (point != NULL && k < v->lookCount) ? point[k][a] : 0;
    v->lookGrey = grey != 0;
    RsView_Changed(view, v);
}

void RsView_PickBegin(HWND view, int n)
{
    struct RsView *v = RsView_Data(view);
    if (v)
        v->pick = (n == 1 || n == 2) ? n : 0;
}

int RsView_Picking(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return v ? v->pick : 0;
}

int RsView_PickResult(HWND view, int *n, int out[3])
{
    struct RsView *v = RsView_Data(view);
    int k;
    if (!v)
        return 0;
    if (n)
        *n = v->pickDone;
    for (k = 0; k < 3; k++)
        out[k] = v->pickAt[k];
    return v->pickHit;
}

int RsView_PickPixel(HWND view, int n, int px, int py)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return 0;
    {
        RECT rc;
        GetClientRect(view, &rc);
        RsView_EnsureFrame(v, rc.right, rc.bottom);
    }
    v->pick = (n == 1 || n == 2) ? n : 0;
    RsView_PickNotify(view, v, px, py);
    return v->pickHit;
}

// ---------------------------------------------------------------------------
// The camera and the display toggles (rs_view.h, THE CAMERA)
// ---------------------------------------------------------------------------
//
// Called by the page (and its automation) they only draw again; the user's
// changes in the view itself (mouse, bar, menu) also notify the parent.

void RsView_SetCamera(HWND view, int yaw, int pitch, int zoomPercent, int panX, int panY)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    RsView_ClampCamera(v);   // the pan as drawn, to tell a new one from the old
    v->yaw = RsView_NormDeg(yaw);
    v->pitch = pitch < RS_VIEW_PITCH_MIN ? RS_VIEW_PITCH_MIN : (pitch > RS_VIEW_PITCH_MAX ? RS_VIEW_PITCH_MAX : pitch);
    if (zoomPercent < RS_VIEW_ZOOM_MIN)
        zoomPercent = RS_VIEW_ZOOM_MIN;
    if (zoomPercent > RS_VIEW_ZOOM_MAX)
        zoomPercent = RS_VIEW_ZOOM_MAX;
    if (panX == v->panX && panY == v->panY) {
        // The pan as it is: zoom about the middle of the model (it stays
        // where it is in the picture), without a model about the middle of
        // the picture.
        long long qx, qy;
        RsView_ModelAnchor(v, &qx, &qy);
        RsView_ZoomAt(v, zoomPercent, qx, qy);
    } else {
        v->zoom = zoomPercent;
        v->panX = panX;
        v->panY = panY;
    }
    v->preset = -1;
    RsView_ClampCamera(v);
    RsView_Changed(view, v);
}

void RsView_GetCamera(HWND view, int *yaw, int *pitch, int *zoomPercent, int *panX, int *panY)
{
    struct RsView *v = RsView_Data(view);
    if (v)
        RsView_ClampCamera(v);
    if (yaw)
        *yaw = v ? v->yaw : 0;
    if (pitch)
        *pitch = v ? v->pitch : 0;
    if (zoomPercent)
        *zoomPercent = v ? v->zoom : 0;
    if (panX)
        *panX = v ? v->panX : 0;
    if (panY)
        *panY = v ? v->panY : 0;
}

void RsView_ResetCamera(HWND view)
{
    RsView_SetPreset(view, RS_VIEW_PRESET_THREE_QUARTER);
}

// The fixed views: yaw and pitch (rs_view.h); zoom 100 %, no pan. Race
// switches to the race camera (RsView_Scene), every other one back.
void RsView_SetPreset(HWND view, int preset)
{
    static const short yawPitch[RS_VIEW_PRESET_COUNT][2] = {
        { 0, 5 }, { 90, 5 }, { 180, 5 }, { 0, 89 }, { RS_VIEW_DEFAULT_YAW, RS_VIEW_DEFAULT_PITCH },
        { 180, RS_VIEW_RACE_PITCH },
    };
    struct RsView *v = RsView_Data(view);
    if (!v || preset < 0 || preset >= RS_VIEW_PRESET_COUNT)
        return;
    v->yaw = yawPitch[preset][0];
    v->pitch = yawPitch[preset][1];
    v->zoom = RS_VIEW_DEFAULT_ZOOM;
    v->panX = 0;
    v->panY = 0;
    v->race = preset == RS_VIEW_PRESET_RACE;
    v->preset = preset;
    v->wheelDelta = 0;
    RsView_Changed(view, v);
}

int RsView_GetPreset(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return v ? v->preset : -1;
}

void RsView_SetModelKey(HWND view, const wchar_t *key)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    if (!key)
        key = L"";
    if (wcscmp(key, v->modelKey) == 0)
        return;
    wcsncpy(v->modelKey, key, RS_VIEW_MESSAGE_CAP - 1);
    v->modelKey[RS_VIEW_MESSAGE_CAP - 1] = 0;
    RsView_ResetCamera(view);
}

void RsView_SetBackground(HWND view, int light)
{
    struct RsView *v = RsView_Data(view);
    if (!v || v->light == (light != 0))
        return;
    v->light = light != 0;
    RsView_Changed(view, v);
}

// The framing changes with it (the model alone or beside the dummy).
void RsView_SetCrashRef(HWND view, int on)
{
    struct RsView *v = RsView_Data(view);
    if (!v || v->crashRef == (on != 0))
        return;
    v->crashRef = on != 0;
    RsView_ClampCamera(v);
    RsView_Changed(view, v);
}

void RsView_SetOverlays(HWND view, int shadow, int exhaust)
{
    struct RsView *v = RsView_Data(view);
    if (!v || (v->showShadow == (shadow != 0) && v->showExhaust == (exhaust != 0)))
        return;
    v->showShadow = shadow != 0;
    v->showExhaust = exhaust != 0;
    RsView_Changed(view, v);
}

void RsView_SetShowNative(HWND view, int native)
{
    struct RsView *v = RsView_Data(view);
    if (!v || v->showNative == (native != 0))
        return;
    v->showNative = native != 0;
    RsView_Changed(view, v);
}

int RsView_HasNative(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return v && RsView_NativeUsable(v);
}

int RsView_GetToggles(HWND view, int *light, int *crash, int *shadow, int *exhaust, int *native)
{
    struct RsView *v = RsView_Data(view);
    if (light)
        *light = v ? v->light : 0;
    if (crash)
        *crash = v ? v->crashRef : 1;
    if (shadow)
        *shadow = v ? v->showShadow : 1;
    if (exhaust)
        *exhaust = v ? v->showExhaust : 1;
    if (native)
        *native = v ? v->showNative : 1;
    return v && RsView_DrawsNative(v);
}

// See rs_view.h: the picture `frames` times, each one step further in an
// orbit - yaw +3 degrees, the pitch swinging between 5 and 60 by 3 per
// picture - timed with QueryPerformanceCounter; the camera back after.
int RsView_Bench(HWND view, int frames, int *avgUs, int *maxUs, int *p95Us, int *w, int *h)
{
    struct RsView *v = RsView_Data(view);
    LARGE_INTEGER freq, a, b;
    long long sum = 0, *ticks;
    int yaw, pitch, preset, i, j;
    RECT rc;

    *avgUs = 0;
    *maxUs = 0;
    *p95Us = 0;
    *w = 0;
    *h = 0;
    if (!v || frames < 1)
        return 0;
    if (frames > RS_VIEW_BENCH_MAX)
        frames = RS_VIEW_BENCH_MAX;
    GetClientRect(view, &rc);
    if (!RsView_EnsureFrame(v, rc.right, rc.bottom))
        return 0;
    *w = v->w;
    *h = v->h;
    ticks = (long long *)Rs_Alloc((size_t)frames * sizeof(long long));
    yaw = v->yaw;
    pitch = v->pitch;
    preset = v->preset;
    QueryPerformanceFrequency(&freq);
    v->benching = 1;
    for (i = 0; i < frames; i++) {
        const int swing = (3 * (i + 1)) % 110;   // 0..107, up and back down
        v->yaw = RsView_NormDeg(yaw + 3 * (i + 1));
        v->pitch = 5 + (swing < 55 ? swing : 110 - swing);
        QueryPerformanceCounter(&a);
        RsView_Render(view, v);
        QueryPerformanceCounter(&b);
        ticks[i] = b.QuadPart - a.QuadPart;
        sum += ticks[i];
    }
    v->benching = 0;
    v->yaw = yaw;
    v->pitch = pitch;
    v->preset = preset;
    RsView_Changed(view, v);
    // Sorted (insertion; the bench is short) for the 95th percentile.
    for (i = 1; i < frames; i++) {
        const long long t = ticks[i];
        for (j = i; j > 0 && ticks[j - 1] > t; j--)
            ticks[j] = ticks[j - 1];
        ticks[j] = t;
    }
    *avgUs = (int)(sum * 1000000 / freq.QuadPart / frames);
    *maxUs = (int)(ticks[frames - 1] * 1000000 / freq.QuadPart);
    *p95Us = (int)(ticks[(frames * 95 + 99) / 100 - 1] * 1000000 / freq.QuadPart);
    Rs_Free(ticks);
    return frames;
}

// The textures of the native model and their bytes with every mip level
// (rs_tex.c); the materials without a texture are returned - a texture the
// check could not use ends there (rldpack warns native-texture with its
// name), as does a material that never had one.
int RsView_TextureInfo(HWND view, int *textures, int *bytes)
{
    struct RsView *v = RsView_Data(view);
    int m, missing = 0;
    const int loaded = v && v->loaded && v->native.triangles > 0;
    if (textures)
        *textures = loaded ? v->native.textureCount : 0;
    if (bytes)
        *bytes = loaded ? (v->native.bytes > 0x7FFFFFFF ? 0x7FFFFFFF : (int)v->native.bytes) : 0;
    for (m = 0; loaded && m < v->native.materialCount; m++)
        if (v->native.mat[m].texture < 0)
            missing++;
    return missing;
}

// ---------------------------------------------------------------------------
// The rear wheel, the axles and the animations (rs_view.h)
// ---------------------------------------------------------------------------

// As RsView_LoadWheelModel: a damaged or missing file drops the rear wheel
// model shown before (FALSE) - the rear pair shows the wheel model again.
BOOL RsView_LoadRearWheelModel(HWND view, const wchar_t *path)
{
    struct RsView *v = RsView_Data(view);
    wchar_t why[256];
    unsigned char *data;
    size_t bytes = 0;
    if (!v)
        return FALSE;
    data = RsView_ReadFile(path, &bytes, why, 256);
    if (!data || !RsView_ParseWheel(v, 1, data, bytes, why, 256)) {
        Rs_Free(data);
        RsView_DropRear(v);
        Rs_AutoLog(L"  preview: the rear wheel model could not be shown: %ls", why);
        RsView_WheelExtent(v);
        RsView_Reframe(view, v);
        return FALSE;
    }
    RsView_WheelExtent(v);
    RsView_Reframe(view, v);
    return TRUE;
}

void RsView_DropRearWheelModel(HWND view)
{
    struct RsView *v = RsView_Data(view);
    if (!v || !v->rearData)
        return;
    RsView_DropRear(v);
    RsView_WheelExtent(v);
    RsView_Reframe(view, v);
}

// A new offset is a new place of the wheels: the framing follows it, as
// the scale (turning never zooms).
void RsView_SetAxle(HWND view, int axle, int dz, int dy, int dtrack)
{
    struct RsView *v = RsView_Data(view);
    static const int lo[3] = { RS_VIEW_AXLE_DZ_MIN, RS_VIEW_AXLE_DY_MIN, RS_VIEW_AXLE_TRACK_MIN };
    static const int hi[3] = { RS_VIEW_AXLE_DZ_MAX, RS_VIEW_AXLE_DY_MAX, RS_VIEW_AXLE_TRACK_MAX };
    int o[3], a;
    if (!v || axle < RS_VIEW_AXLE_FRONT || axle > RS_VIEW_AXLE_REAR)
        return;
    o[0] = dz;
    o[1] = dy;
    o[2] = dtrack;
    for (a = 0; a < 3; a++)
        o[a] = o[a] < lo[a] ? lo[a] : (o[a] > hi[a] ? hi[a] : o[a]);
    if (memcmp(o, v->axle[axle], sizeof(o)) == 0)
        return;
    memcpy(v->axle[axle], o, sizeof(o));
    if (v->wheelCount) {
        RsView_WheelExtent(v);
        RsView_Reframe(view, v);
    }
}

void RsView_SetRearWheelSize(HWND view, int size)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    if (size < RS_VIEW_REAR_SIZE_MIN)
        size = RS_VIEW_REAR_SIZE_MIN;
    if (size > RS_VIEW_REAR_SIZE_MAX)
        size = RS_VIEW_REAR_SIZE_MAX;
    if (size == v->rearSize)
        return;
    v->rearSize = size;
    if (v->wheelCount && v->rearCount) {
        RsView_WheelExtent(v);
        RsView_Reframe(view, v);
    }
}

void RsView_GetAxle(HWND view, int axle, int *dz, int *dy, int *dtrack)
{
    struct RsView *v = RsView_Data(view);
    const int ok = v && axle >= 0 && axle <= 1;
    if (dz)
        *dz = ok ? v->axle[axle][0] : 0;
    if (dy)
        *dy = ok ? v->axle[axle][1] : 0;
    if (dtrack)
        *dtrack = ok ? v->axle[axle][2] : 0;
}

void RsView_SetWheelsAlways(HWND view, int on)
{
    struct RsView *v = RsView_Data(view);
    if (v)
        v->wheelsAlways = on != 0;
}

int RsView_GetWheelsAlways(HWND view)
{
    struct RsView *v = RsView_Data(view);
    return v ? v->wheelsAlways : 0;
}

void RsView_SetAnimFrame(HWND view, int anim, int frame)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    RsView_PlayStop(view, v);
    if (anim < 0)
        anim = 0;
    if (anim >= RS_VIEW_ANIM_COUNT)
        anim = RS_VIEW_ANIM_COUNT - 1;
    if (frame < 0)
        frame = 0;
    if (frame >= s_rsViewAnimFrames[anim])
        frame = s_rsViewAnimFrames[anim] - 1;
    RsView_AnimShow(view, v, anim, frame, RS_VIEW_END_NONE, 0);   // a frame chosen ends the end pose
}

void RsView_SetSteer(HWND view, int steer)
{
    if (steer < -RS_VIEW_STEER_MAX)
        steer = -RS_VIEW_STEER_MAX;
    if (steer > RS_VIEW_STEER_MAX)
        steer = RS_VIEW_STEER_MAX;
    RsView_SetAnimFrame(view, RS_VIEW_ANIM_STEER, RS_VIEW_NEUTRAL_FRAME + steer);
}

void RsView_SetEndPose(HWND view, int end, int weight)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    RsView_PlayStop(view, v);
    if (end < RS_VIEW_END_NONE || end > RS_VIEW_END_LOSE)
        end = RS_VIEW_END_NONE;
    if (weight < 0)
        weight = 0;
    if (weight > 100)
        weight = 100;
    RsView_AnimShow(view, v, v->anim, v->frame, end, weight);
}

void RsView_PlayAnim(HWND view, int what, int on)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    if (!on || what < 0 || what >= RS_VIEW_PLAY_COUNT) {
        RsView_PlayStop(view, v);
        return;
    }
    v->playing = what;
    v->playStep = 0;
    RsView_PlayApply(view, v);
    RsView_AnimTimer(view, v, IsWindowVisible(view));
}

int RsView_AnimTicks(HWND view, int ticks)
{
    struct RsView *v = RsView_Data(view);
    int i;
    if (!v || v->playing < 0 || ticks <= 0)
        return 0;
    for (i = 0; i < ticks; i++) {
        v->playStep++;
        RsView_PlayApply(view, v);
    }
    return ticks;
}

int RsView_AnimInfo(HWND view, struct RsViewAnimInfo *info)
{
    struct RsView *v = RsView_Data(view);
    memset(info, 0, sizeof(*info));
    info->playing = v ? v->playing : -1;
    if (v) {
        info->anim = v->anim;
        info->frame = v->frame;
        info->end = v->endPose;
        info->weight = v->endWeight;
    }
    if (!v || !v->loaded)
        return 0;
    info->classicFrames = v->poseCount;
    info->nativeFrames = v->native.triangles > 0 ? v->native.poses : 0;
    info->nativeEnd = (v->native.triangles > 0 && v->native.end[RS_TEX_END_WIN] ? 1 : 0) |
                      (v->native.triangles > 0 && v->native.end[RS_TEX_END_LOSE] ? 2 : 0);
    info->drawsNative = RsView_DrawsNative(v);
    return 1;
}
