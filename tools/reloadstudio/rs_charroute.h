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
    int upscaleDefault;           // "Upscale small textures" at the start of the route (0 on every
                                  // route: the page suggests it per model, RsCharHires_SuggestUpscale)
};

// The preset of a route; NULL outside RS_ROUTE_VANILLA..RS_ROUTE_HIRES.
const struct RsRoutePreset *RsRoute_Preset(int route);
// The route of an automation word, RS_ROUTE_NONE when unknown.
int RsRoute_FromWord(const wchar_t *word);

// THE MESSAGES OF A CHRG ROUTE, in plain English, each at the place it is
// about. A model without clips gives ONE line (no clip list), a model
// without wheel nodes ONE line; every rule finding of the check keeps its
// rule ID and gets a "How to fix" sentence. PS1-like vertex colours (HRG-36)
// are a warning only in the mode glTF (c->vcolMode, the mode the page chose:
// RsHires_SetColourMode), with the fix "Switch Vertex color blending on.";
// in PSX blend and off they are no message.
enum RsWizPlace {
    RS_WIZ_PLACE_MODEL = 0,      // the model field of the step Model
    RS_WIZ_PLACE_ANIMATIONS,     // the line about clips (step Model, below the model)
    RS_WIZ_PLACE_WHEELS,         // the line about wheel nodes (step Model)
    RS_WIZ_PLACE_SIZE,           // triangles, textures, the budget and the route's targets
    RS_WIZ_PLACE_BUILD           // the step Build
};

#define RS_WIZ_TEXT_MAX 256

// HRG-41 (include/rldhires.inc): a model that carries the kart of the Creator
// Kit, made from the user's game data - shown at the step Build and in the
// report, word for word.
#define RS_TEXT_KIT_KART L"This file contains the original kart from your game data. Don't share it, or export without the kart."

// THE KART of a CHRG route (rs_hires_model.h, RsHiresCheck.kartCase): the
// lines of the step Model. A kart part (a node "kart" with its meshes) is
// packed with --kart-part; the game's kart (the kit's mesh-less kart joint, or
// the switch "Kart: from the player's game data" under Advanced) with
// --game-kart template.
#define RS_TEXT_KART_PART L"Your kart is left out on the podium, in the menu and in the driver select."
#define RS_FIX_KART_PART  L"Nothing to do; in a race it is drawn with your driver."
#define RS_TEXT_GAME_KART L"The game adds the original kart from the player's game data."
// ... and the classic model (make-char --figure-only): the figure in a stand-in.
#define RS_TEXT_GAME_KART_CLASSIC L"The game adds the original kart from the player's game data; the classic model shows your figure in a simple grey kart."
// The switch's lines (RsCharHires_GameKartLine, RsCharHires_GameKartStepLine):
// on = the switch, suggested = the model has the kit's kart joint (RS_KART_GAME).
// The line below the switch under Advanced: what it does and why it is set.
void RsCharHires_GameKartLine(int on, int suggested, wchar_t *out, int cap);
// The line on the step Model (the standing rule of suggestions under
// Advanced): said when the switch is suggested or the author's choice differs
// from the suggestion; L"" when it is off and not suggested.
void RsCharHires_GameKartStepLine(int on, int suggested, wchar_t *out, int cap);

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

// THE VERTEX COLOURS of a CHRG route (include/rldhires_color.inc; decision
// V-E4 of the round's plan). A model with COLOR_0 gets the switch "Vertex
// color blending" on its step Model: ON = PSX blend (grey 128 is neutral, as
// on the PS1), OFF = the vertex colours are ignored; the third mode, glTF
// (white is neutral), is chosen under Advanced. A model without COLOR_0 has
// no switch, and the build passes none (rldpack writes glTF, the bytes of
// before). The page sets the switch to the suggestion when a model is
// chosen; a mode the author chose stays with that model. The suggestion:
//   PS1-like colours (the check said HRG-36)       PSX     RS_VCOL_WHY_PS1
//   COLOR_0, not PS1-like                          glTF    RS_VCOL_WHY_WHITE
//   not decidable (no COLOR_0 on a textured face,  the route's: Low-Poly PSX,
//   or the check's rows ran over before HRG-36)    Hi-Res glTF  RS_VCOL_WHY_ROUTE
enum RsVcolWhy {
    RS_VCOL_WHY_NONE = 0,        // no COLOR_0: no switch
    RS_VCOL_WHY_PS1,
    RS_VCOL_WHY_WHITE,
    RS_VCOL_WHY_ROUTE
};

// The suggested mode (RS_HIRES_VCOL_*, rs_hires_model.h) of a check for a
// CHRG route, -1 when the model has no COLOR_0; why (may be NULL) RS_VCOL_WHY_*.
int RsCharHires_SuggestColours(const struct RsHiresCheck *c, int route, int *why);

// The one short line below the switch: what the mode does and why it is set
// - the suggestion's reason ("Your vertex colours look like PS1 shading -
// blending is on."), or, when the author chose another mode, that mode and
// the suggestion.
void RsCharHires_ColourLine(int mode, int suggested, int why, int route, wchar_t *out, int cap);

// THE UPSCALE of a CHRG route ("Upscale small textures", Advanced of the step
// Model: rldpack hires-upscale before hires-add). Off at the start of every
// route (upscaleDefault 0); when a model is checked the page SUGGESTS it - the
// box ticked and one line below it - only for a model whose textures are all
// small. The rule, the same for Low-Poly and Hi-Res:
//   the model has at least one base colour texture, the size of every one of
//   them could be read, and the largest edge (width or height) of the largest
//   of them, AS THE FILE HAS IT (RsHiresCheck.baseEdgeFile, before the game's
//   round-up to a power of two), is below RS_UPSCALE_SUGGEST_EDGE.
// The edge before the round-up: a 40 x 40 texture is small, though the game
// holds it as 64 x 64. Normal maps do not count (hires-upscale leaves them).
// A choice the author made (the box clicked) stays with that model, as the
// vertex colour mode does.
#define RS_UPSCALE_SUGGEST_EDGE 64u

// 1 = suggested for this check on this route (RS_ROUTE_LOWPOLY or
// RS_ROUTE_HIRES), 0 = not; largest (may be NULL) = baseEdgeFile of the check.
int RsCharHires_SuggestUpscale(const struct RsHiresCheck *c, int route, unsigned *largest);

// The one short line below the box: the suggestion ("Your textures are small
// (largest 32 px) - Upscale small textures is on."), or, when the author's
// choice differs from it, that choice and the suggestion; L"" when the box is
// off and was not suggested (no line).
void RsCharHires_UpscaleLine(int on, int suggested, unsigned largest, wchar_t *out, int cap);

// The one line on the step Model itself (outside Advanced, which is closed at
// the start), followed by the button "Change" that opens Advanced at the box.
// It shows what the build will do only where that is not the plain default
// (the default: the suggestion; without one, off):
//   suggested, box on:  "Your textures are small (largest 16 px) - they will
//                        be upscaled."
//   suggested, box off: "Your textures will stay as they are (your choice;
//                        suggested: upscale, largest 16 px)."
//   not suggested, on:  "Your textures will be upscaled (your choice)."
//   not suggested, off: L"" (no line).
// L"" on every route but RS_ROUTE_LOWPOLY and RS_ROUTE_HIRES (Vanilla has no
// upscale).
void RsCharHires_UpscaleStepLine(int route, int on, int suggested, unsigned largest, wchar_t *out, int cap);

// THE JPEG TEXTURES of a CHRG route. The game reads PNG textures only (rule
// HRG-16; CHRG of version 1 holds PNG only, and hires-add stores the .glb
// byte for byte), so a model with a JPEG texture is refused - the error stays
// at its place and says what is wrong. Its "How to fix" offers the button
// "Convert to PNG" on the step Model: the author's deliberate step, never
// suggested by the Studio. The click ticks "Convert JPEG textures to PNG"
// (Advanced, card Options, below "Upscale small textures"), kept with the
// model as the upscale choice is; the page then checks the model as
// converted (rldpack hires-png into its temp folder, Rs_HiresPngFile) - the
// JPEG error goes, the upscale suggestion sees the textures' sizes - and the
// step Model says so in one line with "Change". Unticked in Advanced, the
// model is checked as it is again and the error comes back. The build runs
//   make-char <model> (the classic fallback reads JPEG itself)
//   -> hires-png <model> -o <model>-png.glb
//   -> [hires-upscale <model>-png.glb -o <model>-png-upscaled.glb]
//   -> hires-add <classic> <the last of them> -o <out>
// exactly as rldpack is called by hand. Vanilla never has any of it.

// The fix of a JPEG row of HRG-16 (the button's name in it).
#define RS_FIX_JPEG L"Press \"Convert to PNG\" - the Studio converts them with rldpack hires-png before packing; your file stays as it is. " \
                    L"Or save the textures as PNG (glTF export: Images > PNG) and export again."

// 1 = a message of RsCharHires_Messages is about JPEG textures (HRG-16, "is JPEG").
int RsCharHires_IsJpegMsg(const struct RsWizMsg *m);

// 1 = the button "Convert to PNG" is shown: a CHRG route, the model as it is
// was checked (the box off) and it has JPEG textures.
int RsCharHires_PngFixShown(const struct RsHiresCheck *c, int route, int on);

// The one line on the step Model (followed by "Change", which opens Advanced
// at the box), only while the box is on:
//   JPEG textures converted:   "Your JPEG textures will be converted to PNG."
//   the conversion failed:     "Your JPEG textures could not be converted to
//                               PNG - see the message below."
//   no JPEG in the model:      "Convert JPEG textures to PNG is on - your
//                               model has no JPEG textures."
// L"" when the box is off and on every route but RS_ROUTE_LOWPOLY and
// RS_ROUTE_HIRES. jpeg: the JPEG textures the conversion found.
void RsCharHires_PngStepLine(int route, int on, int jpeg, int failed, wchar_t *out, int cap);

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

// The command line of that test for a file outside the Studio (the Creator
// Kit's START-DEMO-PUPPE.bat): the game program (Rs_FindGameExe, else
// "ctr_native.exe"), then --char <rldchar> --level 0 --driver <templateId> -
// the same switches as Rs_TestCharacter, without --windowed and --log. The
// file need not exist yet. 1 = written.
int Rs_TestCharacterCommand(const wchar_t *rldchar, int templateId, wchar_t *out, int outCap);

#endif
