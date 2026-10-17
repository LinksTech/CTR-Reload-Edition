// rs_charhires.c - the presets of the three routes and the plain-English
// messages of the CHRG routes (rs_charroute.h)
//
// NOTHING HERE CHECKS. The findings are those of RsHires_CheckFile
// (rs_hires_model.c), which runs include/rldhires.inc - the rules the game and
// rldpack hires-check run - in this process. This file only says them in the
// author's words: every rule finding keeps its rule ID (HRG-n), gets a plain
// sentence and a "How to fix", and goes to the place of the step it is about
// (RsWizPlace). The rows of the check stay as they are for "Show details".
//
// THE SENTENCE. Most rules carry the specific part in the check's own text
// ("texture 2 ('paint') is JPEG - export textures as PNG"): the line takes
// that text up to its " - " (what is the matter, with the names and numbers),
// starting with a capital, and the fix of the table below. Rules whose text
// says little to an author (the container rules HRG-1..4, HRG-12) take the
// table's own sentence. Several findings of one rule are one line ("... (and
// 3 more)"), the budget (HRG-27) excepted: each limit passed is its own line.
//
// WHAT IS OPTIONAL IS ONE LINE (decision E8 of the round's plan). Clips and
// wheel nodes are things a model may simply not have yet; the check says
// HRG-19 once per missing clip and HRG-20 for the wheels. Here:
//   no clip at all     one note "Your model has no animations yet - it will
//                      stand still. Optional." (place ANIMATIONS)
//   some clips         one note "N of 12 animations present - the others use
//                      their stand-ins. Optional." (the per-clip rows stay
//                      behind "Show details")
//   no wheel node      one note "Your model has no wheels of its own - the game
//                      adds standard wheels. Optional." (place WHEELS)
//   1..3 wheel nodes   one warning (that looks like a naming slip), with the
//                      check's list of what is missing
//
// THE ROUTE'S TARGETS (decision E7). Low-Poly advises 8000 triangles and a
// texture edge of 512: above, one warning each with the fix "Use Hi-Res
// instead, or reduce the model in Blender"; the build stays possible. The
// game's budget (HRG-27) is an error on every CHRG route, as in the game.
// Hi-Res has no target of its own: its preset is the game's budget
// (RLDHIRES_BUDGET_*, through RsHires_Budget of rs_hires_model.c, not written
// twice).
//
// "UPSCALE SMALL TEXTURES" IS A SUGGESTION, NOT A PRESET: off on every route,
// the Studio suggests it only for small textures (RsCharHires_SuggestUpscale,
// see rs_charroute.h, THE UPSCALE).

#include "reloadstudio.h"
#include "rs_charroute.h"
#include "rs_hires_model.h"

#include <stdio.h>
#include <string.h>
#include <wctype.h>

// ---------------------------------------------------------------------------
// The presets

static struct RsRoutePreset s_routes[RS_ROUTES] = {
    { L"vanilla", L"Vanilla", L"Looks like the original game and runs everywhere.", 0, 0, 0, 0 },
    { L"lowpoly", L"Low-Poly", L"A modern, stylised look with clear surfaces.", 1, 8000, 512, 0 },
    { L"hires", L"Hi-Res", L"Full quality with animations.", 1, 0, 0, 0 },   // the game's budget: RsRoute_Preset
};

const struct RsRoutePreset *RsRoute_Preset(int route)
{
    if (route < 0 || route >= RS_ROUTES)
        return NULL;
    if (s_routes[RS_ROUTE_HIRES].triangleTarget == 0) {
        // Hi-Res = the game's own budget (include/rldhires.inc), the same
        // values every time: set once, from the header the game uses.
        unsigned long long triangles = 0, bytes = 0;
        unsigned edge = 0;
        RsHires_Budget(&triangles, &edge, &bytes);
        s_routes[RS_ROUTE_HIRES].textureEdgeTarget = edge;
        s_routes[RS_ROUTE_HIRES].triangleTarget = triangles;
    }
    return &s_routes[route];
}

int RsRoute_FromWord(const wchar_t *word)
{
    int i;
    for (i = 0; word && i < RS_ROUTES; i++)
        if (wcscmp(word, s_routes[i].word) == 0)
            return i;
    return RS_ROUTE_NONE;
}

// ---------------------------------------------------------------------------
// The messages

// How a rule is said: TEXT_ROW = the check's text up to its " - ", TEXT_OWN =
// the table's sentence.
enum { TEXT_ROW = 0, TEXT_OWN };

struct RsHrgSay {
    const wchar_t *rule;
    int place;          // RS_WIZ_PLACE_*
    int text;           // TEXT_ROW / TEXT_OWN
    const wchar_t *own; // TEXT_OWN: the sentence
    const wchar_t *fix; // "How to fix", never empty
};

#define RS_FIX_EXPORT L"Export the model again from Blender (File > Export > glTF 2.0, format glTF Binary .glb)."
#define RS_FIX_CHRG L"Build the character again from its .glb."
#define RS_CHRG_DAMAGED L"The hi-res part of this character file is damaged or from a newer version."

// Every rule of s_rldHiresRules (include/rldhires.inc) that a check can say.
// HRG-19 and HRG-20 are folded into one line each (RsCharHires_Messages).
static const struct RsHrgSay s_say[] = {
    { L"HRG-1", RS_WIZ_PLACE_MODEL, TEXT_OWN, RS_CHRG_DAMAGED, RS_FIX_CHRG },
    { L"HRG-2", RS_WIZ_PLACE_MODEL, TEXT_OWN, RS_CHRG_DAMAGED, RS_FIX_CHRG },
    { L"HRG-3", RS_WIZ_PLACE_MODEL, TEXT_OWN, RS_CHRG_DAMAGED, RS_FIX_CHRG },
    { L"HRG-4", RS_WIZ_PLACE_MODEL, TEXT_OWN, RS_CHRG_DAMAGED, RS_FIX_CHRG },
    { L"HRG-5", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Keep the .glb under 64 MiB (smaller textures help most), or build the character again from its .glb." },
    { L"HRG-6", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, RS_FIX_EXPORT },
    { L"HRG-7", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, RS_FIX_EXPORT },
    { L"HRG-8", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Export again with Compression switched off in Blender's glTF export." },
    { L"HRG-9", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Export one .glb (glTF Binary) so that the textures and all data are inside the file." },
    { L"HRG-10", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Remove loose edges and points in Blender, or add a Triangulate modifier, and export again." },
    { L"HRG-11", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, RS_FIX_EXPORT },
    { L"HRG-12", RS_WIZ_PLACE_MODEL, TEXT_OWN, L"The model has more than one armature.", L"Use one armature for the whole model: join them in Blender (select them, Ctrl+J)." },
    { L"HRG-13", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Use at most 64 bones, and export the armature again." },
    { L"HRG-14", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Parent the mesh to the armature with weights and export with skinning on (glTF export: Animation > Skinning)." },
    { L"HRG-15", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Export with normals on (glTF export: Data > Mesh > Normals)." },
    { L"HRG-16", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Save the textures as PNG (glTF export: Images > PNG) and export again." },
    { L"HRG-17", RS_WIZ_PLACE_SIZE, TEXT_ROW, NULL, L"Scale the texture down to 2048 pixels or less on its longest edge." },
    { L"HRG-18", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Apply the scale in Blender (Ctrl+A > Scale) and keep the driver with the kart within 4 m (a kart is about 2 m long)." },
    { L"HRG-21", RS_WIZ_PLACE_ANIMATIONS, TEXT_ROW, NULL, L"Name the actions idle, steer_left, steer_right, jump, drift_left, drift_right, hit, podium_1, podium_2, podium_3, lose and menu." },
    { L"HRG-22", RS_WIZ_PLACE_ANIMATIONS, TEXT_ROW, NULL, L"Nothing to do; for exact curves set the keys to Linear in Blender." },
    { L"HRG-23", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Nothing to do; Hi-Res drivers move with the armature, not with shape keys." },
    { L"HRG-24", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Nothing to do; delete them in Blender to keep the file small." },
    { L"HRG-25", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Nothing to do; for a sharp edge set the material's Blend Mode to Alpha Clip." },
    { L"HRG-26", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Nothing to do; the game draws the base colour and the normal map." },
    { L"HRG-27", RS_WIZ_PLACE_SIZE, TEXT_ROW, NULL, L"Reduce the model in Blender (Decimate modifier) or make the textures smaller." },
    { L"HRG-28", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Export the meshes of the driver and the kart, not only the armature (glTF export: Include)." },
    { L"HRG-29", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Export the model again from Blender with sparse accessors switched off." },
    { L"HRG-30", RS_WIZ_PLACE_MODEL, TEXT_ROW, NULL, L"Save the texture as an 8-bit RGB or RGBA PNG." },
    { L"HRG-31", RS_WIZ_PLACE_WHEELS, TEXT_ROW, NULL, L"Give each wheel object its own name and its mesh, without child objects." },
    { L"HRG-32", RS_WIZ_PLACE_SIZE, TEXT_ROW, NULL, L"Join materials that share a texture in Blender." },
    { L"HRG-33", RS_WIZ_PLACE_SIZE, TEXT_ROW, NULL, L"Reduce the model in Blender (Decimate modifier)." },
    { L"HRG-34", RS_WIZ_PLACE_SIZE, TEXT_ROW, NULL, L"Join the mesh objects in Blender (select them, Ctrl+J) or use fewer bones." },
    { L"HRG-35", RS_WIZ_PLACE_SIZE, TEXT_ROW, NULL, L"Use fewer different textures: let materials share their images." },
    { L"HRG-36", RS_WIZ_PLACE_MODEL, TEXT_OWN, L"Your vertex colours look like PS1 shading - blended as glTF, the kart will look too dark.",
      L"Switch Vertex color blending on." },
};

#define RS_SAY_COUNT ((int)(sizeof(s_say) / sizeof(s_say[0])))
#define RS_FIX_UNKNOWN L"Open Show details for the full check, change the model in Blender and export again."
#define RS_FIX_CLIPS L"To animate it, add actions named idle, steer_left, steer_right, jump, drift_left, drift_right, hit, podium_1..3, lose and menu in Blender."
#define RS_FIX_SOME_CLIPS L"Add the missing actions in Blender if you like; Show details lists them."
#define RS_FIX_WHEELS L"To use its own wheels, name four objects wheel_fl, wheel_fr, wheel_rl and wheel_rr in Blender."
#define RS_FIX_TARGET L"Use Hi-Res instead, or reduce the model in Blender"

static const struct RsHrgSay *RsCharHires_Say(const wchar_t *rule)
{
    int i;
    for (i = 0; i < RS_SAY_COUNT; i++)
        if (wcscmp(s_say[i].rule, rule) == 0)
            return &s_say[i];
    return NULL;
}

// The check's text up to its " - ", with a capital at the start.
static void RsCharHires_Head(const wchar_t *text, wchar_t *out, int cap)
{
    const wchar_t *dash = wcsstr(text, L" - ");
    int n = dash ? (int)(dash - text) : (int)wcslen(text);
    if (n > cap - 1)
        n = cap - 1;
    wmemcpy(out, text, (size_t)n);
    out[n] = 0;
    if (n > 0)
        out[0] = (wchar_t)towupper(out[0]);
}

// text + tail + "." (when it does not end in one), cut to the field.
static void RsCharHires_Sentence(wchar_t *out, const wchar_t *text, const wchar_t *tail)
{
    size_t n;
    swprintf(out, RS_WIZ_TEXT_MAX, L"%ls%ls", text, tail ? tail : L"");
    n = wcslen(out);
    if (n > 0 && out[n - 1] != L'.' && out[n - 1] != L'!' && out[n - 1] != L'?' && n + 1 < RS_WIZ_TEXT_MAX) {
        out[n] = L'.';
        out[n + 1] = 0;
    }
}

static void RsCharHires_Put(struct RsWizMsg *m, int severity, int place, const wchar_t *rule, const wchar_t *text, const wchar_t *fix)
{
    memset(m, 0, sizeof(*m));
    m->severity = severity;
    m->place = place;
    wcsncpy(m->rule, rule, 7);
    wcsncpy(m->text, text, RS_WIZ_TEXT_MAX - 1);
    wcsncpy(m->fix, fix, RS_WIZ_TEXT_MAX - 1);
}

#define RS_WIZ_LIST_MAX (RS_HIRES_ROWS_MAX + 8)

int RsCharHires_Messages(const struct RsHiresCheck *c, int route, struct RsWizMsg *out, int cap)
{
    static struct RsWizMsg list[RS_WIZ_LIST_MAX];   // the page builds its lines on one thread
    const struct RsRoutePreset *preset = RsRoute_Preset(route);
    int done[RS_HIRES_ROWS_MAX];
    int count = 0, i, j, clipRows = 0, wheelRow = -1;
    wchar_t head[RS_WIZ_TEXT_MAX], line[RS_WIZ_TEXT_MAX];

    if (!c || !out || cap <= 0 || !preset || !preset->chrg)
        return 0;

    if (!c->read) {
        RsCharHires_Head(c->why[0] ? c->why : L"the model cannot be read", head, RS_WIZ_TEXT_MAX);
        RsCharHires_Sentence(line, head, NULL);
        RsCharHires_Put(&list[count++], RS_SEV_ERROR, RS_WIZ_PLACE_MODEL, L"", line,
                        L"Check that the file exists, is not open in another program and is a .glb exported from Blender (glTF Binary).");
    }

    // The rule findings, one line per rule (HRG-27 one per limit); HRG-19 and
    // HRG-20 are counted here and said once below. 64 rows at most leave room
    // for the 4 lines after them.
    memset(done, 0, sizeof(done));
    for (i = 0; c->read && i < c->rowCount; i++) {
        const struct RsHiresRow *r = &c->rows[i];
        const struct RsHrgSay *say;
        int more = 0;
        if (done[i])
            continue;
        if (wcscmp(r->rule, L"HRG-19") == 0) {
            clipRows++;
            continue;
        }
        if (wcscmp(r->rule, L"HRG-20") == 0) {
            if (wheelRow < 0)
                wheelRow = i;
            continue;
        }
        // PS1-like vertex colours: a warning while they are blended as glTF,
        // nothing in PSX blend (they are read right) or off (ignored).
        if (wcscmp(r->rule, L"HRG-36") == 0 && c->vcolMode != RS_HIRES_VCOL_GLTF)
            continue;
        say = RsCharHires_Say(r->rule);
        if (wcscmp(r->rule, L"HRG-27") != 0) {
            for (j = i + 1; j < c->rowCount; j++)
                if (!done[j] && wcscmp(c->rows[j].rule, r->rule) == 0) {
                    done[j] = 1;
                    more++;
                }
        }
        if (say && say->text == TEXT_OWN) {
            RsCharHires_Sentence(line, say->own, NULL);
        } else {
            RsCharHires_Head(r->text, head, RS_WIZ_TEXT_MAX);
            if (more > 0) {
                wchar_t tail[32];
                swprintf(tail, 32, L" (and %d more)", more);
                RsCharHires_Sentence(line, head, tail);
            } else {
                RsCharHires_Sentence(line, head, NULL);
            }
        }
        RsCharHires_Put(&list[count++], wcscmp(r->rule, L"HRG-36") == 0 ? RS_SEV_WARNING : r->severity, say ? say->place : RS_WIZ_PLACE_MODEL,
                        r->rule, line, say ? say->fix : RS_FIX_UNKNOWN);
    }

    // The clips: one line. The check reached them when it said HRG-19, or
    // (its rows full) when it measured the model.
    if (c->read && c->clips < RS_HIRES_CLIPS && (clipRows > 0 || (c->rowsDropped > 0 && c->triangles > 0))) {
        if (c->clips == 0) {
            RsCharHires_Put(&list[count++], RS_SEV_NOTE, RS_WIZ_PLACE_ANIMATIONS, L"HRG-19",
                            L"Your model has no animations yet - it will stand still. Optional.", RS_FIX_CLIPS);
        } else {
            swprintf(line, RS_WIZ_TEXT_MAX, L"%d of %d animations present - the others use their stand-ins. Optional.", c->clips, RS_HIRES_CLIPS);
            RsCharHires_Put(&list[count++], RS_SEV_NOTE, RS_WIZ_PLACE_ANIMATIONS, L"HRG-19", line, RS_FIX_SOME_CLIPS);
        }
    }

    // The wheels: one line.
    if (c->read && c->wheels < 4 && (wheelRow >= 0 || (c->rowsDropped > 0 && c->triangles > 0))) {
        if (c->wheels == 0) {
            RsCharHires_Put(&list[count++], RS_SEV_NOTE, RS_WIZ_PLACE_WHEELS, L"HRG-20",
                            L"Your model has no wheels of its own - the game adds standard wheels. Optional.", RS_FIX_WHEELS);
        } else {
            if (wheelRow >= 0)
                RsCharHires_Head(c->rows[wheelRow].text, head, RS_WIZ_TEXT_MAX);
            else
                swprintf(head, RS_WIZ_TEXT_MAX, L"Only %d of 4 wheel nodes usable", c->wheels);
            RsCharHires_Sentence(line, head, L" - the game adds standard wheels instead");
            RsCharHires_Put(&list[count++], RS_SEV_WARNING, RS_WIZ_PLACE_WHEELS, L"HRG-20", line, RS_FIX_WHEELS);
        }
    }

    // The route's targets (Low-Poly): advice, the build stays possible. Over
    // the game's budget the error HRG-27 says it already.
    if (c->read && preset->triangleTarget && c->triangles > preset->triangleTarget && c->triangles <= c->budgetTriangles) {
        swprintf(line, RS_WIZ_TEXT_MAX, L"The model has %llu triangles - %ls aims for %llu or fewer.", c->triangles, preset->title,
                 preset->triangleTarget);
        RsCharHires_Put(&list[count++], RS_SEV_WARNING, RS_WIZ_PLACE_SIZE, L"", line, RS_FIX_TARGET);
    }
    if (c->read && preset->textureEdgeTarget && c->textureEdge > preset->textureEdgeTarget && c->textureEdge <= c->budgetEdge) {
        swprintf(line, RS_WIZ_TEXT_MAX, L"The largest texture is %u pixels across - %ls aims for %u or less.", c->textureEdge, preset->title,
                 preset->textureEdgeTarget);
        RsCharHires_Put(&list[count++], RS_SEV_WARNING, RS_WIZ_PLACE_SIZE, L"", line, RS_FIX_TARGET);
    }

    // Most severe first; the order of the check within one severity.
    for (i = 1; i < count; i++) {
        struct RsWizMsg m = list[i];
        for (j = i; j > 0 && list[j - 1].severity < m.severity; j--)
            list[j] = list[j - 1];
        list[j] = m;
    }
    if (count > cap)
        count = cap;
    memcpy(out, list, (size_t)count * sizeof(*out));
    return count;
}

// ---------------------------------------------------------------------------
// The vertex colours (rs_charroute.h, THE VERTEX COLOURS)

int RsCharHires_SuggestColours(const struct RsHiresCheck *c, int route, int *why)
{
    int w = RS_VCOL_WHY_NONE, mode = -1;
    if (c && c->read && c->vcolPresent) {
        if (c->vcolPs1) {
            w = RS_VCOL_WHY_PS1;
            mode = RS_HIRES_VCOL_PSX;
        } else if (c->vcolMedian >= 0 && c->rowsDropped == 0 && c->vcolChecked == RS_HIRES_VCOL_GLTF) {
            // Measured on textured faces, and the check (in glTF, where it says
            // HRG-36) had room to say it: not PS1-like.
            w = RS_VCOL_WHY_WHITE;
            mode = RS_HIRES_VCOL_GLTF;
        } else {
            w = RS_VCOL_WHY_ROUTE;
            mode = route == RS_ROUTE_LOWPOLY ? RS_HIRES_VCOL_PSX : RS_HIRES_VCOL_GLTF;
        }
    }
    if (why)
        *why = w;
    return mode;
}

void RsCharHires_ColourLine(int mode, int suggested, int why, int route, wchar_t *out, int cap)
{
    static const wchar_t *const what[RS_HIRES_VCOL_MODES] = {
        L"glTF blending: white shows the texture unchanged",
        L"Blending is on: grey 128 shows the texture unchanged, as on the PS1",
        L"Blending is off: the vertex colours are ignored",
    };
    static const wchar_t *const word[RS_HIRES_VCOL_MODES] = { L"glTF (Advanced)", L"on", L"off" };
    if (!out || cap <= 0)
        return;
    out[0] = 0;
    if (mode < 0 || mode >= RS_HIRES_VCOL_MODES || why == RS_VCOL_WHY_NONE)
        return;
    if (mode != suggested && suggested >= 0 && suggested < RS_HIRES_VCOL_MODES) {
        swprintf(out, (size_t)cap, L"%ls. Your choice - suggested: %ls.", what[mode], word[suggested]);
        return;
    }
    switch (why) {
    case RS_VCOL_WHY_PS1:
        swprintf(out, (size_t)cap, L"Your vertex colours look like PS1 shading - blending is on.");
        break;
    case RS_VCOL_WHY_WHITE:
        swprintf(out, (size_t)cap, L"Your vertex colours use white as neutral - they are blended as glTF (Advanced).");
        break;
    default:
        swprintf(out, (size_t)cap, mode == RS_HIRES_VCOL_PSX ? L"Your model has vertex colours - blending is on, as usual for %ls."
                                                             : L"Your model has vertex colours - they are blended as glTF (white is neutral), as usual for %ls.",
                 route == RS_ROUTE_LOWPOLY ? L"Low-Poly" : L"Hi-Res");
        break;
    }
}

// ---------------------------------------------------------------------------
// The upscale (rs_charroute.h, THE UPSCALE)

int RsCharHires_SuggestUpscale(const struct RsHiresCheck *c, int route, unsigned *largest)
{
    int on = 0;
    if (largest)
        *largest = c ? c->baseEdgeFile : 0u;
    // Every base colour texture under 64 px on its larger edge, measured in
    // the file before the round-up; a texture of unknown size (not an
    // embedded PNG) is not known to be small, so nothing is suggested.
    if (c && c->read && (route == RS_ROUTE_LOWPOLY || route == RS_ROUTE_HIRES))
        on = c->baseTextures > 0 && c->baseUnsized == 0 && c->baseEdgeFile > 0u && c->baseEdgeFile < RS_UPSCALE_SUGGEST_EDGE;
    return on;
}

void RsCharHires_UpscaleLine(int on, int suggested, unsigned largest, wchar_t *out, int cap)
{
    if (!out || cap <= 0)
        return;
    out[0] = 0;
    if (on && suggested)
        swprintf(out, (size_t)cap, L"Your textures are small (largest %u px) - Upscale small textures is on.", largest);
    else if (suggested)
        swprintf(out, (size_t)cap, L"Upscale small textures is off. Your choice - suggested: on (your textures are small, largest %u px).", largest);
    else if (on)
        swprintf(out, (size_t)cap, L"Upscale small textures is on. Your choice - suggested: off (only for textures under %u px).",
                 RS_UPSCALE_SUGGEST_EDGE);
}

void RsCharHires_UpscaleStepLine(int route, int on, int suggested, unsigned largest, wchar_t *out, int cap)
{
    if (!out || cap <= 0)
        return;
    out[0] = 0;
    if (route != RS_ROUTE_LOWPOLY && route != RS_ROUTE_HIRES)
        return;
    if (on && suggested)
        swprintf(out, (size_t)cap, L"Your textures are small (largest %u px) - they will be upscaled.", largest);
    else if (suggested)
        swprintf(out, (size_t)cap, L"Your textures will stay as they are (your choice; suggested: upscale, largest %u px).", largest);
    else if (on)
        swprintf(out, (size_t)cap, L"Your textures will be upscaled (your choice).");
}

// ---------------------------------------------------------------------------
// The classic fallback

int RsCharHires_FallbackArgs(int route, const wchar_t **args)
{
    int n = 0;
    if (route != RS_ROUTE_LOWPOLY && route != RS_ROUTE_HIRES)
        return 0;
    // --reduce auto: the classic model always fits the game's draw memory for a
    // driver, whatever the page's own switch said (the later switch wins).
    args[n++] = L"--reduce";
    args[n++] = L"auto";
    // --rig bind-pose: a model with an armature (as every animated hi-res
    // driver has) stands in its bind pose in the classic model; without it
    // make-char refuses a skin (model-rig). tools/rldpack_gltf.inc, A RIG.
    // A model with its own four wheel nodes: the page adds --wheels off after
    // these, so that the game's kart wheels do not stand beside them.
    args[n++] = L"--rig";
    args[n++] = L"bind-pose";
    return n;
}
