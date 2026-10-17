// rs_module_test.c - test modules for the module loader of Reload Studio
//
// Built only with the tests (CMakeLists.txt, ctest reloadstudio_modules), once
// per variant (RS_TEST_VARIANT), each as its own DLL. It includes nothing of
// Reload Studio but rs_module.h - the header a module of someone else gets -
// so the test also shows that the header is enough to write a module.
//
//   1 ok            page "ok-test"; verb "ok-ping" writes "ok-test: pong" to the log
//   2 ok2           page "ok-two", needs "ok-test"; verb "ok2-ping"
//   3 crash-create  page "crash-create"; create() writes through a NULL pointer
//   4 crash-auto    page "crash-auto"; verb "crash-now" writes through a NULL pointer
//   5 abi           made for module interface 99
//   6 core          needs Reload Studio 99.0.0
//   7 noentry       has no function ReloadStudioModule
//   8 null          ReloadStudioModule gives NULL
//   9 dupword       page word "track" (a built-in one)
//  10 needs         needs "no-such-module"
//  11 crash-entry   ReloadStudioModule itself writes through a NULL pointer
//  12 badword       page word "Bad Word"

#include "rs_module.h"

#ifndef RS_TEST_VARIANT
#error "RS_TEST_VARIANT (1..12) comes from CMakeLists.txt"
#endif

static const struct RsCoreApi *g_core;
static HWND g_label;

static void Crash(void)
{
    volatile int *p = 0;
    *p = 1;
}

static void Test_Create(HWND page)
{
#if RS_TEST_VARIANT == 3
    Crash();
#endif
    g_label = g_core->label(page, 100, L"A test module.", RS_FONT_BODY);
}

static void Test_Layout(HWND page, int w, int h)
{
    RECT card;
    (void)h;
    g_core->cardClear(page);
    card.left = g_core->px(24);
    card.top = g_core->pageTop();
    card.right = w - g_core->px(24);
    card.bottom = card.top + g_core->px(120);
    g_core->cardAdd(page, &card, L"Test");
    {
        RECT in = g_core->cardInner(&card, 1);
        MoveWindow(g_label, in.left, in.top, in.right - in.left, g_core->px(24), TRUE);
    }
}

static int Test_Automate(HWND page, const wchar_t *verb, const wchar_t *arg)
{
    (void)page;
    (void)arg;
#if RS_TEST_VARIANT == 1
    if (wcscmp(verb, L"ok-ping") == 0) {
        g_core->log(L"ok-test: pong (core %u, %ls)", g_core->coreVersion, g_core->versionText);
        return RS_AUTO_DONE;
    }
#elif RS_TEST_VARIANT == 2
    if (wcscmp(verb, L"ok2-ping") == 0) {
        g_core->log(L"ok-two: pong");
        return RS_AUTO_DONE;
    }
#elif RS_TEST_VARIANT == 4
    if (wcscmp(verb, L"crash-now") == 0) {
        Crash();
        return RS_AUTO_DONE;
    }
#endif
    return RS_AUTO_UNKNOWN;
}

static const struct RsPageDef g_page = {
    L"Test module", L"Test module", L"A module made for the tests of the module loader.",
    Test_Create, Test_Layout, NULL, NULL, NULL, Test_Automate, NULL, 0, 0
};

#if RS_TEST_VARIANT == 2
static const wchar_t *const g_needs[] = { L"ok-test", NULL };
#define TEST_NEEDS g_needs
#elif RS_TEST_VARIANT == 10
static const wchar_t *const g_needs[] = { L"no-such-module", NULL };
#define TEST_NEEDS g_needs
#else
#define TEST_NEEDS NULL
#endif

#if RS_TEST_VARIANT == 1
#define TEST_WORD L"ok-test"
#elif RS_TEST_VARIANT == 2
#define TEST_WORD L"ok-two"
#elif RS_TEST_VARIANT == 3
#define TEST_WORD L"crash-create"
#elif RS_TEST_VARIANT == 4
#define TEST_WORD L"crash-auto"
#elif RS_TEST_VARIANT == 9
#define TEST_WORD L"track"
#elif RS_TEST_VARIANT == 12
#define TEST_WORD L"Bad Word"
#else
#define TEST_WORD L"variant"
#endif

#if RS_TEST_VARIANT == 5
#define TEST_ABI 99
#else
#define TEST_ABI RS_MODULE_ABI
#endif

#if RS_TEST_VARIANT == 6
#define TEST_MIN_CORE RS_CORE_VERSION_OF(99, 0, 0)
#else
#define TEST_MIN_CORE RS_CORE_VERSION_OF(0, 9, 0)
#endif

static const struct RsModuleDef g_module = {
    TEST_ABI, sizeof(struct RsModuleDef), TEST_WORD, NULL, TEST_MIN_CORE, TEST_NEEDS, &g_page
};

#if RS_TEST_VARIANT == 7
__declspec(dllexport) int __cdecl SomethingElse(void)
{
    return (int)g_module.abi + (g_core != NULL);
}
#else
RS_MODULE_EXPORT const struct RsModuleDef *__cdecl ReloadStudioModule(const struct RsCoreApi *core)
{
    g_core = core;
#if RS_TEST_VARIANT == 8
    return NULL;
#else
#if RS_TEST_VARIANT == 11
    Crash();
#endif
    return &g_module;
#endif
}
#endif
