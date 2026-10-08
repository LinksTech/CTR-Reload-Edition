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
// machine and font settings.
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
// Camera slightly from above (RS_VIEW_PITCH), perspective. Two things stand
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
// dummy stands alone in the middle.
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
// Only with --enable-preview-features does the page load more into the view:
// a pose set (RLDPS1, drawn instead of the model's pose while one is chosen)
// and a wheel model (RLDPW1, drawn under the model at the four wheel points
// of Rs_DummyTires instead of the game's wheels, by the same rule: never over
// the model). The dummy beside it keeps the game's wheels. The wheel turns by
// whole degrees from the same sine table; its animation runs on the view's
// own timer in fixed steps per tick. Nothing loaded: nothing of it is used,
// and the picture is the same as without these features.
//
// THE NATIVE MODEL (preview feature, renderer step 5b)
//
// With make-char --native-model on, rldpack appends the native model (CNET
// with its CTXT textures, checked and decoded by rldpack: "RLDPN1", described
// at THE NATIVE MODEL IN THE PREVIEW in tools/rldpack_native.inc) to the
// RLDPV2 data of --preview. Only with --enable-preview-features does the view
// accept that block; it then draws the native model in place of the game's own
// model (in the same pose, the turn frames 10, 0 and 20), with "Native model"
// above it. The texture is sampled per pixel, nearest texel, perspective
// correct, all in integers: the weights of the corners in Q16, u and v
// (Q16 of the texture) times the corner depth, divided by the interpolated
// depth; wrap repeat, clamp or mirror per CTXT flags; the texel times the
// corner colour (COL0 times the material colour). Alpha mode mask and blend:
// a texel below alpha 128 is left out (no blending in the view). Back faces
// are culled (CNET winds counter-clockwise from outside). No light, like the
// model of the game. Without the block - and always without the switch - the
// view parses, frames and draws exactly as before.

#include "reloadstudio.h"
#include "rs_view.h"
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
#define RS_VIEW_NATIVE_TEX_MAX 16                       // textures of the native model (RLDPN1, CTXT holds at most 16)
#define RS_VIEW_NATIVE_TRI_BYTES 40                     // per triangle: 3 x {s32 u, s32 v, u8 r, g, b, pad}, s16 texture, u8 alpha, pad
#define RS_VIEW_NATIVE_POS_BYTES 18                     // per triangle and pose: 3 x s16 x, y, z
#define RS_VIEW_NATIVE_EDGE_MAX 2048
#define RS_VIEW_WHEEL_TRIS_MAX 16384                    // triangles of a wheel model (RLDPW1)
#define RS_VIEW_TIMER_WHEEL 1                           // the view's own timer: the wheel animation
#define RS_VIEW_WHEEL_TICK_MS 33
#define RS_VIEW_WHEEL_SPIN_STEP 12                      // degrees of spin per tick
#define RS_VIEW_WHEEL_STEER_STEP 2                      // degrees of steering per tick

// Crash with his kart in tenths of game units while the caller has given no
// box (RS_VIEW_DUMMY_CRASH_*, rs_view.h).
static const int s_rsViewCrashDefault[6] = {
    RS_VIEW_DUMMY_CRASH_X0, RS_VIEW_DUMMY_CRASH_Y0, RS_VIEW_DUMMY_CRASH_Z0,
    RS_VIEW_DUMMY_CRASH_X1, RS_VIEW_DUMMY_CRASH_Y1, RS_VIEW_DUMMY_CRASH_Z1,
};

static const unsigned char s_rsViewMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'V', '1', 0, 0 };
static const unsigned char s_rsViewMagic2[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'V', '2', 0, 0 };
static const unsigned char s_rsViewSetMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'S', '1', 0, 0 };
static const unsigned char s_rsViewWheelMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'W', '1', 0, 0 };
static const unsigned char s_rsViewNativeMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'N', '1', 0, 0 };

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

// The native model of the preview file (RLDPN1, preview feature); all
// pointers into RsView.data. count = 0: none.
struct RsViewNativeTex {
    int w, h;
    unsigned int flags;          // CTXT flags: wrap U bits 1-2, wrap V bits 3-4
    const unsigned char *rgba;   // w x h x 4, rows from the top
};

struct RsViewNative {
    int count;                                       // triangles
    const unsigned char *pos[RS_VIEW_POSE_COUNT];    // count x RS_VIEW_NATIVE_POS_BYTES each
    const unsigned char *attr;                       // count x RS_VIEW_NATIVE_TRI_BYTES
    int texCount;
    struct RsViewNativeTex tex[RS_VIEW_NATIVE_TEX_MAX];
};

struct RsView {
    // The model. data owns the bytes of the file; loaded = 0: none.
    unsigned char *data;
    struct RsViewPoseData poses[RS_VIEW_POSE_COUNT];
    struct RsViewNative native;   // preview feature: drawn in place of the model while count > 0
    int loaded;
    long long radius2;         // largest x*x + z*z over all poses (whole game units, rounded up)
    int ymin, ymax;            // over all poses (whole game units, rounded outwards)
    wchar_t message[RS_VIEW_MESSAGE_CAP];

    // The dummy (Rs_DummyMesh with its wheels), built again on every render:
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

    int pose;
    int yaw;                   // 0..359

    // A pose set (RsView_LoadPoseSet): setData owns the bytes, setCount = 0:
    // none. setIndex >= 0 draws that pose instead of the model's. Extents
    // like radius2, ymin, ymax, over the whole set.
    unsigned char *setData;
    struct RsViewPoseData setPoses[RS_VIEW_SET_POSES_MAX];
    int setCount;
    int setIndex;
    long long setRadius2;
    int setYmin, setYmax;

    // A wheel model (RsView_LoadWheelModel): wheelData owns the bytes,
    // wheelCount = 0: none - the game's wheels are drawn as before.
    // wheelR2: largest y*y + z*z, wheelHalfW: largest |x|, both of the file
    // (1/16 units, wheel-local). tire*: the dummy's wheel points (Rs_DummyTires).
    unsigned char *wheelData;
    const unsigned char *wheelTris;
    int wheelCount;
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

    int dragging;
    int dragX;
    int dragYaw;

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
    long long u, v;             // texture coordinates, Q16 (only the native model's)
};

// Everything the projection needs; filled by RsView_Scene.
struct RsViewCam {
    int yawSin, yawCos;         // Q14
    int pitchSin, pitchCos;     // Q14
    long long ycq;              // height the camera looks at, 1/16 units
    long long dq;               // camera distance, 1/16 units
    long long fq;               // focal length in 1/16 pixels
    long long cxq, cyq;         // screen centre in 1/16 pixels
};

struct RsViewScene {
    struct RsViewCam cam;
    long long offModel;         // sideways offsets after the turn, 1/16 units
    long long offDummy;
    long long floorX, floorZ;   // half width and half depth of the floor, 1/16 units
    long long floorY;           // its height, 1/16 units
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

// floor(v / 16) also for negative v (C division truncates towards zero).
static long long RsView_FloorDiv16(long long v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
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
    Rs_Free(v->data);
    v->data = NULL;
    memset(v->poses, 0, sizeof(v->poses));
    memset(&v->native, 0, sizeof(v->native));
    v->loaded = 0;
    v->radius2 = 0;
    v->ymin = 0;
    v->ymax = 0;
}

// One corner of a pose into the extents of the framing: radius2 the largest
// x*x + z*z in whole game units (rounded up), ymin/ymax in whole game units
// (rounded outwards). sub: 1/16 units per file unit (RsViewPoseData.sub); for
// whole units (sub = RS_VIEW_SUB) the numbers are the file's own.
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

// The native model behind the RLDPV2 data (preview feature): "RLDPN1", the
// textures, the triangles. Every length checked before it is used; 1 = the
// block fills data[at..bytes) exactly.
static int RsView_ParseNative(const unsigned char *data, size_t bytes, size_t at, struct RsViewNative *out,
                              wchar_t *why, int whyCap)
{
    unsigned int count, tris, poses, t;
    int p;

    memset(out, 0, sizeof(*out));
    if (bytes - at < RS_VIEW_MAGIC_BYTES + 4 || memcmp(data + at, s_rsViewNativeMagic, RS_VIEW_MAGIC_BYTES) != 0) {
        swprintf(why, whyCap, L"%u bytes follow after the last pose", (unsigned)(bytes - at));
        return 0;
    }
    at += RS_VIEW_MAGIC_BYTES;
    count = RsView_ReadU32(data + at);
    at += 4;
    if (count > RS_VIEW_NATIVE_TEX_MAX) {
        swprintf(why, whyCap, L"the native model has %u textures", count);
        return 0;
    }
    for (t = 0; t < count; t++) {
        unsigned int w, h;
        if (bytes - at < 12) {
            swprintf(why, whyCap, L"the native model ends in texture %u", t);
            return 0;
        }
        w = RsView_ReadU32(data + at);
        h = RsView_ReadU32(data + at + 4);
        if (w == 0 || h == 0 || w > RS_VIEW_NATIVE_EDGE_MAX || h > RS_VIEW_NATIVE_EDGE_MAX ||
            (size_t)w * h * 4 > bytes - at - 12) {
            swprintf(why, whyCap, L"texture %u of the native model is %u x %u or ends early", t, w, h);
            return 0;
        }
        out->tex[t].w = (int)w;
        out->tex[t].h = (int)h;
        out->tex[t].flags = RsView_ReadU32(data + at + 8);
        out->tex[t].rgba = data + at + 12;
        at += 12 + (size_t)w * h * 4;
    }
    out->texCount = (int)count;
    if (bytes - at < 8) {
        swprintf(why, whyCap, L"the native model ends before its triangles");
        return 0;
    }
    tris = RsView_ReadU32(data + at);
    poses = RsView_ReadU32(data + at + 4);
    at += 8;
    // Compared by division: a damaged count could wrap a product.
    if (poses != RS_VIEW_POSE_COUNT || tris > (bytes - at) / (RS_VIEW_POSE_COUNT * RS_VIEW_NATIVE_POS_BYTES + RS_VIEW_NATIVE_TRI_BYTES)) {
        swprintf(why, whyCap, L"the native model claims %u triangles in %u poses", tris, poses);
        return 0;
    }
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++) {
        out->pos[p] = data + at;
        at += (size_t)tris * RS_VIEW_NATIVE_POS_BYTES;
    }
    out->attr = data + at;
    at += (size_t)tris * RS_VIEW_NATIVE_TRI_BYTES;
    if (at != bytes) {
        swprintf(why, whyCap, L"%u bytes follow after the native model", (unsigned)(bytes - at));
        return 0;
    }
    for (t = 0; t < tris; t++) {
        const unsigned char *a = out->attr + (size_t)t * RS_VIEW_NATIVE_TRI_BYTES;
        const int tex = RsView_ReadS16(a + 36);
        if (tex < -1 || tex >= (int)count) {
            swprintf(why, whyCap, L"triangle %u of the native model names texture %d", t, tex);
            return 0;
        }
    }
    out->count = (int)tris;
    return 1;
}

// Checks every length before it is used; takes data on success (then it belongs
// to v), leaves v untouched and why filled otherwise.
static int RsView_Parse(struct RsView *v, unsigned char *data, size_t bytes, wchar_t *why, int whyCap)
{
    struct RsViewPoseData poses[RS_VIEW_POSE_COUNT];
    struct RsViewNative native;
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
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++) {
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
    // Behind the poses only the native model, and only with the preview
    // features; without them a byte more is an error as it always was.
    memset(&native, 0, sizeof(native));
    if (at != bytes) {
        if (!g_rsPreviewFeatures) {
            swprintf(why, whyCap, L"%u bytes follow after the last pose", (unsigned)(bytes - at));
            return 0;
        }
        if (!RsView_ParseNative(data, bytes, at, &native, why, whyCap))
            return 0;
    }

    // Extent over all poses, for a framing that does not change with the pose.
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++)
        for (i = 0; i < poses[p].count; i++)
            for (c = 0; c < 3; c++)
                RsView_ExtentAdd(poses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES + c * RS_VIEW_CORNER_BYTES, sub,
                                 &radius2, &ymin, &ymax, &any);
    // The native model in it (its corners are s16 x, y, z as those of RLDPV2).
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++)
        for (i = 0; i < 3 * native.count; i++)
            RsView_ExtentAdd(native.pos[p] + (size_t)i * 6, 1, &radius2, &ymin, &ymax, &any);

    RsView_DropModel(v);
    v->data = data;
    memcpy(v->poses, poses, sizeof(poses));
    v->native = native;
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
// Both files carry the triangle records of RLDPV1 (RsView_Parse): a pose set
// ("RLDPS1") in whole game units like an RLDPV1 model, a wheel model ("RLDPW1") in
// 1/16 game units about its axle. Without them nothing below is used and the
// view draws as before.

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
    Rs_Free(v->wheelData);
    v->wheelData = NULL;
    v->wheelTris = NULL;
    v->wheelCount = 0;
    v->wheelR2 = 0;
    v->wheelHalfW = 0;
    v->wheelRadius2 = 0;
    v->wheelYmin = 0;
    v->wheelYmax = 0;
}

// A wheel model; takes data on success, leaves v untouched otherwise.
static int RsView_ParseWheel(struct RsView *v, unsigned char *data, size_t bytes, wchar_t *why, int whyCap)
{
    struct RsViewPoseData tris;
    size_t at = RS_VIEW_MAGIC_BYTES;
    long long r2 = 0;
    int i, c, halfW = 0;

    if (bytes < RS_VIEW_MAGIC_BYTES + 4 || memcmp(data, s_rsViewWheelMagic, RS_VIEW_MAGIC_BYTES) != 0) {
        swprintf(why, whyCap, L"it is not a wheel model (it does not start with RLDPW1)");
        return 0;
    }
    if (!RsView_ParseTris(data, bytes, &at, &tris) || at != bytes) {
        swprintf(why, whyCap, L"its length does not match its triangle count");
        return 0;
    }
    if (tris.count < 1 || tris.count > RS_VIEW_WHEEL_TRIS_MAX) {
        swprintf(why, whyCap, L"it has %d triangles, 1..%d are possible", tris.count, RS_VIEW_WHEEL_TRIS_MAX);
        return 0;
    }
    for (i = 0; i < tris.count; i++)
        for (c = 0; c < 3; c++) {
            const unsigned char *q = tris.tris + (size_t)i * RS_VIEW_TRI_BYTES + c * RS_VIEW_CORNER_BYTES;
            const long long x = RsView_ReadS16(q), y = RsView_ReadS16(q + 2), z = RsView_ReadS16(q + 4);
            const long long ax = x < 0 ? -x : x;
            if (y * y + z * z > r2)
                r2 = y * y + z * z;
            if (ax > halfW)
                halfW = (int)ax;
        }

    RsView_DropWheel(v);
    v->wheelData = data;
    v->wheelTris = tris.tris;
    v->wheelCount = tris.count;
    v->wheelR2 = r2;
    v->wheelHalfW = halfW;
    return 1;
}

// The wheel model at its scale about a wheel point, 1/16 units: radius = how
// far it reaches up and down, reach = across. Under every spin it stays in a
// cylinder of its radius and half width; under every steering angle that
// stays in a circle of radius reach about the point.
static void RsView_WheelReach(const struct RsView *v, long long *radius, long long *reach)
{
    const long long r = ((long long)RsView_Isqrt((unsigned long long)v->wheelR2) + 1) * v->wheelScale / 100 + 1;
    const long long hw = (long long)v->wheelHalfW * v->wheelScale / 100 + 1;
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
        const long long x = v->tireAt[i][0], y = v->tireAt[i][1], z = v->tireAt[i][2];
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

// The model's span across the picture (all poses) with the game's wheels
// under it - whether they are shown or not, so that switching them never zooms.
static void RsView_ModelSpans(struct RsView *v)
{
    struct RsViewTrig tr;
    int p, i, c, positions = 0;

    RsView_TrigFill(&tr);
    RsView_SpanReset(v->modelLo, v->modelHi);
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++) {
        for (i = 0; i < v->poses[p].count; i++) {
            const unsigned char *t = v->poses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES;
            for (c = 0; c < 3; c++) {
                const unsigned char *q = t + c * RS_VIEW_CORNER_BYTES;
                RsView_SpanAdd(&tr, v->modelLo, v->modelHi, (long long)RsView_ReadS16(q) * v->poses[p].sub,
                               (long long)RsView_ReadS16(q + 4) * v->poses[p].sub);
            }
        }
    }
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++)
        for (i = 0; i < 3 * v->native.count; i++) {
            const unsigned char *q = v->native.pos[p] + (size_t)i * 6;
            RsView_SpanAdd(&tr, v->modelLo, v->modelHi, RsView_ReadS16(q), RsView_ReadS16(q + 4));
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
        for (i = 0; i < 4; i++)
            for (c = 0; c < 4; c++)
                RsView_SpanAdd(&tr, v->modelLo, v->modelHi, v->tireAt[i][0] + ((c & 1) ? reach : -reach),
                               v->tireAt[i][2] + ((c & 2) ? reach : -reach));
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
static void RsView_Project(const struct RsViewCam *cam, long long x, long long y, long long z,
                           long long off, int turn, struct RsViewVert *out)
{
    long long x1 = x, z1 = z, y2, z2, d;
    if (turn) {
        x1 = (x * cam->yawCos + z * cam->yawSin) / 16384;
        z1 = (z * cam->yawCos - x * cam->yawSin) / 16384;
    }
    x1 += off;
    y -= cam->ycq;
    y2 = (y * cam->pitchCos - z1 * cam->pitchSin) / 16384;
    z2 = (y * cam->pitchSin + z1 * cam->pitchCos) / 16384;
    d = cam->dq - z2;   // the framing keeps it above half the camera distance
    if (d < 1)
        d = 1;
    out->x = cam->cxq + x1 * cam->fq / d;
    out->y = cam->cyq - y2 * cam->fq / d;
    out->z = (int)((cam->dq << RS_VIEW_DEPTH_SHIFT) / d);
}

static int RsView_TopLeft(long long dx, long long dy)
{
    return dy < 0 || (dy == 0 && dx > 0);
}

// Fills a triangle whose edge function (b - a) x (c - a) is positive. Pixels are
// sampled at their centre; an edge pixel belongs to the triangle on its top or
// left edge (as Direct3D and OpenGL do), so that neighbouring triangles neither
// overlap nor leave a gap. Colour and depth are interpolated linearly in screen
// space - affine Gouraud like the PS1 GPU; the depth is 1/distance, which is
// linear in screen space.
static void RsView_Fill(const struct RsViewTarget *t, const struct RsViewVert *a,
                        const struct RsViewVert *b, const struct RsViewVert *c)
{
    const long long area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    long long px0, px1, py0, py1, px, py;
    long long sx = 0, sy = 0;
    long long e0Row, e1Row, e2Row;
    long long e0dx, e1dx, e2dx, e0dy, e1dy, e2dy;
    int bias0, bias1, bias2;

    // Pixel range: centres 16 * p + 8 inside the bounding box.
    px0 = -RsView_FloorDiv16(-(RsView_Min3(a->x, b->x, c->x) - 8));
    px1 = RsView_FloorDiv16(RsView_Max3(a->x, b->x, c->x) - 8);
    py0 = -RsView_FloorDiv16(-(RsView_Min3(a->y, b->y, c->y) - 8));
    py1 = RsView_FloorDiv16(RsView_Max3(a->y, b->y, c->y) - 8);
    if (px0 < 0)
        px0 = 0;
    if (py0 < 0)
        py0 = 0;
    if (px1 > t->w - 1)
        px1 = t->w - 1;
    if (py1 > t->h - 1)
        py1 = t->h - 1;
    if (px0 > px1 || py0 > py1)
        return;

    // Edge i is the one opposite corner i; its value is the weight of corner i.
    e0dx = -(c->y - b->y) * RS_VIEW_SUB;
    e1dx = -(a->y - c->y) * RS_VIEW_SUB;
    e2dx = -(b->y - a->y) * RS_VIEW_SUB;
    e0dy = (c->x - b->x) * RS_VIEW_SUB;
    e1dy = (a->x - c->x) * RS_VIEW_SUB;
    e2dy = (b->x - a->x) * RS_VIEW_SUB;
    bias0 = RsView_TopLeft(c->x - b->x, c->y - b->y) ? 0 : -1;
    bias1 = RsView_TopLeft(a->x - c->x, a->y - c->y) ? 0 : -1;
    bias2 = RsView_TopLeft(b->x - a->x, b->y - a->y) ? 0 : -1;

    sx = px0 * RS_VIEW_SUB + 8;
    sy = py0 * RS_VIEW_SUB + 8;
    e0Row = (c->x - b->x) * (sy - b->y) - (c->y - b->y) * (sx - b->x);
    e1Row = (a->x - c->x) * (sy - c->y) - (a->y - c->y) * (sx - c->x);
    e2Row = (b->x - a->x) * (sy - a->y) - (b->y - a->y) * (sx - a->x);

    for (py = py0; py <= py1; py++) {
        long long e0 = e0Row, e1 = e1Row, e2 = e2Row;
        unsigned int *pix = t->pixels + (size_t)py * (size_t)t->w;
        int *dep = t->depth + (size_t)py * (size_t)t->w;
        unsigned char *own = t->mask + (size_t)py * (size_t)t->w;
        for (px = px0; px <= px1; px++) {
            if (e0 + bias0 >= 0 && e1 + bias1 >= 0 && e2 + bias2 >= 0 &&
                !(t->maskMode == RS_VIEW_MASK_SKIP && own[px])) {
                // e0 + e1 + e2 == area and all three >= 0 here, so every sum
                // below is a weighted mean without overflow (area < 2^40,
                // depth < 2^21).
                const int z = (int)((e0 * a->z + e1 * b->z + e2 * c->z) / area);
                if (z > dep[px]) {
                    const long long half = area / 2;
                    const unsigned int r = (unsigned int)((e0 * a->r + e1 * b->r + e2 * c->r + half) / area);
                    const unsigned int g = (unsigned int)((e0 * a->g + e1 * b->g + e2 * c->g + half) / area);
                    const unsigned int bl = (unsigned int)((e0 * a->b + e1 * b->b + e2 * c->b + half) / area);
                    dep[px] = z;
                    pix[px] = (r << 16) | (g << 8) | bl;
                    if (t->maskMode == RS_VIEW_MASK_SET)
                        own[px] = 1;
                }
            }
            e0 += e0dx;
            e1 += e1dx;
            e2 += e2dx;
        }
        e0Row += e0dy;
        e1Row += e1dy;
        e2Row += e2dy;
    }
}

// One texel coordinate by the wrap of CTXT (0 repeat, 1 clamp, 2 mirror):
// q is Q16 of the texture, n its texels on that axis.
static int RsView_TexAt(long long q, int n, unsigned int wrap)
{
    long long t = q * n;
    t = t >= 0 ? t >> 16 : -((-t + 65535) >> 16);   // floor
    if (wrap == 1)
        return t < 0 ? 0 : (t >= n ? n - 1 : (int)t);
    if (wrap == 2) {
        long long m = t % (2LL * n);
        if (m < 0)
            m += 2LL * n;
        return (int)(m < n ? m : 2LL * n - 1 - m);
    }
    t %= n;
    return (int)(t < 0 ? t + n : t);
}

// RsView_Fill with a texture (the native model): the same edges, depth and
// Gouraud colour; u and v perspective correct (weights in Q16 times u x z,
// divided by the same sum of z). alphaMode 1 or 2: a texel below 128 is left
// out. Products stay below 2^63: weights 2^16, z below 2^21, u and v within
// +-2^22 (64 repeats, RLDPN1).
static void RsView_FillTex(const struct RsViewTarget *t, const struct RsViewVert *a, const struct RsViewVert *b,
                           const struct RsViewVert *c, const struct RsViewNativeTex *tex, int alphaMode)
{
    const long long area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    const long long auz = a->u * a->z, buz = b->u * b->z, cuz = c->u * c->z;
    const long long avz = a->v * a->z, bvz = b->v * b->z, cvz = c->v * c->z;
    const unsigned int wrapU = (tex->flags >> 1) & 3u, wrapV = (tex->flags >> 3) & 3u;
    long long px0, px1, py0, py1, px, py;
    long long sx, sy;
    long long e0Row, e1Row, e2Row;
    long long e0dx, e1dx, e2dx, e0dy, e1dy, e2dy;
    int bias0, bias1, bias2;

    px0 = -RsView_FloorDiv16(-(RsView_Min3(a->x, b->x, c->x) - 8));
    px1 = RsView_FloorDiv16(RsView_Max3(a->x, b->x, c->x) - 8);
    py0 = -RsView_FloorDiv16(-(RsView_Min3(a->y, b->y, c->y) - 8));
    py1 = RsView_FloorDiv16(RsView_Max3(a->y, b->y, c->y) - 8);
    if (px0 < 0)
        px0 = 0;
    if (py0 < 0)
        py0 = 0;
    if (px1 > t->w - 1)
        px1 = t->w - 1;
    if (py1 > t->h - 1)
        py1 = t->h - 1;
    if (px0 > px1 || py0 > py1)
        return;

    e0dx = -(c->y - b->y) * RS_VIEW_SUB;
    e1dx = -(a->y - c->y) * RS_VIEW_SUB;
    e2dx = -(b->y - a->y) * RS_VIEW_SUB;
    e0dy = (c->x - b->x) * RS_VIEW_SUB;
    e1dy = (a->x - c->x) * RS_VIEW_SUB;
    e2dy = (b->x - a->x) * RS_VIEW_SUB;
    bias0 = RsView_TopLeft(c->x - b->x, c->y - b->y) ? 0 : -1;
    bias1 = RsView_TopLeft(a->x - c->x, a->y - c->y) ? 0 : -1;
    bias2 = RsView_TopLeft(b->x - a->x, b->y - a->y) ? 0 : -1;

    sx = px0 * RS_VIEW_SUB + 8;
    sy = py0 * RS_VIEW_SUB + 8;
    e0Row = (c->x - b->x) * (sy - b->y) - (c->y - b->y) * (sx - b->x);
    e1Row = (a->x - c->x) * (sy - c->y) - (a->y - c->y) * (sx - c->x);
    e2Row = (b->x - a->x) * (sy - a->y) - (b->y - a->y) * (sx - a->x);

    for (py = py0; py <= py1; py++) {
        long long e0 = e0Row, e1 = e1Row, e2 = e2Row;
        unsigned int *pix = t->pixels + (size_t)py * (size_t)t->w;
        int *dep = t->depth + (size_t)py * (size_t)t->w;
        unsigned char *own = t->mask + (size_t)py * (size_t)t->w;
        for (px = px0; px <= px1; px++) {
            if (e0 + bias0 >= 0 && e1 + bias1 >= 0 && e2 + bias2 >= 0 &&
                !(t->maskMode == RS_VIEW_MASK_SKIP && own[px])) {
                const int z = (int)((e0 * a->z + e1 * b->z + e2 * c->z) / area);
                if (z > dep[px]) {
                    const long long half = area / 2;
                    const long long w0 = (e0 << 16) / area, w1 = (e1 << 16) / area, w2 = (e2 << 16) / area;
                    const long long zs = w0 * a->z + w1 * b->z + w2 * c->z;
                    const long long uq = zs > 0 ? (w0 * auz + w1 * buz + w2 * cuz) / zs : a->u;
                    const long long vq = zs > 0 ? (w0 * avz + w1 * bvz + w2 * cvz) / zs : a->v;
                    const unsigned char *texel =
                        tex->rgba + ((size_t)RsView_TexAt(vq, tex->h, wrapV) * (size_t)tex->w +
                                     (size_t)RsView_TexAt(uq, tex->w, wrapU)) * 4;
                    if (alphaMode == 0 || texel[3] >= 128) {
                        const unsigned int r = (unsigned int)((e0 * a->r + e1 * b->r + e2 * c->r + half) / area);
                        const unsigned int g = (unsigned int)((e0 * a->g + e1 * b->g + e2 * c->g + half) / area);
                        const unsigned int bl = (unsigned int)((e0 * a->b + e1 * b->b + e2 * c->b + half) / area);
                        dep[px] = z;
                        pix[px] = (((texel[0] * r + 127) / 255) << 16) | (((texel[1] * g + 127) / 255) << 8) |
                                  ((texel[2] * bl + 127) / 255);
                        if (t->maskMode == RS_VIEW_MASK_SET)
                            own[px] = 1;
                    }
                }
            }
            e0 += e0dx;
            e1 += e1dx;
            e2 += e2dx;
        }
        e0Row += e0dy;
        e1Row += e1dy;
        e2Row += e2dy;
    }
}

// cull: draw only the visible side. In pixel rows (y down) a triangle that
// runs counter-clockwise as seen (right-hand normal towards the viewer) has a
// negative edge function.
static void RsView_Triangle(const struct RsViewTarget *t, const struct RsViewVert *v, int cull)
{
    long long area;
    int i;

    // Far outside the window only through a broken framing; skipping keeps the
    // edge functions below 2^40.
    for (i = 0; i < 3; i++)
        if (v[i].x < -RS_VIEW_COORD_MAX || v[i].x > RS_VIEW_COORD_MAX ||
            v[i].y < -RS_VIEW_COORD_MAX || v[i].y > RS_VIEW_COORD_MAX)
            return;
    area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
    if (area == 0)
        return;
    if (cull && area > 0)
        return;
    if (area < 0)
        RsView_Fill(t, &v[0], &v[2], &v[1]);
    else
        RsView_Fill(t, &v[0], &v[1], &v[2]);
}

// RsView_Triangle with a texture (the native model); tex NULL: untextured.
static void RsView_TriangleTex(const struct RsViewTarget *t, const struct RsViewVert *v, int cull,
                               const struct RsViewNativeTex *tex, int alphaMode)
{
    long long area;
    int i;

    if (!tex) {
        RsView_Triangle(t, v, cull);
        return;
    }
    for (i = 0; i < 3; i++)
        if (v[i].x < -RS_VIEW_COORD_MAX || v[i].x > RS_VIEW_COORD_MAX ||
            v[i].y < -RS_VIEW_COORD_MAX || v[i].y > RS_VIEW_COORD_MAX)
            return;
    area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
    if (area == 0)
        return;
    if (cull && area > 0)
        return;
    if (area < 0)
        RsView_FillTex(t, &v[0], &v[2], &v[1], tex, alphaMode);
    else
        RsView_FillTex(t, &v[0], &v[1], &v[2], tex, alphaMode);
}

// ---------------------------------------------------------------------------
// The scene
// ---------------------------------------------------------------------------

// Framing from the extents only (not from the yaw, the pose or the wheels).
// Across the picture the spans give, for every yaw, where model and dummy
// reach; the dummy stands so far to the right that the two keep RS_VIEW_GAP
// between them at the worst yaw, and the pair is centred. In depth both fit
// in a cylinder about their vertical axis, so any turn stays inside. topH:
// room kept free at the top for text.
static void RsView_Scene(const struct RsView *v, int w, int h, int topH, struct RsViewScene *s)
{
    long long rModel = (long long)RsView_Isqrt((unsigned long long)v->radius2) + 1;
    long long rTire = (long long)RsView_Isqrt((unsigned long long)v->tireRadius2) + 1;
    long long rDummy = (long long)RsView_Isqrt((unsigned long long)v->dumRadius2) + 1;
    long long rMax, halfW, ylo, yhi, ey, ey2, ez2, rScene, dist, dmin, hw, hh, fx, fy;
    long long floorX, floorZ, left, right, sep = 0, lo, hi, gapNeed;
    const int margin = Rs_Px(12);
    const int ps = RsView_Sin(RS_VIEW_PITCH), pc = RsView_Cos(RS_VIEW_PITCH);
    int d;

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
        gapNeed = v->modelHi[0] - v->dumLo[0];
        lo = v->modelLo[0];
        for (d = 1; d < 360; d++) {
            if (v->modelHi[d] - v->dumLo[d] > gapNeed)
                gapNeed = v->modelHi[d] - v->dumLo[d];
            if (v->modelLo[d] < lo)
                lo = v->modelLo[d];
        }
        sep = RsView_SpanUnitsCeil(gapNeed) + RS_VIEW_GAP;
        left = RsView_SpanUnitsFloor(lo);
        right += sep;
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
    s->cam.pitchSin = ps;
    s->cam.pitchCos = pc;
    s->cam.ycq = (ylo + yhi) * RS_VIEW_SUB / 2;
    s->cam.dq = dist * RS_VIEW_SUB;
    s->cam.fq = fx < fy ? fx : fy;
    if (s->cam.fq < 1)
        s->cam.fq = 1;
    s->cam.cxq = (long long)w * RS_VIEW_SUB / 2;
    s->cam.cyq = (long long)(h + topH) * RS_VIEW_SUB / 2;   // the room for text is at the top
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

// Closed solids of the dummy, shaded per face, without culling: the
// triangles whose corners all lie in the positions lo..hi-1, turned and then
// moved sideways by off.
static void RsView_DrawSolid(const struct RsViewTarget *t, const struct RsViewScene *s, long long off,
                             const int *pos, int positions, const int *tri, const unsigned int *rgbs, int tris,
                             int lo, int hi)
{
    int i, k;
    for (i = 0; i < tris; i++) {
        struct RsViewVert vert[3];
        const int *corner[3];
        const unsigned int rgb = rgbs[i];
        int shade, inside = 1;
        for (k = 0; k < 3; k++) {
            const int at = tri[3 * i + k];
            if (at < 0 || at >= positions)
                return;   // cannot happen; never read past the buffer
            if (at < lo || at >= hi)
                inside = 0;
            corner[k] = pos + 3 * at;
        }
        if (!inside)
            continue;
        shade = RsView_DummyShade(corner[0], corner[1], corner[2]);
        for (k = 0; k < 3; k++) {
            RsView_Project(&s->cam, corner[k][0], corner[k][1], corner[k][2], off, 1, &vert[k]);
            vert[k].r = (int)((rgb >> 16) & 0xFF) * shade / 100;
            vert[k].g = (int)((rgb >> 8) & 0xFF) * shade / 100;
            vert[k].b = (int)(rgb & 0xFF) * shade / 100;
        }
        RsView_Triangle(t, vert, 0);
    }
}

// The wheel model at the four wheel points under the model (instead of the
// game's wheels), each corner: spin about +X (rolling forward: the top goes
// to +Z), on the -X side mirrored (x -> -x, corners 1 and 2 swapped, so the
// rim faces outwards and the culling stays right), scaled, the front pair
// steered about +Y (+ = left: the front edge toward +X), moved to its point.
// Colours 1:1 as in the file, like the model; pad bit 0 = both sides.
static void RsView_DrawWheels(const struct RsView *v, const struct RsViewTarget *t, const struct RsViewScene *s)
{
    const long long ss = RsView_Sin(v->wheelSpin), sc = RsView_Cos(v->wheelSpin);
    const long long ps = RsView_Sin(v->wheelSteer), pc = RsView_Cos(v->wheelSteer);
    int w, i, c;

    for (w = 0; w < 4; w++) {
        const int mirror = v->tireAt[w][0] < 0;
        const int front = w < 2;
        for (i = 0; i < v->wheelCount; i++) {
            const unsigned char *p = v->wheelTris + (size_t)i * RS_VIEW_TRI_BYTES;
            struct RsViewVert vert[3];
            for (c = 0; c < 3; c++) {
                const unsigned char *q = p + (mirror && c ? 3 - c : c) * RS_VIEW_CORNER_BYTES;
                const long long x0 = RsView_ReadS16(q), y0 = RsView_ReadS16(q + 2), z0 = RsView_ReadS16(q + 4);
                long long x = mirror ? -x0 : x0;
                long long y = (y0 * sc - z0 * ss) / 16384;
                long long z = (y0 * ss + z0 * sc) / 16384;
                x = x * v->wheelScale / 100;
                y = y * v->wheelScale / 100;
                z = z * v->wheelScale / 100;
                if (front) {
                    const long long xs = (x * pc + z * ps) / 16384;
                    z = (z * pc - x * ps) / 16384;
                    x = xs;
                }
                RsView_Project(&s->cam, x + v->tireAt[w][0], y + v->tireAt[w][1], z + v->tireAt[w][2], s->offModel,
                               1, &vert[c]);
                vert[c].r = q[6];
                vert[c].g = q[7];
                vert[c].b = q[8];
            }
            RsView_Triangle(t, vert, !(p[9] & 1));
        }
    }
}

// The game's wheels under the model (only into pixels the model left free),
// then the dummy beside it: kart and driver in the pose of the view.
static void RsView_DrawDummy(struct RsView *v, const struct RsViewTarget *t, const struct RsViewScene *s)
{
    struct RsViewTarget own = *t;
    struct RsViewDriver driver;
    int positions = 0;
    const int tris = Rs_DummyMesh(1, v->pose, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor,
                                  RS_VIEW_DUMMY_TRI_MAX, &positions);

    if (v->loaded && v->wheels && v->wheelCount) {
        own.maskMode = RS_VIEW_MASK_SKIP;
        RsView_DrawWheels(v, &own, s);
    } else if (v->loaded && v->wheels && v->tireEnd > v->tireFirst && v->tireEnd <= positions) {
        own.maskMode = RS_VIEW_MASK_SKIP;
        RsView_DrawSolid(&own, s, s->offModel, v->dumPos, positions, v->dumTri, v->dumColor, tris, v->tireFirst,
                         v->tireEnd);
    }
    own.maskMode = RS_VIEW_MASK_NONE;
    RsView_DrawSolid(&own, s, s->offDummy, v->dumPos, positions, v->dumTri, v->dumColor, tris, 0, positions);
    RsView_DriverMesh(v, v->pose, &driver);
    RsView_DrawSolid(&own, s, s->offDummy, driver.pos, driver.positions, driver.tri, driver.rgb, driver.triangles, 0,
                     driver.positions);
}

static void RsView_DrawText(struct RsView *v, const RECT *rc, const wchar_t *text, int font, UINT format)
{
    HGDIOBJ oldFont;
    RECT r = *rc;
    if (!text || !*text)
        return;
    SetBkMode(v->mem, TRANSPARENT);
    SetTextColor(v->mem, RS_COL_MUTED);
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

// The triangles drawn for the model: a pose of the pose set while one is
// chosen, else the pose of the preview file.
static const struct RsViewPoseData *RsView_ShownPose(const struct RsView *v)
{
    if (v->setIndex >= 0 && v->setIndex < v->setCount)
        return &v->setPoses[v->setIndex];
    return &v->poses[v->pose];
}

static void RsView_Render(HWND view, struct RsView *v)
{
    struct RsViewTarget t;
    struct RsViewScene s;
    const unsigned int bg = RsView_Pixel(RS_COL_PAGE);
    const int labelH = Rs_Px(20);
    // Room at the top: the label above the dummy, and the window text while
    // the dummy stands alone.
    const int topH = v->loaded ? labelH : labelH + Rs_Px(36);
    size_t i, n = (size_t)v->w * (size_t)v->h;
    wchar_t text[RS_VIEW_MESSAGE_CAP];
    int k;
    // The native model (preview feature) is timed: its triangles and the
    // whole picture, into the automation log (D.4 5b measures the time per
    // preview picture). Nothing of it without a native model.
    LARGE_INTEGER renderStart, nativeStart, nativeEnd, renderEnd, freq;
    int nativeTimed = 0;

    QueryPerformanceCounter(&renderStart);
    nativeStart = renderStart;
    nativeEnd = renderStart;
    GdiFlush();   // GDI must be done with the DIB before its bits are written
    for (i = 0; i < n; i++) {
        v->pixels[i] = bg;
        v->depth[i] = 0;   // depth 0 = infinitely far
        v->mask[i] = 0;
    }
    v->dirty = 0;
    v->drawnPal = g_rsPal;
    v->drawnScale = Rs_Px(96);

    // A message instead of the picture (RsView_Clear, a preview that cannot be read).
    if (!v->loaded && v->message[0]) {
        RsView_CenterText(v, v->message);
        return;
    }
    if (v->w < 8 || v->h < 8)
        return;

    t.pixels = v->pixels;
    t.depth = v->depth;
    t.mask = v->mask;
    t.maskMode = RS_VIEW_MASK_NONE;
    t.w = v->w;
    t.h = v->h;
    RsView_Scene(v, v->w, v->h, topH, &s);

    // The floor: two triangles under everything, not turned with it.
    {
        struct RsViewVert q[4], tri[3];
        const COLORREF fc = RS_COL_BORDER;
        for (k = 0; k < 4; k++) {
            RsView_Project(&s.cam, (k & 1) ? s.floorX : -s.floorX, s.floorY, (k & 2) ? s.floorZ : -s.floorZ, 0, 0,
                           &q[k]);
            q[k].r = GetRValue(fc);
            q[k].g = GetGValue(fc);
            q[k].b = GetBValue(fc);
        }
        tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[3];
        RsView_Triangle(&t, tri, 0);
        tri[0] = q[0]; tri[1] = q[3]; tri[2] = q[2];
        RsView_Triangle(&t, tri, 0);
    }

    // The model, in file order; it marks its pixels. The native model
    // (preview feature) in its place while there is one and no pose of a
    // pose set is chosen.
    t.maskMode = RS_VIEW_MASK_SET;
    if (v->loaded && v->native.count > 0 && !(v->setIndex >= 0 && v->setIndex < v->setCount)) {
        const struct RsViewNative *nm = &v->native;
        const int pose = (v->pose >= 0 && v->pose < RS_VIEW_POSE_COUNT) ? v->pose : RS_VIEW_POSE_NEUTRAL;
        int tri, c;
        QueryPerformanceCounter(&nativeStart);
        nativeTimed = 1;
        for (tri = 0; tri < nm->count; tri++) {
            const unsigned char *pp = nm->pos[pose] + (size_t)tri * RS_VIEW_NATIVE_POS_BYTES;
            const unsigned char *at = nm->attr + (size_t)tri * RS_VIEW_NATIVE_TRI_BYTES;
            const int tex = RsView_ReadS16(at + 36);
            struct RsViewVert vert[3];
            for (c = 0; c < 3; c++) {
                const unsigned char *q = pp + c * 6;
                const unsigned char *k = at + c * 12;
                RsView_Project(&s.cam, RsView_ReadS16(q), RsView_ReadS16(q + 2), RsView_ReadS16(q + 4), s.offModel, 1,
                               &vert[c]);
                vert[c].u = (long long)(int)RsView_ReadU32(k);
                vert[c].v = (long long)(int)RsView_ReadU32(k + 4);
                vert[c].r = k[8];
                vert[c].g = k[9];
                vert[c].b = k[10];
            }
            RsView_TriangleTex(&t, vert, 1, tex >= 0 ? &nm->tex[tex] : NULL, at[38]);
        }
        QueryPerformanceCounter(&nativeEnd);
    } else if (v->loaded) {
        const struct RsViewPoseData *pose = RsView_ShownPose(v);
        int tri, c;
        for (tri = 0; tri < pose->count; tri++) {
            const unsigned char *p = pose->tris + (size_t)tri * RS_VIEW_TRI_BYTES;
            struct RsViewVert vert[3];
            for (c = 0; c < 3; c++) {
                const unsigned char *q = p + c * RS_VIEW_CORNER_BYTES;
                RsView_Project(&s.cam, (long long)RsView_ReadS16(q) * pose->sub,
                               (long long)RsView_ReadS16(q + 2) * pose->sub,
                               (long long)RsView_ReadS16(q + 4) * pose->sub, s.offModel, 1, &vert[c]);
                vert[c].r = q[6];
                vert[c].g = q[7];
                vert[c].b = q[8];
            }
            // pad bit 0: no cull bit in the game, drawn from both sides
            RsView_Triangle(&t, vert, !(p[9] & 1));
        }
    }

    // The game's wheels under the model (never over it), the dummy beside it.
    t.maskMode = RS_VIEW_MASK_NONE;
    RsView_DrawDummy(v, &t, &s);

    GdiFlush();   // the bits are done; GDI writes the text on top
    {
        // Above the dummy driver's head.
        const long long lo[3] = { RsView_TenthsSub(-RS_VIEW_DRIVER_HEAD_X), RsView_TenthsSub(RS_VIEW_DRIVER_NECK_TOP),
                                  RsView_TenthsSub(RS_VIEW_DRIVER_HEAD_Z0) };
        const long long hi[3] = { RsView_TenthsSub(RS_VIEW_DRIVER_HEAD_X), RsView_TenthsSub(RsView_DriverTop(v)),
                                  RsView_TenthsSub(RS_VIEW_DRIVER_HEAD_Z1) };
        RsView_BoxLabel(v, &s, lo, hi, s.offDummy, labelH, L"Crash size");
    }
    if (v->loaded && v->native.count > 0 && !(v->setIndex >= 0 && v->setIndex < v->setCount)) {
        // Above the native model: its box in the pose shown.
        const int pose = (v->pose >= 0 && v->pose < RS_VIEW_POSE_COUNT) ? v->pose : RS_VIEW_POSE_NEUTRAL;
        long long lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
        int i, a;
        for (i = 0; i < 3 * v->native.count; i++)
            for (a = 0; a < 3; a++) {
                const long long q = RsView_ReadS16(v->native.pos[pose] + (size_t)i * 6 + 2 * a);
                if (i == 0 || q < lo[a])
                    lo[a] = q;
                if (i == 0 || q > hi[a])
                    hi[a] = q;
            }
        RsView_BoxLabel(v, &s, lo, hi, s.offModel, labelH, L"Native model");
    }
    if (!v->loaded) {
        // The dummy alone: the window text above it.
        RECT r = { 0, 0, v->w, Rs_Px(28) };
        InflateRect(&r, -Rs_Px(16), 0);
        OffsetRect(&r, 0, Rs_Px(8));
        GetWindowTextW(view, text, RS_VIEW_MESSAGE_CAP);
        text[RS_VIEW_MESSAGE_CAP - 1] = 0;
        RsView_DrawText(v, &r, text, RS_FONT_BODY, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (RsView_ShownPose(v)->count == 0) {
        RsView_CenterText(v, L"This pose has no triangles.");
    }
    if (nativeTimed && Rs_Automating()) {
        QueryPerformanceCounter(&renderEnd);
        QueryPerformanceFrequency(&freq);
        Rs_AutoLog(L"  view: native model %d triangles, pose %d, %d x %d pixels: triangles %lld us, whole picture %lld us",
                   v->native.count, v->pose, v->w, v->h,
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

static void RsView_PaintTo(HWND view, struct RsView *v, HDC dc)
{
    RECT rc;
    GetClientRect(view, &rc);
    if (v && RsView_EnsureFrame(v, rc.right, rc.bottom)) {
        // Drawn again only when something changed - also the colour scheme
        // or the scale, which the shell switches without telling the
        // controls (a new dpi may keep the size in pixels).
        if (v->dirty || v->drawnPal != g_rsPal || v->drawnScale != Rs_Px(96))
            RsView_Render(view, v);
        BitBlt(dc, 0, 0, v->w, v->h, v->mem, 0, 0, SRCCOPY);
    } else if (rc.right > 0 && rc.bottom > 0) {
        HBRUSH br = CreateSolidBrush(RS_COL_PAGE);
        FillRect(dc, &rc, br);
        DeleteObject(br);
    }
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

static void RsView_SetYawFrom(HWND view, struct RsView *v, int degrees, int notify)
{
    const int yaw = RsView_NormDeg(degrees);
    if (yaw == v->yaw)
        return;
    v->yaw = yaw;
    RsView_Changed(view, v);
    if (notify) {
        HWND parent = GetParent(view);
        if (parent)
            SendMessageW(parent, WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(view), RS_VIEW_N_YAW), (LPARAM)view);
    }
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

static LRESULT CALLBACK RsView_Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct RsView *v = RsView_Data(hwnd);
    switch (msg) {
    case WM_CREATE:
        v = (struct RsView *)Rs_Alloc(sizeof(struct RsView));
        v->yaw = RS_VIEW_DEFAULT_YAW;
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
        Rs_DummyTires(v->tireAt, NULL);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        return 0;
    case WM_DESTROY:
        if (v) {
            RsView_WheelTimer(hwnd, v, 0);
            RsView_FreeFrame(v);
            RsView_DropModel(v);
            RsView_DropSet(v);
            RsView_DropWheel(v);
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
        if (v && !wParam)
            RsView_WheelTimer(hwnd, v, 0);
        break;
    case WM_TIMER:
        if (v && wParam == RS_VIEW_TIMER_WHEEL) {
            // Fixed steps per tick, not per time: hidden, minimized or
            // without a wheel model the timer stops (and the picture with
            // it) until the next paint starts it again.
            if (!v->wheelAnim || !v->wheelCount || !IsWindowVisible(hwnd) ||
                IsIconic(GetAncestor(hwnd, GA_ROOT))) {
                RsView_WheelTimer(hwnd, v, 0);
                return 0;
            }
            v->wheelSpin = RsView_NormDeg(v->wheelSpin + RS_VIEW_WHEEL_SPIN_STEP);
            if (v->wheelSteerStep == 0)
                v->wheelSteerStep = RS_VIEW_WHEEL_STEER_STEP;
            if (v->wheelSteer + v->wheelSteerStep > RS_VIEW_WHEEL_STEER_MAX ||
                v->wheelSteer + v->wheelSteerStep < -RS_VIEW_WHEEL_STEER_MAX)
                v->wheelSteerStep = -v->wheelSteerStep;
            v->wheelSteer += v->wheelSteerStep;
            if (v->wheelCount && v->loaded && v->wheels)
                RsView_Changed(hwnd, v);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc;
        if (v && v->wheelAnim && v->wheelCount && !v->wheelTimer)
            RsView_WheelTimer(hwnd, v, 1);   // shown again, or a wheel model came
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
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (v && RsView_Turnable(v)) {
            v->dragging = 1;
            v->dragX = (short)LOWORD(lParam);
            v->dragYaw = v->yaw;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (v && v->dragging && GetCapture() == hwnd) {
            // Half a turn per 360 pixels at 96 dpi; integer, so the same drag
            // gives the same angle.
            const int dx = (short)LOWORD(lParam) - v->dragX;
            RsView_SetYawFrom(hwnd, v, v->dragYaw + dx * 180 / Rs_Px(360), 1);
        }
        return 0;
    case WM_LBUTTONUP:
        if (v && v->dragging)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (v)
            v->dragging = 0;
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

void RsView_SetPose(HWND view, int pose)
{
    struct RsView *v = RsView_Data(view);
    if (!v)
        return;
    if (pose < 0)
        pose = 0;
    if (pose >= RS_VIEW_POSE_COUNT)
        pose = RS_VIEW_POSE_COUNT - 1;
    if (pose == v->pose)
        return;
    v->pose = pose;
    RsView_Changed(view, v);
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
    return v->poses[pose].count;
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
    if (!data || !RsView_ParseWheel(v, data, bytes, why, 256)) {
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
