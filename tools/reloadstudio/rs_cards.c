// rs_cards.c - the pictures of the three cards "What do you want to build?"
// (rs_charroute.h)
//
// A PICTOGRAM OF THE LOOK, DRAWN IN CODE. No stored picture of any kind: the
// repository takes no picture files (tools/content_guard.py). The same motif -
// a kart with its driver, seen from the side - is drawn in the look of each
// route:
//   Vanilla   the PS1 way: drawn at a quarter of the size without smoothing,
//             few flat colours, round parts as coarse polygons, then scaled up
//             nearest - chunky pixels
//   Low-Poly  faceted flat polygons in clear colours: every facet one shade,
//             lit from the upper left; edges smoothed
//   Hi-Res    smooth shapes with gradients, soft light, highlights and a soft
//             shadow on the ground; edges smoothed
// The motif is laid out in a design space of 360 x 240 and scaled into the
// size the card asks for, the aspect kept and centred; the background (a calm
// vertical grey gradient that reads on the dark and the light theme alike)
// fills the whole picture. Smoothing is supersampling: the picture is drawn
// three times as large and averaged down. Everything is plain arithmetic on
// a float buffer - the same bytes on every machine - and the result is a
// top-down 32-bit DIB section. Nothing is cached: the page asks again after a
// size change and deletes the old bitmap.

#include "reloadstudio.h"
#include "rs_charroute.h"
#include <math.h>
#include <string.h>

#define CARD_DESIGN_W 360.0f
#define CARD_DESIGN_H 240.0f
#define CARD_SS 3              // supersampling of Low-Poly and Hi-Res (per axis)
#define CARD_PIXEL 4           // Vanilla: one picture pixel = CARD_PIXEL x CARD_PIXEL screen pixels
#define CARD_MAX_POINTS 96
#define CARD_PI 3.14159265358979f

enum { CARD_FLAT = 0, CARD_LINEAR, CARD_RADIAL };

// How a shape is filled: a flat colour, or colour and opacity going from
// (c0, a0) to (c1, a1) - along the line from (x0, y0) to (x1, y1) (linear), or
// from the centre (x0, y0) out to the radii rx, ry (radial). Design units.
struct CardPaint {
    int kind;
    float c0[3], c1[3];
    float a0, a1;
    float x0, y0, x1, y1;
    float rx, ry;
};

struct CardCanvas {
    int w, h;
    float *rgb;            // w * h * 3, 0..255
    float scale, ox, oy;   // design -> canvas pixels
};

static void Card_Rgb(float out[3], unsigned rgb)
{
    out[0] = (float)((rgb >> 16) & 255u);
    out[1] = (float)((rgb >> 8) & 255u);
    out[2] = (float)(rgb & 255u);
}

static struct CardPaint Card_Flat(unsigned rgb, float alpha)
{
    struct CardPaint p;
    memset(&p, 0, sizeof(p));
    p.kind = CARD_FLAT;
    Card_Rgb(p.c0, rgb);
    Card_Rgb(p.c1, rgb);
    p.a0 = p.a1 = alpha;
    return p;
}

static struct CardPaint Card_Linear(unsigned from, unsigned to, float x0, float y0, float x1, float y1)
{
    struct CardPaint p = Card_Flat(from, 1.0f);
    p.kind = CARD_LINEAR;
    Card_Rgb(p.c1, to);
    p.x0 = x0;
    p.y0 = y0;
    p.x1 = x1;
    p.y1 = y1;
    return p;
}

static struct CardPaint Card_Radial(unsigned centre, unsigned edge, float a0, float a1, float x, float y, float rx, float ry)
{
    struct CardPaint p = Card_Flat(centre, a0);
    p.kind = CARD_RADIAL;
    Card_Rgb(p.c1, edge);
    p.a1 = a1;
    p.x0 = x;
    p.y0 = y;
    p.rx = rx;
    p.ry = ry;
    return p;
}

// A colour scaled by a light factor (facets).
static unsigned Card_Shade(unsigned rgb, float k)
{
    float c[3];
    unsigned out = 0;
    int i;
    Card_Rgb(c, rgb);
    for (i = 0; i < 3; i++) {
        float v = c[i] * k;
        if (v > 255.0f)
            v = 255.0f;
        out = (out << 8) | (unsigned)(v + 0.5f);
    }
    return out;
}

// Fills a polygon (design units, even-odd rule, pixel centres) with a paint.
static void Card_Poly(struct CardCanvas *c, const float *pts, int n, const struct CardPaint *p)
{
    float xs[CARD_MAX_POINTS];
    float cx[CARD_MAX_POINTS], cy[CARD_MAX_POINTS];
    float ymin = 1e9f, ymax = -1e9f;
    int i, y, x;

    if (n < 3 || n > CARD_MAX_POINTS)
        return;
    for (i = 0; i < n; i++) {
        cx[i] = c->ox + pts[i * 2] * c->scale;
        cy[i] = c->oy + pts[i * 2 + 1] * c->scale;
        if (cy[i] < ymin)
            ymin = cy[i];
        if (cy[i] > ymax)
            ymax = cy[i];
    }
    for (y = (int)ceilf(ymin - 0.5f); y <= (int)floorf(ymax - 0.5f); y++) {
        float yc = (float)y + 0.5f;
        int k = 0, a, b;
        if (y < 0 || y >= c->h)
            continue;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            if ((cy[i] <= yc) != (cy[j] <= yc))
                xs[k++] = cx[i] + (yc - cy[i]) * (cx[j] - cx[i]) / (cy[j] - cy[i]);
        }
        for (a = 1; a < k; a++) {          // insertion sort, k is small
            float v = xs[a];
            for (b = a - 1; b >= 0 && xs[b] > v; b--)
                xs[b + 1] = xs[b];
            xs[b + 1] = v;
        }
        for (a = 0; a + 1 < k; a += 2) {
            int x0 = (int)ceilf(xs[a] - 0.5f), x1 = (int)ceilf(xs[a + 1] - 0.5f) - 1;
            if (x0 < 0)
                x0 = 0;
            if (x1 > c->w - 1)
                x1 = c->w - 1;
            for (x = x0; x <= x1; x++) {
                // The paint at this pixel centre, in design units.
                float dx = ((float)x + 0.5f - c->ox) / c->scale;
                float dy = (yc - c->oy) / c->scale;
                float t = 0.0f, alpha, col[3];
                float *px = &c->rgb[((size_t)y * (size_t)c->w + (size_t)x) * 3];
                if (p->kind == CARD_LINEAR) {
                    float vx = p->x1 - p->x0, vy = p->y1 - p->y0;
                    float len2 = vx * vx + vy * vy;
                    t = len2 > 0.0f ? ((dx - p->x0) * vx + (dy - p->y0) * vy) / len2 : 0.0f;
                } else if (p->kind == CARD_RADIAL) {
                    float u = (dx - p->x0) / p->rx, v = (dy - p->y0) / p->ry;
                    t = sqrtf(u * u + v * v);
                }
                if (t < 0.0f)
                    t = 0.0f;
                if (t > 1.0f)
                    t = 1.0f;
                alpha = p->a0 + (p->a1 - p->a0) * t;
                for (i = 0; i < 3; i++) {
                    col[i] = p->c0[i] + (p->c1[i] - p->c0[i]) * t;
                    px[i] = px[i] + (col[i] - px[i]) * alpha;
                }
            }
        }
    }
}

static void Card_Rect(struct CardCanvas *c, float x0, float y0, float x1, float y1, const struct CardPaint *p)
{
    const float pts[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
    Card_Poly(c, pts, 4, p);
}

// A bar of the given width from (x0, y0) to (x1, y1).
static void Card_Bar(struct CardCanvas *c, float x0, float y0, float x1, float y1, float width, const struct CardPaint *p)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    float nx = len > 0.0f ? -dy / len * width * 0.5f : 0.0f;
    float ny = len > 0.0f ? dx / len * width * 0.5f : 0.0f;
    const float pts[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    Card_Poly(c, pts, 4, p);
}

// An ellipse as a polygon of segs corners (a coarse one is the PS1 look).
static void Card_Ellipse(struct CardCanvas *c, float x, float y, float rx, float ry, int segs, const struct CardPaint *p)
{
    float pts[CARD_MAX_POINTS * 2];
    int i;
    if (segs > CARD_MAX_POINTS)
        segs = CARD_MAX_POINTS;
    for (i = 0; i < segs; i++) {
        float a = 2.0f * CARD_PI * ((float)i + 0.5f) / (float)segs;
        pts[i * 2] = x + rx * cosf(a);
        pts[i * 2 + 1] = y + ry * sinf(a);
    }
    Card_Poly(c, pts, segs, p);
}

// A circle of flat facets (triangles from the centre), each lit by the angle
// of its outer edge against a light from the upper left.
static void Card_FacetDisc(struct CardCanvas *c, float x, float y, float r, int segs, unsigned rgb)
{
    int i;
    for (i = 0; i < segs; i++) {
        float a0 = 2.0f * CARD_PI * (float)i / (float)segs + 0.3f;
        float a1 = 2.0f * CARD_PI * (float)(i + 1) / (float)segs + 0.3f;
        float mid = (a0 + a1) * 0.5f;
        // light direction (-0.6, -0.8) in screen space (y down)
        float k = 0.80f + 0.30f * (cosf(mid) * -0.6f + sinf(mid) * -0.8f);
        const float pts[6] = { x, y, x + r * cosf(a0), y + r * sinf(a0), x + r * cosf(a1), y + r * sinf(a1) };
        struct CardPaint p = Card_Flat(Card_Shade(rgb, k), 1.0f);
        Card_Poly(c, pts, 3, &p);
    }
}

// The colours of the motif.
#define CARD_BG_TOP    0x9AA1A9u
#define CARD_BG_BOTTOM 0x7D848Cu
#define CARD_BODY      0xD22D28u
#define CARD_SUIT      0x285AC8u
#define CARD_HELMET    0xF5C828u
#define CARD_VISOR     0x1E285Au
#define CARD_TYRE      0x28282Eu
#define CARD_RIM       0xC8CDD7u
#define CARD_WING      0xE6E6EBu
#define CARD_METAL     0x6E737Du

// The kart, facing right, in design units (360 x 240; the ground at y 192).
static const float s_cardBody[] = {
    52, 150, 66, 128, 150, 122, 205, 118, 300, 134, 326, 150, 318, 170, 74, 174
};
static const float s_cardBodyTop[] = {    // the lit upper half (Low-Poly)
    52, 150, 66, 128, 150, 122, 205, 118, 300, 134, 326, 150
};
static const float s_cardTorso[] = { 146, 128, 156, 84, 192, 80, 204, 124 };
static const float s_cardVisor[] = { 184, 52, 206, 50, 208, 72, 188, 74 };

static void Card_Motif(struct CardCanvas *c, int route)
{
    struct CardPaint p;
    int vanilla = route == RS_ROUTE_VANILLA, lowpoly = route == RS_ROUTE_LOWPOLY;
    int hires = route == RS_ROUTE_HIRES;
    int round = vanilla ? 10 : (lowpoly ? 9 : 72);     // corners of a round part
    const float wheels[2][3] = { { 104, 160, 32 }, { 268, 166, 26 } };
    int i;

    // Shadow on the ground.
    p = hires ? Card_Radial(0x000000u, 0x000000u, 0.38f, 0.0f, 186, 192, 158, 14)
              : Card_Flat(0x000000u, 0.22f);
    Card_Ellipse(c, 186, 192, hires ? 158.0f : 150.0f, hires ? 14.0f : 10.0f, hires ? 72 : 16, &p);

    // Rear wing on its strut, the exhaust.
    p = hires ? Card_Linear(0x8A8F99u, 0x50555Eu, 76, 100, 84, 128) : Card_Flat(lowpoly ? 0x5A5F69u : CARD_METAL, 1.0f);
    Card_Rect(c, 74, 100, 84, 130, &p);
    p = hires ? Card_Linear(0xFFFFFFu, 0xB4B7C0u, 60, 86, 60, 102) : Card_Flat(CARD_WING, 1.0f);
    Card_Rect(c, 44, 88, 108, 102, &p);
    if (lowpoly) {
        p = Card_Flat(Card_Shade(CARD_WING, 0.80f), 1.0f);
        Card_Rect(c, 44, 96, 108, 102, &p);
    }
    p = hires ? Card_Linear(0xB4B9C3u, 0x5A5F69u, 40, 138, 40, 150) : Card_Flat(CARD_METAL, 1.0f);
    Card_Rect(c, 36, 138, 60, 150, &p);

    // The driver: torso (the body covers its lower end), arm, steering wheel.
    p = hires ? Card_Linear(0x5082E6u, 0x1A3C96u, 150, 82, 200, 126) : Card_Flat(CARD_SUIT, 1.0f);
    Card_Poly(c, s_cardTorso, 4, &p);
    if (lowpoly) {
        const float side[8] = { 174, 82, 192, 80, 204, 124, 176, 126 };
        p = Card_Flat(Card_Shade(CARD_SUIT, 0.78f), 1.0f);
        Card_Poly(c, side, 4, &p);
    }

    // Body.
    if (hires) {
        p = Card_Linear(0xF06450u, 0x8C1914u, 180, 118, 180, 174);
        Card_Poly(c, s_cardBody, 8, &p);
        p = Card_Linear(0xFFFFFFu, 0xFFFFFFu, 180, 121, 180, 140);
        p.a0 = 0.45f;
        p.a1 = 0.0f;
        Card_Poly(c, s_cardBodyTop, 6, &p);
    } else {
        p = Card_Flat(lowpoly ? Card_Shade(CARD_BODY, 0.78f) : CARD_BODY, 1.0f);
        Card_Poly(c, s_cardBody, 8, &p);
        if (lowpoly) {
            p = Card_Flat(Card_Shade(CARD_BODY, 1.10f), 1.0f);
            Card_Poly(c, s_cardBodyTop, 6, &p);
        }
    }
    // A white racing number panel on the side.
    p = hires ? Card_Linear(0xFFFFFFu, 0xD2D5DCu, 220, 138, 220, 162) : Card_Flat(0xF5F5F5u, 1.0f);
    Card_Ellipse(c, 222, 150, 16, 12, round, &p);

    p = hires ? Card_Linear(0x3C82F0u, 0x1E46A0u, 200, 96, 236, 112) : Card_Flat(Card_Shade(CARD_SUIT, lowpoly ? 1.15f : 1.0f), 1.0f);
    Card_Bar(c, 190, 98, 236, 114, 10, &p);
    p = hires ? Card_Linear(0x50505Au, 0x1E1E24u, 240, 104, 240, 128) : Card_Flat(CARD_TYRE, 1.0f);
    Card_Bar(c, 240, 104, 246, 128, 7, &p);

    // Helmet and visor.
    if (lowpoly) {
        Card_FacetDisc(c, 180, 62, 25, round, CARD_HELMET);
    } else {
        p = hires ? Card_Radial(0xFFF5AAu, 0xD29614u, 1.0f, 1.0f, 170, 50, 34, 34) : Card_Flat(CARD_HELMET, 1.0f);
        Card_Ellipse(c, 180, 62, 25, 25, round, &p);
    }
    p = hires ? Card_Linear(0x4A5A9Cu, 0x10163Au, 190, 50, 204, 74) : Card_Flat(CARD_VISOR, 1.0f);
    Card_Poly(c, s_cardVisor, 4, &p);
    if (hires) {
        p = Card_Radial(0xFFFFFFu, 0xFFFFFFu, 0.85f, 0.0f, 170, 48, 9, 6);
        Card_Ellipse(c, 170, 48, 9, 6, 32, &p);
    }

    // Wheels: tyre, rim, hub.
    for (i = 0; i < 2; i++) {
        float x = wheels[i][0], y = wheels[i][1], r = wheels[i][2];
        if (lowpoly) {
            Card_FacetDisc(c, x, y, r, round, CARD_TYRE);
            Card_FacetDisc(c, x, y, r * 0.55f, 7, CARD_RIM);
            continue;
        }
        p = hires ? Card_Radial(0x3C3C46u, 0x141418u, 1.0f, 1.0f, x, y, r, r) : Card_Flat(CARD_TYRE, 1.0f);
        Card_Ellipse(c, x, y, r, r, round, &p);
        p = hires ? Card_Radial(0xFAFAFFu, 0x8C919Bu, 1.0f, 1.0f, x - r * 0.2f, y - r * 0.25f, r * 0.75f, r * 0.75f)
                  : Card_Flat(CARD_RIM, 1.0f);
        Card_Ellipse(c, x, y, r * 0.55f, r * 0.55f, vanilla ? 8 : round, &p);
        if (hires) {
            p = Card_Radial(0x5A5F69u, 0x3C4049u, 1.0f, 1.0f, x, y, r * 0.18f, r * 0.18f);
            Card_Ellipse(c, x, y, r * 0.18f, r * 0.18f, 32, &p);
        }
    }
}

// The background and the motif on a canvas of w x h pixels.
static void Card_Paint(struct CardCanvas *c, int route)
{
    float top[3], bottom[3];
    int x, y, i;

    Card_Rgb(top, CARD_BG_TOP);
    Card_Rgb(bottom, CARD_BG_BOTTOM);
    for (y = 0; y < c->h; y++) {
        float t = c->h > 1 ? (float)y / (float)(c->h - 1) : 0.0f;
        for (x = 0; x < c->w; x++)
            for (i = 0; i < 3; i++)
                c->rgb[((size_t)y * (size_t)c->w + (size_t)x) * 3 + (size_t)i] = top[i] + (bottom[i] - top[i]) * t;
    }
    c->scale = (float)c->w / CARD_DESIGN_W;
    if ((float)c->h / CARD_DESIGN_H < c->scale)
        c->scale = (float)c->h / CARD_DESIGN_H;
    c->ox = ((float)c->w - CARD_DESIGN_W * c->scale) * 0.5f;
    c->oy = ((float)c->h - CARD_DESIGN_H * c->scale) * 0.5f;
    Card_Motif(c, route);
}

HBITMAP Rs_RouteImage(int route, int w, int h)
{
    struct CardCanvas c;
    BITMAPINFO bi;
    BYTE *bits = NULL;
    HBITMAP bmp;
    int ss, x, y, i;

    if (route < RS_ROUTE_VANILLA || route >= RS_ROUTES || w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return NULL;
    // Vanilla: a canvas CARD_PIXEL times smaller, scaled up nearest below.
    // The others: CARD_SS times larger, averaged down.
    if (route == RS_ROUTE_VANILLA) {
        ss = 1;
        c.w = (w + CARD_PIXEL - 1) / CARD_PIXEL;
        c.h = (h + CARD_PIXEL - 1) / CARD_PIXEL;
    } else {
        ss = CARD_SS;
        c.w = w * ss;
        c.h = h * ss;
    }
    c.rgb = (float *)Rs_Alloc((size_t)c.w * (size_t)c.h * 3 * sizeof(float));
    Card_Paint(&c, route);

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;     // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!bmp || !bits) {
        if (bmp)
            DeleteObject(bmp);
        Rs_Free(c.rgb);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        BYTE *row = bits + (size_t)y * (size_t)w * 4;
        for (x = 0; x < w; x++) {
            float sum[3] = { 0.0f, 0.0f, 0.0f };
            int sx, sy;
            if (route == RS_ROUTE_VANILLA) {
                const float *s = &c.rgb[((size_t)(y / CARD_PIXEL) * (size_t)c.w + (size_t)(x / CARD_PIXEL)) * 3];
                for (i = 0; i < 3; i++)
                    sum[i] = s[i];
            } else {
                for (sy = 0; sy < ss; sy++)
                    for (sx = 0; sx < ss; sx++) {
                        const float *s = &c.rgb[((size_t)(y * ss + sy) * (size_t)c.w + (size_t)(x * ss + sx)) * 3];
                        for (i = 0; i < 3; i++)
                            sum[i] += s[i];
                    }
                for (i = 0; i < 3; i++)
                    sum[i] /= (float)(ss * ss);
            }
            // BGRA, opaque.
            for (i = 0; i < 3; i++) {
                float v = sum[2 - i] + 0.5f;
                row[x * 4 + i] = (BYTE)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
            }
            row[x * 4 + 3] = 255;
        }
    }
    GdiFlush();
    Rs_Free(c.rgb);
    return bmp;
}
