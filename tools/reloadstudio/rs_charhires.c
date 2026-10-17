// rs_charhires.c - the presets of the three routes and the plain-English
// messages of the CHRG routes (rs_charroute.h)

#include "reloadstudio.h"
#include "rs_charroute.h"
#include "rs_hires_model.h"

#include <string.h>

static const struct RsRoutePreset s_routes[RS_ROUTES] = {
    { L"vanilla", L"Vanilla", L"Looks like the original game and runs everywhere.", 0, 0, 0, 0 },
    { L"lowpoly", L"Low-Poly", L"A modern, stylised look with clear surfaces.", 1, 8000, 512, 1 },
    { L"hires", L"Hi-Res", L"Full quality with animations.", 1, 64000, 2048, 0 },
};

const struct RsRoutePreset *RsRoute_Preset(int route)
{
    return (route >= 0 && route < RS_ROUTES) ? &s_routes[route] : NULL;
}

int RsRoute_FromWord(const wchar_t *word)
{
    int i;
    for (i = 0; word && i < RS_ROUTES; i++)
        if (wcscmp(word, s_routes[i].word) == 0)
            return i;
    return RS_ROUTE_NONE;
}

int RsCharHires_Messages(const struct RsHiresCheck *c, int route, struct RsWizMsg *out, int cap)
{
    (void)c;
    (void)route;
    (void)out;
    (void)cap;
    return 0;
}

int RsCharHires_FallbackArgs(int route, const wchar_t **args)
{
    (void)route;
    (void)args;
    return 0;
}
