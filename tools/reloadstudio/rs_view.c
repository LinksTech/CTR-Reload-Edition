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
// is fixed (floor, model in file order, dummy, Crash outline). The dummy
// comes from rldpack's own numbers in doubles (Rs_DummyMesh, rs_rldpack.c),
// computed without library functions and rounded to 1/16 units there. Only
// the text comes from GDI, which is the same for the same machine and font
// settings.
//
// THE AXES
//
// The preview carries model coordinates in game units, as rldpack builds them
// (the file format is described at "--preview" in tools/rldpack_char.inc:
// "+Y up, +Z forward, +X the driver's left"; RldMk_Axes maps the PLY axes by a
// proper rotation; the records are "X, Z (forward), Y (up)"): the ground is
// y = 0, the hip sits at y = 16 (RLDMK_HIP_Y), the retail kart's bottom at
// y = 6. The system is right-handed. The view keeps that: y is drawn upwards, so the
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
// does not imitate that.
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
// Camera slightly from above (RS_VIEW_PITCH), perspective, the model turning
// about its origin - the point the game turns a kart about - and standing on
// a floor at y = 0. The framing depends on all three poses, the dummy (with
// its wheels, in all three poses), the Crash outline and the floor, which
// stays inside the picture - not on the yaw, the pose or the wheels: turning,
// changing the pose or switching the wheels never zooms.
//
// THE DUMMY
//
// Under the model, in its coordinates (so it turns with it), the view draws
// the reference dummy of make-char (tools/rldpack_dummy.inc, RldDum_Mesh):
// the retail kart at Crash's size with his seat, his steering wheel (turned
// as in the pose) and, with RsView_SetWheels on, the four wheels the game
// draws for every driver - the game places them by the kart, not by the
// model. make-char fits every model onto this dummy (--fit crash), so the two
// lie on each other. The dummy NEVER covers the model: the model is drawn
// first and marks its pixels (RsViewTarget.mask), the dummy is drawn only
// into pixels the model left free, with the depth test against the floor and
// itself. So every pixel of the model is exactly as without the dummy (also
// the parts inside the dummy's walls, e.g. a model's own wheels), and the
// grey dummy shows only where the model leaves a gap. The dummy has one
// colour per triangle; it is shaded by a fixed light per face of the dummy,
// so its shape stays readable.
// No separate retail kart stands beside the model: the dummy is the measured
// retail kart in its real place, which compares better than a box next to it.
//
// The size of Crash with his kart stays as a dashed outline about the model
// (RsView_SetCrashBox): the dummy has no driver, the outline shows his height.

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

static const unsigned char s_rsViewMagic[RS_VIEW_MAGIC_BYTES] = { 'R', 'L', 'D', 'P', 'V', '1', 0, 0 };

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
};

struct RsView {
    // The model. data owns the bytes of the file; loaded = 0: none.
    unsigned char *data;
    struct RsViewPoseData poses[RS_VIEW_POSE_COUNT];
    int loaded;
    long long radius2;         // largest x*x + z*z over all poses (game units)
    int ymin, ymax;            // over all poses
    wchar_t message[RS_VIEW_MESSAGE_CAP];

    // The dummy (Rs_DummyMesh), built again on every render: positions in
    // 1/16 units, triangles, a colour per triangle. Its extent over all poses
    // with wheels, in game units, for the framing.
    int *dumPos;
    int *dumTri;
    unsigned int *dumColor;
    long long dumRadius2;
    int dumYmin, dumYmax;
    int wheels;                // the game's kart wheels on the dummy

    // Crash with his kart (tenths of game units), drawn as an outline.
    int hasCrash;
    int crash[6];              // x0 y0 z0 x1 y1 z1, lo <= hi

    int pose;
    int yaw;                   // 0..359

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
};

// One projected corner: position in 1/16 pixels, depth (larger = nearer), colour.
struct RsViewVert {
    long long x, y;
    int z;
    int r, g, b;
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
    long long offModel;         // sideways offset after the turn, 1/16 units
    long long floorX, floorZ;   // half width and half depth of the floor, 1/16 units
    long long floorY;           // its height, 1/16 units
};

// mask / maskMode: RS_VIEW_MASK_* - the model marks its pixels, the dummy
// leaves them alone.
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
// The preview file
// ---------------------------------------------------------------------------

static void RsView_DropModel(struct RsView *v)
{
    Rs_Free(v->data);
    v->data = NULL;
    memset(v->poses, 0, sizeof(v->poses));
    v->loaded = 0;
    v->radius2 = 0;
    v->ymin = 0;
    v->ymax = 0;
}

// Checks every length before it is used; takes data on success (then it belongs
// to v), leaves v untouched and why filled otherwise.
static int RsView_Parse(struct RsView *v, unsigned char *data, size_t bytes, wchar_t *why, int whyCap)
{
    struct RsViewPoseData poses[RS_VIEW_POSE_COUNT];
    size_t at;
    unsigned int count;
    int p, i, c;
    long long radius2 = 0;
    int ymin = 0, ymax = 0, any = 0;

    if (bytes < RS_VIEW_MAGIC_BYTES + 4) {
        swprintf(why, whyCap, L"the file is too short (%u bytes)", (unsigned)bytes);
        return 0;
    }
    if (memcmp(data, s_rsViewMagic, RS_VIEW_MAGIC_BYTES) != 0) {
        swprintf(why, whyCap, L"it is not a preview file (it does not start with RLDPV1)");
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
        at += (size_t)tris * RS_VIEW_TRI_BYTES;
    }
    if (at != bytes) {
        swprintf(why, whyCap, L"%u bytes follow after the last pose", (unsigned)(bytes - at));
        return 0;
    }

    // Extent over all poses, for a framing that does not change with the pose.
    for (p = 0; p < RS_VIEW_POSE_COUNT; p++) {
        for (i = 0; i < poses[p].count; i++) {
            const unsigned char *t = poses[p].tris + (size_t)i * RS_VIEW_TRI_BYTES;
            for (c = 0; c < 3; c++) {
                const unsigned char *q = t + c * RS_VIEW_CORNER_BYTES;
                long long x = RsView_ReadS16(q), y = RsView_ReadS16(q + 2), z = RsView_ReadS16(q + 4);
                long long r2 = x * x + z * z;
                if (r2 > radius2)
                    radius2 = r2;
                if (!any || y < ymin)
                    ymin = (int)y;
                if (!any || y > ymax)
                    ymax = (int)y;
                any = 1;
            }
        }
    }

    RsView_DropModel(v);
    v->data = data;
    memcpy(v->poses, poses, sizeof(poses));
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

// ---------------------------------------------------------------------------
// The scene
// ---------------------------------------------------------------------------

// Framing from the extents only (not from the yaw, the pose or the wheels):
// model, dummy and Crash outline fit in a cylinder about the vertical axis,
// so any turn stays inside.
static void RsView_Scene(const struct RsView *v, int w, int h, int labelH, struct RsViewScene *s)
{
    long long rModel = (long long)RsView_Isqrt((unsigned long long)v->radius2) + 1;
    long long rDummy = (long long)RsView_Isqrt((unsigned long long)v->dumRadius2) + 1;
    long long rMax, halfW, ylo, yhi, ey, ey2, ez2, rScene, dist, dmin, hw, hh, fx, fy;
    long long floorX, floorZ;
    const int margin = Rs_Px(12);
    const int ps = RsView_Sin(RS_VIEW_PITCH), pc = RsView_Cos(RS_VIEW_PITCH);

    ylo = v->ymin < 0 ? v->ymin : 0;
    yhi = v->ymax > 0 ? v->ymax : 0;
    if (!v->loaded) {
        rModel = 1;
        ylo = 0;
        yhi = 0;
    }
    if (rDummy > rModel)
        rModel = rDummy;
    if (v->dumYmin < ylo)
        ylo = v->dumYmin;
    if (v->dumYmax > yhi)
        yhi = v->dumYmax;
    if (v->hasCrash) {
        // Whole units outwards, so that the outline stays inside the framing.
        long long ax = RsView_TenthsCeil(v->crash[3]) > -RsView_TenthsFloor(v->crash[0])
                           ? RsView_TenthsCeil(v->crash[3]) : -RsView_TenthsFloor(v->crash[0]);
        long long az = RsView_TenthsCeil(v->crash[5]) > -RsView_TenthsFloor(v->crash[2])
                           ? RsView_TenthsCeil(v->crash[5]) : -RsView_TenthsFloor(v->crash[2]);
        long long rCrash;
        if (ax < 0)
            ax = 0;
        if (az < 0)
            az = 0;
        rCrash = (long long)RsView_Isqrt((unsigned long long)(ax * ax + az * az)) + 1;
        if (rCrash > rModel)
            rModel = rCrash;
        if (RsView_TenthsFloor(v->crash[1]) < ylo)
            ylo = RsView_TenthsFloor(v->crash[1]);
        if (RsView_TenthsCeil(v->crash[4]) > yhi)
            yhi = RsView_TenthsCeil(v->crash[4]);
    }
    rMax = rModel;
    halfW = rModel;
    s->offModel = 0;
    ey = (yhi - ylo + 1) / 2 + 1;

    floorX = halfW + rMax / 8 + 1;
    floorZ = rMax + rMax / 8 + 1;
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
    hh = (h - labelH) / 2 - margin;
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
    s->cam.cyq = (long long)(h + labelH) * RS_VIEW_SUB / 2;   // the label room is at the top
}

// A dashed line of square dots, thick pixels wide, from a to b. Depth is
// interpolated like in RsView_Fill; a dot is drawn where nothing nearer is
// in the picture, so the model hides the outline behind it. The depth buffer
// is left as it is (the outline is drawn last).
static void RsView_DashLine(const struct RsViewTarget *t, const struct RsViewVert *a, const struct RsViewVert *b,
                            unsigned int pixel, int thick, int dash)
{
    const long long dx = b->x - a->x, dy = b->y - a->y;
    long long steps, i;

    if (a->x < -RS_VIEW_COORD_MAX || a->x > RS_VIEW_COORD_MAX || a->y < -RS_VIEW_COORD_MAX ||
        a->y > RS_VIEW_COORD_MAX || b->x < -RS_VIEW_COORD_MAX || b->x > RS_VIEW_COORD_MAX ||
        b->y < -RS_VIEW_COORD_MAX || b->y > RS_VIEW_COORD_MAX)
        return;
    // One step per pixel along the longer screen axis.
    steps = ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy)) / RS_VIEW_SUB + 1;
    for (i = 0; i <= steps; i++) {
        long long px, py;
        int z, ox, oy;
        if ((i / dash) & 1)
            continue;   // the gap of the dash
        px = RsView_FloorDiv16(a->x + dx * i / steps) - thick / 2;
        py = RsView_FloorDiv16(a->y + dy * i / steps) - thick / 2;
        z = (int)(a->z + (long long)(b->z - a->z) * i / steps);
        for (oy = 0; oy < thick; oy++) {
            for (ox = 0; ox < thick; ox++) {
                const long long x = px + ox, y = py + oy;
                size_t at;
                if (x < 0 || y < 0 || x >= t->w || y >= t->h)
                    continue;
                at = (size_t)y * (size_t)t->w + (size_t)x;
                if (z >= t->depth[at])
                    t->pixels[at] = pixel;
            }
        }
    }
}

// The outline of Crash's box about the model: its 12 edges, dashed.
static void RsView_DrawCrash(const struct RsView *v, const struct RsViewTarget *t, const struct RsViewScene *s,
                             unsigned int pixel)
{
    // Edges as pairs of corners (corner k: x from bit 0, y from bit 1, z from bit 2).
    static const int edges[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },     // along x
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },     // along y
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },     // along z
    };
    struct RsViewVert corner[8];
    const int thick = Rs_Px(1) > 1 ? Rs_Px(1) : 1;
    const int dash = Rs_Px(5) > 2 ? Rs_Px(5) : 2;
    int k;

    for (k = 0; k < 8; k++)
        RsView_Project(&s->cam, RsView_TenthsSub(v->crash[(k & 1) ? 3 : 0]),
                       RsView_TenthsSub(v->crash[(k & 2) ? 4 : 1]),
                       RsView_TenthsSub(v->crash[(k & 4) ? 5 : 2]), s->offModel, 1, &corner[k]);
    for (k = 0; k < 12; k++)
        RsView_DashLine(t, &corner[edges[k][0]], &corner[edges[k][1]], pixel, thick, dash);
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

// The dummy in the pose of the view, under the model.
static void RsView_DrawDummy(struct RsView *v, const struct RsViewTarget *t, const struct RsViewScene *s)
{
    int positions = 0, i, k;
    const int tris = Rs_DummyMesh(v->wheels, v->pose, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor,
                                  RS_VIEW_DUMMY_TRI_MAX, &positions);

    for (i = 0; i < tris; i++) {
        struct RsViewVert vert[3];
        const int *corner[3];
        const unsigned int rgb = v->dumColor[i];
        int shade;
        for (k = 0; k < 3; k++) {
            const int at = v->dumTri[3 * i + k];
            if (at < 0 || at >= positions)
                return;   // cannot happen with RldDum_Mesh; never read past the buffer
            corner[k] = v->dumPos + 3 * at;
        }
        shade = RsView_DummyShade(corner[0], corner[1], corner[2]);
        for (k = 0; k < 3; k++) {
            RsView_Project(&s->cam, corner[k][0], corner[k][1], corner[k][2], s->offModel, 1, &vert[k]);
            vert[k].r = (int)((rgb >> 16) & 0xFF) * shade / 100;
            vert[k].g = (int)((rgb >> 8) & 0xFF) * shade / 100;
            vert[k].b = (int)(rgb & 0xFF) * shade / 100;
        }
        // Closed solids: drawn without culling, as the boxes before.
        RsView_Triangle(t, vert, 0);
    }
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
// units - above it, so that the dummy's wheels in front do not cover it.
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

static void RsView_Render(HWND view, struct RsView *v)
{
    struct RsViewTarget t;
    struct RsViewScene s;
    const unsigned int bg = RsView_Pixel(RS_COL_PAGE);
    const int labelH = v->hasCrash ? Rs_Px(20) : 0;
    size_t i, n = (size_t)v->w * (size_t)v->h;
    wchar_t text[RS_VIEW_MESSAGE_CAP];
    int k;

    GdiFlush();   // GDI must be done with the DIB before its bits are written
    for (i = 0; i < n; i++) {
        v->pixels[i] = bg;
        v->depth[i] = 0;   // depth 0 = infinitely far
        v->mask[i] = 0;
    }
    v->dirty = 0;
    v->drawnPal = g_rsPal;

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
    RsView_Scene(v, v->w, v->h, labelH, &s);

    // The floor: two triangles under everything, not turned with it.
    {
        struct RsViewVert q[4], tri[3];
        const COLORREF fc = RS_COL_BORDER;
        for (k = 0; k < 4; k++) {
            RsView_Project(&s.cam, (k & 1) ? s.floorX : -s.floorX, s.floorY, (k & 2) ? s.floorZ : -s.floorZ, 0, 0, &q[k]);
            q[k].r = GetRValue(fc);
            q[k].g = GetGValue(fc);
            q[k].b = GetBValue(fc);
        }
        tri[0] = q[0]; tri[1] = q[1]; tri[2] = q[3];
        RsView_Triangle(&t, tri, 0);
        tri[0] = q[0]; tri[1] = q[3]; tri[2] = q[2];
        RsView_Triangle(&t, tri, 0);
    }

    // The model, in file order; it marks its pixels.
    t.maskMode = RS_VIEW_MASK_SET;
    if (v->loaded) {
        const struct RsViewPoseData *pose = &v->poses[v->pose];
        int tri, c;
        for (tri = 0; tri < pose->count; tri++) {
            const unsigned char *p = pose->tris + (size_t)tri * RS_VIEW_TRI_BYTES;
            struct RsViewVert vert[3];
            for (c = 0; c < 3; c++) {
                const unsigned char *q = p + c * RS_VIEW_CORNER_BYTES;
                RsView_Project(&s.cam, (long long)RsView_ReadS16(q) * RS_VIEW_SUB,
                               (long long)RsView_ReadS16(q + 2) * RS_VIEW_SUB,
                               (long long)RsView_ReadS16(q + 4) * RS_VIEW_SUB, s.offModel, 1, &vert[c]);
                vert[c].r = q[6];
                vert[c].g = q[7];
                vert[c].b = q[8];
            }
            // pad bit 0: no cull bit in the game, drawn from both sides
            RsView_Triangle(&t, vert, !(p[9] & 1));
        }
    }

    // The dummy the model is fitted onto, only where the model left the
    // picture free: it never covers the model.
    t.maskMode = RS_VIEW_MASK_SKIP;
    RsView_DrawDummy(v, &t, &s);
    t.maskMode = RS_VIEW_MASK_NONE;

    // Crash's size about the model, over everything but what is in front of it.
    if (v->hasCrash)
        RsView_DrawCrash(v, &t, &s, RsView_Pixel(RS_COL_ACCENT));

    GdiFlush();   // the bits are done; GDI writes the text on top
    if (v->hasCrash) {
        long long lo[3], hi[3];
        for (k = 0; k < 3; k++) {
            lo[k] = RsView_TenthsSub(v->crash[k]);
            hi[k] = RsView_TenthsSub(v->crash[k + 3]);
        }
        RsView_BoxLabel(v, &s, lo, hi, s.offModel, labelH, L"Crash size");
    }
    if (!v->loaded) {
        // The dummy alone: the window text above it.
        RECT r = { 0, 0, v->w, Rs_Px(28) };
        InflateRect(&r, -Rs_Px(16), 0);
        OffsetRect(&r, 0, Rs_Px(8));
        GetWindowTextW(view, text, RS_VIEW_MESSAGE_CAP);
        text[RS_VIEW_MESSAGE_CAP - 1] = 0;
        RsView_DrawText(v, &r, text, RS_FONT_BODY, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (v->poses[v->pose].count == 0) {
        RsView_CenterText(v, L"This pose has no triangles.");
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
        // Drawn again only when something changed - also the colour scheme,
        // which the shell switches without telling the controls.
        if (v->dirty || v->drawnPal != g_rsPal)
            RsView_Render(view, v);
        BitBlt(dc, 0, 0, v->w, v->h, v->mem, 0, 0, SRCCOPY);
    } else if (rc.right > 0 && rc.bottom > 0) {
        HBRUSH br = CreateSolidBrush(RS_COL_PAGE);
        FillRect(dc, &rc, br);
        DeleteObject(br);
    }
}

// The extent of the dummy over all poses, with its wheels, in whole game units
// outwards - for a framing that does not change with the pose or the wheels.
static void RsView_DummyExtent(struct RsView *v)
{
    long long r2 = 0;
    int ymin = 0, ymax = 0, pose, i, positions;

    for (pose = 0; pose < RS_VIEW_POSE_COUNT; pose++) {
        if (!Rs_DummyMesh(1, pose, v->dumPos, RS_VIEW_DUMMY_POS_MAX, v->dumTri, v->dumColor, RS_VIEW_DUMMY_TRI_MAX,
                          &positions))
            continue;
        for (i = 0; i < positions; i++) {
            const long long x = v->dumPos[3 * i], y = v->dumPos[3 * i + 1], z = v->dumPos[3 * i + 2];
            const long long ax = (x < 0 ? -x : x) / RS_VIEW_SUB + 1, az = (z < 0 ? -z : z) / RS_VIEW_SUB + 1;
            const int ylo = (int)RsView_FloorDiv16(y), yhi = (int)-RsView_FloorDiv16(-y);
            if (ax * ax + az * az > r2)
                r2 = ax * ax + az * az;
            if (ylo < ymin)
                ymin = ylo;
            if (yhi > ymax)
                ymax = yhi;
        }
    }
    v->dumRadius2 = r2;
    v->dumYmin = ymin;
    v->dumYmax = ymax;
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
        RsView_DummyExtent(v);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        return 0;
    case WM_DESTROY:
        if (v) {
            RsView_FreeFrame(v);
            RsView_DropModel(v);
            Rs_Free(v->dumPos);
            Rs_Free(v->dumTri);
            Rs_Free(v->dumColor);
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
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
    if (RegisterClassExW(&wc))
        return TRUE;
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
    // Tenths of the s16 range of the model: the squares in RsView_Scene cannot
    // overflow whatever the caller passes.
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
