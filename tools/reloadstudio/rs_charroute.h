// rs_charroute.h - the guided flow "Build a character": the three routes
//
// The page "Character" (rs_char.c) asks first "What do you want to build?"
// and shows three cards; the choice is the route. Each route shows only its
// own steps; what it does not need is not there.
//   Vanilla   the classic model (make-char), the way the page built before
//   Low-Poly  the classic model made automatically from the same glTF
//             (make-char, the fallback) and the model itself in the chunk
//             CHRG (hires-add): a small triangle target, clear surfaces
//   Hi-Res    the same chain with the full budget of the game
// Nothing here checks or packs by itself: the check is RsHires_CheckFile
// (include/rldhires.inc, the game's rules), the build is rldpack make-char
// and hires-add started through Rs_RunRldpack - byte for byte rldpack's.
//
// Owners: the presets and the messages rs_charhires.c, the pictures of the
// cards rs_cards.c, the game start rs_test.c, the flow rs_char.c.

#ifndef RS_CHARROUTE_H
#define RS_CHARROUTE_H

#include <wchar.h>

struct RsHiresCheck;   // rs_hires_model.h

enum RsCharRoute {
    RS_ROUTE_NONE = -1,      // not chosen yet: the page shows the three cards
    RS_ROUTE_VANILLA = 0,
    RS_ROUTE_LOWPOLY,
    RS_ROUTE_HIRES,
    RS_ROUTES
};

// What a route presets. The targets of Low-Poly are the Studio's advice, not
// rules of the game: over them the page warns and still builds; the game's
// own budget (include/rldhires.inc) stays the limit of every CHRG route.
struct RsRoutePreset {
    const wchar_t *word;          // automation: L"vanilla", L"lowpoly", L"hires"
    const wchar_t *title;         // the card: L"Vanilla", L"Low-Poly", L"Hi-Res"
    const wchar_t *sentence;      // the card's one sentence
    int chrg;                     // 1 = the build adds the chunk CHRG (hires-add)
    unsigned long long triangleTarget;   // 0 = none (Vanilla: make-char's own limit)
    unsigned textureEdgeTarget;          // 0 = none
    int upscaleDefault;           // "Upscale small textures" at the start of the route
};

// The preset of a route; NULL outside RS_ROUTE_VANILLA..RS_ROUTE_HIRES.
const struct RsRoutePreset *RsRoute_Preset(int route);
// The route of an automation word, RS_ROUTE_NONE when unknown.
int RsRoute_FromWord(const wchar_t *word);

// THE MESSAGES OF A CHRG ROUTE, in plain English, each at the place it is
// about. A model without clips gives ONE line (no clip list), a model
// without wheel nodes ONE line; every rule finding of the check keeps its
// rule ID and gets a "How to fix" sentence.
enum RsWizPlace {
    RS_WIZ_PLACE_MODEL = 0,      // the model field of the step Model
    RS_WIZ_PLACE_ANIMATIONS,     // the line about clips (step Model, below the model)
    RS_WIZ_PLACE_WHEELS,         // the line about wheel nodes (step Model)
    RS_WIZ_PLACE_SIZE,           // triangles, textures, the budget and the route's targets
    RS_WIZ_PLACE_BUILD           // the step Build
};

#define RS_WIZ_TEXT_MAX 256

struct RsWizMsg {
    int severity;                     // RS_SEV_* (reloadstudio.h)
    int place;                        // RS_WIZ_PLACE_*
    wchar_t rule[8];                  // L"HRG-16", or L"" for a line of the Studio
    wchar_t text[RS_WIZ_TEXT_MAX];    // what is the matter, one sentence
    wchar_t fix[RS_WIZ_TEXT_MAX];     // "How to fix", L"" when nothing needs fixing
};

// The messages of a check for a route (RS_ROUTE_LOWPOLY or RS_ROUTE_HIRES),
// most severe first; returns how many were written (at most cap).
int RsCharHires_Messages(const struct RsHiresCheck *c, int route, struct RsWizMsg *out, int cap);

// The extra make-char switches of the classic fallback of a CHRG route
// (written after the page's own switches); returns the count, at most
// RS_CHARHIRES_FALLBACK_ARGS.
#define RS_CHARHIRES_FALLBACK_ARGS 8
int RsCharHires_FallbackArgs(int route, const wchar_t **args);

// The picture of a card (rs_cards.c): the example of the route, scaled into
// w x h pixels (aspect kept, centred). NULL when it cannot be made; the
// caller deletes it (DeleteObject).
#ifdef _WINDOWS_
HBITMAP Rs_RouteImage(int route, int w, int h);
#endif

// "Test in game" (rs_test.c): starts the game with this character in a race
// right away - a CHRG route with the switches the hi-res driver needs. In
// automation it only logs the command line. 1 = started (or logged), 0 =
// not, why says why in plain English.
int Rs_TestCharacter(const wchar_t *rldchar, int route, wchar_t *why, int whyCap);

#endif
