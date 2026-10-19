// rs_expansions.c - the entry "Expansions" of the sidebar
//
// A built-in module of its own (word "expansions", rs_module.c), so that the
// function it stands for can move in later without changing the sidebar, the
// page IDs or the settings: its entry is the bottom group of the sidebar,
// right above "Light mode" / "Dark mode", in the look of the other entries,
// and its tooltip says what is coming (g_rsExpansionsTip). Today its page
// holds one line, "Coming soon" - no button, no switch, nothing to set, and
// no automation verbs.

#include "reloadstudio.h"

#define EXP_ID_SOON 100

// The tooltip of the entry (rs_module.c, g_builtinPlace): shown on hover in
// the wide sidebar too.
const wchar_t g_rsExpansionsTip[] = L"CNK \x2013 coming soon";

static HWND g_expSoon;

static void Exp_Create(HWND page)
{
    g_expSoon = Rs_Label(page, EXP_ID_SOON, L"Coming soon", RS_FONT_BODY);
    Rs_SetTextColor(g_expSoon, RS_COL_MUTED);
}

static void Exp_Layout(HWND page, int w, int h)
{
    // The page has no subtitle: the line starts right below its title.
    int top = Rs_PageHeadBottom(page, w) + Rs_Px(16), left = Rs_Px(32);
    (void)h;
    Rs_CardClear(page);
    MoveWindow(g_expSoon, left, top, w - left - Rs_Px(32), Rs_Px(24), TRUE);
}

const struct RsPageDef g_rsExpansionsPage = {
    L"Expansions",
    L"Expansions",
    L"",
    Exp_Create,
    Exp_Layout,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    400,                // one line: no scroll bars in a small window
    200
};
