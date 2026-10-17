// rs_cards.c - the pictures of the three cards "What do you want to build?"
// (rs_charroute.h)

#include "reloadstudio.h"
#include "rs_charroute.h"

HBITMAP Rs_RouteImage(int route, int w, int h)
{
    (void)route;
    (void)w;
    (void)h;
    return NULL;
}

int Rs_TestCharacter(const wchar_t *rldchar, int route, wchar_t *why, int whyCap)
{
    (void)rldchar;
    (void)route;
    if (why && whyCap > 0)
        why[0] = 0;
    return 0;
}
